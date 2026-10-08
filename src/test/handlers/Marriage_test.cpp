#include "../testhelper/playing.hpp"

// include the CPP file with the Marriage functions in it for testing
#include "../../handlers/Marriage.cpp"

#include "../../npc.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdio>

namespace client = eolib::protocol::net::client;
namespace pub = eolib::protocol::pub;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short MarriageTestServerPort = 38140;
constexpr int MarriageTestMapId = 45;
constexpr int LawyerNpcId = 1;
constexpr int LawyerSessionId = 1234;

class MarriageTests : public PlayingTest
{
protected:
    NPC* lawyer = nullptr;

    MarriageTests()
        : PlayingTest(MarriageTestServerPort, MarriageTestMapId)
    {
    }

    void ConfigureServer(Config& config) override
    {
        pub::EnfRecord lawyer_record;
        lawyer_record.name = "Lawyer";
        lawyer_record.type = pub::NpcType::Lawyer;
        lawyer_record.hp = 10;

        pub::Enf enf;
        enf.rid = {1, 2};
        enf.total_npcs_count = 1;
        enf.npcs = {lawyer_record};
        WriteFile("marriage_test.enf", enf);

        pub::EifRecord gold;
        gold.name = "Gold";

        pub::Eif eif;
        eif.rid = {1, 2};
        eif.total_items_count = 1;
        eif.items = {gold};
        WriteFile("marriage_test.eif", eif);

        config["ENF"] = "marriage_test.enf";
        config["EIF"] = "marriage_test.eif";
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove("marriage_test.enf");
        std::remove("marriage_test.eif");
    }

    PlayingCharacter& GivenCharacterAtLawyer()
    {
        GivenMap(EmptyEmf(10, 10));
        server->world->config["MarriagePrice"] = 30;
        server->world->config["DivorcePrice"] = 20;

        eolib::protocol::Coords loc;
        loc.x = 3;
        loc.y = 4;
        lawyer = new NPC(map, LawyerNpcId, loc, NPC::StationarySpawnType, 0, Direction::Down, map->GenerateNPCIndex(), true);
        map->npcs.push_back(lawyer);
        lawyer->Spawn();

        PlayingCharacter& entry = GivenCharacter(3, 3);
        entry.character->npc = lawyer;
        entry.character->npc_type = pub::NpcType::Lawyer;
        entry.character->npc_session_id = LawyerSessionId;
        entry.character->AddItem(1, 100);
        return entry;
    }

    static client::MarriageRequestClientPacket Request(client::MarriageRequestType type, int session_id, const std::string& name)
    {
        client::MarriageRequestClientPacket packet;
        packet.request_type = type;
        packet.session_id = session_id;
        packet.name = name;
        return packet;
    }
};

}

TEST_F(MarriageTests, Open_Lawyer_SendsNpcSession)
{
    Character* character = GivenCharacterAtLawyer().character;
    character->npc = nullptr;
    character->npc_type = pub::NpcType::Friendly;

    client::MarriageOpenClientPacket packet;
    packet.npc_index = lawyer->index;
    Handlers::Marriage_Open(character, packet);

    const auto reply = LastSent<server::MarriageOpenServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_NE(0, reply->session_id);
    EXPECT_EQ(character->npc_session_id, reply->session_id);
    EXPECT_EQ(lawyer, character->npc);
    EXPECT_EQ(pub::NpcType::Lawyer, character->npc_type);
}

TEST_F(MarriageTests, Request_Approval_SetsFianceAndSendsGold)
{
    PlayingCharacter& entry = GivenCharacterAtLawyer();

    Handlers::Marriage_Request(entry.character, Request(client::MarriageRequestType::MarriageApproval, LawyerSessionId, "Partner"));

    EXPECT_EQ("partner", entry.character->fiance);
    const auto reply = LastSent<server::MarriageReplyServerPacket>(entry);
    ASSERT_TRUE(reply);
    ASSERT_EQ(server::MarriageReply::Success, reply->reply_code);
    EXPECT_EQ(70, reply->AsSuccess()->gold_amount);
}

TEST_F(MarriageTests, Request_ApprovalAlreadyMarried_SendsAlreadyMarried)
{
    PlayingCharacter& entry = GivenCharacterAtLawyer();
    entry.character->partner = "spouse";

    Handlers::Marriage_Request(entry.character, Request(client::MarriageRequestType::MarriageApproval, LawyerSessionId, "partner"));

    const auto reply = LastSent<server::MarriageReplyServerPacket>(entry);
    ASSERT_TRUE(reply);
    EXPECT_EQ(server::MarriageReply::AlreadyMarried, reply->reply_code);
    EXPECT_EQ(100, entry.character->HasItem(1));
}

TEST_F(MarriageTests, Request_WrongSession_Ignored)
{
    PlayingCharacter& entry = GivenCharacterAtLawyer();

    EXPECT_CALL(*entry.client, Close(testing::_)).Times(0);
    Handlers::Marriage_Request(entry.character, Request(client::MarriageRequestType::MarriageApproval, LawyerSessionId + 1, "partner"));

    EXPECT_FALSE(LastSent<server::MarriageReplyServerPacket>(entry));
    EXPECT_TRUE(entry.character->fiance.empty());
}

TEST_F(MarriageTests, Request_DivorceOnlinePartner_NotifiesPartner)
{
    PlayingCharacter& entry = GivenCharacterAtLawyer();
    PlayingCharacter& spouse = GivenOnlineCharacter(5, 5);
    entry.character->partner = spouse.character->SourceName();
    spouse.character->partner = entry.character->SourceName();

    Handlers::Marriage_Request(entry.character, Request(client::MarriageRequestType::Divorce, LawyerSessionId, spouse.character->SourceName()));

    EXPECT_TRUE(entry.character->partner.empty());
    EXPECT_TRUE(spouse.character->partner.empty());
    const auto notification = LastSent<server::MarriageReplyServerPacket>(spouse);
    ASSERT_TRUE(notification);
    EXPECT_EQ(server::MarriageReply::DivorceNotification, notification->reply_code);
    const auto reply = LastSent<server::MarriageReplyServerPacket>(entry);
    ASSERT_TRUE(reply);
    ASSERT_EQ(server::MarriageReply::Success, reply->reply_code);
    EXPECT_EQ(80, reply->AsSuccess()->gold_amount);
}
