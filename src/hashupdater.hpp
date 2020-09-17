
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
#include "util/secure_string.hpp"
#include "util/semaphore.hpp"

class PasswordHashUpdater
{
public:
    PasswordHashUpdater(Config &config, const std::unordered_map<HashFunc, std::shared_ptr<Hasher>>& passwordHashers);

    void QueueUpdatePassword(const std::string& username, util::secure_string&& password, HashFunc hashFunc);

private:
    Config& _config;

    // Maintain a separate database connection for the background thread
    std::unique_ptr<Database> _database;
    std::unordered_map<HashFunc, std::shared_ptr<Hasher>> _passwordHashers;

    struct UpdateState
    {
        std::string username;
        util::secure_string password;
        HashFunc hashFunc;
    };
};