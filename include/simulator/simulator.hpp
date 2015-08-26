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

#ifndef SIMULATOR_HPP_INCLUDED
#define SIMULATOR_HPP_INCLUDED

#include <boost/config.hpp>
#include <boost/asio/detail/config.hpp>
#include <boost/asio/basic_deadline_timer.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>

#if defined BOOST_ASIO_HAS_STD_CHRONO
#include <chrono>
#else
#include <boost/chrono/duration.hpp>
#include <boost/chrono/time_point.hpp>
#include <boost/ratio.hpp>
#endif

#include <boost/system/error_code.hpp>
#include <boost/function.hpp>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <vector>
#include <functional>

#ifdef SIMULATOR_BUILDING_SHARED
#define SIMULATOR_DECL BOOST_SYMBOL_EXPORT
#elif defined SIMULATOR_LINKING_SHARED
#define SIMULATOR_DECL BOOST_SYMBOL_IMPORT
#else
#define SIMULATOR_DECL
#endif

namespace sim
{
	namespace aux
	{
		struct channel;
		struct packet;
		struct sink_forwarder;
	}

	// this is an interface for somthing that can accept incoming packets,
	// such as queues, sockets, NATs and TCP congestion windows
	struct SIMULATOR_DECL sink
	{
		virtual void incoming_packet(aux::packet p) = 0;

		// used for visualization
		virtual std::string label() const = 0;

		virtual std::string attributes() const { return "shape=box"; }
	};

	// this represents a network route (a series of sinks to pass a packet
	// through)
	struct SIMULATOR_DECL route
	{
		route() = default;
		route(route&&) = default;
		route(route const&) = default;
		route& operator=(route&&) = default;
		route& operator=(route const&) = default;
		route(std::shared_ptr<sink> const& s) { append(s); }

		friend route operator+(route lhs, route rhs)
		{
			lhs.append(std::move(rhs));
			return lhs;
		}

		std::shared_ptr<sink> next_hop() const { return hops.front(); }
		std::shared_ptr<sink> pop_front()
		{
			std::shared_ptr<sink> ret(std::move(hops.front()));
			hops.erase(hops.begin());
			return ret;
		}
		void replace_last(std::shared_ptr<sink> s) { hops.back() = s; }
		void prepend(route const& r)
		{ hops.insert(hops.begin(), r.hops.begin(), r.hops.end()); }
		void prepend(std::shared_ptr<sink> s) { hops.insert(hops.begin(), s); }
		void append(route const& r)
		{ hops.insert(hops.end(), r.hops.begin(), r.hops.end()); }
		void append(std::shared_ptr<sink> s) { hops.push_back(s); }
		bool empty() const { return hops.empty(); }
		std::shared_ptr<sink> last() const
		{ return hops.back(); }


	private:
		std::vector<std::shared_ptr<sink>> hops;
	};

	void forward_packet(aux::packet p);

	struct simulation;

	namespace chrono
	{
#if defined BOOST_ASIO_HAS_STD_CHRONO
	using std::chrono::seconds;
	using std::chrono::milliseconds;
	using std::chrono::microseconds;
	using std::chrono::nanoseconds;
	using std::chrono::minutes;
	using std::chrono::hours;
	using std::chrono::duration_cast;
	using std::chrono::time_point;
	using std::chrono::duration;
#else
	using boost::chrono::seconds;
	using boost::chrono::milliseconds;
	using boost::chrono::microseconds;
	using boost::chrono::nanoseconds;
	using boost::chrono::minutes;
	using boost::chrono::hours;
	using boost::chrono::duration_cast;
	using boost::chrono::time_point;
	using boost::chrono::duration;
#endif

	// std.chrono / boost.chrono compatible high_resolution_clock using a simulated time
	struct SIMULATOR_DECL high_resolution_clock
	{
		typedef boost::int64_t rep;
#if defined BOOST_ASIO_HAS_STD_CHRONO
		typedef std::nano period;
		typedef std::chrono::time_point<high_resolution_clock, nanoseconds> time_point;
		typedef std::chrono::duration<boost::int64_t, std::nano> duration;
#else
		typedef boost::nano period;
		typedef time_point<high_resolution_clock, nanoseconds> time_point;
		typedef duration<boost::int64_t, boost::nano> duration;
#endif
		static const bool is_steady = true;
		static time_point now();

		// private interface
		static void fast_forward(high_resolution_clock::duration d);
	};

	} // chrono

	namespace asio
	{

	using boost::asio::buffer_size;
	using boost::asio::buffer_cast;
	using boost::asio::const_buffer;
	using boost::asio::mutable_buffer;
	using boost::asio::const_buffers_1;
	using boost::asio::mutable_buffers_1;

	struct io_service;

	struct SIMULATOR_DECL high_resolution_timer
	{
		friend struct sim::simulation;

		typedef chrono::high_resolution_clock::time_point time_type;
		typedef chrono::high_resolution_clock::duration duration_type;

		explicit high_resolution_timer(io_service& io_service);
		high_resolution_timer(io_service& io_service,
			const time_type& expiry_time);
		high_resolution_timer(io_service& io_service,
			const duration_type& expiry_time);

		std::size_t cancel(boost::system::error_code& ec);
		std::size_t cancel();
		std::size_t cancel_one();
		std::size_t cancel_one(boost::system::error_code& ec);

		time_type expires_at() const;
		std::size_t expires_at(const time_type& expiry_time);
		std::size_t expires_at(const time_type& expiry_time
			, boost::system::error_code& ec);

		duration_type expires_from_now() const;
		std::size_t expires_from_now(const duration_type& expiry_time);
		std::size_t expires_from_now(const duration_type& expiry_time
			, boost::system::error_code& ec);

		void wait();
		void wait(boost::system::error_code& ec);

		void async_wait(boost::function<void(boost::system::error_code)> handler);

		io_service& get_io_service() const { return m_io_service; }

	private:

		void fire(boost::system::error_code ec);

		time_type m_expiration_time;
		boost::function<void(boost::system::error_code const&)> m_handler;
		io_service& m_io_service;
		bool m_expired;
	};

	typedef high_resolution_timer waitable_timer;

	namespace error = boost::asio::error;
	typedef boost::asio::null_buffers null_buffers;

	namespace ip {

	typedef boost::asio::ip::address address;
	typedef boost::asio::ip::address_v4 address_v4;
	typedef boost::asio::ip::address_v6 address_v6;

	struct SIMULATOR_DECL socket_base
	{
		socket_base(io_service& ios);

		// io_control
		struct non_blocking_io
		{
			non_blocking_io(bool on): non_blocking(on) {}
			bool non_blocking;
		};

		struct reuse_address
		{
			reuse_address(bool on): reuse(on) {}
			int value() const { return reuse; }
			bool reuse;
		};

		// socket options
		struct send_buffer_size
		{
			send_buffer_size() : size(0) {}
			send_buffer_size(int s): size(s) {}
			int value() const { return size; }
			int size;
		};

		struct receive_buffer_size
		{
			receive_buffer_size() : size(0) {}
			receive_buffer_size(int s): size(s) {}
			int value() const { return size; }
			int size;
		};

		template <class Option>
		boost::system::error_code set_option(Option const&
			, boost::system::error_code& ec) { return ec; }

		boost::system::error_code set_option(receive_buffer_size const& op
			, boost::system::error_code& ec)
		{
			m_max_receive_queue_size = op.value();
			return ec;
		}

		boost::system::error_code set_option(send_buffer_size const&
			, boost::system::error_code& ec)
		{
			// TODO: implement
			return ec;
		}

		boost::system::error_code set_option(reuse_address const&
			, boost::system::error_code& ec)
		{
			// TODO: implement
			return ec;
		}

		template <class Option>
		boost::system::error_code get_option(Option&
			, boost::system::error_code& ec) { return ec; }

		boost::system::error_code get_option(receive_buffer_size& op
			, boost::system::error_code& ec)
		{
			op.size = m_max_receive_queue_size;
			return ec;
		}

		template <class IoControl>
		boost::system::error_code io_control(IoControl const&
			, boost::system::error_code& ec) { return ec; }

		boost::system::error_code io_control(non_blocking_io const& ioc
			, boost::system::error_code& ec)
		{ m_non_blocking = ioc.non_blocking; return ec; }

		bool is_open() const;

		io_service& get_io_service() const { return m_io_service; }

		typedef int message_flags;

		// internal interface

		route get_incoming_route();
		route get_outgoing_route();

	protected:

		io_service& m_io_service;

		// this is an object implementing the sink interface, forwarding
		// packets to this socket. If this socket is destructed, this forwarder
		// is redirected to just drop packets. This is necessary since sinks
		// must be held by shared_ptr, and socket objects aren't.
		std::shared_ptr<aux::sink_forwarder> m_forwarder;

		// whether the socket is open or not
		bool m_open;

		// true if the socket is set to non-blocking mode
		bool m_non_blocking;

		// the max size of the incoming queue. This is to emulate the send and
		// receive buffers. This should also depend on the bandwidth, to not
		// make the queue size not grow too long in time.
		int m_max_receive_queue_size;

	};

	struct SIMULATOR_DECL udp
	{
		static udp v4() { return udp(AF_INET); }
		static udp v6() { return udp(AF_INET6); }

		struct endpoint : boost::asio::ip::udp::endpoint
		{
			using boost::asio::ip::udp::endpoint::endpoint;

			// this constructor is temporary, until we support the resolver
			endpoint(boost::asio::ip::udp::endpoint const& ep)
				: boost::asio::ip::udp::endpoint(ep) {}
			endpoint() : boost::asio::ip::udp::endpoint() {}
			ip::udp protocol() const { return address().is_v4() ? v4() : v6(); }
		};

		struct SIMULATOR_DECL socket : socket_base, sink
		{
			typedef ip::udp::endpoint endpoint_type;
			typedef ip::udp protocol_type;
			typedef socket lowest_layer_type;

			socket(io_service& ios);
			~socket();

			lowest_layer_type& lowest_layer() { return *this; }

			udp::endpoint local_endpoint(boost::system::error_code& ec) const;
			udp::endpoint local_endpoint() const;

			boost::system::error_code bind(ip::udp::endpoint const& ep
				, boost::system::error_code& ec);
			void bind(ip::udp::endpoint const& ep);

			boost::system::error_code close();
			boost::system::error_code close(boost::system::error_code& ec);

			boost::system::error_code cancel(boost::system::error_code& ec);
			void cancel();

			boost::system::error_code open(udp protocol, boost::system::error_code& ec);
			void open(udp protocol);

			template<typename ConstBufferSequence>
			std::size_t send_to(const ConstBufferSequence& bufs
				, const udp::endpoint& destination
				, socket_base::message_flags flags
				, boost::system::error_code& ec)
			{
				std::vector<asio::const_buffer> b(bufs.begin(), bufs.end());
				if (m_send_handler) abort_send_handler();
				return send_to_impl(b, destination, flags, ec);
			}

			template<typename ConstBufferSequence>
			std::size_t send_to(const ConstBufferSequence& bufs
				, const udp::endpoint& destination)
			{
				std::vector<asio::const_buffer> b(bufs.begin(), bufs.end());
				if (m_send_handler) abort_send_handler();
				boost::system::error_code ec;
				std::size_t ret = send_to_impl(b, destination, 0, ec);
				if (ec) throw boost::system::system_error(ec);
				return ret;
			}

			void async_send(const asio::null_buffers& bufs
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler);

			void async_receive(asio::null_buffers const&
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				if (m_recv_handler) abort_recv_handler();
				async_receive_null_buffers_impl(NULL, handler);
			}

			void async_receive_from(asio::null_buffers const&
				, udp::endpoint& sender
				, socket_base::message_flags /* flags */
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				if (m_recv_handler) abort_recv_handler();
				async_receive_null_buffers_impl(&sender, handler);
			}

			void async_receive_from(asio::null_buffers const&
				, udp::endpoint& sender
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				// TODO: does it make sense to receive null_buffers and still have a
				// sender argument?
				if (m_recv_handler) abort_recv_handler();
				async_receive_null_buffers_impl(&sender, handler);
			}

			template <class BufferSequence>
			void async_receive_from(BufferSequence const& bufs
				, udp::endpoint& sender
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				std::vector<asio::mutable_buffer> b(bufs.begin(), bufs.end());
				if (m_recv_handler) abort_recv_handler();

				async_receive_from_impl(b, &sender, 0, handler);
			}

			template <class BufferSequence>
			void async_receive_from(BufferSequence const& bufs
				, udp::endpoint& sender
				, socket_base::message_flags flags
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				std::vector<asio::mutable_buffer> b(bufs.begin(), bufs.end());
				if (m_recv_handler) abort_recv_handler();

				async_receive_from_impl(b, &sender, flags, handler);
			}
/*
			void async_read_from(null_buffers const&
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				if (m_recv_handler) abort_recv_handler();
				async_read_some_null_buffers_impl(handler);
			}
*/

			template <class BufferSequence>
			std::size_t receive_from(BufferSequence const& bufs
				, udp::endpoint& sender)
			{
				std::vector<asio::mutable_buffer> b(bufs.begin(), bufs.end());
				assert(!b.empty());
				if (m_recv_handler) abort_recv_handler();
				boost::system::error_code ec;
				std::size_t ret = receive_from_impl(b, &sender, 0, ec);
				if (ec) throw boost::system::system_error(ec);
				return ret;
			}

			template <class BufferSequence>
			std::size_t receive_from(BufferSequence const& bufs
				, udp::endpoint& sender
				, socket_base::message_flags)
			{
				std::vector<asio::mutable_buffer> b(bufs.begin(), bufs.end());
				assert(!b.empty());
				if (m_recv_handler) abort_recv_handler();
				boost::system::error_code ec;
				std::size_t ret = receive_from_impl(b, &sender, 0, ec);
				if (ec) throw boost::system::system_error(ec);
				return ret;
			}

			template <class BufferSequence>
			std::size_t receive_from(BufferSequence const& bufs
				, udp::endpoint& sender
				, socket_base::message_flags
				, boost::system::error_code& ec)
			{
				std::vector<asio::mutable_buffer> b(bufs.begin(), bufs.end());
				assert(!b.empty());
				if (m_recv_handler) abort_recv_handler();
				return receive_from_impl(b, &sender, 0, ec);
			}

			// TODO: support connect and remote_endpoint

			// internal interface

			// implements sink
			virtual void incoming_packet(aux::packet p) override final;
			virtual std::string label() const override final
			{ return m_bound_to.address().to_string(); }

			void async_receive_from_impl(std::vector<asio::mutable_buffer> const& bufs
				, udp::endpoint* sender
				, socket_base::message_flags flags
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler);

			std::size_t receive_from_impl(
				std::vector<asio::mutable_buffer> const& bufs
				, udp::endpoint* sender
				, socket_base::message_flags flags
				, boost::system::error_code& ec);

			void async_receive_null_buffers_impl(
				udp::endpoint* sender
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler);

		private:
			void maybe_wakeup_reader();
			void abort_send_handler();
			void abort_recv_handler();

			std::size_t send_to_impl(std::vector<asio::const_buffer> const& b
				, udp::endpoint const& dst, message_flags flags
				, boost::system::error_code& ec);

			udp::endpoint m_bound_to;

			// this is the next time we'll have an opportunity to send another
			// outgoing packet. This is used to implement the bandwidth constraints
			// of channels. This may be in the past, in which case it's OK to send
			// a packet immediately.
			chrono::high_resolution_clock::time_point m_next_send;

			// while we're blocked in an async_write_some operation, this is the
			// handler that should be called once we're done sending
			boost::function<void(boost::system::error_code const&, std::size_t)>
				m_send_handler;

			// if we have an outstanding read on this socket, this is set to the
			// handler.
			boost::function<void(boost::system::error_code const&, std::size_t)>
				m_recv_handler;

			// if we have an outstanding read operation, this is the buffer to
			// receive into
			std::vector<asio::mutable_buffer> m_recv_buffer;

			// if we have an outstanding receive operation, this may point to an
			// endpoint to fill in the senders IP in
			udp::endpoint* m_recv_sender;

			asio::high_resolution_timer m_recv_timer;
			asio::high_resolution_timer m_send_timer;

			// this is the incoming queue of packets for each socket
			std::vector<aux::packet> m_incoming_queue;

			bool m_recv_null_buffers;

			// the number of bytes in the incoming packet queue
			int m_queue_size;

			// our address family
			bool m_is_v4;
		};

		struct resolver : boost::asio::ip::udp::resolver
		{
			resolver(io_service& ios);
		};

		friend bool operator==(udp const& lhs, udp const& rhs)
		{ return lhs.m_family == rhs.m_family; }

		friend bool operator!=(udp const& lhs, udp const& rhs)
		{ return lhs.m_family != rhs.m_family; }

	private:
		// Construct with a specific family.
		explicit udp(int protocol_family)
			: m_family(protocol_family)
		{}

		int m_family;

	}; // udp

	struct SIMULATOR_DECL tcp
	{
		// temporary fix until the resolvers are implemented using our endpoint
		tcp(boost::asio::ip::tcp p) : m_family(p.family()) {}

		static tcp v4() { return tcp(AF_INET); }
		static tcp v6() { return tcp(AF_INET6); }

		int family() const { return m_family; }

		struct endpoint : boost::asio::ip::tcp::endpoint
		{
			using boost::asio::ip::tcp::endpoint::endpoint;

			// this constructor is temporary, until we support the resolver
			endpoint(boost::asio::ip::tcp::endpoint const& ep)
				: boost::asio::ip::tcp::endpoint(ep) {}
			endpoint() : boost::asio::ip::tcp::endpoint() {}
			ip::tcp protocol() const { return address().is_v4() ? v4() : v6(); }
		};

		struct SIMULATOR_DECL socket : socket_base, sink
		{
			typedef ip::tcp::endpoint endpoint_type;
			typedef ip::tcp protocol_type;
			typedef socket lowest_layer_type;

			socket(io_service& ios);
			~socket();

			boost::system::error_code close();
			boost::system::error_code close(boost::system::error_code& ec);
			boost::system::error_code open(tcp protocol, boost::system::error_code& ec);
			void open(tcp protocol);
			boost::system::error_code bind(ip::tcp::endpoint const& ep
				, boost::system::error_code& ec);
			void bind(ip::tcp::endpoint const& ep);
			tcp::endpoint local_endpoint(boost::system::error_code& ec) const;
			tcp::endpoint local_endpoint() const;
			tcp::endpoint remote_endpoint(boost::system::error_code& ec) const;
			tcp::endpoint remote_endpoint() const;

			lowest_layer_type& lowest_layer() { return *this; }

			void async_connect(tcp::endpoint const& target
				, boost::function<void(boost::system::error_code const&)> h);

			template <class ConstBufferSequence>
			void async_write_some(ConstBufferSequence const& bufs
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				std::vector<asio::const_buffer> b(bufs.begin(), bufs.end());
				if (m_send_handler) abort_send_handler();
				async_write_some_impl(b, handler);
			}

			void async_write_some(null_buffers const&
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& /* handler */)
			{
				if (m_send_handler) abort_send_handler();
				assert(false && "not supported yet");
//				async_write_some_null_buffers_impl(b, handler);
			}

			void async_read_some(null_buffers const&
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				if (m_recv_handler) abort_recv_handler();
				async_read_some_null_buffers_impl(handler);
			}

			template <class BufferSequence>
			std::size_t read_some(BufferSequence const& bufs
				, boost::system::error_code& ec)
			{
				assert(m_non_blocking && "blocking operations not supported");
				std::vector<asio::mutable_buffer> b(bufs.begin(), bufs.end());
				return read_some_impl(b, ec);
			}

			template <class ConstBufferSequence>
			std::size_t write_some(ConstBufferSequence const& bufs
				, boost::system::error_code& ec)
			{
				assert(m_non_blocking && "blocking operations not supported");
				std::vector<asio::const_buffer> b(bufs.begin(), bufs.end());
				return write_some_impl(b, ec);
			}

			template <class BufferSequence>
			void async_read_some(BufferSequence const& bufs
				, boost::function<void(boost::system::error_code const&
					, std::size_t)> const& handler)
			{
				std::vector<asio::mutable_buffer> b(bufs.begin(), bufs.end());
				if (m_recv_handler) abort_recv_handler();

				async_read_some_impl(b, handler);
			}

			std::size_t available(boost::system::error_code & ec) const;
			std::size_t available() const;

			boost::system::error_code cancel(boost::system::error_code& ec);
			void cancel();

			using socket_base::set_option;
			using socket_base::get_option;
			using socket_base::io_control;

			// private interface

			// implements sink
			virtual void incoming_packet(aux::packet p) override;
			virtual std::string label() const override final
			{ return m_bound_to.address().to_string(); }

			void internal_connect(tcp::endpoint const& bind_ip
				, std::shared_ptr<aux::channel> const& c
				, boost::system::error_code& ec);

			void abort_send_handler();
			void abort_recv_handler();

			virtual bool internal_is_listening();
		protected:

			void maybe_wakeup_reader();
			void maybe_wakeup_writer();

			void async_write_some_impl(std::vector<asio::const_buffer> const& bufs
				, boost::function<void(boost::system::error_code const&, std::size_t)> const& handler);
			void async_read_some_impl(std::vector<asio::mutable_buffer> const& bufs
				, boost::function<void(boost::system::error_code const&, std::size_t)> const& handler);
			void async_read_some_null_buffers_impl(
				boost::function<void(boost::system::error_code const&, std::size_t)> const& handler);
			std::size_t write_some_impl(std::vector<asio::const_buffer> const& bufs
				, boost::system::error_code& ec);
			std::size_t read_some_impl(std::vector<asio::mutable_buffer> const& bufs
				, boost::system::error_code& ec);

			void send_packet(aux::packet p);

			// called when a packet is dropped
			void packet_dropped(aux::packet p);

			ip::tcp::endpoint m_bound_to;

			boost::function<void(boost::system::error_code const&)> m_connect_handler;

			asio::high_resolution_timer m_connect_timer;

			// the tcp "packet size" (segment size)
			int m_mss;

			// while we're blocked in an async_write_some operation, this is the
			// handler that should be called once we're done sending
			boost::function<void(boost::system::error_code const&, std::size_t)>
				m_send_handler;

			std::vector<asio::const_buffer> m_send_buffer;

			// this is the incoming queue of packets for each socket
			std::vector<aux::packet> m_incoming_queue;

			// the number of bytes in the incoming packet queue
			int m_queue_size;

			// if we have an outstanding read on this socket, this is set to the
			// handler.
			boost::function<void(boost::system::error_code const&, std::size_t)>
				m_recv_handler;

			// if we have an outstanding buffers to receive into, these are them
			std::vector<asio::mutable_buffer> m_recv_buffer;

			asio::high_resolution_timer m_recv_timer;

			// our address family
			bool m_is_v4;

			// true if the currently outstanding read operation is for null_buffers
			bool m_recv_null_buffers;

			// true if the currenly outstanding write operation is for null_buffers
			bool m_send_null_buffers;

			// if this socket is connected to another endpoint, this object is
			// shared between both sockets and contain information and state about
			// the channel.
			std::shared_ptr<aux::channel> m_channel;

			std::uint64_t m_next_outgoing_seq;
			std::uint64_t m_next_incoming_seq;

			// the sequence number of the last dropped packet. We should only cut
			// the cwnd in half once per round-trip. If a whole window is lost, we
			// need to only halve it once
			std::uint64_t m_last_drop_seq;

			// the current congestion window size (in bytes)
			int m_cwnd;

			// the number of bytes that have been sent but not ACKed yet
			int m_bytes_in_flight;

			// reorder buffer for when packets are dropped
			std::map<std::uint64_t, aux::packet> m_reorder_buffer;

			// the sizes of packets given their sequence number
			std::unordered_map<std::uint64_t, int> m_outstanding_packet_sizes;

			// packets to re-send (because they were dropped)
			std::vector<aux::packet> m_outgoing_packets;
		};

		struct SIMULATOR_DECL acceptor : socket
		{
			acceptor(io_service& ios);
			~acceptor();

			boost::system::error_code cancel(boost::system::error_code& ec);
			void cancel();

			void listen(int qs, boost::system::error_code& ec);

			void async_accept(ip::tcp::socket& peer
				, boost::function<void(boost::system::error_code const&)> h);
			void async_accept(ip::tcp::socket& peer
				, ip::tcp::endpoint& peer_endpoint
				, boost::function<void(boost::system::error_code const&)> h);

			boost::system::error_code close(boost::system::error_code& ec);
			void close();

			// private interface

			// implements sink
			virtual void incoming_packet(aux::packet p) override final;
			virtual bool internal_is_listening();

		private:
			// check the incoming connection queue to see if any connection in
			// there is ready to be accepted and delivered to the user
			void check_accept_queue();
			void do_check_accept_queue(boost::system::error_code const& ec);

			boost::function<void(boost::system::error_code const&)> m_accept_handler;

			// the number of half-open incoming connections this listen socket can
			// hold. If this is -1, this socket is not yet listening and incoming
			// connection attempts should be rejected.
			int m_queue_size_limit;

			// these are incoming connection attempts. Both half-open and
			// completely connected. When accepting a connection, this queue is
			// checked first before waiting for a connection attempt.
			typedef std::vector<std::shared_ptr<aux::channel> > incoming_conns_t;
			incoming_conns_t m_incoming_queue;

			// the socket to accept a connection into
			tcp::socket* m_accept_into;

			// the endpoint to write the remote endpoint into when accepting
			tcp::endpoint* m_remote_endpoint;

			// non copyable
			acceptor(acceptor const&);
			acceptor& operator=(acceptor const&);
		};

		struct SIMULATOR_DECL resolver : boost::asio::ip::tcp::resolver
		{
			resolver(io_service& ios);
		};

		friend bool operator==(tcp const& lhs, tcp const& rhs)
		{ return lhs.m_family == rhs.m_family; }

		friend bool operator!=(tcp const& lhs, tcp const& rhs)
		{ return lhs.m_family != rhs.m_family; }

	private:
		// Construct with a specific family.
		explicit tcp(int protocol_family)
			: m_family(protocol_family)
		{}

		int m_family;
	};

	} // ip

	using boost::asio::async_write;
	using boost::asio::async_read;

	// boost.asio compatible io_service class that simulates the network
	// and time.
	struct SIMULATOR_DECL io_service
	{
		struct work : boost::asio::io_service::work
		{
			work(io_service& ios) :
				boost::asio::io_service::work(ios.get_internal_service()) {}
		};

		io_service(sim::simulation& sim, ip::address const& ip);
		io_service();
		~io_service();

		// not copyable and non movable (it's not movable because we currently
		// keep pointers to the io_service instances in the simulator object)
		io_service(io_service const&) = delete;
		io_service(io_service&&) = delete;
		io_service& operator=(io_service const&) = delete;
		io_service& operator=(io_service&&) = delete;

		std::size_t run(boost::system::error_code& ec);
		std::size_t run();

		std::size_t poll(boost::system::error_code& ec);
		std::size_t poll();

		std::size_t poll_one(boost::system::error_code& ec);
		std::size_t poll_one();

		void stop();
		bool stopped() const;
		void reset();

		void dispatch(boost::function<void()> handler);
		void post(boost::function<void()> handler);

		// internal interface
		boost::asio::io_service& get_internal_service();

		void add_timer(high_resolution_timer* t);
		void remove_timer(high_resolution_timer* t);

		ip::tcp::endpoint bind_socket(ip::tcp::socket* socket, ip::tcp::endpoint ep
			, boost::system::error_code& ec);
		void unbind_socket(ip::tcp::socket* socket
			, ip::tcp::endpoint ep);

		ip::udp::endpoint bind_udp_socket(ip::udp::socket* socket, ip::udp::endpoint ep
			, boost::system::error_code& ec);
		void unbind_udp_socket(ip::udp::socket* socket
			, ip::udp::endpoint ep);

		std::shared_ptr<aux::channel> internal_connect(ip::tcp::socket* s
			, ip::tcp::endpoint const& target, boost::system::error_code& ec);

		route find_udp_socket(asio::ip::udp::socket const& socket
			, ip::udp::endpoint const& ep);

		route const& get_outgoing_route() const
		{ return m_outgoing_route; }

		route const& get_incoming_route() const
		{ return m_incoming_route; }

		int get_path_mtu(asio::ip::address ip) const;
		ip::address get_ip() const { return m_ip; }

		sim::simulation& sim() { return m_sim; }

	private:

		sim::simulation& m_sim;
		ip::address m_ip;

		// these are determined by the configuration. They may include NATs and
		// DSL modems (queues)
		route m_outgoing_route;
		route m_incoming_route;

		bool m_stopped;
	};

	} // asio

	struct configuration;
	struct queue;

	// user supplied configuration of the network to simulate
	struct SIMULATOR_DECL configuration
	{
		// build the network
		virtual void build(simulation& sim) = 0;

		// return the hops on the network packets from src to dst need to traverse
		virtual route channel_route(asio::ip::address src
			, asio::ip::address dst) = 0;

		// return the hops an incoming packet to ep need to traverse before
		// reaching the socket (for instance a NAT)
		virtual route incoming_route(asio::ip::address ip) = 0;

		// return the hops an outgoing packet from ep need to traverse before
		// reaching the network (for instance a DSL modem)
		virtual route outgoing_route(asio::ip::address ip) = 0;

		// return the path MTU between the two IP addresses
		// For TCP sockets, this will be called once when the connection is
		// established. For UDP sockets it's called for every burst of packets
		// that are sent
		virtual int path_mtu(asio::ip::address ip1, asio::ip::address ip2) = 0;
	};

	struct SIMULATOR_DECL default_config : configuration
	{
		default_config() : m_sim(nullptr) {}

		virtual void build(simulation& sim) override final;
		virtual route channel_route(asio::ip::address src
			, asio::ip::address dst) override final;
		virtual route incoming_route(asio::ip::address ip) override final;
		virtual route outgoing_route(asio::ip::address ip) override final;
		virtual int path_mtu(asio::ip::address ip1, asio::ip::address ip2)
			override final;

	private:
		std::shared_ptr<queue> m_network;
		std::map<asio::ip::address, std::shared_ptr<queue>> m_incoming;
		std::map<asio::ip::address, std::shared_ptr<queue>> m_outgoing;
		simulation* m_sim;
	};

	struct SIMULATOR_DECL simulation
	{
		// it calls fire() when a timer fires
		friend struct high_resolution_timer;

		simulation(configuration& config);

		std::size_t run(boost::system::error_code& ec);
		std::size_t run();

		std::size_t poll(boost::system::error_code& ec);
		std::size_t poll();

		std::size_t poll_one(boost::system::error_code& ec);
		std::size_t poll_one();

		void stop();
		bool stopped() const;
		void reset();
		// private interface

		void add_timer(asio::high_resolution_timer* t);
		void remove_timer(asio::high_resolution_timer* t);

		boost::asio::io_service& get_internal_service()
		{ return m_service; }

		asio::io_service& get_io_service() { return m_internal_ios; }

		asio::ip::tcp::endpoint bind_socket(asio::ip::tcp::socket* socket
			, asio::ip::tcp::endpoint ep
			, boost::system::error_code& ec);
		void unbind_socket(asio::ip::tcp::socket* socket
			, asio::ip::tcp::endpoint ep);

		asio::ip::udp::endpoint bind_udp_socket(asio::ip::udp::socket* socket
			, asio::ip::udp::endpoint ep
			, boost::system::error_code& ec);
		void unbind_udp_socket(asio::ip::udp::socket* socket
			, asio::ip::udp::endpoint ep);

		std::shared_ptr<aux::channel> internal_connect(asio::ip::tcp::socket* s
			, asio::ip::tcp::endpoint const& target, boost::system::error_code& ec);

		route find_udp_socket(
			asio::ip::udp::socket const& socket
			, asio::ip::udp::endpoint const& ep);

		configuration& config() const { return m_config; }

		void add_io_service(asio::io_service* ios);
		void remove_io_service(asio::io_service* ios);
		std::vector<asio::io_service*> get_all_io_services() const;

	private:
		struct timer_compare
		{
			bool operator()(asio::high_resolution_timer const* lhs
				, asio::high_resolution_timer const* rhs)
			{ return lhs->expires_at() < rhs->expires_at(); }
		};

		configuration& m_config;

		// these are the io services that represent nodes on the network
		std::unordered_set<asio::io_service*> m_nodes;

		// all non-expired timers
		typedef std::multiset<asio::high_resolution_timer*, timer_compare> timer_queue_t;
		timer_queue_t m_timer_queue;
		// underlying message queue
		boost::asio::io_service m_service;

		// used for internal timers
		asio::io_service m_internal_ios;

		typedef std::map<asio::ip::tcp::endpoint, asio::ip::tcp::socket*>
			listen_sockets_t;
		typedef listen_sockets_t::iterator listen_socket_iter_t;
		listen_sockets_t m_listen_sockets;

		typedef std::map<asio::ip::udp::endpoint, asio::ip::udp::socket*>
			udp_sockets_t;
		typedef udp_sockets_t::iterator udp_socket_iter_t;
		udp_sockets_t m_udp_sockets;

		bool m_stopped;
	};

	namespace aux
	{
		struct SIMULATOR_DECL packet
		{
			packet()
				: type(uninitialized)
				, overhead{20}
				, seq_nr{0}
			{}

			// to keep things simple, don't drop ACKs or errors
			bool ok_to_drop() const
			{
				return type != syn_ack && type != ack && type != error;
			}

			enum type_t
			{
				uninitialized, // invalid type (used for debugging)
				syn, // TCP connect
				syn_ack, // TCP connection accepted
				ack, // the seq_nr is interpreted as "we received this"
				error, // the error_code (ec) is set
				payload // the buffer is filled
			} type;

			boost::system::error_code ec;

			// actual payload
			std::vector<boost::uint8_t> buffer;

			// used for UDP packets
			asio::ip::udp::endpoint from;

			// the number of bytes of overhead for this packet. The total packet
			// size is the number of bytes in the buffer + this number
			int overhead;

			// each hop in the route will pop itself off and forward the packet to
			// the next hop
			route hops;

			// for SYN packets, this is set to the channel we're trying to
			// establish
			std::shared_ptr<aux::channel> channel;

			// sequence number of this packet (used for debugging)
			std::uint64_t seq_nr;

			// this function must be called with this packet in case the packet is
			// dropped.
			boost::function<void(aux::packet)> drop_fun;
		};

		struct SIMULATOR_DECL sink_forwarder : sink
		{
			sink_forwarder(sink* dst) : m_dst(dst) {}

			virtual void incoming_packet(packet p) override final
			{
				if (m_dst == nullptr) return;
				m_dst->incoming_packet(std::move(p));
			}

			virtual std::string label() const override final
			{ return m_dst ? m_dst->label() : ""; }

			void clear() { m_dst = nullptr; }

		private:
			sink* m_dst;
		};

		/* the channel can be in the following states:
			1. handshake-1 - the initiating socket has sent SYN
			2. handshake-2 - the accepting connection has sent SYN+ACK
			3. handshake-3 - the initiating connection has received the SYN+ACK and
			                 considers the connection open, but the 3rd handshake
			                 message is still in flight.
			4. connected   - the accepting side has received the 3rd handshake
			                 packet and considers it open

			Whenever a connection attempt is made to a listening socket, as long as
			there is still space in the incoming socket queue, the accepting side
			will always respond immediately and complete the handshake, then wait
			until the user calls async_accept (which in this case would complete
			immediately).
		*/
		struct SIMULATOR_DECL channel
		{
			channel() {}
			// index 0 is the incoming route to the socket that initiated the connection.
			// index 1 may be empty while the connection is half-open
			route hops[2];

			// the endpoint of each end of the channel
			asio::ip::tcp::endpoint ep[2];

			int remote_idx(asio::ip::tcp::endpoint self) const;
			int self_idx(asio::ip::tcp::endpoint self) const;
		};

	} // aux

	// this is a queue. It can be configured to contrain
	struct SIMULATOR_DECL queue : sink
	{
		queue(asio::io_service& ios, int bandwidth
			, chrono::high_resolution_clock::duration propagation_delay
			, int max_queue_size, std::string name = "queue");

		virtual void incoming_packet(aux::packet p) override final;

		virtual std::string label() const override final;

	private:

		void begin_send_next_packet();
		void next_packet_sent();

		// the queue can't hold more than this number of bytes. Once it's full,
		// any new packets arriving will be dropped (tail drop)
		const int m_max_queue_size;

		// the amount of time it takes to forward a packet. Every packet is
		// delayed by at least this much before being forwarded
		const chrono::high_resolution_clock::duration m_forwarding_latency;

		// the number of bytes per second that can be sent. This includes the
		// packet overhead
		const int m_bandwidth;

		// the number of bytes currently in the packet queue
		int m_queue_size;

		std::string m_node_name;

		// this is the queue of packets and the time each packet was enqueued
		std::vector<std::pair<chrono::high_resolution_clock::time_point, aux::packet>> m_queue;
		asio::high_resolution_timer m_forward_timer;

		chrono::high_resolution_clock::time_point m_last_forward;
	};

	void SIMULATOR_DECL dump_network_graph(simulation const& s, std::string filename);
}

#endif // SIMULATOR_HPP_INCLUDED

