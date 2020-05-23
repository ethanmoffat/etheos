#include "hashupdater.hpp"
#include "util.hpp"
#include "world.hpp"

PasswordHashUpdater::PasswordHashUpdater(Config& config, const std::unordered_map<HashFunc, std::shared_ptr<Hasher>>& passwordHashers)
    : _config(config)
    , _passwordHashers(passwordHashers)
    , _database(nullptr)
    , _terminating(false)
{
    this->_updateThread = std::thread([this] { this->updateThreadProc(); });

    auto dbType = util::lowercase(std::string(config["DBType"]));

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

    auto dbHost = std::string(config["DBHost"]);
    auto dbUser = std::string(config["DBUser"]);
    auto dbPass = std::string(config["DBPass"]);
    auto dbName = std::string(config["DBName"]);
    auto dbPort = int(config["DBPort"]);

    this->_database.reset(new Database(engine, dbHost, dbPort, dbUser, dbPass, dbName));
}

PasswordHashUpdater::~PasswordHashUpdater()
{
    this->_terminating = true;

    this->_updateSignal.notify_all();
    this->_updateThread.join();
}

void PasswordHashUpdater::QueueUpdatePassword(const std::string& username, util::secure_string&& password, HashFunc hashFunc)
{
    UpdateState state { username, std::move(password), hashFunc };

    std::lock_guard<std::mutex> queueGuard(this->_updateQueueLock);
    this->_updateQueue.push(std::move(state));

    this->_updateSignal.notify_one();
}

void PasswordHashUpdater::updateThreadProc()
{
    while (!this->_terminating)
    {
        std::unique_lock<std::mutex> lock(this->_updateMutex);
        this->_updateSignal.wait(lock);

        if (this->_terminating)
        {
            break;
        }

        std::string username;
        util::secure_string updatedPassword("");
        HashFunc hashFunc = NONE;

        {
            std::lock_guard<std::mutex> queueGuard(this->_updateQueueLock);

            if (this->_updateQueue.empty())
            {
                continue;
            }

            UpdateState updateState = std::move(this->_updateQueue.front());
            this->_updateQueue.pop();

            username = updateState.username;
            updatedPassword = std::move(Hasher::SaltPassword(std::string(this->_config["PasswordSalt"]), username, std::move(updateState.password)));
            hashFunc = updateState.hashFunc;
        }

        if (hashFunc == NONE)
        {
            continue;
        }

        updatedPassword = std::move(this->_passwordHashers[hashFunc]->hash(std::move(updatedPassword.str())));

        this->_database->Query("UPDATE `accounts` SET `password` = '$', `password_version` = # WHERE `username` = '$'",
            updatedPassword.str().c_str(),
            hashFunc,
            username.c_str());
    }
}
