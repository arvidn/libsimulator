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

typedef sim::chrono::high_resolution_clock::time_point time_point;
typedef sim::chrono::high_resolution_clock::duration duration;

using namespace std::placeholders;

namespace sim {
namespace asio {
namespace ip {

	template<typename Protocol>
	basic_resolver<Protocol>::basic_resolver(io_service& ios)
		: m_ios(ios)
		, m_timer(ios)
	{}

	template<typename Protocol>
	void basic_resolver<Protocol>::async_resolve(basic_resolver_query<Protocol> q,
		boost::function<void(boost::system::error_code const&
			, basic_resolver_iterator<Protocol>)> handler)
	{
		std::vector<asio::ip::address> result;
		boost::system::error_code ec;

		const chrono::high_resolution_clock::time_point start_time =
			m_queue.empty() ? chrono::high_resolution_clock::now() :
			m_queue.front().completion_time;

		assert(!m_ios.get_ips().empty() && "internal io service objects can only "
			"be used for timers");

		// if the hostname is an IP address, resolve it immediately
		asio::ip::address addr = asio::ip::address_v4::from_string(q.host_name(), ec);
		if (ec) addr = asio::ip::address_v6::from_string(q.host_name(), ec);
		if (!ec)
		{
			const chrono::high_resolution_clock::time_point t = chrono::high_resolution_clock::now()
				+ chrono::microseconds(1);
			basic_resolver_iterator<Protocol> iter;
			int const port = atoi(q.service_name().c_str());
			assert(port >= 0 && port <= 0xffff)
			iter.m_idx = 0;
			iter.m_results.emplace_back(
				typename Protocol::endpoint(addr, static_cast<unsigned short>(port))
				, q.host_name()
				, q.service_name());
			result_t res = {t, ec, iter, handler };
			m_queue.insert(m_queue.begin(), res);
			m_timer.expires_at(m_queue.front().completion_time);
			m_timer.async_wait(std::bind(&basic_resolver::on_lookup, this, _1));
			return;
		}
		ec.clear();

		const chrono::high_resolution_clock::time_point completion_time =
			start_time
			+ m_ios.sim().config().hostname_lookup(m_ios.get_ips().front(), q.host_name()
				, result, ec);

		basic_resolver_iterator<Protocol> iter;

		int const port = atoi(q.service_name().c_str());
		assert(port >= 0 && port <= 0xffff)

		iter.m_idx = 0;
		for (auto const& ip : result)
		{
			iter.m_results.emplace_back(
				typename Protocol::endpoint(ip, static_cast<unsigned short>(port))
				, q.host_name()
				, q.service_name());
		}

		m_queue.push_back({completion_time, ec, iter, handler });

		m_timer.expires_at(m_queue.front().completion_time);
		m_timer.async_wait(std::bind(&basic_resolver::on_lookup, this, _1));
	}

	template<typename Protocol>
	void basic_resolver<Protocol>::on_lookup(boost::system::error_code const& ec)
	{
		if (ec == asio::error::operation_aborted) return;

		if (m_queue.empty()) return;

		typename queue_t::value_type v = m_queue.front();
		m_queue.erase(m_queue.begin());

		v.handler(v.err, v.iter);

		if (m_queue.empty()) return;

		m_timer.expires_at(m_queue.front().completion_time);
		m_timer.async_wait(std::bind(&basic_resolver::on_lookup, this, _1));
	}

	template<typename Protocol>
	void basic_resolver<Protocol>::cancel()
	{
		queue_t q;
		m_queue.swap(q);
		for (auto& r : q)
		{
			r.err = asio::error::operation_aborted;
			r.iter = basic_resolver_iterator<Protocol>();
			m_timer.get_io_service().post(std::bind(r.handler
				, r.err
				, r.iter));
		}
	}

	// explicitly instantiate the functions
	template struct basic_resolver<udp>;
	template struct basic_resolver<tcp>;
}
}
}

