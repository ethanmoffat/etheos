
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
#include "handlers/handlers.hpp"
#include "map.hpp"

#include "console.hpp"
#include "socket.hpp"
#include "util.hpp"

#include <eolib/data/eo_numeric_limits.hpp>
#include <eolib/data/eo_writer.hpp>
#include <eolib/data/number_encoder.hpp>
#include <eolib/encrypt/data_encrypter.hpp>
#include <eolib/errors.hpp>
#include <eolib/protocol/net/enums.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace net = eolib::protocol::net;

void ActionQueue::AddAction(QueuedPacket packet, double time, bool auto_queue)
{
	this->queue.emplace(new ActionQueue_Action(std::move(packet), time, auto_queue));
}

void ActionQueue::Hold(double seconds, unsigned short allow_states, std::function<void(EOClient&)> on_release)
{
	if (this->hold)
		throw std::logic_error("Action queue is already on hold");

	this->hold = HoldState{this->queue.size(), false, seconds, allow_states, std::move(on_release)};
}

std::size_t ActionQueue::Size() const
{
	return this->queue.size();
}

void ActionQueue::Pump(EOClient* client, double now)
{
	if (this->next > now)
		return;

	if (this->hold && this->hold->ahead == 0)
	{
		this->PumpHold(client, now);
		return;
	}

	if (this->queue.empty())
		return;

	std::unique_ptr<ActionQueue_Action> action = std::move(this->queue.front());
	this->queue.pop();

	if (this->hold)
		--this->hold->ahead;

	this->next = now + action->time;

	Handlers::Handle(client, action->packet, !action->auto_queue);
}

void ActionQueue::PumpHold(EOClient* client, double now)
{
	if (!this->hold->armed)
	{
		this->hold->armed = true;
		this->next = now + this->hold->duration;
		return;
	}

	HoldState hold = std::move(*this->hold);
	this->hold.reset();
	this->next = now;

	if (Handlers::packet_handler_register::StateCheck(client, hold.allow_states))
		hold.on_release(*client);
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
	this->SetSendBufferLimit(EOClient::SendBufferLimit);
	this->server_encryption_multiple = 0;
	this->client_encryption_multiple = 0;
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

void EOClient::Tick()
{
	std::string data;
	int done = false;
	int oldlength;

	data = this->Recv((this->packet_state == EOClient::ReadData) ? this->length : 1);

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
				this->length = eolib::data::NumberEncoder::DecodeNumber(this->raw_length, 2);
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

eolib::packet::InitSequenceStart EOClient::InitNewSequence()
{
	auto start = eolib::packet::InitSequenceStart::Generate();
	this->sequencer.SetSequenceStart(start);
	return start;
}

eolib::packet::PingSequenceStart EOClient::PingNewSequence()
{
	auto start = eolib::packet::PingSequenceStart::Generate();
	this->upcoming_sequence_start = start;
	return start;
}

eolib::packet::AccountReplySequenceStart EOClient::AccountReplyNewSequence()
{
	auto start = eolib::packet::AccountReplySequenceStart::Generate();
	this->sequencer.SetSequenceStart(start);
	return start;
}

void EOClient::NewCreateID()
{
	this->create_id = this->server()->world->GenerateOperationID([](const EOClient* c) { return c->create_id; });
}

// Init_Init is exchanged before encryption is set up, so it's never encrypted
static bool IsInitInit(const std::string& data)
{
	return data.length() >= 2
		&& static_cast<unsigned char>(data[0]) == static_cast<unsigned char>(net::PacketAction::Init)
		&& static_cast<unsigned char>(data[1]) == static_cast<unsigned char>(net::PacketFamily::Init);
}

void EOClient::EncryptPacket(std::string& body, int multiple)
{
	if (multiple == 0 || IsInitInit(body))
		return;

	eolib::encrypt::DataEncrypter::SwapMultiples(body, multiple);
	eolib::encrypt::DataEncrypter::Interleave(body);
	eolib::encrypt::DataEncrypter::FlipMsb(body);
}

void EOClient::DecryptPacket(std::string& data, int multiple)
{
	if (multiple == 0 || IsInitInit(data))
		return;

	eolib::encrypt::DataEncrypter::FlipMsb(data);
	eolib::encrypt::DataEncrypter::Deinterleave(data);
	eolib::encrypt::DataEncrypter::SwapMultiples(data, multiple);
}

void EOClient::Execute(const std::string &data)
{
	if (data.length() < 2)
		return;

	if (!this->Connected())
		return;

	std::string decrypted = data;
	EOClient::DecryptPacket(decrypted, this->client_encryption_multiple);

	auto action = net::PacketAction(static_cast<unsigned char>(decrypted[0]));
	auto family = net::PacketFamily(static_cast<unsigned char>(decrypted[1]));

	this->LogPacket(PacketFamily(family), PacketAction(action), decrypted.length(), "RECV");

	std::size_t payload_start = 2;

	if (family != net::PacketFamily::Init)
	{
		// A client that replies before it has been pinged keeps its current sequence start
		if (family == net::PacketFamily::Connection && action == net::PacketAction::Ping && this->upcoming_sequence_start)
			this->sequencer.SetSequenceStart(*this->upcoming_sequence_start);

		int server_seq = this->sequencer.NextSequence();

		std::size_t seq_length = (server_seq >= int(eolib::data::EoNumericLimits::CharMax)) ? 2 : 1;
		seq_length = std::min(seq_length, decrypted.length() - payload_start);

		int client_seq = eolib::data::NumberEncoder::DecodeNumber(reinterpret_cast<const std::uint8_t*>(decrypted.data() + payload_start), seq_length);
		payload_start += seq_length;

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
		this->sequencer.NextSequence();
	}

	queue.AddAction(QueuedPacket{family, action, decrypted.substr(payload_start)}, 0.02, true);
}

template <typename TData> static TData WithMapFile(net::server::MapFile map_file)
{
	TData data;
	data.map_file = std::move(map_file);
	return data;
}

template <typename TData> static TData WithPubFile(net::server::PubFile pub_file)
{
	TData data;
	data.pub_file = std::move(pub_file);
	return data;
}

bool EOClient::ReadFile(const std::string& filename, std::vector<std::uint8_t>& content)
{
	std::ifstream file(filename, std::ios::binary);

	if (!file)
		return false;

	content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

	return !file.bad();
}

bool EOClient::UploadMap(int map_id, net::server::InitReply reply_code)
{
	char mapbuf[7];
	std::snprintf(mapbuf, sizeof mapbuf, "%05i", int(std::abs(map_id)));

	net::server::MapFile map_file;

	if (!EOClient::ReadFile(std::string(this->server()->world->config["MapDir"]) + mapbuf + ".emf", map_file.content))
		return false;

	if (this->server()->world->config["GlobalPK"] && !this->server()->world->PKExcept(map_id))
		Map::PatchGlobalPK(map_file.content);

	net::server::InitInitServerPacket packet;
	packet.reply_code = reply_code;

	switch (reply_code)
	{
		case net::server::InitReply::FileEmf:
			packet.reply_code_data = WithMapFile<net::server::InitInitServerPacket::ReplyCodeDataFileEmf>(std::move(map_file));
			break;
		case net::server::InitReply::WarpMap:
			packet.reply_code_data = WithMapFile<net::server::InitInitServerPacket::ReplyCodeDataWarpMap>(std::move(map_file));
			break;
		case net::server::InitReply::MapMutation:
			packet.reply_code_data = WithMapFile<net::server::InitInitServerPacket::ReplyCodeDataMapMutation>(std::move(map_file));
			break;
		default:
			throw std::invalid_argument("Reply code " + net::server::ToString(reply_code) + " isn't a map upload");
	}

	this->Send(packet);

	return true;
}

bool EOClient::UploadPubFile(net::client::FileType file_type)
{
	// etheos serves each pub type as a single file
	constexpr int pub_file_id = 1;

	std::string filename;
	net::server::InitReply reply_code;

	switch (file_type)
	{
		case net::client::FileType::Eif: filename = std::string(this->server()->world->config["EIF"]); reply_code = net::server::InitReply::FileEif; break;
		case net::client::FileType::Enf: filename = std::string(this->server()->world->config["ENF"]); reply_code = net::server::InitReply::FileEnf; break;
		case net::client::FileType::Esf: filename = std::string(this->server()->world->config["ESF"]); reply_code = net::server::InitReply::FileEsf; break;
		case net::client::FileType::Ecf: filename = std::string(this->server()->world->config["ECF"]); reply_code = net::server::InitReply::FileEcf; break;
		default: return false;
	}

	net::server::PubFile pub_file;
	pub_file.file_id = pub_file_id;

	if (!EOClient::ReadFile(filename, pub_file.content))
		return false;

	net::server::InitInitServerPacket packet;
	packet.reply_code = reply_code;

	switch (file_type)
	{
		case net::client::FileType::Eif: packet.reply_code_data = WithPubFile<net::server::InitInitServerPacket::ReplyCodeDataFileEif>(std::move(pub_file)); break;
		case net::client::FileType::Enf: packet.reply_code_data = WithPubFile<net::server::InitInitServerPacket::ReplyCodeDataFileEnf>(std::move(pub_file)); break;
		case net::client::FileType::Esf: packet.reply_code_data = WithPubFile<net::server::InitInitServerPacket::ReplyCodeDataFileEsf>(std::move(pub_file)); break;
		default: packet.reply_code_data = WithPubFile<net::server::InitInitServerPacket::ReplyCodeDataFileEcf>(std::move(pub_file)); break;
	}

	this->Send(packet);

	return true;
}

void EOClient::Send(const PacketBuilder &builder)
{
	this->SendBody(builder.Get().substr(2));
}

void EOClient::Send(const net::Packet& packet)
{
	eolib::data::EoWriter writer;

	try
	{
		packet.Serialize(writer);
	}
	catch (const eolib::SerializationError& e)
	{
		Console::Err("Failed to serialize packet %s_%s for %s: %s", net::ToString(packet.Family()).c_str(), net::ToString(packet.Action()).c_str(), static_cast<std::string>(this->GetRemoteAddr()).c_str(), e.what());
		return;
	}

	const auto& bytes = writer.Data();

	std::string body;
	body.reserve(bytes.size() + 2);
	body += char(packet.Action());
	body += char(packet.Family());
	body.append(bytes.begin(), bytes.end());

	this->SendBody(std::move(body));
}

void EOClient::SendBody(std::string body)
{
	std::lock_guard<std::mutex> lock(send_mutex);

	this->LogPacket(PacketFamily(static_cast<unsigned char>(body[1])), PacketAction(static_cast<unsigned char>(body[0])), body.length() - 2, "SEND");

	EOClient::EncryptPacket(body, this->server_encryption_multiple);

	auto length = eolib::data::NumberEncoder::EncodeNumber(int(body.length()));

	std::string data;
	data.reserve(body.length() + 2);
	data += char(length[0]);
	data += char(length[1]);
	data += body;

	Client::Send(data);
}

EOClient::~EOClient()
{
	if (this->player)
	{
		delete this->player;
	}
}
