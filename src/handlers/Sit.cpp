
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

namespace Handlers
{

void Sit_Request(Character* character, const client::SitRequestClientPacket& packet)
{
	if (packet.sit_action == client::SitAction::Sit && character->sitting == SitState::Stand)
	{
		server::SitReplyServerPacket reply;
		reply.player_id = character->PlayerID();
		reply.coords = character->loc;
		reply.direction = character->direction;
		character->Send(reply);
		character->Sit(SitState::Floor);
	}
	else if (character->sitting == SitState::Floor)
	{
		server::SitCloseServerPacket reply;
		reply.player_id = character->PlayerID();
		reply.coords = character->loc;
		character->Send(reply);
		character->Stand();
	}
}

PACKET_HANDLER_REGISTER(PACKET_SIT)
	Register(Sit_Request, Playing);
PACKET_HANDLER_REGISTER_END(PACKET_SIT)

}
