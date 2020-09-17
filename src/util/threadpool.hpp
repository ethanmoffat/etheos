
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#pragma once

#include <condition_variable>
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
        typedef void(*WorkFunc)(const void*);

        static void Queue(const WorkFunc workerFunction, const void* state);
        static size_t Workers();
        static void SetNumThreads(size_t numThreads);

    public:
        ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool(ThreadPool&&) = delete;
        virtual ~ThreadPool();

    private:
        typedef std::pair<const WorkFunc, const void*> WorkFuncWithState;

        void QueueWork(const WorkFunc workerFunction, const void* state);
        size_t AvailableWorkers() const { return this->_workReadySemaphore.Count(); }
        void SetNumWorkers(size_t numWorkers);

        void _worker();

        volatile bool _terminating;

        Semaphore _workReadySemaphore;
        std::mutex _workQueueLock;
        std::queue<WorkFuncWithState> _work;

        std::vector<std::thread> _threads;
    };
}