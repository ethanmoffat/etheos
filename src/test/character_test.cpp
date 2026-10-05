#include "testhelper/playing.hpp"

#include <memory>

namespace
{

constexpr unsigned short CharacterSpawnTestServerPort = 38109;
constexpr int CharacterSpawnTestMapId = 14;

class CharacterSpawnTests : public PlayingTest
{
protected:
    CharacterSpawnTests()
        : PlayingTest(CharacterSpawnTestServerPort, CharacterSpawnTestMapId)
    {
    }
};

}

TEST_F(CharacterSpawnTests, SpawnLoc_AssignedHome_ReturnsHomeLocation)
{
    auto home = std::make_shared<Home>();
    home->id = "testhome";
    home->map = CharacterSpawnTestMapId;
    home->loc.x = 4;
    home->loc.y = 7;
    server->world->homes.push_back(home);

    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& playing_character = GivenCharacter(1, 1);
    playing_character.character->home = "testhome";

    const eolib::protocol::Coords spawn_loc = playing_character.character->SpawnLoc();

    EXPECT_EQ(CharacterSpawnTestMapId, playing_character.character->SpawnMap());
    EXPECT_EQ(4, spawn_loc.x);
    EXPECT_EQ(7, spawn_loc.y);
}
