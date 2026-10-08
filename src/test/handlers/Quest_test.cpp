#include "../testhelper/playing.hpp"

// include the CPP file with the Quest functions in it for testing
#include "../../handlers/Quest.cpp"

#include "../../npc.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdio>
#include <fstream>
#include <memory>

namespace client = eolib::protocol::net::client;
namespace net = eolib::protocol::net;
namespace pub = eolib::protocol::pub;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short QuestTestServerPort = 38142;
constexpr int QuestTestMapId = 47;
constexpr int QuestNpcId = 1;
constexpr int TestQuestId = 1;
constexpr int AcceptLinkId = 1;

class QuestTests : public PlayingTest
{
protected:
    NPC* quest_npc = nullptr;

    QuestTests()
        : PlayingTest(QuestTestServerPort, QuestTestMapId)
    {
    }

    void ConfigureServer(Config& config) override
    {
        pub::EnfRecord quest_record;
        quest_record.name = "Quest Giver";
        quest_record.type = pub::NpcType::Quest;
        quest_record.behavior_id = TestQuestId;
        quest_record.hp = 10;

        pub::Enf enf;
        enf.rid = {1, 2};
        enf.total_npcs_count = 1;
        enf.npcs = {quest_record};
        WriteFile("quest_test.enf", enf);

        // Quest files are named QuestDir + the zero-padded quest ID
        std::ofstream quest("quest_test_00001.eqf");
        quest << "Main\n{\n\tquestname \"Test Quest\"\n\tversion 1\n}\n"
              << "state Begin\n{\n\tdesc \"Say yes\"\n"
              << "\taction AddNpcText(1, \"Hello\");\n"
              << "\taction AddNpcInput(1, 1, \"Yes\");\n"
              << "\trule InputNpc(1) goto Finish\n}\n"
              << "state Finish\n{\n\taction End();\n}\n";

        config["ENF"] = "quest_test.enf";
        config["QuestDir"] = "quest_test_";
        config["Quests"] = 0;
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove("quest_test.enf");
        std::remove("quest_test_00001.eqf");
    }

    PlayingCharacter& GivenCharacterNearQuestNpc()
    {
        GivenMap(EmptyEmf(10, 10));
        server->world->quests[TestQuestId] = std::make_shared<Quest>(TestQuestId, server->world);

        eolib::protocol::Coords loc;
        loc.x = 3;
        loc.y = 4;
        quest_npc = new NPC(map, QuestNpcId, loc, NPC::StationarySpawnType, 0, Direction::Down, map->GenerateNPCIndex(), true);
        map->npcs.push_back(quest_npc);
        quest_npc->Spawn();

        return GivenCharacter(3, 3);
    }

    void TalkTo(Character* character)
    {
        client::QuestUseClientPacket packet;
        packet.npc_index = quest_npc->index;
        packet.quest_id = 0;
        Handlers::Quest_Use(character, packet);
    }

    static client::QuestAcceptClientPacket AcceptLink(int session_id, int action)
    {
        client::QuestAcceptClientPacket::ReplyTypeDataLink link;
        link.action = action;

        client::QuestAcceptClientPacket packet;
        packet.session_id = session_id;
        packet.quest_id = TestQuestId;
        packet.behavior_id = TestQuestId;
        packet.reply_type = client::DialogReply::Link;
        packet.reply_type_data = link;
        return packet;
    }

    static client::QuestListClientPacket List(net::QuestPage page)
    {
        client::QuestListClientPacket packet;
        packet.page = page;
        return packet;
    }
};

}

TEST_F(QuestTests, Use_QuestNpc_SendsDialogWithNpcSession)
{
    PlayingCharacter& entry = GivenCharacterNearQuestNpc();

    TalkTo(entry.character);

    const auto reply = LastSent<server::QuestDialogServerPacket>(entry);
    ASSERT_TRUE(reply);
    EXPECT_EQ(TestQuestId, reply->behavior_id);
    EXPECT_EQ(TestQuestId, reply->quest_id);
    EXPECT_NE(0, reply->session_id);
    EXPECT_EQ(entry.character->npc_session_id, reply->session_id);
    ASSERT_EQ(1u, reply->quest_entries.size());
    EXPECT_EQ(TestQuestId, reply->quest_entries[0].quest_id);
    EXPECT_EQ("Test Quest", reply->quest_entries[0].quest_name);

    ASSERT_EQ(2u, reply->dialog_entries.size());
    EXPECT_EQ(server::DialogEntryType::Text, reply->dialog_entries[0].entry_type);
    EXPECT_EQ("Hello", reply->dialog_entries[0].line);
    ASSERT_EQ(server::DialogEntryType::Link, reply->dialog_entries[1].entry_type);
    EXPECT_EQ(AcceptLinkId, reply->dialog_entries[1].AsLink()->link_id);
    EXPECT_EQ("Yes", reply->dialog_entries[1].line);

    EXPECT_EQ(pub::NpcType::Quest, entry.character->npc_type);
}

TEST_F(QuestTests, Accept_Link_AdvancesQuest)
{
    PlayingCharacter& entry = GivenCharacterNearQuestNpc();
    TalkTo(entry.character);

    Handlers::Quest_Accept(entry.character, AcceptLink(entry.character->npc_session_id, AcceptLinkId));

    auto context = entry.character->GetQuest(TestQuestId);
    ASSERT_TRUE(context);
    EXPECT_EQ("end", context->StateName());
}

TEST_F(QuestTests, Accept_WrongSession_Ignored)
{
    PlayingCharacter& entry = GivenCharacterNearQuestNpc();
    TalkTo(entry.character);

    EXPECT_CALL(*entry.client, Close(testing::_)).Times(0);
    Handlers::Quest_Accept(entry.character, AcceptLink(entry.character->npc_session_id + 1, AcceptLinkId));

    EXPECT_EQ("begin", entry.character->GetQuest(TestQuestId)->StateName());
}

TEST_F(QuestTests, List_Progress_SendsActiveQuests)
{
    PlayingCharacter& entry = GivenCharacterNearQuestNpc();
    TalkTo(entry.character);

    Handlers::Quest_List(entry.character, List(net::QuestPage::Progress));

    const auto reply = LastSent<server::QuestListServerPacket>(entry);
    ASSERT_TRUE(reply);
    EXPECT_EQ(1, reply->quests_count);
    const auto* progress = reply->AsProgress();
    ASSERT_NE(nullptr, progress);
    ASSERT_EQ(1u, progress->quest_progress_entries.size());
    EXPECT_EQ("Test Quest", progress->quest_progress_entries[0].name);
    EXPECT_EQ("Say yes", progress->quest_progress_entries[0].description);
    EXPECT_EQ(server::QuestRequirementIcon::Talk, progress->quest_progress_entries[0].icon);
}

TEST_F(QuestTests, List_HistoryAfterFinishing_SendsQuestName)
{
    PlayingCharacter& entry = GivenCharacterNearQuestNpc();
    TalkTo(entry.character);
    Handlers::Quest_Accept(entry.character, AcceptLink(entry.character->npc_session_id, AcceptLinkId));

    Handlers::Quest_List(entry.character, List(net::QuestPage::History));

    const auto reply = LastSent<server::QuestListServerPacket>(entry);
    ASSERT_TRUE(reply);
    const auto* history = reply->AsHistory();
    ASSERT_NE(nullptr, history);
    EXPECT_EQ((std::vector<std::string>{"Test Quest"}), history->completed_quests);
}
