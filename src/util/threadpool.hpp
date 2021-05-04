
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

        // Shut down the threadpool
        static void Shutdown();

    public:
        ThreadPool(size_t numThreads = DEFAULT_THREADS);
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool(ThreadPool&&) = delete;
        virtual ~ThreadPool();

        static const size_t MAX_THREADS;
        static const size_t DEFAULT_THREADS;

    private:
        typedef std::pair<const WorkFunc, const void*> WorkFuncWithState;

    protected:
        void queueInternal(const WorkFunc workerFunction, const void * state);
        void setNumThreadsInternal(size_t numWorkers);
        void shutdownInternal();

        void _workerProc(size_t threadNum);

        volatile bool _terminating;

        Semaphore _workReadySemaphore;
        std::mutex _workQueueLock;
        std::queue<WorkFuncWithState> _work;

        std::vector<std::thread> _threads;
    };
}