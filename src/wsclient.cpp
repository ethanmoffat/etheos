
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifdef WEBSOCKET_SUPPORT

#include "wsclient.hpp"

#include "console.hpp"
#include "eoserver.hpp"
#include "packet.hpp"
#include "player.hpp"
#include "character.hpp"
#include "world.hpp"
#include "socket.hpp"

#include <cstring>
#include <mutex>
#include <string>

WSClient::WSClient(std::nullptr_t, EOServer *server, const IPAddress &addr)
	: EOClient(server)
	, remote_addr(addr)
{
	this->connected = true;
	this->is_websocket = true;
	this->connect_time = std::time(0);

	this->SetRecvBuffer(32768);
	this->SetSendBuffer(32768);
}

void WSClient::SetWebSocketSender(SendFunction sender)
{
	ws_send = sender;
}

void WSClient::QueueRecvData(const std::string &data)
{
	std::lock_guard<std::mutex> lock(recv_mutex);
	recv_queue.push(data);
}

void WSClient::PumpRecvQueue()
{
	std::lock_guard<std::mutex> lock(recv_mutex);

	while (!recv_queue.empty())
	{
		const std::string &data = recv_queue.front();

		if (data.length() > this->recv_buffer.length() - this->recv_buffer_used)
		{
			Console::Wrn("WSClient recv buffer overflow, disconnecting");
			recv_queue = std::queue<std::string>();
			this->Close(true);
			return;
		}

		const std::size_t mask = this->recv_buffer.length() - 1;

		for (std::size_t i = 0; i < data.length(); ++i)
		{
			this->recv_buffer_ppos = (this->recv_buffer_ppos + 1) & mask;
			this->recv_buffer[this->recv_buffer_ppos] = data[i];
		}

		this->recv_buffer_used += data.length();

		recv_queue.pop();
	}
}

bool WSClient::NeedTick()
{
	return false;
}

bool WSClient::Upload(FileType type, int id, InitReply init_reply)
{
	char mapbuf[7];
	std::sprintf(mapbuf, "%05i", int(std::abs(id)));

	std::string filename;
	switch (type)
	{
		case FILE_MAP: filename = std::string(server()->world->config["MapDir"]) + mapbuf + ".emf"; break;
		case FILE_ITEM: filename = std::string(server()->world->config["EIF"]); break;
		case FILE_NPC: filename = std::string(server()->world->config["ENF"]); break;
		case FILE_SPELL: filename = std::string(server()->world->config["ESF"]); break;
		case FILE_CLASS: filename = std::string(server()->world->config["ECF"]); break;
		default: return false;
	}

	std::FILE *fh = std::fopen(filename.c_str(), "rb");
	if (!fh)
		return false;

	std::fseek(fh, 0, SEEK_END);
	std::size_t file_size = std::ftell(fh);
	std::fseek(fh, 0, SEEK_SET);

	std::string file_data(file_size, '\0');
	if (std::fread(&file_data[0], 1, file_size, fh) != file_size)
	{
		std::fclose(fh);
		return false;
	}
	std::fclose(fh);

	// Dynamically rewrite map bytes for GlobalPK
	if (type == FILE_MAP && server()->world->config["GlobalPK"] && !server()->world->PKExcept(player->character->mapid))
	{
		if (file_size > 0x03)
			file_data[0x03] = (char)0xFF;
		if (file_size > 0x04)
			file_data[0x04] = (char)0x01;
		if (file_size > 0x1F)
			file_data[0x1F] = (char)0x04;
	}

	// Build the header packet
	PacketBuilder builder(PACKET_F_INIT, PACKET_A_INIT, 2);
	builder.AddChar(init_reply);

	if (type != FILE_MAP)
		builder.AddChar(1);

	builder.AddSize(file_size);

	LogPacket(PACKET_F_INIT, PACKET_A_INIT, builder.Length(), "UPLD");

	// Encode the header
	std::string header = this->processor.Encode(builder);

	// Combine header + raw file data into one message
	std::string combined;
	combined.reserve(header.size() + file_data.size());
	combined.append(header);
	combined.append(file_data);

	// Send as a single WebSocket binary message
	if (ws_send && ws_connected)
	{
		ws_send(combined);
	}

	return true;
}

void WSClient::Send(const PacketBuilder &builder)
{
	std::lock_guard<std::mutex> lock(send_mutex);

	auto fam = PacketFamily(PacketProcessor::EPID(builder.GetID())[1]);
	auto act = PacketAction(PacketProcessor::EPID(builder.GetID())[0]);
	this->LogPacket(fam, act, builder.Length(), "SEND");

	std::string data = this->processor.Encode(builder);

	if (this->upload_fh)
	{
		if (data.length() > this->send_buffer2.length() - this->send_buffer2_used)
		{
			this->Close(true);
			return;
		}

		const std::size_t mask = this->send_buffer2.length() - 1;

		for (std::size_t i = 0; i < data.length(); ++i)
		{
			this->send_buffer2_ppos = (this->send_buffer2_ppos + 1) & mask;
			this->send_buffer2[this->send_buffer2_ppos] = data[i];
		}

		this->send_buffer2_used += data.length();
	}
	else
	{
		if (ws_send && ws_connected)
		{
			ws_send(data);
		}
	}
}

bool WSClient::Connected() const
{
	return this->connected && this->ws_connected;
}

IPAddress WSClient::GetRemoteAddr() const
{
	return remote_addr;
}

void WSClient::Close(bool force)
{
	(void)force;

	this->connected = false;
	this->ws_connected = false;
	this->closed_time = std::time(0);

	if (force)
	{
		this->closed_time = 0;
	}

	ws_send = nullptr;
}

WSClient::~WSClient()
{
	ws_send = nullptr;
}

#endif // WEBSOCKET_SUPPORT
