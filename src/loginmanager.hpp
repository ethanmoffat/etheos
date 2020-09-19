
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#pragma once

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

class LoginManager
{
public:
    LoginManager(Config &config, const std::unordered_map<HashFunc, std::shared_ptr<Hasher>>& passwordHashers);

    bool CheckLogin(const std::string& username, util::secure_string&& password);
    void SetPassword(const std::string& username, util::secure_string&& password);

    void CreateAccountAsync(AccountCreateInfo&& accountInfo, std::function<void(void)> successCallback);
    void SetPasswordAsync(PasswordChangeInfo&& passwordChangeInfo, std::function<void(void)> successCallback, std::function<void(void)> failureCallback);
    void UpdatePasswordVersionAsync(const std::string& username, util::secure_string&& password, HashFunc hashFunc);

private:
    // Factory function for creating a database connection on-demand in background threads
    //   based on values in _config
    std::unique_ptr<Database> CreateDbConnection();

    Config& _config;
    std::unordered_map<HashFunc, std::shared_ptr<Hasher>> _passwordHashers;

    struct UpdateState
    {
        std::string username;
        util::secure_string password;
        HashFunc hashFunc;
    };
};