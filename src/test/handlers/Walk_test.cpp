#include "../testhelper/playing.hpp"

// include the CPP file with the Walk functions in it for testing
#include "../../handlers/Walk.cpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short WalkTestServerPort = 38102;
constexpr int WalkTestMapId = 7;

class WalkTests : public PlayingTest
{
protected:
    WalkTests()
        : PlayingTest(WalkTestServerPort, WalkTestMapId)
    {
    }

    static client::WalkAction WalkAction(eolib::protocol::Direction direction, int x, int y, int timestamp = 100)
    {
        client::WalkAction walk;
        walk.direction = direction;
        walk.timestamp = timestamp;
        walk.coords.x = x;
        walk.coords.y = y;
        return walk;
    }

    static client::WalkPlayerClientPacket WalkPlayer(eolib::protocol::Direction direction, int x, int y, int timestamp = 100)
    {
        client::WalkPlayerClientPacket packet;
        packet.walk_action = WalkAction(direction, x, y, timestamp);
        return packet;
    }
};

}


TEST_F(WalkTests, Player_OpenTile_MovesAndNotifiesNearby)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& walker = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(5, 2);

    Handlers::Walk_Player(walker.character, WalkPlayer(eolib::protocol::Direction::Right, 3, 2));

    EXPECT_EQ(3, walker.character->loc.x);
    EXPECT_EQ(2, walker.character->loc.y);
    EXPECT_EQ(0, Timestamp(100) - walker.character->timestamp);
    EXPECT_FALSE(SentLegacy(walker, PACKET_REFRESH, PACKET_REPLY));

    const auto walk = LastSent<server::WalkPlayerServerPacket>(observer);
    ASSERT_TRUE(walk);
    EXPECT_EQ(walker.character->PlayerID(), walk->player_id);
    EXPECT_EQ(eolib::protocol::Direction::Right, walk->direction);
    EXPECT_EQ(3, walk->coords.x);
    EXPECT_EQ(2, walk->coords.y);
}

TEST_F(WalkTests, Player_ObserverOutOfRange_IsNotNotified)
{
    GivenMap(EmptyEmf(30, 10));
    PlayingCharacter& walker = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(20, 2);

    Handlers::Walk_Player(walker.character, WalkPlayer(eolib::protocol::Direction::Left, 1, 2));

    EXPECT_EQ(1, walker.character->loc.x);
    EXPECT_FALSE(LastSent<server::WalkPlayerServerPacket>(observer));
}

TEST_F(WalkTests, Player_ItemComingIntoView_IsListedInReply)
{
    GivenMap(EmptyEmf(20, 10));
    const int see_distance = server->world->config["SeeDistance"];
    const auto item = map->AddItem(1, 5, 3 + see_distance, 2);
    PlayingCharacter& walker = GivenCharacter(2, 2);

    Handlers::Walk_Player(walker.character, WalkPlayer(eolib::protocol::Direction::Right, 3, 2));

    const auto reply = LastSent<server::WalkReplyServerPacket>(walker);
    ASSERT_TRUE(reply);
    EXPECT_TRUE(reply->player_ids.empty());
    EXPECT_TRUE(reply->npc_indexes.empty());
    ASSERT_EQ(1u, reply->items.size());
    EXPECT_EQ(item->uid, reply->items[0].uid);
    EXPECT_EQ(1, reply->items[0].id);
    EXPECT_EQ(3 + see_distance, reply->items[0].coords.x);
    EXPECT_EQ(2, reply->items[0].coords.y);
    EXPECT_EQ(5, reply->items[0].amount);
}

TEST_F(WalkTests, Player_TimestampTooSoon_IsIgnored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& walker = GivenCharacter(2, 2);
    walker.character->timestamp = 100;

    Handlers::Walk_Player(walker.character, WalkPlayer(eolib::protocol::Direction::Right, 3, 2, 135));

    EXPECT_EQ(2, walker.character->loc.x);
    EXPECT_EQ(0, Timestamp(100) - walker.character->timestamp);
    EXPECT_TRUE(walker.sent.empty());
    EXPECT_TRUE(walker.legacy_sent.empty());
}

TEST_F(WalkTests, Player_TimestampTooSoonWithoutEnforceTimestamps_Moves)
{
    server->world->config["EnforceTimestamps"] = "false";
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& walker = GivenCharacter(2, 2);
    walker.character->timestamp = 100;

    Handlers::Walk_Player(walker.character, WalkPlayer(eolib::protocol::Direction::Right, 3, 2, 135));

    EXPECT_EQ(3, walker.character->loc.x);
}

TEST_F(WalkTests, Player_Sitting_IsIgnored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& walker = GivenCharacter(2, 2);
    walker.character->sitting = SitState::Floor;

    Handlers::Walk_Player(walker.character, WalkPlayer(eolib::protocol::Direction::Right, 3, 2));

    EXPECT_EQ(2, walker.character->loc.x);
    EXPECT_EQ(0, Timestamp(100) - walker.character->timestamp);
    EXPECT_TRUE(walker.legacy_sent.empty());
}

TEST_F(WalkTests, Player_InvalidDirection_RefreshesClient)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& walker = GivenCharacter(2, 2);

    Handlers::Walk_Player(walker.character, WalkPlayer(static_cast<eolib::protocol::Direction>(4), 2, 2));

    EXPECT_EQ(2, walker.character->loc.x);
    EXPECT_EQ(2, walker.character->loc.y);
    EXPECT_TRUE(SentLegacy(walker, PACKET_REFRESH, PACKET_REPLY));
}

TEST_F(WalkTests, Player_BlockedTile_RefreshesClient)
{
    eolib::protocol::map::Emf file = EmptyEmf(10, 10);
    AddTileSpec(file, 3, 2, eolib::protocol::map::MapTileSpec::Wall);
    GivenMap(file);
    PlayingCharacter& walker = GivenCharacter(2, 2);

    Handlers::Walk_Player(walker.character, WalkPlayer(eolib::protocol::Direction::Right, 3, 2));

    EXPECT_EQ(2, walker.character->loc.x);
    EXPECT_TRUE(SentLegacy(walker, PACKET_REFRESH, PACKET_REPLY));
}

TEST_F(WalkTests, Player_MapEdge_RefreshesClient)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& walker = GivenCharacter(0, 2);

    Handlers::Walk_Player(walker.character, WalkPlayer(eolib::protocol::Direction::Left, 0, 2));

    EXPECT_EQ(0, walker.character->loc.x);
    EXPECT_TRUE(SentLegacy(walker, PACKET_REFRESH, PACKET_REPLY));
}

TEST_F(WalkTests, Player_ClientPositionMismatch_MovesAndRefreshesClient)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& walker = GivenCharacter(2, 2);

    Handlers::Walk_Player(walker.character, WalkPlayer(eolib::protocol::Direction::Right, 4, 2));

    EXPECT_EQ(3, walker.character->loc.x);
    EXPECT_TRUE(SentLegacy(walker, PACKET_REFRESH, PACKET_REPLY));
}

TEST_F(WalkTests, Player_Trading_ClosesTradeWithPartner)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& walker = GivenCharacter(2, 2);
    PlayingCharacter& partner = GivenCharacter(2, 3);
    walker.character->trading = true;
    walker.character->trade_partner = partner.character;
    partner.character->trading = true;
    partner.character->trade_partner = walker.character;

    Handlers::Walk_Player(walker.character, WalkPlayer(eolib::protocol::Direction::Right, 3, 2));

    EXPECT_TRUE(SentLegacy(partner, PACKET_TRADE, PACKET_CLOSE));
    EXPECT_FALSE(walker.character->trading);
    EXPECT_FALSE(partner.character->trading);
    EXPECT_EQ(nullptr, walker.character->trade_partner);
    EXPECT_EQ(nullptr, partner.character->trade_partner);
    EXPECT_EQ(3, walker.character->loc.x);
}

TEST_F(WalkTests, Admin_WithoutNowallAccess_IsIgnored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& walker = GivenCharacter(2, 2);

    client::WalkAdminClientPacket packet;
    packet.walk_action = WalkAction(eolib::protocol::Direction::Right, 3, 2);
    Handlers::Walk_Admin(walker.character, packet);

    EXPECT_EQ(2, walker.character->loc.x);
    EXPECT_NE(0, Timestamp(100) - walker.character->timestamp);
}

TEST_F(WalkTests, Admin_WithNowallAccess_WalksThroughWall)
{
    eolib::protocol::map::Emf file = EmptyEmf(10, 10);
    AddTileSpec(file, 3, 2, eolib::protocol::map::MapTileSpec::Wall);
    GivenMap(file);
    PlayingCharacter& walker = GivenCharacter(2, 2);
    walker.character->admin = ADMIN_HGM;

    client::WalkAdminClientPacket packet;
    packet.walk_action = WalkAction(eolib::protocol::Direction::Right, 3, 2);
    Handlers::Walk_Admin(walker.character, packet);

    EXPECT_EQ(3, walker.character->loc.x);
}

TEST_F(WalkTests, Spec_OpenTile_Moves)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& walker = GivenCharacter(2, 2);

    client::WalkSpecClientPacket packet;
    packet.walk_action = WalkAction(eolib::protocol::Direction::Down, 2, 3);
    Handlers::Walk_Spec(walker.character, packet);

    EXPECT_EQ(3, walker.character->loc.y);
}
