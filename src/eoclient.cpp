
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "eoclient.hpp"

#include "character.hpp"
#include "config.hpp"
#include "eoclient.hpp"
#include "eodata.hpp"
#include "eoserver.hpp"
#include "packet.hpp"
#include "player.hpp"
#include "timer.hpp"
#include "world.hpp"

#include "console.hpp"
#include "socket.hpp"
#include "util.hpp"
#include "websocket.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>

void ActionQueue::AddAction(const PacketReader& reader, double time, bool auto_queue)
{
	this->queue.emplace(new ActionQueue_Action(reader, time, auto_queue));
}

ActionQueue::~ActionQueue()
{
	while (!this->queue.empty())
	{
		this->queue.pop();
	}
}

void EOClient::Initialize()
{
	this->upload_fh = 0;
	this->seq_start = 0;
	this->upcoming_seq_start = -1;
	this->seq = 0;
	this->id = this->server()->world->GenerateClientID();
	this->create_id = 0;
	this->length = 0;
	this->packet_state = EOClient::ReadLen1;
	this->state = EOClient::Uninitialized;
	this->player = 0;
	this->version = 0;
	this->needpong = false;
	this->login_attempts = 0;
	this->start = Timer::GetTime();
}

void EOClient::LogPacket(PacketFamily family, PacketAction action, size_t sz, const char * const actionStr)
{
	std::string ignoreFamilies = static_cast<std::string>(this->server()->world->config["IgnorePacketFamilies"]);

	if (ignoreFamilies != "*")
	{
		time_t rawtime;
		time(&rawtime);
		const tm * timeinfo = localtime(&rawtime);

		std::string fam = PacketProcessor::GetFamilyName(family);
		std::string act = PacketProcessor::GetActionName(action);
		if (ignoreFamilies.find(fam) == std::string::npos)
		{
			Console::Out("%02d/%02d/%04d - %02d:%02d:%02d | %-12s | %4s Family: %-15s | Action: %-15s | SIZE=%d",
				timeinfo->tm_mon + 1,
				timeinfo->tm_mday,
				timeinfo->tm_year + 1900,
				timeinfo->tm_hour,
				timeinfo->tm_min,
				timeinfo->tm_sec,
				player ? player->character ? player->character->real_name.c_str() : "no char" : "no char",
				actionStr,
				fam.c_str(),
				act.c_str(),
				sz);
		}
	}
}

bool EOClient::NeedTick()
{
	return this->upload_fh
	    || (this->websocket_ && this->ws_payload_pos_ < this->ws_payload_buf_.size());
}

void EOClient::DoWsHandshake()
{
	// Drain recv_buffer into ws_buf_ to accumulate the HTTP upgrade request
	while (this->recv_buffer_used > 0)
		ws_buf_ += this->Recv(std::min(this->recv_buffer_used, std::size_t(256)));

	// Wait until we have the full HTTP request (ends with blank line)
	if (ws_buf_.find("\r\n\r\n") == std::string::npos)
		return;

	// Parse Sec-WebSocket-Key header
	std::size_t key_pos = ws_buf_.find("Sec-WebSocket-Key:");
	if (key_pos == std::string::npos)
	{
		// Not a valid WebSocket upgrade request
		this->Close(true);
		return;
	}

	key_pos += 18; // skip "Sec-WebSocket-Key:"
	while (key_pos < ws_buf_.size() && ws_buf_[key_pos] == ' ')
		++key_pos;

	std::size_t key_end = ws_buf_.find_first_of("\r\n", key_pos);
	if (key_end == std::string::npos)
	{
		this->Close(true);
		return;
	}

	std::string key = ws_buf_.substr(key_pos, key_end - key_pos);

	// Preserve any WS frame bytes that arrived after the HTTP headers
	std::size_t header_end = ws_buf_.find("\r\n\r\n");
	ws_buf_ = ws_buf_.substr(header_end + 4);

	// Send HTTP 101 response (goes directly to socket send buffer)
	Client::Send(websocket::build_handshake_response(key));

	ws_handshake_done_ = true;
}

void EOClient::DecodeWsFrames()
{
	if (!this->Connected())
		return;

	// Drain new raw bytes from recv_buffer into ws_buf_
	while (this->recv_buffer_used > 0)
		ws_buf_ += this->Recv(std::min(this->recv_buffer_used, std::size_t(4096)));

	// Parse complete frames from ws_buf_
	std::size_t pos = 0;
	while (pos < ws_buf_.size())
	{
		std::string payload;
		uint8_t opcode = 0;
		std::size_t consumed = 0;

		if (!websocket::decode_frame(ws_buf_.c_str() + pos, ws_buf_.size() - pos, payload, opcode, consumed))
			break; // incomplete frame — wait for more data

		if (opcode == websocket::WS_OPCODE_CLOSE)
		{
			// RFC 6455: echo the close frame, then close
			Client::Send(websocket::wrap_close_frame());
			ws_buf_.clear();
			this->Close(true);
			return;
		}
		else if (opcode == websocket::WS_OPCODE_BINARY || opcode == websocket::WS_OPCODE_CONTINUATION)
		{
			ws_payload_buf_ += payload;
		}
		// Silently discard ping / pong / text frames

		pos += consumed;
	}

	if (pos > 0)
		ws_buf_.erase(0, pos);
}

std::string EOClient::WsRecv(std::size_t length)
{
	// Periodically compact the decoded payload buffer
	if (ws_payload_pos_ > 4096)
	{
		ws_payload_buf_.erase(0, ws_payload_pos_);
		ws_payload_pos_ = 0;
	}

	std::size_t available = ws_payload_buf_.size() - ws_payload_pos_;
	length = std::min(length, available);

	if (length == 0)
		return std::string();

	std::string ret = ws_payload_buf_.substr(ws_payload_pos_, length);
	ws_payload_pos_ += length;
	return ret;
}

void EOClient::Tick()
{
	std::string data;
	int done = false;
	int oldlength;

	if (this->upload_fh)
	{
		// Send more of the file instead of doing other tasks
		// (WebSocket clients use the single-packet path in Upload(), so this is TCP only)
		std::size_t upload_available = std::min(this->upload_size - this->upload_pos, Client::SendBufferRemaining());

		if (upload_available != 0)
		{
			upload_available = std::fread(&this->send_buffer[this->send_buffer_ppos + 1], 1, upload_available, this->upload_fh);

			// Dynamically rewrite the bytes of the map to enable PK
			if (this->upload_type == FILE_MAP && this->server()->world->config["GlobalPK"] && !this->server()->world->PKExcept(player->character->mapid))
			{
				if (this->upload_pos <= 0x03 && this->upload_pos + upload_available > 0x03)
					this->send_buffer[this->send_buffer_ppos + 1 + 0x03 - this->upload_pos] = (char)0xFF;

				if (this->upload_pos <= 0x03 && this->upload_pos + upload_available > 0x04)
					this->send_buffer[this->send_buffer_ppos + 1 + 0x04 - this->upload_pos] = static_cast<char>(0x01);

				if (this->upload_pos <= 0x1F && this->upload_pos + upload_available > 0x1F)
					this->send_buffer[this->send_buffer_ppos + 1 + 0x1F - this->upload_pos] = static_cast<char>(0x04);
			}

			this->upload_pos += upload_available;
			this->send_buffer_ppos += upload_available;
			this->send_buffer_used += upload_available;
		}
		else if (this->upload_pos == this->upload_size && this->SendBufferRemaining() == this->send_buffer.length())
		{
			using std::swap;

			std::fclose(this->upload_fh);
			this->upload_fh = 0;
			this->upload_pos = 0;
			this->upload_size = 0;

			// Place our temporary buffer back as the real one
			swap(this->send_buffer, this->send_buffer2);
			swap(this->send_buffer_gpos, this->send_buffer2_gpos);
			swap(this->send_buffer_ppos, this->send_buffer2_ppos);
			swap(this->send_buffer_used, this->send_buffer2_used);

			// We're not using this anymore...
			std::string empty;
			swap(this->send_buffer2, empty);
		}
	}
	else
	{
		if (this->websocket_)
		{
			if (!this->ws_handshake_done_)
			{
				this->DoWsHandshake();
				return;
			}

			this->DecodeWsFrames();
			data = this->WsRecv((this->packet_state == EOClient::ReadData) ? this->length : 1);
		}
		else
		{
			data = this->Recv((this->packet_state == EOClient::ReadData) ? this->length : 1);
		}

		while (data.length() > 0 && !done)
		{
			switch (this->packet_state)
			{
				case EOClient::ReadLen1:
					this->raw_length[0] = data[0];
					data[0] = '\0';
					data.erase(0, 1);
					this->packet_state = EOClient::ReadLen2;

					if (data.length() == 0)
					{
						break;
					}
					// fall through
				case EOClient::ReadLen2:
					this->raw_length[1] = data[0];
					data[0] = '\0';
					data.erase(0, 1);
					this->length = PacketProcessor::Number(this->raw_length[0], this->raw_length[1]);
					this->packet_state = EOClient::ReadData;

					if (data.length() == 0)
					{
						break;
					}
					// fall through
				case EOClient::ReadData:
					oldlength = this->data.length();
					this->data += data.substr(0, this->length);
					std::fill(data.begin(), data.begin() + std::min<std::size_t>(data.length(), this->length), '\0');
					data.erase(0, this->length);
					this->length -= this->data.length() - oldlength;

					if (this->length == 0)
					{
						this->Execute(this->data);

						std::fill(UTIL_RANGE(this->data), '\0');
						this->data.erase();
						this->packet_state = EOClient::ReadLen1;

						done = true;
					}
					break;

				default:
					// If the code ever gets here, something is broken, so we just reset the client's state.
					std::fill(UTIL_RANGE(data), '\0');
					std::fill(UTIL_RANGE(this->data), '\0');
					data.erase();
					this->data.erase();
					this->packet_state = EOClient::ReadLen1;
			}
		}
	}
}

void EOClient::InitNewSequence()
{
	this->seq_start = util::rand(0, 1757);
}

void EOClient::PingNewSequence()
{
	this->upcoming_seq_start = util::rand(0, 1757);
}

void EOClient::PongNewSequence()
{
	this->seq_start = this->upcoming_seq_start;
}

void EOClient::AccountReplyNewSequence()
{
	this->seq_start = util::rand(0, 240);
}

int EOClient::GetSeqStart()
{
	return this->seq_start;
}

std::pair<unsigned char, unsigned char> EOClient::GetSeqInitBytes()
{
	int s1_max = (this->seq_start + 13) / 7;
	int s1_min = std::max(0, (this->seq_start - 252 + 13 + 6) / 7);

	unsigned char s1 = util::rand(s1_min, s1_max);
	unsigned char s2 = this->seq_start - s1 * 7 + 13;

	return {s1, s2};
}

std::pair<unsigned short, unsigned char> EOClient::GetSeqUpdateBytes()
{
	int s1_max = this->upcoming_seq_start + 252;
	int s1_min = this->upcoming_seq_start;

	unsigned short s1 = util::rand(s1_min, s1_max);
	unsigned char s2 = s1 - this->upcoming_seq_start;

	return {s1, s2};
}

int EOClient::GenSequence()
{
	int result = std::uint32_t(this->seq_start + this->seq);

	this->seq = (this->seq + 1) % 10;

	return result;
}

int EOClient::GenUpcomingSequence()
{
	int result = std::uint32_t(this->upcoming_seq_start + this->seq);

	this->seq = (this->seq + 1) % 10;

	return result;
}

void EOClient::NewCreateID()
{
	this->create_id = this->server()->world->GenerateOperationID([](const EOClient* c) { return c->create_id; });
}

void EOClient::Execute(const std::string &data)
{
	if (data.length() < 2)
		return;

	if (!this->Connected())
		return;

	PacketReader reader(processor.Decode(data));

	this->LogPacket(reader.Family(), reader.Action(), reader.Length(), "RECV");

	if (reader.Family() == PACKET_INTERNAL)
	{
		Console::Wrn("Closing client connection sending a reserved packet ID: %s", static_cast<std::string>(this->GetRemoteAddr()).c_str());
		this->AsyncOpPending(false);
		this->Close();
		return;
	}

	if (reader.Family() != PACKET_F_INIT)
	{
		bool ping_reply = (reader.Family() == PACKET_CONNECTION && reader.Action() == PACKET_PING);

		if (ping_reply)
			this->PongNewSequence();

		int client_seq;
		int server_seq = this->GenSequence();

		if (server_seq >= 253)
			client_seq = reader.GetShort();
		else
			client_seq = reader.GetChar();

		if (this->server()->world->config["EnforceSequence"])
		{
			if (client_seq != server_seq)
			{
				Console::Wrn("Closing client connection sending invalid sequence: %s, Got %i, expected %i.", static_cast<std::string>(this->GetRemoteAddr()).c_str(), client_seq, server_seq);
				this->AsyncOpPending(false);
				this->Close();
				return;
			}
		}
	}
	else
	{
		this->GenSequence();
	}

	queue.AddAction(reader, 0.02, true);
}

bool EOClient::Upload(FileType type, int id, InitReply init_reply)
{
	char mapbuf[7];
	std::sprintf(mapbuf, "%05i", int(std::abs(id)));

	switch (type)
	{
		case FILE_MAP: return EOClient::Upload(type, std::string(server()->world->config["MapDir"]) + mapbuf + ".emf", init_reply);
		case FILE_ITEM: return EOClient::Upload(type, std::string(this->server()->world->config["EIF"]), init_reply);
		case FILE_NPC: return EOClient::Upload(type, std::string(this->server()->world->config["ENF"]),init_reply);
		case FILE_SPELL: return EOClient::Upload(type, std::string(this->server()->world->config["ESF"]), init_reply);
		case FILE_CLASS: return EOClient::Upload(type, std::string(this->server()->world->config["ECF"]), init_reply);
		default: return false;
	}
}

bool EOClient::Upload(FileType type, const std::string &filename, InitReply init_reply)
{
	using std::swap;

	if (this->upload_fh)
		throw std::runtime_error("Already uploading file");

	// For WebSocket clients, embed the entire file in one packet (reoserv-compatible).
	// Browsers receive each WS message as a discrete blob, so raw streaming bytes sent
	// as separate WS messages would not be recognizable as file data by the web client.
	if (this->websocket_)
	{
		FILE* fh = std::fopen(filename.c_str(), "rb");
		if (!fh)
			return false;

		if (std::fseek(fh, 0, SEEK_END) != 0)
		{
			std::fclose(fh);
			return false;
		}

		std::size_t file_size = static_cast<std::size_t>(std::ftell(fh));
		std::fseek(fh, 0, SEEK_SET);

		std::string file_data(file_size, '\0');
		std::fread(&file_data[0], 1, file_size, fh);
		std::fclose(fh);

		// Apply PK patch bytes directly into the raw file data before sending
		if (type == FILE_MAP && this->server()->world->config["GlobalPK"] && !this->server()->world->PKExcept(player->character->mapid))
		{
			if (file_size > 0x03) file_data[0x03] = static_cast<char>(0xFF);
			if (file_size > 0x04) file_data[0x04] = static_cast<char>(0x01);
			if (file_size > 0x1F) file_data[0x1F] = static_cast<char>(0x04);
		}

		PacketBuilder builder(PACKET_F_INIT, PACKET_A_INIT, 1 + (type != FILE_MAP ? 1 : 0) + file_size);
		builder.AddChar(init_reply);
		if (type != FILE_MAP)
			builder.AddChar(1);
		builder.AddString(file_data);

		LogPacket(PACKET_F_INIT, PACKET_A_INIT, builder.Length(), "UPLD");
		Client::Send(websocket::wrap_frame(this->processor.Encode(builder)));
		return true;
	}

	this->upload_fh = std::fopen(filename.c_str(), "rb");

	if (!this->upload_fh)
		return false;

	if (std::fseek(this->upload_fh, 0, SEEK_END) != 0)
	{
		std::fclose(this->upload_fh);
		return false;
	}

	this->upload_type = type;
	this->upload_pos = 0;
	this->upload_size = std::ftell(this->upload_fh);

	std::fseek(this->upload_fh, 0, SEEK_SET);

	std::size_t temp_buffer_size = this->send_buffer.size();

	// Allocate a power-of-two buffer size large enough to hold the file
	while (temp_buffer_size < this->upload_size + 6)
		temp_buffer_size *= 2;

	this->send_buffer2.resize(temp_buffer_size);
	this->send_buffer2_gpos = 0;
	this->send_buffer2_ppos = 0;
	this->send_buffer2_used = 0;

	swap(this->send_buffer, this->send_buffer2);
	swap(this->send_buffer_gpos, this->send_buffer2_gpos);
	swap(this->send_buffer_ppos, this->send_buffer2_ppos);
	swap(this->send_buffer_used, this->send_buffer2_used);

	// Build the file upload header packet (TCP: size announced, data streamed separately)
	PacketBuilder builder(PACKET_F_INIT, PACKET_A_INIT, 2);
	builder.AddChar(init_reply);

	if (type != FILE_MAP)
		builder.AddChar(1);

	builder.AddSize(this->upload_size);

	LogPacket(PACKET_F_INIT, PACKET_A_INIT, builder.Length(), "UPLD");
	Client::Send(builder);

	return true;
}

void EOClient::Send(const PacketBuilder &builder)
{
	std::lock_guard<std::mutex> lock(send_mutex);

	auto fam = PacketFamily(PacketProcessor::EPID(builder.GetID())[1]);
	auto act = PacketAction(PacketProcessor::EPID(builder.GetID())[0]);
	this->LogPacket(fam, act, builder.Length(), "SEND");

	std::string data = this->processor.Encode(builder);

	if (this->websocket_)
		data = websocket::wrap_frame(data);

	if (this->upload_fh)
	{
		// Stick any incoming data in to our temporary buffer
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
		Client::Send(data);
	}
}

EOClient::~EOClient()
{
	if (this->upload_fh)
	{
		std::fclose(this->upload_fh);
	}

	if (this->player)
	{
		delete this->player;
	}
}
