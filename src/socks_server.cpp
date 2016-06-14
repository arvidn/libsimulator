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
#include "simulator/socks_server.hpp"

#include <functional>
#include <cstdio> // for printf

using namespace sim::asio;
using namespace sim::asio::ip;
using namespace std::placeholders;

using boost::system::error_code;

namespace sim
{
	using namespace aux;

	socks_server::socks_server(io_service& ios, int listen_port, int version)
		: m_ios(ios)
		, m_listen_socket(ios)
		, m_conn(std::make_shared<socks_connection>(m_ios, version))
		, m_close(false)
		, m_version(version)
	{
		address local_ip = ios.get_ips().front();
		if (local_ip.is_v4())
		{
			m_listen_socket.open(tcp::v4());
			m_listen_socket.bind(tcp::endpoint(address_v4::any(), listen_port));
		}
		else
		{
			m_listen_socket.open(tcp::v6());
			m_listen_socket.bind(tcp::endpoint(address_v6::any(), listen_port));
		}
		m_listen_socket.listen();

		m_listen_socket.async_accept(m_conn->socket(), m_ep
			, std::bind(&socks_server::on_accept, this, _1));
	}

	void socks_server::on_accept(error_code const& ec)
	{
		if (ec == asio::error::operation_aborted)
			return;

		if (ec)
		{
			std::printf("socks_server::on_accept: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			return;
		}

		std::printf("socks_server accepted connection from: %s : %d\n",
			m_ep.address().to_string().c_str(), m_ep.port());

		m_conn->start();

		// create a new connection to accept into
		m_conn = std::make_shared<socks_connection>(m_ios, m_version);

		// now we can accept another connection
		m_listen_socket.async_accept(m_conn->socket(), m_ep
			, std::bind(&socks_server::on_accept, this, _1));
	}

	void socks_server::stop()
	{
		m_close = true;
		m_listen_socket.close();
	}

	socks_connection::socks_connection(asio::io_service& ios
		, int version)
		: m_ios(ios)
		, m_resolver(m_ios)
		, m_client_connection(ios)
		, m_server_connection(m_ios)
		, m_bind_socket(m_ios)
		, m_num_out_bytes(0)
		, m_num_in_bytes(0)
		, m_close(false)
		, m_version(version)
		, m_command(0)
	{
	}

	void socks_connection::start()
	{
		if (m_version == 4)
		{
			asio::async_read(m_client_connection, asio::mutable_buffers_1(&m_out_buffer[0], 9)
				, std::bind(&socks_connection::on_request1, shared_from_this(), _1, _2));
		} else {
			// read protocol version and number of auth-methods
			asio::async_read(m_client_connection, asio::mutable_buffers_1(&m_out_buffer[0], 2)
				, std::bind(&socks_connection::on_handshake1, shared_from_this(), _1, _2));
		}
	}

	void socks_connection::on_handshake1(error_code const& ec, size_t bytes_transferred)
	{
		if (ec || bytes_transferred != 2)
		{
			std::printf("socks_connection::on_handshake1: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		if (m_out_buffer[0] != 4 && m_out_buffer[0] != 5)
		{
			std::printf("socks_connection::on_handshake1: unexpected socks protocol version: %d"
				, int(m_out_buffer[0]));
			close_connection();
			return;
		}

		int num_methods = unsigned(m_out_buffer[1]);

		// read list of auth-methods
		asio::async_read(m_client_connection, asio::mutable_buffers_1(&m_out_buffer[0],
				num_methods)
			, std::bind(&socks_connection::on_handshake2, shared_from_this()
				, _1, _2));
	}

	void socks_connection::on_handshake2(error_code const& ec, size_t bytes_transferred)
	{
		if (ec)
		{
			std::printf("socks_connection::on_handshake2: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		if (std::count(m_out_buffer, m_out_buffer + bytes_transferred, 0) == 0)
		{
			std::printf("socks_connection: could not find auth-method 0 (no-auth) in socks handshake\n");
			close_connection();
			return;
		}

		m_in_buffer[0] = 5; // socks version
		m_in_buffer[1] = 0; // auth-method (no-auth)

		asio::async_write(m_client_connection, asio::const_buffers_1(&m_in_buffer[0], 2)
			, std::bind(&socks_connection::on_handshake3, shared_from_this()
				, _1, _2));
	}

	void socks_connection::on_handshake3(error_code const& ec, size_t bytes_transferred)
	{
		if (ec || bytes_transferred != 2)
		{
			std::printf("socks_connection::on_handshake3: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		asio::async_read(m_client_connection, asio::mutable_buffers_1(&m_out_buffer[0], 10)
			, std::bind(&socks_connection::on_request1, shared_from_this()
				, _1, _2));
	}

	void socks_connection::on_request1(error_code const& ec, size_t bytes_transferred)
	{
		size_t const expected = m_version == 4 ? 9 : 10;
		if (ec || bytes_transferred != expected)
		{
			std::printf("socks_connection::on_request1: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

// +----+-----+-------+------+----------+----------+
// |VER | CMD |  RSV  | ATYP | DST.ADDR | DST.PORT |
// +----+-----+-------+------+----------+----------+
// | 1  |  1  | X'00' |  1   | Variable |    2     |
// +----+-----+-------+------+----------+----------+

		int const version = m_out_buffer[0];
		int const command = m_out_buffer[1];
		m_command = command;

		if (version != m_version)
		{
			std::printf("socks_connection::on_request1: unexpected socks protocol version: %d expected: %d\n"
				, int(m_out_buffer[0]), m_version);
			close_connection();
			return;
		}

		if (m_version == 4)
		{
			if (command != 1 && command != 2)
			{
				std::printf("socks_connection::on_request1: unexpected socks command: %d\n"
					, command);
				close_connection();
				return;
			}

			boost::uint16_t port = m_out_buffer[2] & 0xff;
			port <<= 8;
			port |= m_out_buffer[3] & 0xff;

			boost::uint32_t addr = m_out_buffer[4] & 0xff;
			addr <<= 8;
			addr |= m_out_buffer[5] & 0xff;
			addr <<= 8;
			addr |= m_out_buffer[6] & 0xff;
			addr <<= 8;
			addr |= m_out_buffer[7] & 0xff;

			if (m_out_buffer[8] != 0)
			{
				// in this case, we would have to read one byte at a time until we
				// get to the null terminator.
				std::printf("socks_connection::on_request1: username in SOCKS4 mode not supported\n");
				close_connection();
				return;
			}
			asio::ip::tcp::endpoint target(asio::ip::address_v4(addr), port);
			if (command == 1)
			{
				open_forward_connection(target);
			}
			else if (command == 2)
			{
				bind_connection(target);
			}

			return;
		}

		if (command != 1 && command != 2)
		{
			std::printf("socks_connection::on_request1: unexpected command: %d\n"
				, command);
			close_connection();
			return;
		}

		if (m_out_buffer[2] != 0)
		{
			std::printf("socks_connection::on_request1: reserved byte is non-zero: %d\n"
				, int(m_out_buffer[2]));
			close_connection();
			return;
		}

		int atyp = unsigned(m_out_buffer[3]);

		if (atyp != 1 && atyp != 3 && atyp != 4)
		{
			std::printf("socks_connection::on_request1: unexpected address type in SOCKS request: %d\n"
				, atyp);
			close_connection();
			return;
		}

		std::printf("socks_connection: received %s request address type: %d\n"
			, command == 1 ? "CONNECT" : "BIND", atyp);

		switch (atyp)
		{
			case 1: { // IPv4 address (we have the whole request already)

// +----+-----+-------+------+----------+----------+
// |VER | CMD |  RSV  | ATYP | BND.ADDR | BND.PORT |
// +----+-----+-------+------+----------+----------+
// | 1  |  1  | X'00' |  1   | 4        |    2     |
// +----+-----+-------+------+----------+----------+

				boost::uint32_t addr = m_out_buffer[4] & 0xff;
				addr <<= 8;
				addr |= m_out_buffer[5] & 0xff;
				addr <<= 8;
				addr |= m_out_buffer[6] & 0xff;
				addr <<= 8;
				addr |= m_out_buffer[7] & 0xff;

				boost::uint16_t port = m_out_buffer[8] & 0xff;
				port <<= 8;
				port |= m_out_buffer[9] & 0xff;

				asio::ip::tcp::endpoint target(asio::ip::address_v4(addr), port);
				if (command == 1)
				{
					open_forward_connection(target);
				}
				else if (command == 2)
				{
					bind_connection(target);
				}

				break;
			}
			case 3: { // domain name

// +----+-----+-------+------+-----+----------+----------+
// |VER | CMD |  RSV  | ATYP | LEN | BND.ADDR | BND.PORT |
// +----+-----+-------+------+-----+----------+----------+
// | 1  |  1  | X'00' |  1   | 1   | Variable |    2     |
// +----+-----+-------+------+-----+----------+----------+

				if (command == 2)
				{
					std::printf("ERROR: cannot BIND to hostname address (only IPv4 or IPv6 addresses)\n");
					close_connection();
					return;
				}

				const int len = boost::uint8_t(m_out_buffer[4]);
				// we already read an address of length 4, assuming it was an IPv4
				// address. Now, with a domain name, one of those bytes was the
				// length-prefix, but we still read 3 bytes already.
				const int additional_bytes = len - 3;
				asio::async_read(m_client_connection, asio::mutable_buffers_1(&m_out_buffer[10], additional_bytes)
					, std::bind(&socks_connection::on_request_domain_name
						, shared_from_this(), _1, _2));
				break;
			}
			case 4: // IPv6 address

// +----+-----+-------+------+----------+----------+
// |VER | CMD |  RSV  | ATYP | BND.ADDR | BND.PORT |
// +----+-----+-------+------+----------+----------+
// | 1  |  1  | X'00' |  1   | 16       |    2     |
// +----+-----+-------+------+----------+----------+

				std::printf("ERROR: unsupported address type %d\n", atyp);
				close_connection();
		}
	}

	void socks_connection::on_request_domain_name(error_code const& ec, size_t bytes_transferred)
	{
		if (ec)
		{
			std::printf("socks_connection::on_request_domain_name(%s): (%d) %s\n"
				, command(), ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		const int buffer_size = 10 + bytes_transferred;

		boost::uint16_t port = m_out_buffer[buffer_size - 2] & 0xff;
		port <<= 8;
		port |= m_out_buffer[buffer_size - 1] & 0xff;

		std::string hostname(&m_out_buffer[5], boost::uint8_t(m_out_buffer[4]));
		std::printf("socks_connection::on_request_domain_name(%s): hostname: %s port: %d\n"
			, command(), hostname.c_str(), port);

		char port_str[10];
		snprintf(port_str, sizeof(port_str), "%d", port);
		asio::ip::tcp::resolver::query q(hostname, port_str);
		m_resolver.async_resolve(q
			, std::bind(&socks_connection::on_request_domain_lookup
				, shared_from_this(), _1, _2));
	}

	void socks_connection::on_request_domain_lookup(boost::system::error_code const& ec
		, asio::ip::tcp::resolver::iterator iter)
	{
		if (ec || iter == asio::ip::tcp::resolver::iterator())
		{
			if (ec)
			{
				std::printf("socks_connection::on_request_domain_lookup(%s): (%d) %s\n"
					, command(), ec.value(), ec.message().c_str());
			}
			else
			{
				std::printf("socks_connection::on_request_domain_lookup(%s): empty response\n"
					, command());
			}

// +----+-----+-------+------+----------+----------+
// |VER | REP |  RSV  | ATYP | BND.ADDR | BND.PORT |
// +----+-----+-------+------+----------+----------+
// | 1  |  1  | X'00' |  1   | Variable |    2     |
// +----+-----+-------+------+----------+----------+

			m_in_buffer[0] = char(m_version); // version
			m_in_buffer[1] = 4; // response (host unreachable)
			m_in_buffer[2] = 0; // reserved
			m_in_buffer[3] = 1; // IPv4
			memset(&m_in_buffer[4], 0, 4);
			m_in_buffer[8] = 0; // port
			m_in_buffer[9] = 0;

			auto self = shared_from_this();
			asio::async_write(m_client_connection
				, asio::const_buffers_1(&m_in_buffer[0], 10)
				, [=](boost::system::error_code const&, size_t)
				{
					self->close_connection();
				});
			return;
		}

		std::printf("socks_connection::on_request_domain_lookup(%s): connecting to: %s port: %d\n"
			, command()
			, iter->endpoint().address().to_string().c_str()
			, iter->endpoint().port());
		open_forward_connection(iter->endpoint());
	}

	void socks_connection::open_forward_connection(asio::ip::tcp::endpoint target)
	{
		std::printf("socks_connection::open_forward_connection(%s): connecting to %s port %d\n"
			, command(), target.address().to_string().c_str(), target.port());

		m_server_connection.open(target.protocol());
		m_server_connection.async_connect(target
			, std::bind(&socks_connection::on_connected, shared_from_this()
				, _1));
	}

	void socks_connection::bind_connection(asio::ip::tcp::endpoint target)
	{
		std::printf("socks_connection::bind_connection(%s): binding to %s port %d\n"
			, command(), target.address().to_string().c_str(), target.port());

		error_code ec;
		m_bind_socket.open(target.protocol(), ec);
		if (ec)
		{
			std::printf("ERROR: open bind socket failed: (%d) %s\n", ec.value()
				, ec.message().c_str());
		}
		else
		{
			m_bind_socket.bind(target, ec);
		}

		int const response = ec
			? (m_version == 4 ? 91 : 1)
			: (m_version == 4 ? 90 : 0);
		int const len = format_response(
			m_bind_socket.local_endpoint(), response);

		if (ec)
		{
			std::printf("ERROR: binding socket to %s %d failed: (%d) %s\n"
				, target.address().to_string().c_str()
				, target.port()
				, ec.value()
				, ec.message().c_str());

			auto self = shared_from_this();

			asio::async_write(m_client_connection
				, asio::const_buffers_1(&m_in_buffer[0], len)
				, [=](boost::system::error_code const&, size_t)
				{
					self->close_connection();
				});
			return;
		}

		// send response
		asio::async_write(m_client_connection
			, asio::const_buffers_1(&m_in_buffer[0], len)
			, std::bind(&socks_connection::start_accept, shared_from_this(), _1));
	}

	void socks_connection::start_accept(boost::system::error_code const& ec)
	{
		if (ec)
		{
			std::printf("socks_connection(%s): error writing to client: (%d) %s\n"
				, command(), ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		m_bind_socket.listen();
		m_bind_socket.async_accept(m_server_connection
			, std::bind(&socks_connection::on_connected
				, shared_from_this(), _1));

		m_client_connection.async_read_some(
			sim::asio::mutable_buffers_1(m_out_buffer, sizeof(m_out_buffer))
			, std::bind(&socks_connection::on_client_receive, shared_from_this()
				, _1, _2));
	}

	int socks_connection::format_response(asio::ip::tcp::endpoint const& ep
		, int response)
	{
		int i = 0;
		if (m_version == 5)
		{
// +----+-----+-------+------+----------+----------+
// |VER | REP |  RSV  | ATYP | BND.ADDR | BND.PORT |
// +----+-----+-------+------+----------+----------+
// | 1  |  1  | X'00' |  1   | Variable |    2     |
// +----+-----+-------+------+----------+----------+

			m_in_buffer[i++] = char(m_version); // version
			m_in_buffer[i++] = char(response); // response
			m_in_buffer[i++] = 0; // reserved
			if (ep.address().is_v4())
			{
				m_in_buffer[i++] = 1; // IPv4
				address_v4::bytes_type b = ep.address().to_v4().to_bytes();
				memcpy(&m_in_buffer[i], &b[0], b.size());
				i += b.size();
			} else {
				m_in_buffer[i++] = 4; // IPv6
				address_v6::bytes_type b = ep.address().to_v6().to_bytes();
				memcpy(&m_in_buffer[i], &b[0], b.size());
				i += b.size();
			}

			m_in_buffer[i++] = ep.port() >> 8;
			m_in_buffer[i++] = ep.port() & 0xff;
		}
		else
		{
			m_in_buffer[i++] = 0; // response version
			m_in_buffer[i++] = char(response); // return code

			assert(ep.address().is_v4());

			m_in_buffer[i++] = ep.port() >> 8;
			m_in_buffer[i++] = ep.port() & 0xff;

			address_v4::bytes_type b = ep.address().to_v4().to_bytes();
			memcpy(&m_in_buffer[i], &b[0], b.size());
			i += b.size();
		}
		return i;
	}

	void socks_connection::on_connected(boost::system::error_code const& ec)
	{
		std::printf("socks_connection(%s): on_connect: (%d) %s\n"
			, command(), ec.value(), ec.message().c_str());

		if (ec == asio::error::operation_aborted
			|| ec == boost::system::errc::bad_file_descriptor)
		{
			return;
		}

		boost::system::error_code err;
		asio::ip::tcp::endpoint const ep = m_server_connection.remote_endpoint(err);
		if (!err)
		{
			std::printf("socks_connection(%s): remote_endpoint: %s %d\n"
				, command(), ep.address().to_string().c_str(), ep.port());
		}

		int const response = ec
			? (m_version == 4 ? 91 : 5)
			: (m_version == 4 ? 90 : 0);
		int const len = format_response(ep, response);

		if (ec)
		{
			std::printf("socks_connection(%s): failed to connect to/accept from target server: (%d) %s\n"
				, command(), ec.value(), ec.message().c_str());

			auto self = shared_from_this();

			asio::async_write(m_client_connection
				, asio::const_buffers_1(&m_in_buffer[0], len)
				, [=](boost::system::error_code const&, size_t)
				{
					self->close_connection();
				});
			return;
		}

		auto self = shared_from_this();

		asio::async_write(m_client_connection
			, asio::const_buffers_1(&m_in_buffer[0], len)
			, [=](boost::system::error_code const& ec, size_t)
			{
				if (ec)
				{
					std::printf("socks_connection(%s): error writing to client: (%d) %s\n"
						, command(), ec.value(), ec.message().c_str());
					return;
				}

			// read from the client and from the server
			self->m_server_connection.async_read_some(
				sim::asio::mutable_buffers_1(m_in_buffer, sizeof(m_in_buffer))
				, std::bind(&socks_connection::on_server_receive, self
					, _1, _2));
			self->m_client_connection.async_read_some(
				sim::asio::mutable_buffers_1(m_out_buffer, sizeof(m_out_buffer))
				, std::bind(&socks_connection::on_client_receive, self
					, _1, _2));
			});
	}

	// we received some data from the client, forward it to the server
	void socks_connection::on_client_receive(boost::system::error_code const& ec
		, std::size_t bytes_transferred)
	{
		// bad file descriptor means the socket has been closed. Whoever closed
		// the socket will have opened a new one, we cannot call
		// close_connection()
		if (ec == asio::error::operation_aborted
			|| ec == boost::system::errc::bad_file_descriptor)
			return;

		if (ec)
		{
			std::printf("socks_connection (%s): error reading from client: (%d) %s\n"
				, command(), ec.value(), ec.message().c_str());
			close_connection();
			return;
		}
		asio::async_write(m_server_connection, asio::const_buffers_1(&m_out_buffer[0], bytes_transferred)
			, std::bind(&socks_connection::on_client_forward, shared_from_this()
				, _1, _2));
	}

	void socks_connection::on_client_forward(error_code const& ec
		, size_t /* bytes_transferred */)
	{
		if (ec)
		{
			std::printf("socks_connection(%s): error writing to server: (%d) %s\n"
				, command(), ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		m_client_connection.async_read_some(
			sim::asio::mutable_buffers_1(m_out_buffer, sizeof(m_out_buffer))
			, std::bind(&socks_connection::on_client_receive, shared_from_this()
				, _1, _2));
	}

	// we received some data from the server, forward it to the server
	void socks_connection::on_server_receive(boost::system::error_code const& ec
		, std::size_t bytes_transferred)
	{
		if (ec)
		{
			std::printf("socks_connection(%s): error reading from server: (%d) %s\n"
				, command(), ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		asio::async_write(m_client_connection, asio::const_buffers_1(&m_in_buffer[0], bytes_transferred)
			, std::bind(&socks_connection::on_server_forward, shared_from_this()
				, _1, _2));
	}

	void socks_connection::on_server_forward(error_code const& ec
		, size_t /* bytes_transferred */)
	{
		if (ec)
		{
			std::printf("socks_connection(%s): error writing to client: (%d) %s\n"
				, command(), ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		m_server_connection.async_read_some(
			sim::asio::mutable_buffers_1(m_in_buffer, sizeof(m_in_buffer))
			, std::bind(&socks_connection::on_server_receive, shared_from_this()
				, _1, _2));
	}

	void socks_connection::close_connection()
	{
		error_code err;
		m_client_connection.close(err);
		if (err)
		{
			std::printf("socks_connection::close: failed to close client connection (%d) %s\n"
				, err.value(), err.message().c_str());
		}
		m_server_connection.close(err);
		if (err)
		{
			std::printf("socks_connection::close: failed to close server connection (%d) %s\n"
				, err.value(), err.message().c_str());
		}

		m_bind_socket.close(err);
		if (err)
		{
			std::printf("socks_connection::close: failed to close bind socket (%d) %s\n"
				, err.value(), err.message().c_str());
		}
	}

	char const* socks_connection::command() const
	{
		switch (m_command)
		{
			case 1: return "CONNECT";
			case 2: return "BIND";
			case 3: return "UDP_ASSOCIATE";
			default: return "UNKNOWN";
		}
	}
}


