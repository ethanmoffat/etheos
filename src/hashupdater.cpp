#include "hashupdater.hpp"
#include "world.hpp"

PasswordHashUpdater::PasswordHashUpdater(World * world)
    : world(world)
    , _terminating(false)
    , updateThread([this] { this->updateThreadProc(); })
{
}

PasswordHashUpdater::~PasswordHashUpdater()
{
    this->_terminating = true;

    this->updateSignal.notify_all();
    this->updateThread.join();
}

void PasswordHashUpdater::QueueUpdatePassword(const std::string& username, const util::secure_string& password, HashFunc hashFunc)
{
    UpdateState state(username, password, hashFunc);

    std::lock_guard<std::mutex> queueGuard(this->updateQueueLock);
    this->updateQueue.push_back(state);
}

void PasswordHashUpdater::SignalUpdatePassword(const std::string& username)
{
    {
        std::lock_guard<std::mutex> queueGuard(this->updateQueueLock);
        this->ready_names.push_back(username);
    }

    this->updateSignal.notify_one();
}

PasswordHashUpdater::UpdateState::UpdateState(const std::string& username, const util::secure_string& password, HashFunc hashFunc)
    : username(username)
    , password(std::move(password))
    , hashFunc(hashFunc)
{
}

PasswordHashUpdater::UpdateState::UpdateState(const PasswordHashUpdater::UpdateState& other)
    : username(other.username)
    , password(std::move(other.password))
    , hashFunc(other.hashFunc)
{
}

PasswordHashUpdater::UpdateState& PasswordHashUpdater::UpdateState::operator=(const PasswordHashUpdater::UpdateState& rhs)
{
    if (this != &rhs)
    {
        this->username = rhs.username;
        this->password = rhs.password;
        this->hashFunc = rhs.hashFunc;
    }

    return *this;
}

void PasswordHashUpdater::updateThreadProc()
{
    while (!this->_terminating)
    {
        std::unique_lock<std::mutex> lock(this->updateMutex);
        this->updateSignal.wait(lock);

        if (this->_terminating)
        {
            break;
        }

        std::list<UpdateState> updateStates;

        {
            std::lock_guard<std::mutex> queueGuard(this->updateQueueLock);

            if (this->updateQueue.empty())
            {
                this->ready_names.clear();
                continue;
            }

            for (const auto& readyName : this->ready_names)
            {
                auto updateStateIter = std::find_if(
                    this->updateQueue.begin(),
                    this->updateQueue.end(),
                    [&readyName](UpdateState& s) { return s.username == readyName; });

                if (updateStateIter != this->updateQueue.end())
                {
                    updateStates.push_back(*updateStateIter);
                    this->updateQueue.erase(updateStateIter);
                }
            }

            this->ready_names.clear();
        }

        for (auto& updateState : updateStates)
        {
            util::secure_string updatedPassword = std::move(this->world->HashPassword(updateState.username, std::move(updateState.password), false));

            this->world->db.Query("UPDATE `accounts` SET `password` = '$', `password_version` = # WHERE `username` = '$'",
                updatedPassword.str().c_str(),
                updateState.username,
                updateState.hashFunc);
        }
    }
}