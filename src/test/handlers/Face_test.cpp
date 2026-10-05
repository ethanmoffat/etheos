#include "../testhelper/playing.hpp"

// include the CPP file with the Face functions in it for testing
#include "../../handlers/Face.cpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short FaceTestServerPort = 38103;
constexpr int FaceTestMapId = 8;

class FaceTests : public PlayingTest
{
protected:
    FaceTests()
        : PlayingTest(FaceTestServerPort, FaceTestMapId)
    {
    }
};

}


TEST_F(FaceTests, Player_Standing_TurnsAndNotifiesNearby)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& facer = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 2);

    client::FacePlayerClientPacket packet;
    packet.direction = eolib::protocol::Direction::Up;
    Handlers::Face_Player(facer.character, packet);

    EXPECT_EQ(Direction::Up, facer.character->direction);
    EXPECT_TRUE(facer.sent.empty());

    const auto face = LastSent<server::FacePlayerServerPacket>(observer);
    ASSERT_TRUE(face);
    EXPECT_EQ(facer.character->PlayerID(), face->player_id);
    EXPECT_EQ(eolib::protocol::Direction::Up, face->direction);
}

TEST_F(FaceTests, Player_ObserverOutOfRange_IsNotNotified)
{
    GivenMap(EmptyEmf(30, 10));
    PlayingCharacter& facer = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(20, 2);

    client::FacePlayerClientPacket packet;
    packet.direction = eolib::protocol::Direction::Left;
    Handlers::Face_Player(facer.character, packet);

    EXPECT_EQ(Direction::Left, facer.character->direction);
    EXPECT_FALSE(LastSent<server::FacePlayerServerPacket>(observer));
}

TEST_F(FaceTests, Player_Sitting_IsIgnored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& facer = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 2);
    facer.character->sitting = SitState::Floor;

    client::FacePlayerClientPacket packet;
    packet.direction = eolib::protocol::Direction::Up;
    Handlers::Face_Player(facer.character, packet);

    EXPECT_EQ(Direction::Down, facer.character->direction);
    EXPECT_TRUE(observer.sent.empty());
}

TEST_F(FaceTests, Player_InvalidDirection_IsIgnored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& facer = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 2);

    client::FacePlayerClientPacket packet;
    packet.direction = static_cast<eolib::protocol::Direction>(4);
    Handlers::Face_Player(facer.character, packet);

    EXPECT_EQ(Direction::Down, facer.character->direction);
    EXPECT_TRUE(observer.sent.empty());
}
