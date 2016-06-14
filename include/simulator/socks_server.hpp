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

#ifndef SOCKS_SERVER_HPP_INCLUDED
#define SOCKS_SERVER_HPP_INCLUDED

#include "simulator/simulator.hpp"

#ifdef _MSC_VER
#pragma warning(push)
// warning C4251: X: class Y needs to have dll-interface to be used by clients of struct
#pragma warning( disable : 4251)
#endif

namespace sim
{

struct SIMULATOR_DECL socks_connection : std::enable_shared_from_this<socks_connection>
{
	socks_connection(asio::io_service& ios, int version);

	asio::ip::tcp::socket& socket() { return m_client_connection; }

	void start();

private:

	void on_handshake1(boost::system::error_code const& ec, size_t bytes_transferred);
	void on_handshake2(boost::system::error_code const& ec, size_t bytes_transferred);
	void on_handshake3(boost::system::error_code const& ec, size_t bytes_transferred);
	void on_request1(boost::system::error_code const& ec, size_t bytes_transferred);
	void on_request2(boost::system::error_code const& ec, size_t bytes_transferred);

	void on_write(boost::system::error_code const& ec, size_t bytes_transferred
		, bool close);
	void close_connection();

	int format_response(asio::ip::tcp::endpoint const& ep, int response);

	void on_connected(boost::system::error_code const& ec);
	void on_request_domain_name(boost::system::error_code const& ec, size_t bytes_transferred);
	void on_request_domain_lookup(boost::system::error_code const& ec
		, asio::ip::tcp::resolver::iterator iter);

	void open_forward_connection(asio::ip::tcp::endpoint target);
	void bind_connection(asio::ip::tcp::endpoint target);
	void start_accept(boost::system::error_code const& ec);

	void on_server_receive(boost::system::error_code const& ec
		, std::size_t bytes_transferred);
	void on_server_forward(boost::system::error_code const& ec
		, size_t bytes_transferred);

	void on_client_receive(boost::system::error_code const& ec
		, std::size_t bytes_transferred);
	void on_client_forward(boost::system::error_code const& ec
		, size_t bytes_transferred);

	char const* command() const;

	asio::io_service& m_ios;

	asio::ip::tcp::resolver m_resolver;

	// this is the SOCKS client connection, i.e. the client connecting to us and
	// being forwarded
	asio::ip::tcp::socket m_client_connection;

	// this is the connection to the server the socks client is being forwarded
	// to
	asio::ip::tcp::socket m_server_connection;
	asio::ip::tcp::acceptor m_bind_socket;

	// receive buffer for data going out, i.e. client -> proxy (us) -> server
	char m_out_buffer[65536];
	// buffer size
	int m_num_out_bytes;

	// receive buffer for data coming in, i.e. server -> proxy (us) -> client
	char m_in_buffer[65536];
	// buffer size
	int m_num_in_bytes;

	// set to true when shutting down
	bool m_close;

	// the SOCKS protocol version (4 or 5)
	const int m_version;

	int m_command;
};

// This is a very simple socks4 and 5 server that only supports a single
// concurrent connection
struct SIMULATOR_DECL socks_server
{
	socks_server(asio::io_service& ios, int listen_port, int version = 5);

	void stop();

private:

	void on_accept(boost::system::error_code const& ec);

	asio::io_service& m_ios;

	asio::ip::tcp::acceptor m_listen_socket;

	std::shared_ptr<socks_connection> m_conn;

	asio::ip::tcp::endpoint m_ep;

	// set to true when shutting down
	bool m_close;

	// the SOCKS protocol version (4 or 5)
	const int m_version;
};

}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif

