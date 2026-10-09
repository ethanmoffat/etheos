#include "../testhelper/mocks.hpp"
#include "../testhelper/packets.hpp"
#include "../testhelper/setup.hpp"
#include "../testhelper/threads.hpp"

// include the CPP file with the Login functions in it for testing
#include "../../handlers/Login.cpp"

#include "console.hpp"
#include "extra/seose_compat.hpp"
#include "util/async.hpp"
#include "util/affinity.hpp"
#include "util/dispatcher.hpp"

namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

static constexpr unsigned short TestServerPort = 38078;

static void WaitForAsyncOperation(const EOClient& client)
{
    for (int i = 0; i < 1000 && util::AsyncKeys::IsPending(client.Serial()); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        util::Dispatcher::RunPending();
    }
}

static client::LoginRequestClientPacket LoginRequest(const std::string& username, const std::string& password)
{
    client::LoginRequestClientPacket packet;
    packet.username = username;
    packet.password = password;
    return packet;
}

GTEST_TEST(LoginTests, BasicParameterTests)
{
    Console::SuppressOutput(true);

    const size_t AccountMaxLength = 15;
    const size_t PasswordMaxLength = 25;
    const size_t AccountMinLength = 4;
    const size_t PasswordMinLength = 8;

    Config config, admin_config;
    CreateConfigWithTestDefaults(config, admin_config);
    config["AccountMaxLength"] = int(AccountMaxLength);
    config["AccountMinLength"] = int(AccountMinLength);
    config["PasswordMaxLength"] = int(PasswordMaxLength);
    config["PasswordMinLength"] = int(PasswordMinLength);
    config["MaxPlayers"] = 0; // ensure server busy if credentials are ok

    auto mockDatabase = CreateMockDatabase();
    auto mockDatabaseFactory = CreateMockDatabaseFactory(mockDatabase, true);

    EOServer server(IPAddress("127.0.0.1"), TestServerPort, mockDatabaseFactory, config, admin_config);

    // Account max length - no response
    {
        MockClient client(&server);

        EXPECT_CALL(client, Send(An<const eolib::protocol::net::Packet &>())).Times(0);
        EXPECT_CALL(client, Close(_)).Times(0);

        auto r = LoginRequest(std::string(AccountMaxLength + 1, 'a'), "test_pass");
        Handlers::Login_Request(&client, r);
    }

    // Password max length - no response
    {
        MockClient client(&server);

        EXPECT_CALL(client, Send(An<const eolib::protocol::net::Packet &>())).Times(0);
        EXPECT_CALL(client, Close(_)).Times(0);

        auto r = LoginRequest("test_user", std::string(PasswordMaxLength + 1, 'a'));
        Handlers::Login_Request(&client, r);
    }

    // Account min length - wrong user
    {
        MockClient client(&server);

        auto expectedResponse = server::LoginReplyServerPacket::ForWrongUser();
        EXPECT_CALL(client, Send(PacketEq(expectedResponse))).Times(1);
        EXPECT_CALL(client, Close(false)).Times(0);

        auto r = LoginRequest(std::string(AccountMinLength - 1, 'a'), "test_pass");
        Handlers::Login_Request(&client, r);
    }

    // Password min length - wrong userpass
    {
        MockClient client(&server);

        auto expectedResponse = server::LoginReplyServerPacket::ForWrongUserPassword();
        EXPECT_CALL(client, Send(PacketEq(expectedResponse))).Times(1);
        EXPECT_CALL(client, Close(false)).Times(0);

        auto r = LoginRequest("test_user", std::string(PasswordMinLength - 1, 'a'));
        Handlers::Login_Request(&client, r);
    }

    // user/pass right length - server busy
    {
        MockClient client(&server);

        auto expectedResponse = server::LoginReplyServerPacket::ForBusy();
        EXPECT_CALL(client, Send(PacketEq(expectedResponse))).Times(1);
        EXPECT_CALL(client, Close(false)).Times(1);

        auto r = LoginRequest("test_user", "test_pass");
        Handlers::Login_Request(&client, r);
    }
}

GTEST_TEST(LoginTests, LoginWhenBannedReturnsBan)
{
    Console::SuppressOutput(true);

    Config config, admin_config;
    CreateConfigWithTestDefaults(config, admin_config);

    auto mockDatabase = CreateMockDatabase();
    auto mockDatabaseFactory = CreateMockDatabaseFactory(mockDatabase, true);

    Database_Result banCheckResult;
    std::unordered_map<std::string, util::variant> banCheckColumns;
    banCheckColumns["expires"] = util::variant(5);
    banCheckResult.push_back(banCheckColumns);

    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(HasSubstr("FROM bans"), _, _))
        .WillRepeatedly(Return(banCheckResult));

    EOServer server(IPAddress("127.0.0.1"), TestServerPort, mockDatabaseFactory, config, admin_config);

    // test INIT ban reply
    {
        server.world->config["InitLoginBan"] = true;
        MockClient client(&server);

        auto expectedResponse = server::InitInitServerPacket::ForBannedPermanent();
        EXPECT_CALL(client, Send(PacketEq(expectedResponse))).Times(1);
        EXPECT_CALL(client, Close(false)).Times(1);

        auto r = LoginRequest("test_user", "test_pass");
        Handlers::Login_Request(&client, r);
    }

    // test ACCOUNT_BANNED login reply
    {
        server.world->config["InitLoginBan"] = false;
        MockClient client(&server);

        auto expectedResponse = server::LoginReplyServerPacket::ForBanned();
        EXPECT_CALL(client, Send(PacketEq(expectedResponse))).Times(1);
        EXPECT_CALL(client, Close(false)).Times(1);

        auto r = LoginRequest("test_user", "test_pass");
        Handlers::Login_Request(&client, r);
    }
}

GTEST_TEST(LoginTests, LoginUnderStressReturnsServerBusy)
{
    Console::SuppressOutput(true);

    const int MaxConcurrentLogins = 5;

    Config config, admin_config;
    CreateConfigWithTestDefaults(config, admin_config);
    config["LoginQueueSize"] = MaxConcurrentLogins;

    auto mockDatabase = CreateMockDatabase();
    auto mockDatabaseFactory = CreateMockDatabaseFactory(mockDatabase, true);

    EOServer server(IPAddress("127.0.0.1"), TestServerPort, mockDatabaseFactory, config, admin_config);

    std::list<std::shared_ptr<MockClient>> clientRefs;
    for (auto i = 0; i < MaxConcurrentLogins + 1; i++)
    {
        std::shared_ptr<MockClient> client(new MockClient(&server));
        clientRefs.push_back(client); // keep reference to client so it doesn't get deallocated

        // Only expect a packet with Busy if we exceed max concurrent logins
        auto expectedResponse = i == MaxConcurrentLogins
            ? server::LoginReplyServerPacket::ForBusy()
            : server::LoginReplyServerPacket::ForWrongUser();
        EXPECT_CALL(*client, Send(PacketEq(expectedResponse))).Times(1);

        // client should never be closed
        EXPECT_CALL(*client, Close(false)).Times(0);

        auto r = LoginRequest("test_user", "test_pass");
        Handlers::Login_Request(client.get(), r);
    }

    for (const auto& client : clientRefs)
        WaitForAsyncOperation(*client);
}

GTEST_TEST(LoginTests, TooManyRepeatedLoginAttemptsDisconnectsClient)
{
    Console::SuppressOutput(true);

    const int MaxLoginAttempts = 2;

    Config config, admin_config;
    CreateConfigWithTestDefaults(config, admin_config);
    config["MaxLoginAttempts"] = MaxLoginAttempts;

    auto mockDatabase = CreateMockDatabase();
    auto mockDatabaseFactory = CreateMockDatabaseFactory(mockDatabase, false);

    EOServer server(IPAddress("127.0.0.1"), TestServerPort, mockDatabaseFactory, config, admin_config);
    MockClient client(&server);

    // Expect a WrongUser reply for each attempt
    auto expectedResponse = server::LoginReplyServerPacket::ForWrongUser();
    EXPECT_CALL(client, Send(PacketEq(expectedResponse))).Times(MaxLoginAttempts);

    // expect the client to be closed once
    EXPECT_CALL(client, Close(_)).Times(1);

    for (auto i = 0; i < MaxLoginAttempts; i++)
    {
        auto r = LoginRequest("test_user", "test_pass");
        Handlers::Login_Request(&client, r);

        WaitForAsyncOperation(client);
    }
}

GTEST_TEST(LoginTests, LoginWithOldPasswordVersionDoesNotUpgradeOnWrongPassword)
{
    Console::SuppressOutput(true);

    const std::string ExpectedUsername = "test_user";
    const std::string UnhashedPassword = "test_pass";

    Config config, admin_config;
    CreateConfigWithTestDefaults(config, admin_config);
    config["PasswordCurrentVersion"] = int(HashFunc::BCRYPT);

    auto mockDatabase = CreateMockDatabase();
    auto mockDatabaseFactory = CreateMockDatabaseFactory(mockDatabase, false);

    Database_Result oldVersionResult;
    std::unordered_map<std::string, util::variant> oldVersionColumns;
    oldVersionColumns["password_version"] = util::variant(HashFunc::SHA256);
    oldVersionColumns["password"] = UnhashedPassword; // use this as a database password so we get WrongUserPassword
    oldVersionResult.push_back(oldVersionColumns);

    // return password result to login manager for any given username
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(StartsWith("SELECT password, password_version FROM accounts"), _, _))
        .WillOnce(Return(oldVersionResult));

    // Expect no call to update the password
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(StartsWith("UPDATE accounts SET password = "), _, _))
        .Times(0);

    EOServer server(IPAddress("127.0.0.1"), TestServerPort, mockDatabaseFactory, config, admin_config);

    MockClient client(&server);

    // Expect a WrongUserPassword reply
    auto expectedResponse = server::LoginReplyServerPacket::ForWrongUserPassword();
    EXPECT_CALL(client, Send(PacketEq(expectedResponse))).Times(1);

    auto r = LoginRequest(ExpectedUsername, UnhashedPassword);
    Handlers::Login_Request(&client, r);

    WaitForAsyncOperation(client);
}

GTEST_TEST(LoginTests, LoginWithOldPasswordVersionUpgradesInBackground)
{
    Console::SuppressOutput(true);

    const std::string ExpectedUsername = "test_user";
    const std::string UnhashedPassword = "test_pass";

    Config config, admin_config;
    CreateConfigWithTestDefaults(config, admin_config);
    config["PasswordCurrentVersion"] = int(HashFunc::BCRYPT);

    // need copies since Hasher::SaltPassword requires util::secure_string inputs for password, which must be move constructed
    std::string passwordCopy(UnhashedPassword);
    std::string passwordCopy2(UnhashedPassword);

    Sha256Hasher sha256;
    auto saltedOldPassword = Hasher::SaltPassword(std::string(config["PasswordSalt"]), ExpectedUsername, std::move(passwordCopy)).str();
    auto expectedOldPassword = sha256.hash(saltedOldPassword);

    BcryptHasher bcrypt(10);
    auto saltedNewPassword = Hasher::SaltPassword(std::string(config["PasswordSalt"]), ExpectedUsername, std::move(passwordCopy2)).str();
    auto expectedNewPassword = bcrypt.hash(saltedNewPassword);

    auto mockDatabase = CreateMockDatabase();
    auto mockDatabaseFactory = CreateMockDatabaseFactory(mockDatabase, false);

    Database_Result oldVersionResult;
    std::unordered_map<std::string, util::variant> oldVersionColumns;
    oldVersionColumns["password_version"] = util::variant(HashFunc::SHA256);
    oldVersionColumns["username"] = ExpectedUsername;
    oldVersionColumns["password"] = expectedOldPassword;
    oldVersionResult.push_back(oldVersionColumns);

    Database_Result newVersionResult;
    std::unordered_map<std::string, util::variant> newVersionColumns;
    newVersionColumns["password_version"] = util::variant(HashFunc::BCRYPT);
    newVersionColumns["username"] = ExpectedUsername;
    newVersionColumns["password"] = expectedNewPassword;
    newVersionResult.push_back(newVersionColumns);

    // return password result to login manager for any given username
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(StartsWith("SELECT password, password_version FROM accounts"), _, _))
        .WillOnce(Return(oldVersionResult))
        .WillOnce(Return(newVersionResult));

    // return some result to the Player constructor
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(StartsWith("SELECT username, password FROM accounts"), _, _))
        .WillRepeatedly(Return(oldVersionResult));

    // ensure no characters (characters not needed for test)
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(HasSubstr("FROM characters"), _, _))
        .WillRepeatedly(Return(Database_Result()));

    // Expect single call to update the password
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(StartsWith("UPDATE accounts SET password = "), _, _))
        .Times(1);

    // expect two of these (happens on client disconnect)
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(StartsWith("UPDATE accounts SET lastused = "), _, _))
        .Times(2);

    EOServer server(IPAddress("127.0.0.1"), TestServerPort, mockDatabaseFactory, config, admin_config);

    std::vector<std::shared_ptr<MockClient>> clientRefs;
    for (auto i = 0; i < 2; i++)
    {
        std::shared_ptr<MockClient> client(new MockClient(&server));
        clientRefs.push_back(client);

        // Expect an Ok reply for each attempt
        auto expectedResponse = server::LoginReplyServerPacket::ForOk({});
        EXPECT_CALL(*dynamic_cast<MockClient*>(client.get()), Send(PacketEq(expectedResponse))).Times(1);

        // always connected - ensures the Ok reply is sent
        EXPECT_CALL(*dynamic_cast<MockClient*>(client.get()), Connected()).WillRepeatedly(Return(true));

        auto r = LoginRequest(ExpectedUsername, UnhashedPassword);
        Handlers::Login_Request(client.get(), r);
        WaitForAsyncOperation(*client);

        // The password version upgrade runs in the background with no completion to wait for
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    }
}

GTEST_TEST(LoginTests, LoginWithSeoseCompatAndShortPassword_ChecksSeoseHashedPassword)
{
    Console::SuppressOutput(true);

    const std::string ExpectedUsername = "test_user";
    const std::string UnhashedPassword = "abc";

    Config config, admin_config;
    CreateConfigWithTestDefaults(config, admin_config);
    config["SeoseCompat"] = true;
    config["PasswordCurrentVersion"] = int(HashFunc::SHA256);
    ASSERT_LT(UnhashedPassword.length(), std::size_t(int(config["PasswordMinLength"])));

    std::string seosePassword = seose_str_hash(UnhashedPassword, config["SeoseCompatKey"]);
    auto saltedPassword = Hasher::SaltPassword(std::string(config["PasswordSalt"]), ExpectedUsername, std::move(seosePassword)).str();

    auto mockDatabase = CreateMockDatabase();
    auto mockDatabaseFactory = CreateMockDatabaseFactory(mockDatabase, false);

    Database_Result accountResult;
    std::unordered_map<std::string, util::variant> accountColumns;
    accountColumns["password_version"] = util::variant(HashFunc::SHA256);
    accountColumns["username"] = ExpectedUsername;
    accountColumns["password"] = Sha256Hasher().hash(saltedPassword);
    accountResult.push_back(accountColumns);

    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(HasSubstr("FROM accounts"), _, _))
        .WillRepeatedly(Return(accountResult));

    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(HasSubstr("FROM characters"), _, _))
        .WillRepeatedly(Return(Database_Result()));

    // happens on client disconnect
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(StartsWith("UPDATE accounts SET lastused = "), _, _))
        .Times(AnyNumber());

    EOServer server(IPAddress("127.0.0.1"), TestServerPort, mockDatabaseFactory, config, admin_config);

    std::shared_ptr<MockClient> client(new MockClient(&server));
    EXPECT_CALL(*client, Connected()).WillRepeatedly(Return(true));
    EXPECT_CALL(*client, Send(PacketEq(server::LoginReplyServerPacket::ForOk({})))).Times(1);

    Handlers::Login_Request(client.get(), LoginRequest(ExpectedUsername, UnhashedPassword));

    WaitForAsyncOperation(*client);
}

GTEST_TEST(LoginTests, Request_AccountLoginPending_KeepsPendingLogin)
{
    Console::SuppressOutput(true);

    Config config, admin_config;
    CreateConfigWithTestDefaults(config, admin_config);

    auto mockDatabase = CreateMockDatabase();
    auto mockDatabaseFactory = CreateMockDatabaseFactory(mockDatabase, false);

    EOServer server(IPAddress("127.0.0.1"), TestServerPort, mockDatabaseFactory, config, admin_config);
    server.world->SetPendingLogin("testuser", true);

    MockClient client(&server);
    EXPECT_CALL(client, Send(PacketEq(server::LoginReplyServerPacket::ForLoggedIn()))).Times(1);

    Handlers::Login_Request(&client, LoginRequest("testuser", "test_pass"));

    EXPECT_TRUE(server.world->GetPendingLogin("testuser"));
}

GTEST_TEST(LoginTests, Request_Completed_ClearsPendingLogin)
{
    Console::SuppressOutput(true);

    Config config, admin_config;
    CreateConfigWithTestDefaults(config, admin_config);

    auto mockDatabase = CreateMockDatabase();
    auto mockDatabaseFactory = CreateMockDatabaseFactory(mockDatabase, true);

    EOServer server(IPAddress("127.0.0.1"), TestServerPort, mockDatabaseFactory, config, admin_config);

    MockClient client(&server);
    EXPECT_CALL(client, Send(PacketEq(server::LoginReplyServerPacket::ForWrongUser()))).Times(1);

    Handlers::Login_Request(&client, LoginRequest("testuser", "test_pass"));
    EXPECT_TRUE(server.world->GetPendingLogin("testuser"));

    WaitForAsyncOperation(client);

    EXPECT_FALSE(util::AsyncKeys::IsPending(client.Serial()));
    EXPECT_FALSE(server.world->GetPendingLogin("testuser"));
}

GTEST_TEST(LoginTests, Request_DatabaseThrows_ClosesClientAndClearsPendingLogin)
{
    Console::SuppressOutput(true);

    Config config, admin_config;
    CreateConfigWithTestDefaults(config, admin_config);

    auto mockDatabase = CreateMockDatabase();
    auto mockDatabaseFactory = CreateMockDatabaseFactory(mockDatabase);

    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(StartsWith("SELECT password, password_version FROM accounts"), _, _))
        .WillOnce(Throw(std::runtime_error("database down")));

    EOServer server(IPAddress("127.0.0.1"), TestServerPort, mockDatabaseFactory, config, admin_config);

    MockClient client(&server);
    EXPECT_CALL(client, Send(_)).Times(0);
    EXPECT_CALL(client, Close(_)).Times(1);

    Handlers::Login_Request(&client, LoginRequest("testuser", "test_pass"));
    WaitForAsyncOperation(client);

    EXPECT_FALSE(util::AsyncKeys::IsPending(client.Serial()));
    EXPECT_FALSE(server.world->GetPendingLogin("testuser"));
}

GTEST_TEST(LoginTests, PendingLogin_OtherThread_ThrowsThreadAffinityViolation)
{
    Console::SuppressOutput(true);

    Config config, admin_config;
    CreateConfigWithTestDefaults(config, admin_config);

    auto mockDatabase = CreateMockDatabase();
    auto mockDatabaseFactory = CreateMockDatabaseFactory(mockDatabase, false);

    EOServer server(IPAddress("127.0.0.1"), TestServerPort, mockDatabaseFactory, config, admin_config);

    auto setThrown = RunOffMainThread([&server]() { server.world->SetPendingLogin("testuser", true); });
    auto getThrown = RunOffMainThread([&server]() { server.world->GetPendingLogin("testuser"); });

    ASSERT_TRUE(setThrown != nullptr);
    ASSERT_TRUE(getThrown != nullptr);
    EXPECT_THROW(std::rethrow_exception(setThrown), util::ThreadAffinityViolation);
    EXPECT_THROW(std::rethrow_exception(getThrown), util::ThreadAffinityViolation);
    EXPECT_FALSE(server.world->GetPendingLogin("testuser"));
}
