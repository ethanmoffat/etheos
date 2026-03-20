#include <gtest/gtest.h>
#include <thread>

#include "database.hpp"
#include "console.hpp"

static const char* TestDbPath = ":memory:";

// Subclass that enables thread affinity checking for SQLite connections.
// Production SQLite connections skip setting owner_thread since they are
// intentionally shared across threads. This wrapper sets it manually so
// that CheckThreadAffinity() can be exercised in tests.
class ThreadAffinityDatabase : public Database
{
public:
    void Connect(Database::Engine type, const std::string& host, unsigned short port,
                 const std::string& authtype, const std::string& user,
                 const std::string& pass, const std::string& db = "") override
    {
        Database::Connect(type, host, port, authtype, user, pass, db);
        this->owner_thread = std::this_thread::get_id();
    }
};

class DatabaseThreadAffinityTest : public testing::Test
{
protected:
    void SetUp() override
    {
        Console::SuppressOutput(true);
    }

    std::unique_ptr<ThreadAffinityDatabase> ConnectDatabase()
    {
        auto db = std::make_unique<ThreadAffinityDatabase>();
        db->Connect(Database::SQLite, TestDbPath, 0, "", "", "");
        return db;
    }
};

TEST_F(DatabaseThreadAffinityTest, Query_SameThread_Succeeds)
{
    auto db = ConnectDatabase();
    EXPECT_NO_THROW(db->Query("SELECT 1"));
}

TEST_F(DatabaseThreadAffinityTest, RawQuery_SameThread_Succeeds)
{
    auto db = ConnectDatabase();
    EXPECT_NO_THROW(db->RawQuery("SELECT 1"));
}

TEST_F(DatabaseThreadAffinityTest, Query_DifferentThread_ThrowsThreadViolation)
{
    auto db = ConnectDatabase();

    std::exception_ptr captured;
    std::thread worker([&db, &captured]()
    {
        try
        {
            db->Query("SELECT 1");
        }
        catch (...)
        {
            captured = std::current_exception();
        }
    });
    worker.join();

    ASSERT_TRUE(captured != nullptr);
    EXPECT_THROW(std::rethrow_exception(captured), Database_ThreadViolation);
}

TEST_F(DatabaseThreadAffinityTest, RawQuery_DifferentThread_ThrowsThreadViolation)
{
    auto db = ConnectDatabase();

    std::exception_ptr captured;
    std::thread worker([&db, &captured]()
    {
        try
        {
            db->RawQuery("SELECT 1");
        }
        catch (...)
        {
            captured = std::current_exception();
        }
    });
    worker.join();

    ASSERT_TRUE(captured != nullptr);
    EXPECT_THROW(std::rethrow_exception(captured), Database_ThreadViolation);
}

TEST_F(DatabaseThreadAffinityTest, Query_BeforeConnect_NoThrow)
{
    Database db;

    // owner_thread is unset before Connect(), so no affinity check
    // Query will fail with "Not connected" but should NOT throw Database_ThreadViolation
    std::exception_ptr captured;
    std::thread worker([&db, &captured]()
    {
        try
        {
            db.Query("SELECT 1");
        }
        catch (...)
        {
            captured = std::current_exception();
        }
    });
    worker.join();

    ASSERT_TRUE(captured != nullptr);
    try
    {
        std::rethrow_exception(captured);
    }
    catch (const Database_ThreadViolation&)
    {
        FAIL() << "Should not throw Database_ThreadViolation before Connect()";
    }
    catch (const Database_QueryFailed&)
    {
        // Expected: "Not connected to database."
    }
}

TEST_F(DatabaseThreadAffinityTest, SqliteConnect_SkipsThreadAffinity)
{
    // A plain Database with SQLite should NOT set owner_thread,
    // so queries from any thread should succeed
    auto db = std::make_unique<Database>();
    db->Connect(Database::SQLite, TestDbPath, 0, "", "", "");

    std::exception_ptr captured;
    std::thread worker([&db, &captured]()
    {
        try
        {
            db->Query("SELECT 1");
        }
        catch (...)
        {
            captured = std::current_exception();
        }
    });
    worker.join();

    EXPECT_TRUE(captured == nullptr) << "SQLite connections should be usable from any thread";
}
