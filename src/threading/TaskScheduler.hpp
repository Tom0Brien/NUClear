/*
 * Copyright (C) 2013      Trent Houliston <trent@houliston.me>, Jake Woods <jake.f.woods@gmail.com>
 *               2014-2017 Trent Houliston <trent@houliston.me>
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

#ifndef NUCLEAR_THREADING_TASKSCHEDULER_HPP
#define NUCLEAR_THREADING_TASKSCHEDULER_HPP

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "../util/GroupDescriptor.hpp"
#include "../util/ThreadPoolDescriptor.hpp"
#include "Reaction.hpp"
#include "ReactionTask.hpp"

namespace NUClear {
namespace threading {

    /**
     * @brief This class is responsible for scheduling tasks and distributing them amongst threads.
     *
     * @details
     * PRIORITY
     *      what priority this task should run with
     *      tasks are ordered by priority -> creation order
     * POOL
     *      which thread pool this task should execute in
     *      0 being execute on the main thread
     *      1 being the default pool
     *      Work out how to create other pools later (fold in always into this?)
     * GROUP
     *      which grouping this task belongs to for concurrency (default to the 0 group)
     * CONCURRENCY
     *      only run if there are less than this many tasks running in this group
     * INLINE
     *      if the submitter of this task should wait until this task is finished before returning (for DIRECT
     * emits)
     *
     *  @em Priority
     *  @code Priority<P> @endcode
     *  When a priority is encountered, the task will be scheduled to execute based on this. If one of the three
     * normal options are specified (HIGH, DEFAULT and LOW), then within the specified Sync group, it will run
     * before, normally or after other reactions.
     *  @attention Note that if Priority<REALTIME> is specified, the Sync type is ignored (Single is not).
     *
     *  @em Sync
     *  @code Sync<SyncGroup> @endcode
     *  When a Sync type is encountered, the system uses this as a compile time mutex flag. It will not allow two
     *  callbacks with the same Sync type to execute at the same time. It will effectively ensure that all of the
     *  callbacks with this type run in sequence with each other, rather then in parallel. It is also important to
     * note again, that if the priority of a task is realtime, it will ignore Sync groups.
     *
     *  @em Single
     *  @code Single @endcode
     *  If single is encountered while processing the function, and a Task object for this Reaction is already
     * running in a thread, or waiting in the Queue, then this task is ignored and dropped from the system.
     */
    class TaskScheduler {
    public:
        /**
         * @brief Constructs a new TaskScheduler instance, and builds the nullptr sync queue.
         */
        TaskScheduler();

        void start(const size_t& thread_count);

        /**
         * @brief
         *  Shuts down the scheduler, all waiting threads are woken, and any attempt to get a task results in an
         *  exception
         */
        void shutdown();

        /**
         * @brief Submit a new task to be executed to the Scheduler.
         *
         * @details
         *  This method submits a new task to the scheduler. This task will then be sorted into the appropriate
         *  queue based on it's sync type and priority. It will then wait there until it is removed by a thread to
         *  be processed.
         *
         * @param task  the task to be executed
         */
        void submit(std::unique_ptr<ReactionTask>&& task);

    private:
        /**
         * @brief Get a task object to be executed by a thread.
         *
         * @details
         *  This method will get a task object to be executed from the queue. It will block until such a time as a
         *  task is available to be executed. For example, if a task with a particular sync type was out, then this
         *  thread would block until that sync type was no longer out, and then it would take a task.
         *
         * @return the task which has been given to be executed
         */
        std::unique_ptr<ReactionTask> get_task(const uint64_t& pool_id);

        /**
         * @brief Creates a new thread pool and ensures threads and a task queue are allocated for it
         *
         * @param pool the descriptor for the thread pool to create
         */
        void create_pool(const util::ThreadPoolDescriptor& pool);

        /**
         * @brief The function that each thread runs
         *
         * @details This function will repeatedly query the task queue for new a task to run and then execute that task
         *
         * @param pool the thread pool to run from and the task queue to get tasks from
         */
        void pool_func(const util::ThreadPoolDescriptor& pool);

        /**
         * @brief Start all threads for the given thread pool
         */
        void start_threads(const util::ThreadPoolDescriptor& pool);

        /**
         * @brief Execute the given task
         *
         * @details After execution of the task has completed the number of active tasks in the tasks' group is
         * decremented
         */
        void run_task(std::unique_ptr<ReactionTask>&& task);

        /**
         * @brief Determines if the given task is able to be executed
         *
         * @details If the current thread is able to be executed the number of active tasks in the tasks' groups is
         * incremented
         *
         * @param task the task to inspect
         * @param pool_id the pool to run the task on
         * @return true if the task is currently runnable
         * @return false if the task is not currently runnable
         */
        bool is_runnable(const std::unique_ptr<ReactionTask>& task, const uint64_t& pool_id);

        /// @brief if the scheduler is running, and accepting new tasks. If this is false and a new, non-immediate, task
        /// is submitted it will be ignored
        std::atomic<bool> running{true};
        /// @brief if the scheduler has been started. This is set to true after a call to start is made. Once this is
        /// set to true all threads will begin executing tasks from the tasks queue
        std::atomic<bool> started{false};

        /// @brief A task queue for each thread pool. In each queue, each task is ordered by priority and then by task
        /// id
        std::map<uint64_t, std::vector<std::unique_ptr<ReactionTask>>> queue;

        /// @brief A map of group ids to the number of active tasks currently running in that group
        std::map<uint64_t, size_t> groups{};

        /// @brief the mutex which our threads synchronize their access to the task queues and the group concurrency
        /// counts
        std::mutex queue_mutex;
        /// @brief the condition object that threads wait on if they can't get a task
        std::condition_variable queue_condition;

        /// @brief A vector of the running threads in the system
        std::vector<std::unique_ptr<std::thread>> threads;
        /// @brief A map of pool descriptor ids to pool descriptors
        std::map<uint64_t, util::ThreadPoolDescriptor> pools{};
        /// @brief A map of thread ids to the pools they belong to
        std::map<std::thread::id, uint64_t> pool_map{};
        /// @brief the mutex which our threads synchronize their access to the thread pool maps and the threads list
        std::mutex pool_mutex;
    };

}  // namespace threading
}  // namespace NUClear

#endif  // NUCLEAR_THREADING_TASKSCHEDULER_HPP
