
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../eoclient.hpp"

#include <eolib/protocol/net/client/packets.hpp>

namespace Handlers
{

// Confirmation of initialization data
void Connection_Accept(EOClient* client, const net::client::ConnectionAcceptClientPacket& packet)
{
	if (client->server_encryption_multiple != packet.server_encryption_multiple
	 || client->client_encryption_multiple != packet.client_encryption_multiple
	 || client->id != static_cast<unsigned int>(packet.player_id))
	{
		client->Close();
		return;
	}

	client->MarkAccepted();
}

// Ping reply
void Connection_Ping(EOClient* client, const net::client::ConnectionPingClientPacket& packet)
{
	(void)packet;

	if (client->needpong)
	{
		client->needpong = false;
	}
}

PACKET_HANDLER_REGISTER(PACKET_CONNECTION)
	Register(Connection_Accept, Menu);
	Register(Connection_Ping, Any | OutOfBand);
PACKET_HANDLER_REGISTER_END(PACKET_CONNECTION)

}
