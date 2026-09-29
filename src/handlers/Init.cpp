
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../config.hpp"
#include "../eoclient.hpp"
#include "../eoserver.hpp"
#include "../packet.hpp"
#include "../timer.hpp"
#include "../world.hpp"

#include "../console.hpp"
#include "../util.hpp"

#include <eolib/encrypt/server_verifier.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <string>

namespace Handlers
{

void Init_Init(EOClient *client, PacketReader &reader)
{
	PacketBuilder reply(PACKET_F_INIT, PACKET_A_INIT, 10);

	unsigned int challenge;
	unsigned int response;

	challenge = reader.GetThree();

	reader.GetChar(); // ?
	reader.GetChar(); // ?
	client->version = reader.GetChar();
	reader.GetChar(); // ?
	reader.GetChar(); // ?

	try
	{
		client->hdid = int(util::to_uint_raw(reader.GetEndString()));
	}
	catch (std::invalid_argument&)
	{
		client->Close();
		return;
	}

	int pc_connections = 0;

	UTIL_FOREACH(client->server()->clients, checkclient)
	{
		EOClient *checkeoclient = static_cast<EOClient *>(checkclient);

		if (checkeoclient->hdid == client->hdid && checkeoclient->GetRemoteAddr() == client->GetRemoteAddr())
		{
			++pc_connections;
		}
	}

	const bool ignore_hdid = client->server()->world->config["IgnoreHDID"];

	if (!ignore_hdid)
	{
		const int max_per_pc = int(client->server()->world->config["MaxConnectionsPerPC"]);

		if (max_per_pc != 0 && pc_connections > max_per_pc)
		{
			char errbuf[64];
			std::snprintf(errbuf, sizeof errbuf, "too many connections from this PC: %08x", client->hdid);
			client->server()->RecordClientRejection(client->GetRemoteAddr(), errbuf);
			client->Close(true);
		}
	}

	int ban_expires;
	IPAddress remote_addr = client->GetRemoteAddr();
	if ((ban_expires = client->server()->world->CheckBan(0, &remote_addr, ignore_hdid ? 0 : &client->hdid)) != -1)
	{
		reply.AddByte(INIT_BANNED);
		if (ban_expires == 0)
		{
			reply.AddByte(INIT_BAN_PERM);
		}
		else
		{
			int mins_remaining = int(std::min(255.0, std::ceil(double(ban_expires - std::time(0)) / 60.0)));
			reply.AddByte(INIT_BAN_TEMP);
			reply.AddByte(mins_remaining);
		}
		client->Send(reply);
		client->Close();
		return;
	}

	int minversion = client->server()->world->config["MinVersion"];
	if (!minversion)
	{
		minversion = 28;
	}

	int maxversion = client->server()->world->config["MaxVersion"];
	if (!maxversion)
	{
		maxversion = 28;
	}

	bool accepted_version = client->version >= minversion && (maxversion < 0 || client->version <= maxversion);

	if (client->server()->world->config["CheckVersion"] && !accepted_version)
	{
		reply.AddByte(INIT_OUT_OF_DATE);
		reply.AddChar(0);
		reply.AddChar(0);
		reply.AddChar(minversion);
		client->Send(reply);
		client->Close();
		return;
	}

	response = static_cast<unsigned int>(eolib::encrypt::ServerVerifier::Hash(static_cast<int>(challenge)));

	int emulti_e = util::rand(6,12);
	int emulti_d = util::rand(6,12);

	auto sequence_start = client->InitNewSequence();

	reply.AddByte(INIT_OK);
	reply.AddByte(sequence_start.Seq1());
	reply.AddByte(sequence_start.Seq2());
	reply.AddByte(emulti_e);
	reply.AddByte(emulti_d);
	reply.AddShort(client->id);
	reply.AddThree(response);

	client->server_encryption_multiple = emulti_e;
	client->client_encryption_multiple = emulti_d;

	client->Send(reply);

	client->state = EOClient::Initialized;

	return;
}

PACKET_HANDLER_REGISTER(PACKET_F_INIT)
	Register(PACKET_A_INIT, Init_Init, Uninitialized);
PACKET_HANDLER_REGISTER_END(PACKET_F_INIT)

}
