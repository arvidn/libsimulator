libsimulator
============

*This is still in initial development, some of this README represents ambitions
rather than the current state*

libsimulator is a library for running descreet event simulations, implementing
the ``boost.asio`` API (or a somewhat faithful emulation of a subset of it,
patches are welcome). This makes it practical to be used as a testing tool of
real implementations of network software as well as for writing simulators that
later turn into live production applications.

The simulation has to have a single time-line to be deterministic, meaning it
must be single threaded and use a single ``io_service`` as the message queue.
These requirements may affect how the program to be tested is written. It may
for instance require that an external io_service can be provided rather than one
being wrapped in an internal thread.

However, ``boost.asio`` programs may generally benefit from being transformed to
this form, as the become *composable*, i.e. agnostic to which io_service they
run on or how many threads are running it.

features
--------

The currently (partially) supported classes are:

* chrono::high_resolution_clock
* asio::high_resolution_timer
* asio::ip::tcp::acceptor
* asio::ip::tcp::endpoint
* asio::ip::address
* asio::ip::tcp::socket

Work in progress:

* asio::ip::udp::socket
* asio::ip::udp::resolver
* asio::ip::tcp::resolver

The ``high_resolution_clock`` in the ``chrono`` namespace implements the timer
concept from the chrono library.

The network socket and resolver classes simulate a network. Sockets can be bound
to any valid internet address, and will implicitly form a new node on the
network.

limitations
-----------

When connecting a socket to a listening socket, both the connecting and the
accepting sockets must first be bound to specific IPs. Binding to ``INADDR_ANY``
is not supported, as there is no obvious mechanism to determine which IP address
the requesting node should be assigned. If one simulated computer binds both a
UDP and TCP socket to ``INADDR_ANY``, in real life they would listen on the same
IPs, but there is no way to signal that two sockets belong to the same virtual
network node other than specifying its IP address.

None of the synchronous APIs are supported, for obvious reasons.

example
-------

Here's a simple example illustrating the asio timer::

	#include "simulator/simulator.hpp"
	#include <boost/bind.hpp>

	void print_time(sim::asio::high_resolution_timer& timer
		, boost::system::error_code const& ec)
	{
		using namespace sim::chrono;
		static int counter = 0;

		printf("[%d] timer fired at: %d milliseconds. error: %s\n"
			, counter
			, int(duration_cast<milliseconds>(high_resolution_clock::now()
					.time_since_epoch()).count())
			, ec.message().c_str());

		++counter;
		if (counter < 5)
		{
			timer.expires_from_now(seconds(counter));
			timer.async_wait(boost::bind(&print_time, boost::ref(timer), _1));
		}
	}

	int main()
	{
		using namespace sim::chrono;

		sim::asio::io_service ios;
		sim::asio::high_resolution_timer timer(ios);

		timer.expires_from_now(seconds(1));
		timer.async_wait(boost::bind(&print_time, boost::ref(timer), _1));

		boost::system::error_code ec;
		ios.run(ec);

		printf("io_service::run() returned: %s at: %d\n"
			, ec.message().c_str()
			, int(duration_cast<milliseconds>(high_resolution_clock::now()
					.time_since_epoch()).count()));
	}

The output from this program is::

	[0] timer fired at: 1000 milliseconds. error: Undefined error: 0
	[1] timer fired at: 2000 milliseconds. error: Undefined error: 0
	[2] timer fired at: 4000 milliseconds. error: Undefined error: 0
	[3] timer fired at: 7000 milliseconds. error: Undefined error: 0
	[4] timer fired at: 11000 milliseconds. error: Undefined error: 0
	io_service::run() returned: Undefined error: 0 at: 11000

And obviously it doesn't take 11 wall-clock seconds to run (it returns
instantly).

configuration
-------------

The simulated network can be configured with per-node pair bandwidth, round-trip
latency and queue sizes. This is controlled via a callback interface that
libsimulator will ask for these properties when nodes get connected.

*TODO: define configuration interface*

history
-------

libsimulator grew out of libtorrent's unit tests, as a tool to make them reliable
and deterministic (i.e. not depend on external systems like sockets and timers)
and also easier to debug. The subset of the asio API initially supported by this
library is the subset used by libtorrent. Patches are welcome to improve
fidelity and support.

