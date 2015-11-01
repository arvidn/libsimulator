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

namespace sim
{

	// builds an HTTP response buffer
	std::string SIMULATOR_DECL send_response(int code, char const* status_message
		, int len = 0, char const** extra_header = NULL);


// This is a very simple http server that only supports a single concurrent
// connection
struct SIMULATOR_DECL http_server
{
	enum flags_t
	{
		keep_alive = 1
	};

	http_server(asio::io_service& ios, int listen_port
		, int flags = http_server::keep_alive);

	void stop();

	using handler_t = std::function<std::string (std::string, std::string
		, std::map<std::string, std::string>&)>;
	void register_handler(std::string path, handler_t const& h);

private:

	void on_accept(boost::system::error_code const& ec);
	void read();
	void on_read(boost::system::error_code const& ec, size_t bytes_transferred);
	void on_write(boost::system::error_code const& ec, size_t bytes_transferred
		, bool close);
	void close_connection();

	asio::io_service& m_ios;

	asio::ip::tcp::acceptor m_listen_socket;

	asio::ip::tcp::socket m_connection;
	asio::ip::tcp::endpoint m_ep;

	std::unordered_map<std::string, handler_t> m_handlers;

	// read buffer, we receive bytes into this buffer for the connection
	std::string m_recv_buffer;

	// the number of bytes of m_recv_buffer that we've actually read data into.
	// The remaining is uninitialized, possibly being read into in an async call
	int m_bytes_used;

	std::string m_send_buffer;

	// set to true when shutting down
	bool m_close;

	int m_flags;
};

}


