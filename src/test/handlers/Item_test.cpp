#include "../testhelper/playing.hpp"

// include the CPP file with the Item functions in it for testing
#include "../../handlers/Item.cpp"
#include "../../converters.hpp"

#include "../../timer.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdio>
#include <memory>

namespace client = eolib::protocol::net::client;
namespace pub = eolib::protocol::pub;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short ItemTestServerPort = 38123;
constexpr int ItemTestMapId = 28;
constexpr int GoldId = 1;
constexpr int PotionId = 2;
constexpr int BeerId = 3;
constexpr int ExpScrollId = 4;
constexpr int RockId = 5;
constexpr int PotionHp = 10;
constexpr int BeerPotency = 4;

class ItemTests : public PlayingTest
{
protected:
    ItemTests()
        : PlayingTest(ItemTestServerPort, ItemTestMapId)
    {
    }

    void ConfigureServer(Config& config) override
    {
        pub::EifRecord gold;
        gold.name = "Gold";

        pub::EifRecord potion;
        potion.name = "Potion";
        potion.type = pub::ItemType::Heal;
        potion.hp = PotionHp;

        pub::EifRecord beer;
        beer.name = "Beer";
        beer.type = pub::ItemType::Alcohol;
        beer.spec1 = BeerPotency;

        pub::EifRecord exp_scroll;
        exp_scroll.name = "Exp Scroll";
        exp_scroll.type = pub::ItemType::ExpReward;
        exp_scroll.spec1 = 1000;

        pub::EifRecord rock;
        rock.name = "Rock";

        pub::Eif eif;
        eif.rid = {1, 2};
        eif.total_items_count = 5;
        eif.items = {gold, potion, beer, exp_scroll, rock};
        WriteFile("item_test.eif", eif);

        config["EIF"] = "item_test.eif";
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove("item_test.eif");
    }

    static client::ItemUseClientPacket Use(int item_id)
    {
        client::ItemUseClientPacket packet;
        packet.item_id = item_id;
        return packet;
    }

    static client::ItemDropClientPacket Drop(int item_id, int amount, int x, int y)
    {
        client::ItemDropClientPacket packet;
        packet.item.id = item_id;
        packet.item.amount = amount;
        packet.coords.x = x;
        packet.coords.y = y;
        return packet;
    }
};

}

TEST_F(ItemTests, Use_NotHeld_RepliesAgree)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& user = GivenCharacter(2, 2);

    Handlers::Item_Use(user.character, Use(PotionId));

    const auto reply = LastSent<server::ItemAgreeServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(PotionId, reply->item_id);
}

TEST_F(ItemTests, Use_Heal_RepliesWithHealDataAndTellsNearby)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& user = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 2);
    user.character->AddItem(PotionId, 2);
    user.character->hp = 0;
    ASSERT_GE(user.character->maxhp, PotionHp);

    Handlers::Item_Use(user.character, Use(PotionId));

    const auto reply = LastSent<server::ItemReplyServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(pub::ItemType::Heal, reply->item_type);
    EXPECT_EQ(PotionId, reply->used_item.id);
    EXPECT_EQ(1, reply->used_item.amount);
    ASSERT_TRUE(reply->AsHeal());
    EXPECT_EQ(PotionHp, reply->AsHeal()->hp_gain);
    EXPECT_EQ(PotionHp, reply->AsHeal()->hp);
    EXPECT_EQ(PotionHp, user.character->hp);
    EXPECT_TRUE(LastSent<server::RecoverAgreeServerPacket>(observer));
}

TEST_F(ItemTests, Use_Alcohol_MakesCharacterDrunk)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& user = GivenCharacter(2, 2);
    user.character->AddItem(BeerId, 1);

    const double before = Timer::GetTime();
    Handlers::Item_Use(user.character, Use(BeerId));

    const auto reply = LastSent<server::ItemReplyServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(pub::ItemType::Alcohol, reply->item_type);
    EXPECT_EQ(0, reply->used_item.amount);
    EXPECT_TRUE(user.character->IsDrunk());
    EXPECT_GE(user.character->drunk_until, before + Character::DrunkSeconds(BeerPotency));
}

TEST_F(ItemTests, Use_ExpRewardLevelUp_SendsAcceptToNearbyOnly)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& user = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 2);
    user.character->AddItem(ExpScrollId, 1);
    user.character->level = 0;
    user.character->exp = 0;

    Handlers::Item_Use(user.character, Use(ExpScrollId));

    const auto reply = LastSent<server::ItemReplyServerPacket>(user);
    ASSERT_TRUE(reply);
    ASSERT_TRUE(reply->AsExpReward());
    EXPECT_EQ(1000, reply->AsExpReward()->experience);
    EXPECT_GT(user.character->level, 0);
    EXPECT_EQ(user.character->level, reply->AsExpReward()->level_up);

    EXPECT_FALSE(LastSent<server::ItemAcceptServerPacket>(user));
    const auto accept = LastSent<server::ItemAcceptServerPacket>(observer);
    ASSERT_TRUE(accept);
    EXPECT_EQ(user.character->PlayerID(), accept->player_id);
}

TEST_F(ItemTests, Drop_AtFeet_DropsAtCharacterAndTellsOthers)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& dropper = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 2);
    dropper.character->AddItem(RockId, 5);

    Handlers::Item_Drop(dropper.character, Drop(RockId, 3, Handlers::DropAtFeetCoord, Handlers::DropAtFeetCoord));

    const auto reply = LastSent<server::ItemDropServerPacket>(dropper);
    ASSERT_TRUE(reply);
    EXPECT_EQ(RockId, reply->dropped_item.id);
    EXPECT_EQ(3, reply->dropped_item.amount);
    EXPECT_EQ(2, reply->remaining_amount);
    EXPECT_EQ(dropper.character->loc, reply->coords);
    EXPECT_FALSE(LastSent<server::ItemAddServerPacket>(dropper));

    const auto add = LastSent<server::ItemAddServerPacket>(observer);
    ASSERT_TRUE(add);
    EXPECT_EQ(reply->item_index, add->item_index);
    EXPECT_EQ(RockId, add->item_id);
    EXPECT_EQ(3, add->item_amount);
    EXPECT_EQ(dropper.character->loc, add->coords);
}

TEST_F(ItemTests, Drop_EncodedCoords_DropsAtDecodedCoords)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& dropper = GivenCharacter(2, 2);
    dropper.character->AddItem(RockId, 1);

    // ByteCoords values are encoded EO numbers: 3 is sent as 4
    Handlers::Item_Drop(dropper.character, Drop(RockId, 1, 4, 3));

    const auto reply = LastSent<server::ItemDropServerPacket>(dropper);
    ASSERT_TRUE(reply);
    EXPECT_EQ(3, reply->coords.x);
    EXPECT_EQ(2, reply->coords.y);
}

TEST_F(ItemTests, Junk_HeldAmount_RepliesWithRemaining)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& user = GivenCharacter(2, 2);
    user.character->AddItem(RockId, 5);

    client::ItemJunkClientPacket packet;
    packet.item.id = RockId;
    packet.item.amount = 2;
    Handlers::Item_Junk(user.character, packet);

    const auto reply = LastSent<server::ItemJunkServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(RockId, reply->junked_item.id);
    EXPECT_EQ(2, reply->junked_item.amount);
    EXPECT_EQ(3, reply->remaining_amount);
}

TEST_F(ItemTests, Junk_AmountNotRepresentableAsThree_Ignored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& user = GivenCharacter(2, 2);
    const int amount = static_cast<int>(eolib::data::EoNumericLimits::ThreeMax);
    user.character->AddItem(GoldId, amount);

    client::ItemJunkClientPacket packet;
    packet.item.id = GoldId;
    packet.item.amount = amount;
    Handlers::Item_Junk(user.character, packet);

    EXPECT_FALSE(LastSent<server::ItemJunkServerPacket>(user));
    EXPECT_EQ(amount, user.character->HasItem(GoldId));
}

TEST_F(ItemTests, Get_PartOfPile_RepliesAndUpdatesPileForOthers)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& getter = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 2);
    std::shared_ptr<Map_Item> pile = map->AddItem(GoldId, 50, Converters::CoordsFrom(2, 2));
    ASSERT_TRUE(pile);
    pile->owner = getter.character->PlayerID();
    server->world->config["MaxItem"] = 20;

    client::ItemGetClientPacket packet;
    packet.item_index = pile->uid;
    Handlers::Item_Get(getter.character, packet);

    const auto reply = LastSent<server::ItemGetServerPacket>(getter);
    ASSERT_TRUE(reply);
    EXPECT_EQ(pile->uid, reply->taken_item_index);
    EXPECT_EQ(GoldId, reply->taken_item.id);
    EXPECT_EQ(20, reply->taken_item.amount);

    const auto remove = LastSent<server::ItemRemoveServerPacket>(observer);
    ASSERT_TRUE(remove);
    EXPECT_EQ(pile->uid, remove->item_index);
    const auto add = LastSent<server::ItemAddServerPacket>(observer);
    ASSERT_TRUE(add);
    EXPECT_EQ(30, add->item_amount);
}
