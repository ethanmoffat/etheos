#include "testhelper/playing.hpp"

#include "npc.hpp"

#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdio>
#include <string>

namespace pub = eolib::protocol::pub;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short NpcCombatTestServerPort = 38114;
constexpr int NpcCombatTestMapId = 19;
constexpr int TestNpcId = 1;
constexpr int TestNpcHp = 100;
constexpr int TestNpcExperience = 300;
constexpr int TestSpellId = 5;

class NpcCombatTests : public PlayingTest
{
protected:
    NpcCombatTests()
        : PlayingTest(NpcCombatTestServerPort, NpcCombatTestMapId)
    {
    }

    static std::string EnfFile()
    {
        return "npc_combat_test.enf";
    }

    void ConfigureServer(Config& config) override
    {
        pub::EnfRecord record;
        record.name = "Goat";
        record.type = pub::NpcType::Passive;
        record.hp = TestNpcHp;
        record.experience = TestNpcExperience;

        pub::Enf file;
        file.rid = {1, 2};
        file.total_npcs_count = 1;
        file.npcs = {record};

        WriteFile(EnfFile(), file);

        config["ENF"] = EnfFile();
        config["ShareMode"] = "2";
        config["ExpRate"] = "1.0";
    }

    // Characters in these tests never log out, which is what removes them from NPC damage lists
    void TearDown() override
    {
        for (NPC* npc : map->npcs)
            npc->damagelist.clear();

        for (const auto& entry : playing)
            entry->character->unregister_npc.clear();

        PlayingTest::TearDown();
        std::remove(EnfFile().c_str());
    }

    NPC* GivenNpc(int x, int y)
    {
        eolib::protocol::Coords loc;
        loc.x = x;
        loc.y = y;

        NPC* npc = new NPC(map, TestNpcId, loc, NPC::StationarySpawnType, 0, Direction::Down, map->GenerateNPCIndex(), true);
        map->npcs.push_back(npc);
        npc->Spawn();
        return npc;
    }
};

}

TEST_F(NpcCombatTests, Damage_NotKilled_OnlyAttackerGetsKillStealProtection)
{
    GivenMap(EmptyEmf(20, 20));
    PlayingCharacter& attacker = GivenCharacter(3, 3);
    PlayingCharacter& bystander = GivenCharacter(4, 4);
    NPC* npc = GivenNpc(3, 4);

    npc->Damage(attacker.character, 10);

    const auto attacker_reply = LastSent<server::NpcReplyServerPacket>(attacker);
    ASSERT_TRUE(attacker_reply);
    EXPECT_EQ(attacker.character->PlayerID(), attacker_reply->player_id);
    EXPECT_EQ(npc->index, attacker_reply->npc_index);
    EXPECT_EQ(10, attacker_reply->damage);
    EXPECT_EQ(90, attacker_reply->hp_percentage);
    EXPECT_EQ(server::NpcKillStealProtectionState::Unprotected, attacker_reply->kill_steal_protection);

    const auto bystander_reply = LastSent<server::NpcReplyServerPacket>(bystander);
    ASSERT_TRUE(bystander_reply);
    EXPECT_EQ(10, bystander_reply->damage);
    EXPECT_FALSE(bystander_reply->kill_steal_protection);
}

TEST_F(NpcCombatTests, Damage_BySpell_OnlyCasterGetsTp)
{
    GivenMap(EmptyEmf(20, 20));
    PlayingCharacter& caster = GivenCharacter(3, 3);
    PlayingCharacter& bystander = GivenCharacter(4, 4);
    caster.character->tp = 33;
    NPC* npc = GivenNpc(3, 4);

    npc->Damage(caster.character, 10, TestSpellId);

    const auto caster_reply = LastSent<server::CastReplyServerPacket>(caster);
    ASSERT_TRUE(caster_reply);
    EXPECT_EQ(TestSpellId, caster_reply->spell_id);
    EXPECT_EQ(33, caster_reply->caster_tp);
    EXPECT_EQ(server::NpcKillStealProtectionState::Unprotected, caster_reply->kill_steal_protection);

    const auto bystander_reply = LastSent<server::CastReplyServerPacket>(bystander);
    ASSERT_TRUE(bystander_reply);
    EXPECT_FALSE(bystander_reply->caster_tp);
    EXPECT_FALSE(bystander_reply->kill_steal_protection);
}

TEST_F(NpcCombatTests, Damage_Killed_OnlyKillerGetsExperience)
{
    GivenMap(EmptyEmf(20, 20));
    PlayingCharacter& killer = GivenCharacter(3, 3);
    PlayingCharacter& bystander = GivenCharacter(4, 4);
    killer.character->level = 50;
    killer.character->exp = server->world->exp_table[50];
    NPC* npc = GivenNpc(3, 4);
    const int npc_index = npc->index;

    npc->Damage(killer.character, TestNpcHp);

    const auto killer_spec = LastSent<server::NpcSpecServerPacket>(killer);
    ASSERT_TRUE(killer_spec);
    EXPECT_EQ(killer.character->PlayerID(), killer_spec->npc_killed_data.killer_id);
    EXPECT_EQ(npc_index, killer_spec->npc_killed_data.npc_index);
    EXPECT_EQ(TestNpcHp, killer_spec->npc_killed_data.damage);
    EXPECT_EQ(killer.character->exp, killer_spec->experience);

    const auto bystander_spec = LastSent<server::NpcSpecServerPacket>(bystander);
    ASSERT_TRUE(bystander_spec);
    EXPECT_EQ(npc_index, bystander_spec->npc_killed_data.npc_index);
    EXPECT_FALSE(bystander_spec->experience);
    EXPECT_FALSE(LastSent<server::RecoverReplyServerPacket>(bystander));
}

TEST_F(NpcCombatTests, Damage_KilledWithLevelUp_KillerGetsNpcAccept)
{
    GivenMap(EmptyEmf(20, 20));
    PlayingCharacter& killer = GivenCharacter(3, 3);
    killer.character->level = 0;
    killer.character->exp = 0;
    NPC* npc = GivenNpc(3, 4);

    npc->Damage(killer.character, TestNpcHp);

    const auto accept = LastSent<server::NpcAcceptServerPacket>(killer);
    ASSERT_TRUE(accept);
    EXPECT_EQ(TestNpcExperience, accept->experience);
    ASSERT_TRUE(accept->level_up);
    EXPECT_EQ(1, accept->level_up->level);
    EXPECT_EQ(killer.character->statpoints, accept->level_up->stat_points);
    EXPECT_EQ(killer.character->skillpoints, accept->level_up->skill_points);
    EXPECT_EQ(killer.character->maxhp, accept->level_up->max_hp);
    EXPECT_FALSE(LastSent<server::NpcSpecServerPacket>(killer));
}

TEST_F(NpcCombatTests, Damage_KilledWithLevelUp_NearbyGetsShortNpcAccept)
{
    GivenMap(EmptyEmf(20, 20));
    PlayingCharacter& killer = GivenCharacter(3, 3);
    PlayingCharacter& bystander = GivenCharacter(4, 4);
    killer.character->level = 0;
    killer.character->exp = 0;
    NPC* npc = GivenNpc(3, 4);

    npc->Damage(killer.character, TestNpcHp);

    const auto accept = LastSent<server::NpcAcceptServerPacket>(bystander);
    ASSERT_TRUE(accept);
    EXPECT_EQ(killer.character->PlayerID(), accept->npc_killed_data.killer_id);
    EXPECT_FALSE(accept->experience);
    EXPECT_FALSE(accept->level_up);
    EXPECT_FALSE(LastSent<server::NpcSpecServerPacket>(bystander));
    EXPECT_FALSE(LastSent<server::ItemAcceptServerPacket>(bystander));
}

TEST_F(NpcCombatTests, Damage_KilledBySpellWithLevelUp_NearbyGetsShortCastAccept)
{
    GivenMap(EmptyEmf(20, 20));
    PlayingCharacter& killer = GivenCharacter(3, 3);
    PlayingCharacter& bystander = GivenCharacter(4, 4);
    killer.character->level = 0;
    killer.character->exp = 0;
    NPC* npc = GivenNpc(3, 4);

    npc->Damage(killer.character, TestNpcHp, TestSpellId);

    const auto killer_accept = LastSent<server::CastAcceptServerPacket>(killer);
    ASSERT_TRUE(killer_accept);
    EXPECT_TRUE(killer_accept->level_up);

    const auto accept = LastSent<server::CastAcceptServerPacket>(bystander);
    ASSERT_TRUE(accept);
    EXPECT_EQ(TestSpellId, accept->spell_id);
    EXPECT_FALSE(accept->caster_tp);
    EXPECT_FALSE(accept->experience);
    EXPECT_FALSE(accept->level_up);
    EXPECT_FALSE(LastSent<server::CastSpecServerPacket>(bystander));
}

TEST_F(NpcCombatTests, Damage_KilledBySpell_KillerGetsCasterTpAndExperience)
{
    GivenMap(EmptyEmf(20, 20));
    PlayingCharacter& killer = GivenCharacter(3, 3);
    PlayingCharacter& bystander = GivenCharacter(4, 4);
    killer.character->level = 50;
    killer.character->exp = server->world->exp_table[50];
    killer.character->tp = 33;
    NPC* npc = GivenNpc(3, 4);

    npc->Damage(killer.character, TestNpcHp, TestSpellId);

    const auto killer_spec = LastSent<server::CastSpecServerPacket>(killer);
    ASSERT_TRUE(killer_spec);
    EXPECT_EQ(TestSpellId, killer_spec->spell_id);
    EXPECT_EQ(33, killer_spec->caster_tp);
    EXPECT_EQ(killer.character->exp, killer_spec->experience);

    const auto bystander_spec = LastSent<server::CastSpecServerPacket>(bystander);
    ASSERT_TRUE(bystander_spec);
    EXPECT_FALSE(bystander_spec->caster_tp);
    EXPECT_FALSE(bystander_spec->experience);
}

TEST_F(NpcCombatTests, Damage_KilledWithSharedExperience_OtherAttackerGetsRecoverPackets)
{
    GivenMap(EmptyEmf(20, 20));
    PlayingCharacter& killer = GivenCharacter(3, 3);
    PlayingCharacter& helper = GivenCharacter(4, 4);
    killer.character->level = 50;
    killer.character->exp = server->world->exp_table[50];
    helper.character->level = 0;
    helper.character->exp = 0;
    NPC* npc = GivenNpc(3, 4);

    npc->Damage(helper.character, TestNpcHp / 2);
    npc->Damage(killer.character, TestNpcHp);

    const auto helper_spec = LastSent<server::NpcSpecServerPacket>(helper);
    ASSERT_TRUE(helper_spec);
    EXPECT_FALSE(helper_spec->experience);

    const auto stats = LastSent<server::RecoverTargetGroupServerPacket>(helper);
    ASSERT_TRUE(stats);
    EXPECT_EQ(helper.character->statpoints, stats->stat_points);
    EXPECT_EQ(helper.character->maxhp, stats->max_hp);

    const auto reply = LastSent<server::RecoverReplyServerPacket>(helper);
    ASSERT_TRUE(reply);
    EXPECT_EQ(TestNpcExperience / 2, reply->experience);
    EXPECT_EQ(1, reply->level_up);
    EXPECT_EQ(helper.character->statpoints, reply->stat_points);
}

TEST_F(NpcCombatTests, Damage_SharedExperienceLevelUp_NearbyGetsItemAccept)
{
    GivenMap(EmptyEmf(20, 20));
    PlayingCharacter& killer = GivenCharacter(3, 3);
    PlayingCharacter& helper = GivenCharacter(4, 4);
    killer.character->level = 50;
    killer.character->exp = server->world->exp_table[50];
    helper.character->level = 0;
    helper.character->exp = 0;
    NPC* npc = GivenNpc(3, 4);

    npc->Damage(helper.character, TestNpcHp / 2);
    npc->Damage(killer.character, TestNpcHp);

    const auto item_accept = LastSent<server::ItemAcceptServerPacket>(killer);
    ASSERT_TRUE(item_accept);
    EXPECT_EQ(helper.character->PlayerID(), item_accept->player_id);
    EXPECT_FALSE(LastSent<server::ItemAcceptServerPacket>(helper));
    EXPECT_TRUE(LastSent<server::NpcSpecServerPacket>(killer));
    EXPECT_FALSE(LastSent<server::NpcAcceptServerPacket>(helper));
}

TEST_F(NpcCombatTests, Attack_OnlyTargetGetsHpAndTp)
{
    GivenMap(EmptyEmf(20, 20));
    PlayingCharacter& target = GivenCharacter(3, 3);
    PlayingCharacter& bystander = GivenCharacter(4, 4);
    target.character->hp = target.character->maxhp = 100;
    NPC* npc = GivenNpc(3, 4);

    npc->Attack(target.character);

    const auto target_packet = LastSent<server::NpcPlayerServerPacket>(target);
    ASSERT_TRUE(target_packet);
    ASSERT_EQ(1u, target_packet->attacks.size());
    EXPECT_EQ(npc->index, target_packet->attacks[0].npc_index);
    EXPECT_EQ(target.character->PlayerID(), target_packet->attacks[0].player_id);
    EXPECT_EQ(target.character->hp, target_packet->hp);
    EXPECT_EQ(target.character->tp, target_packet->tp);

    const auto bystander_packet = LastSent<server::NpcPlayerServerPacket>(bystander);
    ASSERT_TRUE(bystander_packet);
    ASSERT_EQ(1u, bystander_packet->attacks.size());
    EXPECT_FALSE(bystander_packet->hp);
    EXPECT_FALSE(bystander_packet->tp);
}

TEST_F(NpcCombatTests, RemoveFromView_WalksNpcAwayAndRemovesIt)
{
    GivenMap(EmptyEmf(20, 20));
    PlayingCharacter& viewer = GivenCharacter(3, 3);
    NPC* npc = GivenNpc(3, 4);

    npc->RemoveFromView(viewer.character);

    const auto walk = LastSent<server::NpcPlayerServerPacket>(viewer);
    ASSERT_TRUE(walk);
    ASSERT_EQ(1u, walk->positions.size());
    EXPECT_EQ(npc->index, walk->positions[0].npc_index);
    EXPECT_EQ(252, walk->positions[0].coords.x);
    EXPECT_EQ(252, walk->positions[0].coords.y);

    const auto remove = LastSent<server::NpcSpecServerPacket>(viewer);
    ASSERT_TRUE(remove);
    EXPECT_EQ(npc->index, remove->npc_killed_data.npc_index);
}
