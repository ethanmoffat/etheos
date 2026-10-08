#include "util/affinity.hpp"
#include "util/dispatcher.hpp"

#include "../testhelper/threads.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

GTEST_TEST(DispatcherTests, Invoke_MainThread_RunsImmediately)
{
    bool ran = false;

    util::Dispatcher::Invoke([&ran]() { ran = true; });

    EXPECT_TRUE(ran);
}

GTEST_TEST(DispatcherTests, Invoke_FromOtherThread_RunsOnlyOnRunPending)
{
    bool ran = false;

    std::thread([&ran]() { util::Dispatcher::Invoke([&ran]() { ran = true; }); }).join();
    EXPECT_FALSE(ran);

    util::Dispatcher::RunPending();
    EXPECT_TRUE(ran);
}

GTEST_TEST(DispatcherTests, Invoke_FromOtherThread_RunsOnCallingThread)
{
    std::thread::id actionThread;

    std::thread([&actionThread]() { util::Dispatcher::Invoke([&actionThread]() { actionThread = std::this_thread::get_id(); }); }).join();
    util::Dispatcher::RunPending();

    EXPECT_EQ(std::this_thread::get_id(), actionThread);
}

GTEST_TEST(DispatcherTests, RunPending_RunsInOrder)
{
    std::vector<int> order;

    std::thread([&order]()
    {
        for (int i = 0; i < 3; ++i)
            util::Dispatcher::Invoke([&order, i]() { order.push_back(i); });
    }).join();

    util::Dispatcher::RunPending();

    EXPECT_EQ((std::vector<int>{0, 1, 2}), order);
}

GTEST_TEST(DispatcherTests, Invoke_DuringRunPending_RunsImmediately)
{
    bool nestedRan = false;
    bool nestedRanBeforeOuterReturned = false;

    std::thread([&nestedRan, &nestedRanBeforeOuterReturned]()
    {
        util::Dispatcher::Invoke([&nestedRan, &nestedRanBeforeOuterReturned]()
        {
            util::Dispatcher::Invoke([&nestedRan]() { nestedRan = true; });
            nestedRanBeforeOuterReturned = nestedRan;
        });
    }).join();

    util::Dispatcher::RunPending();

    EXPECT_TRUE(nestedRanBeforeOuterReturned);
}

GTEST_TEST(DispatcherTests, RunPending_OtherThread_ThrowsThreadAffinityViolation)
{
    auto thrown = RunOffMainThread([]() { util::Dispatcher::RunPending(); });

    ASSERT_TRUE(thrown != nullptr);
    EXPECT_THROW(std::rethrow_exception(thrown), util::ThreadAffinityViolation);
}
