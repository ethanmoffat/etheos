#include "../testhelper/mocks.hpp"
#include "../testhelper/packets.hpp"
#include "../testhelper/players.hpp"
#include "../testhelper/setup.hpp"

// include the CPP file with the Character functions in it for testing
#include "../../handlers/Character.cpp"

#include "console.hpp"

#include <memory>

namespace net = eolib::protocol::net;

namespace
{

constexpr unsigned short CharacterTestServerPort = 38085;
constexpr int SessionId = 12345;

class CharacterTests : public testing::Test
{
protected:
    std::shared_ptr<Database> database;
    std::unique_ptr<EOServer> server;
    std::unique_ptr<MockClient> client;
    std::unique_ptr<TestPlayer> test_player;

    void SetUp() override
    {
        Console::SuppressOutput(true);
        database = CreateMockDatabase();

        Config config, admin_config;
        CreateConfigWithTestDefaults(config, admin_config);
        server = std::make_unique<EOServer>(IPAddress("127.0.0.1"), CharacterTestServerPort, CreateMockDatabaseFactory(database), config, admin_config);

        client = std::make_unique<MockClient>(server.get());
        test_player = std::make_unique<TestPlayer>(server->world, client.get());
        test_player->player->char_op_id = SessionId;
    }

    void TearDown() override
    {
        test_player.reset();
        client.reset();
    }

    Player* player() const
    {
        return test_player->player.get();
    }

    MockDatabase* mock_database() const
    {
        return dynamic_cast<MockDatabase*>(database.get());
    }

    template <typename TData> static net::server::CharacterReplyServerPacket ExpectedCharacterReply(net::server::CharacterReply reply_code, TData data = TData())
    {
        net::server::CharacterReplyServerPacket reply;
        reply.reply_code = reply_code;
        reply.reply_code_data = std::move(data);
        return reply;
    }

    static net::client::CharacterCreateClientPacket CharacterCreate(int session_id, const std::string& name)
    {
        net::client::CharacterCreateClientPacket packet;
        packet.session_id = session_id;
        packet.gender = eolib::protocol::Gender::Male;
        packet.hair_style = 1;
        packet.hair_color = 0;
        packet.skin = 0;
        packet.name = name;
        return packet;
    }
};

}

TEST_F(CharacterTests, Request_New_RepliesWithSessionId)
{
    player()->char_op_id = 0;

    net::server::CharacterReplyServerPacket reply;
    EXPECT_CALL(*client, Send(An<const net::Packet&>()))
        .WillOnce(Invoke([&](const net::Packet& packet) { reply = dynamic_cast<const net::server::CharacterReplyServerPacket&>(packet); }));

    Handlers::Character_Request(player(), net::client::CharacterRequestClientPacket());

    // A reply code above 9 is the character creation session ID
    EXPECT_GT(player()->char_op_id, 9);
    EXPECT_EQ(ExpectedCharacterReply<net::server::CharacterReplyServerPacket::ReplyCodeDataDefault>(static_cast<net::server::CharacterReply>(player()->char_op_id)), reply);
}

TEST_F(CharacterTests, Create_WrongSessionId_ClosesClient)
{
    EXPECT_CALL(*client, Send(An<const net::Packet&>())).Times(0);
    EXPECT_CALL(*client, Close(false)).Times(1);

    Handlers::Character_Create(player(), CharacterCreate(SessionId + 1, "testchar"));
}

TEST_F(CharacterTests, Create_HairStyleOutOfRange_ClosesClient)
{
    EXPECT_CALL(*client, Send(An<const net::Packet&>())).Times(0);
    EXPECT_CALL(*client, Close(false)).Times(1);

    auto request = CharacterCreate(SessionId, "testchar");
    request.hair_style = static_cast<int>(server->world->config["CreateMaxHairStyle"]) + 1;
    Handlers::Character_Create(player(), request);
}

TEST_F(CharacterTests, Create_TooManyCharacters_RepliesFull)
{
    server->world->config["MaxCharacters"] = 1;
    test_player->AddCharacter(1, "existing");

    EXPECT_CALL(*client, Send(PacketEq(ExpectedCharacterReply<net::server::CharacterReplyServerPacket::ReplyCodeDataFull>(net::server::CharacterReply::Full)))).Times(1);

    Handlers::Character_Create(player(), CharacterCreate(SessionId, "testchar"));

    EXPECT_EQ(0, player()->char_op_id);
}

TEST_F(CharacterTests, Create_InvalidName_RepliesNotApproved)
{
    EXPECT_CALL(*client, Send(PacketEq(ExpectedCharacterReply<net::server::CharacterReplyServerPacket::ReplyCodeDataNotApproved>(net::server::CharacterReply::NotApproved)))).Times(1);

    Handlers::Character_Create(player(), CharacterCreate(SessionId, "test char!"));
}

TEST_F(CharacterTests, Create_ExistingName_RepliesExists)
{
    Database_Result result;
    result.push_back({{"1", util::variant(1)}});
    EXPECT_CALL(*mock_database(), RawQuery(HasSubstr("FROM characters"), _, _)).WillRepeatedly(Return(result));

    EXPECT_CALL(*client, Send(PacketEq(ExpectedCharacterReply<net::server::CharacterReplyServerPacket::ReplyCodeDataExists>(net::server::CharacterReply::Exists)))).Times(1);

    Handlers::Character_Create(player(), CharacterCreate(SessionId, "testchar"));
}

TEST_F(CharacterTests, Remove_OwnedCharacter_DeletesAndRepliesWithRemainingCharacters)
{
    test_player->AddCharacter(1, "first");
    Character* remaining = test_player->AddCharacter(2, "second");

    EXPECT_CALL(*mock_database(), RawQuery(HasSubstr("DELETE FROM characters"), _, _)).Times(1);
    EXPECT_CALL(*mock_database(), RawQuery(HasSubstr("UPDATE characters SET partner"), _, _)).Times(1);

    net::server::CharacterReplyServerPacket::ReplyCodeDataDeleted deleted;
    deleted.characters.push_back(remaining->SelectionListEntry());
    EXPECT_CALL(*client, Send(PacketEq(ExpectedCharacterReply(net::server::CharacterReply::Deleted, deleted)))).Times(1);

    net::client::CharacterRemoveClientPacket request;
    request.session_id = SessionId;
    request.character_id = 1;
    Handlers::Character_Remove(player(), request);

    ASSERT_EQ(1u, player()->characters.size());
    EXPECT_EQ(remaining, player()->characters.front());
    EXPECT_EQ(0, player()->char_op_id);
}

TEST_F(CharacterTests, Remove_WrongSessionId_ClosesClient)
{
    test_player->AddCharacter(1, "first");

    EXPECT_CALL(*client, Send(An<const net::Packet&>())).Times(0);
    EXPECT_CALL(*client, Close(false)).Times(1);

    net::client::CharacterRemoveClientPacket request;
    request.session_id = SessionId + 1;
    request.character_id = 1;
    Handlers::Character_Remove(player(), request);

    EXPECT_EQ(1u, player()->characters.size());
}

TEST_F(CharacterTests, Take_OwnedCharacter_RepliesWithNewSessionId)
{
    test_player->AddCharacter(7, "first");

    net::server::CharacterPlayerServerPacket reply;
    EXPECT_CALL(*client, Send(An<const net::Packet&>()))
        .WillOnce(Invoke([&](const net::Packet& packet) { reply = dynamic_cast<const net::server::CharacterPlayerServerPacket&>(packet); }));

    net::client::CharacterTakeClientPacket request;
    request.character_id = 7;
    Handlers::Character_Take(player(), request);

    EXPECT_EQ(7, reply.character_id);
    EXPECT_EQ(player()->char_op_id, reply.session_id);
}

TEST_F(CharacterTests, Take_UnknownCharacter_ClosesClient)
{
    test_player->AddCharacter(7, "first");

    EXPECT_CALL(*client, Send(An<const net::Packet&>())).Times(0);
    EXPECT_CALL(*client, Close(false)).Times(1);

    net::client::CharacterTakeClientPacket request;
    request.character_id = 8;
    Handlers::Character_Take(player(), request);
}
