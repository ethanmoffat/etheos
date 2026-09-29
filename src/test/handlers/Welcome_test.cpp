#include "../testhelper/mocks.hpp"
#include "../testhelper/packets.hpp"
#include "../testhelper/players.hpp"
#include "../testhelper/setup.hpp"

// include the CPP file with the Welcome functions in it for testing
#include "../../handlers/Welcome.cpp"

#include "console.hpp"

#include <eolib/data/number_encoder.hpp>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>

namespace net = eolib::protocol::net;

namespace
{

constexpr unsigned short WelcomeTestServerPort = 38086;
constexpr int TestMapId = 5;

class WelcomeTests : public testing::Test
{
protected:
    std::unique_ptr<EOServer> server;
    std::unique_ptr<MockClient> client;
    std::unique_ptr<TestPlayer> test_player;

    void SetUp() override
    {
        Console::SuppressOutput(true);

        Config config, admin_config;
        CreateConfigWithTestDefaults(config, admin_config);
        server = std::make_unique<EOServer>(IPAddress("127.0.0.1"), WelcomeTestServerPort, CreateMockDatabaseFactory(CreateMockDatabase()), config, admin_config);

        client = std::make_unique<MockClient>(server.get());
        client->state = EOClient::LoggedIn;
        test_player = std::make_unique<TestPlayer>(server->world, client.get());
    }

    void TearDown() override
    {
        test_player.reset();
        client.reset();
        std::remove(TestMapFile().c_str());
    }

    Player* player() const
    {
        return test_player->player.get();
    }

    static std::string TestMapFile()
    {
        char name[16];
        std::snprintf(name, sizeof name, "%05i.emf", TestMapId);
        return name;
    }

    static std::vector<std::uint8_t> ReadAll(const std::string& filename)
    {
        std::ifstream file(filename, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    static net::client::WelcomeAgreeClientPacket AgreeMap(int session_id, int map_id)
    {
        net::client::WelcomeAgreeClientPacket::FileTypeDataEmf emf;
        emf.file_id = map_id;

        net::client::WelcomeAgreeClientPacket packet;
        packet.file_type = net::client::FileType::Emf;
        packet.session_id = session_id;
        packet.file_type_data = emf;
        return packet;
    }

    // Writes a map file with the given RID in the working directory, which is used as the MapDir
    std::vector<std::uint8_t> GivenMapFile(int rid)
    {
        std::vector<std::uint8_t> content(0x40, 0x01);
        const auto encoded_rid = eolib::data::NumberEncoder::EncodeNumber(rid);
        content[0x03] = encoded_rid[0];
        content[0x04] = encoded_rid[1];

        std::ofstream file(TestMapFile(), std::ios::binary);
        file.write(reinterpret_cast<const char*>(content.data()), content.size());

        server->world->config["MapDir"] = "./";
        return content;
    }

};

}

TEST_F(WelcomeTests, Request_UnknownCharacter_DoesNotSelect)
{
    test_player->AddCharacter(1, "first");

    EXPECT_CALL(*client, Send(An<const net::Packet&>())).Times(0);

    net::client::WelcomeRequestClientPacket request;
    request.character_id = 2;
    Handlers::Welcome_Request(player(), request);

    EXPECT_EQ(nullptr, player()->character);
}

TEST_F(WelcomeTests, Msg_WrongCharacter_RepliesServerBusyAndCloses)
{
    player()->character = test_player->AddCharacter(1, "first");

    net::server::WelcomeReplyServerPacket expected;
    expected.welcome_code = net::server::WelcomeCode::ServerBusy;

    EXPECT_CALL(*client, Send(PacketEq(expected))).Times(1);
    EXPECT_CALL(*client, Close(false)).Times(1);

    net::client::WelcomeMsgClientPacket request;
    request.session_id = player()->id;
    request.character_id = 2;
    Handlers::Welcome_Msg(player(), request);

    EXPECT_EQ(EOClient::LoggedIn, client->state);
}

TEST_F(WelcomeTests, Agree_WrongSessionId_ClosesClient)
{
    EXPECT_CALL(*client, Send(An<const net::Packet&>())).Times(0);
    EXPECT_CALL(*client, Close(false)).Times(1);

    Handlers::Welcome_Agree(player(), AgreeMap(player()->id + 1, TestMapId));
}

TEST_F(WelcomeTests, Agree_PubFile_UploadsWholeFile)
{
    net::server::InitInitServerPacket reply;
    EXPECT_CALL(*client, Send(An<const net::Packet&>()))
        .WillOnce(Invoke([&](const net::Packet& packet) { reply = dynamic_cast<const net::server::InitInitServerPacket&>(packet); }));

    net::client::WelcomeAgreeClientPacket request;
    request.file_type = net::client::FileType::Eif;
    request.session_id = player()->id;
    request.file_type_data = net::client::WelcomeAgreeClientPacket::FileTypeDataEif();
    Handlers::Welcome_Agree(player(), request);

    ASSERT_EQ(net::server::InitReply::FileEif, reply.reply_code);
    const auto& data = std::get<net::server::InitInitServerPacket::ReplyCodeDataFileEif>(reply.reply_code_data);
    EXPECT_EQ(1, data.pub_file.file_id);
    EXPECT_EQ(ReadAll(server->world->config["EIF"]), data.pub_file.content);
}

TEST_F(WelcomeTests, Agree_Map_UploadsWholeFile)
{
    const auto content = GivenMapFile(12345);

    net::server::InitInitServerPacket reply;
    EXPECT_CALL(*client, Send(An<const net::Packet&>()))
        .WillOnce(Invoke([&](const net::Packet& packet) { reply = dynamic_cast<const net::server::InitInitServerPacket&>(packet); }));

    Handlers::Welcome_Agree(player(), AgreeMap(player()->id, TestMapId));

    ASSERT_EQ(net::server::InitReply::FileEmf, reply.reply_code);
    EXPECT_EQ(content, std::get<net::server::InitInitServerPacket::ReplyCodeDataFileEmf>(reply.reply_code_data).map_file.content);
}

TEST_F(WelcomeTests, Agree_MapWithGlobalPK_UploadsPatchedFile)
{
    auto content = GivenMapFile(12345);
    server->world->config["GlobalPK"] = true;

    net::server::InitInitServerPacket reply;
    EXPECT_CALL(*client, Send(An<const net::Packet&>()))
        .WillOnce(Invoke([&](const net::Packet& packet) { reply = dynamic_cast<const net::server::InitInitServerPacket&>(packet); }));

    Handlers::Welcome_Agree(player(), AgreeMap(player()->id, TestMapId));

    Map::PatchGlobalPK(content);
    ASSERT_EQ(net::server::InitReply::FileEmf, reply.reply_code);
    EXPECT_EQ(content, std::get<net::server::InitInitServerPacket::ReplyCodeDataFileEmf>(reply.reply_code_data).map_file.content);
}

TEST_F(WelcomeTests, Agree_MissingMap_Throws)
{
    server->world->config["MapDir"] = "./missing/";

    EXPECT_CALL(*client, Send(An<const net::Packet&>())).Times(0);

    EXPECT_THROW(Handlers::Welcome_Agree(player(), AgreeMap(player()->id, TestMapId)), std::runtime_error);
}
