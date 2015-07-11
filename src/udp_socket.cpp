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

namespace sim {
namespace asio {
namespace ip {

	udp::socket::socket(io_service& ios)
		: socket_base(ios)
		, m_next_send(chrono::high_resolution_clock::now())
		, m_recv_sender(NULL)
		, m_recv_timer(ios)
		, m_queue_size(0)
		, m_is_v4(true)
	{}

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

		cancel(ec);

		ec.clear();
		return ec;
	}

	boost::system::error_code udp::socket::cancel(boost::system::error_code& ec)
	{
		// cancel outstanding async operations
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
		m_io_service.post(boost::bind(m_send_handler
			, boost::system::error_code(error::operation_aborted), 0));
		m_send_handler.clear();
//		m_send_buffer.clear();
	}

	void udp::socket::abort_recv_handler()
	{
		m_io_service.post(boost::bind(m_recv_handler
			, boost::system::error_code(error::operation_aborted), 0));
		m_recv_timer.cancel();
		m_recv_handler.clear();
		m_recv_buffer.clear();
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

		time_point now = chrono::high_resolution_clock::now();
		if (m_incoming_queue.empty()
			|| m_incoming_queue.front().receive_time > now)
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
			if (p.buffer.empty()) break;
		}

		m_incoming_queue.erase(m_incoming_queue.begin());
		return read;
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

			if (!m_incoming_queue.empty())
			{
				m_recv_timer.expires_at(m_incoming_queue.front().receive_time);
				m_recv_timer.async_wait(boost::bind(&udp::socket::async_receive_from_impl
					, this, bufs, sender, 0, handler));
			}
			return;
		}

		if (ec)
		{
			m_io_service.post(boost::bind(handler, ec, 0));
			m_recv_handler.clear();
			m_recv_buffer.clear();
			m_recv_sender = NULL;
			m_recv_null_buffers = false;
			return;
		}

		m_io_service.post(boost::bind(handler, ec, bytes_transferred));
		m_recv_handler.clear();
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
		const duration delay = chrono::milliseconds(50);
		const int mtu = 1475;

		if (ret > mtu)
		{
			ec = boost::system::error_code(error::message_size);
			return 0;
		}

		udp::socket* s = m_io_service.find_udp_socket(dst);
		if (s == NULL)
		{
			// the packet is silently dropped
			return ret;
		}

		// determine the bandwidth in terms of nanoseconds / byte
		const double nanoseconds_per_byte = 1000000000.0
			/ double(bandwidth);

		if (now - m_next_send > chrono::milliseconds(1000))
		{
			// our send queue is too large.
			ec = boost::system::error_code(asio::error::would_block);
			return 0;
		}

		m_next_send = (std::max)(now, m_next_send);
		time_point receive_time = m_next_send + delay;

		// this may still drop the packet if the incoming queue is too large
		s->internal_incoming_payload(b, receive_time, m_bound_to);

		m_next_send += chrono::duration_cast<duration>(chrono::nanoseconds(
			boost::int64_t(nanoseconds_per_byte * ret)));

		return ret;
	}

	void udp::socket::internal_incoming_payload(std::vector<const_buffer> const& b
		, chrono::high_resolution_clock::time_point receive_time
		, udp::endpoint const& from)
	{
		// silent drop
		if (m_queue_size > 256 * 1024) return;

		aux::packet p;
		p.receive_time = receive_time;
		p.type = aux::packet::payload;
		p.from = from;
		for (std::vector<asio::const_buffer>::const_iterator i = b.begin(), end(b.end());
			i != end; ++i)
		{
			p.buffer.insert(p.buffer.end(), buffer_cast<boost::uint8_t const*>(*i)
				, buffer_cast<boost::uint8_t const*>(*i) + buffer_size(*i));
		}
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
			assert(false && "not implemented");
//			async_receive_null_buffers_impl(m_recv_buffer, m_recv_sender, 0, m_recv_handler);
		}
		else
		{
			async_receive_from_impl(m_recv_buffer, m_recv_sender, 0, m_recv_handler);
		}
	}


	}
	}

}

