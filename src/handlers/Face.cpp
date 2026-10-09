
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

// Player changing direction
void Face_Player(Character* character, const client::FacePlayerClientPacket& packet)
{
	const Direction direction = packet.direction;

	if (character->sitting != SitState::Stand)
	{
		return;
	}

	if (direction <= Direction::Right)
	{
		character->map->Face(character, direction);
	}
}

PACKET_HANDLER_REGISTER(Face)
	Register(Face_Player, Playing, 0.12);
PACKET_HANDLER_REGISTER_END(Face)

}
