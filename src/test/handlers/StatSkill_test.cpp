#include "../testhelper/playing.hpp"

// include the CPP file with the StatSkill functions in it for testing
#include "../../handlers/StatSkill.cpp"

#include "../../npc.hpp"
#include "../../npc_data.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdio>
#include <string>

namespace client = eolib::protocol::net::client;
namespace pub = eolib::protocol::pub;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short StatSkillTestServerPort = 38116;
constexpr int StatSkillTestMapId = 21;
constexpr int TrainerNpcId = 1;
constexpr int TrainerSessionId = 1234;
constexpr int TestSpellId = 1;
constexpr int BaseClassId = 1;
constexpr int SubclassId = 2;

class StatSkillTests : public PlayingTest
{
protected:
    NPC* trainer = nullptr;

    StatSkillTests()
        : PlayingTest(StatSkillTestServerPort, StatSkillTestMapId)
    {
    }

    void ConfigureServer(Config& config) override
    {
        pub::EnfRecord trainer_record;
        trainer_record.name = "Trainer";
        trainer_record.type = pub::NpcType::Trainer;
        trainer_record.hp = 10;

        pub::Enf enf;
        enf.rid = {1, 2};
        enf.total_npcs_count = 1;
        enf.npcs = {trainer_record};
        WriteFile("statskill_test.enf", enf);

        pub::EsfRecord spell;
        spell.name = "Spell";

        pub::Esf esf;
        esf.rid = {1, 2};
        esf.total_skills_count = 1;
        esf.skills = {spell};
        WriteFile("statskill_test.esf", esf);

        pub::EcfRecord base_class;
        base_class.name = "Priest";

        pub::EcfRecord subclass;
        subclass.name = "High Priest";
        subclass.parent_type = BaseClassId;

        pub::Ecf ecf;
        ecf.rid = {1, 2};
        ecf.total_classes_count = 2;
        ecf.classes = {base_class, subclass};
        WriteFile("statskill_test.ecf", ecf);

        config["ENF"] = "statskill_test.enf";
        config["ESF"] = "statskill_test.esf";
        config["ECF"] = "statskill_test.ecf";
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove("statskill_test.enf");
        std::remove("statskill_test.esf");
        std::remove("statskill_test.ecf");
    }

    // A character talking to a trainer that teaches TestSpellId for free, with no requirements
    Character* GivenTrainingCharacter(int class_requirement = 0)
    {
        GivenMap(EmptyEmf(10, 10));

        server::SkillLearn skill;
        skill.id = TestSpellId;
        skill.class_requirement = class_requirement;
        skill.skill_requirements = {0, 0, 0, 0};
        server->world->npc_data[TrainerNpcId]->skill_learn.push_back(skill);
        server->world->npc_data[TrainerNpcId]->skill_name = "Trainer";

        eolib::protocol::Coords loc;
        loc.x = 3;
        loc.y = 4;
        trainer = new NPC(map, TrainerNpcId, loc, NPC::StationarySpawnType, 0, Direction::Down, map->GenerateNPCIndex(), true);
        map->npcs.push_back(trainer);
        trainer->Spawn();

        Character* character = GivenCharacter(3, 3).character;
        character->npc = trainer;
        character->npc_type = pub::NpcType::Trainer;
        character->npc_session_id = TrainerSessionId;
        return character;
    }

    static client::StatSkillTakeClientPacket Take(int session_id)
    {
        client::StatSkillTakeClientPacket packet;
        packet.session_id = session_id;
        packet.spell_id = TestSpellId;
        return packet;
    }
};

}

TEST_F(StatSkillTests, Open_Trainer_SendsSkillsWithNpcSession)
{
    Character* character = GivenTrainingCharacter();
    character->npc = nullptr;

    client::StatSkillOpenClientPacket packet;
    packet.npc_index = trainer->index;
    Handlers::StatSkill_Open(character, packet);

    const auto reply = LastSent<server::StatSkillOpenServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_NE(0, reply->session_id);
    EXPECT_EQ(character->npc_session_id, reply->session_id);
    EXPECT_EQ("Trainer", reply->shop_name);
    ASSERT_EQ(1u, reply->skills.size());
    EXPECT_EQ(TestSpellId, reply->skills[0].id);
    EXPECT_EQ(trainer, character->npc);
}

TEST_F(StatSkillTests, Take_MatchingSession_LearnsSpell)
{
    Character* character = GivenTrainingCharacter();

    Handlers::StatSkill_Take(character, Take(TrainerSessionId));

    EXPECT_TRUE(character->HasSpell(TestSpellId));
    const auto reply = LastSent<server::StatSkillTakeServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_EQ(TestSpellId, reply->spell_id);
}

TEST_F(StatSkillTests, Take_WrongSession_Ignored)
{
    Character* character = GivenTrainingCharacter();

    Handlers::StatSkill_Take(character, Take(TrainerSessionId + 1));

    EXPECT_FALSE(character->HasSpell(TestSpellId));
    EXPECT_TRUE(playing.back()->sent.empty());
}

TEST_F(StatSkillTests, Take_WrongSessionWithEnforceSessionsOff_Ignored)
{
    Character* character = GivenTrainingCharacter();
    server->world->config["EnforceSessions"] = false;

    Handlers::StatSkill_Take(character, Take(TrainerSessionId + 1));

    EXPECT_FALSE(character->HasSpell(TestSpellId));
}

TEST_F(StatSkillTests, Take_WrongClass_RepliesWithRequiredClass)
{
    Character* character = GivenTrainingCharacter(SubclassId);
    character->clas = BaseClassId;

    Handlers::StatSkill_Take(character, Take(TrainerSessionId));

    EXPECT_FALSE(character->HasSpell(TestSpellId));
    const auto reply = LastSent<server::StatSkillReplyServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    ASSERT_NE(nullptr, reply->AsWrongClass());
    EXPECT_EQ(SubclassId, reply->AsWrongClass()->class_id);
}

TEST_F(StatSkillTests, Take_Subclass_LearnsBaseClassSpell)
{
    Character* character = GivenTrainingCharacter(BaseClassId);
    character->clas = SubclassId;

    Handlers::StatSkill_Take(character, Take(TrainerSessionId));

    EXPECT_TRUE(character->HasSpell(TestSpellId));
}

TEST_F(StatSkillTests, Take_UnmetRequirement_RepliesWrongClassWithOwnClass)
{
    Character* character = GivenTrainingCharacter();
    character->clas = BaseClassId;
    server->world->npc_data[TrainerNpcId]->skill_learn.back().level_requirement = 5;

    Handlers::StatSkill_Take(character, Take(TrainerSessionId));

    EXPECT_FALSE(character->HasSpell(TestSpellId));
    const auto reply = LastSent<server::StatSkillReplyServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    ASSERT_NE(nullptr, reply->AsWrongClass());
    EXPECT_EQ(BaseClassId, reply->AsWrongClass()->class_id);
}

TEST_F(StatSkillTests, Remove_WrongSession_Ignored)
{
    Character* character = GivenTrainingCharacter();
    character->AddSpell(TestSpellId);

    client::StatSkillRemoveClientPacket packet;
    packet.session_id = TrainerSessionId + 1;
    packet.spell_id = TestSpellId;
    Handlers::StatSkill_Remove(character, packet);

    EXPECT_TRUE(character->HasSpell(TestSpellId));
    EXPECT_FALSE(LastSent<server::StatSkillRemoveServerPacket>(*playing.back()));
}

TEST_F(StatSkillTests, Remove_MatchingSession_ForgetsSpell)
{
    Character* character = GivenTrainingCharacter();
    character->AddSpell(TestSpellId);

    client::StatSkillRemoveClientPacket packet;
    packet.session_id = TrainerSessionId;
    packet.spell_id = TestSpellId;
    Handlers::StatSkill_Remove(character, packet);

    EXPECT_FALSE(character->HasSpell(TestSpellId));
    const auto reply = LastSent<server::StatSkillRemoveServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_EQ(TestSpellId, reply->spell_id);
}

TEST_F(StatSkillTests, Junk_WrongSession_Ignored)
{
    Character* character = GivenTrainingCharacter();
    character->str = 5;

    client::StatSkillJunkClientPacket packet;
    packet.session_id = TrainerSessionId + 1;
    Handlers::StatSkill_Junk(character, packet);

    EXPECT_EQ(5, character->str);
    EXPECT_FALSE(LastSent<server::StatSkillJunkServerPacket>(*playing.back()));
}

TEST_F(StatSkillTests, Junk_MatchingSession_ResetsStats)
{
    Character* character = GivenTrainingCharacter();
    character->str = 5;

    client::StatSkillJunkClientPacket packet;
    packet.session_id = TrainerSessionId;
    Handlers::StatSkill_Junk(character, packet);

    EXPECT_EQ(0, character->str);
    const auto reply = LastSent<server::StatSkillJunkServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_EQ(character->statpoints, reply->stats.stat_points);
}

TEST_F(StatSkillTests, Add_Stat_SendsUpdatedStats)
{
    Character* character = GivenTrainingCharacter();
    character->statpoints = 2;

    client::StatSkillAddClientPacket::ActionTypeDataStat stat;
    stat.stat_id = client::StatId::Str;
    Handlers::StatSkill_Add(character, client::StatSkillAddClientPacket::ForStat(stat));

    EXPECT_EQ(1, character->str);
    const auto reply = LastSent<server::StatSkillPlayerServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_EQ(1, reply->stat_points);
    EXPECT_EQ(character->display_str, reply->stats.base_stats.str);
}

TEST_F(StatSkillTests, Add_Skill_SendsNewSkillLevel)
{
    Character* character = GivenTrainingCharacter();
    character->AddSpell(TestSpellId);
    character->skillpoints = 2;

    client::StatSkillAddClientPacket::ActionTypeDataSkill skill;
    skill.spell_id = TestSpellId;
    Handlers::StatSkill_Add(character, client::StatSkillAddClientPacket::ForSkill(skill));

    const auto reply = LastSent<server::StatSkillAcceptServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_EQ(1, reply->skill_points);
    EXPECT_EQ(TestSpellId, reply->spell.id);
    EXPECT_EQ(1, reply->spell.level);
}
