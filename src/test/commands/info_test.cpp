#include "../testhelper/playing.hpp"

// include the CPP file with the info commands in it for testing
#include "../../commands/info.cpp"

#include <eolib/protocol/net/server/packets.hpp>

#include <string>
#include <vector>

namespace protocol = eolib::protocol;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short InfoTestServerPort = 38137;
constexpr int InfoTestMapId = 42;

class InfoCommandTests : public PlayingTest
{
protected:
    InfoCommandTests()
        : PlayingTest(InfoTestServerPort, InfoTestMapId)
    {
    }

    static void GivenItem(std::list<protocol::net::Item>& items, int id, int amount)
    {
        protocol::net::Item item;
        item.id = id;
        item.amount = amount;
        items.push_back(item);
    }
};

}

TEST_F(InfoCommandTests, Info_OnlineCharacter_RepliesWithStats)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& admin = GivenOnlineCharacter(2, 2);
    PlayingCharacter& victim = GivenOnlineCharacter(3, 4);
    admin.character->admin = protocol::AdminLevel::GameMaster;
    victim.character->goldbank = 500;
    victim.character->exp = 1000;
    victim.character->level = 10;
    victim.character->hp = 90;
    victim.character->maxhp = 100;

    Commands::Info({victim.character->SourceName()}, admin.character);

    const auto reply = LastSent<server::AdminInteractTellServerPacket>(admin);
    ASSERT_TRUE(reply);
    EXPECT_EQ(util::ucfirst(victim.character->SourceName()) + " ", reply->name);
    EXPECT_EQ(500, reply->gold_bank);
    EXPECT_EQ(1000, reply->exp);
    EXPECT_EQ(10, reply->level);
    EXPECT_EQ(InfoTestMapId, reply->map_id);
    EXPECT_EQ(3, reply->map_coords.x);
    EXPECT_EQ(4, reply->map_coords.y);
    EXPECT_EQ(90, reply->stats.hp);
    EXPECT_EQ(100, reply->stats.max_hp);
}

TEST_F(InfoCommandTests, Info_ProtectedAdmin_Denied)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& admin = GivenOnlineCharacter(2, 2);
    PlayingCharacter& victim = GivenOnlineCharacter(3, 4);
    admin.character->admin = protocol::AdminLevel::Guardian;
    victim.character->admin = protocol::AdminLevel::GameMaster;

    Commands::Info({victim.character->SourceName()}, admin.character);

    EXPECT_FALSE(LastSent<server::AdminInteractTellServerPacket>(admin));
    EXPECT_TRUE(LastSent<server::TalkServerServerPacket>(admin));
}

TEST_F(InfoCommandTests, Inventory_OnlineCharacter_RepliesWithItems)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& admin = GivenOnlineCharacter(2, 2);
    PlayingCharacter& victim = GivenOnlineCharacter(3, 4);
    admin.character->admin = protocol::AdminLevel::GameMaster;
    victim.character->goldbank = 500;
    GivenItem(victim.character->inventory, 2, 70000);
    GivenItem(victim.character->bank, 3, 9);

    Commands::Inventory({victim.character->SourceName()}, admin.character);

    const auto reply = LastSent<server::AdminInteractListServerPacket>(admin);
    ASSERT_TRUE(reply);
    EXPECT_EQ(500, reply->gold_bank);
    ASSERT_EQ(1u, reply->inventory.size());
    EXPECT_EQ(2, reply->inventory[0].id);
    EXPECT_EQ(70000, reply->inventory[0].amount);
    ASSERT_EQ(1u, reply->bank.size());
    EXPECT_EQ(3, reply->bank[0].id);
    EXPECT_EQ(9, reply->bank[0].amount);
}

TEST_F(InfoCommandTests, Book_UnknownCharacter_RepliesNotFound)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& admin = GivenOnlineCharacter(2, 2);

    Commands::Book({"nobody"}, admin.character);

    EXPECT_FALSE(LastSent<server::BookReplyServerPacket>(admin));
    EXPECT_TRUE(LastSent<server::TalkServerServerPacket>(admin));
}

TEST_F(InfoCommandTests, Book_OnlineCharacter_RepliesWithDetails)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& admin = GivenOnlineCharacter(2, 2);
    PlayingCharacter& victim = GivenOnlineCharacter(3, 4);

    Commands::Book({victim.character->SourceName()}, admin.character);

    const auto reply = LastSent<server::BookReplyServerPacket>(admin);
    ASSERT_TRUE(reply);
    EXPECT_EQ(victim.character->PlayerID(), reply->details.player_id);
}

TEST_F(InfoCommandTests, Inventory_HigherAdminAtCmdprotect_DeniesAccess)
{
    GivenMap(EmptyEmf(10, 10));
    server->world->admin_config["cmdprotect"] = static_cast<int>(protocol::AdminLevel::Guardian);
    PlayingCharacter& admin = GivenOnlineCharacter(2, 2);
    PlayingCharacter& victim = GivenOnlineCharacter(3, 4);
    admin.character->admin = protocol::AdminLevel::Guardian;
    victim.character->admin = protocol::AdminLevel::GameMaster;

    Commands::Inventory({victim.character->SourceName()}, admin.character);

    EXPECT_FALSE(LastSent<server::AdminInteractListServerPacket>(admin));
    EXPECT_TRUE(LastSent<server::TalkServerServerPacket>(admin));
}

TEST_F(InfoCommandTests, Inventory_EqualAdminAtCmdprotect_RepliesWithItems)
{
    GivenMap(EmptyEmf(10, 10));
    server->world->admin_config["cmdprotect"] = static_cast<int>(protocol::AdminLevel::Guardian);
    PlayingCharacter& admin = GivenOnlineCharacter(2, 2);
    PlayingCharacter& victim = GivenOnlineCharacter(3, 4);
    admin.character->admin = protocol::AdminLevel::GameMaster;
    victim.character->admin = protocol::AdminLevel::GameMaster;

    Commands::Inventory({victim.character->SourceName()}, admin.character);

    EXPECT_TRUE(LastSent<server::AdminInteractListServerPacket>(admin));
}

TEST_F(InfoCommandTests, Inventory_HigherAdminBelowCmdprotect_RepliesWithItems)
{
    GivenMap(EmptyEmf(10, 10));
    server->world->admin_config["cmdprotect"] = static_cast<int>(protocol::AdminLevel::GameMaster);
    PlayingCharacter& admin = GivenOnlineCharacter(2, 2);
    PlayingCharacter& victim = GivenOnlineCharacter(3, 4);
    admin.character->admin = protocol::AdminLevel::LightGuide;
    victim.character->admin = protocol::AdminLevel::Guardian;

    Commands::Inventory({victim.character->SourceName()}, admin.character);

    EXPECT_TRUE(LastSent<server::AdminInteractListServerPacket>(admin));
}
