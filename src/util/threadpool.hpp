
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <queue>
#include <thread>
#include <vector>

#include "semaphore.hpp"

namespace util
{
    class ThreadPool
    {
    public:
        typedef std::function<void(const void*)> WorkFunc;

        // Queue work on the thread pool. Memory allocated and passed to 'state' must be freed by the caller.
        static void Queue(const WorkFunc workerFunction, const void * state);

        // Set the number of threads in the thread pool. Any queued work will be removed. In-progress work will be allowed to complete.
        static void SetNumThreads(size_t numThreads);

    public:
        ThreadPool(size_t numThreads = DEFAULT_THREADS);
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool(ThreadPool&&) = delete;
        virtual ~ThreadPool();

        static constexpr size_t MAX_THREADS = 32;
        static constexpr size_t DEFAULT_THREADS = 4;

    private:
        typedef std::pair<const WorkFunc, const void*> WorkFuncWithState;
        void _workerProc();

    protected:
        void queueInternal(const WorkFunc workerFunction, const void * state);
        void setNumThreadsInternal(size_t numWorkers);

        volatile bool _terminating;

        Semaphore _workReadySemaphore;
        std::mutex _workQueueLock;
        std::queue<WorkFuncWithState> _work;

        std::vector<std::thread> _threads;
    };
}