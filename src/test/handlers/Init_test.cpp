#include "../testhelper/mocks.hpp"
#include "../testhelper/packets.hpp"
#include "../testhelper/setup.hpp"

// include the CPP file with the Init functions in it for testing
#include "../../handlers/Init.cpp"

#include "console.hpp"

#include <ctime>
#include <memory>

namespace net = eolib::protocol::net;
namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

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

    static client::InitInitClientPacket InitRequest(int version, int protocol_version = client::InitInitClientPacket::DEFAULT_PROTOCOL_VERSION)
    {
        client::InitInitClientPacket packet;
        packet.challenge = 123456;
        packet.version.major = 0;
        packet.version.minor = 0;
        packet.version.patch = version;
        packet.protocol_version = protocol_version;
        packet.hdid = "1234567890";
        return packet;
    }

};

}

TEST_F(InitTests, Init_SupportedVersion_RepliesOkAndInitializesClient)
{
    CreateServer();
    MockClient client(server.get());

    server::InitInitServerPacket reply;
    EXPECT_CALL(client, Send(An<const net::Packet&>()))
        .WillOnce(Invoke([&](const net::Packet& packet) { reply = dynamic_cast<const server::InitInitServerPacket&>(packet); }));
    EXPECT_CALL(client, Close(_)).Times(0);

    const auto request = InitRequest(DefaultVersion);
    Handlers::Init_Init(&client, request);

    const auto* ok = reply.AsOk();
    ASSERT_NE(nullptr, ok);
    EXPECT_EQ(static_cast<int>(client.id), ok->player_id);
    EXPECT_EQ(eolib::encrypt::ServerVerifier::Hash(request.challenge), ok->challenge_response);
    EXPECT_EQ(client.server_encryption_multiple, ok->server_encryption_multiple);
    EXPECT_EQ(client.client_encryption_multiple, ok->client_encryption_multiple);
    EXPECT_EQ(EOClient::Initialized, client.state);
}

TEST_F(InitTests, Init_UnsupportedVersion_RepliesOutOfDateAndCloses)
{
    CreateServer();
    MockClient client(server.get());

    server::InitInitServerPacket::ReplyCodeDataOutOfDate out_of_date;
    out_of_date.version.patch = DefaultVersion;

    auto expected = server::InitInitServerPacket::ForOutOfDate(out_of_date);

    EXPECT_CALL(client, Send(PacketEq(expected))).Times(1);
    EXPECT_CALL(client, Close(false)).Times(1);

    Handlers::Init_Init(&client, InitRequest(DefaultVersion - 1));

    EXPECT_EQ(EOClient::Uninitialized, client.state);
}

TEST_F(InitTests, Init_UnsupportedProtocolVersion_RepliesOutOfDateAndCloses)
{
    CreateServer();
    MockClient client(server.get());

    server::InitInitServerPacket::ReplyCodeDataOutOfDate out_of_date;
    out_of_date.version.patch = DefaultVersion;

    auto expected = server::InitInitServerPacket::ForOutOfDate(out_of_date);

    EXPECT_CALL(client, Send(PacketEq(expected))).Times(1);
    EXPECT_CALL(client, Close(false)).Times(1);

    Handlers::Init_Init(&client, InitRequest(DefaultVersion, client::InitInitClientPacket::DEFAULT_PROTOCOL_VERSION + 1));

    EXPECT_EQ(EOClient::Uninitialized, client.state);
}

TEST_F(InitTests, Init_UnsupportedProtocolVersionWithoutCheckVersion_RepliesOk)
{
    CreateServer();
    server->world->config["CheckVersion"] = false;
    MockClient client(server.get());

    server::InitInitServerPacket reply;
    EXPECT_CALL(client, Send(An<const net::Packet&>()))
        .WillOnce(Invoke([&](const net::Packet& packet) { reply = dynamic_cast<const server::InitInitServerPacket&>(packet); }));
    EXPECT_CALL(client, Close(_)).Times(0);

    Handlers::Init_Init(&client, InitRequest(DefaultVersion, client::InitInitClientPacket::DEFAULT_PROTOCOL_VERSION + 1));

    EXPECT_EQ(server::InitReply::Ok, reply.reply_code);
    EXPECT_EQ(EOClient::Initialized, client.state);
}

TEST_F(InitTests, Init_PermanentBan_RepliesPermanentBanAndCloses)
{
    GivenBanExpires(0);
    CreateServer();
    MockClient client(server.get());

    EXPECT_CALL(client, Send(PacketEq(server::InitInitServerPacket::ForBannedPermanent()))).Times(1);
    EXPECT_CALL(client, Close(false)).Times(1);

    Handlers::Init_Init(&client, InitRequest(DefaultVersion));
}

TEST_F(InitTests, Init_TemporaryBan_RepliesTemporaryBanWithMinutesRemaining)
{
    // 90 seconds rounds up to 2 minutes
    GivenBanExpires(static_cast<int>(std::time(nullptr)) + 90);
    CreateServer();
    MockClient client(server.get());

    server::InitInitServerPacket::ReplyCodeDataBanned::BanTypeDataTemporary temporary;
    temporary.minutes_remaining = 2;

    EXPECT_CALL(client, Send(PacketEq(server::InitInitServerPacket::ForBannedTemporary(temporary)))).Times(1);
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
