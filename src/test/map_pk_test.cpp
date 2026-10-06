#include "testhelper/playing.hpp"

#include <eolib/data/eo_writer.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace pub = eolib::protocol::pub;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short MapPkTestServerPort = 38112;
constexpr int MapPkTestMapId = 17;
constexpr int TestSpellId = 1;

class MapPkTests : public PlayingTest
{
protected:
    MapPkTests()
        : PlayingTest(MapPkTestServerPort, MapPkTestMapId)
    {
    }

    static std::string EsfFile()
    {
        return "map_pk_test.esf";
    }

    void ConfigureServer(Config& config) override
    {
        pub::EsfRecord record;
        record.name = "Bolt";
        record.type = pub::SkillType::Attack;

        pub::Esf file;
        file.rid = {1, 2};
        file.total_skills_count = 1;
        file.skills = {record};

        eolib::data::EoWriter writer;
        file.Serialize(writer);
        const std::vector<std::uint8_t> content = writer.Data();

        std::ofstream stream(EsfFile(), std::ios::binary);
        stream.write(reinterpret_cast<const char*>(content.data()), content.size());
        stream.close();

        config["ESF"] = EsfFile();
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove(EsfFile().c_str());
    }

    // A character with the stats that combat formulas read
    PlayingCharacter& GivenFighter(int x, int y)
    {
        PlayingCharacter& fighter = GivenCharacter(x, y);
        Character* character = fighter.character;
        character->nointeract = 0;
        character->level = 1;
        character->exp = 0;
        character->hp = character->maxhp = 1000;
        character->tp = character->maxtp = 10;
        character->maxsp = 0;
        character->weight = character->maxweight = 0;
        character->karma = 0;
        character->mindam = character->maxdam = 1;
        character->adj_str = character->adj_intl = character->adj_wis = character->adj_agi = character->adj_con = character->adj_cha = 0;
        character->str = character->intl = character->wis = character->agi = character->con = character->cha = 0;
        character->accuracy = character->evade = character->armor = 0;
        character->bot = false;
        character->usage = 0;
        character->clas = 0;
        character->whispers = true;
        character->goldbank = 0;
        character->statpoints = character->skillpoints = 0;
        return fighter;
    }
};

}

TEST_F(MapPkTests, AttackPK_Hit_AttackerCopyHasNoAttackerId)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& attacker = GivenFighter(2, 2);
    PlayingCharacter& victim = GivenFighter(2, 3);
    PlayingCharacter& observer = GivenFighter(3, 3);

    ASSERT_TRUE(map->AttackPK(attacker.character, Direction::Down));

    const auto attacker_copy = LastSent<server::AvatarReplyServerPacket>(attacker);
    ASSERT_TRUE(attacker_copy);
    EXPECT_EQ(0, attacker_copy->player_id);
    EXPECT_EQ(victim.character->PlayerID(), attacker_copy->victim_id);
    EXPECT_EQ(Direction::Down, attacker_copy->direction);
    EXPECT_FALSE(attacker_copy->dead);

    const auto observer_copy = LastSent<server::AvatarReplyServerPacket>(observer);
    ASSERT_TRUE(observer_copy);
    EXPECT_EQ(attacker.character->PlayerID(), observer_copy->player_id);
    EXPECT_EQ(victim.character->PlayerID(), observer_copy->victim_id);
    EXPECT_EQ(attacker_copy->damage, observer_copy->damage);
    EXPECT_EQ(attacker_copy->hp_percentage, observer_copy->hp_percentage);
}

TEST_F(MapPkTests, SpellAttackPK_PkMap_SendsAvatarAdminToVictimRange)
{
    GivenMap(EmptyEmf(10, 10));
    map->pk = true;
    PlayingCharacter& caster = GivenFighter(2, 2);
    PlayingCharacter& victim = GivenFighter(4, 4);
    caster.character->direction = Direction::Right;

    map->SpellAttackPK(caster.character, victim.character, TestSpellId);

    for (const PlayingCharacter* entry : {&caster, &victim})
    {
        const auto avatar_admin = LastSent<server::AvatarAdminServerPacket>(*entry);
        ASSERT_TRUE(avatar_admin);
        EXPECT_EQ(caster.character->PlayerID(), avatar_admin->caster_id);
        EXPECT_EQ(victim.character->PlayerID(), avatar_admin->victim_id);
        EXPECT_EQ(Direction::Right, avatar_admin->caster_direction);
        EXPECT_EQ(TestSpellId, avatar_admin->spell_id);
        EXPECT_FALSE(avatar_admin->victim_died);
    }
}
