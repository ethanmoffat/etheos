
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../map.hpp"

#include <eolib/protocol/net/client/packets.hpp>

namespace Handlers
{

// User opening a door
void Door_Open(Character* character, const client::DoorOpenClientPacket& packet)
{
	character->map->OpenDoor(character, packet.coords);
}

PACKET_HANDLER_REGISTER(PACKET_DOOR)
	Register(Door_Open, Playing);
PACKET_HANDLER_REGISTER_END(PACKET_DOOR)

}
