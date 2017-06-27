/*

Copyright (c) 2017, Arvid Norberg
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

#ifndef SIMULATOR_FUNCTION_HPP_INCLUDED
#define SIMULATOR_FUNCTION_HPP_INCLUDED

#include <utility>

namespace sim {
namespace aux {

	// this is a std::function-like class that supports move-only function
	// objects
	template <typename R, typename... A>
	struct callable
	{
		virtual R call(A&&...) = 0;
		virtual ~callable() {}
	};

	template <typename Callable, typename R, typename... A>
	struct function_impl : callable<R, A...>
	{
		function_impl(Callable c) : m_callable(std::move(c)) {}
		R call(A&&... a) override
		{
			return m_callable(std::forward<A>(a)...);
		}
	private:
		Callable m_callable;
	};

	template <typename Fun>
	struct function;

	template <typename R, typename... A>
	struct function<R(A...)>
	{
		using result_type = R;

		template <typename C>
		function(C c)
			: m_callable(new function_impl<C, R, A...>(std::move(c)))
		{}
		function(function&&) = default;
		function& operator=(function&&) = default;

		// boost.asio requires handlers to be copy-constructible, but it will move
		// them, if they're movable. So we trick asio into accepting this handler.
		// If it attempts to copy, it will cause a link error
		function(function const&)
#ifndef _MSC_VER
		;
#else
		= default;
#endif
		function& operator=(function const&) = delete;

		function() = default;
		explicit operator bool() const { return bool(m_callable); }
		function& operator=(std::nullptr_t) { m_callable.reset(); return *this; }
		void clear() { m_callable.reset(); }
		R operator()(A... a)
		{
			assert(m_callable);
			return m_callable->call(std::forward<A>(a)...);
		}
	private:
#ifdef _MSC_VER
		// as of msvc-14.1, there's still terrible move support
		std::shared_ptr<callable<R, A...>> m_callable;
#else
		std::unique_ptr<callable<R, A...>> m_callable;
#endif
	};

	// index sequence, to unpack tuple
	template<std::size_t...> struct seq {};
	template<std::size_t N, std::size_t... S> struct gens : gens<N-1, N-1, S...> {};
	template<std::size_t... S> struct gens<0, S...> { using type = seq<S...>; };

	// a binder for move-only types, and movable arguments. It's not a general
	// binder as it doesn't support partial application, it just binds all
	// arguments and ignores any arguments passed to the call
	template <typename Callable, typename R, typename... A>
	struct move_binder
	{
		move_binder(Callable c, A... a)
			: m_args(std::move(a)...)
			, m_callable(std::move(c))
		{}

		move_binder(move_binder const&) = delete;
		move_binder& operator=(move_binder const&) = delete;

		move_binder(move_binder&&) = default;
		move_binder& operator=(move_binder&&) = default;

		// ignore any arguments passed in. This is used to ignore an error_code
		// argument for instance
		template <typename... Args>
		R operator()(Args...)
		{
			return call(typename gens<sizeof...(A)>::type());
		}

	private:

		template<std::size_t... I>
		R call(seq<I...>)
		{
			return m_callable(std::move(std::get<I>(m_args))...);
		}
		std::tuple<A...> m_args;
		Callable m_callable;
	};

	template <typename R, typename C, typename... A>
	move_binder<C, R, A...> move_bind(C c, A... a)
	{
		return move_binder<C, R, A...>(std::move(c), std::forward<A>(a)...);
	}

}
}

#endif

