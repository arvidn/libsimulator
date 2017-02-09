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

#include "simulator/config.hpp"
#include "simulator/simulator.hpp" // for route, endpoint

namespace sim { namespace aux {

	struct channel;

	struct SIMULATOR_DECL packet
	{
		packet()
			: type(type_t::uninitialized)
			, from(new asio::ip::udp::endpoint)
			, overhead{20}
			, seq_nr{0}
			, byte_counter{0}
		{}

		// this is move-only
#if LIBSIMULATOR_USE_MOVE
		packet(packet const&) = delete;
		packet& operator=(packet const&) = delete;
		packet(packet&&) = default;
		packet& operator=(packet&&) = default;
#endif

		// to keep things simple, don't drop ACKs or errors
		bool ok_to_drop() const
		{
			return type != type_t::syn_ack
				&& type != type_t::ack
				&& type != type_t::error;
		}

		enum class type_t
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
		// this is a unique_ptr just to make this type movable. the endpoint
		// itself isn't
#if LIBSIMULATOR_USE_MOVE
		std::unique_ptr<asio::ip::udp::endpoint> from;
#else
		std::shared_ptr<asio::ip::udp::endpoint> from;
#endif

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

		// the number of (payload) bytes sent over this channel so far. This is
		// meant to map to the TCP sequence number
		std::uint32_t byte_counter;

		// this function must be called with this packet in case the packet is
		// dropped.
#if LIBSIMULATOR_USE_MOVE
		std::unique_ptr<std::function<void(aux::packet)>> drop_fun;
#else
		std::shared_ptr<std::function<void(aux::packet)>> drop_fun;
#endif
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

}} // sim

