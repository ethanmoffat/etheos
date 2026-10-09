#include "testhelper/playing.hpp"

#include "arena.hpp"
#include "converters.hpp"

#include <eolib/protocol/map/structs.hpp>
#include <eolib/protocol/net/server/packets.hpp>

namespace emf = eolib::protocol::map;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short MapEffectTestServerPort = 38117;
constexpr int MapEffectTestMapId = 22;

class MapEffectTests : public PlayingTest
{
protected:
    MapEffectTests()
        : PlayingTest(MapEffectTestServerPort, MapEffectTestMapId)
    {
    }

    PlayingCharacter& GivenHealthyCharacter(int x, int y)
    {
        PlayingCharacter& entry = GivenCharacter(x, y);
        entry.character->hp = entry.character->maxhp = 100;
        entry.character->tp = entry.character->maxtp = 100;
        return entry;
    }

    // The map owns its arena, the same as one loaded from the arenas file
    Arena* GivenArena(int block)
    {
        map->arena = new Arena(map, 60, block);
        return map->arena;
    }
};

}

TEST_F(MapEffectTests, Quake_SendsStrengthToEveryone)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& near = GivenCharacter(3, 3);
    PlayingCharacter& far = GivenCharacter(35, 35);

    map->Quake(6);

    for (const PlayingCharacter* entry : {&near, &far})
    {
        const auto packet = LastSent<server::EffectUseServerPacket>(*entry);
        ASSERT_TRUE(packet);
        ASSERT_NE(nullptr, packet->AsQuake());
        EXPECT_EQ(6, packet->AsQuake()->quake_strength);
    }
}

TEST_F(MapEffectTests, TimedSpikes_CharacterOnSpikes_TakesDamageAndWatchersSeeIt)
{
    emf::Emf file = EmptyEmf(10, 10);
    AddTileSpec(file, 3, 3, emf::MapTileSpec::TimedSpikes);
    GivenMap(file);
    server->world->config["SpikeDamage"] = 0.2;
    PlayingCharacter& victim = GivenHealthyCharacter(3, 3);
    PlayingCharacter& watcher = GivenHealthyCharacter(4, 4);

    map->TimedSpikes();

    const auto damage = LastSent<server::EffectSpecServerPacket>(victim);
    ASSERT_TRUE(damage);
    ASSERT_NE(nullptr, damage->AsSpikes());
    EXPECT_EQ(20, damage->AsSpikes()->hp_damage);
    EXPECT_EQ(80, damage->AsSpikes()->hp);
    EXPECT_EQ(100, damage->AsSpikes()->max_hp);

    const auto seen = LastSent<server::EffectAdminServerPacket>(watcher);
    ASSERT_TRUE(seen);
    EXPECT_EQ(victim.character->PlayerID(), seen->player_id);
    EXPECT_EQ(80, seen->hp_percentage);
    EXPECT_FALSE(seen->died);
    EXPECT_EQ(20, seen->damage);

    EXPECT_TRUE(LastSent<server::EffectReportServerPacket>(watcher));
    EXPECT_FALSE(LastSent<server::EffectReportServerPacket>(victim));
}

TEST_F(MapEffectTests, TimedDrains_HpDrain_SendsOwnAndNearbyDamage)
{
    emf::Emf file = EmptyEmf(10, 10);
    file.timed_effect = emf::MapTimedEffect::HpDrain;
    GivenMap(file);
    server->world->config["DrainHPDamage"] = 0.2;
    PlayingCharacter& first = GivenHealthyCharacter(3, 3);
    PlayingCharacter& second = GivenHealthyCharacter(4, 4);

    map->TimedDrains();

    const auto packet = LastSent<server::EffectTargetOtherServerPacket>(first);
    ASSERT_TRUE(packet);
    EXPECT_EQ(20, packet->damage);
    EXPECT_EQ(80, packet->hp);
    EXPECT_EQ(100, packet->max_hp);
    ASSERT_EQ(1u, packet->others.size());
    EXPECT_EQ(second.character->PlayerID(), packet->others[0].player_id);
    EXPECT_EQ(80, packet->others[0].hp_percentage);
    EXPECT_EQ(20, packet->others[0].damage);
}

TEST_F(MapEffectTests, TimedDrains_TpDrain_SendsTpDamage)
{
    emf::Emf file = EmptyEmf(10, 10);
    file.timed_effect = emf::MapTimedEffect::TpDrain;
    GivenMap(file);
    server->world->config["DrainTPDamage"] = 0.2;
    PlayingCharacter& drained = GivenHealthyCharacter(3, 3);

    map->TimedDrains();

    const auto packet = LastSent<server::EffectSpecServerPacket>(drained);
    ASSERT_TRUE(packet);
    ASSERT_NE(nullptr, packet->AsTpDrain());
    EXPECT_EQ(20, packet->AsTpDrain()->tp_damage);
    EXPECT_EQ(80, packet->AsTpDrain()->tp);
    EXPECT_EQ(100, packet->AsTpDrain()->max_tp);
}

TEST_F(MapEffectTests, CharacterEffect_NoEcho_SendsOthersInRange)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& source = GivenCharacter(3, 3);
    PlayingCharacter& near = GivenCharacter(4, 4);
    PlayingCharacter& far = GivenCharacter(35, 35);

    source.character->Effect(7, false);

    const auto packet = LastSent<server::EffectPlayerServerPacket>(near);
    ASSERT_TRUE(packet);
    ASSERT_EQ(1u, packet->effects.size());
    EXPECT_EQ(source.character->PlayerID(), packet->effects[0].player_id);
    EXPECT_EQ(7, packet->effects[0].effect_id);
    EXPECT_FALSE(LastSent<server::EffectPlayerServerPacket>(source));
    EXPECT_FALSE(LastSent<server::EffectPlayerServerPacket>(far));
}

TEST_F(MapEffectTests, ArenaSpawn_Full_SendsDrop)
{
    GivenMap(EmptyEmf(10, 10));
    Arena* arena = GivenArena(1);
    arena->spawns.push_back(new Arena_Spawn{Converters::CoordsFrom(3, 3), Converters::CoordsFrom(5, 5)});
    arena->occupants = 1;
    PlayingCharacter& waiting = GivenCharacter(3, 3);

    arena->Spawn();

    EXPECT_TRUE(LastSent<server::ArenaDropServerPacket>(waiting));
    EXPECT_EQ(3, waiting.character->loc.x);
}

TEST_F(MapEffectTests, ArenaSpawn_Open_LaunchesPlayers)
{
    GivenMap(EmptyEmf(10, 10));
    Arena* arena = GivenArena(4);
    arena->spawns.push_back(new Arena_Spawn{Converters::CoordsFrom(3, 3), Converters::CoordsFrom(5, 5)});
    PlayingCharacter& waiting = GivenCharacter(3, 3);

    arena->Spawn();

    const auto packet = LastSent<server::ArenaUseServerPacket>(waiting);
    ASSERT_TRUE(packet);
    EXPECT_EQ(1, packet->players_count);
}

TEST_F(MapEffectTests, ArenaAttack_Kill_SendsKillerAndVictim)
{
    GivenMap(EmptyEmf(10, 10));
    Arena* arena = GivenArena(4);
    arena->occupants = 3;
    PlayingCharacter& killer = GivenCharacter(3, 3);
    PlayingCharacter& victim = GivenCharacter(3, 4);
    killer.character->direction = Direction::Down;
    killer.character->arena = arena;
    victim.character->arena = arena;

    arena->Attack(killer.character, Direction::Down);

    const auto packet = LastSent<server::ArenaSpecServerPacket>(killer);
    ASSERT_TRUE(packet);
    EXPECT_EQ(killer.character->PlayerID(), packet->player_id);
    EXPECT_EQ(Direction::Down, packet->direction);
    EXPECT_EQ(1, packet->kills_count);
    EXPECT_EQ(killer.character->SourceName(), packet->killer_name);
    EXPECT_EQ(victim.character->SourceName(), packet->victim_name);
}

TEST_F(MapEffectTests, ArenaAttack_LastKill_SendsWinner)
{
    GivenMap(EmptyEmf(10, 10));
    Arena* arena = GivenArena(4);
    arena->occupants = 2;
    PlayingCharacter& killer = GivenCharacter(3, 3);
    PlayingCharacter& victim = GivenCharacter(3, 4);
    killer.character->direction = Direction::Down;
    killer.character->arena = arena;
    victim.character->arena = arena;

    arena->Attack(killer.character, Direction::Down);

    const auto packet = LastSent<server::ArenaAcceptServerPacket>(killer);
    ASSERT_TRUE(packet);
    EXPECT_EQ(killer.character->SourceName(), packet->winner_name);
    EXPECT_EQ(1, packet->kills_count);
    EXPECT_EQ(victim.character->SourceName(), packet->victim_name);
}
