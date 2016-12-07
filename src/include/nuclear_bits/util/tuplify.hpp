/*
 * Copyright (C) 2013-2016 Trent Houliston <trent@houliston.me>, Jake Woods <jake.f.woods@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef NUCLEAR_UTIL_TUPLIFY_HPP
#define NUCLEAR_UTIL_TUPLIFY_HPP

#include <tuple>

namespace NUClear {
namespace util {

	template <typename TData>
	static inline std::tuple<TData> tuplify(TData&& data) {
		return std::make_tuple(std::move(data));
	}

	template <typename... TElements>
	static inline std::tuple<TElements...> tuplify(std::tuple<TElements...>&& tuple) {
		return tuple;
	}

	template <typename TFirst, typename TSecond, typename... TElements>
	static inline std::tuple<TFirst, TSecond, TElements...> detuplify(
		std::tuple<TFirst, TSecond, TElements...>&& tuple) {
		return tuple;
	}

	template <typename TData>
	static inline TData detuplify(std::tuple<TData>&& tuple) {
		return std::move(std::get<0>(tuple));
	}

}  // namespace util
}  //  namespace NUClear

#endif  // NUCLEAR_UTIL_TUPLIFY_HPP
