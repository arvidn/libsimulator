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

typedef sim::chrono::high_resolution_clock::time_point time_point;
typedef sim::chrono::high_resolution_clock::duration duration;

namespace sim {
namespace asio {
namespace ip {

	// ----- resolver ------

	udp::resolver::resolver(io_service& ios)
		: boost::asio::ip::udp::resolver(ios.get_internal_service())
	{}

	tcp::resolver::resolver(io_service& ios)
		: boost::asio::ip::tcp::resolver(ios.get_internal_service())
	{}

	default_config default_cfg;

} // ip
} // asio

void forward_packet(aux::packet p)
{
	std::shared_ptr<sink> next_hop = p.hops.pop_front();
	next_hop->incoming_packet(std::move(p));
}

namespace aux {

	int channel::remote_idx(asio::ip::tcp::endpoint self) const
	{
		if (ep[0] == self) return 1;
		if (ep[1] == self) return 0;
		assert(false && "invalid socket");
	}

	int channel::self_idx(asio::ip::tcp::endpoint self) const
	{
		if (ep[0] == self) return 0;
		if (ep[1] == self) return 1;
		assert(false && "invalid socket");
	}

} // aux
} // sim

