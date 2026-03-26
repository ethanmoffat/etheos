
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
#include "socket_impl.hpp"
#include "util.hpp"

#include <array>
#include <cerrno>
#include <cstring>
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

	// Set up optional WebSocket listener
	int ws_port = int(this->world->config["WebSocketPort"]);
	if (ws_port > 0)
	{
		this->ws_listen_sock_ = socket(AF_INET, SOCK_STREAM, 0);

		if (this->ws_listen_sock_ != INVALID_SOCKET)
		{
			const int yes = 1;
#ifdef WIN32
			setsockopt(this->ws_listen_sock_, SOL_SOCKET, SO_REUSEADDR,
			           reinterpret_cast<const char*>(&yes), sizeof(yes));
#else
			setsockopt(this->ws_listen_sock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
			setsockopt(this->ws_listen_sock_, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif

			sockaddr_in sin;
			std::memset(&sin, 0, sizeof(sin));
			sin.sin_family = AF_INET;
			sin.sin_addr.s_addr = htonl(this->address);
			sin.sin_port = htons(static_cast<uint16_t>(ws_port));

			if (bind(this->ws_listen_sock_, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) == SOCKET_ERROR
			 || listen(this->ws_listen_sock_, int(this->world->config["ListenBacklog"])) == SOCKET_ERROR)
			{
				Console::Err("Failed to bind WebSocket listener on port %i", ws_port);
#ifdef WIN32
				closesocket(this->ws_listen_sock_);
#else
				close(this->ws_listen_sock_);
#endif
				this->ws_listen_sock_ = INVALID_SOCKET;
			}
			else
			{
				this->ws_enabled_ = true;
			}
		}
	}

	this->UpdateConfig();
}

Client *EOServer::ClientFactory(const Socket &sock)
{
	 return new EOClient(sock, this);
}

void EOServer::PollWebSocket()
{
	sockaddr_in sin;
	socklen_t addrsize = sizeof(sockaddr_in);

#ifdef WIN32
	unsigned long nonblocking = 1;
	ioctlsocket(this->ws_listen_sock_, FIONBIO, &nonblocking);
#else
	fcntl(this->ws_listen_sock_, F_SETFL, FNONBLOCK|FASYNC);
#endif

	SOCKET newsock = accept(this->ws_listen_sock_, reinterpret_cast<sockaddr*>(&sin), &addrsize);

#ifdef WIN32
	nonblocking = 0;
	ioctlsocket(this->ws_listen_sock_, FIONBIO, &nonblocking);
#else
	fcntl(this->ws_listen_sock_, F_SETFL, 0);
#endif

	if (newsock == INVALID_SOCKET)
		return;

	// Apply the same rate-limiting and connection limit checks as Tick()
	double now = Timer::GetTime();
	IPAddress remote_addr(ntohl(sin.sin_addr.s_addr));
	int ip_connections = 0;
	bool throttle = false;

	const double reconnect_limit = int(this->world->config["IPReconnectLimit"]);
	const int max_per_ip = int(this->world->config["MaxConnectionsPerIP"]);
	const int log_connection = static_cast<LogConnection>(int(this->world->config["LogConnection"]));

	// Check if server is at capacity
	if (this->clients.size() >= this->maxconn)
	{
#ifdef WIN32
		closesocket(newsock);
#else
		close(newsock);
#endif
		return;
	}

	UTIL_IFOREACH(connection_log, connection)
	{
		double last_connection_time = connection->second.last_connection_time;
		double last_rejection_time = connection->second.last_rejection_time;

		if (last_connection_time + reconnect_limit < now
		 && last_rejection_time + 30.0 < now)
		{
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
		if (client->GetRemoteAddr() == remote_addr)
			++ip_connections;
	}

	EOClient *newclient = new EOClient(Socket(newsock, sin), this);
	newclient->SetRecvBuffer(this->recv_buffer_max);
	newclient->SetSendBuffer(this->send_buffer_max);
	newclient->SetWebSocket(true);
	this->clients.push_back(newclient);

	if (throttle)
	{
		this->RecordClientRejection(remote_addr, "reconnecting too fast");
		newclient->Close(true);
	}
	else if (max_per_ip != 0 && ip_connections > max_per_ip)
	{
		this->RecordClientRejection(remote_addr, "too many connections from this address");
		newclient->Close(true);
	}
	else if (log_connection == LogConnection::LogAll || (log_connection == LogConnection::FilterPrivate && !remote_addr.IsPrivate()))
	{
		connection_log[remote_addr].last_connection_time = Timer::GetTime();
		Console::Out("New WebSocket connection from %s (%i/%i connections)", std::string(remote_addr).c_str(), this->Connections(), this->MaxConnections());
	}
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

	if (this->ws_enabled_)
		this->PollWebSocket();
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

	if (this->ws_enabled_ && this->ws_listen_sock_ != INVALID_SOCKET)
	{
#ifdef WIN32
		closesocket(this->ws_listen_sock_);
#else
		close(this->ws_listen_sock_);
#endif
		this->ws_listen_sock_ = INVALID_SOCKET;
	}

	Close();
}
