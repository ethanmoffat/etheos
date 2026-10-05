#include "../testhelper/playing.hpp"

// include the CPP file with the Chair functions in it for testing
#include "../../handlers/Chair.cpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <optional>
#include <tuple>

namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;
namespace emf = eolib::protocol::map;

namespace
{

constexpr unsigned short ChairTestServerPort = 38105;
constexpr int ChairTestMapId = 10;

class ChairTests : public PlayingTest
{
protected:
    ChairTests()
        : PlayingTest(ChairTestServerPort, ChairTestMapId)
    {
    }

    static constexpr int ChairX = 5;
    static constexpr int ChairY = 5;

    void GivenChair(emf::MapTileSpec spec)
    {
        emf::Emf file = EmptyEmf(10, 10);
        AddTileSpec(file, ChairX, ChairY, spec);
        GivenMap(file);
    }

    static client::ChairRequestClientPacket SitOn(int x, int y)
    {
        client::ChairRequestClientPacket::SitActionDataSit sit;
        sit.coords.x = x;
        sit.coords.y = y;
        client::ChairRequestClientPacket packet;
        packet.sit_action = client::SitAction::Sit;
        packet.sit_action_data = sit;
        return packet;
    }
};

// Chair tile spec, character offset from the chair, and the direction faced on the chair (nullopt if it can't be used)
using ChairCase = std::tuple<emf::MapTileSpec, int, int, std::optional<Direction>>;

class ChairOrientationTests : public ChairTests, public testing::WithParamInterface<ChairCase>
{
};

}


TEST_P(ChairOrientationTests, Request_SitFromSide_FacesChairDirectionWhenAllowed)
{
    const emf::MapTileSpec spec = std::get<0>(GetParam());
    const int dx = std::get<1>(GetParam());
    const int dy = std::get<2>(GetParam());
    const std::optional<Direction> direction = std::get<3>(GetParam());

    GivenChair(spec);
    PlayingCharacter& sitter = GivenCharacter(ChairX + dx, ChairY + dy);

    Handlers::Chair_Request(sitter.character, SitOn(ChairX, ChairY));

    if (!direction)
    {
        EXPECT_EQ(SitState::Stand, sitter.character->sitting);
        EXPECT_EQ(ChairX + dx, sitter.character->loc.x);
        EXPECT_EQ(ChairY + dy, sitter.character->loc.y);
        EXPECT_TRUE(sitter.sent.empty());
        return;
    }

    EXPECT_EQ(SitState::Chair, sitter.character->sitting);
    EXPECT_EQ(ChairX, sitter.character->loc.x);
    EXPECT_EQ(ChairY, sitter.character->loc.y);
    EXPECT_EQ(*direction, sitter.character->direction);

    const auto reply = LastSent<server::ChairReplyServerPacket>(sitter);
    ASSERT_TRUE(reply);
    EXPECT_EQ(sitter.character->PlayerID(), reply->player_id);
    EXPECT_EQ(ChairX, reply->coords.x);
    EXPECT_EQ(ChairY, reply->coords.y);
    EXPECT_EQ(*direction, reply->direction);
}

INSTANTIATE_TEST_SUITE_P(Chairs, ChairOrientationTests, testing::Values(
    ChairCase(emf::MapTileSpec::ChairDown, 0, 1, Direction::Down),
    ChairCase(emf::MapTileSpec::ChairDown, 0, -1, std::nullopt),
    ChairCase(emf::MapTileSpec::ChairUp, 0, -1, Direction::Up),
    ChairCase(emf::MapTileSpec::ChairUp, 0, 1, std::nullopt),
    ChairCase(emf::MapTileSpec::ChairLeft, -1, 0, Direction::Left),
    ChairCase(emf::MapTileSpec::ChairLeft, 1, 0, std::nullopt),
    ChairCase(emf::MapTileSpec::ChairRight, 1, 0, Direction::Right),
    ChairCase(emf::MapTileSpec::ChairRight, -1, 0, std::nullopt),
    ChairCase(emf::MapTileSpec::ChairDownRight, 1, 0, Direction::Right),
    ChairCase(emf::MapTileSpec::ChairDownRight, 0, 1, Direction::Down),
    ChairCase(emf::MapTileSpec::ChairDownRight, -1, 0, std::nullopt),
    ChairCase(emf::MapTileSpec::ChairUpLeft, -1, 0, Direction::Left),
    ChairCase(emf::MapTileSpec::ChairUpLeft, 0, -1, Direction::Up),
    ChairCase(emf::MapTileSpec::ChairUpLeft, 1, 0, std::nullopt),
    ChairCase(emf::MapTileSpec::ChairAll, 1, 0, Direction::Right),
    ChairCase(emf::MapTileSpec::ChairAll, -1, 0, Direction::Left),
    ChairCase(emf::MapTileSpec::ChairAll, 0, -1, Direction::Up),
    ChairCase(emf::MapTileSpec::ChairAll, 0, 1, Direction::Down),
    ChairCase(emf::MapTileSpec::ChairAll, 0, 3, std::nullopt),
    ChairCase(emf::MapTileSpec::ChairAll, -3, 0, std::nullopt),
    ChairCase(emf::MapTileSpec::Wall, 0, 1, std::nullopt)));

TEST_F(ChairTests, Request_Sit_NotifiesNearby)
{
    GivenChair(emf::MapTileSpec::ChairDown);
    PlayingCharacter& sitter = GivenCharacter(ChairX, ChairY + 1);
    PlayingCharacter& observer = GivenCharacter(ChairX + 1, ChairY + 1);

    Handlers::Chair_Request(sitter.character, SitOn(ChairX, ChairY));

    EXPECT_FALSE(LastSent<server::ChairPlayerServerPacket>(sitter));

    const auto nearby = LastSent<server::ChairPlayerServerPacket>(observer);
    ASSERT_TRUE(nearby);
    EXPECT_EQ(sitter.character->PlayerID(), nearby->player_id);
    EXPECT_EQ(ChairX, nearby->coords.x);
    EXPECT_EQ(ChairY, nearby->coords.y);
    EXPECT_EQ(eolib::protocol::Direction::Down, nearby->direction);
}

TEST_F(ChairTests, Request_ChairOccupied_IsIgnored)
{
    GivenChair(emf::MapTileSpec::ChairAll);
    PlayingCharacter& sitter = GivenCharacter(ChairX, ChairY + 1);
    GivenCharacter(ChairX, ChairY);

    Handlers::Chair_Request(sitter.character, SitOn(ChairX, ChairY));

    EXPECT_EQ(SitState::Stand, sitter.character->sitting);
    EXPECT_EQ(ChairY + 1, sitter.character->loc.y);
    EXPECT_TRUE(sitter.sent.empty());
}

TEST_F(ChairTests, Request_SitWhileOnFloor_IsIgnored)
{
    GivenChair(emf::MapTileSpec::ChairAll);
    PlayingCharacter& sitter = GivenCharacter(ChairX, ChairY + 1);
    sitter.character->sitting = SitState::Floor;

    Handlers::Chair_Request(sitter.character, SitOn(ChairX, ChairY));

    EXPECT_EQ(SitState::Floor, sitter.character->sitting);
    EXPECT_TRUE(sitter.sent.empty());
}

TEST_F(ChairTests, Request_StandFromChair_StepsOffInFacingDirection)
{
    GivenChair(emf::MapTileSpec::ChairDown);
    PlayingCharacter& sitter = GivenCharacter(ChairX, ChairY);
    PlayingCharacter& observer = GivenCharacter(ChairX + 1, ChairY + 1);
    sitter.character->sitting = SitState::Chair;
    sitter.character->direction = Direction::Down;

    client::ChairRequestClientPacket packet;
    packet.sit_action = client::SitAction::Stand;
    Handlers::Chair_Request(sitter.character, packet);

    EXPECT_EQ(SitState::Stand, sitter.character->sitting);
    EXPECT_EQ(ChairX, sitter.character->loc.x);
    EXPECT_EQ(ChairY + 1, sitter.character->loc.y);

    const auto reply = LastSent<server::ChairCloseServerPacket>(sitter);
    ASSERT_TRUE(reply);
    EXPECT_EQ(sitter.character->PlayerID(), reply->player_id);
    EXPECT_EQ(ChairX, reply->coords.x);
    EXPECT_EQ(ChairY + 1, reply->coords.y);

    const auto nearby = LastSent<server::SitRemoveServerPacket>(observer);
    ASSERT_TRUE(nearby);
    EXPECT_EQ(sitter.character->PlayerID(), nearby->player_id);
    EXPECT_EQ(ChairX, nearby->coords.x);
    EXPECT_EQ(ChairY + 1, nearby->coords.y);
}
