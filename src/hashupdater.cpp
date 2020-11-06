
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
{
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

        if (hashFunc != NONE)
        {
            updatedPassword = std::move(this->_passwordHashers[hashFunc]->hash(std::move(updatedPassword.str())));

            this->CreateDbConnection()->Query("UPDATE `accounts` SET `password` = '$', `password_version` = # WHERE `username` = '$'",
                updatedPassword.str().c_str(),
                hashFunc,
                username.c_str());
        }

        delete updateState;
    };

    auto state = reinterpret_cast<void*>(new (std::nothrow) UpdateState { username, std::move(password), hashFunc });
    util::ThreadPool::Queue(updateThreadProc, state);
}

PasswordHashUpdater::~PasswordHashUpdater()
{
	// Allow any pending threadpool work to finish on shutdown
	// Prevents access violation if the lambda in PasswordHashUpdater::QueueUpdatePassword outlives this instance
	//
	util::ThreadPool::Shutdown();
}

std::unique_ptr<Database> PasswordHashUpdater::CreateDbConnection()
{
    auto dbType = util::lowercase(std::string(this->_config["DBType"]));

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

    auto dbHost = std::string(this->_config["DBHost"]);
    auto dbUser = std::string(this->_config["DBUser"]);
    auto dbPass = std::string(this->_config["DBPass"]);
    auto dbName = std::string(this->_config["DBName"]);
    auto dbPort = int(this->_config["DBPort"]);

    return std::move(std::unique_ptr<Database>(new (std::nothrow) Database(engine, dbHost, dbPort, dbUser, dbPass, dbName)));
}