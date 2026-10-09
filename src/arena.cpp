
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "arena.hpp"

#include "character.hpp"
#include "eoclient.hpp"
#include "map.hpp"
#include "timer.hpp"
#include "world.hpp"

#include "console.hpp"
#include "util.hpp"

#include <eolib/protocol/net/server/packets.hpp>

#include <vector>

namespace server = eolib::protocol::net::server;

void arena_spawn(void *arena_void)
{
	Arena *arena = static_cast<Arena *>(arena_void);

	arena->Spawn();
}

Arena::Arena(Map *map, int time, int block)
{
	this->map = map;
	this->time = time;
	this->block = block;
	this->occupants = 0;

	this->spawn_timer = new TimeEvent(arena_spawn, this, time, Timer::FOREVER);
	this->map->world->timer.Register(this->spawn_timer);
}

void Arena::Spawn(bool force)
{
	int newplayers = 0;

	UTIL_FOREACH(this->spawns, spawn)
	{
		UTIL_FOREACH(this->map->characters, character)
		{
			if (character->loc == spawn->loc)
			{
				++newplayers;
			}
		}
	}

	if (newplayers == 0)
	{
		return;
	}

	if (this->occupants >= this->block && !force)
	{
		this->map->SendToMap(server::ArenaDropServerPacket());
		return;
	}

	struct Arena_Spawn_Action
	{
		Character *character;
		short map;
		eolib::protocol::Coords loc;

		Arena_Spawn_Action(Character *character, short map, const eolib::protocol::Coords& loc)
			: character(character)
			, map(map)
			, loc(loc)
		{ }
	};

	std::vector<Arena_Spawn_Action> actions;

	UTIL_FOREACH(this->spawns, spawn)
	{
		UTIL_FOREACH(this->map->characters, character)
		{
			if (character->loc == spawn->loc)
			{
				character->next_arena = this;
				character->arena_kills = 0;
				actions.push_back({character, this->map->id, spawn->dest_loc});
				break;
			}
		}
	}

	UTIL_FOREACH(actions, act)
	{
		act.character->Warp(act.map, act.loc);
	}

	server::ArenaUseServerPacket packet;
	packet.players_count = newplayers;
	this->map->SendToMap(packet);
}

void Arena::Attack(Character *from, Direction direction)
{
	(void)direction;

	eolib::protocol::Coords target = from->loc;

	switch (from->direction)
	{
		case Direction::Up:
			target.y -= 1;
			break;

		case Direction::Right:
			target.x += 1;
			break;

		case Direction::Down:
			target.y += 1;
			break;

		case Direction::Left:
			target.x -= 1;
			break;
	}

	struct Arena_Spawn_Action
	{
		Character *character;
		short map;
		eolib::protocol::Coords loc;

		Arena_Spawn_Action(Character *character, short map, const eolib::protocol::Coords& loc)
			: character(character)
			, map(map)
			, loc(loc)
		{ }
	};

	std::vector<Arena_Spawn_Action> actions;

	UTIL_FOREACH(this->map->characters, character)
	{
		if (character->arena == this && character->loc == target)
		{
			++from->arena_kills;
			actions.push_back({character, this->map->id, this->map->relog_loc});

			if (from->arena->occupants == 2)
			{
				actions.push_back({from, this->map->id, this->map->relog_loc});

				server::ArenaAcceptServerPacket packet;
				packet.winner_name = from->SourceName();
				packet.kills_count = from->arena_kills;
				packet.killer_name = from->SourceName();
				packet.victim_name = character->SourceName();
				this->map->SendToMap(packet);
			}
			else
			{
				server::ArenaSpecServerPacket packet;
				packet.player_id = from->PlayerID();
				packet.direction = from->direction;
				packet.kills_count = from->arena_kills;
				packet.killer_name = from->SourceName();
				packet.victim_name = character->SourceName();
				this->map->SendToMap(packet);
			}

			break;
		}
	}

	UTIL_FOREACH(actions, act)
	{
		act.character->Warp(act.map, act.loc);
	}
}

Arena::~Arena()
{
	delete this->spawn_timer;
}
