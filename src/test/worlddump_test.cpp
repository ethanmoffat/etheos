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
        : dumpFileName("test_dump.bak.json")
    {
        Console::SuppressOutput(true);

        Config config, aConfig;
        CreateConfigWithTestDefaults(config, aConfig);

        database = CreateMockDatabase();
        databaseFactory = CreateMockDatabaseFactory(database);
        za_warudo = std::make_shared<World>(databaseFactory, config, aConfig);
    }

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

    std::shared_ptr<Database> database;
    std::shared_ptr<DatabaseFactory> databaseFactory;
    std::shared_ptr<World> za_warudo;

    std::list<Player> players;
    std::list<Character> characters;

    Player& CreatePlayer(const std::string name)
    {
        Player player(name);
        player.world = za_warudo.get();
        players.push_back(player);
        return players.back();
    }

    Character& CreateCharacter(Player& player, const std::string name)
    {
        Character testChar(za_warudo.get());
        testChar.player = &player;
        testChar.real_name = name;

        characters.push_back(testChar);
        za_warudo->characters.push_back(&characters.back());

        return characters.back();
    }

    nlohmann::json LoadDump()
    {
        nlohmann::json dump;
        std::ifstream inFile(dumpFileName);
        inFile >> dump;
        inFile.close();
        return dump;
    }
};

GTEST_TEST_F(WorldDumpTest, DumpToFile_StoresCharacters)
{
    const std::string ExpectedName = "Dio Brando";
    const std::string ExpectedGuildRank = "Bisexual Vampire";

    auto& player = CreatePlayer(ExpectedName);
    auto& character = CreateCharacter(player, ExpectedName);
    character.guild_rank_string = ExpectedGuildRank;

    za_warudo->DumpToFile(dumpFileName);

    auto dump = LoadDump();
    ASSERT_NE(dump.find("characters"), dump.end());
    ASSERT_EQ(dump["characters"].size(), 1);
    ASSERT_EQ(dump["characters"].front()["account"], ExpectedName);
    ASSERT_EQ(dump["characters"].front()["name"], ExpectedName);
    ASSERT_EQ(dump["characters"].front()["guildrank_str"], ExpectedGuildRank);
}

GTEST_TEST_F(WorldDumpTest, DumpToFile_ExistingCharacter_Overwrites)
{
    const std::string ExistingName = "Jonathan Jostar";
    const std::string ExistingGuildRank = "Gentleman";
    const std::string ExistingTitle = "Hamon Master";
    const std::string OverwriteGuildRank = "Star Platinum";

    nlohmann::json dump;
    dump["characters"] = nlohmann::json::array();
    dump["characters"].push_back(nlohmann::json::object({{"name", ExistingName}, {"guildrank_str", ExistingGuildRank}, {"title", ExistingTitle}}));
    std::ofstream existing(dumpFileName);
    existing << dump;
    existing.close();

    auto& player = CreatePlayer(ExistingName);
    auto& character = CreateCharacter(player, ExistingName);
    character.guild_rank_string = OverwriteGuildRank;
    character.title = ExistingTitle;

    za_warudo->DumpToFile(dumpFileName);

    dump = LoadDump();
    ASSERT_NE(dump.find("characters"), dump.end());
    ASSERT_EQ(dump["characters"].size(), 1);
    ASSERT_EQ(dump["characters"].front()["account"], ExistingName);
    ASSERT_EQ(dump["characters"].front()["name"], ExistingName);
    ASSERT_EQ(dump["characters"].front()["guildrank_str"], OverwriteGuildRank);
    ASSERT_EQ(dump["characters"].front()["title"], ExistingTitle);
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
