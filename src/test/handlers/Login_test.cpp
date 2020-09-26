// include the CPP file with the Login functions in it for testing
#include "../../handlers/Login.cpp"

#include "console.hpp"
#include "eoserv_config.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace ::testing;

static constexpr unsigned short TestServerPort = 38078;

class MockDatabase : public Database
{
public:
    MOCK_METHOD(void, Connect, (Database::Engine type, const std::string& host, unsigned short port, const std::string& user, const std::string& pass, const std::string& db), (override));
    MOCK_METHOD(void, Close, (), (override));
    MOCK_METHOD(Database_Result, RawQuery, (const char* query, bool tx_control, bool prepared), (override));
    MOCK_METHOD(std::string, Escape, (const std::string& esc), (override));
    MOCK_METHOD(void, ExecuteFile, (const std::string& filename), (override));

    MOCK_METHOD(bool, Pending, (), (override, const));
    MOCK_METHOD(bool, BeginTransaction, (), (override));
    MOCK_METHOD(void, Commit, (), (override));
    MOCK_METHOD(void, Rollback, (), (override));

    virtual Database_Result Query(const char *format, ...) override
    {
        return this->RawQuery(format, false, false);
    }
};

class MockClient : public EOClient
{
public:
    MockClient(EOServer * server) : EOClient(server) { }
    MOCK_METHOD(void, Send, (const PacketBuilder &packet), (override));
    MOCK_METHOD(void, Close, (bool force), (override));
};

GTEST_TEST(LoginTests, LoginUnderStressReturnsServerBusy)
{
    Console::SuppressOutput(true);

    Config config;
    eoserv_config_validate_config(config);

    Config admin_config;
    std::unique_ptr<Database> mockDatabase(new MockDatabase);

    const int MaxConcurrentLogins = 2;

    // these are needed for the test to run
    // test assumes relative to directory {install_dir}/test
    config["ServerLanguage"] = "../lang/en.ini";
    config["EIF"] = "../data/pub/dat001.eif";
    config["ENF"] = "../data/pub/dtn001.enf";
    config["ESF"] = "../data/pub/dsl001.esf";
    config["ECF"] = "../data/pub/dat001.ecf";

    //turn off SLN
    config["SLN"] = "false";

    // test-specific configs
    config["BCryptWorkload"] = 16;
    config["PasswordVersion"] = static_cast<int>(HashFunc::BCRYPT);
    config["LoginQueueSize"] = MaxConcurrentLogins;

    // set up responses to database queries
    Database_Result banCheckResult;
    std::unordered_map<std::string, util::variant> banCheckColumns;
    banCheckColumns["expires"] = util::variant(-1);
    banCheckResult.push_back(banCheckColumns);

    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(HasSubstr("FROM bans"), AnyOf(true, false), AnyOf(true, false)))
        .WillRepeatedly(Return(banCheckResult));

    // Suppress gmock "uninteresting method call" warnings in output
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()), Pending()).WillRepeatedly(Return(false));
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()), Escape(_)).WillRepeatedly(Return(""));
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()), Commit()).WillRepeatedly(Return());

    EOServer server(IPAddress("127.0.0.1"), TestServerPort, std::move(mockDatabase), config, admin_config);

    auto createFunc = [&](int currentIndex)
    {
        MockClient client(&server);

        if (currentIndex == MaxConcurrentLogins)
        {
            // Expect a packet with LOGIN_BUSY if we exceed max concurrent logins
            PacketBuilder expectedResponse(PACKET_LOGIN, PACKET_REPLY, 2);
            expectedResponse.AddShort(LOGIN_BUSY);
            EXPECT_CALL(client, Send(expectedResponse)).Times(1);
        }

        // client should not be closed in any case
        EXPECT_CALL(client, Close(false)).Times(0);

        PacketBuilder b(PACKET_LOGIN, PACKET_REQUEST, 20);
        PacketReader r(b.AddBreakString("test_user").AddBreakString("test_pass").Get());

        Handlers::Login_Request(&client, r);
    };

    for (auto i = 0; i < MaxConcurrentLogins + 1; i++)
    {
        createFunc(i);
    }
}