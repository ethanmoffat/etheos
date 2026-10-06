#include "../testhelper/playing.hpp"

// include the CPP file with the Refresh functions in it for testing
#include "../../handlers/Refresh.cpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <memory>

namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short RefreshTestServerPort = 38111;
constexpr int RefreshTestMapId = 16;

class RefreshTests : public PlayingTest
{
protected:
    RefreshTests()
        : PlayingTest(RefreshTestServerPort, RefreshTestMapId)
    {
    }
};

}

TEST_F(RefreshTests, Request_SendsCharactersAndItemsInRange)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& refresher = GivenCharacter(2, 2);
    PlayingCharacter& near = GivenCharacter(4, 4);
    GivenCharacter(30, 30);
    const std::shared_ptr<Map_Item> near_item = map->AddItem(1, 5, 3, 3);
    map->AddItem(2, 5, 35, 35);

    Handlers::Refresh_Request(refresher.character, client::RefreshRequestClientPacket());

    const auto reply = LastSent<server::RefreshReplyServerPacket>(refresher);
    ASSERT_TRUE(reply);
    ASSERT_EQ(2u, reply->nearby.characters.size());
    EXPECT_EQ(refresher.character->PlayerID(), reply->nearby.characters[0].player_id);
    EXPECT_EQ(near.character->PlayerID(), reply->nearby.characters[1].player_id);
    EXPECT_TRUE(reply->nearby.npcs.empty());
    ASSERT_EQ(1u, reply->nearby.items.size());
    EXPECT_EQ(near_item->uid, reply->nearby.items[0].uid);
    EXPECT_EQ(1, reply->nearby.items[0].id);
    EXPECT_EQ(5, reply->nearby.items[0].amount);
}
