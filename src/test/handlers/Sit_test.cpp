#include "../testhelper/playing.hpp"

// include the CPP file with the Sit functions in it for testing
#include "../../handlers/Sit.cpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short SitTestServerPort = 38104;
constexpr int SitTestMapId = 9;

class SitTests : public PlayingTest
{
protected:
    SitTests()
        : PlayingTest(SitTestServerPort, SitTestMapId)
    {
    }
};

}


TEST_F(SitTests, Request_SitWhileStanding_SitsOnFloor)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& sitter = GivenCharacter(2, 3);
    PlayingCharacter& observer = GivenCharacter(3, 3);
    sitter.character->direction = Direction::Left;

    client::SitRequestClientPacket packet;
    packet.sit_action = client::SitAction::Sit;
    Handlers::Sit_Request(sitter.character, packet);

    EXPECT_EQ(SitState::Floor, sitter.character->sitting);

    const auto reply = LastSent<server::SitReplyServerPacket>(sitter);
    ASSERT_TRUE(reply);
    EXPECT_EQ(sitter.character->PlayerID(), reply->player_id);
    EXPECT_EQ(2, reply->coords.x);
    EXPECT_EQ(3, reply->coords.y);
    EXPECT_EQ(eolib::protocol::Direction::Left, reply->direction);
    EXPECT_FALSE(LastSent<server::SitPlayerServerPacket>(sitter));

    const auto nearby = LastSent<server::SitPlayerServerPacket>(observer);
    ASSERT_TRUE(nearby);
    EXPECT_EQ(sitter.character->PlayerID(), nearby->player_id);
    EXPECT_EQ(2, nearby->coords.x);
    EXPECT_EQ(3, nearby->coords.y);
    EXPECT_EQ(eolib::protocol::Direction::Left, nearby->direction);
}

TEST_F(SitTests, Request_StandWhileOnFloor_Stands)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& sitter = GivenCharacter(2, 3);
    PlayingCharacter& observer = GivenCharacter(3, 3);
    sitter.character->sitting = SitState::Floor;

    client::SitRequestClientPacket packet;
    packet.sit_action = client::SitAction::Stand;
    Handlers::Sit_Request(sitter.character, packet);

    EXPECT_EQ(SitState::Stand, sitter.character->sitting);

    const auto reply = LastSent<server::SitCloseServerPacket>(sitter);
    ASSERT_TRUE(reply);
    EXPECT_EQ(sitter.character->PlayerID(), reply->player_id);
    EXPECT_EQ(2, reply->coords.x);
    EXPECT_EQ(3, reply->coords.y);

    const auto nearby = LastSent<server::SitRemoveServerPacket>(observer);
    ASSERT_TRUE(nearby);
    EXPECT_EQ(sitter.character->PlayerID(), nearby->player_id);
    EXPECT_EQ(2, nearby->coords.x);
    EXPECT_EQ(3, nearby->coords.y);
}

TEST_F(SitTests, Request_SitWhileOnFloor_Stands)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& sitter = GivenCharacter(2, 3);
    sitter.character->sitting = SitState::Floor;

    client::SitRequestClientPacket packet;
    packet.sit_action = client::SitAction::Sit;
    Handlers::Sit_Request(sitter.character, packet);

    EXPECT_EQ(SitState::Stand, sitter.character->sitting);
    EXPECT_TRUE(LastSent<server::SitCloseServerPacket>(sitter));
}

TEST_F(SitTests, Request_OnChair_IsIgnored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& sitter = GivenCharacter(2, 3);
    PlayingCharacter& observer = GivenCharacter(3, 3);
    sitter.character->sitting = SitState::Chair;

    for (const client::SitAction action : {client::SitAction::Sit, client::SitAction::Stand})
    {
        client::SitRequestClientPacket packet;
        packet.sit_action = action;
        Handlers::Sit_Request(sitter.character, packet);
    }

    EXPECT_EQ(SitState::Chair, sitter.character->sitting);
    EXPECT_TRUE(sitter.sent.empty());
    EXPECT_TRUE(observer.sent.empty());
}

TEST_F(SitTests, Request_StandWhileStanding_IsIgnored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& sitter = GivenCharacter(2, 3);

    client::SitRequestClientPacket packet;
    packet.sit_action = client::SitAction::Stand;
    Handlers::Sit_Request(sitter.character, packet);

    EXPECT_EQ(SitState::Stand, sitter.character->sitting);
    EXPECT_TRUE(sitter.sent.empty());
}
