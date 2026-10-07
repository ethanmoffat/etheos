#include "../testhelper/playing.hpp"

// include the CPP file with the Global functions in it for testing
#include "../../handlers/Global.cpp"

#include <eolib/protocol/net/client/packets.hpp>

namespace client = eolib::protocol::net::client;

namespace
{

constexpr unsigned short GlobalTestServerPort = 38136;
constexpr int GlobalTestMapId = 41;

class GlobalTests : public PlayingTest
{
protected:
    GlobalTests()
        : PlayingTest(GlobalTestServerPort, GlobalTestMapId)
    {
    }
};

}

TEST_F(GlobalTests, Player_DisablesWhispers)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& user = GivenCharacter(2, 2);

    Handlers::Global_Player(user.character, client::GlobalPlayerClientPacket());

    EXPECT_FALSE(user.character->whispers);
}

TEST_F(GlobalTests, Remove_EnablesWhispers)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& user = GivenCharacter(2, 2);
    user.character->whispers = false;

    Handlers::Global_Remove(user.character, client::GlobalRemoveClientPacket());

    EXPECT_TRUE(user.character->whispers);
}
