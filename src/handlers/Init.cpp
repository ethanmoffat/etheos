
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
#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <string>

namespace Handlers
{

void Init_Init(EOClient* client, const net::client::InitInitClientPacket& packet)
{
	client->version = packet.version.patch;

	try
	{
		client->hdid = int(util::to_uint_raw(packet.hdid));
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
		if (ban_expires == 0)
		{
			client->Send(net::server::InitInitServerPacket::ForBannedPermanent());
		}
		else
		{
			net::server::InitInitServerPacket::ReplyCodeDataBanned::BanTypeDataTemporary temporary;
			temporary.minutes_remaining = int(std::min(255.0, std::ceil(double(ban_expires - std::time(0)) / 60.0)));
			client->Send(net::server::InitInitServerPacket::ForBannedTemporary(temporary));
		}

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

	// The official server also rejects any protocol version other than 112
	bool accepted_version = (client->version >= minversion)
	                     && ((maxversion < 0) || (client->version <= maxversion))
	                     && (packet.protocol_version == net::client::InitInitClientPacket::DEFAULT_PROTOCOL_VERSION);

	if (client->server()->world->config["CheckVersion"] && !accepted_version)
	{
		net::server::InitInitServerPacket::ReplyCodeDataOutOfDate out_of_date;
		out_of_date.version.major = 0;
		out_of_date.version.minor = 0;
		out_of_date.version.patch = minversion;

		client->Send(net::server::InitInitServerPacket::ForOutOfDate(out_of_date));
		client->Close();
		return;
	}

	client->server_encryption_multiple = util::rand(6,12);
	client->client_encryption_multiple = util::rand(6,12);

	auto sequence_start = client->InitNewSequence();

	net::server::InitInitServerPacket::ReplyCodeDataOk ok;
	ok.seq1 = sequence_start.Seq1();
	ok.seq2 = sequence_start.Seq2();
	ok.server_encryption_multiple = client->server_encryption_multiple;
	ok.client_encryption_multiple = client->client_encryption_multiple;
	ok.player_id = client->id;
	ok.challenge_response = eolib::encrypt::ServerVerifier::Hash(packet.challenge);

	client->Send(net::server::InitInitServerPacket::ForOk(ok));

	client->state = EOClient::Initialized;

	return;
}

PACKET_HANDLER_REGISTER(PACKET_F_INIT)
	Register(Init_Init, Uninitialized);
PACKET_HANDLER_REGISTER_END(PACKET_F_INIT)

}
