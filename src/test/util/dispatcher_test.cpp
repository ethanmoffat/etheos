#include "util/dispatcher.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

GTEST_TEST(DispatcherTests, Invoke_RunsOnlyOnRunPending)
{
    bool ran = false;

    util::Dispatcher::Invoke([&ran]() { ran = true; });
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

    for (int i = 0; i < 3; ++i)
        util::Dispatcher::Invoke([&order, i]() { order.push_back(i); });

    util::Dispatcher::RunPending();

    EXPECT_EQ((std::vector<int>{0, 1, 2}), order);
}

GTEST_TEST(DispatcherTests, Invoke_DuringRunPending_WaitsForNextRun)
{
    bool nestedRan = false;

    util::Dispatcher::Invoke([&nestedRan]() { util::Dispatcher::Invoke([&nestedRan]() { nestedRan = true; }); });

    util::Dispatcher::RunPending();
    EXPECT_FALSE(nestedRan);

    util::Dispatcher::RunPending();
    EXPECT_TRUE(nestedRan);
}
