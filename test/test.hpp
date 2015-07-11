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

#include <boost/system/error_code.hpp>
#include <stdio.h>
#include "simulator/simulator.hpp"

inline void exit_on_error(char const* msg, boost::system::error_code const& ec)
{
	using namespace sim::chrono;

	if (!ec) return;

	int millis = int(duration_cast<milliseconds>(high_resolution_clock::now()
		.time_since_epoch()).count());

	printf("[%4d] %s failed: %s\n", millis, msg, ec.message().c_str());
	exit(1);
}

