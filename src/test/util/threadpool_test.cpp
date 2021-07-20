#include <gtest/gtest.h>

#include "util/semaphore.hpp"
#include "util/threadpool.hpp"

using Semaphore = util::Semaphore;
using ThreadPool = util::ThreadPool;

#define SLEEP_MS(x) std::this_thread::sleep_for(std::chrono::milliseconds(x))

// Testing class that allows the thread pool to be instanced
// Prevents cross-test access from causing exceptions/hangs
class TestThreadPool : public ThreadPool
{
public:
    TestThreadPool(size_t numThreads = 4)
        : ThreadPool(numThreads) { }

    void QueueWork(const util::ThreadPool::WorkFunc workFunc, const void * state)
    {
        this->queueInternal(workFunc, state);
    }

    size_t GetNumThreads() const { return this->_threads.size(); }

    bool IsShutdown() const { return this->_workReadySemaphore.Count() == this->_threads.size() && this->_terminating; }

    void SetNumThreads(size_t numThreads)
    {
        this->setNumThreadsInternal(numThreads);
    }

    void Shutdown()
    {
        this->shutdownInternal();
    }

    // Allow for tests to force threadpool threads to join so tests don't hang
    void JoinAll()
    {
        this->Shutdown();
        this->_threads.clear();
    }
};

GTEST_TEST(ThreadPoolTests, ZeroThreadsUsesDefault)
{
    TestThreadPool t(0);
    ASSERT_EQ(ThreadPool::DEFAULT_THREADS, t.GetNumThreads());
}

GTEST_TEST(ThreadPoolTests, GreaterThanMaxThreadsUsesDefault)
{
    {
        TestThreadPool t(ThreadPool::MAX_THREADS);
        ASSERT_EQ(ThreadPool::MAX_THREADS, t.GetNumThreads());
    }

    {
        TestThreadPool t(ThreadPool::MAX_THREADS + 1);
        ASSERT_EQ(ThreadPool::DEFAULT_THREADS, t.GetNumThreads());
    }
}

GTEST_TEST(ThreadPoolTests, QueueDoesWork)
{
    volatile bool done = false;
    auto workFunc = [&done](const void * state)
    {
        (void)state;
        SLEEP_MS(100);
        done = true;
    };

    ThreadPool::Queue(workFunc, nullptr);
    SLEEP_MS(200);

    ASSERT_TRUE(done);
}

GTEST_TEST(ThreadPoolTests, QueueManyDoesAllWork)
{
    const size_t numThreads = 10;

    std::vector<bool> results;
    results.resize(numThreads, false);

    Semaphore workDone(0, numThreads);

    ASSERT_EQ(numThreads, results.size());

    auto workFunc = [&results, &workDone](const void * state)
    {
        auto ndx = reinterpret_cast<const size_t*>(state);
        results[*ndx] = true;
        workDone.Release();
        delete ndx;
    };

    for (size_t i = 0; i < numThreads; ++i)
        ThreadPool::Queue(workFunc, new size_t(i));

    while (workDone.Count() < workDone.MaxCount())
        SLEEP_MS(500);

    for (size_t i = 0; i < numThreads; ++i)
    {
        ASSERT_TRUE(results[i]) << "Expected work to be completed, but was not (result " << i << ")";
    }
}

GTEST_TEST(ThreadPoolTests, QueueRespectsMaxThreads)
{
    const size_t defaultMaxThreads = 4;

    TestThreadPool testThreadPool(defaultMaxThreads);

    Semaphore s(0, defaultMaxThreads);
    volatile unsigned workCounter = 0;

    auto workFunc = [&workCounter, &s](const void * state)
    {
        (void)state;
        workCounter++;
        s.Wait(std::chrono::milliseconds(1000));
    };

    for (size_t i = 0; i < defaultMaxThreads + 1; i++)
    {
        testThreadPool.QueueWork(workFunc, nullptr);
    }

    SLEEP_MS(200);
    ASSERT_EQ(workCounter, defaultMaxThreads) << "Expected work counter to match the maximum number of threads";

    // Release should allow one of the queued workers to complete
    s.Release();

    SLEEP_MS(100);
    ASSERT_EQ(workCounter, defaultMaxThreads+1) << "Expected work counter to increase when allowing another thread to work";

    s.Release(defaultMaxThreads);
    testThreadPool.JoinAll();
}

GTEST_TEST(ThreadPoolTests, ResizeToZeroUsesDefault)
{
    TestThreadPool t;
    t.SetNumThreads(0);
    ASSERT_EQ(ThreadPool::DEFAULT_THREADS, t.GetNumThreads());
}

GTEST_TEST(ThreadPoolTests, ResizeToGreaterThanMaxUsesDefault)
{
    {
        TestThreadPool t;
        t.SetNumThreads(ThreadPool::MAX_THREADS);
        ASSERT_EQ(ThreadPool::MAX_THREADS, t.GetNumThreads());
    }

    {
        TestThreadPool t;
        t.SetNumThreads(ThreadPool::MAX_THREADS+1);
        ASSERT_EQ(ThreadPool::DEFAULT_THREADS, t.GetNumThreads());
    }
}

GTEST_TEST(ThreadPoolTests, ResizeLessThreadsReducesThreadPoolSize)
{
    const size_t defaultMaxThreads = 4;
    const size_t newThreadPoolSize = 1;

    TestThreadPool testThreadPool(defaultMaxThreads);
    testThreadPool.SetNumThreads(newThreadPoolSize);

    Semaphore s(0);
    volatile unsigned workCounter = 0;

    auto workFunc = [&workCounter, &s](const void * state)
    {
        (void)state;
        workCounter++;
        s.Wait(std::chrono::milliseconds(1000));
    };

    for (size_t i = 0; i < defaultMaxThreads + 1; i++)
    {
        testThreadPool.QueueWork(workFunc, nullptr);
    }

    SLEEP_MS(100);
    ASSERT_EQ(workCounter, newThreadPoolSize) << "Expected work counter to match decreased threadpool size";

    // Release should allow one of the queued workers to complete
    s.Release();

    SLEEP_MS(100);
    ASSERT_EQ(workCounter, newThreadPoolSize+1) << "Expected work counter to increment by one";

    s.Release(defaultMaxThreads);

    SLEEP_MS(100);
    ASSERT_EQ(workCounter, defaultMaxThreads+1) << "Expected work counter to match number of queued work procs";
    testThreadPool.JoinAll();
}

GTEST_TEST(ThreadPoolTests, ResizeMoreThreadsIncreasesThreadPoolSize)
{
    const size_t defaultMaxThreads = 4;
    const size_t newThreadPoolSize = 8;

    TestThreadPool testThreadPool(defaultMaxThreads);
    testThreadPool.SetNumThreads(newThreadPoolSize);

    Semaphore s(0);
    volatile unsigned workCounter = 0;

    auto workFunc = [&workCounter, &s](const void * state)
    {
        (void)state;
        workCounter++;
        s.Wait(std::chrono::milliseconds(1000));
    };

    for (size_t i = 0; i < newThreadPoolSize; i++)
    {
        testThreadPool.QueueWork(workFunc, nullptr);
    }

    SLEEP_MS(100);
    ASSERT_EQ(workCounter, newThreadPoolSize) << "Expected work counter to match increased threadpool size";

    s.Release(newThreadPoolSize);

    testThreadPool.JoinAll();
}

GTEST_TEST(ThreadPoolTests, ShutdownAllowsWorkToComplete)
{
    const size_t defaultMaxThreads = 1;

    TestThreadPool testThreadPool(defaultMaxThreads);

    volatile unsigned workCounter = 0;

    auto workFunc = [&workCounter](const void * state)
    {
        (void)state;
        SLEEP_MS(1000);
        workCounter++;
    };

    testThreadPool.QueueWork(workFunc, nullptr);
    SLEEP_MS(100);

    testThreadPool.Shutdown();
    ASSERT_TRUE(testThreadPool.IsShutdown()) << "Expected threadpool to be shutdown but it was not";
    ASSERT_EQ(workCounter, 1) << "Expected work counter to indicate threadpool task had completed";

    testThreadPool.JoinAll();
}

GTEST_TEST(ThreadPoolTests, ShutdownPreventsStateChanges)
{
    const size_t defaultMaxThreads = 1;

    TestThreadPool testThreadPool(defaultMaxThreads);

    volatile unsigned workCounter = 0;

    auto workFunc = [&workCounter](const void * state)
    {
        (void)state;
        SLEEP_MS(1000);
        workCounter++;
    };

    testThreadPool.QueueWork(workFunc, nullptr);
    testThreadPool.Shutdown();

    ASSERT_THROW(testThreadPool.QueueWork([](const void * state) { (void)state; }, nullptr), std::runtime_error) << "Expected exception when queuing ThreadPool work during shutdown";
    ASSERT_THROW(testThreadPool.SetNumThreads(defaultMaxThreads + 1), std::runtime_error) << "Expected exception when resizing ThreadPool during shutdown";
    ASSERT_NO_THROW(testThreadPool.SetNumThreads(defaultMaxThreads)) << "Expected no exception when setting same number of threads during shutdown";

    testThreadPool.JoinAll();
}
