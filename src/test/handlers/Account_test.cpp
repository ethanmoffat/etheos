#include "../testhelper/mocks.hpp"
#include "../testhelper/packets.hpp"
#include "../testhelper/players.hpp"
#include "../testhelper/setup.hpp"

// include the CPP file with the Account functions in it for testing
#include "../../handlers/Account.cpp"

#include "console.hpp"
#include "hash.hpp"
#include "util/async.hpp"
#include "util/dispatcher.hpp"

#include <chrono>
#include <memory>
#include <thread>

namespace net = eolib::protocol::net;

namespace
{

constexpr unsigned short AccountTestServerPort = 38084;

class AccountTests : public testing::Test
{
protected:
    std::shared_ptr<Database> database;
    std::unique_ptr<EOServer> server;
    Config config;

    void SetUp() override
    {
        Console::SuppressOutput(true);
        database = CreateMockDatabase();

        Config admin_config;
        CreateConfigWithTestDefaults(config, admin_config);
        server = std::make_unique<EOServer>(IPAddress("127.0.0.1"), AccountTestServerPort, CreateMockDatabaseFactory(database), config, admin_config);
    }

    void GivenAccountExists()
    {
        Database_Result result;
        result.push_back({{"1", util::variant(1)}});

        EXPECT_CALL(*dynamic_cast<MockDatabase*>(database.get()), RawQuery(HasSubstr("FROM accounts"), _, _))
            .WillRepeatedly(Return(result));
    }

    MockDatabase& Db()
    {
        return *dynamic_cast<MockDatabase*>(database.get());
    }

    std::string StoredPassword(const std::string& username, const std::string& password)
    {
        return Sha256Hasher().hash(Hasher::SaltPassword(std::string(config["PasswordSalt"]), username, util::secure_string(std::string(password))).str());
    }

    void GivenAccountWithPassword(const std::string& username, const std::string& password)
    {
        Database_Result result;
        result.push_back({{"password", util::variant(StoredPassword(username, password))}, {"password_version", util::variant(HashFunc::SHA256)}});

        EXPECT_CALL(Db(), RawQuery(HasSubstr("FROM accounts"), _, _))
            .WillRepeatedly(Return(result));
    }

    static void WaitForAsyncOperation(const EOClient& client)
    {
        for (int i = 0; i < 1000 && util::AsyncKeys::IsPending(client.Serial()); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            util::Dispatcher::RunPending();
        }
    }

    static net::client::AccountRequestClientPacket AccountRequest(const std::string& username)
    {
        net::client::AccountRequestClientPacket packet;
        packet.username = username;
        return packet;
    }

    static net::client::AccountCreateClientPacket AccountCreate(int session_id, const std::string& username)
    {
        net::client::AccountCreateClientPacket packet;
        packet.session_id = session_id;
        packet.username = username;
        packet.password = "testpass";
        packet.full_name = "Test User";
        packet.location = "Here";
        packet.email = "test@example.com";
        packet.computer = "Test PC";
        packet.hdid = "1234567890";
        return packet;
    }

    static net::client::AccountAgreeClientPacket AccountAgree(const std::string& username, const std::string& old_password, const std::string& new_password)
    {
        net::client::AccountAgreeClientPacket packet;
        packet.username = username;
        packet.old_password = old_password;
        packet.new_password = new_password;
        return packet;
    }
};

}

TEST_F(AccountTests, Request_AvailableName_RepliesWithSessionIdAndSequenceStart)
{
    MockClient client(server.get());

    net::server::AccountReplyServerPacket reply;
    EXPECT_CALL(client, Send(An<const net::Packet&>()))
        .WillOnce(Invoke([&](const net::Packet& packet) { reply = dynamic_cast<const net::server::AccountReplyServerPacket&>(packet); }));

    Handlers::Account_Request(&client, AccountRequest("testuser"));

    EXPECT_GT(client.create_id, 9);
    EXPECT_EQ(client.create_id, static_cast<int>(reply.reply_code));
    EXPECT_NE(nullptr, reply.AsReplyCodeDefault());
}

TEST_F(AccountTests, Request_InvalidName_RepliesNotApproved)
{
    MockClient client(server.get());

    EXPECT_CALL(client, Send(PacketEq(net::server::AccountReplyServerPacket::ForNotApproved()))).Times(1);

    Handlers::Account_Request(&client, AccountRequest("test user!"));

    EXPECT_EQ(0, client.create_id);
}

TEST_F(AccountTests, Request_ExistingName_RepliesExists)
{
    GivenAccountExists();
    MockClient client(server.get());

    EXPECT_CALL(client, Send(PacketEq(net::server::AccountReplyServerPacket::ForExists()))).Times(1);

    Handlers::Account_Request(&client, AccountRequest("testuser"));
}

TEST_F(AccountTests, Create_WrongSessionId_ClosesClient)
{
    MockClient client(server.get());
    client.create_id = 12345;

    EXPECT_CALL(client, Send(An<const net::Packet&>())).Times(0);
    EXPECT_CALL(client, Close(false)).Times(1);

    Handlers::Account_Create(&client, AccountCreate(12346, "testuser"));
}

TEST_F(AccountTests, Create_WrongSessionIdWithEnforceSessionsOff_IsProcessed)
{
    server->world->config["EnforceSessions"] = false;
    MockClient client(server.get());
    client.create_id = 12345;

    EXPECT_CALL(client, Send(PacketEq(net::server::AccountReplyServerPacket::ForNotApproved()))).Times(1);
    EXPECT_CALL(client, Close(_)).Times(0);

    Handlers::Account_Create(&client, AccountCreate(12346, "test user!"));
}

TEST_F(AccountTests, Create_InvalidName_RepliesNotApproved)
{
    MockClient client(server.get());
    client.create_id = 12345;

    EXPECT_CALL(client, Send(PacketEq(net::server::AccountReplyServerPacket::ForNotApproved()))).Times(1);

    Handlers::Account_Create(&client, AccountCreate(client.create_id, "test user!"));
}

TEST_F(AccountTests, Create_ExistingName_RepliesExists)
{
    GivenAccountExists();
    MockClient client(server.get());
    client.create_id = 12345;

    EXPECT_CALL(client, Send(PacketEq(net::server::AccountReplyServerPacket::ForExists()))).Times(1);

    Handlers::Account_Create(&client, AccountCreate(client.create_id, "testuser"));
}

TEST_F(AccountTests, Create_InvalidHdid_IsIgnored)
{
    MockClient client(server.get());
    client.create_id = 12345;

    EXPECT_CALL(client, Send(An<const net::Packet&>())).Times(0);
    EXPECT_CALL(client, Close(_)).Times(0);

    auto request = AccountCreate(client.create_id, "testuser");
    request.hdid = "not a number";
    Handlers::Account_Create(&client, request);
}

TEST_F(AccountTests, Create_ValidRequest_InsertsAccountWithHashedPasswordAndRepliesCreated)
{
    MockClient client(server.get());
    client.create_id = 12345;
    EXPECT_CALL(client, Connected()).WillRepeatedly(Return(true));

    EXPECT_CALL(Db(), RawQuery(AllOf(StartsWith("INSERT INTO accounts"), HasSubstr(", testuser,"), HasSubstr(",1234567890,"), Not(HasSubstr("testpass"))), _, _))
        .WillOnce(Return(Database_Result()));
    EXPECT_CALL(client, Send(PacketEq(net::server::AccountReplyServerPacket::ForCreated()))).Times(1);

    Handlers::Account_Create(&client, AccountCreate(client.create_id, "TestUser"));
    WaitForAsyncOperation(client);

    EXPECT_EQ(0, client.create_id);
}

TEST_F(AccountTests, Agree_CorrectOldPassword_StoresNewPasswordAndRepliesChanged)
{
    GivenAccountWithPassword("testuser", "oldpass");
    MockClient client(server.get());
    TestPlayer player(server->world, &client, "testuser");
    EXPECT_CALL(client, Connected()).WillRepeatedly(Return(true));

    EXPECT_CALL(Db(), RawQuery(AllOf(StartsWith("UPDATE accounts SET password = "), HasSubstr("testuser"), Not(HasSubstr("newpass"))), _, _))
        .WillOnce(Return(Database_Result()));
    EXPECT_CALL(client, Send(PacketEq(net::server::AccountReplyServerPacket::ForChanged()))).Times(1);

    Handlers::Account_Agree(player.player.get(), AccountAgree("testuser", "oldpass", "newpass"));
    WaitForAsyncOperation(client);
}

TEST_F(AccountTests, Agree_WrongOldPassword_RepliesChangeFailed)
{
    GivenAccountWithPassword("testuser", "oldpass");
    MockClient client(server.get());
    TestPlayer player(server->world, &client, "testuser");
    EXPECT_CALL(client, Connected()).WillRepeatedly(Return(true));

    EXPECT_CALL(Db(), RawQuery(StartsWith("UPDATE accounts SET password = "), _, _)).Times(0);
    EXPECT_CALL(client, Send(PacketEq(net::server::AccountReplyServerPacket::ForChangeFailed()))).Times(1);

    Handlers::Account_Agree(player.player.get(), AccountAgree("testuser", "wrongpass", "newpass"));
    WaitForAsyncOperation(client);
}
