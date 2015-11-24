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

using namespace sim::asio;
using namespace sim::asio::ip;
using namespace std::placeholders;

using boost::system::error_code;

namespace sim
{
	using namespace aux;

	socks_server::socks_server(io_service& ios, int listen_port, int version)
		: m_ios(ios)
		, m_resolver(ios)
		, m_listen_socket(ios)
		, m_client_connection(ios)
		, m_server_connection(ios)
		, m_num_out_bytes(0)
		, m_num_in_bytes(0)
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

		m_listen_socket.async_accept(m_client_connection, m_ep
			, std::bind(&socks_server::on_accept, this, _1));
	}

	void socks_server::on_accept(error_code const& ec)
	{
		if (ec == asio::error::operation_aborted)
			return;

		if (ec)
		{
			printf("socks_server::on_accept: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		printf("socks_server accepted connection from: %s : %d\n",
			m_ep.address().to_string().c_str(), m_ep.port());

		if (m_version == 4)
		{
			asio::async_read(m_client_connection, asio::mutable_buffers_1(&m_out_buffer[0], 9)
				, std::bind(&socks_server::on_request1, this, _1, _2));
		} else {
			// read protocol version and number of auth-methods
			asio::async_read(m_client_connection, asio::mutable_buffers_1(&m_out_buffer[0], 2)
				, std::bind(&socks_server::on_handshake1, this, _1, _2));
		}
	}

	void socks_server::on_handshake1(error_code const& ec, size_t bytes_transferred)
	{
		if (ec || bytes_transferred != 2)
		{
			printf("socks_server::on_handshake1: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		if (m_out_buffer[0] != 4 && m_out_buffer[0] != 5)
		{
			printf("socks_server::on_handshake1: unexpected socks protocol version: %d"
				, int(m_out_buffer[0]));
			close_connection();
			return;
		}

		int num_methods = unsigned(m_out_buffer[1]);

		// read list of auth-methods
		asio::async_read(m_client_connection, asio::mutable_buffers_1(&m_out_buffer[0],
				num_methods)
			, std::bind(&socks_server::on_handshake2, this, _1, _2));
	}

	void socks_server::on_handshake2(error_code const& ec, size_t bytes_transferred)
	{
		if (ec)
		{
			printf("socks_server::on_handshake2: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		if (std::count(m_out_buffer, m_out_buffer + bytes_transferred, 0) == 0)
		{
			printf("socks_server: could not find auth-method 0 (no-auth) in socks handshake\n");
			close_connection();
			return;
		}

		m_in_buffer[0] = 5; // socks version
		m_in_buffer[1] = 0; // auth-method (no-auth)

		asio::async_write(m_client_connection, asio::const_buffers_1(&m_in_buffer[0], 2)
			, std::bind(&socks_server::on_handshake3, this, _1, _2));
	}

	void socks_server::on_handshake3(error_code const& ec, size_t bytes_transferred)
	{
		if (ec || bytes_transferred != 2)
		{
			printf("socks_server::on_handshake3: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		asio::async_read(m_client_connection, asio::mutable_buffers_1(&m_out_buffer[0], 10)
			, std::bind(&socks_server::on_request1, this, _1, _2));
	}

	void socks_server::on_request1(error_code const& ec, size_t bytes_transferred)
	{
		const int expected = m_version == 4 ? 9 : 10;
		if (ec || bytes_transferred != expected)
		{
			printf("socks_server::on_request1: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

// +----+-----+-------+------+----------+----------+
// |VER | CMD |  RSV  | ATYP | DST.ADDR | DST.PORT |
// +----+-----+-------+------+----------+----------+
// | 1  |  1  | X'00' |  1   | Variable |    2     |
// +----+-----+-------+------+----------+----------+

		int version = m_out_buffer[0];

		if (version != m_version)
		{
			printf("socks_server::on_request1: unexpected socks protocol version: %d expected: %d"
				, int(m_out_buffer[0]), m_version);
			close_connection();
			return;
		}

		if (m_version == 4)
		{
			if (m_out_buffer[1] != 1)
			{
				printf("socks_server::on_request1: unexpected socks command: %d"
					, int(m_out_buffer[1]));
				close_connection();
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
				printf("socks_server::on_request1: username in SOCKS4 mode not supported");
				close_connection();
				return;
			}
			asio::ip::tcp::endpoint target(asio::ip::address_v4(addr), port);
			open_forward_connection(target);

			return;
		}

		if (m_out_buffer[2] != 0)
		{
			printf("socks_server::on_request1: reserved byte is non-zero: %d"
				, int(m_out_buffer[2]));
			close_connection();
			return;
		}

		int atyp = unsigned(m_out_buffer[3]);

		if (atyp != 1 && atyp != 3 && atyp != 4)
		{
			printf("socks_server::on_request1: unexpected address type in SOCKS request: %d"
				, atyp);
			close_connection();
			return;
		}

		printf("socks_server: received request address type: %d\n", atyp);

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
				open_forward_connection(target);

				break;
			}
			case 3: { // domain name

// +----+-----+-------+------+-----+----------+----------+
// |VER | CMD |  RSV  | ATYP | LEN | BND.ADDR | BND.PORT |
// +----+-----+-------+------+-----+----------+----------+
// | 1  |  1  | X'00' |  1   | 1   | Variable |    2     |
// +----+-----+-------+------+-----+----------+----------+

				const int len = boost::uint8_t(m_out_buffer[4]);
				// we already read an address of length 4, assuming it was an IPv4
				// address. Now, with a domain name, one of those bytes was the
				// length-prefix, but we still read 3 bytes already.
				const int additional_bytes = len - 3;
				asio::async_read(m_client_connection, asio::mutable_buffers_1(&m_out_buffer[10], additional_bytes)
					, std::bind(&socks_server::on_request_domain_name, this, _1, _2));
				break;
			}
			case 4: // IPv6 address

// +----+-----+-------+------+----------+----------+
// |VER | CMD |  RSV  | ATYP | BND.ADDR | BND.PORT |
// +----+-----+-------+------+----------+----------+
// | 1  |  1  | X'00' |  1   | 16       |    2     |
// +----+-----+-------+------+----------+----------+

				printf("ERROR: unsupported address type %d\n", atyp);
				close_connection();
		}
	}

	void socks_server::on_request_domain_name(error_code const& ec, size_t bytes_transferred)
	{
		if (ec)
		{
			printf("socks_server::on_request_domain_name: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		const int buffer_size = 10 + bytes_transferred;

		boost::uint16_t port = m_out_buffer[buffer_size - 2] & 0xff;
		port <<= 8;
		port |= m_out_buffer[buffer_size - 1] & 0xff;

		std::string hostname(&m_out_buffer[5], boost::uint8_t(m_out_buffer[4]));
		printf("socks_server::on_request_domain_name: hostname: %s port: %d\n"
			, hostname.c_str(), port);

		asio::ip::tcp::resolver::query q(hostname, std::to_string(port).c_str());
		m_resolver.async_resolve(q
			, std::bind(&socks_server::on_request_domain_lookup, this, _1, _2));
	}

	void socks_server::on_request_domain_lookup(boost::system::error_code const& ec
		, asio::ip::tcp::resolver::iterator iter)
	{
		if (ec || iter == asio::ip::tcp::resolver::iterator())
		{
			if (ec)
			{
				printf("socks_server::on_request_domain_lookup: (%d) %s\n"
					, ec.value(), ec.message().c_str());
			}
			else
			{
				printf("socks_server::on_request_domain_lookup: empty response\n");
			}

// +----+-----+-------+------+----------+----------+
// |VER | REP |  RSV  | ATYP | BND.ADDR | BND.PORT |
// +----+-----+-------+------+----------+----------+
// | 1  |  1  | X'00' |  1   | Variable |    2     |
// +----+-----+-------+------+----------+----------+

			m_in_buffer[0] = m_version; // version
			m_in_buffer[1] = 4; // response (host unreachable)
			m_in_buffer[2] = 0; // reserved
			m_in_buffer[3] = 1; // IPv4
			memset(&m_in_buffer[4], 0, 4);
			m_in_buffer[8] = 0; // port
			m_in_buffer[9] = 0;

			asio::async_write(m_client_connection
				, asio::const_buffers_1(&m_in_buffer[0], 10)
				, [=](boost::system::error_code const& ec, size_t)
				{
					this->close_connection();
				});
			return;
		}

		printf("socks_server::on_request_domain_lookup: connecting to: %s port: %d\n"
			, iter->endpoint().address().to_string().c_str()
			, iter->endpoint().port());
		open_forward_connection(iter->endpoint());
	}

	void socks_server::open_forward_connection(asio::ip::tcp::endpoint target)
	{
		m_server_connection.open(target.protocol());
		m_server_connection.async_connect(target
			, std::bind(&socks_server::on_connected, this, _1));
	}

	void socks_server::on_connected(boost::system::error_code const& ec)
	{

		int i = 0;
		if (m_version == 5)
		{
// +----+-----+-------+------+----------+----------+
// |VER | REP |  RSV  | ATYP | BND.ADDR | BND.PORT |
// +----+-----+-------+------+----------+----------+
// | 1  |  1  | X'00' |  1   | Variable |    2     |
// +----+-----+-------+------+----------+----------+

			m_in_buffer[i++] = m_version; // version
			++i; // response
			m_in_buffer[i++] = 0; // reserved
			asio::ip::tcp::endpoint local_ep = m_server_connection.local_endpoint();
			if (local_ep.address().is_v4())
			{
				m_in_buffer[i++] = 1; // IPv4
				address_v4::bytes_type b = local_ep.address().to_v4().to_bytes();
				memcpy(&m_in_buffer[i], &b[0], b.size());
				i += b.size();
			} else {
				m_in_buffer[i++] = 4; // IPv6
				address_v6::bytes_type b = local_ep.address().to_v6().to_bytes();
				memcpy(&m_in_buffer[i], &b[0], b.size());
				i += b.size();
			}

			m_in_buffer[i++] = local_ep.port() >> 8;
			m_in_buffer[i++] = local_ep.port() & 0xff;
		}
		else
		{
			m_in_buffer[i++] = 0; // response version
			++i; // response

			asio::ip::tcp::endpoint local_ep = m_server_connection.local_endpoint();
			assert(local_ep.address().is_v4());

			m_in_buffer[i++] = local_ep.port() >> 8;
			m_in_buffer[i++] = local_ep.port() & 0xff;

			address_v4::bytes_type b = local_ep.address().to_v4().to_bytes();
			memcpy(&m_in_buffer[i], &b[0], b.size());
			i += b.size();
		}

		if (ec)
		{
			m_in_buffer[1] = m_version == 4 ? 91 : 5; // connection refused
			printf("socks_server: failed to connect to target server: (%d) %s\n"
				, ec.value(), ec.message().c_str());

			asio::async_write(m_client_connection
				, asio::const_buffers_1(&m_in_buffer[0], i)
				, [=](boost::system::error_code const& ec, size_t)
				{
					this->close_connection();
				});
			return;
		}

		m_in_buffer[1] = m_version == 4 ? 90 : 0; // OK

		asio::async_write(m_client_connection
			, asio::const_buffers_1(&m_in_buffer[0], i)
			, [=](boost::system::error_code const& ec, size_t)
			{
				if (ec)
				{
					printf("socks_server: error writing to client: (%d) %s\n"
						, ec.value(), ec.message().c_str());
					return;
				}

			// read from the client and from the server
			m_server_connection.async_read_some(
				sim::asio::mutable_buffers_1(m_in_buffer, sizeof(m_in_buffer))
				, std::bind(&socks_server::on_server_receive, this, _1, _2));
			m_client_connection.async_read_some(
				sim::asio::mutable_buffers_1(m_out_buffer, sizeof(m_out_buffer))
				, std::bind(&socks_server::on_client_receive, this, _1, _2));
			});
	}

	// we received some data from the client, forward it to the server
	void socks_server::on_client_receive(boost::system::error_code const& ec
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
			printf("socks_server: error reading from client: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			close_connection();
			return;
		}
		asio::async_write(m_server_connection, asio::const_buffers_1(&m_out_buffer[0], bytes_transferred)
			, std::bind(&socks_server::on_client_forward, this, _1, _2));
	}

	void socks_server::on_client_forward(error_code const& ec, size_t bytes_transferred)
	{
		if (ec)
		{
			printf("socks_server: error writing to server: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		m_client_connection.async_read_some(
			sim::asio::mutable_buffers_1(m_out_buffer, sizeof(m_out_buffer))
			, std::bind(&socks_server::on_client_receive, this, _1, _2));
	}

	// we received some data from the server, forward it to the server
	void socks_server::on_server_receive(boost::system::error_code const& ec
		, std::size_t bytes_transferred)
	{
		if (ec)
		{
			printf("socks_server: error reading from server: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		asio::async_write(m_client_connection, asio::const_buffers_1(&m_in_buffer[0], bytes_transferred)
			, std::bind(&socks_server::on_server_forward, this, _1, _2));
	}

	void socks_server::on_server_forward(error_code const& ec, size_t bytes_transferred)
	{
		if (ec)
		{
			printf("socks_server: error writing to client: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		m_server_connection.async_read_some(
			sim::asio::mutable_buffers_1(m_in_buffer, sizeof(m_in_buffer))
			, std::bind(&socks_server::on_server_receive, this, _1, _2));
	}

	void socks_server::stop()
	{
		m_close = true;
		m_listen_socket.close();
	}

	void socks_server::close_connection()
	{
		m_num_out_bytes = 0;
		m_num_in_bytes = 0;

		error_code err;
		m_client_connection.close(err);
		if (err)
		{
			printf("socks_server::close: failed to close client connection (%d) %s\n"
				, err.value(), err.message().c_str());
		}
		m_server_connection.close(err);
		if (err)
		{
			printf("socks_server::close: failed to close server connection (%d) %s\n"
				, err.value(), err.message().c_str());
		}

		if (m_close) return;

		// now we can accept another connection
		m_listen_socket.async_accept(m_client_connection, m_ep
			, std::bind(&socks_server::on_accept, this, _1));
	}
}


