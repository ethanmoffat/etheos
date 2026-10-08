#include "util/async.hpp"
#include "util/affinity.hpp"
#include "util/dispatcher.hpp"

#include "../testhelper/threads.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    struct TestContext
    {
        std::optional<util::AsyncKey> key;
        std::vector<std::string> events;
    };

    struct PlainContext { };

    struct DestructionProbe
    {
        std::thread::id* destroyedOn;
        ~DestructionProbe() { *this->destroyedOn = std::this_thread::get_id(); }
    };
}

template<> struct util::AsyncContext<TestContext>
{
    static std::optional<AsyncKey> Key(const TestContext& context) { return context.key; }
    static void OnError(TestContext& context, const std::string& error) { context.events.push_back("context error: " + error); }
};

namespace
{
    using TestOperation = AsyncOperation<TestContext, int, bool>;

    constexpr util::AsyncKey TestKey = 1;

    void WaitForWork(const std::atomic<bool>& done)
    {
        for (int i = 0; i < 100 && !done; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // The completion is dispatched just after the operation returns
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    void RunPendingUntil(const bool& done)
    {
        for (int i = 0; i < 100 && !done; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            util::Dispatcher::RunPending();
        }
    }
}

GTEST_TEST(AsyncOperationTests, Execute_WorkDone_CallbacksWaitForRunPending)
{
    TestContext context;
    std::atomic<bool> workDone(false);
    TestContext* successContext = nullptr;
    std::thread::id callbackThread;

    TestOperation::Create(&context, [&workDone](std::shared_ptr<int>) { workDone = true; return true; }, true)
        ->OnSuccess([&successContext, &callbackThread](TestContext* c) { successContext = c; callbackThread = std::this_thread::get_id(); })
        ->Execute(std::make_shared<int>(0));

    WaitForWork(workDone);
    EXPECT_EQ(nullptr, successContext);

    util::Dispatcher::RunPending();

    EXPECT_EQ(&context, successContext);
    EXPECT_EQ(std::this_thread::get_id(), callbackThread);
}

GTEST_TEST(AsyncOperationTests, Execute_WorkDone_DestroyedOnMainThreadAfterRunPending)
{
    TestContext context;
    std::atomic<bool> workDone(false);
    std::thread::id destroyedOn;
    std::weak_ptr<TestOperation> weak;

    {
        auto probe = std::make_shared<DestructionProbe>(DestructionProbe{&destroyedOn});
        auto operation = TestOperation::Create(&context, [&workDone](std::shared_ptr<int>) { workDone = true; return true; }, true);
        weak = operation;
        operation->OnComplete([probe]() { })->Execute(std::make_shared<int>(0));
    }

    WaitForWork(workDone);
    EXPECT_FALSE(weak.expired());

    util::Dispatcher::RunPending();

    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(std::this_thread::get_id(), destroyedOn);
}

GTEST_TEST(AsyncOperationTests, Execute_Failure_RunsFailureAndCompleteCallbacks)
{
    TestContext context;
    int failureResult = 0;
    bool completed = false;

    AsyncOperation<TestContext, int, int>::Create(&context, [](std::shared_ptr<int> state) { return *state; })
        ->OnSuccess([](TestContext*) { FAIL() << "success callback ran"; })
        ->OnFailure([&failureResult](TestContext*, int result) { failureResult = result; })
        ->OnComplete([&completed]() { completed = true; })
        ->Execute(std::make_shared<int>(7));

    RunPendingUntil(completed);

    EXPECT_EQ(7, failureResult);
}

GTEST_TEST(AsyncOperationTests, Execute_OperationThrows_ReportsToContextAndRunsCompleteCallbacks)
{
    TestContext context;
    bool completed = false;

    TestOperation::Create(&context, [](std::shared_ptr<int>) -> bool { throw std::runtime_error("database down"); }, true)
        ->OnSuccess([](TestContext*) { FAIL() << "success callback ran"; })
        ->OnFailure([](TestContext*, bool) { FAIL() << "failure callback ran"; })
        ->OnComplete([&completed]() { completed = true; })
        ->Execute(std::make_shared<int>(0));

    RunPendingUntil(completed);

    EXPECT_EQ(std::vector<std::string>{"context error: database down"}, context.events);
}

GTEST_TEST(AsyncOperationTests, Execute_CallbackThrows_ReportsOnceAndSkipsRemainingCallbacks)
{
    TestContext context{TestKey, {}};
    std::atomic<bool> workDone(false);
    bool completed = false;
    bool laterActionRan = false;

    TestOperation::Create(&context, [&workDone](std::shared_ptr<int>) { workDone = true; return true; }, true)
        ->OnSuccess([](TestContext*) { throw std::runtime_error("callback failed"); })
        ->OnSuccess([](TestContext*) { FAIL() << "success callback after the throwing one ran"; })
        ->OnComplete([&completed]() { completed = true; })
        ->Execute(std::make_shared<int>(0));

    WaitForWork(workDone);
    std::thread([&laterActionRan]() { util::Dispatcher::Invoke([&laterActionRan]() { laterActionRan = true; }); }).join();

    EXPECT_NO_THROW(util::Dispatcher::RunPending());

    EXPECT_EQ(std::vector<std::string>{"context error: callback failed"}, context.events);
    EXPECT_TRUE(completed);
    EXPECT_FALSE(util::AsyncKeys::IsPending(TestKey));
    EXPECT_TRUE(laterActionRan);
}

GTEST_TEST(AsyncOperationTests, FromResult_FailureCallbackThrows_ReportsToContextAndRunsCompleteCallbacks)
{
    TestContext context;
    bool completed = false;

    AsyncOperation<TestContext, int, int>::FromResult(3, &context)
        ->OnFailure([](TestContext*, int) { throw 42; })
        ->OnComplete([&completed]() { completed = true; })
        ->Execute(std::make_shared<int>(0));

    EXPECT_EQ(std::vector<std::string>{"context error: unknown exception"}, context.events);
    EXPECT_TRUE(completed);
}

GTEST_TEST(AsyncOperationTests, Execute_CompleteCallbacksThrow_ReportsEachAndRunsTheRest)
{
    TestContext context{TestKey, {}};
    std::atomic<bool> workDone(false);
    bool lastCompleted = false;
    bool laterActionRan = false;

    TestOperation::Create(&context, [&workDone](std::shared_ptr<int>) { workDone = true; return true; }, true)
        ->OnComplete([]() { throw std::runtime_error("first"); })
        ->OnComplete([]() { throw std::runtime_error("second"); })
        ->OnComplete([&lastCompleted]() { lastCompleted = true; })
        ->Execute(std::make_shared<int>(0));

    WaitForWork(workDone);
    std::thread([&laterActionRan]() { util::Dispatcher::Invoke([&laterActionRan]() { laterActionRan = true; }); }).join();

    EXPECT_NO_THROW(util::Dispatcher::RunPending());

    const std::vector<std::string> expected{"context error: first", "context error: second"};
    EXPECT_EQ(expected, context.events);
    EXPECT_TRUE(lastCompleted);
    EXPECT_TRUE(laterActionRan);
}

GTEST_TEST(AsyncOperationTests, Execute_KeyedContext_PendingUntilCompletion)
{
    TestContext context{TestKey, {}};
    std::atomic<bool> workDone(false);
    bool keyPendingInCallback = true;

    TestOperation::Create(&context, [&workDone](std::shared_ptr<int>) { workDone = true; return true; }, true)
        ->OnSuccess([&keyPendingInCallback](TestContext*) { keyPendingInCallback = util::AsyncKeys::IsPending(TestKey); })
        ->Execute(std::make_shared<int>(0));

    WaitForWork(workDone);
    EXPECT_TRUE(util::AsyncKeys::IsPending(TestKey));

    util::Dispatcher::RunPending();

    EXPECT_FALSE(util::AsyncKeys::IsPending(TestKey));
    EXPECT_FALSE(keyPendingInCallback);
}

GTEST_TEST(AsyncOperationTests, Execute_KeyAlreadyPending_Throws)
{
    TestContext context{TestKey, {}};
    bool completed = false;

    TestOperation::Create(&context, [](std::shared_ptr<int>) { return true; }, true)
        ->OnComplete([&completed]() { completed = true; })
        ->Execute(std::make_shared<int>(0));

    std::weak_ptr<TestOperation> second;

    {
        auto operation = TestOperation::Create(&context, [](std::shared_ptr<int>) { return true; }, true);
        second = operation;
        EXPECT_THROW(operation->Execute(std::make_shared<int>(0)), std::runtime_error);
    }

    EXPECT_TRUE(second.expired());

    RunPendingUntil(completed);
}

GTEST_TEST(AsyncOperationTests, Execute_DifferentKeys_BothRun)
{
    TestContext first{TestKey, {}}, second{TestKey + 1, {}};
    bool firstCompleted = false, secondCompleted = false;

    TestOperation::Create(&first, [](std::shared_ptr<int>) { return true; }, true)
        ->OnComplete([&firstCompleted]() { firstCompleted = true; })
        ->Execute(std::make_shared<int>(0));

    TestOperation::Create(&second, [](std::shared_ptr<int>) { return true; }, true)
        ->OnComplete([&secondCompleted]() { secondCompleted = true; })
        ->Execute(std::make_shared<int>(0));

    RunPendingUntil(firstCompleted);
    RunPendingUntil(secondCompleted);

    EXPECT_TRUE(firstCompleted);
    EXPECT_TRUE(secondCompleted);
}

GTEST_TEST(AsyncOperationTests, Execute_ContextWithoutPolicy_RunsConcurrentlyAndIgnoresErrors)
{
    PlainContext context;
    bool firstCompleted = false, secondCompleted = false;

    AsyncOperation<PlainContext, int, bool>::Create(&context, [](std::shared_ptr<int>) { return true; }, true)
        ->OnComplete([&firstCompleted]() { firstCompleted = true; })
        ->Execute(std::make_shared<int>(0));

    AsyncOperation<PlainContext, int, bool>::Create(&context, [](std::shared_ptr<int>) -> bool { throw std::runtime_error("failed"); }, true)
        ->OnComplete([&secondCompleted]() { secondCompleted = true; })
        ->Execute(std::make_shared<int>(0));

    RunPendingUntil(firstCompleted);
    RunPendingUntil(secondCompleted);

    EXPECT_TRUE(firstCompleted);
    EXPECT_TRUE(secondCompleted);
}

GTEST_TEST(AsyncOperationTests, FromResult_RunsCallbacksInline)
{
    TestContext context{TestKey, {}};
    TestContext* failureContext = nullptr;
    bool completed = false;

    AsyncOperation<TestContext, int, int>::FromResult(3, &context)
        ->OnFailure([&failureContext](TestContext* c, int) { failureContext = c; })
        ->OnComplete([&completed]() { completed = true; })
        ->Execute(std::make_shared<int>(0));

    EXPECT_EQ(&context, failureContext);
    EXPECT_TRUE(completed);
    EXPECT_FALSE(util::AsyncKeys::IsPending(TestKey));
}

GTEST_TEST(AsyncOperationTests, Execute_OtherThread_ThrowsThreadAffinityViolation)
{
    TestContext context{TestKey, {}};
    auto operation = TestOperation::Create(&context, [](std::shared_ptr<int>) { return true; }, true);

    auto thrown = RunOffMainThread([&operation]() { operation->Execute(std::make_shared<int>(0)); });

    ASSERT_TRUE(thrown != nullptr);
    EXPECT_THROW(std::rethrow_exception(thrown), util::ThreadAffinityViolation);
    EXPECT_FALSE(util::AsyncKeys::IsPending(TestKey));
}

GTEST_TEST(AsyncKeysTests, IsPending_OtherThread_ThrowsThreadAffinityViolation)
{
    auto thrown = RunOffMainThread([]() { util::AsyncKeys::IsPending(TestKey); });

    ASSERT_TRUE(thrown != nullptr);
    EXPECT_THROW(std::rethrow_exception(thrown), util::ThreadAffinityViolation);
}
