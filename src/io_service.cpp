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

#include <boost/make_shared.hpp>
#include <boost/system/error_code.hpp>

namespace sim { namespace asio {

	// TODO: an io_service could be assumed to be a network node. The main
	// message loop would have to be run in a higher level object (like a
	// simulation). Each io_service could be constructed with its IP address and
	// possibly some network parameters (like in- and out latency)
	io_service::io_service(sim::simulation& sim, asio::ip::address const& ip)
		: m_sim(sim)
		, m_ip(ip)
	{}

/*
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
*/

	void io_service::dispatch(boost::function<void()> handler)
	{ m_sim.get_internal_service().dispatch(handler); }

	void io_service::post(boost::function<void()> handler)
	{ m_sim.get_internal_service().post(handler); }

	// private interface

	void io_service::add_timer(high_resolution_timer* t)
	{
		m_sim.add_timer(t);
	}

	void io_service::remove_timer(high_resolution_timer* t)
	{
		m_sim.remove_timer(t);
	}

	boost::asio::io_service& io_service::get_internal_service()
	{ return m_sim.get_internal_service(); }

	ip::tcp::endpoint io_service::bind_socket(ip::tcp::socket* socket
		, ip::tcp::endpoint ep, boost::system::error_code& ec)
	{
		if (ep.address() == ip::address())
		{
			ep.address(m_ip);
		}
		else if (ep.address() != m_ip)
		{
			// you can only bind to the IP assigned to this node.
			// TODO: support loopback
			ec.assign(boost::system::errc::address_not_available
				, boost::system::generic_category());
			return ip::tcp::endpoint();
		}

		return m_sim.bind_socket(socket, ep, ec);
	}

	void io_service::unbind_socket(ip::tcp::socket* socket
		, ip::tcp::endpoint ep)
	{
		m_sim.unbind_socket(socket, ep);
	}

	boost::shared_ptr<aux::channel> io_service::internal_connect(ip::tcp::socket* s
		, ip::tcp::endpoint const& target, boost::system::error_code& ec)
	{
		return m_sim.internal_connect(s, target, ec);
	}

} // asio
} // sim

