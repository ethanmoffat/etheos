
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "eoserver.hpp"

#include "config.hpp"
#include "eoclient.hpp"
#include "packet.hpp"
#include "sln.hpp"
#include "timer.hpp"
#include "world.hpp"
#include "handlers/handlers.hpp"

#include "console.hpp"
#include "socket.hpp"
#include "util.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

void server_ping_all(void *server_void)
{
	EOServer *server = static_cast<EOServer *>(server_void);

	UTIL_FOREACH(server->clients, rawclient)
	{
		EOClient *client = static_cast<EOClient *>(rawclient);

		if (client->needpong)
		{
			client->AsyncOpPending(false);
			client->Close();
		}
		else
		{
			client->PingNewSequence();
			auto seq_bytes = client->GetSeqUpdateBytes();

			PacketBuilder builder(PACKET_CONNECTION, PACKET_PLAYER, 3);
			builder.AddShort(seq_bytes.first);
			builder.AddChar(seq_bytes.second);

			client->needpong = true;
			client->Send(builder);
		}
	}
}

void server_check_hangup(void *server_void)
{
	EOServer *server = static_cast<EOServer *>(server_void);

	double now = Timer::GetTime();
	double delay = server->HangupDelay;

	UTIL_FOREACH(server->clients, rawclient)
	{
		 EOClient *client = static_cast<EOClient *>(rawclient);

		if (client->Connected() && !client->Accepted() && client->start + delay < now)
		{
			server->RecordClientRejection(client->GetRemoteAddr(), "hanging up on idle client");
			client->Close(true);
		}
	}
}

void server_pump_queue(void *server_void)
{
	EOServer *server = static_cast<EOServer *>(server_void);
	double now = Timer::GetTime();

	UTIL_FOREACH(server->clients, rawclient)
	{
		EOClient *client = static_cast<EOClient *>(rawclient);

		if (!client->Connected())
			continue;

		std::size_t size = client->queue.queue.size();

		if (size > std::size_t(int(server->world->config["PacketQueueMax"])))
		{
			Console::Wrn("Client was disconnected for filling up the action queue: %s", static_cast<std::string>(client->GetRemoteAddr()).c_str());
			client->AsyncOpPending(false);
			client->Close();
			continue;
		}

		if (size != 0 && client->queue.next <= now)
		{
			std::unique_ptr<ActionQueue_Action> action = std::move(client->queue.queue.front());
			client->queue.queue.pop();

#ifndef DEBUG_EXCEPTIONS
			try
			{
#endif // DEBUG_EXCEPTIONS
				Handlers::Handle(action->reader.Family(), action->reader.Action(), client, action->reader, !action->auto_queue);
#ifndef DEBUG_EXCEPTIONS
			}
			catch (Socket_Exception& e)
			{
				Console::Err("Client caused an exception and was closed: %s.", static_cast<std::string>(client->GetRemoteAddr()).c_str());
				Console::Err("%s: %s", e.what(), e.error());
				client->AsyncOpPending(false);
				client->Close();
			}
			catch (Database_Exception& e)
			{
				Console::Err("Client caused an exception and was closed: %s.", static_cast<std::string>(client->GetRemoteAddr()).c_str());
				Console::Err("%s: %s", e.what(), e.error());
				client->AsyncOpPending(false);
				client->Close();
			}
			catch (std::runtime_error& e)
			{
				Console::Err("Client caused an exception and was closed: %s.", static_cast<std::string>(client->GetRemoteAddr()).c_str());
				Console::Err("Runtime Error: %s", e.what());
				client->AsyncOpPending(false);
				client->Close();
			}
			catch (std::logic_error& e)
			{
				Console::Err("Client caused an exception and was closed: %s.", static_cast<std::string>(client->GetRemoteAddr()).c_str());
				Console::Err("Logic Error: %s", e.what());
				client->AsyncOpPending(false);
				client->Close();
			}
			catch (std::exception& e)
			{
				Console::Err("Client caused an exception and was closed: %s.", static_cast<std::string>(client->GetRemoteAddr()).c_str());
				Console::Err("Uncaught Exception: %s", e.what());
				client->AsyncOpPending(false);
				client->Close();
			}
			catch (...)
			{
				Console::Err("Client caused an exception and was closed: %s.", static_cast<std::string>(client->GetRemoteAddr()).c_str());
				client->AsyncOpPending(false);
				client->Close();
			}
#endif // DEBUG_EXCEPTIONS

			client->queue.next = now + action->time;
		}
	}
}

void EOServer::UpdateConfig()
{
	delete ping_timer;
	ping_timer = new TimeEvent(server_ping_all, this, double(this->world->config["PingRate"]), Timer::FOREVER);
	this->world->timer.Register(ping_timer);

	this->QuietConnectionErrors = bool(this->world->config["QuietConnectionErrors"]);
	this->HangupDelay = double(this->world->config["HangupDelay"]);

	this->maxconn = unsigned(int(this->world->config["MaxConnections"]));
}

void EOServer::Initialize(std::shared_ptr<DatabaseFactory> databaseFactory, const Config &eoserv_config, const Config &admin_config)
{
	this->world = new World(databaseFactory, eoserv_config, admin_config);

	TimeEvent *event = new TimeEvent(server_check_hangup, this, 1.0, Timer::FOREVER);
	this->world->timer.Register(event);

	event = new TimeEvent(server_pump_queue, this, 0.001, Timer::FOREVER);
	this->world->timer.Register(event);

	this->world->server = this;

	if (this->world->config["SLN"])
	{
		this->sln = new SLN(this);
	}
	else
	{
		this->sln = 0;
	}

	this->start = Timer::GetTime();

	this->UpdateConfig();
}

Client *EOServer::ClientFactory(const Socket &sock)
{
	 return new EOClient(sock, this);
}

void EOServer::Tick()
{
	std::vector<Client *> *active_clients = 0;
	EOClient *newclient = static_cast<EOClient *>(this->Poll());

	if (newclient)
	{
		double now = Timer::GetTime();
		int ip_connections = 0;
		bool throttle = false;
		IPAddress remote_addr = newclient->GetRemoteAddr();

		const double reconnect_limit = int(this->world->config["IPReconnectLimit"]);
		const int max_per_ip = int(this->world->config["MaxConnectionsPerIP"]);
		const int log_connection = static_cast<LogConnection>(int(this->world->config["LogConnection"]));

		UTIL_IFOREACH(connection_log, connection)
		{
			double last_connection_time = connection->second.last_connection_time;
			double last_rejection_time = connection->second.last_rejection_time;

			if (last_connection_time + reconnect_limit < now
			 && last_rejection_time + 30.0 < now)
			{
				int rejections = connection->second.rejections;

				if (rejections > 1)
					Console::Wrn("Connections from %s were rejected (%dx)", std::string(connection->first).c_str(), rejections);
				else if (rejections == 1)
					Console::Wrn("Connection from %s was rejected (1x)", std::string(connection->first).c_str());

				connection = connection_log.erase(connection);

				if (connection == connection_log.end())
					break;

				continue;
			}

			if (connection->first == remote_addr
			 && last_connection_time + reconnect_limit >= now)
			{
				throttle = true;
			}
		}

		UTIL_FOREACH(this->clients, client)
		{
			if (client->GetRemoteAddr() == newclient->GetRemoteAddr())
			{
				++ip_connections;
			}
		}

		if (throttle)
		{
			this->RecordClientRejection(newclient->GetRemoteAddr(), "reconnecting too fast");
			newclient->Close(true);
		}
		else if (max_per_ip != 0 && ip_connections > max_per_ip)
		{
			this->RecordClientRejection(newclient->GetRemoteAddr(), "too many connections from this address");
			newclient->Close(true);
		}
		else if (log_connection == LogConnection::LogAll || (log_connection == LogConnection::FilterPrivate && !remote_addr.IsPrivate()))
		{
			connection_log[remote_addr].last_connection_time = Timer::GetTime();
			Console::Out("New connection from %s (%i/%i connections)", std::string(remote_addr).c_str(), this->Connections(), this->MaxConnections());
		}
	}

	try
	{
		active_clients = this->Select(0.001);
	}
	catch (Socket_SelectFailed &e)
	{
		(void)e;
		if (errno != EINTR)
			throw;
	}

	if (active_clients)
	{
		UTIL_FOREACH(*active_clients, client)
		{
			EOClient *eoclient = static_cast<EOClient *>(client);
			eoclient->Tick();
		}

		active_clients->clear();
	}

	this->BuryTheDead();

	this->world->timer.Tick();
}

void EOServer::RecordClientRejection(const IPAddress& ip, const char* reason)
{
	if (QuietConnectionErrors)
	{
		// Buffer up to 100 rejections + 30 seconds in delayed error mode
		if (++this->connection_log[ip].rejections < 100)
			this->connection_log[ip].last_rejection_time = Timer::GetTime();
	}
	else
	{
		Console::Wrn("Connection from %s was rejected (%s)", std::string(ip).c_str(), reason);
	}
}

EOServer::~EOServer()
{
	// All clients must be fully closed before the world ends
	UTIL_FOREACH(this->clients, client)
	{
		client->AsyncOpPending(false);
		client->Close();
	}

	// Spend up to 2 seconds shutting down
	while (this->clients.size() > 0)
	{
		std::vector<Client *> *active_clients = this->Select(0.1);

		if (active_clients)
		{
			UTIL_FOREACH(*active_clients, client)
			{
				EOClient *eoclient = static_cast<EOClient *>(client);
				eoclient->Tick();
			}

			active_clients->clear();
		}

		this->BuryTheDead();
	}

	delete this->sln;
	delete this->world;

	Close();
}
