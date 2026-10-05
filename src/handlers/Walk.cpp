
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../eodata.hpp"
#include "../npc.hpp"
#include "../packet.hpp"
#include "../world.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/client/structs.hpp>

namespace Handlers
{

static void walk_common(Character* character, const client::WalkAction& walk, Map::WalkResult (Character::*f)(Direction))
{
	const Direction direction = walk.direction;
	const Timestamp timestamp = walk.timestamp;
	const int x = walk.coords.x;
	const int y = walk.coords.y;
	Map::WalkResult walk_result = Map::WalkFail;

	if (character->world->config["EnforceTimestamps"])
	{
		if (timestamp - character->timestamp < 36)
		{
			return;
		}
	}

	character->timestamp = timestamp;

	if (character->sitting != SitState::Stand)
	{
		return;
	}

	if (direction <= Direction::Right)
	{
		character->npc = 0;
		character->npc_type = pub::NpcType::Friendly;
		character->board = 0;
		character->jukebox_open = false;

		if (character->trading)
		{
			PacketBuilder builder(PACKET_TRADE, PACKET_CLOSE, 2);
			builder.AddShort(character->PlayerID());
			character->trade_partner->Send(builder);

			character->trading = false;
			character->trade_inventory.clear();
			character->trade_agree = false;

			character->trade_partner->trading = false;
			character->trade_partner->trade_inventory.clear();
			character->trade_agree = false;

			character->CheckQuestRules();
			character->trade_partner->CheckQuestRules();

			character->trade_partner->trade_partner = 0;
			character->trade_partner = 0;
		}

		walk_result = (character->*f)(direction);
	}

	if (walk_result == Map::WalkFail || (walk_result == Map::WalkOK && (character->loc.x != x || character->loc.y != y)))
	{
		character->Refresh();
	}
}

// Player walking (admin)
void Walk_Admin(Character* character, const client::WalkAdminClientPacket& packet)
{
	if (character->SourceDutyAccess() < int(character->world->admin_config["nowall"]))
		return;

	walk_common(character, packet.walk_action, &Character::AdminWalk);
}

// Player walking (normal)
void Walk_Player(Character* character, const client::WalkPlayerClientPacket& packet)
{
	walk_common(character, packet.walk_action, &Character::Walk);
}

// Player walking (ghost)
void Walk_Spec(Character* character, const client::WalkSpecClientPacket& packet)
{
	walk_common(character, packet.walk_action, &Character::Walk);
}

PACKET_HANDLER_REGISTER(PACKET_WALK)
	Register(Walk_Admin, Playing, 0.4);
	Register(Walk_Player, Playing, 0.4);
	Register(Walk_Spec, Playing, 0.4);
PACKET_HANDLER_REGISTER_END(PACKET_WALK)

}
