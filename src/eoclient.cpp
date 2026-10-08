
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "eoclient.hpp"

#include "character.hpp"
#include "config.hpp"
#include "eoclient.hpp"
#include "eodata.hpp"
#include "eodata_file.hpp"
#include "eoserver.hpp"
#include "map.hpp"
#include "packet.hpp"
#include "player.hpp"
#include "timer.hpp"
#include "world.hpp"
#include "handlers/handlers.hpp"

#include "console.hpp"
#include "socket.hpp"

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
#include <stdexcept>
#include <string>
#include <utility>

namespace net = eolib::protocol::net;
namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

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

// The largest packet the server can send (the biggest 2-byte length plus the 2 length bytes) must fit
static_assert(Client::SendBufferMaxSize >= (eolib::data::EoNumericLimits::ShortMax - 1) + 2);

void EOClient::Initialize()
{
	this->server_encryption_multiple = 0;
	this->client_encryption_multiple = 0;
	this->id = this->server()->world->GenerateClientID();
	this->create_id = 0;
	this->data.reserve(EOClient::RecvBufferInitialSize);
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

bool EOClient::RecvUpTo(std::vector<std::uint8_t>& buffer, std::size_t length)
{
	if (buffer.size() >= length)
		return true;

	// reserve is exact, while growing through resize may over-allocate
	if (length > buffer.capacity())
		buffer.reserve(length);

	this->Recv(buffer, length - buffer.size());

	return buffer.size() == length;
}

void EOClient::Tick()
{
	if (!this->RecvUpTo(this->length_bytes, 2))
		return;

	const std::size_t length = eolib::data::NumberEncoder::DecodeNumber(this->length_bytes.data(), 2);

	if (!this->RecvUpTo(this->data, length))
		return;

	this->Execute(this->data.data(), length);

	this->data.clear();
	this->length_bytes.clear();
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
static bool IsInitInit(const std::uint8_t* data, std::size_t length)
{
	return length >= 2
		&& data[0] == static_cast<std::uint8_t>(net::PacketAction::Init)
		&& data[1] == static_cast<std::uint8_t>(net::PacketFamily::Init);
}

void EOClient::EncryptPacket(std::uint8_t* body, std::size_t length, int multiple)
{
	if (multiple == 0 || IsInitInit(body, length))
		return;

	eolib::encrypt::DataEncrypter::SwapMultiples(body, length, multiple);
	eolib::encrypt::DataEncrypter::Interleave(body, length);
	eolib::encrypt::DataEncrypter::FlipMsb(body, length);
}

void EOClient::DecryptPacket(std::uint8_t* data, std::size_t length, int multiple)
{
	if (multiple == 0 || IsInitInit(data, length))
		return;

	eolib::encrypt::DataEncrypter::FlipMsb(data, length);
	eolib::encrypt::DataEncrypter::Deinterleave(data, length);
	eolib::encrypt::DataEncrypter::SwapMultiples(data, length, multiple);
}

void EOClient::Execute(std::uint8_t* data, std::size_t length)
{
	if (length < 2)
		return;

	if (!this->Connected())
		return;

	EOClient::DecryptPacket(data, length, this->client_encryption_multiple);

	auto action = net::PacketAction(data[0]);
	auto family = net::PacketFamily(data[1]);

	this->LogPacket(PacketFamily(family), PacketAction(action), length, "RECV");

	std::size_t payload_start = 2;

	if (family != net::PacketFamily::Init)
	{
		// A client that replies before it has been pinged keeps its current sequence start
		if (family == net::PacketFamily::Connection && action == net::PacketAction::Ping && this->upcoming_sequence_start)
			this->sequencer.SetSequenceStart(*this->upcoming_sequence_start);

		int server_seq = this->sequencer.NextSequence();

		std::size_t seq_length = (server_seq >= int(eolib::data::EoNumericLimits::CharMax)) ? 2 : 1;
		seq_length = std::min(seq_length, length - payload_start);

		int client_seq = eolib::data::NumberEncoder::DecodeNumber(data + payload_start, seq_length);
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

	queue.AddAction(QueuedPacket{family, action, std::vector<std::uint8_t>(data + payload_start, data + length)}, 0.02, true);
}

template <typename TData> static TData WithMapFile(server::MapFile map_file)
{
	TData data;
	data.map_file = std::move(map_file);
	return data;
}

template <typename TData> static TData WithPubFile(server::PubFile pub_file)
{
	TData data;
	data.pub_file = std::move(pub_file);
	return data;
}

bool EOClient::UploadMap(int map_id, server::InitReply reply_code)
{
	char mapbuf[7];
	std::snprintf(mapbuf, sizeof mapbuf, "%05i", int(std::abs(map_id)));

	server::MapFile map_file;

	if (!EODataFile::Read(std::string(this->server()->world->config["MapDir"]) + mapbuf + ".emf", map_file.content))
		return false;

	if (this->server()->world->config["GlobalPK"] && !this->server()->world->PKExcept(map_id))
		Map::PatchGlobalPK(map_file.content);

	using Init = server::InitInitServerPacket;

	switch (reply_code)
	{
		case server::InitReply::FileEmf:
			this->Send(Init::ForFileEmf(WithMapFile<Init::ReplyCodeDataFileEmf>(std::move(map_file))));
			break;
		case server::InitReply::WarpMap:
			this->Send(Init::ForWarpMap(WithMapFile<Init::ReplyCodeDataWarpMap>(std::move(map_file))));
			break;
		case server::InitReply::MapMutation:
			this->Send(Init::ForMapMutation(WithMapFile<Init::ReplyCodeDataMapMutation>(std::move(map_file))));
			break;
		default:
			throw std::invalid_argument("Reply code " + server::ToString(reply_code) + " isn't a map upload");
	}

	return true;
}

bool EOClient::UploadPubFile(client::FileType file_type)
{
	using Init = server::InitInitServerPacket;

	// etheos serves each pub type as a single file
	constexpr int pub_file_id = 1;

	const char* config_key;

	switch (file_type)
	{
		case client::FileType::Eif: config_key = "EIF"; break;
		case client::FileType::Enf: config_key = "ENF"; break;
		case client::FileType::Esf: config_key = "ESF"; break;
		case client::FileType::Ecf: config_key = "ECF"; break;
		default: return false;
	}

	server::PubFile pub_file;
	pub_file.file_id = pub_file_id;

	if (!EODataFile::Read(std::string(this->server()->world->config[config_key]), pub_file.content))
		return false;

	switch (file_type)
	{
		case client::FileType::Eif: this->Send(Init::ForFileEif(WithPubFile<Init::ReplyCodeDataFileEif>(std::move(pub_file)))); break;
		case client::FileType::Enf: this->Send(Init::ForFileEnf(WithPubFile<Init::ReplyCodeDataFileEnf>(std::move(pub_file)))); break;
		case client::FileType::Esf: this->Send(Init::ForFileEsf(WithPubFile<Init::ReplyCodeDataFileEsf>(std::move(pub_file)))); break;
		case client::FileType::Ecf: this->Send(Init::ForFileEcf(WithPubFile<Init::ReplyCodeDataFileEcf>(std::move(pub_file)))); break;
		default: return false;
	}

	return true;
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

	std::vector<std::uint8_t> frame;
	frame.reserve(bytes.size() + 4);
	frame.assign({0, 0, static_cast<std::uint8_t>(packet.Action()), static_cast<std::uint8_t>(packet.Family())});
	frame.insert(frame.end(), bytes.begin(), bytes.end());

	this->SendFrame(frame);
}

void EOClient::SendFrame(std::vector<std::uint8_t>& frame)
{
	std::uint8_t* body = frame.data() + 2;
	const std::size_t body_length = frame.size() - 2;

	this->LogPacket(PacketFamily(body[1]), PacketAction(body[0]), body_length - 2, "SEND");

	EOClient::EncryptPacket(body, body_length, this->server_encryption_multiple);

	const auto encoded_length = eolib::data::NumberEncoder::EncodeNumber(int(body_length));
	frame[0] = encoded_length[0];
	frame[1] = encoded_length[1];

	Client::Send(frame);
}

EOClient::~EOClient()
{
	if (this->player)
	{
		delete this->player;
	}
}
