#include "../testhelper/playing.hpp"

// include the CPP file with the Shop functions in it for testing
#include "../../handlers/Shop.cpp"

#include "../../npc.hpp"
#include "../../npc_data.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdio>

namespace client = eolib::protocol::net::client;
namespace pub = eolib::protocol::pub;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short ShopTestServerPort = 38122;
constexpr int ShopTestMapId = 27;
constexpr int ShopNpcId = 1;
constexpr int ShopSessionId = 1234;
constexpr int GoldId = 1;
constexpr int OreId = 2;
constexpr int BarId = 3;

class ShopTests : public PlayingTest
{
protected:
    NPC* shopkeeper = nullptr;

    ShopTests()
        : PlayingTest(ShopTestServerPort, ShopTestMapId)
    {
    }

    void ConfigureServer(Config& config) override
    {
        pub::EnfRecord shop_record;
        shop_record.name = "Shopkeeper";
        shop_record.type = pub::NpcType::Shop;
        shop_record.hp = 10;

        pub::Enf enf;
        enf.rid = {1, 2};
        enf.total_npcs_count = 1;
        enf.npcs = {shop_record};
        WriteFile("shop_test.enf", enf);

        pub::EifRecord gold;
        gold.name = "Gold";
        pub::EifRecord ore;
        ore.name = "Ore";
        pub::EifRecord bar;
        bar.name = "Bar";

        pub::Eif eif;
        eif.rid = {1, 2};
        eif.total_items_count = 3;
        eif.items = {gold, ore, bar};
        WriteFile("shop_test.eif", eif);

        config["ENF"] = "shop_test.enf";
        config["EIF"] = "shop_test.eif";
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove("shop_test.enf");
        std::remove("shop_test.eif");
    }

    // A character at a shop that trades ore (buy 10, sell 5) and crafts a bar from 2 ore
    Character* GivenCharacterAtShop()
    {
        GivenMap(EmptyEmf(10, 10));

        NPC_Data& data = *server->world->npc_data[ShopNpcId];
        data.shop_name = "Smith";

        server::ShopTradeItem trade;
        trade.item_id = OreId;
        trade.buy_price = 10;
        trade.sell_price = 5;
        trade.max_buy_amount = 4;
        data.shop_trade = {trade};

        server::ShopCraftItem craft;
        craft.item_id = BarId;
        craft.ingredients.resize(4);
        craft.ingredients[0].id = OreId;
        craft.ingredients[0].amount = 2;
        data.shop_craft = {craft};

        eolib::protocol::Coords loc;
        loc.x = 3;
        loc.y = 4;
        shopkeeper = new NPC(map, ShopNpcId, loc, NPC::StationarySpawnType, 0, Direction::Down, map->GenerateNPCIndex(), true);
        map->npcs.push_back(shopkeeper);
        shopkeeper->Spawn();

        Character* character = GivenCharacter(3, 3).character;
        character->npc = shopkeeper;
        character->npc_type = pub::NpcType::Shop;
        character->npc_session_id = ShopSessionId;
        character->AddItem(GoldId, 100);
        character->AddItem(OreId, 3);
        return character;
    }

    static client::ShopBuyClientPacket Buy(int amount, int session_id)
    {
        client::ShopBuyClientPacket packet;
        packet.buy_item.id = OreId;
        packet.buy_item.amount = amount;
        packet.session_id = session_id;
        return packet;
    }

    static client::ShopSellClientPacket Sell(int amount, int session_id)
    {
        client::ShopSellClientPacket packet;
        packet.sell_item.id = OreId;
        packet.sell_item.amount = amount;
        packet.session_id = session_id;
        return packet;
    }

    static client::ShopCreateClientPacket Create(int session_id)
    {
        client::ShopCreateClientPacket packet;
        packet.craft_item_id = BarId;
        packet.session_id = session_id;
        return packet;
    }
};

}

TEST_F(ShopTests, Open_Shop_SendsItemsWithNpcSession)
{
    Character* character = GivenCharacterAtShop();
    character->npc = nullptr;
    character->npc_type = pub::NpcType::Friendly;

    client::ShopOpenClientPacket packet;
    packet.npc_index = shopkeeper->index;
    Handlers::Shop_Open(character, packet);

    const auto reply = LastSent<server::ShopOpenServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_NE(0, reply->session_id);
    EXPECT_EQ(character->npc_session_id, reply->session_id);
    EXPECT_EQ("Smith", reply->shop_name);
    EXPECT_EQ(server->world->npc_data[ShopNpcId]->shop_trade, reply->trade_items);
    EXPECT_EQ(server->world->npc_data[ShopNpcId]->shop_craft, reply->craft_items);
    EXPECT_EQ(shopkeeper, character->npc);
    EXPECT_EQ(pub::NpcType::Shop, character->npc_type);
}

TEST_F(ShopTests, Buy_MatchingSession_BuysItem)
{
    Character* character = GivenCharacterAtShop();

    Handlers::Shop_Buy(character, Buy(2, ShopSessionId));

    const auto reply = LastSent<server::ShopBuyServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_EQ(80, reply->gold_amount);
    EXPECT_EQ(OreId, reply->bought_item.id);
    EXPECT_EQ(2, reply->bought_item.amount);
    EXPECT_EQ(5, character->HasItem(OreId));
}

TEST_F(ShopTests, Buy_WrongSession_Ignored)
{
    GivenCharacterAtShop();
    PlayingCharacter& entry = *playing.back();

    EXPECT_CALL(*entry.client, Close(testing::_)).Times(0);
    Handlers::Shop_Buy(entry.character, Buy(2, ShopSessionId + 1));

    EXPECT_FALSE(LastSent<server::ShopBuyServerPacket>(entry));
    EXPECT_EQ(100, entry.character->HasItem(GoldId));
}

TEST_F(ShopTests, Buy_WrongSessionWithEnforceSessionsOff_Ignored)
{
    Character* character = GivenCharacterAtShop();
    server->world->config["EnforceSessions"] = false;

    Handlers::Shop_Buy(character, Buy(2, ShopSessionId + 1));

    EXPECT_FALSE(LastSent<server::ShopBuyServerPacket>(*playing.back()));
    EXPECT_EQ(100, character->HasItem(GoldId));
}

TEST_F(ShopTests, Buy_OverMaxShopBuy_Ignored)
{
    Character* character = GivenCharacterAtShop();

    Handlers::Shop_Buy(character, Buy(static_cast<int>(server->world->config["MaxShopBuy"]) + 1, ShopSessionId));

    EXPECT_FALSE(LastSent<server::ShopBuyServerPacket>(*playing.back()));
}

TEST_F(ShopTests, Sell_MatchingSession_SellsItem)
{
    Character* character = GivenCharacterAtShop();

    Handlers::Shop_Sell(character, Sell(2, ShopSessionId));

    const auto reply = LastSent<server::ShopSellServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_EQ(1, reply->sold_item.amount);
    EXPECT_EQ(OreId, reply->sold_item.id);
    EXPECT_EQ(110, reply->gold_amount);
}

TEST_F(ShopTests, Sell_WrongSession_Ignored)
{
    Character* character = GivenCharacterAtShop();

    Handlers::Shop_Sell(character, Sell(2, ShopSessionId + 1));

    EXPECT_FALSE(LastSent<server::ShopSellServerPacket>(*playing.back()));
    EXPECT_EQ(3, character->HasItem(OreId));
}

TEST_F(ShopTests, Create_HasIngredients_CraftsItem)
{
    Character* character = GivenCharacterAtShop();

    Handlers::Shop_Create(character, Create(ShopSessionId));

    const auto reply = LastSent<server::ShopCreateServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_EQ(BarId, reply->craft_item_id);
    ASSERT_EQ(4u, reply->ingredients.size());
    EXPECT_EQ(OreId, reply->ingredients[0].id);
    EXPECT_EQ(1, reply->ingredients[0].amount);
    EXPECT_EQ(1, character->HasItem(BarId));
}

TEST_F(ShopTests, Create_MissingIngredients_Ignored)
{
    Character* character = GivenCharacterAtShop();
    character->DelItem(OreId, 2);

    Handlers::Shop_Create(character, Create(ShopSessionId));

    EXPECT_FALSE(LastSent<server::ShopCreateServerPacket>(*playing.back()));
    EXPECT_EQ(0, character->HasItem(BarId));
}

TEST_F(ShopTests, Create_WrongSession_Ignored)
{
    Character* character = GivenCharacterAtShop();

    Handlers::Shop_Create(character, Create(ShopSessionId + 1));

    EXPECT_FALSE(LastSent<server::ShopCreateServerPacket>(*playing.back()));
    EXPECT_EQ(3, character->HasItem(OreId));
}
