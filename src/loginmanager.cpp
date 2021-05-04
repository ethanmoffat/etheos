
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include <ctime>

#include "util/threadpool.hpp"

#include "loginmanager.hpp"
#include "player.hpp"
#include "world.hpp"

LoginManager::LoginManager(std::shared_ptr<DatabaseFactory> databaseFactory, Config& config, const std::unordered_map<HashFunc, std::shared_ptr<Hasher>>& passwordHashers)
    : _databaseFactory(databaseFactory)
    , _config(config)
    , _passwordHashers(passwordHashers)
    , _processCount(0)
{
}

bool LoginManager::CheckLogin(const std::string& username, util::secure_string&& password)
{
    auto res = this->_databaseFactory->CreateDatabase(this->_config)->Query("SELECT `password`, `password_version` FROM `accounts` WHERE `username` = '$'", username.c_str());

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

    this->_databaseFactory->CreateDatabase(this->_config)->Query("UPDATE `accounts` SET `password` = '$', `password_version` = # WHERE username = '$'",
        password.str().c_str(),
        int(passwordVersion),
        username.c_str());
}

AsyncOperation<AccountCreateInfo, bool>* LoginManager::CreateAccountAsync(EOClient* client)
{
    auto createAccountThreadProc = [this](std::shared_ptr<AccountCreateInfo> accountCreateInfo)
    {
        auto passwordVersion = static_cast<HashFunc>(int(this->_config["PasswordCurrentVersion"]));

        auto password = std::move(accountCreateInfo->password);
        password = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), accountCreateInfo->username, std::move(password)));
        password = std::move(this->_passwordHashers[passwordVersion]->hash(password.str()));

        auto db_res = this->_databaseFactory->CreateDatabase(this->_config)->Query(
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

    return new AsyncOperation<AccountCreateInfo, bool>(client, createAccountThreadProc, true);
}

AsyncOperation<PasswordChangeInfo, bool>* LoginManager::SetPasswordAsync(EOClient* client)
{
    auto setPasswordThreadProc = [this](std::shared_ptr<PasswordChangeInfo> passwordChangeInfo)
    {
        auto oldPassword = std::move(passwordChangeInfo->oldpassword);
        auto newPassword = std::move(passwordChangeInfo->newpassword);

        if (this->CheckLogin(passwordChangeInfo->username, std::move(oldPassword)))
        {
            this->SetPassword(passwordChangeInfo->username, std::move(newPassword));
            return true;
        }

        return false;
    };

    return new AsyncOperation<PasswordChangeInfo, bool>(client, setPasswordThreadProc, true);
}

// This doesn't return AsyncOperation because it doesn't operate within the context of sending a client response
void LoginManager::UpdatePasswordVersionInBackground(AccountCredentials&& accountCredentials)
{
    auto updateThreadProc = [this](const void * state)
    {
        auto updateState = static_cast<const AccountCredentials*>(state);
        auto username = updateState->username;
        auto password = std::move(updateState->password);
        auto hashFunc = updateState->hashFunc;
        delete updateState;

        if (hashFunc != NONE)
        {
            password = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), username, std::move(password)));
            password = std::move(this->_passwordHashers[hashFunc]->hash(std::move(password.str())));

            this->_databaseFactory->CreateDatabase(this->_config)->Query("UPDATE `accounts` SET `password` = '$', `password_version` = # WHERE `username` = '$'",
                password.str().c_str(),
                hashFunc,
                username.c_str());
        }
    };

    auto state = static_cast<void*>(new AccountCredentials(std::move(accountCredentials)));
    util::ThreadPool::Queue(updateThreadProc, state);
}

AsyncOperation<AccountCredentials, LoginReply>* LoginManager::CheckLoginAsync(EOClient* client)
{
    auto loginThreadProc = [this, client](std::shared_ptr<AccountCredentials> updateState)
    {
        auto username = updateState->username;
        auto password = std::move(updateState->password);

        auto database = this->_databaseFactory->CreateDatabase(this->_config);
        Database_Result res = database->Query("SELECT `password`, `password_version` FROM `accounts` WHERE `username` = '$'", username.c_str());

        if (!res.empty())
        {
            HashFunc dbPasswordVersion = static_cast<HashFunc>(res[0]["password_version"].GetInt());
            HashFunc currentPasswordVersion = static_cast<HashFunc>(this->_config["PasswordCurrentVersion"].GetInt());
            std::string dbPasswordHash = std::string(res[0]["password"]);

            // make a copy of the password for input to the salting function
            // original password needs to be preserved for update of password version (if necessary)
            util::secure_string passwordCopy(std::string(password.str()));
            util::secure_string saltedPassword = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), username, std::move(passwordCopy)));
            if (this->_passwordHashers[dbPasswordVersion]->check(saltedPassword.str(), dbPasswordHash))
            {
                if (dbPasswordVersion < currentPasswordVersion)
                {
                    // There is a potential race here:
                    // 1. User logs in
                    // 2. Password version starts update in background thread
                    // 3. User changes password while updating version in background
                    // 4. Password version completes update; overwrites changed password
                    this->UpdatePasswordVersionInBackground(std::move(AccountCredentials { username, std::move(password), currentPasswordVersion }));
                }

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

    auto asyncOp = new AsyncOperation<AccountCredentials, LoginReply>(client, loginThreadProc, LOGIN_OK);
    return asyncOp->OnComplete([this]() { this->_processCount--; });
}
