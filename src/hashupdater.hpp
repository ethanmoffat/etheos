#pragma once

#include <condition_variable>
#include <list>
#include <string>
#include <thread>

#include "hash.hpp"
#include "fwd/world.hpp"
#include "util/secure_string.hpp"

class PasswordHashUpdater
{
public:
    PasswordHashUpdater(World * world);
    ~PasswordHashUpdater();

    void QueueUpdatePassword(const std::string& username, const util::secure_string& password, HashFunc hashFunc);
    void SignalUpdatePassword(const std::string& username);

private:
    World * world;

    volatile bool _terminating;

    struct UpdateState
    {
        std::string username;
        util::secure_string password;
        HashFunc hashFunc;

        UpdateState(const UpdateState& other);
        UpdateState(const std::string& username, const util::secure_string& password, HashFunc hashFunc);
        UpdateState& operator=(const UpdateState& rhs);
    };

    std::thread updateThread;

    std::mutex updateMutex;
    std::condition_variable updateSignal;

    std::list<std::string> ready_names;

    std::mutex updateQueueLock;
    std::list<UpdateState> updateQueue;

    void updateThreadProc();
};