
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

#include "hash.hpp"
#include "fwd/config.hpp"
#include "fwd/database.hpp"
#include "fwd/eoclient.hpp"
#include "fwd/player.hpp"
#include "fwd/world.hpp"
#include "util/secure_string.hpp"
#include "util/semaphore.hpp"
#include "util/async.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

// The reply to send when a login fails, or nothing when it succeeds
using LoginResult = std::optional<eolib::protocol::net::server::LoginReplyServerPacket>;

class LoginManager
{
public:
    LoginManager(std::shared_ptr<DatabaseFactory> databaseFactory, Config& config, const std::unordered_map<HashFunc, std::shared_ptr<Hasher>>& passwordHashers);

    bool CheckLogin(const std::string& username, util::secure_string&& password);
    void SetPassword(const std::string& username, util::secure_string&& password);

    std::shared_ptr<ClientAsyncOperation<eolib::protocol::net::client::AccountCreateClientPacket, bool>> CreateAccountAsync(EOClient* client);
    std::shared_ptr<ClientAsyncOperation<eolib::protocol::net::client::AccountAgreeClientPacket, bool>> SetPasswordAsync(EOClient* client);
    std::shared_ptr<ClientAsyncOperation<eolib::protocol::net::client::LoginRequestClientPacket, LoginResult>> CheckLoginAsync(EOClient* client);

    void UpdatePasswordVersionInBackground(const std::string& username, util::secure_string&& password, HashFunc hashFunc);

    bool LoginBusy() const;

private:
    // Moves a password out of a client packet, wiping the packet's copy, and applies SeoseCompat hashing if enabled
    util::secure_string Secure(std::string& input);

    std::shared_ptr<DatabaseFactory> _databaseFactory;

    Config& _config;
    std::unordered_map<HashFunc, std::shared_ptr<Hasher>> _passwordHashers;

    // Count of the number of concurrent login requests
    volatile std::atomic_int _processCount;
};