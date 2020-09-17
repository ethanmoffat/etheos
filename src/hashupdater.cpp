
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "hashupdater.hpp"
#include "util/threadpool.hpp"
#include "world.hpp"

PasswordHashUpdater::PasswordHashUpdater(Config& config, const std::unordered_map<HashFunc, std::shared_ptr<Hasher>>& passwordHashers)
    : _config(config)
    , _passwordHashers(passwordHashers)
    , _database(nullptr)
{
    auto dbType = util::lowercase(std::string(config["DBType"]));

    Database::Engine engine = Database::MySQL;
    if (!dbType.compare("sqlite"))
    {
        engine = Database::SQLite;
    }
    else if (!dbType.compare("sqlserver"))
    {
        engine = Database::SqlServer;
    }
    else if (!dbType.compare("mysql"))
    {
        engine = Database::MySQL;
    }

    auto dbHost = std::string(config["DBHost"]);
    auto dbUser = std::string(config["DBUser"]);
    auto dbPass = std::string(config["DBPass"]);
    auto dbName = std::string(config["DBName"]);
    auto dbPort = int(config["DBPort"]);

    this->_database.reset(new Database(engine, dbHost, dbPort, dbUser, dbPass, dbName));
}

void PasswordHashUpdater::QueueUpdatePassword(const std::string& username, util::secure_string&& password, HashFunc hashFunc)
{
    auto updateThreadProc = [this](const void * state)
    {
        auto updateState = reinterpret_cast<const PasswordHashUpdater::UpdateState*>(state);

        auto username = updateState->username;
        auto updatedPassword = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]),
                                                              username,
                                                              std::move(const_cast<PasswordHashUpdater::UpdateState*>(updateState)->password)));
        auto hashFunc = updateState->hashFunc;

        if (hashFunc == NONE)
            return;

        updatedPassword = std::move(this->_passwordHashers[hashFunc]->hash(std::move(updatedPassword.str())));

        this->_database->Query("UPDATE `accounts` SET `password` = '$', `password_version` = # WHERE `username` = '$'",
            updatedPassword.str().c_str(),
            hashFunc,
            username.c_str());

        delete updateState;
    };

    auto state = reinterpret_cast<void*>(new UpdateState { username, std::move(password), hashFunc });
    util::ThreadPool::Queue(updateThreadProc, state);
}
