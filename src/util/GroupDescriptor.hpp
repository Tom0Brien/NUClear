/*
 * Copyright (C) 2023      Alex Biddulph <bidskii@gmail.com>
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

#ifndef NUCLEAR_UTIL_GROUPDESCRIPTOR_HPP
#define NUCLEAR_UTIL_GROUPDESCRIPTOR_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace NUClear {
namespace util {

    struct GroupDescriptor {
        /// @brief Set a unique identifier for this pool
        uint64_t group_id{0};

        /// @brief The number of threads this thread pool will use.
        size_t thread_count{std::numeric_limits<size_t>::max()};

        static uint64_t get_unique_group_id() {
            // Make group 0 the default group
            static std::atomic<uint64_t> source{1};
            return source++;
        }
    };

}  // namespace util
}  // namespace NUClear

#endif  // NUCLEAR_UTIL_GROUPDESCRIPTOR_HPP
