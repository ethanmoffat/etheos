
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "../console.hpp"
#include "threadpool.hpp"

namespace util
{
    // There should really only be a single thread pool per application
    static ThreadPool threadPoolInstance;

    void ThreadPool::Queue(const WorkFunc workerFunction, const void* state)
    {
        threadPoolInstance.queueInternal(workerFunction, state);
    }

    void ThreadPool::SetNumThreads(size_t numThreads)
    {
        threadPoolInstance.setNumThreadsInternal(numThreads);
    }

    ThreadPool::ThreadPool(size_t numThreads)
        : _terminating(false)
        , _workReadySemaphore(0)
    {
        if (numThreads == 0 || numThreads > MAX_THREADS)
        {
            numThreads = DEFAULT_THREADS;
        }

        for (size_t i = 0; i < numThreads; i++)
        {
            auto newThread = std::thread([this, i]() { this->_workerProc(i); });
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

    void ThreadPool::queueInternal(const ThreadPool::WorkFunc workerFunction, const void* state)
    {
        std::lock_guard<std::mutex> queueGuard(this->_workQueueLock);

        auto newPair = std::make_pair(workerFunction, state);
        this->_work.emplace(std::move(newPair));

        this->_workReadySemaphore.Release();
    }

    void ThreadPool::setNumThreadsInternal(size_t numWorkers)
    {
        if (numWorkers == 0 || numWorkers > MAX_THREADS)
        {
            numWorkers = DEFAULT_THREADS;
        }

        if (numWorkers == this->_threads.size())
            return;

        std::lock_guard<std::mutex> queueGuard(this->_workQueueLock);

        if (numWorkers < this->_threads.size())
        {
            while (!this->_work.empty())
                this->_work.pop();

            this->_terminating = true;
            this->_workReadySemaphore.Release(this->_threads.size());

            for (auto& thread : this->_threads)
                thread.join();

            this->_threads.clear();
            this->_terminating = false;
        }

        this->_workReadySemaphore.Reset(0);

        for (size_t i = this->_threads.size(); i < numWorkers; ++i)
        {
            auto newThread = std::thread([this, i]() { this->_workerProc(i); });
            this->_threads.push_back(std::move(newThread));
        }
    }

    void ThreadPool::_workerProc(size_t threadNum)
    {
#if !DEBUG
        (void)threadNum;
#endif

        while (!this->_terminating)
        {
            this->_workReadySemaphore.Wait();

#if DEBUG
            Console::Dbg("Thread %d starting work", threadNum);
#endif

            if (this->_terminating)
                break;

            std::unique_ptr<WorkFuncWithState> workPair;

            {
                std::lock_guard<std::mutex> queueGuard(this->_workQueueLock);
                workPair = std::make_unique<WorkFuncWithState>(std::move(this->_work.front()));
                this->_work.pop();
            }

            workPair->first(workPair->second);

#if DEBUG
            Console::Dbg("Thread %d completed work", threadNum);
#endif
        }

#if DEBUG
        Console::Dbg("Thread %d terminating", threadNum);
#endif
    }
}