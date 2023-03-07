
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../eoclient.hpp"

namespace Handlers
{

// Confirmation of initialization data
void Connection_Accept(EOClient *client, PacketReader &reader)
{
	auto emulti_d = reader.GetShort();
	auto emulti_e = reader.GetShort();
	auto client_id = reader.GetShort();

	auto multis = client->processor.GetEMulti();
	if (multis.first != emulti_e || multis.second != emulti_d || client->id != client_id)
	{
		client->Close();
		return;
	}

	client->MarkAccepted();
}

// Ping reply
void Connection_Ping(EOClient *client, PacketReader &reader)
{
	if (reader.GetEndString() != "k")
	{
		client->Close();
		return;
	}

	if (client->needpong)
	{
		client->needpong = false;
	}
}

PACKET_HANDLER_REGISTER(PACKET_CONNECTION)
	Register(PACKET_ACCEPT, Connection_Accept, Menu);
	Register(PACKET_PING, Connection_Ping, Any | OutOfBand);
PACKET_HANDLER_REGISTER_END(PACKET_CONNECTION)

}
