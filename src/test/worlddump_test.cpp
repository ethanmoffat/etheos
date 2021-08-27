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

    void AssertCharacterProperties(const nlohmann::json& dump, const std::string& name, const std::string& guild_rank_str, const std::string& title = "")
    {
        ASSERT_NE(dump.find("characters"), dump.end());
        ASSERT_GE(dump["characters"].size(), static_cast<size_t>(1));

        auto character = std::find_if(dump["characters"].begin(), dump["characters"].end(),
            [&name](nlohmann::json check)
            {
                return check.find("name") != check.end() && check["name"] == name;
            });
        ASSERT_NE(character, dump["characters"].end());

        ASSERT_EQ((*character)["account"], name);
        ASSERT_EQ((*character)["name"], name);
        ASSERT_EQ((*character)["guildrank_str"], guild_rank_str);

        if (!title.empty())
            ASSERT_EQ((*character)["title"], title);
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
    AssertCharacterProperties(dump, ExpectedName, ExpectedGuildRank);
}

GTEST_TEST_F(WorldDumpTest, DumpToFile_ExistingCharacter_Overwrites)
{
    const std::string ExistingName = "Jonathan Jostar", ExistingName2 = "Robert E. O. Speedwagon";
    const std::string ExistingGuildRank = "Gentleman", ExistingGuildRank2 = "Thug";
    const std::string ExistingTitle = "Hamon Master", ExistingTitle2 = "Hype man";
    const std::string OverwriteGuildRank = "Star Platinum";

    nlohmann::json dump;
    dump["characters"] = nlohmann::json::array();
    dump["characters"].push_back(nlohmann::json::object({{"account", ExistingName}, {"name", ExistingName}, {"guildrank_str", ExistingGuildRank}, {"title", ExistingTitle}}));
    dump["characters"].push_back(nlohmann::json::object({{"account", ExistingName2}, {"name", ExistingName2}, {"guildrank_str", ExistingGuildRank2}, {"title", ExistingTitle2}}));
    std::ofstream existing(dumpFileName);
    existing << dump;
    existing.close();

    auto& player = CreatePlayer(ExistingName);
    auto& character = CreateCharacter(player, ExistingName);
    character.guild_rank_string = OverwriteGuildRank;
    character.title = ExistingTitle;

    za_warudo->DumpToFile(dumpFileName);

    dump = LoadDump();
    AssertCharacterProperties(dump, ExistingName, OverwriteGuildRank, ExistingTitle);
    AssertCharacterProperties(dump, ExistingName2, ExistingGuildRank2, ExistingTitle2);
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
