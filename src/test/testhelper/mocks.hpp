#pragma once

#include "database.hpp"
#include "eoclient.hpp"
#include "eoserver.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cstdarg>
#include <numeric>

using namespace ::testing;

class MockDatabase : public Database
{
public:
    MockDatabase(Database::Engine type)
    {
        this->engine = type;
    }

    MOCK_METHOD(void, Connect, (Database::Engine type, const std::string& host, unsigned short port, const std::string& user, const std::string& pass, const std::string& db), (override));
    MOCK_METHOD(void, Close, (), (override));
    MOCK_METHOD(Database_Result, RawQuery, (const char* query, bool tx_control, bool prepared), (override));
    MOCK_METHOD(std::string, Escape, (const std::string& esc), (override));
    MOCK_METHOD(void, ExecuteFile, (const std::string& filename), (override));

    MOCK_METHOD(bool, Pending, (), (override, const));
    MOCK_METHOD(bool, BeginTransaction, (), (override));
    MOCK_METHOD(void, Commit, (), (override));
    MOCK_METHOD(void, Rollback, (), (override));

    virtual Database_Result Query(const char *format, ...) override
    {
        std::va_list ap;
        va_start(ap, format);
        auto queryState = this->ParseQueryArgs(format, ap);
        va_end(ap);

        // append all the query parameters so we can verify inputs on the mock
        auto fullQuery = std::accumulate(queryState.second.begin(), queryState.second.end(), queryState.first,
            [](std::string a, std::string b)
            {
                return std::move(a) + ", " + std::move(b);
            });

        return this->RawQuery(fullQuery.c_str(), false, false);
    }
};

class MockClient : public EOClient
{
public:
    MockClient(EOServer * server) : EOClient(server) { }
    MOCK_METHOD(void, Send, (const PacketBuilder &packet), (override));
    MOCK_METHOD(void, Close, (bool force), (override));
    MOCK_METHOD(bool, Connected, (), (const override));
};

class MockDatabaseFactory : public DatabaseFactory
{
public:
    MOCK_METHOD(std::shared_ptr<Database>, CreateDatabase, (Config& config, bool logConnection), (override));
};
