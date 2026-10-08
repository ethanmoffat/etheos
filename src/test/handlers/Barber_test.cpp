#include "../testhelper/playing.hpp"

// include the CPP file with the Barber functions in it for testing
#include "../../handlers/Barber.cpp"

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

constexpr unsigned short BarberTestServerPort = 38138;
constexpr int BarberTestMapId = 43;
constexpr int BarberNpcId = 1;
constexpr int BarberSessionId = 1234;

class BarberTests : public PlayingTest
{
protected:
    NPC* barber = nullptr;

    BarberTests()
        : PlayingTest(BarberTestServerPort, BarberTestMapId)
    {
    }

    void ConfigureServer(Config& config) override
    {
        pub::EnfRecord barber_record;
        barber_record.name = "Barber";
        barber_record.type = pub::NpcType::Barber;
        barber_record.hp = 10;

        pub::Enf enf;
        enf.rid = {1, 2};
        enf.total_npcs_count = 1;
        enf.npcs = {barber_record};
        WriteFile("barber_test.enf", enf);

        pub::EifRecord gold;
        gold.name = "Gold";

        pub::Eif eif;
        eif.rid = {1, 2};
        eif.total_items_count = 1;
        eif.items = {gold};
        WriteFile("barber_test.eif", eif);

        config["ENF"] = "barber_test.enf";
        config["EIF"] = "barber_test.eif";
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove("barber_test.enf");
        std::remove("barber_test.eif");
    }

    PlayingCharacter& GivenCharacterAtBarber()
    {
        GivenMap(EmptyEmf(10, 10));
        server->world->config["BarberBase"] = 0;
        server->world->config["BarberStep"] = 10;
        server->world->config["MaxHairStyle"] = 20;
        server->world->config["MaxHairColor"] = 9;

        eolib::protocol::Coords loc;
        loc.x = 3;
        loc.y = 4;
        barber = new NPC(map, BarberNpcId, loc, NPC::StationarySpawnType, 0, Direction::Down, map->GenerateNPCIndex(), true);
        map->npcs.push_back(barber);
        barber->Spawn();

        PlayingCharacter& entry = GivenCharacter(3, 3);
        entry.character->level = 5;
        entry.character->npc = barber;
        entry.character->npc_type = pub::NpcType::Barber;
        entry.character->npc_session_id = BarberSessionId;
        entry.character->AddItem(1, 100);
        return entry;
    }

    static client::BarberBuyClientPacket Buy(int style, int color, int session_id)
    {
        client::BarberBuyClientPacket packet;
        packet.hair_style = style;
        packet.hair_color = color;
        packet.session_id = session_id;
        return packet;
    }
};

}

TEST_F(BarberTests, Open_Barber_SendsNpcSession)
{
    Character* character = GivenCharacterAtBarber().character;
    character->npc = nullptr;
    character->npc_type = pub::NpcType::Friendly;

    client::BarberOpenClientPacket packet;
    packet.npc_index = barber->index;
    Handlers::Barber_Open(character, packet);

    const auto reply = LastSent<server::BarberOpenServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_NE(0, reply->session_id);
    EXPECT_EQ(character->npc_session_id, reply->session_id);
    EXPECT_EQ(barber, character->npc);
    EXPECT_EQ(pub::NpcType::Barber, character->npc_type);
}

TEST_F(BarberTests, Buy_MatchingSession_ChangesHairAndCharges)
{
    PlayingCharacter& entry = GivenCharacterAtBarber();
    PlayingCharacter& watcher = GivenCharacter(4, 4);

    Handlers::Barber_Buy(entry.character, Buy(7, 3, BarberSessionId));

    EXPECT_EQ(7, entry.character->hairstyle);
    EXPECT_EQ(3, entry.character->haircolor);

    const auto reply = LastSent<server::BarberAgreeServerPacket>(entry);
    ASSERT_TRUE(reply);
    EXPECT_EQ(50, reply->gold_amount);
    EXPECT_EQ(entry.character->PlayerID(), reply->change.player_id);
    const auto* hair = reply->change.AsHair();
    ASSERT_NE(nullptr, hair);
    EXPECT_EQ(7, hair->hair_style);
    EXPECT_EQ(3, hair->hair_color);

    EXPECT_TRUE(LastSent<server::AvatarAgreeServerPacket>(watcher));
}

TEST_F(BarberTests, Buy_WrongSession_Ignored)
{
    PlayingCharacter& entry = GivenCharacterAtBarber();

    EXPECT_CALL(*entry.client, Close(testing::_)).Times(0);
    Handlers::Barber_Buy(entry.character, Buy(7, 3, BarberSessionId + 1));

    EXPECT_FALSE(LastSent<server::BarberAgreeServerPacket>(entry));
    EXPECT_EQ(100, entry.character->HasItem(1));
}

TEST_F(BarberTests, Buy_StyleAboveMax_Ignored)
{
    PlayingCharacter& entry = GivenCharacterAtBarber();

    EXPECT_CALL(*entry.client, Close(testing::_)).Times(0);
    Handlers::Barber_Buy(entry.character, Buy(21, 3, BarberSessionId));

    EXPECT_FALSE(LastSent<server::BarberAgreeServerPacket>(entry));
    EXPECT_EQ(100, entry.character->HasItem(1));
}
