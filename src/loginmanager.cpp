
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include <ctime>

#include "util/threadpool.hpp"

#include "loginmanager.hpp"
#include "player.hpp"
#include "world.hpp"

LoginManager::LoginManager(Config& config, const std::unordered_map<HashFunc, std::shared_ptr<Hasher>>& passwordHashers)
    : _config(config)
    , _passwordHashers(passwordHashers)
{
}

void LoginManager::CreateAccount(AccountCreateInfo&& accountInfo, std::function<void(void)> successCallback)
{
    auto createAccountThreadProc = [this, successCallback](const void * state)
    {
        auto accountCreateInfo = const_cast<AccountCreateInfo*>(reinterpret_cast<const AccountCreateInfo*>(state));
        auto passwordVersion = static_cast<HashFunc>(int(this->_config["PasswordCurrentVersion"]));
        auto password = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), accountCreateInfo->username, std::move(accountCreateInfo->password)));
        password = std::move(this->_passwordHashers[passwordVersion]->hash(password.str()));

        auto db_res = this->CreateDbConnection()->Query(
            "INSERT INTO `accounts` (`username`, `password`, `fullname`, `location`, `email`, `computer`, `hdid`, `regip`, `created`, `password_version`)"
            " VALUES ('$','$','$','$','$','$',#,'$',#,#)",
            accountCreateInfo->username.c_str(),
            password.str().c_str(),
            accountCreateInfo->fullname.c_str(),
            accountCreateInfo->location.c_str(),
            accountCreateInfo->email.c_str(),
            accountCreateInfo->computer.c_str(),
            accountCreateInfo->hdid,
            static_cast<std::string>(accountCreateInfo->remoteIp).c_str(),
            int(std::time(0)),
            int(passwordVersion));

        if (!db_res.Error())
            successCallback();
    };

    auto state = reinterpret_cast<void*>(new AccountCreateInfo(std::move(accountInfo)));
    util::ThreadPool::Queue(createAccountThreadProc, state);
}

void LoginManager::UpdatePasswordVersion(const std::string& username, util::secure_string&& password, HashFunc hashFunc)
{
    auto updateThreadProc = [this](const void * state)
    {
        auto updateState = reinterpret_cast<const LoginManager::UpdateState*>(state);

        auto username = updateState->username;
        auto updatedPassword = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]),
                                                              username,
                                                              std::move(const_cast<LoginManager::UpdateState*>(updateState)->password)));
        auto hashFunc = updateState->hashFunc;

        if (hashFunc == NONE)
            return;

        updatedPassword = std::move(this->_passwordHashers[hashFunc]->hash(std::move(updatedPassword.str())));

        this->CreateDbConnection()->Query("UPDATE `accounts` SET `password` = '$', `password_version` = # WHERE `username` = '$'",
            updatedPassword.str().c_str(),
            hashFunc,
            username.c_str());

        delete updateState;
    };

    auto state = reinterpret_cast<void*>(new UpdateState { username, std::move(password), hashFunc });
    util::ThreadPool::Queue(updateThreadProc, state);
}

std::unique_ptr<Database> LoginManager::CreateDbConnection()
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

    return std::move(std::unique_ptr<Database>(new Database(engine, dbHost, dbPort, dbUser, dbPass, dbName)));
}