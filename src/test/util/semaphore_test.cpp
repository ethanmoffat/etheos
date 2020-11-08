#include <gtest/gtest.h>
#include <thread>

#include "util/semaphore.hpp"

using Semaphore = util::Semaphore;

GTEST_TEST(SemaphoreTests, WaitDecrementsCount)
{
    const size_t count = 5;
    Semaphore s(count, count);

    s.Wait();

    ASSERT_EQ(count-1, s.Count());
}

GTEST_TEST(SemaphoreTests, ReleaseIncrementsCount)
{
    const size_t count = 0;
    const size_t maxCount = 5;
    Semaphore s(count, maxCount);

    s.Release();

    ASSERT_EQ(count+1, s.Count());
}

GTEST_TEST(SemaphoreTests, ReleaseDoesNotGoBeyondMaxCount)
{
    const size_t count = 5;
    Semaphore s(count, count);

    s.Release();

    ASSERT_EQ(count, s.Count());
}

GTEST_TEST(SemaphoreTests, WaitFailsIfTimeoutExpires)
{
    const size_t count = 0;
    const size_t maxCount = 5;
    Semaphore s(count, maxCount);

    auto result = s.Wait(std::chrono::milliseconds(50));

    ASSERT_FALSE(result);
}

GTEST_TEST(SemaphoreTests, WaitReleasesWhenSignalled)
{
    const size_t count = 0;
    const size_t maxCount = 5;
    Semaphore s(count, maxCount);

    std::thread signaller([&s]()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        s.Release();
    });

    auto result = s.Wait(std::chrono::milliseconds(1000));

    ASSERT_TRUE(result);
    ASSERT_EQ(count, s.Count());

    signaller.join();
}

GTEST_TEST(SemaphoreTests, MultipleWaitsReleaseWhenSignalled)
{
    const size_t count = 0;
    const size_t maxCount = 5;
    Semaphore s(count, maxCount);

    volatile bool res1 = false, res2 = false;

    std::thread waiter_1([&s, &res1]()
    {
        res1 = s.Wait(std::chrono::seconds(1));
    });

    std::thread waiter_2([&s, &res2]()
    {
        res2 = s.Wait(std::chrono::seconds(1));
    });

    s.Release(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    ASSERT_TRUE(res1);
    ASSERT_TRUE(res2);

    ASSERT_EQ(count, s.Count());

    waiter_1.join();
    waiter_2.join();
}

GTEST_TEST(SemaphoreTests, ResetSignalsAllWaiters)
{
    const size_t count = 0;
    const size_t maxCount = 5;
    Semaphore s(count, maxCount);

    volatile bool res1 = false, res2 = false;

    std::thread waiter_1([&s, &res1]()
    {
        res1 = s.Wait(std::chrono::seconds(1));
    });

    std::thread waiter_2([&s, &res2]()
    {
        res2 = s.Wait(std::chrono::seconds(1));
    });

    s.Reset(maxCount, maxCount*2);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    ASSERT_TRUE(res1);
    ASSERT_TRUE(res2);

    // The waiters will take some of the count released by the call to Reset
    ASSERT_EQ(maxCount - 2, s.Count());
    ASSERT_EQ(maxCount*2, s.MaxCount());

    waiter_1.join();
    waiter_2.join();
}
