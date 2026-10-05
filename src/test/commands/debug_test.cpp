#include "../testhelper/playing.hpp"

// include the CPP file with the debug commands in it for testing
#include "../../commands/debug.cpp"

#include <eolib/data/eo_writer.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace pub = eolib::protocol::pub;

namespace
{

constexpr unsigned short DebugTestServerPort = 38108;
constexpr int DebugTestMapId = 13;
constexpr int TestNpcId = 1;

class SpawnNpcTests : public PlayingTest
{
protected:
    SpawnNpcTests()
        : PlayingTest(DebugTestServerPort, DebugTestMapId)
    {
    }

    static std::string EnfFile()
    {
        return "spawn_npc_test.enf";
    }

    void ConfigureServer(Config& config) override
    {
        pub::EnfRecord record;
        record.name = "Goat";
        record.hp = 10;

        pub::Enf file;
        file.rid = {1, 2};
        file.total_npcs_count = 1;
        file.npcs = {record};

        eolib::data::EoWriter writer;
        file.Serialize(writer);
        const std::vector<std::uint8_t> content = writer.Data();

        std::ofstream stream(EnfFile(), std::ios::binary);
        stream.write(reinterpret_cast<const char*>(content.data()), content.size());
        stream.close();

        config["ENF"] = EnfFile();
        config["SpawnNPCSpeed"] = "0";
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove(EnfFile().c_str());
    }

    Character* GivenSpawner()
    {
        GivenMap(EmptyEmf(10, 10));
        return GivenCharacter(3, 3).character;
    }
};

class SpawnNpcInvalidArgumentTests : public SpawnNpcTests, public testing::WithParamInterface<std::vector<std::string>>
{
};

}

TEST_F(SpawnNpcTests, SpawnNPC_Stationary_SpawnsFacingGivenDirection)
{
    Character* spawner = GivenSpawner();

    Commands::SpawnNPC({std::to_string(TestNpcId), "1", "7", "3"}, spawner);

    ASSERT_EQ(1u, map->npcs.size());
    const NPC* npc = map->npcs.front();
    EXPECT_TRUE(npc->temporary);
    EXPECT_TRUE(npc->alive);
    EXPECT_EQ(NPC::StationarySpawnType, npc->spawn_type);
    EXPECT_EQ(0, npc->respawn_time);
    EXPECT_EQ(Direction::Right, npc->direction);
    EXPECT_EQ(spawner->loc, npc->loc);
}

TEST_F(SpawnNpcTests, SpawnNPC_DefaultSpeed_SpawnsMovingNpcs)
{
    Character* spawner = GivenSpawner();

    Commands::SpawnNPC({std::to_string(TestNpcId), "2"}, spawner);

    ASSERT_EQ(2u, map->npcs.size());
    for (const NPC* npc : map->npcs)
    {
        EXPECT_TRUE(npc->temporary);
        EXPECT_TRUE(npc->alive);
        EXPECT_EQ(0, npc->spawn_type);
        EXPECT_EQ(0, npc->respawn_time);
        EXPECT_EQ(spawner->loc, npc->spawn_loc);
    }
}

TEST_F(SpawnNpcTests, SpawnNPC_UnknownNpc_IsIgnored)
{
    Character* spawner = GivenSpawner();

    Commands::SpawnNPC({std::to_string(TestNpcId + 1)}, spawner);

    EXPECT_TRUE(map->npcs.empty());
}

TEST_P(SpawnNpcInvalidArgumentTests, SpawnNPC_InvalidArgument_IsIgnored)
{
    Character* spawner = GivenSpawner();

    Commands::SpawnNPC(GetParam(), spawner);

    EXPECT_TRUE(map->npcs.empty());
}

INSTANTIATE_TEST_SUITE_P(Arguments, SpawnNpcInvalidArgumentTests, testing::Values(
    std::vector<std::string>{"1", "1", "-1"},
    std::vector<std::string>{"1", "1", "8"},
    std::vector<std::string>{"1", "1", "7", "-1"},
    std::vector<std::string>{"1", "1", "7", "4"}));
