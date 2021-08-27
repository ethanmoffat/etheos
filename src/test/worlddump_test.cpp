#include <gtest/gtest.h>
#include <json.hpp>
#include <fstream>

#include "world.hpp"
#include "character.hpp"
#include "player.hpp"

#include "testhelper/mocks.hpp"
#include "testhelper/setup.hpp"

#include "console.hpp"

class WorldDumpTest : public testing::Test
{
public:
    WorldDumpTest()
        : dumpFileName("test_dump.bak.json") { }

    ~WorldDumpTest()
    {
        std::ifstream dumpFile(dumpFileName);
        if (dumpFile.is_open())
        {
            dumpFile.close();
            std::remove(dumpFileName.c_str());
        }
    }

protected:
    const std::string dumpFileName;
};

GTEST_TEST_F(WorldDumpTest, DumpToFile_StoresCharacters)
{
    Console::SuppressOutput(true);

    Config config, aConfig;
    CreateConfigWithTestDefaults(config, aConfig);

    auto database = CreateMockDatabase();
    auto databaseFactory = CreateMockDatabaseFactory(database);
    auto za_warudo = std::make_shared<World>(databaseFactory, config, aConfig);

    const std::string ExpectedName = "Dio Brando";
    const std::string ExpectedGuildRank = "Bisexual Vampire";

    Player player(ExpectedName);
    player.world = za_warudo.get();

    Character testChar(za_warudo.get());
    testChar.player = &player;

    testChar.real_name = ExpectedName;
    testChar.guild_rank_string = ExpectedGuildRank;

    za_warudo->characters.push_back(&testChar);
    za_warudo->DumpToFile(dumpFileName);

    nlohmann::json dump;
    std::ifstream inFile(dumpFileName);
    inFile >> dump;
    inFile.close();

    ASSERT_NE(dump.find("characters"), dump.end());
    ASSERT_EQ(dump["characters"].size(), 1);
    ASSERT_EQ(dump["characters"].front()["account"], ExpectedName);
    ASSERT_EQ(dump["characters"].front()["name"], ExpectedName);
    ASSERT_EQ(dump["characters"].front()["guildrank_str"], ExpectedGuildRank);
}

GTEST_TEST_F(WorldDumpTest, RestoreFromDump_RestoresCharacters)
{
}

GTEST_TEST_F(WorldDumpTest, DumpToFile_StoresGuilds)
{

}

GTEST_TEST_F(WorldDumpTest, DumpToFile_StoresMapItems)
{

}

GTEST_TEST_F(WorldDumpTest, DumpToFile_StoresMapChests)
{

}
