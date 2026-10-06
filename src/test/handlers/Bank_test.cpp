#include "../testhelper/playing.hpp"

// include the CPP file with the Bank functions in it for testing
#include "../../handlers/Bank.cpp"

#include "../../npc.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdio>
#include <limits>

namespace client = eolib::protocol::net::client;
namespace pub = eolib::protocol::pub;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short BankTestServerPort = 38121;
constexpr int BankTestMapId = 26;
constexpr int BankerNpcId = 1;

class BankTests : public PlayingTest
{
protected:
    NPC* banker = nullptr;

    BankTests()
        : PlayingTest(BankTestServerPort, BankTestMapId)
    {
    }

    void ConfigureServer(Config& config) override
    {
        pub::EnfRecord banker_record;
        banker_record.name = "Banker";
        banker_record.type = pub::NpcType::Bank;
        banker_record.hp = 10;

        pub::Enf enf;
        enf.rid = {1, 2};
        enf.total_npcs_count = 1;
        enf.npcs = {banker_record};
        WriteFile("bank_test.enf", enf);

        pub::EifRecord gold;
        gold.name = "Gold";

        pub::Eif eif;
        eif.rid = {1, 2};
        eif.total_items_count = 1;
        eif.items = {gold};
        WriteFile("bank_test.eif", eif);

        config["ENF"] = "bank_test.enf";
        config["EIF"] = "bank_test.eif";
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove("bank_test.enf");
        std::remove("bank_test.eif");
    }

    PlayingCharacter& GivenCharacterAtBank()
    {
        GivenMap(EmptyEmf(10, 10));

        eolib::protocol::Coords loc;
        loc.x = 3;
        loc.y = 4;
        banker = new NPC(map, BankerNpcId, loc, NPC::StationarySpawnType, 0, Direction::Down, map->GenerateNPCIndex(), true);
        map->npcs.push_back(banker);
        banker->Spawn();

        PlayingCharacter& entry = GivenCharacter(3, 3);
        entry.character->npc = banker;
        entry.character->npc_type = pub::NpcType::Bank;
        entry.character->AddItem(1, 100);
        entry.character->goldbank = 50;
        return entry;
    }

    static client::BankAddClientPacket Add(int amount, int session_id)
    {
        client::BankAddClientPacket packet;
        packet.amount = amount;
        packet.session_id = session_id;
        return packet;
    }

    static client::BankTakeClientPacket Take(int amount, int session_id)
    {
        client::BankTakeClientPacket packet;
        packet.amount = amount;
        packet.session_id = session_id;
        return packet;
    }
};

}

TEST_F(BankTests, Open_Banker_SendsBalanceWithNpcSession)
{
    Character* character = GivenCharacterAtBank().character;
    character->npc = nullptr;
    character->npc_type = pub::NpcType::Friendly;

    client::BankOpenClientPacket packet;
    packet.npc_index = banker->index;
    Handlers::Bank_Open(character, packet);

    const auto reply = LastSent<server::BankOpenServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_EQ(50, reply->gold_bank);
    EXPECT_EQ(BankerNpcId, reply->session_id);
    EXPECT_EQ(banker, character->npc);
    EXPECT_EQ(pub::NpcType::Bank, character->npc_type);
}

TEST_F(BankTests, Add_MatchingSession_DepositsGold)
{
    Character* character = GivenCharacterAtBank().character;

    Handlers::Bank_Add(character, Add(30, BankerNpcId));

    const auto reply = LastSent<server::BankReplyServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_EQ(70, reply->gold_inventory);
    EXPECT_EQ(80, reply->gold_bank);
}

TEST_F(BankTests, Add_WrongSession_ClosesClient)
{
    PlayingCharacter& entry = GivenCharacterAtBank();

    EXPECT_CALL(*entry.client, Close(false)).Times(1);
    Handlers::Bank_Add(entry.character, Add(30, BankerNpcId + 1));

    EXPECT_FALSE(LastSent<server::BankReplyServerPacket>(entry));
    EXPECT_EQ(50, entry.character->goldbank);
}

TEST_F(BankTests, Take_MatchingSession_WithdrawsGold)
{
    Character* character = GivenCharacterAtBank().character;

    Handlers::Bank_Take(character, Take(20, BankerNpcId));

    const auto reply = LastSent<server::BankReplyServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_EQ(120, reply->gold_inventory);
    EXPECT_EQ(30, reply->gold_bank);
}

TEST_F(BankTests, Take_WrongSession_ClosesClient)
{
    PlayingCharacter& entry = GivenCharacterAtBank();

    EXPECT_CALL(*entry.client, Close(false)).Times(1);
    Handlers::Bank_Take(entry.character, Take(20, BankerNpcId + 1));

    EXPECT_EQ(50, entry.character->goldbank);
}

TEST_F(BankTests, Take_WrongSessionWithEnforceSessionsOff_WithdrawsGold)
{
    PlayingCharacter& entry = GivenCharacterAtBank();
    server->world->config["EnforceSessions"] = false;

    EXPECT_CALL(*entry.client, Close(testing::_)).Times(0);
    Handlers::Bank_Take(entry.character, Take(20, BankerNpcId + 1));

    EXPECT_EQ(30, entry.character->goldbank);
}

TEST_F(BankTests, Take_NotAtBank_Ignored)
{
    PlayingCharacter& entry = GivenCharacterAtBank();
    entry.character->npc_type = pub::NpcType::Friendly;

    EXPECT_CALL(*entry.client, Close(testing::_)).Times(0);
    Handlers::Bank_Take(entry.character, Take(20, BankerNpcId));

    EXPECT_FALSE(LastSent<server::BankReplyServerPacket>(entry));
    EXPECT_EQ(50, entry.character->goldbank);
}

TEST_F(BankTests, Add_ExceedsMaxBankGold_Ignored)
{
    PlayingCharacter& entry = GivenCharacterAtBank();
    server->world->config["MaxBankGold"] = 70;

    Handlers::Bank_Add(entry.character, Add(30, BankerNpcId));

    EXPECT_FALSE(LastSent<server::BankReplyServerPacket>(entry));
    EXPECT_EQ(50, entry.character->goldbank);
    EXPECT_EQ(100, entry.character->HasItem(1));
}

TEST_F(BankTests, Add_NearIntMax_IgnoredWithoutOverflow)
{
    PlayingCharacter& entry = GivenCharacterAtBank();
    server->world->config["MaxBankGold"] = std::numeric_limits<int>::max();
    entry.character->goldbank = std::numeric_limits<int>::max() - 10;

    Handlers::Bank_Add(entry.character, Add(30, BankerNpcId));

    EXPECT_FALSE(LastSent<server::BankReplyServerPacket>(entry));
    EXPECT_EQ(std::numeric_limits<int>::max() - 10, entry.character->goldbank);
}

TEST_F(BankTests, Take_MoreThanBanked_Ignored)
{
    PlayingCharacter& entry = GivenCharacterAtBank();

    Handlers::Bank_Take(entry.character, Take(51, BankerNpcId));

    EXPECT_FALSE(LastSent<server::BankReplyServerPacket>(entry));
    EXPECT_EQ(50, entry.character->goldbank);
}

TEST_F(BankTests, Take_ExceedsMaxItem_IgnoredWithoutLosingGold)
{
    PlayingCharacter& entry = GivenCharacterAtBank();
    server->world->config["MaxItem"] = 120;

    Handlers::Bank_Take(entry.character, Take(30, BankerNpcId));

    EXPECT_FALSE(LastSent<server::BankReplyServerPacket>(entry));
    EXPECT_EQ(50, entry.character->goldbank);
    EXPECT_EQ(100, entry.character->HasItem(1));
}
