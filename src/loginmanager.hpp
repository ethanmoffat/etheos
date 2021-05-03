
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

#include "hash.hpp"
#include "fwd/config.hpp"
#include "fwd/database.hpp"
#include "fwd/player.hpp"
#include "fwd/world.hpp"
#include "util/secure_string.hpp"
#include "util/semaphore.hpp"
#include "util/async.hpp"

class LoginManager
{
public:
    LoginManager(std::shared_ptr<DatabaseFactory> databaseFactory, Config& config, const std::unordered_map<HashFunc, std::shared_ptr<Hasher>>& passwordHashers);

    bool CheckLogin(const std::string& username, util::secure_string&& password);
    void SetPassword(const std::string& username, util::secure_string&& password);

    AsyncOperation<AccountCreateInfo, bool>* CreateAccountAsync(EOClient* client);
    AsyncOperation<PasswordChangeInfo, bool>* SetPasswordAsync(EOClient* client);
    AsyncOperation<AccountCredentials, LoginReply>* CheckLoginAsync(EOClient* client);

    void UpdatePasswordVersionInBackground(AccountCredentials&& accountCredentials);

    bool LoginBusy() const { return this->_processCount >= static_cast<int>(this->_config["LoginQueueSize"]); };

private:
    std::shared_ptr<DatabaseFactory> _databaseFactory;

    Config& _config;
    std::unordered_map<HashFunc, std::shared_ptr<Hasher>> _passwordHashers;

    // Count of the number of concurrent login requests
    volatile std::atomic_int _processCount;
};