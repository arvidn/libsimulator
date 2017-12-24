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

#ifndef HANDLER_ALLOCATOR_HPP_INCLUDED
#define HANDLER_ALLOCATOR_HPP_INCLUDED

namespace sim
{
namespace aux
{

// this is a handler wrapper that customizes the asio handler allocator to use
// malloc instead of new. The purpose is to distinguish allocations that are
// internal to the simulator and allocations part of the program under test.
template <typename Handler>
struct malloc_allocator
{
	malloc_allocator(Handler h) : m_handler(std::move(h)) {}

	template <typename... Args>
	void operator()(Args&&... a)
	{
		m_handler(std::forward<Args>(a)...);
	}

	friend void* asio_handler_allocate(
		std::size_t size, malloc_allocator<Handler>*)
	{
		return std::malloc(size);
	}

	friend void asio_handler_deallocate(
		void* ptr, std::size_t, malloc_allocator<Handler>*)
	{
		std::free(ptr);
	}

private:
	Handler m_handler;
};

template <typename T>
malloc_allocator<T> make_malloc(T h)
{
	return malloc_allocator<T>(std::move(h));
}

}
}

#endif

