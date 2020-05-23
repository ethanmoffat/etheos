#pragma once

#include <condition_variable>
#include <queue>
#include <string>
#include <thread>

#include "hash.hpp"
#include "fwd/database.hpp"
#include "util/secure_string.hpp"

class PasswordHashUpdater
{
public:
    PasswordHashUpdater(Config &config, const std::unordered_map<HashFunc, std::shared_ptr<Hasher>>& passwordHashers);
    ~PasswordHashUpdater();

    void QueueUpdatePassword(const std::string& username, util::secure_string&& password, HashFunc hashFunc);

private:
    Config& _config;

    // Maintain a separate database connection for the background thread
    std::unique_ptr<Database> _database;

    std::unordered_map<HashFunc, std::shared_ptr<Hasher>> _passwordHashers;

    volatile bool _terminating;

    struct UpdateState
    {
        std::string username;
        util::secure_string password;
        HashFunc hashFunc;
    };

    std::thread _updateThread;

    std::mutex _updateMutex;
    std::condition_variable _updateSignal;

    std::mutex _updateQueueLock;
    std::queue<UpdateState> _updateQueue;

    void updateThreadProc();
};