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

#ifndef NUCLEAR_DSL_WORD_POOL_HPP
#define NUCLEAR_DSL_WORD_POOL_HPP

#include <map>
#include <mutex>
#include <typeindex>

#include "../../threading/ReactionTask.hpp"
#include "../../util/ThreadPoolDescriptor.hpp"

namespace NUClear {
namespace dsl {
    namespace word {

        /**
         * @brief
         *  This is used to specify that this reaction should run in the designated thread pool
         *
         * @details
         *  @code on<Trigger<T, ...>, Pool<PoolType>>() @endcode
         *  This DSL will cause the creation of a new thread pool with a specific number of threads allocated to it.
         *
         *  All tasks for this reaction will be queued to run on threads from this thread pool.
         *
         *  Tasks in the queue are ordered based on their priority level, then their task id.
         *
         *  When this DSL is not specified the default thread pool will be used. For tasks that need to run on the main
         *  thread use MainThread.
         *
         * @attention
         *  This DSL should be used sparingly as having an increased number of threads running concurrently on the
         *  system can lead to a degradation in performance.
         *
         * @par Implements
         *  pool
         *
         * @tparam PoolType
         *  A struct that contains the details of the thread pool to create. This struct should contain a static int
         *  member that sets the number of threads that should be allocated to this pool.
         *  @code
         *  struct ThreadPool {
         *      static const int thread_count = 2;
         *  };
         *  @endcode
         *  While it is valid to have a non-const static thread count in the struct, NUClear will not look at any
         *  changes to this value. Only the first value that NUClear sees will be used to initiate the thread pool.
         */
        template <typename PoolType>
        struct Pool {

            static_assert(PoolType::thread_count > 0, "Can not have a thread pool with less than 1 thread");

            static const util::ThreadPoolDescriptor pool_descriptor;

            template <typename DSL>
            static inline util::ThreadPoolDescriptor pool(threading::Reaction& /*reaction*/) {
                return pool_descriptor;
            }
        };

        // Initialise the thread pool descriptor
        template <typename PoolType>
        const util::ThreadPoolDescriptor Pool<PoolType>::pool_descriptor = {
            util::ThreadPoolDescriptor::get_unique_pool_id(),
            PoolType::thread_count};

    }  // namespace word
}  // namespace dsl
}  // namespace NUClear

#endif  // NUCLEAR_DSL_WORD_POOL_HPP
