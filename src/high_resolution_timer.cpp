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
#include "simulator/handler_allocator.hpp"

#include <functional>
#include <boost/system/error_code.hpp>

namespace sim
{

	namespace asio {

	high_resolution_timer::high_resolution_timer(io_service& io_service)
		: m_expiration_time(time_type())
		, m_io_service(&io_service)
		, m_expired(true)
	{
	}

	high_resolution_timer::high_resolution_timer(io_service& io_service,
		const time_type& expiry_time)
		: m_expiration_time(time_type())
		, m_io_service(&io_service)
		, m_expired(true)
	{
		expires_at(expiry_time);
	}

	high_resolution_timer::high_resolution_timer(io_service& io_service,
		const duration_type& expiry_time)
		: m_expiration_time(time_type())
		, m_io_service(&io_service)
		, m_expired(true)
	{
		expires_from_now(expiry_time);
	}

	std::size_t high_resolution_timer::cancel(boost::system::error_code& ec)
	{
		ec.clear();
		if (m_expired) return 0;
		m_expired = true;
		m_io_service->remove_timer(this);
		if (!m_handler) return 0;
		try
		{
			fire(boost::asio::error::operation_aborted);
		}
		catch (std::bad_alloc const&)
		{
			ec = boost::asio::error::no_memory;
			return 0;
		}
		catch (std::exception const&)
		{
			ec = make_error_code(boost::system::errc::operation_canceled);
			return 0;
		}
		return 1;
	}

	std::size_t high_resolution_timer::cancel()
	{
		if (m_expired) return 0;
		m_expired = true;
		m_io_service->remove_timer(this);
		if (!m_handler) return 0;
		fire(boost::asio::error::operation_aborted);
		return 1;
	}

	std::size_t high_resolution_timer::cancel_one()
	{
		// TODO: support multiple handlers
		return cancel();
	}

	std::size_t high_resolution_timer::cancel_one(boost::system::error_code& ec)
	{
		// TODO: support multiple handlers
		return cancel(ec);
	}

	high_resolution_timer::time_type high_resolution_timer::expires_at() const
	{ return m_expiration_time; }

	std::size_t high_resolution_timer::expires_at(high_resolution_timer::time_type const& expiry_time)
	{
		boost::system::error_code ec;
		return expires_at(expiry_time, ec);
	}

	std::size_t high_resolution_timer::expires_at(high_resolution_timer::time_type const& expiry_time, boost::system::error_code& ec)
	{
		ec.clear();
		std::size_t ret = cancel(ec);
		m_expiration_time = expiry_time;
		m_expired = false;
		m_io_service->add_timer(this);
		return ret;
	}

	high_resolution_timer::duration_type high_resolution_timer::expires_from_now() const
	{
		return m_expiration_time - chrono::high_resolution_clock::now();
	}

	std::size_t high_resolution_timer::expires_from_now(const duration_type& expiry_time)
	{
		boost::system::error_code ec;
		return expires_from_now(expiry_time, ec);
	}

	std::size_t high_resolution_timer::expires_from_now(const duration_type& expiry_time
		, boost::system::error_code& ec)
	{
		ec.clear();
		std::size_t ret = cancel(ec);
		m_expiration_time = chrono::high_resolution_clock::now() + expiry_time;
		m_expired = false;
		m_io_service->add_timer(this);
		return ret;
	}

	void high_resolution_timer::wait()
	{
		assert(false);
		boost::system::error_code ec;
		wait(ec);
	}

	void high_resolution_timer::wait(boost::system::error_code&)
	{
		assert(false);
		time_type now = chrono::high_resolution_clock::now();
		if (now >= m_expiration_time) return;
		chrono::high_resolution_clock::fast_forward(m_expiration_time - now);
	}

	void high_resolution_timer::async_wait(aux::function<void(boost::system::error_code const&)> handler)
	{
		// TODO: support multiple handlers
		assert(!m_handler);
		m_handler = std::move(handler);
		if (m_expired)
		{
			fire(boost::system::error_code());
			return;
		}
	}

	void high_resolution_timer::fire(boost::system::error_code ec)
	{
		m_expired = true;
		if (!m_handler) return;
		auto h = std::move(m_handler);
		m_handler = nullptr;
		m_io_service->post(make_malloc(std::bind(std::move(h), ec)));
	}

	} // asio

} // sim

