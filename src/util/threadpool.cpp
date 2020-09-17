
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "threadpool.hpp"

namespace util
{
    static ThreadPool threadPoolInstance;

    void ThreadPool::Queue(const WorkFunc workerFunction, const void* state)
    {
        threadPoolInstance.QueueWork(workerFunction, state);
    }

    size_t ThreadPool::Workers()
    {
        return threadPoolInstance.AvailableWorkers();
    }

    void ThreadPool::SetNumThreads(size_t numThreads)
    {
        threadPoolInstance.SetNumWorkers(numThreads);
    }

    ThreadPool::ThreadPool(size_t numThreads)
        : _terminating(false)
        , _workReadySemaphore(0, numThreads)
    {
        for (size_t i = 0; i < numThreads; i++)
        {
            auto newThread = std::thread([this]() { this->_worker(); });
            this->_threads.push_back(std::move(newThread));
        }
    }

    ThreadPool::~ThreadPool()
    {
        this->_terminating = true;
        this->_workReadySemaphore.Release(this->_threads.size());

        for (auto& thread : this->_threads)
        {
            thread.join();
        }
    }

    void ThreadPool::QueueWork(const ThreadPool::WorkFunc workerFunction, const void* state)
    {
        std::lock_guard<std::mutex> queueGuard(this->_workQueueLock);

        auto newPair = std::make_pair(workerFunction, state);
        this->_work.emplace(newPair);

        this->_workReadySemaphore.Release();
    }

    void ThreadPool::SetNumWorkers(size_t numWorkers)
    {
        if (numWorkers == this->AvailableWorkers())
            return;

        std::lock_guard<std::mutex> queueGuard(this->_workQueueLock);

        if (numWorkers < this->AvailableWorkers())
        {
            while (!this->_work.empty())
                this->_work.pop();

            this->_terminating = true;
            this->_workReadySemaphore.Release(this->_threads.size());

            for (auto& thread : this->_threads)
                thread.join();

            this->_threads.clear();
            this->_terminating = false;

            this->_workReadySemaphore.Reset(0, numWorkers);
        }

        for (size_t i = this->_threads.size(); i < numWorkers; ++i)
        {
            auto newThread = std::thread([this]() { this->_worker(); });
            this->_threads.push_back(std::move(newThread));
        }
    }

    void ThreadPool::_worker()
    {
        while (!this->_terminating)
        {
            this->_workReadySemaphore.Wait();

            if (this->_terminating)
                break;

            std::unique_ptr<WorkFuncWithState> workPair;

            {
                std::lock_guard<std::mutex> queueGuard(this->_workQueueLock);
                workPair = std::make_unique<WorkFuncWithState>(std::move(this->_work.front()));
                this->_work.pop();
            }

            workPair->first(workPair->second);
        }
    }
}