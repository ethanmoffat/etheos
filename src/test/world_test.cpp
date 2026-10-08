#include "testhelper/playing.hpp"

#include "npc_data.hpp"

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

constexpr unsigned short WorldTestServerPort = 38145;
constexpr int WorldTestMapId = 49;

class WorldReloadPubTests : public PlayingTest
{
protected:
    WorldReloadPubTests()
        : PlayingTest(WorldTestServerPort, WorldTestMapId)
    {
    }

    static std::string EnfFile()
    {
        return "world_reload_pub_test.enf";
    }

    static void WriteEnf(int npc_count)
    {
        pub::Enf file;
        file.rid = {1, npc_count + 1};
        file.total_npcs_count = npc_count;

        for (int i = 1; i <= npc_count; ++i)
        {
            pub::EnfRecord record;
            record.name = "Npc" + std::to_string(i);
            record.hp = 10;
            file.npcs.push_back(record);
        }

        eolib::data::EoWriter writer;
        file.Serialize(writer);
        const std::vector<std::uint8_t> content = writer.Data();

        std::ofstream stream(EnfFile(), std::ios::binary);
        stream.write(reinterpret_cast<const char*>(content.data()), content.size());
    }

    void ConfigureServer(Config& config) override
    {
        WriteEnf(1);
        config["ENF"] = EnfFile();
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove(EnfFile().c_str());
    }
};

}

TEST_F(WorldReloadPubTests, ReloadPub_EnfGrows_CreatesAndLoadsNewNpcData)
{
    WriteEnf(3);

    server->world->ReloadPub(true);

    ASSERT_EQ(4u, server->world->npc_data.size());
    for (short id = 1; id <= 3; ++id)
    {
        const NPC_Data* data = server->world->GetNpcData(id);
        ASSERT_NE(nullptr, data);
        EXPECT_EQ(id, data->id);
    }
}

TEST_F(WorldReloadPubTests, ReloadPub_EnfShrinks_UnknownIdsFallBackToEmptyNpcData)
{
    WriteEnf(0);

    server->world->ReloadPub(true);

    ASSERT_EQ(1u, server->world->npc_data.size());
    EXPECT_EQ(0, server->world->GetNpcData(1)->id);
}
