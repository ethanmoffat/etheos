#include <gtest/gtest.h>

#include "util/secure_string.hpp"

#include <string>

namespace
{

struct Credentials
{
    std::string username;
    std::string password;
    std::string other_password;
};

}

GTEST_TEST(SecureStringTests, WipeMembers_GivenMembers_ClearsOnlyThoseMembers)
{
    Credentials credentials { "user", "password", "other" };

    util::wipe_members(credentials, &Credentials::password, &Credentials::other_password);

    EXPECT_EQ("user", credentials.username);
    EXPECT_TRUE(credentials.password.empty());
    EXPECT_TRUE(credentials.other_password.empty());
}

GTEST_TEST(SecureStringTests, MakeSecureShared_GivenValue_ReturnsCopyAndLeavesValueUnchanged)
{
    const Credentials credentials { "user", "password", "other" };

    auto copy = util::make_secure_shared(credentials, &Credentials::password);

    EXPECT_NE(&credentials, copy.get());
    EXPECT_EQ("user", copy->username);
    EXPECT_EQ("password", copy->password);
    EXPECT_EQ("other", copy->other_password);
    EXPECT_EQ("password", credentials.password);
}
