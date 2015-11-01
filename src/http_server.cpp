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
#include "simulator/http_server.hpp"

#include <functional>

using namespace sim::asio;
using namespace sim::asio::ip;
using namespace std::placeholders;

using boost::system::error_code;

namespace sim
{
	using namespace aux;

	namespace
	{
		std::string trim(std::string s)
		{
			if (s.empty()) return s;

			int start = 0;
			int end = s.size();
			while (strchr(" \r\n\t", s[start]) != NULL && start < end)
			{
				++start;
			}

			while (strchr(" \r\n\t", s[end-1]) != NULL && end > start)
			{
				--end;
			}
			return s.substr(start, end - start);
		}

		std::string lower_case(std::string s)
		{
			std::string ret;
			std::transform(s.begin(), s.end(), std::back_inserter(ret)
				, [](char c) { return tolower(c); } );
			return ret;
		}

		std::string normalize(std::string s)
		{
			std::vector<std::string> elements;
			char const* start = s.c_str();
			if (*start == '/') ++start;
			char const* slash = strchr(start, '/');
			while (slash != NULL)
			{
				std::string element(start, slash - start);
				if (element != "..")
				{
					elements.push_back(element);
				} else if (!elements.empty())
				{
					elements.erase(elements.end()-1);
				}
				start = slash + 1;
				slash = strchr(start, '/');
			}
			elements.push_back(start);

			std::string ret;
			for (auto const& e : elements)
			{
				ret += '/';
				ret += e;
			}

			return ret;
		}
	}

	std::string send_response(int code, char const* status_message
		, int len, char const** extra_header)
	{
		char msg[600];
		int pkt_len = snprintf(msg, sizeof(msg), "HTTP/1.1 %d %s\r\n"
			"content-length: %d\r\n"
			"%s"
			"%s"
			"%s"
			"%s"
			"\r\n"
			, code, status_message, len
			, extra_header ? extra_header[0] : ""
			, extra_header ? extra_header[1] : ""
			, extra_header ? extra_header[2] : ""
			, extra_header ? extra_header[3] : "");
		return std::string(msg, pkt_len);
	}

	http_server::http_server(io_service& ios, int listen_port, int flags)
		: m_ios(ios)
		, m_listen_socket(ios)
		, m_connection(ios)
		, m_bytes_used(0)
		, m_close(false)
		, m_flags(flags)
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

		m_listen_socket.async_accept(m_connection, m_ep
			, std::bind(&http_server::on_accept, this, _1));
	}

	void http_server::on_accept(error_code const& ec)
	{
		if (ec)
		{
			printf("http_server::on_accept: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		printf("http_server accepted connection from: %s : %d\n",
			m_ep.address().to_string().c_str(), m_ep.port());

		read();
	}

	void http_server::register_handler(std::string path, handler_t const& h)
	{
		m_handlers[path] = h;
	}

	void http_server::read()
	{
		if (m_bytes_used >= int(m_recv_buffer.size()) / 2)
		{
			m_recv_buffer.resize((std::max)(500, m_bytes_used * 2));
		}
		assert(int(m_recv_buffer.size()) > m_bytes_used);
		m_connection.async_read_some(asio::mutable_buffers_1(&m_recv_buffer[m_bytes_used]
				, m_recv_buffer.size() - m_bytes_used)
			, std::bind(&http_server::on_read, this, _1, _2));
	}

	void http_server::on_read(error_code const& ec, size_t bytes_transferred)
	{
		if (ec)
		{
			printf("http_server::on_read: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		m_bytes_used += bytes_transferred;

		char const* end_of_request = strstr(m_recv_buffer.c_str(), "\r\n\r\n");
		if (end_of_request == NULL)
		{
			read();
			return;
		}
		const int req_len = end_of_request - m_recv_buffer.c_str() + 4;

		char const* space = strstr(m_recv_buffer.c_str(), " ");
		if (space == NULL)
		{
			printf("http_server: failed to parse request:\n%s\n"
				, m_recv_buffer.c_str());
			close_connection();
			return;
		}

		char const* space2 = strstr(space + 1, " ");
		if (space2 == NULL)
		{
			printf("http_server: failed to parse request:\n%s\n"
				, m_recv_buffer.c_str());
			close_connection();
			return;
		}
		std::string method(m_recv_buffer.c_str(), space);
		std::string req(space+1, space2);
		std::string path(normalize(req.substr(0, req.find_first_of('?'))));
		printf("http_server: incoming request: %s %s [%s]\n"
			, method.c_str(), path.c_str(), req.c_str());

		std::map<std::string, std::string> headers;

		char const* header = strstr(space2, "\r\n");
		while (header != end_of_request)
		{
			if (header == NULL)
			{
				printf("http_server: failed to parse request:\n%s\n"
					, m_recv_buffer.c_str());
				close_connection();
				return;
			}
			char const* next = strstr(header + 2, "\r\n");
			char const* value = strchr(header, ':');
			if (value == NULL || next == NULL || value > next)
			{
				printf("http_server: failed to parse request:\n%s\n"
					, m_recv_buffer.c_str());
				close_connection();
				return;
			}

			headers[lower_case(trim(std::string(header, value)))]
				= trim(std::string(value+1, next));

			header = next;
		}

		auto it = m_handlers.find(path);
		if (it == m_handlers.end())
		{
			// no handler found, 404
			m_send_buffer = send_response(404, "Not Found");
		}
		else
		{
			m_send_buffer = it->second(method, req, headers);
		}

		m_recv_buffer.erase(m_recv_buffer.begin(), m_recv_buffer.begin() + req_len);

		bool close = lower_case(headers["connection"]) == "close";

		async_write(m_connection, asio::const_buffers_1(m_send_buffer.data()
			, m_send_buffer.size()), std::bind(&http_server::on_write
			, this, _1, _2, close));
	}

	void http_server::on_write(error_code const& ec, size_t bytes_transferred
		, bool close)
	{
		if (ec)
		{
			printf("http_server::on_write: (%d) %s\n"
				, ec.value(), ec.message().c_str());
			close_connection();
			return;
		}

		if (!close && (m_flags & keep_alive))
		{
			// try to read another request out of the buffer
			m_ios.post(std::bind(&http_server::on_read, this, error_code(), 0));
		}
		else
		{
			close_connection();
		}
	}

	void http_server::stop()
	{
		m_close = true;
		m_listen_socket.close();
	}

	void http_server::close_connection()
	{
		m_recv_buffer.clear();
		m_bytes_used = 0;

		error_code err;
		m_connection.close(err);
		if (err)
		{
			printf("http_server::close: failed to close connection (%d) %s\n"
				, err.value(), err.message().c_str());
			return;
		}

		if (m_close) return;

		// now we can accept another connection
		m_listen_socket.async_accept(m_connection, m_ep
			, std::bind(&http_server::on_accept, this, _1));
	}
}

