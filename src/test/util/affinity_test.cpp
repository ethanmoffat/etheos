#include "util/affinity.hpp"

#include "../testhelper/threads.hpp"

#include <gtest/gtest.h>

#include <exception>
#include <string>
#include <thread>

namespace
{
    bool IsOwningThreadOnOtherThread(const util::ThreadAffinity& affinity)
    {
        bool owning = true;
        std::thread([&affinity, &owning]() { owning = affinity.IsOwningThread(); }).join();
        return owning;
    }
}

static void NamedCallSite()
{
    util::MainThreadAffinity::ThrowOnCrossThreadAccess();
}

GTEST_TEST(ThreadAffinityTests, IsOwningThread_DefaultUnbound_ThrowsThreadBindViolation)
{
    util::ThreadAffinity affinity;

    EXPECT_THROW(affinity.IsOwningThread(), util::ThreadBindViolation);
}

GTEST_TEST(ThreadAffinityTests, IsOwningThread_LooseEvaluationUnbound_AnyThreadIsOwningThread)
{
    util::ThreadAffinity affinity(util::ThreadAffinity::Behavior::LooseEvaluation);

    EXPECT_TRUE(affinity.IsOwningThread());
    EXPECT_TRUE(IsOwningThreadOnOtherThread(affinity));
}

GTEST_TEST(ThreadAffinityTests, IsOwningThread_LooseEvaluationBound_OnlyOwnerIsOwningThread)
{
    util::ThreadAffinity affinity(util::ThreadAffinity::Behavior::LooseEvaluation);
    affinity.Bind();

    EXPECT_TRUE(affinity.IsOwningThread());
    EXPECT_FALSE(IsOwningThreadOnOtherThread(affinity));
}

GTEST_TEST(ThreadAffinityTests, IsOwningThread_Bound_OnlyOwnerIsOwningThread)
{
    util::ThreadAffinity affinity;
    affinity.Bind();

    EXPECT_TRUE(affinity.IsOwningThread());
    EXPECT_FALSE(IsOwningThreadOnOtherThread(affinity));
}

GTEST_TEST(ThreadAffinityTests, Bind_OtherThread_TakesOwnership)
{
    util::ThreadAffinity affinity;
    affinity.Bind();

    std::thread([&affinity]() { affinity.Bind(); }).join();

    EXPECT_FALSE(affinity.IsOwningThread());
}

GTEST_TEST(ThreadAffinityTests, Constructor_BindOnCreate_CreatingThreadIsOwningThread)
{
    util::ThreadAffinity affinity(util::ThreadAffinity::Behavior::BindOnCreate);

    EXPECT_TRUE(affinity.IsOwningThread());
    EXPECT_FALSE(IsOwningThreadOnOtherThread(affinity));
}

GTEST_TEST(ThreadAffinityTests, Bind_SingleBindAlreadyBound_ThrowsThreadBindViolationAndKeepsOwner)
{
    util::ThreadAffinity affinity(util::ThreadAffinity::Behavior::SingleBind);
    affinity.Bind();

    std::exception_ptr thrown;
    std::thread([&affinity, &thrown]()
    {
        try
        {
            affinity.Bind();
        }
        catch (...)
        {
            thrown = std::current_exception();
        }
    }).join();

    ASSERT_TRUE(thrown != nullptr);
    EXPECT_THROW(std::rethrow_exception(thrown), util::ThreadBindViolation);
    EXPECT_TRUE(affinity.IsOwningThread());
}

GTEST_TEST(ThreadAffinityTests, Constructor_BindOnCreateAndSingleBind_BindThrowsThreadBindViolation)
{
    util::ThreadAffinity affinity(util::ThreadAffinity::Behavior::BindOnCreate | util::ThreadAffinity::Behavior::SingleBind);

    EXPECT_THROW(affinity.Bind(), util::ThreadBindViolation);
}

GTEST_TEST(MainThreadAffinityTests, IsMainThread_BeforeAnyCheck_TestMainThreadIsMainThread)
{
    EXPECT_TRUE(util::MainThreadAffinity::IsMainThread());
}

GTEST_TEST(MainThreadAffinityTests, IsMainThread_OtherThread_ReturnsFalse)
{
    bool isMainThread = true;
    std::thread([&isMainThread]() { isMainThread = util::MainThreadAffinity::IsMainThread(); }).join();

    EXPECT_FALSE(isMainThread);
}

GTEST_TEST(MainThreadAffinityTests, ThrowOnCrossThreadAccess_MainThread_DoesNotThrow)
{
    EXPECT_NO_THROW(util::MainThreadAffinity::ThrowOnCrossThreadAccess());
}

GTEST_TEST(MainThreadAffinityTests, ThrowOnCrossThreadAccess_OtherThread_ThrowsWithCallSite)
{
    auto thrown = RunOffMainThread(NamedCallSite);

    ASSERT_TRUE(thrown != nullptr);
    try
    {
        std::rethrow_exception(thrown);
    }
    catch (const util::ThreadAffinityViolation& e)
    {
        EXPECT_NE(std::string(e.what()).find("NamedCallSite"), std::string::npos);
    }
    catch (...)
    {
        FAIL() << "Expected util::ThreadAffinityViolation";
    }
}

GTEST_TEST(MainThreadAffinityTests, ThrowOnCrossThreadAccess_AfterOtherThreadViolates_MainThreadKeepsAccess)
{
    RunOffMainThread([]() { util::MainThreadAffinity::ThrowOnCrossThreadAccess(); });

    EXPECT_NO_THROW(util::MainThreadAffinity::ThrowOnCrossThreadAccess());
}
