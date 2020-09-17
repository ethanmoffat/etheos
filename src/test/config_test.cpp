#include <gtest/gtest.h>
#include "config.hpp"

GTEST_TEST(ConfigTests, SampleTestPasses)
{
    Config config;
    ASSERT_TRUE(true);
}

GTEST_TEST(ConfigTests, SampleTestFails)
{
    GTEST_SKIP();

    Config config;
    ASSERT_TRUE(false);
}