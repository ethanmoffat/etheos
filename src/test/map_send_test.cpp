#include "testhelper/playing.hpp"

#include <eolib/protocol/net/server/packets.hpp>

namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short MapSendTestServerPort = 38119;
constexpr int MapSendTestMapId = 24;

class MapSendTests : public PlayingTest
{
protected:
    MapSendTests()
        : PlayingTest(MapSendTestServerPort, MapSendTestMapId)
    {
    }

    static server::FacePlayerServerPacket FacePacket(int player_id)
    {
        server::FacePlayerServerPacket packet;
        packet.player_id = player_id;
        packet.direction = Direction::Up;
        return packet;
    }
};

TEST_F(MapSendTests, SendToNearby_CharacterWithoutEcho_SendsToOthersInRange)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& from = GivenCharacter(3, 3);
    PlayingCharacter& near = GivenCharacter(4, 4);
    PlayingCharacter& far = GivenCharacter(35, 35);

    map->SendToNearby(from.character, FacePacket(from.character->PlayerID()), false);

    EXPECT_FALSE(LastSent<server::FacePlayerServerPacket>(from));
    EXPECT_TRUE(LastSent<server::FacePlayerServerPacket>(near));
    EXPECT_FALSE(LastSent<server::FacePlayerServerPacket>(far));
}

TEST_F(MapSendTests, SendToNearby_CharacterWithEcho_IncludesSender)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& from = GivenCharacter(3, 3);
    PlayingCharacter& far = GivenCharacter(35, 35);

    map->SendToNearby(from.character, FacePacket(from.character->PlayerID()), true);

    EXPECT_TRUE(LastSent<server::FacePlayerServerPacket>(from));
    EXPECT_FALSE(LastSent<server::FacePlayerServerPacket>(far));
}

TEST_F(MapSendTests, SendToNearby_Coords_SendsToCharactersInRange)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& near = GivenCharacter(3, 3);
    PlayingCharacter& far = GivenCharacter(35, 35);

    eolib::protocol::Coords coords;
    coords.x = 5;
    coords.y = 5;
    map->SendToNearby(coords, FacePacket(1));

    EXPECT_TRUE(LastSent<server::FacePlayerServerPacket>(near));
    EXPECT_FALSE(LastSent<server::FacePlayerServerPacket>(far));
}

TEST_F(MapSendTests, SendToMap_SendsToEveryCharacterOnMap)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& near = GivenCharacter(3, 3);
    PlayingCharacter& far = GivenCharacter(35, 35);

    map->SendToMap(FacePacket(1));

    EXPECT_TRUE(LastSent<server::FacePlayerServerPacket>(near));
    EXPECT_TRUE(LastSent<server::FacePlayerServerPacket>(far));
}

TEST_F(MapSendTests, Emote_WithEcho_OnlyReachesCharactersInRange)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& from = GivenCharacter(3, 3);
    PlayingCharacter& far = GivenCharacter(35, 35);

    map->Emote(from.character, eolib::protocol::Emote::Happy, true);

    EXPECT_TRUE(LastSent<server::EmotePlayerServerPacket>(from));
    EXPECT_FALSE(LastSent<server::EmotePlayerServerPacket>(far));
}

}
