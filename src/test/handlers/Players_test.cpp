#include "../testhelper/playing.hpp"

// include the CPP file with the Players functions in it for testing
#include "../../handlers/Players.cpp"

#include <eolib/data/eo_reader.hpp>
#include <eolib/data/eo_writer.hpp>
#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace protocol = eolib::protocol;
namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short PlayersTestServerPort = 38132;
constexpr int PlayersTestMapId = 37;
constexpr int OtherMapId = 1;

class PlayersTests : public PlayingTest
{
protected:
    PlayersTests()
        : PlayingTest(PlayersTestServerPort, PlayersTestMapId)
    {
    }

    static client::PlayersAcceptClientPacket Find(const std::string& name)
    {
        client::PlayersAcceptClientPacket packet;
        packet.name = name;
        return packet;
    }

    static std::optional<server::InitInitServerPacket> LastInit(const PlayingCharacter& entry)
    {
        return LastSent<server::InitInitServerPacket>(entry);
    }

    // A connected client with no logged in character, recording its typed sends
    struct AnonymousClient
    {
        std::unique_ptr<testing::NiceMock<MockClient>> client;
        std::vector<std::vector<std::uint8_t>> sent;
    };

    std::unique_ptr<AnonymousClient> GivenAnonymousClient()
    {
        auto anonymous = std::make_unique<AnonymousClient>();
        anonymous->client = std::make_unique<testing::NiceMock<MockClient>>(server.get());
        auto& sent = anonymous->sent;
        ON_CALL(*anonymous->client, Send(testing::An<const eolib::protocol::net::Packet&>()))
            .WillByDefault([&sent](const eolib::protocol::net::Packet& packet)
            {
                eolib::data::EoWriter writer;
                packet.Serialize(writer);
                sent.push_back(std::move(writer).ToByteArray());
            });
        return anonymous;
    }

    static server::InitInitServerPacket Decode(const std::vector<std::uint8_t>& payload)
    {
        eolib::data::EoReader reader(payload);
        server::InitInitServerPacket packet;
        packet.Deserialize(reader);
        return packet;
    }
};

}

TEST_F(PlayersTests, Accept_SameMap_RepliesPong)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& finder = GivenOnlineCharacter(2, 2);
    PlayingCharacter& target = GivenOnlineCharacter(3, 3);

    Handlers::Players_Accept(finder.character, Find(target.character->SourceName()));

    const auto reply = LastSent<server::PlayersPongServerPacket>(finder);
    ASSERT_TRUE(reply);
    EXPECT_EQ(target.character->SourceName(), reply->name);
}

TEST_F(PlayersTests, Accept_OtherMap_RepliesNet242)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& finder = GivenOnlineCharacter(2, 2);
    PlayingCharacter& target = GivenOnlineCharacter(3, 3);
    target.character->mapid = OtherMapId;

    Handlers::Players_Accept(finder.character, Find(target.character->SourceName()));

    EXPECT_TRUE(LastSent<server::PlayersNet242ServerPacket>(finder));
}

TEST_F(PlayersTests, Accept_InvisibleCharacter_RepliesPing)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& finder = GivenOnlineCharacter(2, 2);
    PlayingCharacter& target = GivenOnlineCharacter(3, 3);
    target.character->hidden = Character::HideInvisible;

    Handlers::Players_Accept(finder.character, Find(target.character->SourceName()));

    const auto reply = LastSent<server::PlayersPingServerPacket>(finder);
    ASSERT_TRUE(reply);
    EXPECT_EQ(target.character->SourceName(), reply->name);
}

TEST_F(PlayersTests, Accept_UnknownCharacter_RepliesPing)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& finder = GivenOnlineCharacter(2, 2);

    Handlers::Players_Accept(finder.character, Find("nobody"));

    const auto reply = LastSent<server::PlayersPingServerPacket>(finder);
    ASSERT_TRUE(reply);
    EXPECT_EQ("nobody", reply->name);
}

TEST_F(PlayersTests, Request_LoggedIn_RepliesWithOnlinePlayers)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenOnlineCharacter(2, 2);
    PlayingCharacter& other = GivenOnlineCharacter(3, 3);
    other.character->level = 12;
    other.character->title = "Hero";
    other.character->clas = 2;
    other.character->admin = protocol::AdminLevel::Guardian;

    Handlers::Players_Request(requester.client.get(), client::PlayersRequestClientPacket());

    const auto reply = LastInit(requester);
    ASSERT_TRUE(reply);
    ASSERT_EQ(server::InitReply::PlayersList, reply->reply_code);
    const auto& players = reply->AsPlayersList()->players_list.players;
    ASSERT_EQ(2u, players.size());
    EXPECT_EQ(other.character->SourceName(), players[1].name);
    EXPECT_EQ("Hero", players[1].title);
    EXPECT_EQ(12, players[1].level);
    EXPECT_EQ(server::CharacterIcon::Gm, players[1].icon);
    EXPECT_EQ(2, players[1].class_id);
}

TEST_F(PlayersTests, Request_HiddenAdmin_ListedWithSuffixForSeehideAdmins)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenOnlineCharacter(2, 2);
    PlayingCharacter& hidden = GivenOnlineCharacter(3, 3);
    requester.character->admin = protocol::AdminLevel::GameMaster;
    hidden.character->admin = protocol::AdminLevel::Guardian;
    hidden.character->hidden = Character::HideOnline;

    Handlers::Players_Request(requester.client.get(), client::PlayersRequestClientPacket());

    const auto reply = LastInit(requester);
    ASSERT_TRUE(reply);
    const auto& players = reply->AsPlayersList()->players_list.players;
    ASSERT_EQ(2u, players.size());
    EXPECT_EQ(hidden.character->SourceName() + " " + server->world->i18n.Format("hidden_admin_suffix"), players[1].name);
}

TEST_F(PlayersTests, Request_HiddenAdmin_NotListedForPlayers)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenOnlineCharacter(2, 2);
    PlayingCharacter& hidden = GivenOnlineCharacter(3, 3);
    hidden.character->admin = protocol::AdminLevel::Guardian;
    hidden.character->hidden = Character::HideOnline;

    Handlers::Players_Request(requester.client.get(), client::PlayersRequestClientPacket());

    const auto reply = LastInit(requester);
    ASSERT_TRUE(reply);
    const auto& players = reply->AsPlayersList()->players_list.players;
    ASSERT_EQ(1u, players.size());
    EXPECT_EQ(requester.character->SourceName(), players[0].name);
}

TEST_F(PlayersTests, Request_AnonymousWithStats_ShowsBotIcon)
{
    server->world->config["AllowStats"] = "true";
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& bot = GivenOnlineCharacter(2, 2);
    bot.character->bot = true;
    const auto anonymous = GivenAnonymousClient();

    Handlers::Players_Request(anonymous->client.get(), client::PlayersRequestClientPacket());

    ASSERT_EQ(1u, anonymous->sent.size());
    const server::InitInitServerPacket reply = Decode(anonymous->sent[0]);
    const auto& players = reply.AsPlayersList()->players_list.players;
    ASSERT_EQ(1u, players.size());
    EXPECT_EQ(20, static_cast<int>(players[0].icon));
}

TEST_F(PlayersTests, Request_AnonymousWithoutStats_Ignored)
{
    server->world->config["AllowStats"] = "false";
    GivenMap(EmptyEmf(10, 10));
    GivenOnlineCharacter(2, 2);
    const auto anonymous = GivenAnonymousClient();

    Handlers::Players_Request(anonymous->client.get(), client::PlayersRequestClientPacket());

    EXPECT_TRUE(anonymous->sent.empty());
}

TEST_F(PlayersTests, List_HiddenAdmin_ListsPlainNamesForSeehideAdmins)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenOnlineCharacter(2, 2);
    PlayingCharacter& hidden = GivenOnlineCharacter(3, 3);
    requester.character->admin = protocol::AdminLevel::GameMaster;
    hidden.character->admin = protocol::AdminLevel::Guardian;
    hidden.character->hidden = Character::HideOnline;

    Handlers::Players_List(requester.client.get(), client::PlayersListClientPacket());

    const auto reply = LastInit(requester);
    ASSERT_TRUE(reply);
    ASSERT_EQ(server::InitReply::PlayersListFriends, reply->reply_code);
    const std::vector<std::string> expected{requester.character->SourceName(), hidden.character->SourceName()};
    EXPECT_EQ(expected, reply->AsPlayersListFriends()->players_list.players);
}
