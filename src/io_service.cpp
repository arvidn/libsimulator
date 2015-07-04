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
#include <boost/make_shared.hpp>
#include <boost/system/error_code.hpp>
#include <boost/tuple/tuple.hpp>

namespace sim { namespace asio {

	// TODO: an io_service could be assumed to be a network node. The main
	// message loop would have to be run in a higher level object (like a
	// simulation). Each io_service could be constructed with its IP address and
	// possibly some network parameters (like in- and out latency)
	io_service::io_service()
		: m_stopped(false)
	{}

	std::size_t io_service::run()
	{
		boost::system::error_code ec;
		return run(ec);
	}

	std::size_t io_service::run(boost::system::error_code& ec)
	{
		std::size_t ret = 0;
		std::size_t last_executed = 0;
		do {

			m_service.reset();
			last_executed = m_service.poll(ec);
			ret += last_executed;

			chrono::high_resolution_clock::time_point now
				= chrono::high_resolution_clock::now();

			if (!m_timer_queue.empty()) {
				high_resolution_timer* next_timer = *m_timer_queue.begin();
				chrono::high_resolution_clock::fast_forward(next_timer->expires_at() - now);

				now = chrono::high_resolution_clock::now();

				while (!m_timer_queue.empty()
					&& (*m_timer_queue.begin())->expires_at() <= now) {

					next_timer = *m_timer_queue.begin();
					m_timer_queue.erase(m_timer_queue.begin());
					next_timer->fire(boost::system::error_code());
					++last_executed;
					++ret;
				}
			}

		} while (last_executed > 0 && !m_stopped);
		return ret;
	}

	std::size_t io_service::poll()
	{
		return m_service.poll();
	}

	std::size_t io_service::poll(boost::system::error_code& ec)
	{
		return m_service.poll(ec);
	}

	std::size_t io_service::poll_one()
	{
		return m_service.poll_one();
	}

	std::size_t io_service::poll_one(boost::system::error_code& ec)
	{
		return m_service.poll_one(ec);
	}

	void io_service::stop() { m_stopped = true; }
	bool io_service::stopped() const { return m_stopped; }
	void io_service::reset() { m_stopped = false; }

	void io_service::dispatch(boost::function<void()> handler)
	{ m_service.dispatch(handler); }

	void io_service::post(boost::function<void()> handler)
	{ m_service.post(handler); }

	// private interface

	void io_service::add_timer(high_resolution_timer* t)
	{
		m_timer_queue.insert(t);
	}

	void io_service::remove_timer(high_resolution_timer* t)
	{
		assert(!m_timer_queue.empty());
		timer_queue_t::iterator begin;
		timer_queue_t::iterator end;
		boost::tuples::tie(begin, end) = m_timer_queue.equal_range(t);
		assert(begin != end);
		begin = std::find(begin, end, t);
		assert(begin != end);
		m_timer_queue.erase(begin);
	}

	ip::tcp::endpoint io_service::bind_socket(ip::tcp::socket* socket
		, ip::tcp::endpoint ep, boost::system::error_code& ec)
	{
		if (ep.address() == boost::asio::ip::address())
		{
			// if the socket is being bound to 0.0.0.0, it means it should be bound
			// to all available interfaces on the computer. It's not obvious what
			// to do with this, because presumably different objects represent
			// different logical computers and should bind to different actual IPs.
			// TODO: make it a customization point to pick the actual interface(s).
			assert(false && "binding to INADDR_ANY not supported");
			ec = boost::asio::error::operation_not_supported;
			return ip::tcp::endpoint();
		}

		if (ep.port() < 1024 && ep.port() > 0)
		{
			// emulate process not running as root
			ec = boost::asio::error::no_permission;
			return ip::tcp::endpoint();
		}

		if (ep.port() == 0)
		{
			// if the socket is being bound to port 0, it means the system picks a
			// free port.
			ep.port(2000);
			listen_socket_iter_t i = m_listen_sockets.lower_bound(ep);
			while (i != m_listen_sockets.end() && i->first == ep)
			{
				ep.port(ep.port() + 1);
				if (ep.port() > 65530)
				{
					ec = boost::asio::error::address_in_use;
					return ip::tcp::endpoint();
				}
				i = m_listen_sockets.lower_bound(ep);
			}
		}

		listen_socket_iter_t i = m_listen_sockets.lower_bound(ep);
		if (i != m_listen_sockets.end() && i->first == ep)
		{
			ec = boost::asio::error::address_in_use;
			return ip::tcp::endpoint();
		}

		m_listen_sockets.insert(i, std::make_pair(ep, socket));
		ec.clear();
		return ep;
	}

	void io_service::unbind_socket(ip::tcp::socket* socket
		, ip::tcp::endpoint ep)
	{
		listen_socket_iter_t i = m_listen_sockets.find(ep);
		assert(i != m_listen_sockets.end());
		m_listen_sockets.erase(i);
	}

	boost::shared_ptr<aux::channel> io_service::internal_connect(ip::tcp::socket* s
		, ip::tcp::endpoint const& target, boost::system::error_code& ec)
	{
		// find remote socket
		listen_sockets_t::iterator i = m_listen_sockets.find(target);
		if (i == m_listen_sockets.end())
		{
			ec = boost::system::error_code(error::connection_refused);
			return boost::shared_ptr<aux::channel>();
		}

		// make sure it's a listening socket
		ip::tcp::socket* remote = i->second;
		if (!remote->internal_is_listening())
		{
			ec = boost::system::error_code(error::connection_refused);
			return boost::shared_ptr<aux::channel>();
		}

		// create a channel
		boost::shared_ptr<aux::channel> c = boost::make_shared<aux::channel>();
		c->sockets[0] = s;

		// TODO: ask policy object about delays for this pair
		c->delay[0] = chrono::milliseconds(50);
		c->delay[1] = chrono::milliseconds(50);
		c->incoming_bandwidth[0] = 1024 * 1024;
		c->incoming_bandwidth[1] = 1024 * 1024;

		c->initiated = chrono::high_resolution_clock::now();

		// add channel to listen queue (fail if queue is full)
		remote->internal_accept_queue(c, ec);
		if (ec) return boost::shared_ptr<aux::channel>();

		return c;
	}

} // asio
} // sim

