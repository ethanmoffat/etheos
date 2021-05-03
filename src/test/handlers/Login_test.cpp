#include "../testhelper/mocks.hpp"
#include "../testhelper/setup.hpp"

// include the CPP file with the Login functions in it for testing
#include "../../handlers/Login.cpp"

#include "console.hpp"

static constexpr unsigned short TestServerPort = 38078;

std::shared_ptr<Database> CreateMockDatabaseForLoginTests()
{
    std::shared_ptr<Database> mockDatabase(new MockDatabase);

    // set up responses to database queries
    Database_Result banCheckResult;
    std::unordered_map<std::string, util::variant> banCheckColumns;
    banCheckColumns["expires"] = util::variant(-1);
    banCheckResult.push_back(banCheckColumns);

    // no bans by default
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(HasSubstr("FROM bans"), AnyOf(true, false), AnyOf(true, false)))
        .WillRepeatedly(Return(banCheckResult));

    // no accounts by default
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(HasSubstr("FROM `accounts`"), AnyOf(true, false), AnyOf(true, false)))
        .WillRepeatedly(Return(Database_Result()));

    // Suppress gmock "uninteresting method call" warnings in output
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()), Pending()).WillRepeatedly(Return(false));
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()), Escape(_)).WillRepeatedly(Return(""));
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()), Commit()).WillRepeatedly(Return());

    return mockDatabase;
}

std::shared_ptr<DatabaseFactory> CreateMockDatabaseFactoryForLoginTests(std::shared_ptr<Database> database, bool delayInCreate = false)
{
    std::shared_ptr<DatabaseFactory> mockDatabaseFactory(new MockDatabaseFactory);
    EXPECT_CALL(*dynamic_cast<MockDatabaseFactory*>(mockDatabaseFactory.get()), CreateDatabase(_, _))
        .WillRepeatedly(Invoke([database, delayInCreate](Unused, Unused)
            {
                if (delayInCreate)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                return database;
            }));
    return mockDatabaseFactory;
}

GTEST_TEST(LoginTests, LoginUnderStressReturnsServerBusy)
{
    Console::SuppressOutput(true);

    const int MaxConcurrentLogins = 2;

    Config config, admin_config;
    CreateConfigWithTestDefaults(config, admin_config);
    config["LoginQueueSize"] = MaxConcurrentLogins;

    auto mockDatabase = CreateMockDatabaseForLoginTests();
    auto mockDatabaseFactory = CreateMockDatabaseFactoryForLoginTests(mockDatabase, true);

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

    const int MaxLoginAttempts = 2;

    Config config, admin_config;
    CreateConfigWithTestDefaults(config, admin_config);
    config["LoginQueueSize"] = 10;
    config["MaxLoginAttempts"] = MaxLoginAttempts;

    auto mockDatabase = CreateMockDatabaseForLoginTests();
    auto mockDatabaseFactory = CreateMockDatabaseFactoryForLoginTests(mockDatabase, false);

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