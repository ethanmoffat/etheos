#include "../testhelper/playing.hpp"

// include the CPP file with the Jukebox functions in it for testing
#include "../../handlers/Jukebox.cpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdio>

namespace client = eolib::protocol::net::client;
namespace pub = eolib::protocol::pub;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short JukeboxTestServerPort = 38133;
constexpr int JukeboxTestMapId = 38;
constexpr int GoldId = 1;
constexpr int InstrumentId = 2;
constexpr int InstrumentGraphic = 49;
constexpr int BardSkillId = 1;
constexpr int JukeboxX = 3;
constexpr int JukeboxY = 2;
constexpr int JukeboxPrice = 25;

class JukeboxTests : public PlayingTest
{
protected:
    JukeboxTests()
        : PlayingTest(JukeboxTestServerPort, JukeboxTestMapId)
    {
    }

    void ConfigureServer(Config& config) override
    {
        pub::EifRecord gold;
        gold.name = "Gold";

        pub::EifRecord instrument;
        instrument.name = "Harp";
        instrument.type = pub::ItemType::Weapon;
        instrument.spec1 = InstrumentGraphic;

        pub::Eif eif;
        eif.rid = {1, 2};
        eif.total_items_count = 2;
        eif.items = {gold, instrument};
        WriteFile("jukebox_test.eif", eif);

        pub::EsfRecord bard;
        bard.name = "Bard";
        bard.type = pub::SkillType::Bard;

        pub::Esf esf;
        esf.rid = {1, 2};
        esf.total_skills_count = 1;
        esf.skills = {bard};
        WriteFile("jukebox_test.esf", esf);

        config["EIF"] = "jukebox_test.eif";
        config["ESF"] = "jukebox_test.esf";
        config["JukeboxPrice"] = std::to_string(JukeboxPrice);
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove("jukebox_test.eif");
        std::remove("jukebox_test.esf");
    }

    void GivenJukeboxMap()
    {
        eolib::protocol::map::Emf file = EmptyEmf(40, 40);
        AddTileSpec(file, JukeboxX, JukeboxY, eolib::protocol::map::MapTileSpec::Jukebox);
        GivenMap(file);
    }

    static client::JukeboxOpenClientPacket Open()
    {
        client::JukeboxOpenClientPacket packet;
        packet.coords.x = JukeboxX;
        packet.coords.y = JukeboxY;
        return packet;
    }

    static client::JukeboxMsgClientPacket Msg(int track_id)
    {
        client::JukeboxMsgClientPacket packet;
        packet.track_id = track_id;
        return packet;
    }

    static client::JukeboxUseClientPacket Use(int note_id)
    {
        client::JukeboxUseClientPacket packet;
        packet.instrument_id = 1;
        packet.note_id = note_id;
        return packet;
    }
};

}

TEST_F(JukeboxTests, Open_InRange_RepliesWithMap)
{
    GivenJukeboxMap();
    PlayingCharacter& user = GivenCharacter(2, 2);

    Handlers::Jukebox_Open(user.character, Open());

    const auto reply = LastSent<server::JukeboxOpenServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(JukeboxTestMapId, reply->map_id);
    EXPECT_TRUE(reply->jukebox_player.empty());
    EXPECT_TRUE(user.character->jukebox_open);
}

TEST_F(JukeboxTests, Open_NoJukeboxInRange_Ignored)
{
    GivenJukeboxMap();
    PlayingCharacter& user = GivenCharacter(20, 20);

    Handlers::Jukebox_Open(user.character, Open());

    EXPECT_TRUE(user.sent.empty());
}

TEST_F(JukeboxTests, Msg_EnoughGold_ChargesAndPlaysTrackForMap)
{
    GivenJukeboxMap();
    PlayingCharacter& user = GivenCharacter(2, 2);
    PlayingCharacter& listener = GivenCharacter(35, 35);
    user.character->AddItem(GoldId, JukeboxPrice + 10);
    Handlers::Jukebox_Open(user.character, Open());

    Handlers::Jukebox_Msg(user.character, Msg(4));

    const auto agree = LastSent<server::JukeboxAgreeServerPacket>(user);
    ASSERT_TRUE(agree);
    EXPECT_EQ(10, agree->gold_amount);
    const auto use = LastSent<server::JukeboxUseServerPacket>(listener);
    ASSERT_TRUE(use);
    EXPECT_EQ(5, use->track_id);
    EXPECT_TRUE(LastSent<server::JukeboxUseServerPacket>(user));
}

TEST_F(JukeboxTests, Msg_NotEnoughGold_RepliesReply)
{
    GivenJukeboxMap();
    PlayingCharacter& user = GivenCharacter(2, 2);
    user.character->AddItem(GoldId, JukeboxPrice - 1);
    Handlers::Jukebox_Open(user.character, Open());

    Handlers::Jukebox_Msg(user.character, Msg(4));

    EXPECT_TRUE(LastSent<server::JukeboxReplyServerPacket>(user));
    EXPECT_FALSE(LastSent<server::JukeboxUseServerPacket>(user));
    EXPECT_EQ(JukeboxPrice - 1, user.character->HasItem(GoldId));
}

TEST_F(JukeboxTests, Msg_TrackPlaying_RepliesReply)
{
    GivenJukeboxMap();
    PlayingCharacter& first = GivenCharacter(2, 2);
    PlayingCharacter& second = GivenCharacter(2, 3);
    first.character->AddItem(GoldId, JukeboxPrice);
    second.character->AddItem(GoldId, JukeboxPrice);
    Handlers::Jukebox_Open(first.character, Open());
    Handlers::Jukebox_Msg(first.character, Msg(4));
    Handlers::Jukebox_Open(second.character, Open());

    Handlers::Jukebox_Msg(second.character, Msg(5));

    EXPECT_EQ(first.character->SourceName(), LastSent<server::JukeboxOpenServerPacket>(second)->jukebox_player);
    EXPECT_TRUE(LastSent<server::JukeboxReplyServerPacket>(second));
    EXPECT_EQ(JukeboxPrice, second.character->HasItem(GoldId));
}

TEST_F(JukeboxTests, Use_BardWithInstrument_SendsMsgToOthersInRange)
{
    GivenJukeboxMap();
    PlayingCharacter& bard = GivenCharacter(2, 2);
    PlayingCharacter& nearby = GivenCharacter(3, 3);
    PlayingCharacter& far = GivenCharacter(35, 35);
    eolib::protocol::net::Spell spell;
    spell.id = BardSkillId;
    spell.level = 1;
    bard.character->spells.push_back(spell);
    bard.character->paperdoll[Character::Weapon] = InstrumentId;

    Handlers::Jukebox_Use(bard.character, Use(7));

    const auto msg = LastSent<server::JukeboxMsgServerPacket>(nearby);
    ASSERT_TRUE(msg);
    EXPECT_EQ(bard.character->PlayerID(), msg->player_id);
    EXPECT_EQ(InstrumentGraphic, msg->instrument_id);
    EXPECT_EQ(7, msg->note_id);
    EXPECT_FALSE(LastSent<server::JukeboxMsgServerPacket>(bard));
    EXPECT_FALSE(LastSent<server::JukeboxMsgServerPacket>(far));
}

TEST_F(JukeboxTests, Use_WithoutBardSkill_Ignored)
{
    GivenJukeboxMap();
    PlayingCharacter& bard = GivenCharacter(2, 2);
    PlayingCharacter& nearby = GivenCharacter(3, 3);
    bard.character->paperdoll[Character::Weapon] = InstrumentId;

    Handlers::Jukebox_Use(bard.character, Use(7));

    EXPECT_FALSE(LastSent<server::JukeboxMsgServerPacket>(nearby));
}

TEST_F(JukeboxTests, PlaySound_SendsMusicPlayer)
{
    GivenJukeboxMap();
    PlayingCharacter& user = GivenCharacter(2, 2);

    user.character->PlaySound(12);

    const auto music = LastSent<server::MusicPlayerServerPacket>(user);
    ASSERT_TRUE(music);
    EXPECT_EQ(12, music->sound_id);
}
