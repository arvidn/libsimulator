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
#include "test.hpp"
#include <functional>

using namespace sim;
using namespace sim::asio::ip;
using namespace sim::chrono;
using sim::simulation;
using sim::default_config;
using namespace std::placeholders;

char receive_buf[1000];
char send_buf[1000];

int num_sent = 0;
int num_received = 0;

void on_receive(boost::system::error_code const& ec, std::size_t bytes_transferred
	, udp::socket& s, udp::endpoint& ep)
{
	int millis = int(duration_cast<milliseconds>(high_resolution_clock::now()
		.time_since_epoch()).count());

	if (ec)
	{
		printf("[%4d] error while receiving: %s\n", millis, ec.message().c_str());
		return;
	}

	num_received += bytes_transferred;

	printf("[%4d] received %d bytes from %s\n", millis
		, int(bytes_transferred)
		, ep.address().to_string().c_str());

	if (bytes_transferred == 100 * 10) return;

	s.async_receive_from(asio::mutable_buffers_1(receive_buf
		, sizeof(receive_buf)), ep, std::bind(&on_receive, _1, _2
		, std::ref(s), std::ref(ep)));
}

int main()
{
	default_config cfg;
	simulation sim(cfg);
	asio::io_service incoming_ios(sim, address_v4::from_string("40.30.20.10"));
	asio::io_service outgoing_ios(sim, address_v4::from_string("10.20.30.40"));

	udp::socket incoming(incoming_ios);
	udp::socket outgoing(outgoing_ios);

	boost::system::error_code ec;
	incoming.open(udp::v4(), ec);
	exit_on_error("open", ec);
	incoming.bind(udp::endpoint(address(), 1337), ec);
	exit_on_error("bind", ec);

	outgoing.open(udp::v4(), ec);
	exit_on_error("open", ec);

	udp::endpoint remote_endpoint;
	incoming.async_receive_from(asio::mutable_buffers_1(receive_buf,
			sizeof(receive_buf)), remote_endpoint, std::bind(&on_receive, _1, _2
			, std::ref(incoming), std::ref(remote_endpoint)));

	outgoing.io_control(udp::socket::non_blocking_io(true), ec);
	exit_on_error("io_control non-blocking-io", ec);

	for (int i = 1; i < 10; ++i)
	{
		num_sent += outgoing.send_to(asio::mutable_buffers_1(send_buf, 100 * i)
			, udp::endpoint(address_v4::from_string("40.30.20.10"), 1337), 0, ec);
		exit_on_error("send_to", ec);
	}

	sim.run(ec);

	int millis = int(duration_cast<milliseconds>(high_resolution_clock::now()
		.time_since_epoch()).count());

	assert(num_sent == num_received);
	assert(num_sent == 100 * 45);

	printf("[%4d] simulation::run() returned: %s\n"
		, millis, ec.message().c_str());
}

