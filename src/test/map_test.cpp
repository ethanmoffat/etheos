#include <gtest/gtest.h>

#include "testhelper/mocks.hpp"
#include "testhelper/setup.hpp"

#include "map.hpp"

#include "console.hpp"
#include "eoserver.hpp"
#include "npc.hpp"
#include "world.hpp"

#include <eolib/data/eo_numeric_limits.hpp>
#include <eolib/data/eo_writer.hpp>
#include <eolib/data/number_encoder.hpp>
#include <eolib/protocol/map/enums.hpp>
#include <eolib/protocol/map/structs.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>

namespace emf = eolib::protocol::map;

namespace
{

constexpr std::size_t RidOffset = 0x03;
constexpr std::size_t TypeOffset = 0x1F;

std::vector<std::uint8_t> MakeMapContent(int rid, std::size_t length)
{
    std::vector<std::uint8_t> content(length, 0x01);
    const auto encoded_rid = eolib::data::NumberEncoder::EncodeNumber(rid);
    content[RidOffset] = encoded_rid[0];
    content[RidOffset + 1] = encoded_rid[1];
    return content;
}

}

GTEST_TEST(MapGlobalPKTests, GlobalPKRid_ReturnsNextValue)
{
    EXPECT_EQ(1, Map::GlobalPKRid(0));
    EXPECT_EQ(12346, Map::GlobalPKRid(12345));
}

GTEST_TEST(MapGlobalPKTests, GlobalPKRid_MaxShort_WrapsToZero)
{
    const int max_short = static_cast<int>(eolib::data::EoNumericLimits::ShortMax) - 1;

    EXPECT_EQ(0, Map::GlobalPKRid(max_short));
}

GTEST_TEST(MapGlobalPKTests, PatchGlobalPK_MapContent_PatchesRidAndType)
{
    const int rid = 12345;
    auto content = MakeMapContent(rid, TypeOffset + 10);
    auto expected = content;

    Map::PatchGlobalPK(content);

    const auto encoded_rid = eolib::data::NumberEncoder::EncodeNumber(Map::GlobalPKRid(rid));
    expected[RidOffset] = encoded_rid[0];
    expected[RidOffset + 1] = encoded_rid[1];
    expected[TypeOffset] = eolib::data::NumberEncoder::EncodeNumber(static_cast<int>(eolib::protocol::map::MapType::Pk))[0];

    EXPECT_EQ(expected, content);
    EXPECT_EQ(Map::GlobalPKRid(rid), eolib::data::NumberEncoder::DecodeNumber(&content[RidOffset], 2));
}

GTEST_TEST(MapGlobalPKTests, PatchGlobalPK_ContentTooShort_IsUnchanged)
{
    auto content = MakeMapContent(12345, TypeOffset);
    const auto expected = content;

    Map::PatchGlobalPK(content);

    EXPECT_EQ(expected, content);
}

namespace
{

constexpr unsigned short MapLoadTestServerPort = 38087;
constexpr int LoadTestMapId = 6;

class MapLoadTests : public testing::Test
{
protected:
    std::unique_ptr<EOServer> server;

    void SetUp() override
    {
        Console::SuppressOutput(true);

        Config config, admin_config;
        CreateConfigWithTestDefaults(config, admin_config);
        config["ChestSpawnRandomization"] = "0";
        server = std::make_unique<EOServer>(IPAddress("127.0.0.1"), MapLoadTestServerPort, CreateMockDatabaseFactory(CreateMockDatabase()), config, admin_config);
        server->world->config["MapDir"] = "./";
    }

    void TearDown() override
    {
        std::remove(MapFile().c_str());
    }

    static std::string MapFile()
    {
        char name[16];
        std::snprintf(name, sizeof name, "%05i.emf", LoadTestMapId);
        return name;
    }

    Map* GivenMap(const std::vector<std::uint8_t>& content)
    {
        std::ofstream file(MapFile(), std::ios::binary);
        file.write(reinterpret_cast<const char*>(content.data()), content.size());
        file.close();

        Map* map = server->world->GetMap(LoadTestMapId);
        map->Reload();
        return map;
    }

    static std::vector<std::uint8_t> Serialize(const emf::Emf& file)
    {
        eolib::data::EoWriter writer;
        file.Serialize(writer);
        return writer.Data();
    }

    // A 10x10 PK map with a chest, a warp and an NPC spawn
    static emf::Emf TestEmf()
    {
        emf::Emf file;
        file.rid = {12345, 678};
        file.type = emf::MapType::Pk;
        file.timed_effect = emf::MapTimedEffect::HpDrain;
        file.width = 9;
        file.height = 9;
        file.can_scroll = true;
        file.relog_x = 4;
        file.relog_y = 5;
        file.graphic_layers.resize(9);

        emf::MapTileSpecRowTile chest;
        chest.x = 3;
        chest.tile_spec = emf::MapTileSpec::Chest;
        emf::MapTileSpecRow spec_row;
        spec_row.y = 2;
        spec_row.tiles = {chest};
        file.tile_spec_rows = {spec_row};

        emf::MapWarpRowTile warp;
        warp.x = 5;
        warp.warp.destination_map = 7;
        warp.warp.destination_coords.x = 1;
        warp.warp.destination_coords.y = 2;
        warp.warp.level_required = 3;
        warp.warp.door = 1;
        emf::MapWarpRow warp_row;
        warp_row.y = 4;
        warp_row.tiles = {warp};
        file.warp_rows = {warp_row};

        emf::MapItem item;
        item.coords.x = 3;
        item.coords.y = 2;
        item.key = 2;
        item.chest_slot = 1;
        item.item_id = 1;
        item.spawn_time = 5;
        item.amount = 100000;
        file.items = {item};

        emf::MapNpc npc;
        npc.coords.x = 6;
        npc.coords.y = 6;
        npc.id = 1;
        npc.spawn_type = 0;
        npc.spawn_time = 30;
        npc.amount = 2;
        file.npcs = {npc};

        return file;
    }
};

}

TEST_F(MapLoadTests, Load_ValidFile_LoadsMap)
{
    const auto content = Serialize(TestEmf());

    Map* map = GivenMap(content);

    ASSERT_TRUE(map->exists);
    EXPECT_EQ((std::array<int, 2>{12345, 678}), map->rid);
    EXPECT_TRUE(map->pk);
    EXPECT_EQ(Map::EffectHPDrain, map->effect);
    EXPECT_EQ(10, map->width);
    EXPECT_EQ(10, map->height);
    EXPECT_TRUE(map->scroll);
    EXPECT_EQ(4, map->relog_x);
    EXPECT_EQ(5, map->relog_y);
    EXPECT_EQ(static_cast<int>(content.size()), map->filesize);

    EXPECT_EQ(emf::MapTileSpec::Chest, map->GetTile(3, 2).tilespec);

    const Map_Warp& warp = map->GetTile(5, 4).warp;
    EXPECT_EQ(7, warp.map);
    EXPECT_EQ(1, warp.loc.x);
    EXPECT_EQ(2, warp.loc.y);
    EXPECT_EQ(3, warp.levelreq);
    EXPECT_EQ(Map_Warp::Door, warp.spec);

    ASSERT_EQ(1u, map->chests.size());
    const Map_Chest& chest = *map->chests.front();
    EXPECT_EQ(2, chest.key);
    EXPECT_EQ(2, chest.slots);
    ASSERT_EQ(1u, chest.spawns.size());
    EXPECT_EQ(2, chest.spawns.front().slot);
    EXPECT_EQ(300.0, chest.spawns.front().time);
    EXPECT_EQ(1, chest.spawns.front().item.id);
    EXPECT_EQ(100000, chest.spawns.front().item.amount);

    ASSERT_EQ(2u, map->npcs.size());
    const NPC* npc = map->npcs.front();
    EXPECT_EQ(1, npc->id);
    EXPECT_EQ(6, npc->spawn_loc.x);
    EXPECT_EQ(6, npc->spawn_loc.y);
    EXPECT_EQ(0, npc->spawn_type);
    EXPECT_EQ(30, npc->respawn_time);
}

TEST_F(MapLoadTests, GetSpec_TileWithoutSpec_ReturnsNoTileSpec)
{
    Map* map = GivenMap(Serialize(TestEmf()));

    EXPECT_EQ(emf::MapTileSpec::Chest, map->GetSpec(3, 2));
    EXPECT_EQ(Map_Tile::NoSpec, map->GetSpec(0, 0));
}

TEST_F(MapLoadTests, GetSpec_OutOfBounds_ReturnsNoTileSpec)
{
    Map* map = GivenMap(Serialize(TestEmf()));

    EXPECT_EQ(Map_Tile::NoSpec, map->GetSpec(10, 0));
    EXPECT_EQ(Map_Tile::NoSpec, map->GetSpec(0, 10));
}

TEST_F(MapLoadTests, Load_StationaryNpcSpawn_DecodesDirectionFromSpawnTime)
{
    emf::Emf file = TestEmf();
    file.npcs.front().spawn_type = NPC::StationarySpawnType;
    file.npcs.front().spawn_time = 0x0106;
    file.npcs.front().amount = 1;

    Map* map = GivenMap(Serialize(file));

    ASSERT_EQ(1u, map->npcs.size());
    const NPC* npc = map->npcs.front();
    EXPECT_EQ(NPC::StationarySpawnType, npc->spawn_type);
    EXPECT_EQ(0, npc->respawn_time);
    EXPECT_EQ(Direction::Up, npc->direction);
    EXPECT_EQ(6, npc->loc.x);
    EXPECT_EQ(6, npc->loc.y);
}

TEST_F(MapLoadTests, Load_OutOfBoundsEntries_AreSkipped)
{
    emf::Emf file = TestEmf();
    file.tile_spec_rows[0].tiles[0].x = 10;
    file.warp_rows[0].y = 10;
    file.npcs[0].coords.x = 10;

    Map* map = GivenMap(Serialize(file));

    ASSERT_TRUE(map->exists);
    EXPECT_TRUE(map->chests.empty());
    EXPECT_FALSE(map->GetTile(5, 4).warp);
    EXPECT_TRUE(map->npcs.empty());
}

TEST_F(MapLoadTests, Load_TruncatedFile_LoadsAvailableData)
{
    auto content = Serialize(TestEmf());
    content.resize(content.size() - 10);

    Map* map = GivenMap(content);

    ASSERT_TRUE(map->exists);
    EXPECT_EQ((std::array<int, 2>{12345, 678}), map->rid);
    EXPECT_EQ(static_cast<int>(content.size()), map->filesize);
}

TEST_F(MapLoadTests, Load_ShortHeader_Fails)
{
    auto content = Serialize(TestEmf());
    content.resize(0x20);

    Map* map = GivenMap(content);

    EXPECT_FALSE(map->exists);
}

TEST_F(MapLoadTests, Reload_SameRid_KeepsLoadedMap)
{
    Map* map = GivenMap(Serialize(TestEmf()));
    ASSERT_TRUE(map->exists);

    emf::Emf changed = TestEmf();
    changed.relog_x = 9;
    map = GivenMap(Serialize(changed));

    EXPECT_EQ(4, map->relog_x);
}

TEST_F(MapLoadTests, ClientRid_GlobalPK_ReplacesFirstHalf)
{
    Map* map = GivenMap(Serialize(TestEmf()));
    server->world->config["GlobalPK"] = true;

    EXPECT_EQ((std::array<int, 2>{Map::GlobalPKRid(12345), 678}), map->ClientRid());
}

TEST_F(MapLoadTests, ClientRid_NotLoaded_IsZero)
{
    Map* map = server->world->GetMap(LoadTestMapId);
    ASSERT_FALSE(map->exists);

    EXPECT_EQ((std::array<int, 2>{0, 0}), map->ClientRid());
}

TEST_F(MapLoadTests, Construct_MissingFile_HasEmptyHeaderFields)
{
    Map* map = server->world->GetMap(LoadTestMapId);
    ASSERT_FALSE(map->exists);

    EXPECT_FALSE(map->pk);
    EXPECT_EQ(Map::EffectNone, map->effect);
    EXPECT_EQ(0, map->filesize);
    EXPECT_EQ(0, map->width);
    EXPECT_EQ(0, map->height);
    EXPECT_FALSE(map->scroll);
    EXPECT_EQ(0, map->relog_x);
    EXPECT_EQ(0, map->relog_y);
}
