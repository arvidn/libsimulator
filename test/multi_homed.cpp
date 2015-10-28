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

#include "catch.hpp"

using namespace sim;
using namespace sim::asio::ip;
using namespace sim::chrono;
using sim::simulation;
using sim::default_config;
using namespace std::placeholders;

namespace {

char receive_buf[1000];
char send_buf[1000];

int num_ipv6 = 0;
int num_ipv4 = 0;

}

void on_receive(boost::system::error_code const& ec, std::size_t bytes_transferred
	, udp::socket& s)
{
	int millis = int(duration_cast<milliseconds>(high_resolution_clock::now()
		.time_since_epoch()).count());

	if (ec)
	{
		printf("[%4d] error while receiving: %s\n", millis, ec.message().c_str());
		return;
	}

	printf("[%4d] received packet on %s\n", millis,
		s.local_endpoint().address().to_string().c_str());

	if (s.local_endpoint().address().is_v4())
	{
		++num_ipv4;
	}
	else
	{
		++num_ipv6;
	}
}

TEST_CASE("one node can have multiple addresses", "multi-homed")
{
	default_config cfg;
	simulation sim(cfg);

	// the machine receiving packets has two IP addresses
	asio::io_service incoming_ios(sim, {
		address_v4::from_string("40.30.20.10"),
		address_v6::from_string("ff::dead:beef:1337")} );
	asio::io_service outgoing_ios(sim, address_v4::from_string("10.20.30.40"));

	// we create two sockets for it, one for each interface
	udp::socket incoming_v4(incoming_ios);
	udp::socket incoming_v6(incoming_ios);
	udp::socket outgoing(outgoing_ios);

	boost::system::error_code ec;
	incoming_v4.open(udp::v4(), ec);
	REQUIRE(!ec);
	incoming_v4.bind(udp::endpoint(address_v4::from_string("40.30.20.10"), 1337), ec);
	REQUIRE(!ec);
	incoming_v4.async_receive(asio::mutable_buffers_1(receive_buf
		, sizeof(receive_buf)), std::bind(&on_receive, _1, _2
		, std::ref(incoming_v4)));

	incoming_v6.open(udp::v6(), ec);
	REQUIRE(!ec);
	incoming_v6.bind(udp::endpoint(address_v6::from_string("ff::dead:beef:1337"), 1337), ec);
	REQUIRE(!ec);
	incoming_v6.async_receive(asio::mutable_buffers_1(receive_buf
		, sizeof(receive_buf)), std::bind(&on_receive, _1, _2
		, std::ref(incoming_v6)));

	outgoing.open(udp::v4(), ec);
	REQUIRE(!ec);

	outgoing.io_control(udp::socket::non_blocking_io(true), ec);
	REQUIRE(!ec);

	// send a packet to the IPv4 address
	outgoing.send_to(asio::mutable_buffers_1(send_buf, sizeof(send_buf))
		, udp::endpoint(address_v4::from_string("40.30.20.10"), 1337), 0, ec);
	REQUIRE(!ec);

	// and a packet to the IPv6 address
	outgoing.send_to(asio::mutable_buffers_1(send_buf, sizeof(send_buf))
		, udp::endpoint(address_v6::from_string("ff::dead:beef:1337"), 1337), 0, ec);
	REQUIRE(!ec);

	sim.run(ec);

	int millis = int(duration_cast<milliseconds>(high_resolution_clock::now()
		.time_since_epoch()).count());

	CHECK(num_ipv4 == 1);
	CHECK(num_ipv6 == 1);

	printf("[%4d] simulation::run() returned: %s\n"
		, millis, ec.message().c_str());
}


