
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
    , _processCount(0)
{
}

bool LoginManager::CheckLogin(const std::string& username, util::secure_string&& password)
{
    auto res = this->CreateDbConnection()->Query("SELECT `password`, `password_version` FROM `accounts` WHERE `username` = '$'", username.c_str());

    if (!res.empty())
    {
        HashFunc dbPasswordVersion = static_cast<HashFunc>(res[0]["password_version"].GetInt());
        std::string dbPasswordHash = std::string(res[0]["password"]);

        password = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), username, std::move(password)));

        return this->_passwordHashers[dbPasswordVersion]->check(password.str(), dbPasswordHash);
    }

    return false;
}

void LoginManager::SetPassword(const std::string& username, util::secure_string&& password)
{
    auto passwordVersion = static_cast<HashFunc>(int(this->_config["PasswordCurrentVersion"]));
    password = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), username, std::move(password)));
    password = std::move(this->_passwordHashers[passwordVersion]->hash(password.str()));

    this->CreateDbConnection()->Query("UPDATE `accounts` SET `password` = '$', `password_version` = # WHERE username = '$'",
        password.str().c_str(),
        int(passwordVersion),
        username.c_str());
}

AsyncOperation* LoginManager::CreateAccountAsync(EOClient* client)
{
    auto createAccountThreadProc = [this](const void * state)
    {
        auto accountCreateInfo = reinterpret_cast<const AccountCreateInfo*>(state);
        auto passwordVersion = static_cast<HashFunc>(int(this->_config["PasswordCurrentVersion"]));

        auto password = std::move(accountCreateInfo->password);
        password = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), accountCreateInfo->username, std::move(password)));
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

        return !db_res.Error();
    };

    return new AsyncOperation(client, createAccountThreadProc, true);
}

AsyncOperation* LoginManager::SetPasswordAsync(EOClient* client)
{
    auto setPasswordThreadProc = [this](const void * state)
    {
        auto passwordChangeInfo = reinterpret_cast<const PasswordChangeInfo*>(state);
        auto oldPassword = std::move(passwordChangeInfo->oldpassword);
        auto newPassword = std::move(passwordChangeInfo->newpassword);

        if (this->CheckLogin(passwordChangeInfo->username, std::move(oldPassword)))
        {
            this->SetPassword(passwordChangeInfo->username, std::move(newPassword));
            return true;
        }

        return false;
    };

    return new AsyncOperation(client, setPasswordThreadProc, true);
}

AsyncOperation* LoginManager::UpdatePasswordVersionAsync(EOClient* client)
{
    auto updateThreadProc = [this](const void * state)
    {
        auto updateState = reinterpret_cast<const AccountCredentials*>(state);
        auto username = updateState->username;
        auto password = std::move(updateState->password);
        auto hashFunc = updateState->hashFunc;
        delete updateState;

        if (hashFunc != NONE)
        {
            password = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), username, std::move(password)));
            password = std::move(this->_passwordHashers[hashFunc]->hash(std::move(password.str())));

            this->CreateDbConnection()->Query("UPDATE `accounts` SET `password` = '$', `password_version` = # WHERE `username` = '$'",
                password.str().c_str(),
                hashFunc,
                username.c_str());
        }

        return 0;
    };

    return new AsyncOperation(client, updateThreadProc);
}

AsyncOperation* LoginManager::CheckLoginAsync(EOClient* client)
{
    auto loginThreadProc = [this, client](const void * state)
    {
        auto updateState = reinterpret_cast<const AccountCredentials*>(state);
        auto username = updateState->username;
        auto password = std::move(updateState->password);

        auto database = this->CreateDbConnection();
        Database_Result res = database->Query("SELECT `password`, `password_version` FROM `accounts` WHERE `username` = '$'", username.c_str());

        if (!res.empty())
        {
            HashFunc dbPasswordVersion = static_cast<HashFunc>(res[0]["password_version"].GetInt());
            HashFunc currentPasswordVersion = static_cast<HashFunc>(this->_config["PasswordCurrentVersion"].GetInt());
            std::string dbPasswordHash = std::string(res[0]["password"]);

            if (dbPasswordVersion < currentPasswordVersion)
            {
                // A copy is made of the password since the background thread needs to have separate ownership of it
                // There is a potential race here:
                // 1. User logs in
                // 2. Password version starts update in background thread
                // 3. User changes password while updating version in background
                // 4. Password version completes update; overwrites changed password
                util::secure_string passwordCopy(std::string(password.str()));
                auto state = reinterpret_cast<void*>(new AccountCredentials { username, std::move(password), currentPasswordVersion });
                this->UpdatePasswordVersionAsync(client)
                    ->OnComplete([state]() { delete state; })
                    ->Execute(state);
            }

            password = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), username, std::move(password)));
            if (this->_passwordHashers[dbPasswordVersion]->check(password.str(), dbPasswordHash))
            {
                return LOGIN_OK;
            }
            else
            {
                return LOGIN_WRONG_USERPASS;
            }
        }
        else
        {
            return LOGIN_WRONG_USER;
        }
    };

    this->_processCount++;

    auto asyncOp = new AsyncOperation(client, loginThreadProc, LOGIN_OK);
    return asyncOp->OnComplete([this]() { this->_processCount--; });
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