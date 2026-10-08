#include "../testhelper/playing.hpp"

// include the CPP file with the Citizen functions in it for testing
#include "../../handlers/Citizen.cpp"

#include "../../npc.hpp"
#include "../../npc_data.hpp"

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

constexpr unsigned short CitizenTestServerPort = 38141;
constexpr int CitizenTestMapId = 46;
constexpr int InnkeeperNpcId = 1;
constexpr int InnSessionId = 1234;
constexpr int InnBehaviorId = 3;

class CitizenTests : public PlayingTest
{
protected:
    NPC* innkeeper = nullptr;

    CitizenTests()
        : PlayingTest(CitizenTestServerPort, CitizenTestMapId)
    {
    }

    void ConfigureServer(Config& config) override
    {
        pub::EnfRecord innkeeper_record;
        innkeeper_record.name = "Innkeeper";
        innkeeper_record.type = pub::NpcType::Inn;
        innkeeper_record.behavior_id = InnBehaviorId;
        innkeeper_record.hp = 10;

        pub::Enf enf;
        enf.rid = {1, 2};
        enf.total_npcs_count = 1;
        enf.npcs = {innkeeper_record};
        WriteFile("citizen_test.enf", enf);

        pub::EifRecord gold;
        gold.name = "Gold";

        pub::Eif eif;
        eif.rid = {1, 2};
        eif.total_items_count = 1;
        eif.items = {gold};
        WriteFile("citizen_test.eif", eif);

        config["ENF"] = "citizen_test.enf";
        config["EIF"] = "citizen_test.eif";
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove("citizen_test.enf");
        std::remove("citizen_test.eif");
    }

    PlayingCharacter& GivenCharacterAtInn()
    {
        GivenMap(EmptyEmf(10, 10));
        server->world->config["CitizenSubscribeAnytime"] = false;
        server->world->config["CitizenUnsubscribeAnywhere"] = false;
        server->world->config["InnSleepCostBase"] = 10;
        server->world->config["InnSleepCostPerHP"] = 2;
        server->world->config["InnSleepCostPerTP"] = 1;

        auto citizenship = std::make_unique<NPC_Citizenship>();
        citizenship->home = "town";
        citizenship->questions = {"Q1", "Q2", "Q3"};
        citizenship->answers = {"a1", "a2", "a3"};
        server->world->npc_data[InnkeeperNpcId]->citizenship = std::move(citizenship);

        eolib::protocol::Coords loc;
        loc.x = 3;
        loc.y = 4;
        innkeeper = new NPC(map, InnkeeperNpcId, loc, NPC::StationarySpawnType, 0, Direction::Down, map->GenerateNPCIndex(), true);
        map->npcs.push_back(innkeeper);
        innkeeper->Spawn();

        PlayingCharacter& entry = GivenCharacter(3, 3);
        entry.character->npc = innkeeper;
        entry.character->npc_type = pub::NpcType::Inn;
        entry.character->npc_session_id = InnSessionId;
        entry.character->AddItem(1, 100);
        entry.character->hp = 5;
        entry.character->maxhp = 10;
        entry.character->tp = 7;
        entry.character->maxtp = 10;
        return entry;
    }

    static client::CitizenRequestClientPacket Request(int session_id, int behavior_id)
    {
        client::CitizenRequestClientPacket packet;
        packet.session_id = session_id;
        packet.behavior_id = behavior_id;
        return packet;
    }

    static client::CitizenReplyClientPacket Reply(int session_id, int behavior_id, std::vector<std::string> answers)
    {
        client::CitizenReplyClientPacket packet;
        packet.session_id = session_id;
        packet.behavior_id = behavior_id;
        packet.answers = std::move(answers);
        return packet;
    }

    static client::CitizenRemoveClientPacket Remove(int behavior_id)
    {
        client::CitizenRemoveClientPacket packet;
        packet.behavior_id = behavior_id;
        return packet;
    }
};

}

TEST_F(CitizenTests, Open_Innkeeper_SendsQuestionsAndSession)
{
    Character* character = GivenCharacterAtInn().character;
    character->npc = nullptr;
    character->npc_type = pub::NpcType::Friendly;

    client::CitizenOpenClientPacket packet;
    packet.npc_index = innkeeper->index;
    Handlers::Citizen_Open(character, packet);

    const auto reply = LastSent<server::CitizenOpenServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_EQ(InnBehaviorId, reply->behavior_id);
    EXPECT_EQ(0, reply->current_home_id);
    EXPECT_NE(0, reply->session_id);
    EXPECT_EQ(character->npc_session_id, reply->session_id);
    EXPECT_EQ((std::vector<std::string>{"Q1", "Q2", "Q3"}), reply->questions);
    EXPECT_EQ(innkeeper, character->npc);
    EXPECT_EQ(pub::NpcType::Inn, character->npc_type);
}

TEST_F(CitizenTests, Request_MatchingSession_SendsSleepCost)
{
    PlayingCharacter& entry = GivenCharacterAtInn();

    Handlers::Citizen_Request(entry.character, Request(InnSessionId, InnBehaviorId));

    const auto reply = LastSent<server::CitizenRequestServerPacket>(entry);
    ASSERT_TRUE(reply);
    EXPECT_EQ(10 + (5 * 2) + (3 * 1), reply->cost);
}

TEST_F(CitizenTests, Request_WrongSession_Ignored)
{
    PlayingCharacter& entry = GivenCharacterAtInn();

    EXPECT_CALL(*entry.client, Close(testing::_)).Times(0);
    Handlers::Citizen_Request(entry.character, Request(InnSessionId + 1, InnBehaviorId));

    EXPECT_FALSE(LastSent<server::CitizenRequestServerPacket>(entry));
}

TEST_F(CitizenTests, Accept_EnoughGold_RestoresAndCharges)
{
    PlayingCharacter& entry = GivenCharacterAtInn();

    client::CitizenAcceptClientPacket packet;
    packet.session_id = InnSessionId;
    packet.behavior_id = InnBehaviorId;
    Handlers::Citizen_Accept(entry.character, packet);

    const auto reply = LastSent<server::CitizenAcceptServerPacket>(entry);
    ASSERT_TRUE(reply);
    EXPECT_EQ(100 - 23, reply->gold_amount);
    EXPECT_EQ(10, entry.character->hp);
    EXPECT_EQ(10, entry.character->tp);
}

TEST_F(CitizenTests, Reply_CorrectAnswers_SetsHome)
{
    PlayingCharacter& entry = GivenCharacterAtInn();

    Handlers::Citizen_Reply(entry.character, Reply(InnSessionId, InnBehaviorId, {"A1", "a2", "a3"}));

    const auto reply = LastSent<server::CitizenReplyServerPacket>(entry);
    ASSERT_TRUE(reply);
    EXPECT_EQ(0, reply->questions_wrong);
    EXPECT_EQ("town", entry.character->home);
}

TEST_F(CitizenTests, Reply_WrongAnswer_CountsWrongAndKeepsHome)
{
    PlayingCharacter& entry = GivenCharacterAtInn();

    Handlers::Citizen_Reply(entry.character, Reply(InnSessionId, InnBehaviorId, {"a1", "x", "a3"}));

    const auto reply = LastSent<server::CitizenReplyServerPacket>(entry);
    ASSERT_TRUE(reply);
    EXPECT_EQ(1, reply->questions_wrong);
    EXPECT_TRUE(entry.character->home.empty());
}

TEST_F(CitizenTests, Remove_Citizen_Unsubscribes)
{
    PlayingCharacter& entry = GivenCharacterAtInn();
    entry.character->home = "town";

    Handlers::Citizen_Remove(entry.character, Remove(InnBehaviorId));

    const auto reply = LastSent<server::CitizenRemoveServerPacket>(entry);
    ASSERT_TRUE(reply);
    EXPECT_EQ(server::InnUnsubscribeReply::Unsubscribed, reply->reply_code);
    EXPECT_TRUE(entry.character->home.empty());
}

TEST_F(CitizenTests, Remove_NotCitizen_SendsNotCitizen)
{
    PlayingCharacter& entry = GivenCharacterAtInn();
    entry.character->home = "elsewhere";

    Handlers::Citizen_Remove(entry.character, Remove(InnBehaviorId));

    const auto reply = LastSent<server::CitizenRemoveServerPacket>(entry);
    ASSERT_TRUE(reply);
    EXPECT_EQ(server::InnUnsubscribeReply::NotCitizen, reply->reply_code);
    EXPECT_EQ("elsewhere", entry.character->home);
}

TEST_F(CitizenTests, Remove_NotAtInn_Ignored)
{
    PlayingCharacter& entry = GivenCharacterAtInn();
    entry.character->home = "town";
    entry.character->npc_type = pub::NpcType::Friendly;

    Handlers::Citizen_Remove(entry.character, Remove(InnBehaviorId));

    EXPECT_FALSE(LastSent<server::CitizenRemoveServerPacket>(entry));
    EXPECT_EQ("town", entry.character->home);
}
