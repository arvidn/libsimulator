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
#include "simulator/packet.hpp"

#include <functional>

#define __STDC_FORMAT_MACROS 1
#include <cinttypes>

typedef sim::chrono::high_resolution_clock::time_point time_point;
typedef sim::chrono::high_resolution_clock::duration duration;

namespace sim {
namespace asio {
namespace ip {

	tcp::acceptor::acceptor(io_service& ios)
		: socket(ios)
		, m_queue_size_limit(-1)
	{}

	tcp::acceptor::~acceptor()
	{
		boost::system::error_code ec;
		close(ec);
	}

	void tcp::acceptor::listen(int qs)
	{
		boost::system::error_code ec;
		listen(qs, ec);
		if (ec) throw boost::system::system_error(ec);
	}

	void tcp::acceptor::listen(int qs, boost::system::error_code& ec)
	{
		if (qs == -1) qs = 20;

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
		if (m_accept_handler)
		{
			m_io_service.post(std::bind(std::move(m_accept_handler)
				, boost::system::error_code(error::operation_aborted)));
			m_accept_handler = nullptr;
		}
	}

	boost::system::error_code tcp::acceptor::cancel(boost::system::error_code& ec)
	{
		ec.clear();
		if (m_accept_handler)
		{
			try
			{
				m_io_service.post(std::bind(std::move(m_accept_handler)
					, boost::system::error_code(error::operation_aborted)));
				m_accept_handler = nullptr;
			}
			catch (std::bad_alloc const&)
			{
				ec = error::no_memory;
			}
			catch (std::exception const&)
			{
				ec = error::no_memory;
			}
		}

		return ec;
	}

	void tcp::acceptor::cancel()
	{
		boost::system::error_code ec;
		cancel(ec);
		if (ec) throw boost::system::system_error(ec);
	}

	void tcp::acceptor::async_accept(ip::tcp::socket& peer
		, aux::function<void(boost::system::error_code const&)> h)
	{
		// TODO: assert that the io_service we use is the same as the one peer use
		if (peer.is_open())
		{
			boost::system::error_code ec;
			peer.close(ec);
		}

		if (m_accept_handler)
		{
			m_io_service.post(std::bind(std::move(m_accept_handler)
				, boost::system::error_code(error::operation_aborted)));
			m_accept_handler = nullptr;
		}
		m_accept_handler = std::move(h);
		m_accept_into = &peer;
		m_remote_endpoint = nullptr;

		check_accept_queue();
	}

	void tcp::acceptor::async_accept(ip::tcp::socket& peer
		, ip::tcp::endpoint& peer_endpoint
		, aux::function<void(boost::system::error_code const&)> h)
	{
		if (peer.is_open())
		{
			boost::system::error_code ec;
			peer.close(ec);
		}

		if (m_accept_handler)
		{
			m_io_service.post(std::bind(std::move(m_accept_handler)
				, boost::system::error_code(error::operation_aborted)));
			m_accept_handler = nullptr;
		}
		m_accept_handler = std::move(h);
		m_accept_into = &peer;
		m_remote_endpoint = &peer_endpoint;

		check_accept_queue();
	}

	void tcp::acceptor::do_check_accept_queue(boost::system::error_code const& ec)
	{
		if (ec) return;
		check_accept_queue();
	}

	void tcp::acceptor::incoming_packet(aux::packet p)
	{
		switch (p.type)
		{
			case aux::packet::type_t::syn:
				m_incoming_queue.push_back(p.channel);
				check_accept_queue();
				return;
			case aux::packet::type_t::error:
				assert(false); // something is not wired up correctly
				if (m_accept_handler)
				{
					m_io_service.post(std::bind(std::move(m_accept_handler)
						, boost::system::error_code(error::operation_aborted)));
					m_accept_handler = nullptr;
					m_accept_into = nullptr;
					m_remote_endpoint = nullptr;
				}
				return;
			default:
				// if this happens, it implies that an incoming connection sent
				// payload before receiving a syn_ack. Alternatively that the
				// acceptor sent the syn_ack but still left the last-hop in the
				// incoming route to point to this socket, instead of the
				// accepted-into socket
				assert(false);
				return;
		}
	}

	void tcp::acceptor::check_accept_queue()
	{
		if (!is_open())
		{
			// if the acceptor socket is closed. Any potential socket in the queue
			// should be closed too.
			for (auto const& incoming : m_incoming_queue)
			{
				aux::packet p;
				*p.from = asio::ip::udp::endpoint(
					m_bound_to.address(), m_bound_to.port());
				p.type = aux::packet::type_t::error;
				p.ec = boost::system::error_code(error::connection_reset);
				p.overhead = 28;
				p.hops = incoming->hops[0];

				forward_packet(std::move(p));
			}
			m_incoming_queue.clear();

			if (m_accept_handler)
			{
				m_io_service.post(std::bind(std::move(m_accept_handler)
					, boost::system::error_code(error::operation_aborted)));
				m_accept_handler = nullptr;
				m_accept_into = nullptr;
				m_remote_endpoint = nullptr;
			}
		}

		// if the user is not waiting for an incoming connection, there's no point
		// in checking the queue
		if (!m_accept_handler) return;

		if (m_incoming_queue.empty()) return;

		std::shared_ptr<aux::channel> c = std::move(m_incoming_queue.front());
		m_incoming_queue.erase(m_incoming_queue.begin());

		// this was initiated at least one 3-way handshake ago.
		// we can pick it up and consider it connected
		if (m_remote_endpoint) *m_remote_endpoint = c->ep[0];

		boost::system::error_code ec;
		// if the acceptor socket is closed. Any potential socket in the queue
		m_accept_into->internal_connect(m_bound_to, c, ec);

		// notify the other end
		aux::packet p;
		*p.from = asio::ip::udp::endpoint(
			m_bound_to.address(), m_bound_to.port());
		if (ec)
		{
			c->hops[1] = route();
			p.type = aux::packet::type_t::error;
			p.ec = ec;
		}
		else
		{
		// TODO: extend pcap logging to include SYN+ACK packets
			p.type = aux::packet::type_t::syn_ack;
		}
		p.channel = c;
		p.overhead = 28;
		p.hops = p.channel->hops[0];

		forward_packet(std::move(p));

		assert(m_accept_handler);
		m_io_service.post(std::bind(std::move(m_accept_handler), ec));
		m_accept_handler = nullptr;
		m_accept_into = nullptr;
		m_remote_endpoint = nullptr;
	}

	bool tcp::acceptor::internal_is_listening()
	{
		return m_queue_size_limit > 0;
	}
}
}
}

