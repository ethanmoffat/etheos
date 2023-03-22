
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifdef CLANG_MODULES_WORKAROUND
#include <ctime>
#include <csignal>
#endif // CLANG_MODULES_WORKAROUND

#include "database.hpp"

#include "config.hpp"
#include "console.hpp"
#include "util.hpp"
#include "util/variant.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <list>
#include <string>
#include <unordered_map>

#include "database_impl.hpp"

#ifndef ER_LOCK_WAIT_TIMEOUT
#define ER_LOCK_WAIT_TIMEOUT 1205
#endif

#ifndef DATABASE_MYSQL
#ifndef DATABASE_SQLITE
#ifndef DATABASE_SQLSERVER
#error At least one database driver must be selected
#endif // DATABASE_SQLSERVER
#endif // DATABASE_SQLITE
#endif // DATABASE_MYSQL

#ifdef DATABASE_SQLSERVER
#define SQLSERVER_SUCCEEDED(x) (x == SQL_SUCCESS || x == SQL_SUCCESS_WITH_INFO)
#endif

struct Database::impl_
{
	union
	{
#ifdef DATABASE_MYSQL
		MYSQL *mysql_handle;
#endif // DATABASE_MYSQL
#ifdef DATABASE_SQLITE
		sqlite3 *sqlite_handle;
#endif // DATABASE_SQLITE
#ifdef DATABASE_SQLSERVER
		SQLHENV hEnv;
		SQLHDBC hConn;
		HSTMT hstmt;
#endif //DATABASE_SQLSERVER
	};
};

#ifdef DATABASE_SQLSERVER
void HandleSqlServerError(SQLSMALLINT handleType, SQLHANDLE handle, SQLRETURN code, void (*consoleFunc)(const char*, ...), std::list<int>* errorCodes = nullptr)
{
	SQLSMALLINT iRec = 0;
	SQLINTEGER iError;
	SQLCHAR messageBuff[1000];
	SQLCHAR stateBuff[SQL_SQLSTATE_SIZE+1];

	if (code == SQL_INVALID_HANDLE)
	{
		fwprintf(stderr, L"Invalid handle!\n");
		return;
	}

	while (SQLGetDiagRec(handleType,
							handle,
							++iRec,
							stateBuff,
							&iError,
							messageBuff,
							(SQLSMALLINT)1000,
							(SQLSMALLINT*)NULL) == SQL_SUCCESS)
	{
		// Hide data truncated..
		if (!strncmp((const char *)stateBuff, "01004", 5))
			continue;

		consoleFunc("[%5.5s] %s (%d)\n", stateBuff, messageBuff, iError);

		if (errorCodes != nullptr)
		{
			errorCodes->push_back(iError);
		}
	}
}
#endif

#ifdef DATABASE_SQLITE
static int sqlite_callback(void *data, int num, char *fields[], char *columns[])
{
	std::unordered_map<std::string, util::variant> result;
	std::string column;
	util::variant field;
	int i;

	for (i = 0; i < num; ++i)
	{
		if (columns[i] == NULL)
		{
			column = "";
		}
		else
		{
			column = columns[i];
		}

		if (fields[i] == NULL)
		{
			field = "";
		}
		else
		{
			field = fields[i];
		}

		result.insert(result.begin(), make_pair(column, field));
	}

	static_cast<Database *>(data)->callbackdata.push_back(result);
	return 0;
}
#endif

int Database_Result::AffectedRows()
{
	return this->affected_rows;
}

bool Database_Result::Error()
{
	return this->error;
}

std::shared_ptr<Database> DatabaseFactory::CreateDatabase(Config& config, bool logConnection)
{
	auto dbType = util::lowercase(std::string(config["DBType"]));
	auto dbHost = std::string(config["DBHost"]);
	auto dbUser = std::string(config["DBUser"]);
	auto dbPass = std::string(config["DBPass"]);
	auto dbName = std::string(config["DBName"]);
	auto dbPort = int(config["DBPort"]);

	auto dbPassFilePath = std::string(config["DBPassFile"]);
	if (dbPassFilePath.size() > 0)
	{
		if (logConnection)
		{
			Console::Out("Using DB password from file %s", dbPassFilePath.c_str());
		}

		std::ifstream dbPassFile(dbPassFilePath);
		if (!dbPassFile.bad())
		{
			dbPassFile >> dbPass;
		}
		else if (logConnection)
		{
			Console::Wrn("DB password file could not be opened. Using existing DBPass config value instead.");
		}

		dbPassFile.close();
	}

	std::string dbdesc;
	std::string engineStr;
	Database::Engine engine = Database::MySQL;
	if (!dbType.compare("sqlite"))
	{
		engine = Database::SQLite;
		engineStr = "SQLite";
		dbdesc = engineStr + ": " + dbHost;
	}
	else
	{
		if (!dbType.compare("sqlserver"))
		{
			engine = Database::SqlServer;
			engineStr = "SqlServer";
		}
		else if (!dbType.compare("mysql"))
		{
			engine = Database::MySQL;
			engineStr = "MySQL";
		}

		dbdesc = engineStr + ": " + dbUser + "@" + dbHost;

		if (dbPort != 0 &&
			((dbPort != 3306 && engine == Database::MySQL) ||
			(dbPort != 1433 && engine == Database::SqlServer)))
		{
			dbdesc += ":" + util::to_string(dbPort);
		}

		dbdesc += "/" + dbName;
	}

	if (logConnection)
		Console::Out("Connecting to database (%s)...", dbdesc.c_str());

	try
	{
		if (engine == Database::SQLite)
		{
			if (!this->_sqliteConnection)
			{
				// for thread safety, SQLite access is serialized (SQLite calls use an internal mutex to serialize access)
				// a shared connection is used to ensure that no concurrent access to the database file occurs across etheos threadpool threads
				this->_sqliteConnection = std::move(std::make_shared<Database>(engine, dbHost, dbPort, dbUser, dbPass, dbName));
			}

			return this->_sqliteConnection;
		}
		else
		{
			return std::make_shared<Database>(engine, dbHost, dbPort, dbUser, dbPass, dbName);
		}
	}
	catch (Database_OpenFailed& ex)
	{
		const auto& errors = ex.getErrorCode();
		// https://docs.oracle.com/cd/E19078-01/mysql/mysql-refman-5.0/error-handling.html#error_er_bad_db_error
		auto missingMySqlDatabase = engine == Database::MySQL && std::find(errors.begin(), errors.end(), 1049) == errors.end();
		// https://docs.microsoft.com/en-us/sql/relational-databases/errors-events/database-engine-events-and-errors?view=sql-server-ver15 (search for 4060)
		auto missingSqlServerDatabase = engine == Database::SqlServer && std::find(errors.begin(), errors.end(), 4060) == errors.end();

		if (!static_cast<bool>(config["AutoCreateDatabase"]) || engine == Database::SQLite || missingMySqlDatabase || missingSqlServerDatabase)
		{
			throw;
		}

		Console::Wrn("Database '%s' does not exist. Attempting to create for engine '%s'", dbName.c_str(), engineStr.c_str());

		Database createDbConn(engine, dbHost, dbPort, dbUser, dbPass);
		createDbConn.RawQuery(std::string(std::string("CREATE DATABASE ") + dbName).c_str());
		createDbConn.RawQuery(std::string(std::string("USE ") + dbName).c_str());
		createDbConn.Close();
	}

	return std::shared_ptr<Database>(new Database(engine, dbHost, dbPort, dbUser, dbPass, dbName));
}

Database::Bulk_Query_Context::Bulk_Query_Context(Database& db)
	: db(db)
	, pending(false)
{
	pending = db.BeginTransaction();
}

bool Database::Bulk_Query_Context::Pending() const
{
	return pending;
}

void Database::Bulk_Query_Context::RawQuery(const std::string& query)
{
	db.RawQuery(query.c_str());
}

void Database::Bulk_Query_Context::Commit()
{
	if (pending)
		db.Commit();

	pending = false;
}

void Database::Bulk_Query_Context::Rollback()
{
	if (pending)
		db.Rollback();

	pending = false;
}

Database::Bulk_Query_Context::~Bulk_Query_Context()
{
	if (pending)
		db.Rollback();
}

Database::Database()
	: impl(new impl_)
	, connected(false)
	, engine(Engine(0))
	, in_transaction(false)
{ }

Database::Database(Database::Engine type, const std::string& host, unsigned short port, const std::string& user, const std::string& pass, const std::string& db, bool connectnow)
	: impl(new impl_)
	, connected(false)
	, in_transaction(false)
{
	if (connectnow)
	{
		this->Connect(type, host, port, user, pass, db);
	}
	else
	{
		this->engine = type;
		this->host = host;
		this->user = user;
		this->pass = pass;
		this->port = port;
		this->db = db;
	}
}

void Database::Connect(Database::Engine type, const std::string& host, unsigned short port, const std::string& user, const std::string& pass, const std::string& db)
{
	this->engine = type;
	this->host = host;
	this->user = user;
	this->pass = pass;
	this->port = port;
	this->db = db;

	if (this->connected)
	{
		return;
	}

	switch (type)
	{
#ifdef DATABASE_MYSQL
		case MySQL:
			if ((this->impl->mysql_handle = mysql_init(0)) == 0)
			{
				throw Database_OpenFailed(mysql_error(this->impl->mysql_handle));
			}

// Linux uses the ABI version number as part of the shared library name
#ifdef WIN32
			if (mysql_get_client_version() != MYSQL_VERSION_ID)
			{
				unsigned int client_version = mysql_get_client_version();

				Console::Err("MySQL client library version mismatch! Please recompile EOSERV with the correct MySQL client library.");
				Console::Err("  Expected version: %i.%i.%i", (MYSQL_VERSION_ID) / 10000, ((MYSQL_VERSION_ID) / 100) % 100, (MYSQL_VERSION_ID) % 100);
				Console::Err("  Library version:  %i.%i.%i", (client_version) / 10000, ((client_version) / 100) % 100, (client_version) % 100);
				Console::Err("Make sure EOSERV is using the correct version of libmariadb.dll");
				throw Database_OpenFailed("MySQL client library version mismatch");
			}
#endif

			if (mysql_real_connect(this->impl->mysql_handle, host.c_str(), user.c_str(), pass.c_str(), 0, this->port, 0, 0) != this->impl->mysql_handle)
			{
				throw Database_OpenFailed(mysql_error(this->impl->mysql_handle));
			}

// This check isn't really neccessary for EOSERV
#if 0
			if ((mysql_get_server_version(this->impl->mysql_handle) / 100) != (MYSQL_VERSION_ID) / 100)
			{
				Console::Wrn("MySQL server version mismatch.");
				Console::Wrn("  Server version: %s", mysql_get_server_info(this->impl->mysql_handle));
				Console::Wrn("  Client version: %s", mysql_get_client_info());
			}
#endif

			if (!db.empty() && mysql_select_db(this->impl->mysql_handle, db.c_str()) != 0)
			{
				throw Database_OpenFailed(mysql_error(this->impl->mysql_handle), { static_cast<int>(mysql_errno(this->impl->mysql_handle)) });
			}

			this->connected = true;

			break;
#endif // DATABASE_MYSQL

#ifdef DATABASE_SQLITE
		case SQLite:
			if (sqlite3_libversion_number() != SQLITE_VERSION_NUMBER)
			{
				if (sqlite3_libversion_number() > SQLITE_VERSION_NUMBER)
				{
					Console::Wrn("SQLite library runtime version is greater than the library version that etheos was compiled with.");
					Console::Wrn("  Compiled version: %s", SQLITE_VERSION);
					Console::Wrn("  Runtime version:  %s", sqlite3_libversion());
					Console::Wrn("This could cause issues if you have modified etheos to use non-stable sqlite3 API calls.");
				}
				else
				{
					Console::Err("SQLite library runtime version is incompatible with the library version that etheos was compiled with.");
					Console::Err("  Compiled version: %s", SQLITE_VERSION);
					Console::Err("  Runtime version:  %s", sqlite3_libversion());
#ifdef WIN32
					Console::Err("Make sure EOSERV is using the correct version of sqlite3.dll");
#endif // WIN32

					throw Database_OpenFailed("SQLite library version mismatch");
				}
			}

			if (sqlite3_open(host.c_str(), &this->impl->sqlite_handle) != SQLITE_OK)
			{
				throw Database_OpenFailed(sqlite3_errmsg(this->impl->sqlite_handle));
			}

			this->connected = true;

			break;
#endif // DATABASE_SQLITE

#ifdef DATABASE_SQLSERVER
		case SqlServer:
		{
			// Set connected so that if a failure occurs the handles get closed properly in the destructor
			this->connected = true;

			SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &this->impl->hEnv);
			if (!SQLSERVER_SUCCEEDED(ret))
			{
				if (ret == SQL_ERROR)
					HandleSqlServerError(SQL_HANDLE_ENV, this->impl->hEnv, ret, Console::Err);
				this->connected = false;
				throw Database_OpenFailed("Unable to allocate ODBC environment handle");
			}

			ret = SQLSetEnvAttr(this->impl->hEnv, SQL_ATTR_ODBC_VERSION, reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3), 0);
			if (!SQLSERVER_SUCCEEDED(ret))
			{
				if (ret == SQL_ERROR)
					HandleSqlServerError(SQL_HANDLE_ENV, this->impl->hEnv, ret, Console::Err);
				this->connected = false;
				throw Database_OpenFailed("Unable to set ODBC version attribute");
			}

			ret = SQLAllocHandle(SQL_HANDLE_DBC, this->impl->hEnv, &this->impl->hConn);
			if (!SQLSERVER_SUCCEEDED(ret))
			{
				if (ret == SQL_ERROR)
					HandleSqlServerError(SQL_HANDLE_DBC, this->impl->hConn, ret, Console::Err);
				this->connected = false;
				throw Database_OpenFailed("Unable to allocate ODBC connection handle");
			}

			char connStrBuff[4096] = { 0 };
#ifdef _WIN32
			auto driver_str = "SQL Server";
#else
			auto driver_str = "ODBC Driver 17 for SQL Server";
#endif
			if (db.empty())
			{
				sprintf(connStrBuff, "DRIVER={%s};SERVER={%s,%d};UID={%s};PWD={%s}",
					driver_str,
					this->host.c_str(),
					this->port,
					this->user.c_str(),
					this->pass.c_str());
			}
			else
			{
				sprintf(connStrBuff, "DRIVER={%s};SERVER={%s,%d};DATABASE={%s};UID={%s};PWD={%s}",
					driver_str,
					this->host.c_str(),
					this->port,
					this->db.c_str(),
					this->user.c_str(),
					this->pass.c_str());
			}

			char sqlRetConnStr[1024] = { 0 };
			ret = SQLDriverConnect(this->impl->hConn, NULL,
				reinterpret_cast<SQLCHAR*>(connStrBuff),
				SQL_NTS,
				reinterpret_cast<SQLCHAR*>(sqlRetConnStr),
				1024,
				NULL,
				SQL_DRIVER_NOPROMPT);

			if (!SQLSERVER_SUCCEEDED(ret))
			{
				std::list<int> errorCodes;
				if (ret == SQL_ERROR)
					HandleSqlServerError(SQL_HANDLE_DBC, this->impl->hConn, ret, Console::Err, &errorCodes);
				this->connected = false;
				throw Database_OpenFailed("Unable to connect to target server", errorCodes);
			}

			ret = SQLAllocHandle(SQL_HANDLE_STMT, this->impl->hConn, &this->impl->hstmt);
			if (!SQLSERVER_SUCCEEDED(ret))
			{
				if (ret == SQL_ERROR)
					HandleSqlServerError(SQL_HANDLE_STMT, this->impl->hstmt, ret, Console::Err);
				this->connected = false;
				throw Database_OpenFailed("Unable to allocate ODBC statement handle");
			}

			// Set transaction isolation level so that reads from other connections aren't blocked
			// EOSERV operates in a pattern where transactions are always open (committed periodically)
			// Default isolation level in SQL Server is `READ COMMITTED`
			//
			Database_Result res = this->Query("SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED");
			if (res.error)
			{
				throw Database_OpenFailed("Unable to set transaction isolation level for SQL Server!");
			}

			break;
		}
#endif // DATABASE_SQLSERVER

		default:
			throw Database_OpenFailed("Invalid database engine.");
	}
}

void Database::Close()
{
	if (!this->connected)
	{
		return;
	}

	this->connected = false;

	switch (this->engine)
	{
		case MySQL:
#ifdef DATABASE_MYSQL
			mysql_close(this->impl->mysql_handle);
#endif // DATABASE_MYSQL
			break;

		case SQLite:
#ifdef DATABASE_SQLITE
			sqlite3_close(this->impl->sqlite_handle);
#endif // DATABASE_SQLITE
			break;

		case SqlServer:
#ifdef DATABASE_SQLSERVER
			SQLFreeHandle(SQL_HANDLE_STMT, this->impl->hstmt);
			SQLDisconnect(this->impl->hConn);
			SQLFreeHandle(SQL_HANDLE_DBC, this->impl->hConn);
			SQLFreeHandle(SQL_HANDLE_ENV, this->impl->hEnv);
#endif // DATABASE_SQLSERVER
			break;
	}
}

Database_Result Database::RawQuery(const char* query, bool tx_control, bool prepared)
{
	if (!this->connected)
	{
		throw Database_QueryFailed("Not connected to database.");
	}

	Database_Result result;

#ifndef DATABASE_MYSQL
	(void)tx_control;
#endif

#ifndef DATABASE_SQLSERVER
	(void)prepared;
#endif

#ifdef DATABASE_DEBUG
	Console::Dbg("%s", query);
#endif // DATABASE_DEBUG

	switch (this->engine)
	{
#ifdef DATABASE_MYSQL
		case MySQL:
		{
			MYSQL_RES* mresult = nullptr;
			MYSQL_FIELD* fields = nullptr;
			int num_fields = 0;
			std::size_t query_length = std::strlen(query);

			exec_query:
			if (mysql_real_query(this->impl->mysql_handle, query, query_length) != 0)
			{
				int myerr = mysql_errno(this->impl->mysql_handle);
				int recovery_attempt = 0;

				if (myerr == CR_SERVER_GONE_ERROR || myerr == CR_SERVER_LOST || myerr == ER_LOCK_WAIT_TIMEOUT)
				{
					server_gone:

					if (++recovery_attempt > 10)
					{
						Console::Err("Could not re-connect to database. Halting server.");
						std::terminate();
					}

					Console::Wrn("Connection to database lost! Attempting to reconnect... (Attempt %i / 10)", recovery_attempt);
					this->Close();
					util::sleep(2.0 * recovery_attempt);

					try
					{
						this->Connect(this->engine, this->host, this->port, this->user, this->pass, this->db);
					}
					catch (const Database_OpenFailed& e)
					{
						Console::Err("Connection failed: %s", e.error());
					}

					if (!this->connected)
					{
						goto server_gone;
					}

					if (this->in_transaction)
					{
#ifdef DEBUG
						Console::Dbg("Replaying %i queries.", this->transaction_log.size());
#endif // DEBUG

						if (mysql_real_query(this->impl->mysql_handle, "START TRANSACTION", std::strlen("START TRANSACTION")) != 0)
						{
							int myerr = mysql_errno(this->impl->mysql_handle);

							if (myerr == CR_SERVER_GONE_ERROR || myerr == CR_SERVER_LOST || myerr == ER_LOCK_WAIT_TIMEOUT)
							{
								goto server_gone;
							}
							else
							{
								Console::Err("Error during recovery: %s", mysql_error(this->impl->mysql_handle));
								Console::Err("Halting server.");
								std::terminate();
							}
						}

						for (const std::string& q : this->transaction_log)
						{
#ifdef DATABASE_DEBUG
							Console::Dbg("%s", q.c_str());
#endif // DATABASE_DEBUG
							int myerr = 0;
							int query_result = mysql_real_query(this->impl->mysql_handle, q.c_str(), q.length());

							if (query_result == 0)
							{
								mresult = mysql_use_result(this->impl->mysql_handle);
							}

							myerr = mysql_errno(this->impl->mysql_handle);

							if (myerr == CR_SERVER_GONE_ERROR || myerr == CR_SERVER_LOST || myerr == ER_LOCK_WAIT_TIMEOUT)
							{
								goto server_gone;
							}
							else if (myerr)
							{
								Console::Err("Error during recovery: %s", mysql_error(this->impl->mysql_handle));
								Console::Err("Halting server.");
								std::terminate();
							}
							else
							{
								if (mresult)
								{
									mysql_free_result(mresult);
									mresult = nullptr;
								}
							}
						}
					}

					goto exec_query;
				}
				else
				{
					throw Database_QueryFailed(mysql_error(this->impl->mysql_handle));
				}
			}

			if (this->in_transaction && !tx_control)
			{
				using namespace std;

				if (strncmp(query, "SELECT", 6) != 0)
					this->transaction_log.emplace_back(std::string(query));
			}

			num_fields = mysql_field_count(this->impl->mysql_handle);

			if ((mresult = mysql_store_result(this->impl->mysql_handle)) == 0)
			{
				if (num_fields == 0)
				{
					result.affected_rows = static_cast<int>(mysql_affected_rows(this->impl->mysql_handle));
					return result;
				}
				else
				{
					throw Database_QueryFailed(mysql_error(this->impl->mysql_handle));
				}
			}

			fields = mysql_fetch_fields(mresult);

			for (int i = 0; i < num_fields; ++i)
			{
				if (!fields[i].name)
				{
					throw Database_QueryFailed("libMySQL critical failure!");
				}
			}

			result.resize(static_cast<unsigned int>(mysql_num_rows(mresult)));
			int i = 0;
			for (MYSQL_ROW row = mysql_fetch_row(mresult); row != 0; row = mysql_fetch_row(mresult))
			{
				std::unordered_map<std::string, util::variant> resrow;
				for (int ii = 0; ii < num_fields; ++ii)
				{
					util::variant rescell;
					if (IS_NUM(fields[ii].type))
					{
						if (row[ii])
						{
							rescell = util::to_int(row[ii]);
						}
						else
						{
							rescell = 0;
						}
					}
					else
					{
						if (row[ii])
						{
							rescell = row[ii];
						}
						else
						{
							rescell = "";
						}
					}
					resrow[fields[ii].name] = rescell;
				}
				result[i++] = resrow;
			}

			mysql_free_result(mresult);
		}
		break;
#endif // DATABASE_MYSQL

#ifdef DATABASE_SQLITE
		case SQLite:
			if (sqlite3_exec(this->impl->sqlite_handle, query, sqlite_callback, (void *)this, 0) != SQLITE_OK)
			{
				throw Database_QueryFailed(sqlite3_errmsg(this->impl->sqlite_handle));
			}
			result = this->callbackdata;
			this->callbackdata.clear();
			break;
#endif // DATABASE_SQLITE

#ifdef DATABASE_SQLSERVER
		case SqlServer:
		{
			SQLRETURN ret = prepared
				? SQLExecute(this->impl->hstmt)
				: SQLExecDirect(this->impl->hstmt, (SQLCHAR*)query, SQL_NTS);

			if (SQLSERVER_SUCCEEDED(ret))
			{
				SQLSMALLINT numCols = 0;
				ret = SQLNumResultCols(this->impl->hstmt, &numCols);
				if (SQLSERVER_SUCCEEDED(ret))
				{
					if (numCols > 0)
					{
						// select data - get column names
						std::vector<std::string> fields;
						fields.reserve(numCols);

						for (int colNdx = 1; colNdx <= numCols; ++colNdx)
						{
							char titleBuf[50] = { 0 };
							SQLColAttribute(this->impl->hstmt, colNdx, SQL_DESC_NAME, titleBuf, sizeof(titleBuf), NULL, NULL);
							fields.push_back(std::string(titleBuf));
						}

						// get data values
						while (SQLFetch(this->impl->hstmt) == SQL_SUCCESS && SQLSERVER_SUCCEEDED(ret))
						{
							std::unordered_map<std::string, util::variant> resrow;

							for (SQLUSMALLINT colNdx = 1; colNdx <= numCols && SQLSERVER_SUCCEEDED(ret); ++colNdx)
							{
								SQLLEN colType;
								ret = SQLColAttribute(this->impl->hstmt, colNdx, SQL_DESC_CONCISE_TYPE, NULL, 0, NULL, &colType);
								if (SQLSERVER_SUCCEEDED(ret))
								{
									SQLLEN nullIndicator = 0;
									if (colType == SQL_CHAR || colType == SQL_VARCHAR || colType == SQL_LONGVARCHAR)
									{
										// handle string data
										SQLCHAR resultStr[2048] = { 0 };
										ret = SQLGetData(this->impl->hstmt, colNdx, SQL_C_CHAR, resultStr, 2048, &nullIndicator);
										if (SQLSERVER_SUCCEEDED(ret))
											resrow[fields[colNdx-1]] = nullIndicator != SQL_NULL_DATA
												? util::variant(std::string((const char*)resultStr))
												: util::variant(std::string());
									}
									else
									{
										// handle numeric data
										SQLINTEGER resultInt;
										ret = SQLGetData(this->impl->hstmt, colNdx, static_cast<SQLSMALLINT>(colType), &resultInt, 0, &nullIndicator);
										if (SQLSERVER_SUCCEEDED(ret))
											resrow[fields[colNdx-1]] = nullIndicator != SQL_NULL_DATA
												? util::variant(resultInt)
												: util::variant(0);
									}
								}
							}

							if (SQLSERVER_SUCCEEDED(ret))
								result.push_back(resrow);
						}
					}
					else
					{
						// insert/update/delete data
						SQLLEN rowCount = 0;
						ret = SQLRowCount(this->impl->hstmt, &rowCount);
						if (SQLSERVER_SUCCEEDED(ret) && rowCount >= 0)
						{
							result.affected_rows = static_cast<int>(rowCount);
						}
					}
				}
			}

			if (ret == SQL_ERROR || ret == SQL_SUCCESS_WITH_INFO)
			{
				HandleSqlServerError(SQL_HANDLE_STMT, this->impl->hstmt, ret, ret == SQL_ERROR ? Console::Err : Console::Dbg);
				if (ret == SQL_ERROR)
				{
					SQLFreeStmt(this->impl->hstmt, SQL_CLOSE);
					throw Database_QueryFailed("Error querying the database");
				}
			}

			SQLFreeStmt(this->impl->hstmt, SQL_CLOSE);
			break;
		}
#endif // DATABASE_SQLSERVER

		default:
			throw Database_QueryFailed("Unknown database engine");
	}

	return result;
}

Database::QueryParameterPair Database::ParseQueryArgs(const char * format, va_list ap) const
{
	std::string finalquery;
	std::list<std::string> parameters;

	int tempi;
	char *tempc;
#if defined(DATABASE_MYSQL) || defined(DATABASE_SQLITE)
	char *escret;
#ifdef DATABASE_MYSQL
	unsigned long esclen;
#endif
#endif

	bool removeQuote = false;
	for (const char *p = format; *p != '\0'; ++p)
	{
		if (*p == '#')
		{
			tempi = va_arg(ap,int);
			finalquery += util::to_string(tempi);
		}
		else if (*p == '@')
		{
			tempc = va_arg(ap,char *);
			auto tmpStr = static_cast<std::string>(tempc);

			if (this->engine == Database::SqlServer)
			{
				// filter backticks on nested insertion for SqlServer driver
				tmpStr.erase(std::remove(tmpStr.begin(), tmpStr.end(), '`'), tmpStr.end());
			}

			finalquery += tmpStr;
		}
		else if (*p == '$')
		{
			tempc = va_arg(ap,char *);
			switch (this->engine)
			{
				case MySQL:
#ifdef DATABASE_MYSQL
					tempi = strlen(tempc);
					escret = new char[tempi*2+1];
					esclen = mysql_real_escape_string(this->impl->mysql_handle, escret, tempc, tempi);
					finalquery += std::string(escret, esclen);
					delete[] escret;
#endif // DATABASE_MYSQL
					break;

				case SQLite:
#ifdef DATABASE_SQLITE
					escret = sqlite3_mprintf("%q",tempc);
					finalquery += escret;
					sqlite3_free(escret);
#endif // DATABASE_SQLITE
					break;

				case SqlServer:
#ifdef DATABASE_SQLSERVER
					// SQL Server prepared statements do not require quoted values
					if (finalquery.back() == '\'')
					{
						finalquery.pop_back();
						removeQuote = true; // signals to remove the next quote in the input string
					}

					finalquery += "?";
					parameters.push_back(std::string(tempc));
#endif // DATABASE_SQLSERVER
					break;
			}
		}
		else if ((*p == '`' && this->engine == SqlServer) || // filter backticks (for SqlServer)
				 (*p == '\'' && removeQuote)) // filter quotes if flag is set
		{
			if (removeQuote) removeQuote = false;
		}
		else
		{
			finalquery += *p;
		}
	}

	return QueryParameterPair(finalquery, parameters);
}

Database_Result Database::Query(const char *format, ...)
{
	if (!this->connected)
	{
		throw Database_QueryFailed("Not connected to database.");
	}

	std::va_list ap;
	va_start(ap, format);
	QueryParameterPair queryState = std::move(this->ParseQueryArgs(format, ap));
	va_end(ap);

	std::string& finalquery = queryState.first;
	bool prepared = false;

#ifdef DATABASE_SQLSERVER
	std::list<std::string>& parameters = queryState.second;

	if (!parameters.empty())
	{
		prepared = true;

		SQLRETURN ret = SQLPrepare(this->impl->hstmt, (SQLCHAR*)(finalquery.c_str()), SQL_NTS);
		if (!SQLSERVER_SUCCEEDED(ret))
		{
			HandleSqlServerError(SQL_HANDLE_STMT, this->impl->hstmt, ret, Console::Err);
			throw Database_QueryFailed("Unable to prepare parameter-bound query for execution!");
		}

		SQLLEN nts = SQL_NTS;
		int paramNdx = 1; // parameter indices start at 1
		for (const auto& parameter : parameters)
		{
			ret = SQLBindParameter(
				this->impl->hstmt,
				paramNdx++,
				SQL_PARAM_INPUT,
				SQL_C_CHAR,
				SQL_VARCHAR,
				parameter.empty() ? 1 : parameter.length(), // parameter length must be non-zero, even for empty strings
				0,
				(SQLPOINTER)parameter.c_str(),
				0,
				&nts // parameter length must be passed by reference as a null-terminated string
			);

			if (!SQLSERVER_SUCCEEDED(ret))
			{
				// Warn when a parameter binding fails, but still try the query (it will probably fail)
				HandleSqlServerError(SQL_HANDLE_STMT, this->impl->hstmt, ret, Console::Wrn);
			}
		}
	}
#endif

	return this->RawQuery(finalquery.c_str(),
		false, // transaction control
		prepared);
}

std::string Database::Escape(const std::string& raw)
{
#if defined(DATABASE_MYSQL) || defined(DATABASE_SQLITE)
	char *escret;
#endif

#ifdef DATABASE_MYSQL
	unsigned long esclen;
#endif
	std::string result;

	switch (this->engine)
	{
		case MySQL:
#ifdef DATABASE_MYSQL
			escret = new char[raw.length()*2+1];
			esclen = mysql_real_escape_string(this->impl->mysql_handle, escret, raw.c_str(), raw.length());
			result.assign(escret, esclen);
			delete[] escret;
#endif // DATABASE_MYSQL
			break;

		case SQLite:
#ifdef DATABASE_SQLITE
			escret = sqlite3_mprintf("%q", raw.c_str());
			result = escret;
			sqlite3_free(escret);
#endif // DATABASE_SQLITE
			break;

		case SqlServer:
#ifdef DATABASE_SQLSERVER
			//todo: escape query string
			result = raw;
#endif // DATABASE_SQLSERVER
			break;
	}

	for (std::string::iterator it = result.begin(); it != result.end(); ++it)
	{
		if (*it == '@' || *it == '#' || *it == '$')
		{
			*it = '?';
		}
	}

	return result;
}

void Database::ExecuteFile(const std::string& filename)
{
	std::list<std::string> queries;
	std::string query;

	FILE* fh = std::fopen(filename.c_str(), "rt");

	try
	{
		while (true)
		{
			char buf[4096];

			const char *result = std::fgets(buf, 4096, fh);

			if (!result || std::feof(fh))
				break;

			for (char* p = buf; *p != '\0'; ++p)
			{
				if (*p == ';')
				{
					queries.push_back(query);
					query.erase();
				}
				else
				{
					query += *p;
				}
			}
		}
	}
	catch (std::exception &e)
	{
		(void)e;
		std::fclose(fh);
		throw;
	}

	std::fclose(fh);

	queries.push_back(query);
	query.erase();

	auto queriesEnd = std::remove_if(UTIL_RANGE(queries), [&](const std::string& s) { return util::trim(s).length() == 0; });
	this->ExecuteQueries(queries.begin(), queriesEnd);
}

bool Database::Pending() const
{
	return this->in_transaction;
}

bool Database::BeginTransaction()
{
	if (this->in_transaction)
		return false;

	for (int attempt = 1; ; ++attempt)
	{
		try
		{
			if (attempt > 1)
			{
				Console::Wrn("Start transaction failed. Trying again... (Attempt %i / 10)", attempt);
				util::sleep(1.0 * attempt);
			}

			switch (this->engine)
			{
				case MySQL:
#ifdef DATABASE_MYSQL
					this->RawQuery("START TRANSACTION", true);
#endif // DATABASE_MYSQL
					break;

				case SQLite:
#ifdef DATABASE_SQLITE
					this->RawQuery("BEGIN", true);
#endif // DATABASE_SQLITE
					break;

				case SqlServer:
#ifdef DATABASE_SQLSERVER
					this->RawQuery("BEGIN TRANSACTION", true);
#endif // DATABASE_SQLSERVER
					break;
			}

			break;
		}
		catch (...)
		{
			if (attempt >= 10)
			{
				Console::Err("Failed to begin transaction. Halting server.");
				std::terminate();
			}
		}
	}

	this->in_transaction = true;

	return true;
}

void Database::Commit()
{
	if (!this->in_transaction)
		throw Database_Exception("No transaction to commit");

	if (this->engine != SqlServer)
	{
		this->RawQuery("COMMIT", true);
	}
	else
	{
		this->RawQuery("COMMIT TRANSACTION", true);
	}

	this->in_transaction = false;
	this->transaction_log.clear();
}

void Database::Rollback()
{
	if (!this->in_transaction)
		throw Database_Exception("No transaction to rollback");

	if (this->engine != SqlServer)
	{
		this->RawQuery("ROLLBACK", true);
	}
	else
	{
		this->RawQuery("ROLLBACK TRANSACTION", true);
	}

	this->in_transaction = false;
	this->transaction_log.clear();
}

Database::~Database()
{
	this->Close();
}
