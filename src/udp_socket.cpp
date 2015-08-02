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

#include <functional>
#include <boost/system/error_code.hpp>
#include <boost/function.hpp>

typedef sim::chrono::high_resolution_clock::time_point time_point;
typedef sim::chrono::high_resolution_clock::duration duration;

namespace sim {
namespace asio {
namespace ip {

	udp::socket::socket(io_service& ios)
		: socket_base(ios)
		, m_next_send(chrono::high_resolution_clock::now())
		, m_recv_sender(NULL)
		, m_recv_timer(ios)
		, m_send_timer(ios)
		, m_recv_null_buffers(0)
		, m_queue_size(0)
		, m_is_v4(true)
	{}

	udp::socket::~socket()
	{
		boost::system::error_code ec;
		close(ec);
	}

	udp::endpoint udp::socket::local_endpoint(boost::system::error_code& ec)
		const
	{
		if (!m_open)
		{
			ec = error::bad_descriptor;
			return udp::endpoint();
		}

		return m_bound_to;
	}

	udp::endpoint udp::socket::local_endpoint() const
	{
		boost::system::error_code ec;
		udp::endpoint ret = local_endpoint(ec);
		if (ec) throw boost::system::system_error(ec);
		return ret;
	}

	boost::system::error_code udp::socket::bind(ip::udp::endpoint const& ep
		, boost::system::error_code& ec)
	{
		if (!m_open)
		{
			ec = error::bad_descriptor;
			return ec;
		}

		if (ep.address().is_v4() != m_is_v4)
		{
			ec = error::address_family_not_supported;
			return ec;
		}

		ip::udp::endpoint addr = m_io_service.bind_udp_socket(this, ep, ec);
		if (ec) return ec;
		m_bound_to = addr;
		return ec;
	}

	void udp::socket::bind(ip::udp::endpoint const& ep)
	{
		boost::system::error_code ec;
		bind(ep, ec);
		if (ec) throw boost::system::system_error(ec);
	}

	boost::system::error_code udp::socket::open(udp protocol
		, boost::system::error_code& ec)
	{
		// TODO: what if it's already open?
		close(ec);
		m_open = true;
		m_is_v4 = (protocol == ip::udp::v4());
		ec.clear();
		m_forwarder = std::make_shared<aux::sink_forwarder>(this);
		return ec;
	}

	void udp::socket::open(udp protocol)
	{
		boost::system::error_code ec;
		open(protocol, ec);
		if (ec) throw boost::system::system_error(ec);
	}

	boost::system::error_code udp::socket::close()
	{
		boost::system::error_code ec;
		return close(ec);
	}

	boost::system::error_code udp::socket::close(boost::system::error_code& ec)
	{
		if (m_bound_to != ip::udp::endpoint())
		{
			m_io_service.unbind_udp_socket(this, m_bound_to);
			m_bound_to = ip::udp::endpoint();
		}
		m_open = false;

		// prevent any more packets from being delivered to this socket
		if (m_forwarder)
		{
			m_forwarder->clear();
			m_forwarder.reset();
		}

		cancel(ec);

		ec.clear();
		return ec;
	}

	boost::system::error_code udp::socket::cancel(boost::system::error_code& ec)
	{
		// cancel outstanding async operations
		if (m_recv_handler) abort_recv_handler();
		if (m_send_handler) abort_send_handler();
		m_recv_timer.cancel(ec);
		m_send_timer.cancel(ec);
		return ec;
	}

	void udp::socket::cancel()
	{
		boost::system::error_code ec;
		cancel(ec);
		if (ec) throw boost::system::system_error(ec);
	}

	void udp::socket::abort_send_handler()
	{
		m_io_service.post(std::bind(m_send_handler
			, boost::system::error_code(error::operation_aborted), 0));
		m_send_timer.cancel();
		m_send_handler = 0;
//		m_send_buffer.clear();
	}

	void udp::socket::abort_recv_handler()
	{
		m_io_service.post(std::bind(m_recv_handler
			, boost::system::error_code(error::operation_aborted), 0));
		m_recv_timer.cancel();
		m_recv_handler = 0;
		m_recv_buffer.clear();
	}

	void udp::socket::async_send(const asio::null_buffers& bufs
		, boost::function<void(boost::system::error_code const&, std::size_t)> const& handler)
	{
		if (m_send_handler) abort_send_handler();

		// TODO: make the send buffer size configurable
		time_point now = chrono::high_resolution_clock::now();
		if (m_next_send  - now > chrono::milliseconds(1000))
		{
			// our send queue is too large.
			m_recv_timer.expires_at(m_next_send - chrono::milliseconds(1000) / 2);
			m_recv_timer.async_wait(std::bind(&udp::socket::async_send
				, this, bufs, handler));
			return;
		}

		// the socket is writable, post the completion handler immediately
		m_io_service.post(std::bind(handler, boost::system::error_code(), 0));
	}

	std::size_t udp::socket::receive_from_impl(
		std::vector<asio::mutable_buffer> const& bufs
		, udp::endpoint* sender
		, socket_base::message_flags flags
		, boost::system::error_code& ec)
	{
		assert(!bufs.empty());
		if (!m_open)
		{
			ec = boost::system::error_code(error::bad_descriptor);
			return 0;
		}

		if (m_bound_to == udp::endpoint())
		{
			ec = boost::system::error_code(error::invalid_argument);
			return 0;
		}

		if (m_incoming_queue.empty())
		{
			ec = boost::system::error_code(error::would_block);
			return 0;
		}

		aux::packet& p = m_incoming_queue.front();
		if (sender) *sender = p.from;

		int read = 0;
		typedef std::vector<boost::asio::mutable_buffer> buffers_t;
		for (buffers_t::const_iterator i = bufs.begin(), end(bufs.end());
			i != end; ++i)
		{
			char* ptr = asio::buffer_cast<char*>(*i);
			int len = asio::buffer_size(*i);
			int to_copy = (std::min)(int(p.buffer.size()), len);
			memcpy(ptr, &p.buffer[0], to_copy);
			read += to_copy;
			p.buffer.erase(p.buffer.begin(), p.buffer.begin() + to_copy);
			m_queue_size -= to_copy;
			if (p.buffer.empty()) break;
		}

		m_incoming_queue.erase(m_incoming_queue.begin());
		return read;
	}

	void udp::socket::async_receive_null_buffers_impl(
		udp::endpoint* sender
		, boost::function<void(boost::system::error_code const&
			, std::size_t)> const& handler)
	{
		if (!m_open)
		{
			m_io_service.post(std::bind(handler
				, boost::system::error_code(error::bad_descriptor), 0));
			return;
		}

		if (m_bound_to == udp::endpoint())
		{
			m_io_service.post(std::bind(handler
				, boost::system::error_code(error::invalid_argument), 0));
			return;
		}

		if (!m_incoming_queue.empty())
		{
			m_io_service.post(std::bind(handler, boost::system::error_code(), 0));
			return;
		}

		m_recv_null_buffers = true;
		m_recv_handler = handler;
		m_recv_sender = sender;
	}

	void udp::socket::async_receive_from_impl(
		std::vector<asio::mutable_buffer> const& bufs
		, udp::endpoint* sender
		, socket_base::message_flags flags
		, boost::function<void(boost::system::error_code const&
			, std::size_t)> const& handler)
	{
		assert(!bufs.empty());

		boost::system::error_code ec;
		std::size_t bytes_transferred = receive_from_impl(bufs, sender, 0, ec);
		if (ec == boost::system::error_code(error::would_block))
		{
			m_recv_buffer = bufs;
			m_recv_handler = handler;
			m_recv_sender = sender;
			m_recv_null_buffers = false;

			return;
		}

		if (ec)
		{
			m_io_service.post(std::bind(handler, ec, 0));
			m_recv_handler = 0;
			m_recv_buffer.clear();
			m_recv_sender = NULL;
			m_recv_null_buffers = false;
			return;
		}

		m_io_service.post(std::bind(handler, ec, bytes_transferred));
		m_recv_handler = 0;
		m_recv_buffer.clear();
		m_recv_sender = NULL;
		m_recv_null_buffers = false;
	}

	std::size_t udp::socket::send_to_impl(std::vector<asio::const_buffer> const& b
		, udp::endpoint const& dst, message_flags flags
		, boost::system::error_code& ec)
	{
		assert(m_non_blocking && "blocking operations not supported");

		if (m_bound_to == ip::udp::endpoint())
		{
			// the socket was not bound, bind to anything
			bind(udp::endpoint(), ec);
			if (ec) return -1;
		}

		ec.clear();
		std::size_t ret = 0;
		for (std::vector<asio::const_buffer>::const_iterator i = b.begin()
			, end(b.end()); i != end; ++i)
		{
			ret += asio::buffer_size(*i);
		}
		if (ret == 0)
		{
			ec = boost::system::error_code(error::invalid_argument);
			return -1;
		}

		time_point now = chrono::high_resolution_clock::now();

		// this is outgoing bandwidth
		// TODO: make this configurable
		const int bandwidth = 1000000; // 1 MB/s
		const int mtu = 1475;

		if (ret > mtu)
		{
			ec = boost::system::error_code(error::message_size);
			return 0;
		}

		// determine the bandwidth in terms of nanoseconds / byte
		const double nanoseconds_per_byte = 1000000000.0
			/ double(bandwidth);

		// TODO: make the send buffer size configurable
		if (m_next_send - now > chrono::milliseconds(500))
		{
			// our send queue is too large.
			ec = boost::system::error_code(asio::error::would_block);
			return 0;
		}

		route hops = m_io_service.find_udp_socket(*this, dst);
		if (hops.empty())
		{
			// the packet is silently dropped
			// TODO: it would be nice if this would result in a round-trip time
			// with an ICMP host unreachable or connection_refused error
			return ret;
		}

		hops.prepend(m_io_service.get_outgoing_route());

		m_next_send = (std::max)(now, m_next_send);

		aux::packet p;
		p.overhead = 28;
		p.type = aux::packet::payload;
		p.from = m_bound_to;
		p.hops = hops;
		for (std::vector<asio::const_buffer>::const_iterator i = b.begin()
			, end(b.end()); i != end; ++i)
		{
			p.buffer.insert(p.buffer.end(), asio::buffer_cast<uint8_t const*>(*i)
				, asio::buffer_cast<uint8_t const*>(*i) + asio::buffer_size(*i));
		}

		const int packet_size = p.buffer.size() + p.overhead;
		forward_packet(std::move(p));

		m_next_send += chrono::duration_cast<duration>(chrono::nanoseconds(
			boost::int64_t(nanoseconds_per_byte * packet_size)));

		return ret;
	}

	void udp::socket::incoming_packet(aux::packet p)
	{
		const int packet_size = p.buffer.size() + p.overhead;

		// silent drop. If the application isn't reading fast enough, drop packets
		if (m_queue_size + packet_size > 256 * 1024) return;

		m_queue_size += p.buffer.size();
		m_incoming_queue.push_back(p);

		maybe_wakeup_reader();
	}

	void udp::socket::maybe_wakeup_reader()
	{
		if (m_incoming_queue.size() != 1 || !m_recv_handler) return;

		// there is an outstanding operation waiting for an incoming packet
		if (m_recv_null_buffers)
		{
			async_receive_null_buffers_impl(m_recv_sender, m_recv_handler);
		}
		else
		{
			async_receive_from_impl(m_recv_buffer, m_recv_sender, 0, m_recv_handler);
		}

		m_recv_handler = 0;
		m_recv_buffer.clear();
		m_recv_sender = NULL;
	}

} // ip
} // asio
} // sim

