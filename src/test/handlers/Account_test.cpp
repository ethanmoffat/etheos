#include "../testhelper/mocks.hpp"
#include "../testhelper/packets.hpp"
#include "../testhelper/setup.hpp"

// include the CPP file with the Account functions in it for testing
#include "../../handlers/Account.cpp"

#include "console.hpp"

#include <memory>

namespace net = eolib::protocol::net;

namespace
{

constexpr unsigned short AccountTestServerPort = 38084;

class AccountTests : public testing::Test
{
protected:
    std::shared_ptr<Database> database;
    std::unique_ptr<EOServer> server;

    void SetUp() override
    {
        Console::SuppressOutput(true);
        database = CreateMockDatabase();

        Config config, admin_config;
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

    template <typename TData> static net::server::AccountReplyServerPacket ExpectedAccountReply(net::server::AccountReply reply_code)
    {
        net::server::AccountReplyServerPacket reply;
        reply.reply_code = reply_code;
        reply.reply_code_data = TData();
        return reply;
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
};

}

TEST_F(AccountTests, Request_AvailableName_RepliesWithSessionIdAndSequenceStart)
{
    MockClient client(server.get());

    net::server::AccountReplyServerPacket reply;
    EXPECT_CALL(client, Send(An<const net::Packet&>()))
        .WillOnce(Invoke([&](const net::Packet& packet) { reply = dynamic_cast<const net::server::AccountReplyServerPacket&>(packet); }));

    Handlers::Account_Request(&client, AccountRequest("testuser"));

    // A reply code above 9 is the account creation session ID
    EXPECT_GT(client.create_id, 9);
    EXPECT_EQ(client.create_id, static_cast<int>(reply.reply_code));
    EXPECT_NE(nullptr, std::get_if<net::server::AccountReplyServerPacket::ReplyCodeDataDefault>(&reply.reply_code_data));
}

TEST_F(AccountTests, Request_InvalidName_RepliesNotApproved)
{
    MockClient client(server.get());

    EXPECT_CALL(client, Send(PacketEq(ExpectedAccountReply<net::server::AccountReplyServerPacket::ReplyCodeDataNotApproved>(net::server::AccountReply::NotApproved)))).Times(1);

    Handlers::Account_Request(&client, AccountRequest("test user!"));

    EXPECT_EQ(0, client.create_id);
}

TEST_F(AccountTests, Request_ExistingName_RepliesExists)
{
    GivenAccountExists();
    MockClient client(server.get());

    EXPECT_CALL(client, Send(PacketEq(ExpectedAccountReply<net::server::AccountReplyServerPacket::ReplyCodeDataExists>(net::server::AccountReply::Exists)))).Times(1);

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

TEST_F(AccountTests, Create_InvalidName_RepliesNotApproved)
{
    MockClient client(server.get());
    client.create_id = 12345;

    EXPECT_CALL(client, Send(PacketEq(ExpectedAccountReply<net::server::AccountReplyServerPacket::ReplyCodeDataNotApproved>(net::server::AccountReply::NotApproved)))).Times(1);

    Handlers::Account_Create(&client, AccountCreate(client.create_id, "test user!"));
}

TEST_F(AccountTests, Create_ExistingName_RepliesExists)
{
    GivenAccountExists();
    MockClient client(server.get());
    client.create_id = 12345;

    EXPECT_CALL(client, Send(PacketEq(ExpectedAccountReply<net::server::AccountReplyServerPacket::ReplyCodeDataExists>(net::server::AccountReply::Exists)))).Times(1);

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
