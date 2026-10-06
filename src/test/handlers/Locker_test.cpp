#include "../testhelper/playing.hpp"

// include the CPP file with the Locker functions in it for testing
#include "../../handlers/Locker.cpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdio>

namespace client = eolib::protocol::net::client;
namespace pub = eolib::protocol::pub;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short LockerTestServerPort = 38126;
constexpr int LockerTestMapId = 31;
constexpr int GoldId = 1;
constexpr int RockId = 2;
constexpr int LockerX = 3;
constexpr int LockerY = 2;

class LockerTests : public PlayingTest
{
protected:
    LockerTests()
        : PlayingTest(LockerTestServerPort, LockerTestMapId)
    {
    }

    void ConfigureServer(Config& config) override
    {
        pub::EifRecord gold;
        gold.name = "Gold";

        pub::EifRecord rock;
        rock.name = "Rock";

        pub::Eif eif;
        eif.rid = {1, 2};
        eif.total_items_count = 2;
        eif.items = {gold, rock};
        WriteFile("locker_test.eif", eif);

        config["EIF"] = "locker_test.eif";
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove("locker_test.eif");
    }

    void GivenLockerMap()
    {
        eolib::protocol::map::Emf file = EmptyEmf(10, 10);
        AddTileSpec(file, LockerX, LockerY, eolib::protocol::map::MapTileSpec::BankVault);
        GivenMap(file);
    }

    static eolib::protocol::Coords LockerCoords()
    {
        eolib::protocol::Coords coords;
        coords.x = LockerX;
        coords.y = LockerY;
        return coords;
    }

    static client::LockerAddClientPacket Add(int item_id, int amount)
    {
        client::LockerAddClientPacket packet;
        packet.locker_coords = LockerCoords();
        packet.deposit_item.id = item_id;
        packet.deposit_item.amount = amount;
        return packet;
    }

    static client::LockerTakeClientPacket Take(int item_id)
    {
        client::LockerTakeClientPacket packet;
        packet.locker_coords = LockerCoords();
        packet.take_item_id = item_id;
        return packet;
    }

    static client::LockerOpenClientPacket Open()
    {
        client::LockerOpenClientPacket packet;
        packet.locker_coords = LockerCoords();
        return packet;
    }

    static void GivenBankItem(Character* character, int id, int amount)
    {
        Character_Item item;
        item.id = id;
        item.amount = amount;
        character->bank.push_back(item);
    }
};

}

TEST_F(LockerTests, Open_InReach_RepliesWithLockerItems)
{
    GivenLockerMap();
    PlayingCharacter& user = GivenCharacter(2, 2);
    GivenBankItem(user.character, RockId, 5);

    Handlers::Locker_Open(user.character, Open());

    const auto reply = LastSent<server::LockerOpenServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(LockerCoords(), reply->locker_coords);
    ASSERT_EQ(1u, reply->locker_items.size());
    EXPECT_EQ(RockId, reply->locker_items[0].id);
    EXPECT_EQ(5, reply->locker_items[0].amount);
}

TEST_F(LockerTests, Open_OutOfReach_Ignored)
{
    GivenLockerMap();
    PlayingCharacter& user = GivenCharacter(5, 5);

    Handlers::Locker_Open(user.character, Open());

    EXPECT_TRUE(user.sent.empty());
}

TEST_F(LockerTests, Add_HeldItem_RepliesWithRemainingAmount)
{
    GivenLockerMap();
    PlayingCharacter& user = GivenCharacter(2, 2);
    user.character->AddItem(RockId, 10);

    Handlers::Locker_Add(user.character, Add(RockId, 4));

    const auto reply = LastSent<server::LockerReplyServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(RockId, reply->deposited_item.id);
    EXPECT_EQ(6, reply->deposited_item.amount);
    ASSERT_EQ(1u, reply->locker_items.size());
    EXPECT_EQ(4, reply->locker_items[0].amount);
}

TEST_F(LockerTests, Add_Gold_Ignored)
{
    GivenLockerMap();
    PlayingCharacter& user = GivenCharacter(2, 2);
    user.character->AddItem(GoldId, 10);

    Handlers::Locker_Add(user.character, Add(GoldId, 4));

    EXPECT_TRUE(user.sent.empty());
    EXPECT_TRUE(user.character->bank.empty());
}

TEST_F(LockerTests, Add_LockerFull_RepliesSpec)
{
    GivenLockerMap();
    PlayingCharacter& user = GivenCharacter(2, 2);
    user.character->AddItem(RockId, 1);
    const int locker_max = static_cast<int>(server->world->config["BaseBankSize"]);
    for (int i = 0; i < locker_max; ++i)
        GivenBankItem(user.character, 100 + i, 1);

    Handlers::Locker_Add(user.character, Add(RockId, 1));

    const auto reply = LastSent<server::LockerSpecServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(locker_max, reply->locker_max_items);
    EXPECT_EQ(1, user.character->HasItem(RockId));
}

TEST_F(LockerTests, Take_LockerItem_RepliesGet)
{
    GivenLockerMap();
    PlayingCharacter& user = GivenCharacter(2, 2);
    GivenBankItem(user.character, RockId, 3);

    Handlers::Locker_Take(user.character, Take(RockId));

    const auto reply = LastSent<server::LockerGetServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(RockId, reply->taken_item.id);
    EXPECT_EQ(3, reply->taken_item.amount);
    EXPECT_TRUE(reply->locker_items.empty());
    EXPECT_EQ(3, user.character->HasItem(RockId));
}

TEST_F(LockerTests, Buy_AtBanker_RepliesWithGoldAndUpgrades)
{
    GivenLockerMap();
    PlayingCharacter& user = GivenCharacter(2, 2);
    const int cost = static_cast<int>(server->world->config["BankUpgradeBase"]);
    user.character->AddItem(GoldId, cost + 25);
    user.character->npc_type = pub::NpcType::Bank;
    user.character->bankmax = 0;

    Handlers::Locker_Buy(user.character, client::LockerBuyClientPacket());

    const auto reply = LastSent<server::LockerBuyServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(25, reply->gold_amount);
    EXPECT_EQ(1, reply->locker_upgrades);
}

TEST_F(LockerTests, Add_StackAtMaxBank_Ignored)
{
    GivenLockerMap();
    PlayingCharacter& user = GivenCharacter(2, 2);
    user.character->AddItem(RockId, 10);
    server->world->config["MaxBank"] = 5;
    GivenBankItem(user.character, RockId, 5);

    Handlers::Locker_Add(user.character, Add(RockId, 4));

    EXPECT_FALSE(LastSent<server::LockerReplyServerPacket>(user));
    EXPECT_EQ(5, user.character->bank.front().amount);
    EXPECT_EQ(10, user.character->HasItem(RockId));
}

TEST_F(LockerTests, Add_StackAboveLoweredMaxBank_KeepsItems)
{
    GivenLockerMap();
    PlayingCharacter& user = GivenCharacter(2, 2);
    user.character->AddItem(RockId, 10);
    server->world->config["MaxBank"] = 5;
    GivenBankItem(user.character, RockId, 8);

    Handlers::Locker_Add(user.character, Add(RockId, 4));

    EXPECT_FALSE(LastSent<server::LockerReplyServerPacket>(user));
    EXPECT_EQ(8, user.character->bank.front().amount);
    EXPECT_EQ(10, user.character->HasItem(RockId));
}

TEST_F(LockerTests, Add_ExistingStack_ClampedToMaxBank)
{
    GivenLockerMap();
    PlayingCharacter& user = GivenCharacter(2, 2);
    user.character->AddItem(RockId, 10);
    server->world->config["MaxBank"] = 5;
    GivenBankItem(user.character, RockId, 3);

    Handlers::Locker_Add(user.character, Add(RockId, 4));

    const auto reply = LastSent<server::LockerReplyServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(8, reply->deposited_item.amount);
    EXPECT_EQ(5, user.character->bank.front().amount);
}
