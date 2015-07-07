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

	tcp::socket::socket(io_service& ios)
		: m_io_service(ios)
		, m_connect_timer(ios)
		, m_next_send(chrono::high_resolution_clock::now())
		, m_total_sent(0)
		, m_queue_size(0)
		, m_max_receive_queue_size(3 * 1024 * 1024)
		, m_receive_queue_full(false)
		, m_recv_timer(ios)
		, m_open(false)
		, m_is_v4(true)
		, m_non_blocking(false)
		, m_recv_null_buffers(false)
		, m_send_null_buffers(false)
	{}

	tcp::socket::~socket()
	{
		boost::system::error_code ec;
		cancel(ec);
	}

	boost::system::error_code tcp::socket::open(tcp protocol
		, boost::system::error_code& ec)
	{
		// TODO: what if it's already open?
		close(ec);
		m_open = true;
		m_is_v4 = (protocol == ip::tcp::v4());
		ec.clear();
		return ec;
	}

	void tcp::socket::open(tcp protocol)
	{
		boost::system::error_code ec;
		open(protocol, ec);
		if (ec) throw boost::system::system_error(ec);
	}

	bool tcp::socket::is_open() const
	{
		return m_open;
	}

	// used to attach an incoming connection to this
	void tcp::socket::internal_connect(tcp::endpoint const& bind_ip
		, boost::shared_ptr<aux::channel> const& c
		, boost::system::error_code& ec)
	{
		open(m_is_v4 ? tcp::v4() : tcp::v6(), ec);
		if (ec) return;
		m_bound_to = bind_ip;
		m_channel = c;
	}

	boost::system::error_code tcp::socket::bind(ip::tcp::endpoint const& ep
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

		ip::tcp::endpoint addr = m_io_service.bind_socket(this, ep, ec);
		if (ec) return ec;
		m_bound_to = addr;
		return ec;
	}

	void tcp::socket::bind(ip::tcp::endpoint const& ep)
	{
		boost::system::error_code ec;
		bind(ep, ec);
		if (ec) throw boost::system::system_error(ec);
	}

	boost::system::error_code tcp::socket::close()
	{
		boost::system::error_code ec;
		return close(ec);
	}

	boost::system::error_code tcp::socket::close(boost::system::error_code& ec)
	{
		if (m_channel)
		{
			int remote = m_channel->remote_idx(this);
			int self = m_channel->self_idx(this);
			tcp::socket* s = m_channel->sockets[remote];
			if (s)
			{
				time_point receive_time = chrono::high_resolution_clock::now()
					+ m_channel->delay[remote];
				s->internal_incoming_eof(receive_time);
			}
			m_channel->sockets[self] = NULL;
			m_channel.reset();
		}

		if (m_bound_to != ip::tcp::endpoint())
		{
			m_io_service.unbind_socket(this, m_bound_to);
			m_bound_to = ip::tcp::endpoint();
		}
		m_open = false;

		cancel(ec);

		ec.clear();
		return ec;
	}

	std::size_t tcp::socket::available(boost::system::error_code& ec) const
	{
		if (!m_open)
		{
			ec = boost::system::error_code(error::bad_descriptor);
			return 0;
		}
		if (!m_channel)
		{
			ec = boost::system::error_code(error::not_connected);
			return 0;
		}
		if (m_incoming_queue.empty()) return 0;

		std::size_t ret = 0;
		time_point now = chrono::high_resolution_clock::now();
		for (std::vector<aux::packet>::const_iterator i = m_incoming_queue.begin()
			, end(m_incoming_queue.end()); i != end; ++i)
		{
			if (i->receive_time > now) break;
			if (i->type == aux::packet::error)
			{
				if (ret > 0) return ret;

				// if the read buffer is drained and there is an error, report that
				// error.
				ec = i->ec;
				return 0;
			}
			ret += i->buffer.size();
		}
		return ret;
	}

	std::size_t tcp::socket::available() const
	{
		boost::system::error_code ec;
		std::size_t ret = available(ec);
		if (ec) throw boost::system::system_error(ec);
		return ret;
	}

	boost::system::error_code tcp::socket::cancel(boost::system::error_code & ec)
	{
		if (m_recv_handler)
		{
			m_io_service.post(boost::bind(m_recv_handler
				, boost::system::error_code(error::operation_aborted), 0));
			m_recv_handler.clear();
			m_recv_buffer.clear();
			m_recv_null_buffers = false;
		}

		if (m_send_handler)
		{
			m_io_service.post(boost::bind(m_send_handler
				, boost::system::error_code(error::operation_aborted), 0));
			m_send_handler.clear();
			m_send_null_buffers = false;
		}

		if (m_connect_handler)
		{
			m_io_service.post(boost::bind(m_connect_handler
				, boost::system::error_code(error::operation_aborted)));
			m_connect_handler.clear();
		}

		m_total_sent = 0;
		ec.clear();
		return ec;
	}

	void tcp::socket::cancel()
	{
		boost::system::error_code ec;
		cancel(ec);
		if (ec) throw boost::system::system_error(ec);
	}

	tcp::endpoint tcp::socket::local_endpoint(boost::system::error_code& ec)
		const
	{
		if (!m_open)
		{
			ec = error::bad_descriptor;
			return tcp::endpoint();
		}

		return m_bound_to;
	}

	tcp::endpoint tcp::socket::local_endpoint() const
	{
		boost::system::error_code ec;
		tcp::endpoint ret = local_endpoint(ec);
		if (ec) throw boost::system::system_error(ec);
		return ret;
	}

	tcp::endpoint tcp::socket::remote_endpoint(boost::system::error_code& ec)
		const
	{
		if (!m_open)
		{
			ec = error::bad_descriptor;
			return tcp::endpoint();
		}

		if (!m_channel)
		{
			ec = error::not_connected;
			return tcp::endpoint();
		}

		int remote = m_channel->remote_idx(this);
		if (m_channel->sockets[remote] == NULL)
		{
			ec = error::not_connected;
			return tcp::endpoint();
		}

		return m_channel->sockets[remote]->local_endpoint(ec);
	}

	tcp::endpoint tcp::socket::remote_endpoint() const
	{
		boost::system::error_code ec;
		tcp::endpoint ret = remote_endpoint(ec);
		if (ec) throw boost::system::system_error(ec);
		return ret;
	}

	void tcp::socket::async_connect(tcp::endpoint const& target
		, boost::function<void(boost::system::error_code const&)> h)
	{
		assert(h);
		assert(!m_connect_handler);
		m_connect_handler = h;

		// find remote socket
		boost::system::error_code ec;
		if (m_bound_to.address() == ip::address())
		{
			ip::tcp::endpoint addr = m_io_service.bind_socket(this
				, ip::tcp::endpoint(), ec);
			if (ec) return;
			m_bound_to = addr;
		}
		m_channel = m_io_service.internal_connect(this, target, ec);
		if (ec)
		{
			m_channel.reset();
			// TODO: ask the policy object what the round-trip to this endpoint is
			m_connect_timer.expires_from_now(chrono::milliseconds(50));
			m_connect_timer.async_wait(boost::bind(m_connect_handler, ec));
			m_connect_handler.clear();
			return;
		}

		// the acceptor socket will call internal_connect_complete once the
		// connection is established
	}

	void tcp::socket::abort_recv_handler()
	{
		m_io_service.post(boost::bind(m_recv_handler
			, boost::system::error_code(error::operation_aborted), 0));
		m_recv_handler.clear();
		m_recv_buffer.clear();
		m_recv_null_buffers = false;
	}

	void tcp::socket::abort_send_handler()
	{
		m_io_service.post(boost::bind(m_send_handler
			, boost::system::error_code(error::operation_aborted), 0));
		m_send_handler.clear();
		m_send_buffer.clear();
		m_send_null_buffers = false;
	}

	void tcp::socket::async_write_some_impl(std::vector<boost::asio::const_buffer> const& bufs
		, boost::function<void(boost::system::error_code const&, std::size_t)> const& handler)
	{
		int buf_size = 0;
		for (int i = 0; i < bufs.size(); ++i)
			buf_size += buffer_size(bufs[i]);

		boost::system::error_code ec;
		std::size_t bytes_transferred = write_some_impl(bufs, ec);
		if (ec == boost::system::error_code(error::would_block)
			|| (!ec && bytes_transferred < 1475 && bytes_transferred < buf_size))
		{
			m_total_sent += bytes_transferred;
			int remote = m_channel->remote_idx(this);
			tcp::socket* s = m_channel->sockets[remote];
			s->subscribe_to_queue_drain();
			m_send_handler = handler;
			m_send_buffer = bufs;
			return;
		}

		if (ec)
		{
			m_io_service.post(boost::bind(handler, ec, 0));
			m_send_handler.clear();
			m_send_buffer.clear();
			m_send_null_buffers = false;
			return;
		}

		m_total_sent += bytes_transferred;

		m_io_service.post(boost::bind(handler, boost::system::error_code(), m_total_sent));
		m_send_handler.clear();
		m_send_buffer.clear();
		m_send_null_buffers = false;
		m_total_sent = 0;
	}

	// this is called by the other socket when it has read some out of its
	// receive buffer (and we indicated that we wanted to be notified)
	void tcp::socket::internal_on_buffer_drained()
	{
		if (!m_send_handler) return;
		async_write_some_impl(m_send_buffer, m_send_handler);
	}

	std::size_t tcp::socket::write_some_impl(
		std::vector<boost::asio::const_buffer> const& bufs
		, boost::system::error_code& ec)
	{
		if (!m_open)
		{
			ec = boost::system::error_code(error::bad_descriptor);
			return 0;
		}
		if (!m_channel)
		{
			ec = boost::system::error_code(error::not_connected);
			return 0;
		}

		int remote = m_channel->remote_idx(this);
		tcp::socket* s = m_channel->sockets[remote];
		if (s == NULL)
		{
			ec = boost::system::error_code(error::not_connected);
			return 0;
		}

		time_point now = chrono::high_resolution_clock::now();

		if (m_next_send > now + chrono::seconds(2))
		{
			// this indicates that the send buffer is very large, we should
			// probably not be able to stuff more bytes down it
			// wait for the receiving end to pop some bytes off
			ec = boost::system::error_code(error::would_block);
			return 0;
		}

		// determine the bandwidth in terms of nanoseconds / byte
		const double nanoseconds_per_byte = 1000000000.0
			/ double(m_channel->incoming_bandwidth[remote]);

		m_next_send = (std::max)(now, m_next_send);
		time_point receive_time = m_next_send + m_channel->delay[remote];

		typedef std::vector<boost::asio::const_buffer> buffers_t;
		std::size_t ret = 0;

		for (buffers_t::const_iterator i = bufs.begin(), end(bufs.end()); i != end; ++i)
		{
			// split up in packets
			int buf_size = buffer_size(*i);
			char const* buf = boost::asio::buffer_cast<char const*>(*i);
			while (buf_size > 0)
			{
				int packet_size = (std::min)(buf_size, 1475);
				int sent = s->internal_incoming_payload(buf, packet_size
					, receive_time);
				buf += sent;
				buf_size -= sent;
				ret += sent;
				// update the time stamp of when we can send more bytes without
				// exceeding the bandwidth limit
				m_next_send += chrono::duration_cast<duration>(chrono::nanoseconds(
					boost::int64_t(nanoseconds_per_byte * sent)));
				receive_time = m_next_send + m_channel->delay[remote];

				assert(sent <= packet_size);
				// the send queue is full or too long
				if (sent < packet_size
					|| m_next_send > now + chrono::seconds(2))
				{
					if (ret == 0)
					{
						ec = boost::system::error_code(error::would_block);
						s->subscribe_to_queue_drain();
						return 0;
					}

					return ret;
				}
			}
		}

		return ret;
	}

	std::size_t tcp::socket::read_some_impl(
		std::vector<boost::asio::mutable_buffer> const& bufs
		, boost::system::error_code& ec)
	{
		assert(!bufs.empty());
		if (!m_open)
		{
			ec = boost::system::error_code(error::bad_descriptor);
			return 0;
		}
		if (!m_channel)
		{
			ec = boost::system::error_code(error::not_connected);
			return 0;
		}

		time_point now = chrono::high_resolution_clock::now();
		if (m_incoming_queue.empty()
			|| m_incoming_queue.front().receive_time > now)
		{
			ec = boost::system::error_code(error::would_block);
			return 0;
		}

		typedef std::vector<boost::asio::mutable_buffer> buffers_t;
		m_recv_buffer = bufs;
		buffers_t::iterator recv_iter = m_recv_buffer.begin();
		int total_received = 0;
		// the offset in the current receive buffer we're writing to. i.e. the
		// buffer recv_iter points to
		int buf_offset = 0;

		while (!m_incoming_queue.empty())
		{
			aux::packet& p = m_incoming_queue.front();
			if (p.receive_time > now) break;

			if (p.type == aux::packet::error)
			{
				// if we have received bytes also, first deliver those. In the next
				// read, deliver the error
				if (total_received > 0) break;

				m_incoming_queue.erase(m_incoming_queue.begin());
				ec = p.ec;
				m_channel.reset();
				return 0;
			}

			if (p.type == aux::packet::payload)
			{
				// copy bytes from the incoming queue into the receive buffer.
				// both are vectors of buffers, so it can get a bit hairy
				while (recv_iter != m_recv_buffer.end())
				{
					int buf_size = asio::buffer_size(*recv_iter);
					int copy_size = (std::min)(int(p.buffer.size())
						, buf_size - buf_offset);

					memcpy(asio::buffer_cast<char*>(*recv_iter) + buf_offset
						, p.buffer.data(), copy_size);

					p.buffer.erase(p.buffer.begin(), p.buffer.begin() + copy_size);
					m_queue_size -= copy_size;

					buf_offset += copy_size;
					assert(buf_offset <= buf_size);
					total_received += copy_size;
					if (buf_offset == buf_size)
					{
						++recv_iter;
						buf_offset = 0;
					}

					if (p.buffer.empty())
					{
						m_incoming_queue.erase(m_incoming_queue.begin());
						break;
					}
				}
			}
			if (recv_iter == m_recv_buffer.end())
				break;
		}

		assert(total_received > 0);

		if (m_receive_queue_full && m_channel)
		{
			m_receive_queue_full = false;
			int remote = m_channel->remote_idx(this);
			tcp::socket* s = m_channel->sockets[remote];
			if (s) s->internal_on_buffer_drained();
		}
		else
		{
			m_receive_queue_full = false;
		}
		ec.clear();
		return total_received;
	}

	void tcp::socket::async_read_some_impl(std::vector<boost::asio::mutable_buffer> const& bufs
		, boost::function<void(boost::system::error_code const&, std::size_t)> const& handler)
	{
		assert(!bufs.empty());

		boost::system::error_code ec;
		std::size_t bytes_transferred = read_some_impl(bufs, ec);
		if (ec == boost::system::error_code(error::would_block))
		{
			m_recv_buffer = bufs;
			m_recv_handler = handler;
			m_recv_null_buffers = false;

			if (!m_incoming_queue.empty())
			{
				m_recv_timer.expires_at(m_incoming_queue.front().receive_time);
				m_recv_timer.async_wait(boost::bind(&tcp::socket::async_read_some_impl
					, this, bufs, handler));
			}
			return;
		}

		if (ec)
		{
			m_io_service.post(boost::bind(handler, ec, 0));
			m_recv_handler.clear();
			m_recv_buffer.clear();
			m_recv_null_buffers = false;
			return;
		}

		m_io_service.post(boost::bind(handler, ec, bytes_transferred));
		m_recv_handler.clear();
		m_recv_buffer.clear();
		m_recv_null_buffers = false;
	}

	void tcp::socket::async_read_some_null_buffers_impl(
		boost::function<void(boost::system::error_code const&, std::size_t)> const& handler)
	{
		boost::system::error_code ec;
		// null_buffers notifies the handler when data is available, without
		// reading any
		int bytes = available(ec);
		if (ec)
		{
			m_io_service.post(boost::bind(handler, ec, 0));
			m_recv_handler.clear();
			m_recv_buffer.clear();
			m_recv_null_buffers = false;
			return;
		}

		if (bytes > 0)
		{
			m_io_service.post(boost::bind(handler, ec, 0));
			m_recv_handler.clear();
			m_recv_buffer.clear();
			m_recv_null_buffers = false;
			return;
		}

		m_recv_handler = handler;
		m_recv_null_buffers = true;

		if (!m_incoming_queue.empty())
		{
			m_recv_timer.expires_at(m_incoming_queue.front().receive_time);
			m_recv_timer.async_wait(boost::bind(&tcp::socket::async_read_some_null_buffers_impl
				, this, handler));
		}
	}

	// if there is an outstanding read operation, and this was the first incoming
	// operation since we last drained, wake up the reader
	void tcp::socket::maybe_wakeup_reader()
	{
		if (m_incoming_queue.size() == 1
			&& m_recv_handler)
		{
			if (m_recv_null_buffers)
			{
				m_recv_timer.expires_at(m_incoming_queue.front().receive_time);
				m_recv_timer.async_wait(boost::bind(&tcp::socket::async_read_some_null_buffers_impl
					, this, m_recv_handler));
			}
			else
			{
				// we have an async. read operation outstanding, and we just put one
				// packet in our incoming queue.

				// try to read from it and potentially fire the handler
				async_read_some_impl(m_recv_buffer, m_recv_handler);
			}
		}
	}

	int tcp::socket::internal_incoming_payload(char const* buffer, int size
		, time_point receive_time)
	{
		int sent = (std::min)(m_max_receive_queue_size - m_queue_size, size);

		if (sent > 0)
		{
			assert(m_incoming_queue.empty()
				|| m_incoming_queue.back().receive_time <= receive_time);
			m_incoming_queue.push_back(aux::packet());
			aux::packet& p = m_incoming_queue.back();

			p.type = aux::packet::payload;
			p.receive_time = receive_time;
			p.buffer.assign(buffer, buffer + sent);
			maybe_wakeup_reader();
		}

		return sent;
	}

	void tcp::socket::internal_incoming_reset(time_point receive_time)
	{
		aux::packet p;
		p.type = aux::packet::error;
		p.ec = asio::error::connection_reset;
		p.receive_time = receive_time;
		m_incoming_queue.push_back(p);
		maybe_wakeup_reader();
	}

	void tcp::socket::internal_incoming_eof(time_point receive_time)
	{
		aux::packet p;
		p.type = aux::packet::error;
		p.ec = asio::error::eof;
		p.receive_time = receive_time;
		m_incoming_queue.push_back(p);
		maybe_wakeup_reader();
	}


	void tcp::socket::subscribe_to_queue_drain()
	{
		// once we pop data off the incoming queue, we should let the other end
		// know, so it can send more bytes down the pipe
		m_receive_queue_full = true;
	}

	// this is called on sockets that are used to accept incoming connections
	// into
	void tcp::socket::internal_connect_complete(
		boost::system::error_code const& ec)
	{
		assert(m_connect_handler);
		m_io_service.post(boost::bind(m_connect_handler, ec));
		m_connect_handler.clear();
		if (ec) m_channel.reset();
	}

	bool tcp::socket::internal_is_listening() { return false; }

	void tcp::socket::internal_accept_queue(boost::shared_ptr<aux::channel> c
		, boost::system::error_code& ec)
	{
		// this is not a listen socket
		ec = boost::system::error_code(asio::error::connection_refused);
	}

}
}
}

