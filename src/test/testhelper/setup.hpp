#pragma once

#include <thread>

#include "config.hpp"
#include "eoserv_config.hpp"

static void CreateConfigWithTestDefaults(Config& config, Config& admin_config)
{
    eoserv_config_validate_config(config);
    eoserv_config_validate_admin(admin_config);

    // these are needed for the test to run
    // test assumes relative to directory {install_dir}/test
    config["ServerLanguage"] = "../lang/en.ini";
    config["EIF"] = "../data/pub/empty.eif";
    config["ENF"] = "../data/pub/empty.enf";
    config["ESF"] = "../data/pub/empty.esf";
    config["ECF"] = "../data/pub/empty.ecf";

    config["DropsFile"] = "../data/drops.ini";
    config["ShopsFile"] = "../data/shops.ini";
    config["ArenasFile"] = "../data/arenas.ini";
    config["FormulasFile"] = "../data/formulas.ini";
    config["HomeFile"] = "../data/home.ini";
    config["SkillsFile"] = "../data/skills.ini";
    config["SpeechFile"] = "../data/speech.ini";

    //turn off SLN
    config["SLN"] = "false";
    // turn off timed save
    config["TimedSave"] = "false";
}

static std::shared_ptr<Database> CreateMockDatabase()
{
    // force SQL server for database mocking stuff
    std::shared_ptr<Database> mockDatabase(new MockDatabase(Database::Engine::SqlServer));

    // set up responses to database queries
    Database_Result banCheckResult;
    std::unordered_map<std::string, util::variant> banCheckColumns;
    banCheckColumns["expires"] = util::variant(-1);
    banCheckResult.push_back(banCheckColumns);

    // no bans by default
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(HasSubstr("FROM bans"), _, _))
        .WillRepeatedly(Return(banCheckResult));

    // no accounts by default
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()),
                RawQuery(HasSubstr("FROM accounts"), _, _))
        .WillRepeatedly(Return(Database_Result()));

    // Suppress gmock "uninteresting method call" warnings in output
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()), Pending()).WillRepeatedly(Return(false));
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()), Escape(_)).WillRepeatedly(Return(""));
    EXPECT_CALL(*dynamic_cast<MockDatabase*>(mockDatabase.get()), Commit()).WillRepeatedly(Return());

    return mockDatabase;
}

static std::shared_ptr<DatabaseFactory> CreateMockDatabaseFactory(std::shared_ptr<Database> database, bool delayInCreate = false)
{
    std::shared_ptr<DatabaseFactory> mockDatabaseFactory(new MockDatabaseFactory);
    EXPECT_CALL(*dynamic_cast<MockDatabaseFactory*>(mockDatabaseFactory.get()), CreateDatabase(_, _))
        .WillRepeatedly(Invoke([database, delayInCreate](Unused, Unused)
            {
                if (delayInCreate)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                return database;
            }));
    return mockDatabaseFactory;
}
