
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifdef WEBSOCKET_SUPPORT

#include "wsserver.hpp"

#include "wsclient.hpp"
#include "eoserver.hpp"

#include "console.hpp"
#include "socket.hpp"

#include <ixwebsocket/IXWebSocketServer.h>
#include <ixwebsocket/IXNetSystem.h>

#include <memory>
#include <mutex>
#include <string>

WSServer::WSServer(EOServer *eo_server, const std::string &host, int port)
	: server(port, host)
	, eo_server(eo_server)
{
	ix::initNetSystem();

	server.setOnClientMessageCallback(
		[this](std::shared_ptr<ix::ConnectionState> connectionState,
		       ix::WebSocket &webSocket,
		       const ix::WebSocketMessagePtr &msg)
		{
			std::string connId = connectionState->getId();

			if (msg->type == ix::WebSocketMessageType::Open)
			{
				std::string remote_ip = connectionState->getRemoteIp();
				IPAddress addr = IPAddress::Lookup(remote_ip);

				Console::Out("WebSocket connection from %s (id: %s)", remote_ip.c_str(), connId.c_str());

				// Create a WSClient for this WebSocket connection
				// We don't use shared_ptr for the WebSocket since the server manages its lifetime
				WSClient *wsclient = new WSClient(nullptr, this->eo_server, addr);

				{
					std::lock_guard<std::mutex> lock(this->clients_mutex);
					this->ws_clients[connId] = wsclient;
				}

				// Set a per-connection message callback to avoid the shared_ptr issue
				webSocket.setOnMessageCallback(
					[this, connId, &webSocket](const ix::WebSocketMessagePtr &innerMsg)
					{
						if (innerMsg->type == ix::WebSocketMessageType::Message)
						{
							if (innerMsg->binary)
							{
								std::lock_guard<std::mutex> lock(this->clients_mutex);
								auto it = this->ws_clients.find(connId);
								if (it != this->ws_clients.end())
								{
									it->second->QueueRecvData(innerMsg->str);
								}
							}
						}
						else if (innerMsg->type == ix::WebSocketMessageType::Close)
						{
							std::lock_guard<std::mutex> lock(this->clients_mutex);
							auto it = this->ws_clients.find(connId);
							if (it != this->ws_clients.end())
							{
								it->second->Close();
								this->ws_clients.erase(it);
							}
						}
					}
				);

				// Store the webSocket reference for sending in WSClient
				// We need to set the send callback on the WSClient
				wsclient->SetWebSocketSender(
					[&webSocket](const std::string &data)
					{
						webSocket.sendBinary(data);
					}
				);

				// Add to the EOServer's client list so timers (ping, hangup, pump_queue) work
				this->eo_server->clients.push_back(wsclient);
			}
			else if (msg->type == ix::WebSocketMessageType::Message)
			{
				if (msg->binary)
				{
					std::lock_guard<std::mutex> lock(this->clients_mutex);
					auto it = this->ws_clients.find(connId);
					if (it != this->ws_clients.end())
					{
						it->second->QueueRecvData(msg->str);
					}
				}
			}
			else if (msg->type == ix::WebSocketMessageType::Close)
			{
				std::lock_guard<std::mutex> lock(this->clients_mutex);
				auto it = this->ws_clients.find(connId);
				if (it != this->ws_clients.end())
				{
					it->second->Close();
					this->ws_clients.erase(it);
				}
			}
			else if (msg->type == ix::WebSocketMessageType::Error)
			{
				Console::Wrn("WebSocket error: %s", msg->errorInfo.reason.c_str());

				std::lock_guard<std::mutex> lock(this->clients_mutex);
				auto it = this->ws_clients.find(connId);
				if (it != this->ws_clients.end())
				{
					it->second->Close();
					this->ws_clients.erase(it);
				}
			}
		}
	);
}

bool WSServer::Start()
{
	auto res = server.listen();
	if (!res.first)
	{
		Console::Err("Failed to start WebSocket server: %s", res.second.c_str());
		return false;
	}

	server.start();
	return true;
}

void WSServer::Tick()
{
	std::lock_guard<std::mutex> lock(clients_mutex);

	for (auto &pair : ws_clients)
	{
		WSClient *client = pair.second;

		if (!client->Connected())
			continue;

		// Pump queued WebSocket messages into the recv buffer
		client->PumpRecvQueue();
	}
}

void WSServer::Stop()
{
	server.stop();

	std::lock_guard<std::mutex> lock(clients_mutex);

	for (auto &pair : ws_clients)
	{
		pair.second->Close();
	}

	ws_clients.clear();

	ix::uninitNetSystem();
}

WSServer::~WSServer()
{
	Stop();
}

#endif // WEBSOCKET_SUPPORT
