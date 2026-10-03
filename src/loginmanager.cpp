
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include <ctime>

#include "util/threadpool.hpp"

#include "loginmanager.hpp"
#include "config.hpp"
#include "player.hpp"
#include "world.hpp"
#include "extra/seose_compat.hpp"

#include "util.hpp"

namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

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

bool LoginManager::LoginBusy() const
{
    return this->_processCount >= static_cast<int>(this->_config["LoginQueueSize"]);
}

AsyncOperation<client::AccountCreateClientPacket, bool>* LoginManager::CreateAccountAsync(EOClient* client)
{
    auto createAccountThreadProc = [this, remoteIp = client->GetRemoteAddr()](std::shared_ptr<client::AccountCreateClientPacket> packet)
    {
        auto passwordVersion = static_cast<HashFunc>(int(this->_config["PasswordCurrentVersion"]));
        auto username = util::lowercase(packet->username);

        auto password = this->Secure(packet->password);
        password = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), username, std::move(password)));
        password = std::move(this->_passwordHashers[passwordVersion]->hash(password.str()));

        auto db_res = this->_databaseFactory->CreateDatabase(this->_config)->Query(
            "INSERT INTO `accounts` (`username`, `password`, `fullname`, `location`, `email`, `computer`, `hdid`, `regip`, `created`, `password_version`)"
            " VALUES ('$','$','$','$','$','$',#,'$',#,#)",
            username.c_str(),
            password.str().c_str(),
            packet->full_name.c_str(),
            packet->location.c_str(),
            packet->email.c_str(),
            packet->computer.c_str(),
            static_cast<int>(util::to_uint_raw(packet->hdid)),
            static_cast<std::string>(remoteIp).c_str(),
            int(std::time(0)),
            int(passwordVersion));

        return !db_res.Error();
    };

    return new AsyncOperation<client::AccountCreateClientPacket, bool>(client, createAccountThreadProc, true);
}

AsyncOperation<client::AccountAgreeClientPacket, bool>* LoginManager::SetPasswordAsync(EOClient* client)
{
    auto setPasswordThreadProc = [this](std::shared_ptr<client::AccountAgreeClientPacket> packet)
    {
        auto oldPassword = this->Secure(packet->old_password);
        auto newPassword = this->Secure(packet->new_password);

        if (this->CheckLogin(packet->username, std::move(oldPassword)))
        {
            this->SetPassword(packet->username, std::move(newPassword));
            return true;
        }

        return false;
    };

    return new AsyncOperation<client::AccountAgreeClientPacket, bool>(client, setPasswordThreadProc, true);
}

// This doesn't return AsyncOperation because it doesn't operate within the context of sending a client response
void LoginManager::UpdatePasswordVersionInBackground(const std::string& username, util::secure_string&& password, HashFunc hashFunc)
{
    if (hashFunc == NONE)
        return;

    auto updateThreadProc = [this, username, password, hashFunc](const void*) mutable
    {
        password = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), username, std::move(password)));
        password = std::move(this->_passwordHashers[hashFunc]->hash(std::move(password.str())));

        this->_databaseFactory->CreateDatabase(this->_config)->Query("UPDATE `accounts` SET `password` = '$', `password_version` = # WHERE `username` = '$'",
            password.str().c_str(),
            hashFunc,
            username.c_str());
    };

    util::ThreadPool::Queue(updateThreadProc, nullptr);
}

AsyncOperation<client::LoginRequestClientPacket, LoginResult>* LoginManager::CheckLoginAsync(EOClient* client)
{
    auto loginThreadProc = [this](std::shared_ptr<client::LoginRequestClientPacket> packet) -> LoginResult
    {
        auto username = util::lowercase(packet->username);
        auto password = this->Secure(packet->password);

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
            util::secure_string saltedPassword = Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), username, std::move(passwordCopy));
            if (this->_passwordHashers[dbPasswordVersion]->check(saltedPassword.str(), dbPasswordHash))
            {
                if (dbPasswordVersion < currentPasswordVersion)
                {
                    // There is a potential race here:
                    // 1. User logs in
                    // 2. Password version starts update in background thread
                    // 3. User changes password while updating version in background
                    // 4. Password version completes update; overwrites changed password
                    this->UpdatePasswordVersionInBackground(username, std::move(password), currentPasswordVersion);
                }

                return std::nullopt;
            }
            else
            {
                return server::LoginReplyServerPacket::ForWrongUserPassword();
            }
        }
        else
        {
            return server::LoginReplyServerPacket::ForWrongUser();
        }
    };

    this->_processCount++;

    auto asyncOp = new AsyncOperation<client::LoginRequestClientPacket, LoginResult>(client, loginThreadProc);
    return asyncOp->OnComplete([this]() { this->_processCount--; });
}

util::secure_string LoginManager::Secure(std::string& input)
{
    util::secure_string result(std::move(input));

    if (this->_config["SeoseCompat"])
        result = seose_str_hash(result.str(), this->_config["SeoseCompatKey"]);

    return result;
}
