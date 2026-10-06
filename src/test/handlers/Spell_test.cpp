#include "../testhelper/playing.hpp"

// include the CPP file with the Spell functions in it for testing
#include "../../handlers/Spell.cpp"

#include "../../party.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdio>
#include <memory>
#include <string>

namespace client = eolib::protocol::net::client;
namespace pub = eolib::protocol::pub;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short SpellTestServerPort = 38115;
constexpr int SpellTestMapId = 20;
constexpr int HealSpellId = 1;
constexpr int HealAmount = 10;

class SpellTests : public PlayingTest
{
protected:
    SpellTests()
        : PlayingTest(SpellTestServerPort, SpellTestMapId)
    {
    }

    static std::string EsfFile()
    {
        return "spell_test.esf";
    }

    void ConfigureServer(Config& config) override
    {
        pub::EsfRecord heal;
        heal.name = "Heal";
        heal.type = pub::SkillType::Heal;
        heal.cast_time = 1;
        heal.hp_heal = HealAmount;

        pub::Esf file;
        file.rid = {1, 2};
        file.total_skills_count = 1;
        file.skills = {heal};
        WriteFile(EsfFile(), file);

        config["ESF"] = EsfFile();
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove(EsfFile().c_str());
    }

    PlayingCharacter& GivenHurtCharacter(int x, int y)
    {
        PlayingCharacter& entry = GivenCharacter(x, y);
        entry.character->maxhp = 100;
        entry.character->hp = 50;
        entry.character->maxtp = 100;
        entry.character->tp = 100;
        return entry;
    }
};

}

TEST_F(SpellTests, Request_KnownSpell_SendsChantToOthers)
{
    GivenMap(EmptyEmf(20, 20));
    PlayingCharacter& caster = GivenCharacter(3, 3);
    PlayingCharacter& watcher = GivenCharacter(4, 4);
    caster.character->AddSpell(HealSpellId);

    client::SpellRequestClientPacket packet;
    packet.spell_id = HealSpellId;
    Handlers::Spell_Request(caster.character, packet);
    caster.character->CancelSpell();

    const auto chant = LastSent<server::SpellRequestServerPacket>(watcher);
    ASSERT_TRUE(chant);
    EXPECT_EQ(caster.character->PlayerID(), chant->player_id);
    EXPECT_EQ(HealSpellId, chant->spell_id);
    EXPECT_FALSE(LastSent<server::SpellRequestServerPacket>(caster));
}

TEST_F(SpellTests, Request_UnknownSpell_SendsNothing)
{
    GivenMap(EmptyEmf(20, 20));
    PlayingCharacter& caster = GivenCharacter(3, 3);
    PlayingCharacter& watcher = GivenCharacter(4, 4);

    client::SpellRequestClientPacket packet;
    packet.spell_id = HealSpellId;
    Handlers::Spell_Request(caster.character, packet);

    EXPECT_FALSE(caster.character->spell_event);
    EXPECT_TRUE(watcher.sent.empty());
}

TEST_F(SpellTests, SpellSelf_OnlyCasterGetsHpAndTp)
{
    GivenMap(EmptyEmf(20, 20));
    PlayingCharacter& caster = GivenHurtCharacter(3, 3);
    PlayingCharacter& watcher = GivenCharacter(4, 4);

    map->SpellSelf(caster.character, HealSpellId);

    const auto caster_packet = LastSent<server::SpellTargetSelfServerPacket>(caster);
    ASSERT_TRUE(caster_packet);
    EXPECT_EQ(HealAmount, caster_packet->spell_heal_hp);
    EXPECT_EQ(60, caster_packet->hp_percentage);
    EXPECT_EQ(60, caster_packet->hp);
    EXPECT_EQ(100, caster_packet->tp);

    const auto watcher_packet = LastSent<server::SpellTargetSelfServerPacket>(watcher);
    ASSERT_TRUE(watcher_packet);
    EXPECT_EQ(caster.character->PlayerID(), watcher_packet->player_id);
    EXPECT_FALSE(watcher_packet->hp);
    EXPECT_FALSE(watcher_packet->tp);
}

TEST_F(SpellTests, SpellAttackPK_Heal_OnlyVictimGetsHp)
{
    GivenMap(EmptyEmf(20, 20));
    PlayingCharacter& caster = GivenHurtCharacter(3, 3);
    PlayingCharacter& victim = GivenHurtCharacter(4, 4);
    PlayingCharacter& watcher = GivenCharacter(5, 5);

    map->SpellAttackPK(caster.character, victim.character, HealSpellId);

    const auto victim_packet = LastSent<server::SpellTargetOtherServerPacket>(victim);
    ASSERT_TRUE(victim_packet);
    EXPECT_EQ(victim.character->PlayerID(), victim_packet->victim_id);
    EXPECT_EQ(caster.character->PlayerID(), victim_packet->caster_id);
    EXPECT_EQ(client::SpellTargetType::Player, victim_packet->target_type);
    EXPECT_EQ(victim.character->hp, victim_packet->hp);

    const auto watcher_packet = LastSent<server::SpellTargetOtherServerPacket>(watcher);
    ASSERT_TRUE(watcher_packet);
    EXPECT_FALSE(watcher_packet->hp);

    const auto recover = LastSent<server::RecoverPlayerServerPacket>(caster);
    ASSERT_TRUE(recover);
    EXPECT_EQ(caster.character->tp, recover->tp);
}

TEST_F(SpellTests, SpellGroup_SendsEachPartyMember)
{
    GivenMap(EmptyEmf(20, 20));
    PlayingCharacter& caster = GivenHurtCharacter(3, 3);
    PlayingCharacter& member = GivenHurtCharacter(4, 4);
    auto party = std::make_unique<Party>(server->world, caster.character, member.character);

    map->SpellGroup(caster.character, HealSpellId);

    const auto packet = LastSent<server::SpellTargetGroupServerPacket>(member);
    ASSERT_TRUE(packet);
    EXPECT_EQ(HealSpellId, packet->spell_id);
    EXPECT_EQ(caster.character->PlayerID(), packet->caster_id);
    ASSERT_EQ(2u, packet->players.size());
    EXPECT_EQ(caster.character->PlayerID(), packet->players[0].player_id);
    EXPECT_EQ(60, packet->players[0].hp);
    EXPECT_EQ(member.character->PlayerID(), packet->players[1].player_id);
    EXPECT_EQ(60, packet->players[1].hp);
}
