#include "../testhelper/playing.hpp"

// include the CPP file with the Priest functions in it for testing
#include "../../handlers/Priest.cpp"

#include "../../npc.hpp"
#include "../../wedding.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdio>

namespace client = eolib::protocol::net::client;
namespace pub = eolib::protocol::pub;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short PriestTestServerPort = 38139;
constexpr int PriestTestMapId = 44;
constexpr int PriestNpcId = 1;
constexpr int PriestSessionId = 1234;

class PriestTests : public PlayingTest
{
protected:
    NPC* priest = nullptr;

    PriestTests()
        : PlayingTest(PriestTestServerPort, PriestTestMapId)
    {
    }

    void ConfigureServer(Config& config) override
    {
        pub::EnfRecord priest_record;
        priest_record.name = "Priest";
        priest_record.type = pub::NpcType::Priest;
        priest_record.hp = 10;

        pub::Enf enf;
        enf.rid = {1, 2};
        enf.total_npcs_count = 1;
        enf.npcs = {priest_record};
        WriteFile("priest_test.enf", enf);

        config["ENF"] = "priest_test.enf";
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove("priest_test.enf");
    }

    void GivenChapel()
    {
        GivenMap(EmptyEmf(10, 10));
        server->world->config["WeddingOutfitMale"] = "0";
        server->world->config["WeddingOutfitFemale"] = "0";
        server->world->config["WeddingMinLevel"] = 5;

        eolib::protocol::Coords loc;
        loc.x = 3;
        loc.y = 4;
        priest = new NPC(map, PriestNpcId, loc, NPC::StationarySpawnType, 0, Direction::Down, map->GenerateNPCIndex(), true);
        map->npcs.push_back(priest);
        priest->Spawn();
        map->LoadWedding();
    }

    // Two engaged characters in the chapel; the first is talking to the priest
    std::pair<PlayingCharacter*, PlayingCharacter*> GivenEngagedCouple()
    {
        GivenChapel();

        PlayingCharacter& requester = GivenOnlineCharacter(3, 3);
        PlayingCharacter& partner = GivenOnlineCharacter(4, 3);
        requester.character->level = 5;
        partner.character->level = 5;
        requester.character->fiance = partner.character->SourceName();
        partner.character->fiance = requester.character->SourceName();
        requester.character->npc = priest;
        requester.character->npc_type = pub::NpcType::Priest;
        requester.character->npc_session_id = PriestSessionId;
        return {&requester, &partner};
    }

    static client::PriestRequestClientPacket Request(int session_id, const std::string& name)
    {
        client::PriestRequestClientPacket packet;
        packet.session_id = session_id;
        packet.name = name;
        return packet;
    }

    static client::PriestAcceptClientPacket Accept(int session_id)
    {
        client::PriestAcceptClientPacket packet;
        packet.session_id = session_id;
        return packet;
    }
};

}

TEST_F(PriestTests, Open_Priest_SendsNpcSession)
{
    GivenChapel();
    Character* character = GivenCharacter(3, 3).character;

    client::PriestOpenClientPacket packet;
    packet.npc_index = priest->index;
    Handlers::Priest_Open(character, packet);

    const auto reply = LastSent<server::PriestOpenServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_NE(0, reply->session_id);
    EXPECT_EQ(character->npc_session_id, reply->session_id);
    EXPECT_EQ(priest, character->npc);
    EXPECT_EQ(pub::NpcType::Priest, character->npc_type);
}

TEST_F(PriestTests, Request_EngagedCouple_SendsRequesterIdToPartner)
{
    auto [requester, partner] = GivenEngagedCouple();

    Handlers::Priest_Request(requester->character, Request(PriestSessionId, partner->character->SourceName()));

    const auto request = LastSent<server::PriestRequestServerPacket>(*partner);
    ASSERT_TRUE(request);
    EXPECT_NE(0, request->session_id);
    EXPECT_EQ(partner->character->npc_session_id, request->session_id);
    EXPECT_EQ(requester->character->SourceName(), request->partner_name);
    EXPECT_TRUE(map->wedding->RequestedWedding(requester->character));
}

TEST_F(PriestTests, Request_WrongSession_Ignored)
{
    auto [requester, partner] = GivenEngagedCouple();

    EXPECT_CALL(*requester->client, Close(testing::_)).Times(0);
    Handlers::Priest_Request(requester->character, Request(PriestSessionId + 1, partner->character->SourceName()));

    EXPECT_FALSE(LastSent<server::PriestRequestServerPacket>(*partner));
    EXPECT_FALSE(LastSent<server::PriestReplyServerPacket>(*requester));
}

TEST_F(PriestTests, Request_LowLevel_SendsLowLevelReply)
{
    auto [requester, partner] = GivenEngagedCouple();
    requester->character->level = 4;

    Handlers::Priest_Request(requester->character, Request(PriestSessionId, partner->character->SourceName()));

    const auto reply = LastSent<server::PriestReplyServerPacket>(*requester);
    ASSERT_TRUE(reply);
    EXPECT_EQ(server::PriestReply::LowLevel, reply->reply_code);
    EXPECT_FALSE(LastSent<server::PriestRequestServerPacket>(*partner));
}

TEST_F(PriestTests, Accept_RequesterSession_StartsWedding)
{
    auto [requester, partner] = GivenEngagedCouple();
    Handlers::Priest_Request(requester->character, Request(PriestSessionId, partner->character->SourceName()));

    Handlers::Priest_Accept(partner->character, Accept(partner->character->npc_session_id));

    EXPECT_TRUE(map->wedding->Busy());
}

TEST_F(PriestTests, Accept_WrongSession_Ignored)
{
    auto [requester, partner] = GivenEngagedCouple();
    Handlers::Priest_Request(requester->character, Request(PriestSessionId, partner->character->SourceName()));

    EXPECT_CALL(*partner->client, Close(testing::_)).Times(0);
    Handlers::Priest_Accept(partner->character, Accept(partner->character->npc_session_id + 1));

    EXPECT_FALSE(map->wedding->Busy());
}

TEST_F(PriestTests, Request_WeddingBusy_RepliesBusyOnly)
{
    auto [requester, partner] = GivenEngagedCouple();
    Handlers::Priest_Request(requester->character, Request(PriestSessionId, partner->character->SourceName()));
    Handlers::Priest_Accept(partner->character, Accept(partner->character->npc_session_id));
    ASSERT_TRUE(map->wedding->Busy());
    requester->sent.clear();
    partner->sent.clear();

    Handlers::Priest_Request(requester->character, Request(PriestSessionId, partner->character->SourceName()));

    const auto reply = LastSent<server::PriestReplyServerPacket>(*requester);
    ASSERT_TRUE(reply);
    EXPECT_EQ(server::PriestReply::Busy, reply->reply_code);
    EXPECT_EQ(1u, requester->sent.size());
    EXPECT_FALSE(LastSent<server::PriestRequestServerPacket>(*partner));
}
