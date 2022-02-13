
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include <future>

#include "../console.hpp"
#include "../database.hpp"
#include "../socket.hpp"
#include "threadpool.hpp"

namespace util
{
    // These are initialized here to allow tests to compile on Ubuntu Linux (g++ 7.4.0)
    // Otherwise, they aren't in the object file in unity build mode and test linking fails
    const size_t ThreadPool::MAX_THREADS = 32;
    const size_t ThreadPool::DEFAULT_THREADS = 4;

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

    void ThreadPool::Shutdown()
    {
        threadPoolInstance.shutdownInternal();
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
        this->shutdownInternal();
    }

    void ThreadPool::queueInternal(const ThreadPool::WorkFunc workerFunction, const void* state)
    {
        if (this->_terminating)
        {
            throw std::runtime_error("Unable to queue work while ThreadPool is terminating");
        }

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

        if (this->_terminating)
        {
            throw std::runtime_error("Unable to set number of threads while ThreadPool is terminating");
        }

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

    void ThreadPool::shutdownInternal()
    {
        if (!this->_terminating)
        {
            this->_terminating = true;
            this->_workReadySemaphore.Release(this->_threads.size());

            for (auto& thread : this->_threads)
            {
                thread.join();
            }
        }
    }

    void ThreadPool::_workerProc(size_t threadNum)
    {
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

            try
            {
                workPair->first(workPair->second);
            }
            catch (const Socket_Exception& se)
            {
                Console::Err("Exception on thread %d: %s: %s", threadNum, se.what(), se.error());
            }
            catch (const Database_Exception& dbe)
            {
                Console::Err("Exception on thread %d: %s: %s", threadNum, dbe.what(), dbe.error());
            }
            catch (const std::exception& e)
            {
                Console::Err("Exception on thread %d: %s", threadNum, e.what());
            }

#if DEBUG
            Console::Dbg("Thread %d completed work", threadNum);
#endif
        }

#if DEBUG
        Console::Dbg("Thread %d terminating", threadNum);
#endif
    }
}