/*

Copyright (c) 2026, Arvid Norberg
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

#ifndef SIMULATOR_SSL_HPP_INCLUDED
#define SIMULATOR_SSL_HPP_INCLUDED

#include "simulator/simulator.hpp"

#include "simulator/push_warnings.hpp"
#include <boost/asio/ssl.hpp>
#include "simulator/pop_warnings.hpp"

namespace sim {
namespace asio {

namespace ssl
{
	using boost::asio::ssl::context;
	using boost::asio::ssl::stream_base;
	using boost::asio::ssl::verify_context;

	template <typename Stream>
	using stream = boost::asio::ssl::stream<Stream>;
} // ssl

} // asio
} // sim

#endif
