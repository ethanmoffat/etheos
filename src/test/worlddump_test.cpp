#include <gtest/gtest.h>
#include <json.hpp>
#include <fstream>

#include "world.hpp"
#include "character.hpp"
#include "player.hpp"
#include "guild.hpp"

#include "testhelper/mocks.hpp"
#include "testhelper/setup.hpp"

#include "console.hpp"

class WorldDumpTest : public testing::Test
{
public:
    WorldDumpTest()
        : dumpFileName("test_dump.bak.json")
    {
        srand(static_cast<unsigned>(time(0)));
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

    Player& CreatePlayer(const std::string& name)
    {
        Player player(name);
        player.world = za_warudo.get();
        players.push_back(player);
        return players.back();
    }

    Character& CreateCharacter(Player& player, const std::string& name)
    {
        Character testChar(za_warudo.get());
        testChar.player = &player;
        testChar.real_name = name;

        characters.push_back(testChar);
        za_warudo->characters.push_back(&characters.back());

        return characters.back();
    }

    std::shared_ptr<Guild> CreateGuild(const std::string& tag, const std::string& name)
    {
        std::shared_ptr<Guild> guild(new Guild(za_warudo->guildmanager));
        guild->tag = tag;
        guild->name = name;

        za_warudo->guildmanager->cache[tag] = guild;

        // make sure guild destructor doesn't do any database stuff
        guild->needs_save = false;
        guild->members.push_back(std::make_shared<Guild_Member>("dummy"));

        return guild;
    }

    nlohmann::json DumpCharacter(nlohmann::json dump, const std::string& name, const std::string& guild_rank_str, const std::string& title = "", int usage = 0)
    {
        if (dump.find("characters") == dump.end())
            dump["characters"] = nlohmann::json::array();

        auto player = CreatePlayer(name);
        Character c(za_warudo.get());
        c.player = &player;
        nlohmann::json character;
        SetCharacterDefaultProperties(character, &c);

        character["account"] = name;
        character["name"] = name;
        character["guildrank_str"] =  guild_rank_str;
        character["title"] = title;
        character["usage"] = usage;
        dump["characters"].push_back(character);

        return dump;
    }

    nlohmann::json DumpGuild(nlohmann::json dump, const std::string& tag, const std::string& name, const std::string& description = "")
    {
        if (dump.find("guilds") == dump.end())
            dump["guilds"] = nlohmann::json::array();

        auto g = CreateGuild(tag, name);
        g->description = description;
        nlohmann::json guild;
        SetGuildDefaultProperties(guild, g);

        dump["guilds"].push_back(guild);

        return dump;
    }

    void DumpItem(nlohmann::json& dump, std::shared_ptr<Map_Item> mapItem, Map* map)
    {
        if (dump.find("mapState") == dump.end())
        {
            dump["mapState"] = nlohmann::json::object();
        }

        if (dump["mapState"].find("items") == dump["mapState"].end())
        {
            dump["mapState"]["items"] = nlohmann::json::array();
        }

        nlohmann::json item = nlohmann::json::object(
        {
            { "mapId", map->id },
            { "x", mapItem->x },
            { "y", mapItem->y },
            { "itemId", mapItem->id },
            { "amount", mapItem->amount },
            { "uid", mapItem->uid }
        });

        dump["mapState"]["items"].push_back(item);
    }

    void DumpChest(nlohmann::json& dump, Map_Chest mapChest, Map_Chest_Item item, Map* map)
    {
        if (dump.find("mapState") == dump.end())
        {
            dump["mapState"] = nlohmann::json::object();
        }

        if (dump["mapState"].find("chests") == dump["mapState"].end())
        {
            dump["mapState"]["chests"] = nlohmann::json::array();
        }

        nlohmann::json chest = nlohmann::json::object(
        {
            { "mapId", map->id },
            { "x", mapChest.x },
            { "y", mapChest.y },
            { "itemId", item.id },
            { "amount", item.amount },
            { "slot", item.slot }
        });

        dump["mapState"]["chests"].push_back(chest);
    }

    void SetCharacterDefaultProperties(nlohmann::json& character, const Character* c)
    {
        character["name"] = c->real_name;
        character["account"] = c->player->username;
        character["title"] = c->title;
        character["home"] = c->home;
        character["fiance"] = c->fiance;
        character["partner"] = c->partner;
        character["class"] = c->clas;
        character["gender"] = c->gender;
        character["race"] = c->race;
        character["hairstyle"] = c->hairstyle;
        character["haircolor"] = c->haircolor;
        character["map"] = c->mapid;
        character["x"] = c->x;
        character["y"] = c->y;
        character["direction"] = c->direction;
        character["admin"] = c->admin;
        character["level"] = c->level;
        character["exp"] = c->exp;
        character["hp"] = c->hp;
        character["tp"] = c->tp;
        character["str"] = c->str;
        character["intl"] = c->intl;
        character["wis"] = c->wis;
        character["agi"] = c->agi;
        character["con"] = c->con;
        character["cha"] = c->cha;
        character["statpoints"] = c->statpoints;
        character["skillpoints"] = c->skillpoints;
        character["karma"] = c->karma;
        character["sitting"] = c->sitting;
        character["hidden"] = c->hidden;
        character["nointeract"] = c->nointeract;
        character["bankmax"] = c->bankmax;
        character["goldbank"] = c->goldbank;
        character["usage"] = c->usage;
        character["inventory"] = ItemSerialize(c->inventory);
        character["bank"] = ItemSerialize(c->bank);
        character["paperdoll"] = DollSerialize(c->paperdoll);
        character["spells"] = SpellSerialize(c->spells);
        character["guild"] = c->guild ? c->guild->tag : "";
        character["guildrank"] = c->guild_rank;
        character["guildrank_str"] = c->guild_rank_string;
        character["quest"] = c->quest_string.empty()
            ? QuestSerialize(c->quests, c->quests_inactive)
            : c->quest_string;
    }

    void SetGuildDefaultProperties(nlohmann::json& guild, std::shared_ptr<Guild> g)
    {
        guild["tag"] = g->tag;
        guild["name"] = g->name;
        guild["description"] = g->description;
        guild["ranks"] = RankSerialize(g->ranks);
        guild["bank"] = g->bank;
    }

    nlohmann::json LoadDump()
    {
        nlohmann::json dump;
        std::ifstream inFile(dumpFileName);
        inFile >> dump;
        inFile.close();
        return dump;
    }

    void WriteDump(nlohmann::json dump)
    {
        std::ofstream existing(dumpFileName);
        existing << dump;
        existing.close();
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
        {
            ASSERT_EQ((*character)["title"], title);
        }
    }

    void AssertGuildProperties(const nlohmann::json& dump, const std::string& tag, const std::string& name)
    {
        ASSERT_NE(dump.find("guilds"), dump.end());
        ASSERT_GE(dump["guilds"].size(), static_cast<size_t>(1));

        auto guild = std::find_if(dump["guilds"].begin(), dump["guilds"].end(),
            [&tag](nlohmann::json check)
            {
                return check.find("tag") != check.end() && check["tag"] == tag;
            });
        ASSERT_NE(guild, dump["guilds"].end());

        ASSERT_EQ((*guild)["tag"], tag);
        ASSERT_EQ((*guild)["name"], name);
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

    void ExpectDatabaseTransaction()
    {
        EXPECT_CALL(*dynamic_cast<MockDatabase*>(database.get()),
                    BeginTransaction())
            .WillOnce(Return(true));
        EXPECT_CALL(*dynamic_cast<MockDatabase*>(database.get()),
                    Commit());
    }

    void GivenExistingCharacter(const std::string& name, int usage)
    {
        std::unordered_map<std::string, util::variant> row;
        row["usage"] = usage;

        Database_Result res;
        res.push_back(row);

        EXPECT_CALL(*dynamic_cast<MockDatabase*>(database.get()),
                    RawQuery(AllOf(StartsWith("SELECT usage FROM characters"), HasSubstr(name)), _, _))
            .WillOnce(Return(res));
    }

    void GivenExistingGuild(const std::string& tag)
    {
        std::unordered_map<std::string, util::variant> row;
        row["count"] = 1;

        Database_Result res;
        res.push_back(row);

        EXPECT_CALL(*dynamic_cast<MockDatabase*>(database.get()),
                    RawQuery(AllOf(StartsWith("SELECT COUNT(1) AS count FROM guilds"), HasSubstr(tag)), _, _))
            .WillOnce(Return(res));
    }

    void ExpectNewCharacter(const std::string& name, const std::string& guild_rank_str)
    {
        EXPECT_CALL(*dynamic_cast<MockDatabase*>(database.get()),
                    RawQuery(StartsWith("SELECT usage FROM characters"), _, _));

        EXPECT_CALL(*dynamic_cast<MockDatabase*>(database.get()),
                    RawQuery(AllOf(StartsWith("INSERT INTO characters"), HasSubstr(name), HasSubstr(guild_rank_str)), _, _));
    }

    void ExpectUpdatedCharacter(const std::string& name, const std::string& guild_rank_str)
    {
        EXPECT_CALL(*dynamic_cast<MockDatabase*>(database.get()),
                    RawQuery(AllOf(StartsWith("UPDATE characters"), HasSubstr(name), HasSubstr(guild_rank_str)), _, _));
    }

    void ExpectNewGuild(const std::string& tag, const std::string& name)
    {
        EXPECT_CALL(*dynamic_cast<MockDatabase*>(database.get()),
                    RawQuery(StartsWith("SELECT COUNT(1) AS count FROM guilds"), _, _));

        EXPECT_CALL(*dynamic_cast<MockDatabase*>(database.get()),
                    RawQuery(AllOf(StartsWith("INSERT INTO guilds"), HasSubstr(tag), HasSubstr(name)), _, _));

        // expect new guilds to be cached into the guild manager
        EXPECT_CALL(*dynamic_cast<MockDatabase*>(database.get()),
                    RawQuery(AllOf(StartsWith("SELECT tag, name, description, created, ranks, bank FROM guilds"), HasSubstr(tag)), _, _));
    }

    void ExpectUpdatedGuild(const std::string& tag, const std::string& description)
    {
        EXPECT_CALL(*dynamic_cast<MockDatabase*>(database.get()),
                    RawQuery(AllOf(StartsWith("UPDATE guilds"), HasSubstr(tag), HasSubstr(description)), _, _));

        // expect new guilds to be cached into the guild manager
        EXPECT_CALL(*dynamic_cast<MockDatabase*>(database.get()),
                    RawQuery(AllOf(StartsWith("SELECT tag, name, description, created, ranks, bank FROM guilds"), HasSubstr(tag)), _, _));
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
    dump = DumpCharacter(dump, ExistingName, ExistingGuildRank, ExistingTitle);
    dump = DumpCharacter(dump, ExistingName2, ExistingGuildRank2, ExistingTitle2);
    WriteDump(dump);

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
    const std::string ExpectedName = "Dio Brando";
    const std::string ExpectedGuildRank = "Bisexual Vampire";

    nlohmann::json dump;
    dump = DumpCharacter(dump, ExpectedName, ExpectedGuildRank);
    WriteDump(dump);

    ExpectDatabaseTransaction();
    ExpectNewCharacter(ExpectedName, ExpectedGuildRank);

    za_warudo->RestoreFromDump(dumpFileName);
}

GTEST_TEST_F(WorldDumpTest, RestoreFromDump_DoesNotOverwrite_CharacterWithNewerData)
{
    const std::string ExpectedName = "Dio Brando";
    const std::string ExpectedGuildRank = "Bisexual Vampire";
    const int ExistingUsage = 100;

    nlohmann::json dump;
    dump = DumpCharacter(dump, ExpectedName, ExpectedGuildRank);
    WriteDump(dump);

    GivenExistingCharacter(ExpectedName, ExistingUsage);
    ExpectDatabaseTransaction();
    // no new character expected

    za_warudo->RestoreFromDump(dumpFileName);
}

GTEST_TEST_F(WorldDumpTest, RestoreFromDump_UpdatesCharacter_WithOlderData)
{
    const std::string ExpectedName = "Dio Brando";
    const std::string ExpectedGuildRank = "Bisexual Vampire";
    const int DumpUsage = 100;
    const int ExistingUsage = 99;

    nlohmann::json dump;
    dump = DumpCharacter(dump, ExpectedName, ExpectedGuildRank, "", DumpUsage);
    WriteDump(dump);

    GivenExistingCharacter(ExpectedName, ExistingUsage);
    ExpectDatabaseTransaction();
    ExpectUpdatedCharacter(ExpectedName, ExpectedGuildRank);

    za_warudo->RestoreFromDump(dumpFileName);
}

GTEST_TEST_F(WorldDumpTest, DumpToFile_StoresGuilds)
{
    const std::string ExpectedName = "stardust crusaders";
    const std::string ExpectedTag = "SDC";

    auto guild = CreateGuild(ExpectedTag, ExpectedName);
    za_warudo->DumpToFile(dumpFileName);

    auto dump = LoadDump();
    AssertGuildProperties(dump, ExpectedTag, ExpectedName);
}

GTEST_TEST_F(WorldDumpTest, DumpToFile_ExistingGuild_Overwrites)
{
    const std::string ExistingName = "Stardust Crusaders", ExistingName2 = "Speedwagon Foundation";
    const std::string ExistingTag = "SDC", ExistingTag2 = "SWF";
    const std::string OverwriteGuildName = "dio bad";

    nlohmann::json dump;
    dump["guilds"] = nlohmann::json::array();
    dump["guilds"].push_back(nlohmann::json::object({{"tag", ExistingTag}, {"name", ExistingName}}));
    dump["guilds"].push_back(nlohmann::json::object({{"tag", ExistingTag2}, {"name", ExistingName2}}));
    std::ofstream existing(dumpFileName);
    existing << dump;
    existing.close();

    auto guild = CreateGuild(ExistingTag, ExistingName);
    guild->name = OverwriteGuildName;

    za_warudo->DumpToFile(dumpFileName);

    dump = LoadDump();
    AssertGuildProperties(dump, ExistingTag, OverwriteGuildName);
    AssertGuildProperties(dump, ExistingTag2, ExistingName2);
}

GTEST_TEST_F(WorldDumpTest, RestoreFromDump_RestoresGuilds)
{
    const std::string ExpectedName = "stardust crusaders";
    const std::string ExpectedTag = "SDC";

    nlohmann::json dump;
    dump = DumpGuild(dump, ExpectedTag, ExpectedName);
    WriteDump(dump);

    ExpectDatabaseTransaction();
    ExpectNewGuild(ExpectedTag, ExpectedName);

    za_warudo->RestoreFromDump(dumpFileName);
}

GTEST_TEST_F(WorldDumpTest, RestoreFromDump_ExistingGuild_UpdatesGuild)
{
    const std::string ExpectedName = "stardust crusaders";
    const std::string ExpectedTag = "SDC";
    const std::string ExpectedDescription = "OH MY GAWD";

    nlohmann::json dump;
    dump = DumpGuild(dump, ExpectedTag, ExpectedName, ExpectedDescription);
    WriteDump(dump);

    GivenExistingGuild(ExpectedTag);
    ExpectDatabaseTransaction();
    ExpectUpdatedGuild(ExpectedTag, ExpectedDescription);

    za_warudo->RestoreFromDump(dumpFileName);
}

GTEST_TEST_F(WorldDumpTest, DumpToFile_StoresMapItems)
{
    std::list<std::pair<Map_Item*, Map*>> items;
    for (const auto& map : za_warudo->maps)
    {
        items.push_back(std::make_pair(map->AddItem(rand() % 480, rand() % 10000, rand() % 25, rand() % 25).get(), map));
    }
    za_warudo->DumpToFile(dumpFileName);

    auto dump = LoadDump();
    for (const auto& item : items)
        AssertMapItemProperties(dump, item.second->id, item.first);
}

GTEST_TEST_F(WorldDumpTest, RestoreFromDump_RestoresMapItems)
{
    std::list<std::pair<std::shared_ptr<Map_Item>, Map*>> items;

    nlohmann::json dump;
    for (const auto& map : za_warudo->maps)
    {
        auto item = map->AddItem(rand() % 480, rand() % 10000, rand() % 25, rand() % 25);
        DumpItem(dump, item, map);
        items.push_back(std::make_pair(item, map));
        map->DelItem(item->uid);
    }
    WriteDump(dump);

    ExpectDatabaseTransaction();

    za_warudo->RestoreFromDump(dumpFileName);

    for (const auto& itemPair : items)
    {
        auto expectedItem = itemPair.first;
        auto restoredItem = itemPair.second->GetItem(expectedItem->uid);

        ASSERT_EQ(expectedItem->uid, restoredItem->uid);
        ASSERT_EQ(expectedItem->id, restoredItem->id);
        ASSERT_EQ(expectedItem->amount, restoredItem->amount);
        ASSERT_EQ(expectedItem->x, restoredItem->x);
        ASSERT_EQ(expectedItem->y, restoredItem->y);
    }
}

GTEST_TEST_F(WorldDumpTest, DumpToFile_StoresMapChests)
{
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
    for (const auto& item : chests)
        AssertChestItemProperties(dump, item.second->id, item.first);
}

GTEST_TEST_F(WorldDumpTest, RestoreFromDump_RestoresMapChests)
{
    std::list<std::pair<std::shared_ptr<Map_Chest_Item>, Map*>> chests;

    nlohmann::json dump;
    for (const auto& map : za_warudo->maps)
    {
        auto chest = std::make_shared<Map_Chest>();
        chest->chestslots = 1;
        chest->maxchest = 10001;
        map->chests.push_back(chest);
        map->chests.front()->AddItem(rand() % 480, rand() % 10000);
        auto item = map->chests.front()->items.front();
        map->chests.front()->DelItem(item.id);

        DumpChest(dump, *chest, item, map);
    }
    WriteDump(dump);

    ExpectDatabaseTransaction();

    za_warudo->RestoreFromDump(dumpFileName);

    for (const auto& chestPair : chests)
    {
        auto& expectedChestItem = chestPair.first;
        auto& expectedChest = chestPair.second->chests.front();

        ASSERT_EQ(expectedChest->items.size(), 1);

        auto& restoredItem = expectedChest->items.front();

        ASSERT_EQ(expectedChestItem->id, restoredItem.id);
        ASSERT_EQ(expectedChestItem->amount, restoredItem.amount);
        ASSERT_EQ(expectedChestItem->slot, restoredItem.slot);
    }
}
