#include "../testhelper/playing.hpp"

// include the CPP file with the Trade functions in it for testing
#include "../../handlers/Trade.cpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdio>

namespace client = eolib::protocol::net::client;
namespace pub = eolib::protocol::pub;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short TradeTestServerPort = 38127;
constexpr int TradeTestMapId = 32;
constexpr int GoldId = 1;
constexpr int RockId = 2;

class TradeTests : public PlayingTest
{
protected:
    TradeTests()
        : PlayingTest(TradeTestServerPort, TradeTestMapId)
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
        WriteFile("trade_test.eif", eif);

        config["EIF"] = "trade_test.eif";
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove("trade_test.eif");
    }

    static client::TradeRequestClientPacket Request(const Character* target)
    {
        client::TradeRequestClientPacket packet;
        packet.request_type = client::TradeRequestClientPacket::DEFAULT_REQUEST_TYPE;
        packet.player_id = target->PlayerID();
        return packet;
    }

    static client::TradeAcceptClientPacket Accept(const Character* requester)
    {
        client::TradeAcceptClientPacket packet;
        packet.request_type = client::TradeAcceptClientPacket::DEFAULT_REQUEST_TYPE;
        packet.player_id = requester->PlayerID();
        return packet;
    }

    static client::TradeAddClientPacket Add(int item_id, int amount)
    {
        client::TradeAddClientPacket packet;
        packet.add_item.id = item_id;
        packet.add_item.amount = amount;
        return packet;
    }

    static client::TradeAgreeClientPacket Agree(bool agree)
    {
        client::TradeAgreeClientPacket packet;
        packet.agree = agree;
        return packet;
    }

    static void GivenTrade(PlayingCharacter& requester, PlayingCharacter& accepter)
    {
        Handlers::Trade_Request(requester.character, Request(accepter.character));
        Handlers::Trade_Accept(accepter.character, Accept(requester.character));
        ASSERT_TRUE(requester.character->trading);
        ASSERT_TRUE(accepter.character->trading);
        requester.sent.clear();
        accepter.sent.clear();
    }
};

}

TEST_F(TradeTests, Request_ValidTarget_SendsRequestToTarget)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenCharacter(2, 2);
    PlayingCharacter& target = GivenCharacter(3, 2);

    Handlers::Trade_Request(requester.character, Request(target.character));

    const auto request = LastSent<server::TradeRequestServerPacket>(target);
    ASSERT_TRUE(request);
    EXPECT_EQ(requester.character->PlayerID(), static_cast<unsigned int>(request->partner_player_id));
    EXPECT_EQ(requester.character->SourceName(), request->partner_player_name);
    EXPECT_EQ(target.character, requester.character->trade_partner);
}

TEST_F(TradeTests, Request_WrongRequestType_Ignored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenCharacter(2, 2);
    PlayingCharacter& target = GivenCharacter(3, 2);
    client::TradeRequestClientPacket packet = Request(target.character);
    packet.request_type = 137;

    Handlers::Trade_Request(requester.character, packet);

    EXPECT_TRUE(target.sent.empty());
    EXPECT_EQ(nullptr, requester.character->trade_partner);
}

TEST_F(TradeTests, Accept_PendingRequest_OpensTradeForBoth)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenCharacter(2, 2);
    PlayingCharacter& accepter = GivenCharacter(3, 2);
    Handlers::Trade_Request(requester.character, Request(accepter.character));

    Handlers::Trade_Accept(accepter.character, Accept(requester.character));

    const auto accepter_open = LastSent<server::TradeOpenServerPacket>(accepter);
    ASSERT_TRUE(accepter_open);
    EXPECT_EQ(requester.character->PlayerID(), static_cast<unsigned int>(accepter_open->partner_player_id));
    EXPECT_EQ(accepter.character->PlayerID(), static_cast<unsigned int>(accepter_open->your_player_id));

    const auto requester_open = LastSent<server::TradeOpenServerPacket>(requester);
    ASSERT_TRUE(requester_open);
    EXPECT_EQ(accepter.character->PlayerID(), static_cast<unsigned int>(requester_open->partner_player_id));
    EXPECT_EQ(requester.character->SourceName(), requester_open->your_player_name);

    EXPECT_TRUE(requester.character->trading);
    EXPECT_TRUE(accepter.character->trading);
}

TEST_F(TradeTests, Accept_WrongRequestType_Ignored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenCharacter(2, 2);
    PlayingCharacter& accepter = GivenCharacter(3, 2);
    Handlers::Trade_Request(requester.character, Request(accepter.character));
    client::TradeAcceptClientPacket packet = Accept(requester.character);
    packet.request_type = 1;

    Handlers::Trade_Accept(accepter.character, packet);

    EXPECT_FALSE(LastSent<server::TradeOpenServerPacket>(accepter));
    EXPECT_FALSE(accepter.character->trading);
}

TEST_F(TradeTests, Add_HeldItem_SendsTradeDataToBoth)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenCharacter(2, 2);
    PlayingCharacter& accepter = GivenCharacter(3, 2);
    GivenTrade(requester, accepter);
    requester.character->AddItem(RockId, 5);

    Handlers::Trade_Add(requester.character, Add(RockId, 3));

    const auto reply = LastSent<server::TradeReplyServerPacket>(accepter);
    ASSERT_TRUE(reply);
    ASSERT_EQ(2u, reply->trade_data.size());
    EXPECT_EQ(requester.character->PlayerID(), static_cast<unsigned int>(reply->trade_data[0].player_id));
    ASSERT_EQ(1u, reply->trade_data[0].items.size());
    EXPECT_EQ(RockId, reply->trade_data[0].items[0].id);
    EXPECT_EQ(3, reply->trade_data[0].items[0].amount);
    EXPECT_TRUE(reply->trade_data[1].items.empty());
    EXPECT_TRUE(LastSent<server::TradeReplyServerPacket>(requester));
}

TEST_F(TradeTests, Add_PartnerAgreed_SendsAdminToPartnerAndResetsAgreement)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenCharacter(2, 2);
    PlayingCharacter& accepter = GivenCharacter(3, 2);
    GivenTrade(requester, accepter);
    requester.character->AddItem(RockId, 5);
    accepter.character->trade_agree = true;

    Handlers::Trade_Add(requester.character, Add(RockId, 3));

    EXPECT_TRUE(LastSent<server::TradeAdminServerPacket>(accepter));
    EXPECT_FALSE(LastSent<server::TradeReplyServerPacket>(accepter));
    EXPECT_TRUE(LastSent<server::TradeReplyServerPacket>(requester));
    EXPECT_FALSE(accepter.character->trade_agree);
}

TEST_F(TradeTests, Remove_OfferedItem_SendsTradeDataToBoth)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenCharacter(2, 2);
    PlayingCharacter& accepter = GivenCharacter(3, 2);
    GivenTrade(requester, accepter);
    requester.character->AddItem(RockId, 5);
    Handlers::Trade_Add(requester.character, Add(RockId, 3));
    accepter.sent.clear();

    client::TradeRemoveClientPacket packet;
    packet.item_id = RockId;
    Handlers::Trade_Remove(requester.character, packet);

    const auto reply = LastSent<server::TradeReplyServerPacket>(accepter);
    ASSERT_TRUE(reply);
    ASSERT_EQ(2u, reply->trade_data.size());
    EXPECT_TRUE(reply->trade_data[0].items.empty());
}

TEST_F(TradeTests, Remove_AfterAgreeing_Ignored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenCharacter(2, 2);
    PlayingCharacter& accepter = GivenCharacter(3, 2);
    GivenTrade(requester, accepter);
    requester.character->AddItem(RockId, 5);
    Handlers::Trade_Add(requester.character, Add(RockId, 3));
    Handlers::Trade_Agree(requester.character, Agree(true));
    requester.sent.clear();
    accepter.sent.clear();

    client::TradeRemoveClientPacket packet;
    packet.item_id = RockId;
    Handlers::Trade_Remove(requester.character, packet);

    EXPECT_TRUE(requester.sent.empty());
    EXPECT_TRUE(accepter.sent.empty());
    ASSERT_EQ(1u, requester.character->trade_inventory.size());
    EXPECT_TRUE(requester.character->trade_agree);
}

TEST_F(TradeTests, Agree_PartnerNotAgreed_SendsSpecAndAgree)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenCharacter(2, 2);
    PlayingCharacter& accepter = GivenCharacter(3, 2);
    GivenTrade(requester, accepter);
    requester.character->AddItem(RockId, 5);
    Handlers::Trade_Add(requester.character, Add(RockId, 3));

    Handlers::Trade_Agree(requester.character, Agree(true));

    const auto spec = LastSent<server::TradeSpecServerPacket>(requester);
    ASSERT_TRUE(spec);
    EXPECT_TRUE(spec->agree);

    const auto agree = LastSent<server::TradeAgreeServerPacket>(accepter);
    ASSERT_TRUE(agree);
    EXPECT_EQ(accepter.character->PlayerID(), static_cast<unsigned int>(agree->partner_player_id));
    EXPECT_TRUE(agree->agree);
}

TEST_F(TradeTests, Agree_BothAgreed_ExchangesItems)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenCharacter(2, 2);
    PlayingCharacter& accepter = GivenCharacter(3, 2);
    GivenTrade(requester, accepter);
    requester.character->AddItem(RockId, 5);
    accepter.character->AddItem(GoldId, 100);
    Handlers::Trade_Add(requester.character, Add(RockId, 3));
    Handlers::Trade_Add(accepter.character, Add(GoldId, 40));
    Handlers::Trade_Agree(accepter.character, Agree(true));

    Handlers::Trade_Agree(requester.character, Agree(true));

    const auto use = LastSent<server::TradeUseServerPacket>(accepter);
    ASSERT_TRUE(use);
    ASSERT_EQ(2u, use->trade_data.size());
    EXPECT_EQ(accepter.character->PlayerID(), static_cast<unsigned int>(use->trade_data[0].player_id));
    EXPECT_TRUE(LastSent<server::TradeUseServerPacket>(requester));

    EXPECT_EQ(2, requester.character->HasItem(RockId));
    EXPECT_EQ(40, requester.character->HasItem(GoldId));
    EXPECT_EQ(3, accepter.character->HasItem(RockId));
    EXPECT_EQ(60, accepter.character->HasItem(GoldId));
    EXPECT_FALSE(requester.character->trading);
    EXPECT_FALSE(accepter.character->trading);
}

TEST_F(TradeTests, Close_Trading_TellsPartnerAndEndsTrade)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenCharacter(2, 2);
    PlayingCharacter& accepter = GivenCharacter(3, 2);
    GivenTrade(requester, accepter);

    Handlers::Trade_Close(requester.character, client::TradeCloseClientPacket());

    const auto close = LastSent<server::TradeCloseServerPacket>(accepter);
    ASSERT_TRUE(close);
    EXPECT_EQ(requester.character->PlayerID(), static_cast<unsigned int>(close->partner_player_id));
    EXPECT_FALSE(requester.character->trading);
    EXPECT_FALSE(accepter.character->trading);
    EXPECT_EQ(nullptr, accepter.character->trade_partner);
}
