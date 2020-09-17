
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

        static void Queue(const WorkFunc workerFunction, const void * state);
        static size_t GetAvailableWorkers();
        static void SetNumThreads(size_t numThreads);

    public:
        ThreadPool(size_t numThreads = 4);
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool(ThreadPool&&) = delete;
        virtual ~ThreadPool();

    private:
        static constexpr size_t MAX_THREADS = 32;

        typedef std::pair<const WorkFunc, const void*> WorkFuncWithState;

        void queueInternal(const WorkFunc workerFunction, const void * state);
        size_t getAvailableWorkersInternal() const { return this->_workReadySemaphore.Count(); }
        void setNumThreadsInternal(size_t numWorkers);

        void _workerProc();

        volatile bool _terminating;

        Semaphore _workReadySemaphore;
        std::mutex _workQueueLock;
        std::queue<WorkFuncWithState> _work;

        std::vector<std::thread> _threads;
    };
}