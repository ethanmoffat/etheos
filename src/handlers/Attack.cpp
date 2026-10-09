
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../map.hpp"
#include "../world.hpp"

#include <eolib/protocol/net/client/packets.hpp>

namespace Handlers
{

// Player attacking
void Attack_Use(Character* character, const client::AttackUseClientPacket& packet)
{
	Direction direction = packet.direction;
	Timestamp timestamp = packet.timestamp;

	int ts_diff = timestamp - character->timestamp;

	if (character->world->config["EnforceTimestamps"])
	{
		if (ts_diff < 48)
		{
			return;
		}
	}

	character->timestamp = timestamp;

	if (character->sitting != SitState::Stand)
		return;

	if (int(character->world->config["EnforceWeight"]) >= 1 && character->weight > character->maxweight)
		return;

	int limit_attack = character->world->config["LimitAttack"];

	if (limit_attack != 0 && character->attacks >= limit_attack)
		return;

	if (!character->world->config["EnforceTimestamps"] || ts_diff >= 60)
	{
		direction = character->direction;
	}

	character->Attack(direction);
}

PACKET_HANDLER_REGISTER(Attack)
	Register(Attack_Use, Playing, 0.5);
PACKET_HANDLER_REGISTER_END(Attack)

}
