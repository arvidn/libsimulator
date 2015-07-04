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

int counter = 0;
int expected_timestamps[] = {1000, 2000, 4000, 7000, 11000 };

using namespace sim::chrono;
using namespace sim::asio;
using sim::simulation;

void print_time(high_resolution_timer& timer
	, boost::system::error_code const& ec)
{
	using namespace sim::chrono;

	int millis = int(duration_cast<milliseconds>(high_resolution_clock::now()
				.time_since_epoch()).count());
	printf("[%d] timer fired at: %d milliseconds. error: %s\n"
		, counter
		, millis
		, ec.message().c_str());

	assert(millis == expected_timestamps[counter]);

	++counter;
	if (counter < 5)
	{
		timer.expires_from_now(seconds(counter));
		timer.async_wait(boost::bind(&print_time, boost::ref(timer), _1));
	}
}

int main()
{
	simulation sim;
	io_service ios(sim, ip::address_v4::from_string("1.2.3.4"));
	high_resolution_timer timer(ios);

	timer.expires_from_now(seconds(1));
	timer.async_wait(boost::bind(&print_time, boost::ref(timer), _1));

	boost::system::error_code ec;
	sim.run(ec);

	printf("io_service::run() returned: %s at: %d\n"
		, ec.message().c_str()
		, int(duration_cast<milliseconds>(high_resolution_clock::now()
				.time_since_epoch()).count()));

	assert(counter == 5);
}

