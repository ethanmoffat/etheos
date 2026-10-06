#include "../testhelper/playing.hpp"

// include the CPP file with the Chest functions in it for testing
#include "../../handlers/Chest.cpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdio>

namespace client = eolib::protocol::net::client;
namespace pub = eolib::protocol::pub;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short ChestTestServerPort = 38125;
constexpr int ChestTestMapId = 30;
constexpr int GoldId = 1;
constexpr int KeyId = 2;
constexpr int SpareKeyId = 3;
constexpr int ChestKey = 5;
constexpr int MissingKey = 6;
constexpr int ChestX = 3;
constexpr int ChestY = 2;

class ChestTests : public PlayingTest
{
protected:
    ChestTests()
        : PlayingTest(ChestTestServerPort, ChestTestMapId)
    {
    }

    void ConfigureServer(Config& config) override
    {
        pub::EifRecord gold;
        gold.name = "Gold";

        pub::EifRecord key;
        key.name = "Key";
        key.type = pub::ItemType::Key;
        key.spec1 = ChestKey;

        pub::EifRecord spare_key = key;
        spare_key.name = "Spare Key";

        pub::Eif eif;
        eif.rid = {1, 2};
        eif.total_items_count = 3;
        eif.items = {gold, key, spare_key};
        WriteFile("chest_test.eif", eif);

        config["EIF"] = "chest_test.eif";
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove("chest_test.eif");
    }

    Map_Chest& GivenChestMap()
    {
        eolib::protocol::map::Emf file = EmptyEmf(10, 10);
        AddTileSpec(file, ChestX, ChestY, eolib::protocol::map::MapTileSpec::Chest);
        GivenMap(file);
        return *map->chests.front();
    }

    static eolib::protocol::Coords ChestCoords()
    {
        eolib::protocol::Coords coords;
        coords.x = ChestX;
        coords.y = ChestY;
        return coords;
    }

    static client::ChestOpenClientPacket Open()
    {
        client::ChestOpenClientPacket packet;
        packet.coords = ChestCoords();
        return packet;
    }

    static client::ChestAddClientPacket Add(int item_id, int amount)
    {
        client::ChestAddClientPacket packet;
        packet.coords = ChestCoords();
        packet.add_item.id = item_id;
        packet.add_item.amount = amount;
        return packet;
    }

    static client::ChestTakeClientPacket Take(int item_id)
    {
        client::ChestTakeClientPacket packet;
        packet.coords = ChestCoords();
        packet.take_item_id = item_id;
        return packet;
    }
};

}

TEST_F(ChestTests, Open_InReach_RepliesWithItems)
{
    Map_Chest& chest = GivenChestMap();
    chest.AddItem(GoldId, 50);
    PlayingCharacter& user = GivenCharacter(2, 2);

    Handlers::Chest_Open(user.character, Open());

    const auto reply = LastSent<server::ChestOpenServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(ChestCoords(), reply->coords);
    ASSERT_EQ(1u, reply->items.size());
    EXPECT_EQ(GoldId, reply->items[0].id);
    EXPECT_EQ(50, reply->items[0].amount);
}

TEST_F(ChestTests, Open_OutOfReach_Ignored)
{
    GivenChestMap();
    PlayingCharacter& user = GivenCharacter(5, 5);

    Handlers::Chest_Open(user.character, Open());

    EXPECT_TRUE(user.sent.empty());
}

TEST_F(ChestTests, Open_NoChestInReach_RepliesBrokenClose)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& user = GivenCharacter(2, 2);

    Handlers::Chest_Open(user.character, Open());

    const auto reply = LastSent<server::ChestCloseServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_FALSE(reply->key);
}

TEST_F(ChestTests, Open_LockedWithoutKey_RepliesCloseWithKey)
{
    Map_Chest& chest = GivenChestMap();
    chest.key = ChestKey;
    PlayingCharacter& user = GivenCharacter(2, 2);

    Handlers::Chest_Open(user.character, Open());

    const auto reply = LastSent<server::ChestCloseServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(ChestKey, reply->key);
    EXPECT_FALSE(LastSent<server::ChestOpenServerPacket>(user));
}

TEST_F(ChestTests, Open_LockedWithUnknownKey_RepliesCloseWithoutKey)
{
    Map_Chest& chest = GivenChestMap();
    chest.key = MissingKey;
    PlayingCharacter& user = GivenCharacter(2, 2);

    Handlers::Chest_Open(user.character, Open());

    const auto reply = LastSent<server::ChestCloseServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_FALSE(reply->key);
}

TEST_F(ChestTests, Open_LockedWithKey_RepliesOpen)
{
    Map_Chest& chest = GivenChestMap();
    chest.key = ChestKey;
    PlayingCharacter& user = GivenCharacter(2, 2);
    user.character->AddItem(KeyId, 1);

    Handlers::Chest_Open(user.character, Open());

    EXPECT_TRUE(LastSent<server::ChestOpenServerPacket>(user));
}

TEST_F(ChestTests, Add_HeldItem_RepliesAndUpdatesNearby)
{
    Map_Chest& chest = GivenChestMap();
    PlayingCharacter& user = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(4, 2);
    user.character->AddItem(GoldId, 100);

    Handlers::Chest_Add(user.character, Add(GoldId, 30));

    const auto reply = LastSent<server::ChestReplyServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(GoldId, reply->added_item_id);
    EXPECT_EQ(70, reply->remaining_amount);
    ASSERT_EQ(1u, reply->items.size());
    EXPECT_EQ(30, reply->items[0].amount);
    EXPECT_EQ(30, chest.HasItem(GoldId));
    EXPECT_FALSE(LastSent<server::ChestAgreeServerPacket>(user));

    const auto agree = LastSent<server::ChestAgreeServerPacket>(observer);
    ASSERT_TRUE(agree);
    ASSERT_EQ(1u, agree->items.size());
    EXPECT_EQ(30, agree->items[0].amount);
}

TEST_F(ChestTests, Add_NotHeld_Ignored)
{
    Map_Chest& chest = GivenChestMap();
    PlayingCharacter& user = GivenCharacter(2, 2);
    user.character->AddItem(GoldId, 10);

    Handlers::Chest_Add(user.character, Add(GoldId, 30));

    EXPECT_FALSE(LastSent<server::ChestReplyServerPacket>(user));
    EXPECT_EQ(0, chest.HasItem(GoldId));
}

TEST_F(ChestTests, Take_ChestItem_RepliesGetAndUpdatesNearby)
{
    Map_Chest& chest = GivenChestMap();
    chest.AddItem(GoldId, 40);
    PlayingCharacter& user = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(4, 2);

    Handlers::Chest_Take(user.character, Take(GoldId));

    const auto reply = LastSent<server::ChestGetServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(GoldId, reply->taken_item.id);
    EXPECT_EQ(40, reply->taken_item.amount);
    EXPECT_TRUE(reply->items.empty());
    EXPECT_EQ(40, user.character->HasItem(GoldId));

    const auto agree = LastSent<server::ChestAgreeServerPacket>(observer);
    ASSERT_TRUE(agree);
    EXPECT_TRUE(agree->items.empty());
}

TEST_F(ChestTests, Open_LockedWithOtherItemOfSameKey_RepliesOpen)
{
    Map_Chest& chest = GivenChestMap();
    chest.key = ChestKey;
    PlayingCharacter& user = GivenCharacter(2, 2);
    user.character->AddItem(SpareKeyId, 1);

    Handlers::Chest_Open(user.character, Open());

    EXPECT_TRUE(LastSent<server::ChestOpenServerPacket>(user));
    EXPECT_FALSE(LastSent<server::ChestCloseServerPacket>(user));
}
