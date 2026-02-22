
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef WSSERVER_HPP_INCLUDED
#define WSSERVER_HPP_INCLUDED

#ifdef WEBSOCKET_SUPPORT

#include "fwd/wsserver.hpp"
#include "fwd/eoserver.hpp"

#include <ixwebsocket/IXWebSocketServer.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class WSClient;

/**
 * WebSocket server that listens for web client connections and bridges
 * them into the existing EOServer client pipeline via WSClient.
 */
class WSServer
{
	private:
		ix::WebSocketServer server;
		EOServer *eo_server;

		std::mutex clients_mutex;
		std::unordered_map<std::string, WSClient*> ws_clients;

	public:
		WSServer(EOServer *eo_server, const std::string &host, int port);

		bool Start();
		void Tick();
		void Stop();

		~WSServer();
};

#endif // WEBSOCKET_SUPPORT

#endif // WSSERVER_HPP_INCLUDED
