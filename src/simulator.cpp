/*

Copyright (c) 2015, Arvid Norberg
All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "simulator/simulator.hpp"

#include <boost/bind.hpp>
#include <boost/system/error_code.hpp>

typedef sim::chrono::high_resolution_clock::time_point time_point;
typedef sim::chrono::high_resolution_clock::duration duration;

namespace sim
{
	namespace asio {

	namespace ip {

	udp::socket::socket(io_service& ios)
		: boost::asio::ip::udp::socket(ios.get_internal_service())
		, m_io_service(ios)
	{}

	udp::resolver::resolver(io_service& ios)
		: boost::asio::ip::udp::resolver(ios.get_internal_service())
	{}

	// acceptor

	tcp::acceptor::acceptor(io_service& ios)
		: socket(ios)
		, m_rtt_timer(ios)
		, m_queue_size_limit(-1)
	{}

	tcp::acceptor::~acceptor()
	{
		boost::system::error_code ec;
		close(ec);
	}

	void tcp::acceptor::listen(int qs, boost::system::error_code& ec)
	{
		if (!m_open)
		{
			ec = error::bad_descriptor;
			return;
		}
		if (m_bound_to == ip::tcp::endpoint())
		{
			ec = error::invalid_argument;
			return;
		}

		m_queue_size_limit = qs;
		ec.clear();
	}

	boost::system::error_code tcp::acceptor::close(boost::system::error_code& ec)
	{
		m_queue_size_limit = -1;
		cancel(ec);
		return socket::close(ec);
	}

	void tcp::acceptor::close()
	{
		boost::system::error_code ec;
		close(ec);
		if (ec) throw boost::system::system_error(ec);
	}

	boost::system::error_code tcp::acceptor::cancel(boost::system::error_code& ec)
	{
		if (m_accept_handler)
		{
			m_io_service.post(boost::bind(m_accept_handler
				, boost::system::error_code(error::operation_aborted)));
			m_accept_handler.clear();
		}

		m_rtt_timer.cancel();
		ec.clear();
		return ec;
	}

	void tcp::acceptor::cancel()
	{
		boost::system::error_code ec;
		cancel(ec);
		if (ec) throw boost::system::system_error(ec);
	}

	void tcp::acceptor::async_accept(ip::tcp::socket& peer
		, boost::function<void(boost::system::error_code const&)> h)
	{
		// TODO: assert that the io_service we use is the same as the one peer use
		if (peer.is_open())
		{
			boost::system::error_code ec;
			peer.close(ec);
		}

		if (m_accept_handler)
		{
			m_io_service.post(boost::bind(m_accept_handler
				, boost::system::error_code(error::operation_aborted)));
			m_accept_handler.clear();
		}
		m_accept_handler = h;
		m_accept_into = &peer;
		m_remote_endpoint = NULL;

		check_accept_queue();
	}

	void tcp::acceptor::async_accept(ip::tcp::socket& peer
		, ip::tcp::endpoint& peer_endpoint
		, boost::function<void(boost::system::error_code const&)> h)
	{
		if (peer.is_open())
		{
			boost::system::error_code ec;
			peer.close(ec);
		}

		if (m_accept_handler)
		{
			m_io_service.post(boost::bind(m_accept_handler
				, boost::system::error_code(error::operation_aborted)));
			m_accept_handler.clear();
		}
		m_accept_handler = h;
		m_accept_into = &peer;
		m_remote_endpoint = &peer_endpoint;

		check_accept_queue();
	}

	void tcp::acceptor::do_check_accept_queue(boost::system::error_code const& ec)
	{
		if (ec) return;
		check_accept_queue();
	}

	void tcp::acceptor::check_accept_queue()
	{
		if (!is_open())
		{
			// if the acceptor socket is closed. Any potential socket in the queue
			// should be closed too.
			for (incoming_conns_t::iterator i = m_incoming_queue.begin()
				, end(m_incoming_queue.end()); i != end; ++i)
			{
				aux::channel *c = i->get();
				c->sockets[0]->internal_connect_complete(boost::system::error_code(
					error::connection_reset));
			}
			m_incoming_queue.clear();

			if (m_accept_handler)
			{
				m_io_service.post(boost::bind(m_accept_handler
					, boost::system::error_code(error::operation_aborted)));
				m_accept_handler.clear();
				m_accept_into = NULL;
				m_remote_endpoint = NULL;
			}
		}

		// if the user is not waiting for an incoming connection, there's no point
		// in checking the queue
		if (!m_accept_handler) return;

		if (m_incoming_queue.empty()) return;

		boost::shared_ptr<aux::channel> c = m_incoming_queue[0];
		time_point now = chrono::high_resolution_clock::now();

		// delay[0] is the delay of our SYN+ACK response.
		// delay[1] is the delay of the initial SYN and the 3rd handshake
		// packet.
		time_point completes = c->initiated + c->delay[0] + c->delay[1] * 2 ;
		if (completes > now)
		{
			m_rtt_timer.expires_at(completes);
			m_rtt_timer.async_wait(boost::bind(&tcp::acceptor::do_check_accept_queue
				, this, _1));
			return;
		}

		// this was initiated at least one 3-way handshake ago.
		// we can pick it up and consider it connected
		boost::system::error_code ec;
		if (m_remote_endpoint)
		{
			*m_remote_endpoint = c->sockets[0]->local_endpoint(ec);
		}

		if (!ec) {
			m_accept_into->internal_connect(m_bound_to, c, ec);
		}

		if (ec)
		{
			// notify the incoming connection that it failed
			c->sockets[0]->internal_connect_complete(boost::system::error_code(
				error::connection_reset));
		}

		c->sockets[1] = m_accept_into;

		m_incoming_queue.erase(m_incoming_queue.begin());
		assert(m_accept_handler);
		m_io_service.post(boost::bind(m_accept_handler, ec));
		m_accept_handler.clear();
		m_accept_into = NULL;
		m_remote_endpoint = NULL;

		// notify the incoming connection that it completed
		c->sockets[0]->internal_connect_complete(ec);
	}

	bool tcp::acceptor::internal_is_listening()
	{
		return m_queue_size_limit > 0;
	}

	void tcp::acceptor::internal_accept_queue(boost::shared_ptr<aux::channel> c
		, boost::system::error_code& ec)
	{
		if (m_incoming_queue.size() >= m_queue_size_limit)
		{
			ec = boost::system::error_code(asio::error::connection_refused);
			return;
		}

		m_incoming_queue.push_back(c);
		check_accept_queue();
	}

	tcp::resolver::resolver(io_service& ios)
		: boost::asio::ip::tcp::resolver(ios.get_internal_service())
	{}

	} // ip

	} // asio

	namespace aux {

		int channel::remote_idx(asio::ip::tcp::socket const* self) const
		{
			if (sockets[0] == self) return 1;
			if (sockets[1] == self) return 0;
			assert(false && "invalid socket");
		}

		int channel::self_idx(asio::ip::tcp::socket const* self) const
		{
			if (sockets[0] == self) return 0;
			if (sockets[1] == self) return 1;
			assert(false && "invalid socket");
		}

	} // aux
} // sim

