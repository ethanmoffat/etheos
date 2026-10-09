#include "../testhelper/playing.hpp"

// include the CPP file with the Door functions in it for testing
#include "../../handlers/Door.cpp"
#include "../../converters.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short DoorTestServerPort = 38106;
constexpr int DoorTestMapId = 11;

class DoorTests : public PlayingTest
{
protected:
    DoorTests()
        : PlayingTest(DoorTestServerPort, DoorTestMapId)
    {
    }

    static constexpr int DoorX = 2;
    static constexpr int DoorY = 1;

    void GivenDoor(int door, int width = 10)
    {
        eolib::protocol::map::Emf file = EmptyEmf(width, 10);
        AddWarp(file, DoorX, DoorY, door);
        GivenMap(file);
    }

    static client::DoorOpenClientPacket OpenAt(int x, int y)
    {
        client::DoorOpenClientPacket packet;
        packet.coords.x = x;
        packet.coords.y = y;
        return packet;
    }
};

}


TEST_F(DoorTests, Open_UnlockedDoorInRange_OpensForNearby)
{
    GivenDoor(1, 30);
    PlayingCharacter& opener = GivenCharacter(DoorX, DoorY + 1);
    PlayingCharacter& observer = GivenCharacter(DoorX + 1, DoorY + 1);
    PlayingCharacter& far_away = GivenCharacter(25, DoorY);

    Handlers::Door_Open(opener.character, OpenAt(DoorX, DoorY));

    EXPECT_TRUE(map->GetWarp(Converters::CoordsFrom(DoorX, DoorY)).open);
    EXPECT_FALSE(LastSent<server::DoorOpenServerPacket>(far_away));

    for (const PlayingCharacter* entry : {&opener, &observer})
    {
        const auto open = LastSent<server::DoorOpenServerPacket>(*entry);
        ASSERT_TRUE(open);
        EXPECT_EQ(DoorX, open->coords.x);
        EXPECT_EQ(DoorY, open->coords.y);
    }
}

TEST_F(DoorTests, Open_AlreadyOpen_IsIgnored)
{
    GivenDoor(1);
    PlayingCharacter& opener = GivenCharacter(DoorX, DoorY + 1);

    Handlers::Door_Open(opener.character, OpenAt(DoorX, DoorY));
    opener.sent.clear();
    Handlers::Door_Open(opener.character, OpenAt(DoorX, DoorY));

    EXPECT_TRUE(opener.sent.empty());
}

TEST_F(DoorTests, Open_LockedDoorWithoutKey_RepliesWithKeyNumber)
{
    GivenDoor(3);
    PlayingCharacter& opener = GivenCharacter(DoorX, DoorY + 1);

    Handlers::Door_Open(opener.character, OpenAt(DoorX, DoorY));

    EXPECT_FALSE(map->GetWarp(Converters::CoordsFrom(DoorX, DoorY)).open);
    EXPECT_FALSE(LastSent<server::DoorOpenServerPacket>(opener));

    const auto close = LastSent<server::DoorCloseServerPacket>(opener);
    ASSERT_TRUE(close);
    EXPECT_EQ(3, close->key);
}

TEST_F(DoorTests, Open_WarpWithoutDoor_IsIgnored)
{
    GivenDoor(0);
    PlayingCharacter& opener = GivenCharacter(DoorX, DoorY + 1);

    Handlers::Door_Open(opener.character, OpenAt(DoorX, DoorY));

    EXPECT_TRUE(opener.sent.empty());
}

TEST_F(DoorTests, Open_NoWarpAtCoordinates_IsIgnored)
{
    GivenDoor(1);
    PlayingCharacter& opener = GivenCharacter(DoorX, DoorY + 1);

    Handlers::Door_Open(opener.character, OpenAt(DoorX + 1, DoorY));

    EXPECT_TRUE(opener.sent.empty());
}

TEST_F(DoorTests, Open_OutOfRange_IsIgnored)
{
    GivenDoor(1, 30);
    PlayingCharacter& opener = GivenCharacter(25, DoorY);

    Handlers::Door_Open(opener.character, OpenAt(DoorX, DoorY));

    EXPECT_FALSE(map->GetWarp(Converters::CoordsFrom(DoorX, DoorY)).open);
    EXPECT_TRUE(opener.sent.empty());
}
