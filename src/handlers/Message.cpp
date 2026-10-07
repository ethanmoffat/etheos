
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

// User sending a ping request (#ping)
void Message_Ping(Character* character, const client::MessagePingClientPacket&)
{
	character->Send(server::MessagePongServerPacket());
}

PACKET_HANDLER_REGISTER(PACKET_MESSAGE)
	Register(Message_Ping, Playing | OutOfBand);
PACKET_HANDLER_REGISTER_END(PACKET_MESSAGE)

}
