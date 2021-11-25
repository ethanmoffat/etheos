#include <gtest/gtest.h>

#include "config.hpp"
#include "console.hpp"
#include "eoserv_config.hpp"

#ifdef _WIN32
#include "eoserv_windows.h"
#endif

GTEST_TEST(ConfigTests, EoservConfig_ValueFromEnvironmentVariable_OverridesFileValue)
{
    Console::SuppressOutput(true);
    setenv("ETHEOS_PORT", "12345", 1);

    Config config;
    (void)config["Port"];

    ASSERT_EQ(config["Port"].GetInt(), 12345);
}
