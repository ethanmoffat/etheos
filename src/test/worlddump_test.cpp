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

    void AssertMapItemProperties(const nlohmann::json& dump, int mapId, const Map_Item* mapItem)
    {
        ASSERT_NE(dump.find("mapState"), dump.end());
        ASSERT_NE(dump["mapState"].find("items"), dump["mapState"].end());
        ASSERT_GE(dump["mapState"]["items"].size(), static_cast<size_t>(1));

        auto& items = dump["mapState"]["items"];
        auto itemIter = std::find_if(items.begin(), items.end(),
            [&](nlohmann::json check)
            {
                return check["mapId"].get<int>() == mapId && check["x"].get<int>() == mapItem->x && check["y"].get<int>() == mapItem->y;
            });

        ASSERT_NE(itemIter, items.end());

        auto item = *itemIter;
        ASSERT_EQ(item["itemId"].get<int>(), mapItem->id);
        ASSERT_EQ(item["amount"].get<int>(), mapItem->amount);
        ASSERT_EQ(item["uid"].get<int>(), mapItem->uid);
    }

    void AssertChestItemProperties(const nlohmann::json& dump, int mapId, Map_Chest_Item* chestItem)
    {
        ASSERT_NE(dump.find("mapState"), dump.end());
        ASSERT_NE(dump["mapState"].find("chests"), dump["mapState"].end());
        ASSERT_GE(dump["mapState"]["chests"].size(), static_cast<size_t>(1));

        auto& chests = dump["mapState"]["chests"];
        auto chestIter = std::find_if(chests.begin(), chests.end(),
            [&](nlohmann::json check)
            {
                return check["mapId"].get<int>() == mapId && check["slot"].get<int>() == chestItem->slot;
            });

        ASSERT_NE(chestIter, chests.end());

        auto chest = *chestIter;
        ASSERT_EQ(chest["itemId"].get<int>(), chestItem->id);
        ASSERT_EQ(chest["amount"].get<int>(), chestItem->amount);
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

GTEST_TEST_F(WorldDumpTest, DumpToFile_ExistingGuild_Overwrites)
{
}

GTEST_TEST_F(WorldDumpTest, RestoreFromDump_RestoresGuilds)
{
}

GTEST_TEST_F(WorldDumpTest, DumpToFile_StoresMapItems)
{
    srand(static_cast<unsigned>(time(0)));

    std::list<std::pair<Map_Item*, Map*>> items;
    for (const auto& map : za_warudo->maps)
    {
        items.push_back(std::make_pair(map->AddItem(rand() % 480, rand() % 10000, rand() % 25, rand() % 25).get(), map));
    }
    za_warudo->DumpToFile(dumpFileName);

    auto dump = LoadDump();
    for (const auto item : items)
        AssertMapItemProperties(dump, item.second->id, item.first);
}

GTEST_TEST_F(WorldDumpTest, RestoreFromDump_RestoresMapItems)
{
}

GTEST_TEST_F(WorldDumpTest, DumpToFile_StoresMapChests)
{
    srand(static_cast<unsigned>(time(0)));

    std::list<std::pair<Map_Chest_Item*, Map*>> chests;
    for (const auto& map : za_warudo->maps)
    {
        auto chest = std::make_shared<Map_Chest>();
        chest->chestslots = 1;
        chest->maxchest = 10001;
        map->chests.push_back(chest);
        map->chests.front()->AddItem(rand() % 480, rand() % 10000);
        chests.push_back(std::make_pair(&map->chests.front()->items.front(), map));
    }
    za_warudo->DumpToFile(dumpFileName);

    auto dump = LoadDump();
    for (const auto item : chests)
        AssertChestItemProperties(dump, item.second->id, item.first);
}

GTEST_TEST_F(WorldDumpTest, RestoreFromDump_RestoresMapChests)
{
}
