#include "../testhelper/mocks.hpp"
#include "../testhelper/packets.hpp"
#include "../testhelper/setup.hpp"

// include the CPP file with the Init functions in it for testing
#include "../../handlers/Init.cpp"

#include "console.hpp"

#include <ctime>
#include <memory>

namespace net = eolib::protocol::net;

namespace
{

constexpr unsigned short InitTestServerPort = 38082;
constexpr int DefaultVersion = 28;

class InitTests : public testing::Test
{
protected:
    std::shared_ptr<Database> database;
    std::unique_ptr<EOServer> server;

    void SetUp() override
    {
        Console::SuppressOutput(true);
        database = CreateMockDatabase();
    }

    void CreateServer()
    {
        Config config, admin_config;
        CreateConfigWithTestDefaults(config, admin_config);
        server = std::make_unique<EOServer>(IPAddress("127.0.0.1"), InitTestServerPort, CreateMockDatabaseFactory(database), config, admin_config);
    }

    void GivenBanExpires(int expires)
    {
        Database_Result result;
        std::unordered_map<std::string, util::variant> columns;
        columns["expires"] = util::variant(expires);
        result.push_back(columns);

        EXPECT_CALL(*dynamic_cast<MockDatabase*>(database.get()), RawQuery(HasSubstr("FROM bans"), _, _))
            .WillRepeatedly(Return(result));
    }

    static net::client::InitInitClientPacket InitRequest(int version)
    {
        net::client::InitInitClientPacket packet;
        packet.challenge = 123456;
        packet.version.major = 0;
        packet.version.minor = 0;
        packet.version.patch = version;
        packet.hdid = "1234567890";
        return packet;
    }

    static net::server::InitInitServerPacket BannedReply(net::server::InitInitServerPacket::ReplyCodeDataBanned banned)
    {
        net::server::InitInitServerPacket reply;
        reply.reply_code = net::server::InitReply::Banned;
        reply.reply_code_data = std::move(banned);
        return reply;
    }
};

}

TEST_F(InitTests, Init_SupportedVersion_RepliesOkAndInitializesClient)
{
    CreateServer();
    MockClient client(server.get());

    net::server::InitInitServerPacket reply;
    EXPECT_CALL(client, Send(An<const net::Packet&>()))
        .WillOnce(Invoke([&](const net::Packet& packet) { reply = dynamic_cast<const net::server::InitInitServerPacket&>(packet); }));
    EXPECT_CALL(client, Close(_)).Times(0);

    const auto request = InitRequest(DefaultVersion);
    Handlers::Init_Init(&client, request);

    ASSERT_EQ(net::server::InitReply::Ok, reply.reply_code);
    const auto& ok = std::get<net::server::InitInitServerPacket::ReplyCodeDataOk>(reply.reply_code_data);
    EXPECT_EQ(static_cast<int>(client.id), ok.player_id);
    EXPECT_EQ(eolib::encrypt::ServerVerifier::Hash(request.challenge), ok.challenge_response);
    EXPECT_EQ(client.server_encryption_multiple, ok.server_encryption_multiple);
    EXPECT_EQ(client.client_encryption_multiple, ok.client_encryption_multiple);
    EXPECT_EQ(EOClient::Initialized, client.state);
}

TEST_F(InitTests, Init_UnsupportedVersion_RepliesOutOfDateAndCloses)
{
    CreateServer();
    MockClient client(server.get());

    net::server::InitInitServerPacket::ReplyCodeDataOutOfDate out_of_date;
    out_of_date.version.patch = DefaultVersion;

    net::server::InitInitServerPacket expected;
    expected.reply_code = net::server::InitReply::OutOfDate;
    expected.reply_code_data = out_of_date;

    EXPECT_CALL(client, Send(PacketEq(expected))).Times(1);
    EXPECT_CALL(client, Close(false)).Times(1);

    Handlers::Init_Init(&client, InitRequest(DefaultVersion - 1));

    EXPECT_EQ(EOClient::Uninitialized, client.state);
}

TEST_F(InitTests, Init_PermanentBan_RepliesPermanentBanAndCloses)
{
    GivenBanExpires(0);
    CreateServer();
    MockClient client(server.get());

    net::server::InitInitServerPacket::ReplyCodeDataBanned banned;
    banned.ban_type = net::server::InitBanType::Permanent;

    EXPECT_CALL(client, Send(PacketEq(BannedReply(banned)))).Times(1);
    EXPECT_CALL(client, Close(false)).Times(1);

    Handlers::Init_Init(&client, InitRequest(DefaultVersion));
}

TEST_F(InitTests, Init_TemporaryBan_RepliesTemporaryBanWithMinutesRemaining)
{
    // 90 seconds rounds up to 2 minutes
    GivenBanExpires(static_cast<int>(std::time(nullptr)) + 90);
    CreateServer();
    MockClient client(server.get());

    net::server::InitInitServerPacket::ReplyCodeDataBanned::BanTypeDataTemporary temporary;
    temporary.minutes_remaining = 2;

    net::server::InitInitServerPacket::ReplyCodeDataBanned banned;
    banned.ban_type = net::server::InitBanType::Temporary;
    banned.ban_type_data = temporary;

    EXPECT_CALL(client, Send(PacketEq(BannedReply(banned)))).Times(1);
    EXPECT_CALL(client, Close(false)).Times(1);

    Handlers::Init_Init(&client, InitRequest(DefaultVersion));
}

TEST_F(InitTests, Init_InvalidHdid_ClosesWithoutReply)
{
    CreateServer();
    MockClient client(server.get());

    EXPECT_CALL(client, Send(An<const net::Packet&>())).Times(0);
    EXPECT_CALL(client, Close(false)).Times(1);

    auto request = InitRequest(DefaultVersion);
    request.hdid = "not a number";
    Handlers::Init_Init(&client, request);
}
