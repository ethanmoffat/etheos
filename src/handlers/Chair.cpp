
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../map.hpp"

#include "../util.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

namespace Handlers
{

// Player sitting on a chair
void Chair_Request(Character* character, const client::ChairRequestClientPacket& packet)
{
	const auto* sit = packet.AsSit();

	if (sit && character->sitting == SitState::Stand)
	{
		const int x = sit->coords.x;
		const int y = sit->coords.y;

		if (util::path_length(character->loc.x, character->loc.y, x, y) > 1)
		{
			return;
		}

		UTIL_FOREACH(character->map->characters, c)
		{
			if (c->loc.x == x && c->loc.y == y)
			{
				return;
			}
		}

		switch (character->map->GetSpec(x, y))
		{
			case emf::MapTileSpec::ChairDown:
				if (character->loc.y == y+1 && character->loc.x == x)
				{
					character->direction = Direction::Down;
				}
				else
				{
					return;
				}
				break;

			case emf::MapTileSpec::ChairUp:
				if (character->loc.y == y-1 && character->loc.x == x)
				{
					character->direction = Direction::Up;
				}
				else
				{
					return;
				}
				break;

			case emf::MapTileSpec::ChairLeft:
				if (character->loc.y == y && character->loc.x == x-1)
				{
					character->direction = Direction::Left;
				}
				else
				{
					return;
				}
				break;

			case emf::MapTileSpec::ChairRight:
				if (character->loc.y == y && character->loc.x == x+1)
				{
					character->direction = Direction::Right;
				}
				else
				{
					return;
				}
				break;

			case emf::MapTileSpec::ChairDownRight:
				if (character->loc.y == y && character->loc.x == x+1)
				{
					character->direction = Direction::Right;
				}
				else if (character->loc.y == y+1 && character->loc.x == x)
				{
					character->direction = Direction::Down;
				}
				else
				{
					return;
				}
				break;

			case emf::MapTileSpec::ChairUpLeft:
				if (character->loc.y == y && character->loc.x == x-1)
				{
					character->direction = Direction::Left;
				}
				else if (character->loc.y == y-1 && character->loc.x == x)
				{
					character->direction = Direction::Up;
				}
				else
				{
					return;
				}
				break;

			case emf::MapTileSpec::ChairAll:
				if (character->loc.y == y && character->loc.x == x+1)
				{
					character->direction = Direction::Right;
				}
				else if (character->loc.y == y && character->loc.x == x-1)
				{
					character->direction = Direction::Left;
				}
				else if (character->loc.y == y-1 && character->loc.x == x)
				{
					character->direction = Direction::Up;
				}
				else if (character->loc.y == y+1 && character->loc.x == x)
				{
					character->direction = Direction::Down;
				}
				else
				{
					return;
				}
				break;

			default:
				return;
		}

		character->loc.x = x;
		character->loc.y = y;

		server::ChairReplyServerPacket reply;
		reply.player_id = character->PlayerID();
		reply.coords = character->loc;
		reply.direction = character->direction;
		character->Send(reply);
		character->Sit(SitState::Chair);
	}
	else if (character->sitting == SitState::Chair)
	{
		switch (character->direction)
		{
			case Direction::Up:
				--character->loc.y;
				break;
			case Direction::Right:
				++character->loc.x;
				break;
			case Direction::Down:
				++character->loc.y;
				break;
			case Direction::Left:
				--character->loc.x;
				break;
		}

		server::ChairCloseServerPacket reply;
		reply.player_id = character->PlayerID();
		reply.coords = character->loc;
		character->Send(reply);
		character->Stand();
	}
}

PACKET_HANDLER_REGISTER(Chair)
	Register(Chair_Request, Playing);
PACKET_HANDLER_REGISTER_END(Chair)

}
