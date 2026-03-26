
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef WSCLIENT_HPP_INCLUDED
#define WSCLIENT_HPP_INCLUDED

#ifdef WEBSOCKET_SUPPORT

#include "fwd/wsclient.hpp"

#include "eoclient.hpp"
#include "eoserver.hpp"

#include <functional>
#include <mutex>
#include <string>
#include <queue>

/**
 * An EOClient subclass that communicates over a WebSocket connection
 * instead of a raw TCP socket.
 *
 * Incoming WebSocket binary messages are queued and fed into the
 * normal EOClient::Tick() pipeline via PumpRecvQueue().
 *
 * Outgoing packets are intercepted by overriding Send() and routed
 * through the WebSocket connection via a sender callback.
 */
class WSClient : public EOClient
{
	public:
		using SendFunction = std::function<void(const std::string &)>;

	private:
		SendFunction ws_send;
		IPAddress remote_addr;

		std::queue<std::string> recv_queue;
		std::mutex recv_mutex;

		bool ws_connected = true;

	public:
		WSClient(std::nullptr_t, EOServer *server, const IPAddress &addr);

		void SetWebSocketSender(SendFunction sender);

		void PumpRecvQueue();
		void QueueRecvData(const std::string &data);

		virtual bool Upload(FileType type, int id, InitReply init_reply) override;
		virtual bool NeedTick() override;
		virtual void Send(const PacketBuilder &packet) override;
		virtual bool Connected() const override;

		IPAddress GetRemoteAddr() const;

		virtual void Close(bool force = false) override;

		virtual ~WSClient();
};

#endif // WEBSOCKET_SUPPORT

#endif // WSCLIENT_HPP_INCLUDED
