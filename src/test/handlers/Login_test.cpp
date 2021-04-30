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

class MockDatabaseFactory : public DatabaseFactory
{
public:
    MOCK_METHOD(std::shared_ptr<Database>, CreateDatabase, (Config& config, bool logConnection), (const override));

private:
    MockDatabase * database;
};

GTEST_TEST(LoginTests, LoginUnderStressReturnsServerBusy)
{
    // todo: re-enable after merge with test fixes
    GTEST_SKIP();
    Console::SuppressOutput(true);

    Config config;
    eoserv_config_validate_config(config);

    Config admin_config;
    std::shared_ptr<Database> mockDatabase(new MockDatabase);

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
    // turn off timed save
    config["TimedSave"] = "false";

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

    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(HasSubstr("FROM `accounts`"), AnyOf(true, false), AnyOf(true, false)))
        .WillRepeatedly(Return(Database_Result()));

    // Suppress gmock "uninteresting method call" warnings in output
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()), Pending()).WillRepeatedly(Return(false));
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()), Escape(_)).WillRepeatedly(Return(""));
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()), Commit()).WillRepeatedly(Return());

    std::shared_ptr<DatabaseFactory> mockDatabaseFactory(new MockDatabaseFactory);
    EXPECT_CALL(*dynamic_cast<MockDatabaseFactory*>(mockDatabaseFactory.get()), CreateDatabase(_, _))
        .WillRepeatedly(Invoke([&](Unused, Unused) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); return mockDatabase; }));

    EOServer server(IPAddress("127.0.0.1"), TestServerPort, mockDatabaseFactory, config, admin_config);

    std::list<std::shared_ptr<MockClient>> clientRefs;
    for (auto i = 0; i < MaxConcurrentLogins + 1; i++)
    {
        std::shared_ptr<MockClient> client(new MockClient(&server));
        clientRefs.push_back(client); // keep reference to client so it doesn't get deallocated

        // Only expect a packet with LOGIN_BUSY if we exceed max concurrent logins
        PacketBuilder expectedResponse(PACKET_LOGIN, PACKET_REPLY, 2);
        expectedResponse.AddShort(i == MaxConcurrentLogins ? LOGIN_BUSY : LOGIN_WRONG_USER);
        EXPECT_CALL(*client, Send(expectedResponse)).Times(1);

        // client should never be closed
        EXPECT_CALL(*client, Close(false)).Times(0);

        PacketBuilder b(PACKET_LOGIN, PACKET_REQUEST, 20);
        PacketReader r(b.AddBreakString("test_user").AddBreakString("test_pass").Get());
        Handlers::Login_Request(client.get(), r);
    }

    // wait for the threads to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

GTEST_TEST(LoginTests, TooManyRepeatedLoginAttemptsDisconnectsClient)
{
    Console::SuppressOutput(true);

    Config config;
    eoserv_config_validate_config(config);

    Config admin_config;
    std::shared_ptr<Database> mockDatabase(new MockDatabase);

    // these are needed for the test to run
    // test assumes relative to directory {install_dir}/test
    config["ServerLanguage"] = "../lang/en.ini";
    config["EIF"] = "../data/pub/dat001.eif";
    config["ENF"] = "../data/pub/dtn001.enf";
    config["ESF"] = "../data/pub/dsl001.esf";
    config["ECF"] = "../data/pub/dat001.ecf";

    // turn off SLN
    config["SLN"] = "false";
    // turn off timed save
    config["TimedSave"] = "false";

    // make password hashing quicker for test
    config["BCryptWorkload"] = 10;
    config["PasswordVersion"] = static_cast<int>(HashFunc::BCRYPT);
    config["LoginQueueSize"] = 10;

    // test-specific configs
    const int MaxLoginAttempts = 2;
    config["MaxLoginAttempts"] = MaxLoginAttempts;

    // set up responses to database queries
    Database_Result banCheckResult;
    std::unordered_map<std::string, util::variant> banCheckColumns;
    banCheckColumns["expires"] = util::variant(-1);
    banCheckResult.push_back(banCheckColumns);

    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(HasSubstr("FROM bans"), _, _))
        .WillRepeatedly(Return(banCheckResult));

    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(HasSubstr("FROM `accounts`"), _, _))
        .WillRepeatedly(Return(Database_Result()));

    // Suppress gmock "uninteresting method call" warnings in output
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()), Pending()).WillRepeatedly(Return(false));
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()), Escape(_)).WillRepeatedly(Return(""));
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()), Commit()).WillRepeatedly(Return());

    std::shared_ptr<DatabaseFactory> mockDatabaseFactory(new MockDatabaseFactory);
    EXPECT_CALL(*dynamic_cast<MockDatabaseFactory*>(mockDatabaseFactory.get()), CreateDatabase(_, _))
        .WillRepeatedly(Return(mockDatabase));

    EOServer server(IPAddress("127.0.0.1"), TestServerPort, mockDatabaseFactory, config, admin_config);
    MockClient client(&server);

    // Expect a packet with LOGIN_WRONG_USER for each attempt
    PacketBuilder expectedResponse(PACKET_LOGIN, PACKET_REPLY, 2);
    expectedResponse.AddShort(LOGIN_WRONG_USER);
    EXPECT_CALL(client, Send(expectedResponse)).Times(MaxLoginAttempts);

    // expect the client to be closed once
    EXPECT_CALL(client, Close(_)).Times(1);

    for (auto i = 0; i < MaxLoginAttempts; i++)
    {
        PacketBuilder b(PACKET_LOGIN, PACKET_REQUEST, 20);
        PacketReader r(b.AddBreakString("test_user").AddBreakString("test_pass").Get());
        Handlers::Login_Request(&client, r);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}