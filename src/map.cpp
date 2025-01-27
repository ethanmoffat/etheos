
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "map.hpp"

#include "arena.hpp"
#include "character.hpp"
#include "config.hpp"
#include "eoclient.hpp"
#include "eodata.hpp"
#include "npc.hpp"
#include "npc_data.hpp"
#include "packet.hpp"
#include "party.hpp"
#include "player.hpp"
#include "quest.hpp"
#include "timer.hpp"
#include "wedding.hpp"
#include "world.hpp"

#include "console.hpp"
#include "util.hpp"
#include "util/rpn.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <list>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

static const char *map_safe_fail_filename;

static void map_safe_fail(int line)
{
	Console::Err("Invalid file / failed read/seek: %s -- %i", map_safe_fail_filename, line);
}

#define SAFE_SEEK(fh, offset, from) if (std::fseek(fh, offset, from) != 0) { std::fclose(fh); map_safe_fail(__LINE__); return false; }
#define SAFE_READ(buf, size, count, fh) if (std::fread(buf, size, count, fh) != static_cast<int>(count)) {  std::fclose(fh); map_safe_fail(__LINE__);return false; }

void map_spawn_chests(void *map_void)
{
	Map *map(static_cast<Map *>(map_void));

	double current_time = Timer::GetTime();
	UTIL_FOREACH(map->chests, chest)
	{
		bool needs_update = false;

		std::vector<std::list<Map_Chest_Spawn>> spawns;
		spawns.resize(chest->slots);

		UTIL_FOREACH(chest->spawns, spawn)
		{
			if (spawn.last_taken + spawn.time*60.0 < current_time)
			{
				bool slot_used = false;

				UTIL_FOREACH(chest->items, item)
				{
					if (item.slot == spawn.slot)
					{
						slot_used = true;
					}
				}

				if (!slot_used)
				{
					spawns[spawn.slot - 1].emplace_back(spawn);
				}
			}
		}

		UTIL_FOREACH(spawns, slot_spawns)
		{
			if (!slot_spawns.empty())
			{
				const Map_Chest_Spawn& spawn = *std::next(slot_spawns.cbegin(), util::rand(0, slot_spawns.size() - 1));

				chest->AddItem(spawn.item.id, spawn.item.amount, spawn.slot);
				needs_update = true;

#ifdef DEBUG
				Console::Dbg("Spawning chest item %i (x%i) on map %i", spawn.item.id, spawn.item.amount, map->id);
#endif // DEBUG
			}
		}

		if (needs_update)
		{
			chest->Update(map, 0);
		}
	}
}

struct map_close_door_struct
{
	Map *map;
	unsigned char x, y;
};

void map_close_door(void *map_close_void)
{
	map_close_door_struct *close(static_cast<map_close_door_struct *>(map_close_void));

	close->map->CloseDoor(close->x, close->y);

	delete close;
}

struct map_evacuate_struct
{
	Map *map;
	int step;
};

void map_evacuate(void *map_evacuate_void)
{
	map_evacuate_struct *evac(static_cast<map_evacuate_struct *>(map_evacuate_void));

	int ticks_per_step = int(evac->map->world->config["EvacuateStep"]) / int(evac->map->world->config["EvacuateTick"]);

	if (evac->step > 0)
	{
		bool step = evac->step % ticks_per_step == 0;

		UTIL_FOREACH(evac->map->characters, character)
		{
			if (step)
				character->ServerMsg(character->world->i18n.Format("map_evacuate", (evac->step / ticks_per_step) * int(evac->map->world->config["EvacuateStep"])));

			character->PlaySound(int(evac->map->world->config["EvacuateSound"]));
		}

		--evac->step;
	}
	else
	{
		std::vector<Character*> evac_chars;

		UTIL_FOREACH(evac->map->characters, character)
		{
			if (character->SourceAccess() < ADMIN_GUIDE)
			{
				evac_chars.push_back(character);
			}
		}

		UTIL_FOREACH(evac_chars, character)
		{
			character->world->Jail(0, character, false);
		}

		evac->map->evacuate_lock = false;
		delete evac;
	}
}

int Map_Chest::HasItem(short item_id) const
{
	UTIL_FOREACH(this->items, item)
	{
		if (item.id == item_id)
		{
			return item.amount;
		}
	}

	return 0;
}

int Map_Chest::AddItem(short item_id, int amount, int slot)
{
	if (amount <= 0)
	{
		return 0;
	}

	if (slot == 0)
	{
		UTIL_FOREACH_REF(this->items, item)
		{
			if (item.id == item_id)
			{
				if (item.amount + amount < 0 || item.amount + amount > this->maxchest)
				{
					return 0;
				}

				item.amount += amount;
				return amount;
			}
		}
	}

	if (this->items.size() >= static_cast<std::size_t>(this->chestslots) || amount > this->maxchest)
	{
		return 0;
	}

	if (slot == 0)
	{
		int user_items = 0;

		UTIL_FOREACH(this->items, item)
		{
			if (item.slot == 0)
			{
				++user_items;
			}
		}

		if (user_items + this->slots >= this->chestslots)
		{
			return 0;
		}
	}

	Map_Chest_Item chestitem;
	chestitem.id = item_id;
	chestitem.amount = amount;
	chestitem.slot = slot;

	if (slot == 0)
	{
		this->items.push_back(chestitem);
	}
	else
	{
		this->items.push_front(chestitem);
	}

	return amount;
}

int Map_Chest::DelItem(short item_id)
{
	UTIL_IFOREACH(this->items, it)
	{
		if (it->id == item_id)
		{
			int amount = it->amount;

			if (it->slot)
			{
				double current_time = Timer::GetTime();

				UTIL_FOREACH_REF(this->spawns, spawn)
				{
					if (spawn.slot == it->slot)
					{
						spawn.last_taken = current_time;
					}
				}
			}

			this->items.erase(it);
			return amount;
		}
	}

	return 0;
}

int Map_Chest::DelSomeItem(short item_id, int amount)
{
	UTIL_IFOREACH(this->items, it)
	{
		if (it->id == item_id)
		{
			if (amount < it->amount)
			{
				it->amount -= amount;

				if (it->slot)
				{
					double current_time = Timer::GetTime();

					UTIL_FOREACH_REF(this->spawns, spawn)
					{
						if (spawn.slot == it->slot)
						{
							spawn.last_taken = current_time;
						}
					}

					it->slot = 0;
				}

				return it->amount;
			}
			else
			{
				return DelItem(item_id);
			}

			break;
		}
	}

	return 0;
}

void Map_Chest::Update(Map *map, Character *exclude) const
{
	PacketBuilder builder(PACKET_CHEST, PACKET_AGREE, this->items.size() * 5);

	UTIL_FOREACH(this->items, item)
	{
		builder.AddShort(item.id);
		builder.AddThree(item.amount);
	}

	UTIL_FOREACH(map->characters, character)
	{
		if (character == exclude)
		{
			continue;
		}

		if (util::path_length(character->x, character->y, this->x, this->y) <= 1)
		{
			character->Send(builder);
		}
	}
}

Map::Map(int id, World *world)
{
	this->id = id;
	this->world = world;
	this->exists = false;
	this->jukebox_protect = 0.0;
	this->arena = nullptr;
	this->wedding = nullptr;
	this->evacuate_lock = false;
	this->has_timed_spikes = false;

	this->Load();

	this->LoadArena();
	this->LoadWedding();

	if (!this->chests.empty())
	{
		TimeEvent *event = new TimeEvent(map_spawn_chests, this, 60.0, Timer::FOREVER);
		this->world->timer.Register(event);
	}

	this->currentQuakeTick = 0;
	this->nextQuakeTick = 0;
	this->TimedQuakes(true); // load initial quake data
}

void Map::LoadArena()
{
	std::list<Character *> update_characters;

	if (this->arena)
	{
		UTIL_FOREACH(this->arena->map->characters, character)
		{
			if (character->arena == this->arena)
			{
				update_characters.push_back(character);
			}
		}

		delete this->arena;
	}

	if (world->arenas_config[util::to_string(id) + ".enabled"])
	{
		std::string spawns_str = world->arenas_config[util::to_string(id) + ".spawns"];

		std::vector<std::string> spawns = util::explode(',', spawns_str);

		if (spawns.size() % 4 != 0)
		{
			Console::Wrn("Invalid arena spawn data for map %i", id);
			this->arena = 0;
		}
		else
		{
			this->arena = new Arena(this, static_cast<int>(world->arenas_config[util::to_string(id) + ".time"]), static_cast<int>(world->arenas_config[util::to_string(id) + ".block"]));
			this->arena->occupants = update_characters.size();

			int i = 1;
			Arena_Spawn *s = 0;
			UTIL_FOREACH(spawns, spawn)
			{
				util::trim(spawn);

				switch (i++ % 4)
				{
					case 1:
						s = new Arena_Spawn;
						s->sx = util::to_int(spawn);
						break;

					case 2:
						s->sy = util::to_int(spawn);
						break;

					case 3:
						s->dx = util::to_int(spawn);
						break;

					case 0:
						s->dy = util::to_int(spawn);
						this->arena->spawns.push_back(s);
						s = 0;
						break;
				}
			}

			if (s)
			{
				Console::Wrn("Invalid arena spawn string");
				delete s;
			}
		}
	}
	else
	{
		this->arena = 0;
	}

	UTIL_FOREACH(update_characters, character)
	{
		character->arena = this->arena;

		if (!this->arena)
		{
			character->Warp(character->map->id, character->map->relog_x, character->map->relog_y);
		}
	}
}

void Map::LoadWedding()
{
	for (NPC* npc : this->npcs)
	{
		if (npc->ENF().type == ENF::Priest)
		{
			this->wedding = new Wedding(this, npc->index);
			break;
		}
	}
}

bool Map::Load()
{
	char namebuf[7];

	if (this->id < 0)
	{
		return false;
	}

	std::string filename = this->world->config["MapDir"];
	std::sprintf(namebuf, "%05i", this->id);
	filename.append(namebuf);
	filename.append(".emf");

	map_safe_fail_filename = filename.c_str();

	std::FILE *fh = std::fopen(filename.c_str(), "rb");

	if (!fh)
		return false;

	this->has_timed_spikes = false;

	SAFE_SEEK(fh, 0x03, SEEK_SET);
	SAFE_READ(this->rid, sizeof(char), 4, fh);

	char buf[12];
	unsigned char outersize;
	unsigned char innersize;

	SAFE_SEEK(fh, 0x1F, SEEK_SET);
	SAFE_READ(buf, sizeof(char), 2, fh);
	this->pk = PacketProcessor::Number(buf[0]) == 3;
	this->effect = static_cast<EffectType>(PacketProcessor::Number(buf[1]));

	SAFE_SEEK(fh, 0x25, SEEK_SET);
	SAFE_READ(buf, sizeof(char), 2, fh);
	this->width = PacketProcessor::Number(buf[0]) + 1;
	this->height = PacketProcessor::Number(buf[1]) + 1;

	this->tiles.resize(this->height * this->width);

	SAFE_SEEK(fh, 0x2A, SEEK_SET);
	SAFE_READ(buf, sizeof(char), 3, fh);
	this->scroll = PacketProcessor::Number(buf[0]);
	this->relog_x = PacketProcessor::Number(buf[1]);
	this->relog_y = PacketProcessor::Number(buf[2]);

	SAFE_SEEK(fh, 0x2E, SEEK_SET);
	SAFE_READ(buf, sizeof(char), 1, fh);
	outersize = PacketProcessor::Number(buf[0]);
	if (outersize)
	{
		SAFE_SEEK(fh, 8 * outersize, SEEK_CUR);
	}

	SAFE_READ(buf, sizeof(char), 1, fh);
	outersize = PacketProcessor::Number(buf[0]);
	if (outersize)
	{
		SAFE_SEEK(fh, 4 * outersize, SEEK_CUR);
	}

	SAFE_READ(buf, sizeof(char), 1, fh);
	outersize = PacketProcessor::Number(buf[0]);
	if (outersize)
	{
		SAFE_SEEK(fh, 12 * outersize, SEEK_CUR);
	}

	SAFE_READ(buf, sizeof(char), 1, fh);
	outersize = PacketProcessor::Number(buf[0]);
	for (int i = 0; i < outersize; ++i)
	{
		SAFE_READ(buf, sizeof(char), 2, fh);
		unsigned char yloc = PacketProcessor::Number(buf[0]);
		innersize = PacketProcessor::Number(buf[1]);
		for (int ii = 0; ii < innersize; ++ii)
		{
			SAFE_READ(buf, sizeof(char), 2, fh);
			unsigned char xloc = PacketProcessor::Number(buf[0]);
			unsigned char spec = PacketProcessor::Number(buf[1]);

			if (!this->InBounds(xloc, yloc))
			{
				Console::Wrn("Tile spec on map %i is outside of map bounds (%ix%i)", this->id, xloc, yloc);
				continue;
			}

			this->GetTile(xloc, yloc).tilespec = static_cast<Map_Tile::TileSpec>(spec);

			if (spec == Map_Tile::Chest)
			{
				Map_Chest chest;
				chest.maxchest = static_cast<int>(this->world->config["MaxChest"]);
				chest.chestslots = static_cast<int>(this->world->config["ChestSlots"]);
				chest.x = xloc;
				chest.y = yloc;
				chest.slots = 0;
				this->chests.push_back(std::make_shared<Map_Chest>(chest));
			}

			if (spec == Map_Tile::Spikes1)
			{
				this->has_timed_spikes = true;
			}
		}
	}

	SAFE_READ(buf, sizeof(char), 1, fh);
	outersize = PacketProcessor::Number(buf[0]);
	for (int i = 0; i < outersize; ++i)
	{
		SAFE_READ(buf, sizeof(char), 2, fh);
		unsigned char yloc = PacketProcessor::Number(buf[0]);
		innersize = PacketProcessor::Number(buf[1]);
		for (int ii = 0; ii < innersize; ++ii)
		{
			Map_Warp newwarp;
			SAFE_READ(buf, sizeof(char), 8, fh);
			unsigned char xloc = PacketProcessor::Number(buf[0]);
			newwarp.map = PacketProcessor::Number(buf[1], buf[2]);
			newwarp.x = PacketProcessor::Number(buf[3]);
			newwarp.y = PacketProcessor::Number(buf[4]);
			newwarp.levelreq = PacketProcessor::Number(buf[5]);
			newwarp.spec = static_cast<Map_Warp::WarpSpec>(PacketProcessor::Number(buf[6], buf[7]));

			if (!this->InBounds(xloc, yloc))
			{
				Console::Wrn("Warp on map %i is outside of map bounds (%ix%i)", this->id, xloc, yloc);
				continue;
			}

			try
			{
				this->GetTile(xloc, yloc).warp = newwarp;
			}
			catch (...)
			{
				std::fclose(fh);
				map_safe_fail(__LINE__);
				return false;
			}
		}
	}

	SAFE_SEEK(fh, 0x2E, SEEK_SET);
	SAFE_READ(buf, sizeof(char), 1, fh);
	outersize = PacketProcessor::Number(buf[0]);
	int index = 0;
	for (int i = 0; i < outersize; ++i)
	{
		SAFE_READ(buf, sizeof(char), 8, fh);
		unsigned char x = PacketProcessor::Number(buf[0]);
		unsigned char y = PacketProcessor::Number(buf[1]);
		short npc_id = PacketProcessor::Number(buf[2], buf[3]);
		unsigned char spawntype = PacketProcessor::Number(buf[4]);
		short spawntime = PacketProcessor::Number(buf[5], buf[6]);
		unsigned char amount = PacketProcessor::Number(buf[7]);

		if (!this->world->enf->Get(npc_id))
		{
			Console::Wrn("An NPC spawn on map %i uses a non-existent NPC (#%i at %ix%i)", this->id, npc_id, x, y);
		}

		for (int ii = 0; ii < amount; ++ii)
		{
			if (!this->InBounds(x, y))
			{
				Console::Wrn("An NPC spawn on map %i is outside of map bounds (%s at %ix%i)", this->id, this->world->enf->Get(npc_id).name.c_str(), x, y);
				continue;
			}

			NPC *newnpc = new NPC(this, npc_id, x, y, spawntype, spawntime, index++);
			this->npcs.push_back(newnpc);

			newnpc->Spawn();
		}
	}

	SAFE_READ(buf, sizeof(char), 1, fh);
	outersize = PacketProcessor::Number(buf[0]);
	if (outersize)
	{
		SAFE_SEEK(fh, 4 * outersize, SEEK_CUR);
	}

	SAFE_READ(buf, sizeof(char), 1, fh);
	outersize = PacketProcessor::Number(buf[0]);
	for (int i = 0; i < outersize; ++i)
	{
		SAFE_READ(buf, sizeof(char), 12, fh);
		unsigned char x = PacketProcessor::Number(buf[0]);
		unsigned char y = PacketProcessor::Number(buf[1]);
		short key = PacketProcessor::Number(buf[2], buf[3]);
		short slot = PacketProcessor::Number(buf[4]);
		short itemid = PacketProcessor::Number(buf[5], buf[6]);
		short time = PacketProcessor::Number(buf[7], buf[8]);
		int amount = PacketProcessor::Number(buf[9], buf[10], buf[11]);

		if (itemid != this->world->eif->Get(itemid).id)
		{
			Console::Wrn("A chest spawn on map %i uses a non-existent item (#%i at %ix%i)", this->id, itemid, x, y);
		}

		UTIL_FOREACH(this->chests, chest)
		{
			if (chest->x == x && chest->y == y)
			{
				Map_Chest_Spawn spawn;

				spawn.slot = slot+1;
				spawn.time = time;
				spawn.last_taken = Timer::GetTime();
				spawn.item.id = itemid;
				spawn.item.amount = amount;

				chest->key = key > 0 ? key : chest->key;

				chest->spawns.push_back(spawn);
				chest->slots = std::max(chest->slots, slot+1);
				goto skip_warning;
			}
		}
		Console::Wrn("A chest spawn on map %i points to a non-chest (%s x%i at %ix%i)", this->id, this->world->eif->Get(itemid).name.c_str(), amount, x, y);
		skip_warning:
		;
	}

	SAFE_SEEK(fh, 0x00, SEEK_END);
	this->filesize = std::ftell(fh);

	std::fclose(fh);

	this->exists = true;

	return true;
}

void Map::Unload()
{
	this->exists = false;

	UTIL_FOREACH(this->npcs, npc)
	{
		UTIL_FOREACH_CREF(npc->damagelist, opponent)
		{
			opponent->attacker->unregister_npc.erase(
				std::remove(UTIL_RANGE(opponent->attacker->unregister_npc), npc),
				opponent->attacker->unregister_npc.end()
			);
		}

		delete npc;
	}

	this->npcs.clear();

	if (this->arena)
	{
		UTIL_FOREACH(this->arena->spawns, spawn)
			delete spawn;

		delete this->arena;
		this->arena = nullptr;
	}

	if (this->wedding)
	{
		delete this->wedding;
		this->wedding = nullptr;
	}

	this->chests.clear();
	this->tiles.clear();
}

int Map::GenerateItemID() const
{
	int lowest_free_id = 1;
	restart_loop:
	UTIL_FOREACH(this->items, item)
	{
		if (item->uid == lowest_free_id)
		{
			lowest_free_id = item->uid + 1;
			goto restart_loop;
		}
	}
	return lowest_free_id;
}

unsigned char Map::GenerateNPCIndex() const
{
	unsigned char lowest_free_id = 1;
	restart_loop:
	UTIL_FOREACH(this->npcs, npc)
	{
		if (npc->index == lowest_free_id)
		{
			lowest_free_id = npc->index + 1;
			goto restart_loop;
		}
	}
	return lowest_free_id;
}

void Map::Enter(Character *character, WarpAnimation animation)
{
	this->characters.push_back(character);
	character->map = this;
	character->last_walk = Timer::GetTime();
	character->attacks = 0;
	character->CancelSpell();

	PacketBuilder builder(PACKET_PLAYERS, PACKET_AGREE, 63);

	builder.AddByte(255);
	builder.AddBreakString(character->SourceName());
	builder.AddShort(character->PlayerID());
	builder.AddShort(character->mapid);
	builder.AddShort(character->x);
	builder.AddShort(character->y);
	builder.AddChar(character->direction);
	builder.AddChar(6); // ?
	builder.AddString(character->PaddedGuildTag());
	builder.AddChar(character->level);
	builder.AddChar(character->gender);
	builder.AddChar(character->hairstyle);
	builder.AddChar(character->haircolor);
	builder.AddChar(character->race);
	builder.AddShort(character->maxhp);
	builder.AddShort(character->hp);
	builder.AddShort(character->maxtp);
	builder.AddShort(character->tp);
	// equipment
	character->AddPaperdollData(builder, "B000A0HSW");

	builder.AddChar(character->sitting);
	builder.AddChar(character->IsHideInvisible());
	builder.AddChar(animation);
	builder.AddByte(255);
	builder.AddChar(1); // 0 = NPC, 1 = player

	UTIL_FOREACH(this->characters, checkcharacter)
	{
		if (checkcharacter == character || !character->InRange(checkcharacter))
		{
			continue;
		}

		checkcharacter->Send(builder);
	}

	character->CheckQuestRules();
}

void Map::Leave(Character *character, WarpAnimation animation, bool silent)
{
	if (!silent)
	{
		PacketBuilder builder(PACKET_AVATAR, PACKET_REMOVE, 3);
		builder.AddShort(character->PlayerID());

		if (animation != WARP_ANIMATION_NONE)
		{
			builder.AddChar(animation);
		}

		UTIL_FOREACH(this->characters, checkcharacter)
		{
			if (checkcharacter == character || !character->InRange(checkcharacter))
			{
				continue;
			}

			checkcharacter->Send(builder);
		}
	}

	if (this->wedding)
	{
		this->wedding->CancelWeddingRequest(character);
	}

	this->characters.erase(
		std::remove(UTIL_RANGE(this->characters), character),
		this->characters.end()
	);

	character->map = 0;
}

void Map::Msg(Character *from, std::string message, bool echo)
{
	message = util::text_cap(message, static_cast<int>(this->world->config["ChatMaxWidth"]) - util::text_width(util::ucfirst(from->SourceName()) + "  "));

	PacketBuilder builder(PACKET_TALK, PACKET_PLAYER, 2 + message.length());
	builder.AddShort(from->PlayerID());
	builder.AddString(message);

	UTIL_FOREACH(this->characters, character)
	{
		if (!from->InRange(character))
			continue;

		character->AddChatLog("", from->SourceName(), message);

		if (!echo && character == from)
			continue;

		character->Send(builder);
	}
}

void Map::Msg(NPC *from, std::string message)
{
	message = util::text_cap(message, static_cast<int>(this->world->config["ChatMaxWidth"]) - util::text_width(util::ucfirst(from->ENF().name) + "  "));

	PacketBuilder builder(PACKET_NPC, PACKET_PLAYER, 4 + message.length());
	builder.AddByte(255);
	builder.AddByte(255);
	builder.AddChar(from->index);
	builder.AddChar(static_cast<unsigned char>(message.length()));
	builder.AddString(message);

	UTIL_FOREACH(this->characters, character)
	{
		if (!character->InRange(from))
			continue;

		character->Send(builder);
	}
}

Map::WalkResult Map::Walk(Character *from, Direction direction, bool admin)
{
	int seedistance = this->world->config["SeeDistance"];

	unsigned char target_x = from->x;
	unsigned char target_y = from->y;

	switch (direction)
	{
		case DIRECTION_UP:
			target_y -= 1;

			if (target_y > from->y)
			{
				return WalkFail;
			}

			break;

		case DIRECTION_RIGHT:
			target_x += 1;

			if (target_x < from->x)
			{
				return WalkFail;
			}

			break;

		case DIRECTION_DOWN:
			target_y += 1;

			if (target_x < from->x)
			{
				return WalkFail;
			}

			break;

		case DIRECTION_LEFT:
			target_x -= 1;

			if (target_x > from->x)
			{
				return WalkFail;
			}

			break;
	}

	if (!this->InBounds(target_x, target_y))
		return WalkFail;

	if (!admin)
	{
		if (!this->Walkable(target_x, target_y))
			return WalkFail;

		if (this->Occupied(target_x, target_y, PlayerOnly) && (from->last_walk + double(this->world->config["GhostTimer"]) > Timer::GetTime()))
			return WalkFail;
	}

	const Map_Warp& warp = this->GetWarp(target_x, target_y);

	if (warp)
	{
		if (from->level >= warp.levelreq && (warp.spec == Map_Warp::NoDoor || warp.open))
		{
			Map* map = this->world->GetMap(warp.map);
			if (from->SourceAccess() < ADMIN_GUIDE && map->evacuate_lock && map->id != from->map->id)
			{
				from->StatusMsg(this->world->i18n.Format("map_evacuate_block"));
				from->Refresh();
			}
			else
			{
				from->Warp(warp.map, warp.x, warp.y);
			}

			return WalkWarped;
		}

		if (!admin)
			return WalkFail;
	}

    from->last_walk = Timer::GetTime();
    from->attacks = 0;
    from->CancelSpell();

	from->direction = direction;

	from->x = target_x;
	from->y = target_y;

	int newx;
	int newy;
	int oldx;
	int oldy;

	std::vector<std::pair<int, int>> newcoords;
	std::vector<std::pair<int, int>> oldcoords;

	std::vector<Character *> newchars;
	std::vector<Character *> oldchars;
	std::vector<NPC *> newnpcs;
	std::vector<NPC *> oldnpcs;
	std::vector<std::shared_ptr<Map_Item>> newitems;

	switch (direction)
	{
		case DIRECTION_UP:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newy = from->y - seedistance + std::abs(i);
				newx = from->x + i;
				oldy = from->y + seedistance + 1 - std::abs(i);
				oldx = from->x + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case DIRECTION_RIGHT:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newx = from->x + seedistance - std::abs(i);
				newy = from->y + i;
				oldx = from->x - seedistance - 1 + std::abs(i);
				oldy = from->y + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case DIRECTION_DOWN:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newy = from->y + seedistance - std::abs(i);
				newx = from->x + i;
				oldy = from->y - seedistance - 1 + std::abs(i);
				oldx = from->x + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case DIRECTION_LEFT:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newx = from->x - seedistance + std::abs(i);
				newy = from->y + i;
				oldx = from->x + seedistance + 1 - std::abs(i);
				oldy = from->y + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

	}

	UTIL_FOREACH(this->characters, checkchar)
	{
		if (checkchar == from)
		{
			continue;
		}

		for (std::size_t i = 0; i < oldcoords.size(); ++i)
		{
			if (checkchar->x == oldcoords[i].first && checkchar->y == oldcoords[i].second)
			{
				oldchars.push_back(checkchar);
			}
			else if (checkchar->x == newcoords[i].first && checkchar->y == newcoords[i].second)
			{
				newchars.push_back(checkchar);
			}
		}
	}

	UTIL_FOREACH(this->npcs, checknpc)
	{
		if (!checknpc->alive)
		{
			continue;
		}

		for (std::size_t i = 0; i < oldcoords.size(); ++i)
		{
			if (checknpc->x == oldcoords[i].first && checknpc->y == oldcoords[i].second)
			{
				oldnpcs.push_back(checknpc);
			}
			else if (checknpc->x == newcoords[i].first && checknpc->y == newcoords[i].second)
			{
				newnpcs.push_back(checknpc);
			}
		}
	}

	UTIL_FOREACH(this->items, checkitem)
	{
		for (std::size_t i = 0; i < oldcoords.size(); ++i)
		{
			if (checkitem->x == newcoords[i].first && checkitem->y == newcoords[i].second)
			{
				newitems.push_back(checkitem);
			}
		}
	}

	PacketBuilder builder(PACKET_AVATAR, PACKET_REMOVE, 2);
	builder.AddShort(from->PlayerID());

	UTIL_FOREACH(oldchars, character)
	{
		PacketBuilder rbuilder(PACKET_AVATAR, PACKET_REMOVE, 2);
		rbuilder.AddShort(character->PlayerID());

		character->Send(builder);
		from->Send(rbuilder);
	}

	builder.Reset(62);
	builder.SetID(PACKET_PLAYERS, PACKET_AGREE);

	builder.AddByte(255);
	builder.AddBreakString(from->SourceName());
	builder.AddShort(from->PlayerID());
	builder.AddShort(from->mapid);
	builder.AddShort(from->x);
	builder.AddShort(from->y);
	builder.AddChar(from->direction);
	builder.AddChar(6); // ?
	builder.AddString(from->PaddedGuildTag());
	builder.AddChar(from->level);
	builder.AddChar(from->gender);
	builder.AddChar(from->hairstyle);
	builder.AddChar(from->haircolor);
	builder.AddChar(from->race);
	builder.AddShort(from->maxhp);
	builder.AddShort(from->hp);
	builder.AddShort(from->maxtp);
	builder.AddShort(from->tp);
	// equipment
	from->AddPaperdollData(builder, "B000A0HSW");
	builder.AddChar(from->sitting);
	builder.AddChar(from->IsHideInvisible());
	builder.AddByte(255);
	builder.AddChar(1); // 0 = NPC, 1 = player

	UTIL_FOREACH(newchars, character)
	{
		PacketBuilder rbuilder(PACKET_PLAYERS, PACKET_AGREE, 62);
		rbuilder.AddByte(255);
		rbuilder.AddBreakString(character->SourceName());
		rbuilder.AddShort(character->PlayerID());
		rbuilder.AddShort(character->mapid);
		rbuilder.AddShort(character->x);
		rbuilder.AddShort(character->y);
		rbuilder.AddChar(character->direction);
		rbuilder.AddChar(6); // ?
		rbuilder.AddString(character->PaddedGuildTag());
		rbuilder.AddChar(character->level);
		rbuilder.AddChar(character->gender);
		rbuilder.AddChar(character->hairstyle);
		rbuilder.AddChar(character->haircolor);
		rbuilder.AddChar(character->race);
		rbuilder.AddShort(character->maxhp);
		rbuilder.AddShort(character->hp);
		rbuilder.AddShort(character->maxtp);
		rbuilder.AddShort(character->tp);
		// equipment
		character->AddPaperdollData(rbuilder, "B000A0HSW");

		rbuilder.AddChar(character->sitting);
		rbuilder.AddChar(character->IsHideInvisible());
		rbuilder.AddByte(255);
		rbuilder.AddChar(1); // 0 = NPC, 1 = player

		character->Send(builder);
		from->Send(rbuilder);
	}

	builder.Reset(5);
	builder.SetID(PACKET_WALK, PACKET_PLAYER);

	builder.AddShort(from->PlayerID());
	builder.AddChar(direction);
	builder.AddChar(from->x);
	builder.AddChar(from->y);

	UTIL_FOREACH(this->characters, character)
	{
		if (character == from || !from->InRange(character))
		{
			continue;
		}

		character->Send(builder);
	}

	builder.Reset(2 + newitems.size() * 9);
	builder.SetID(PACKET_WALK, PACKET_REPLY);

	builder.AddByte(255);
	builder.AddByte(255);
	UTIL_FOREACH(newitems, item)
	{
		builder.AddShort(item->uid);
		builder.AddShort(item->id);
		builder.AddChar(item->x);
		builder.AddChar(item->y);
		builder.AddThree(item->amount);
	}
	from->Send(builder);

	builder.SetID(PACKET_RANGE, PACKET_REPLY);
	UTIL_FOREACH(newnpcs, npc)
	{
		builder.Reset(8);
		builder.AddChar(0);
		builder.AddByte(255);
		builder.AddChar(npc->index);
		builder.AddShort(npc->id);
		builder.AddChar(npc->x);
		builder.AddChar(npc->y);
		builder.AddChar(npc->direction);

		from->Send(builder);
	}

	UTIL_FOREACH(oldnpcs, npc)
	{
		npc->RemoveFromView(from);
	}

	from->CheckQuestRules();

	Map_Tile::TileSpec spec = this->GetSpec(from->x, from->y);

	double spike_damage = this->world->config["SpikeDamage"];

	if (spike_damage > 0.0 && (spec == Map_Tile::Spikes2 || spec == Map_Tile::Spikes3) && !from->IsHideInvisible())
	{
		int amount = static_cast<int>(from->maxhp * spike_damage);

		from->SpikeDamage(amount);

		if (from->hp == 0)
		{
			from->DeathRespawn();
			return WalkWarped;
		}
	}

	return WalkOK;
}

Map::WalkResult Map::Walk(NPC *from, Direction direction)
{
	int seedistance = this->world->config["SeeDistance"];

	unsigned char target_x = from->x;
	unsigned char target_y = from->y;

	switch (direction)
	{
		case DIRECTION_UP:
			target_y -= 1;

			if (target_y > from->y)
			{
				return WalkFail;
			}

			break;

		case DIRECTION_RIGHT:
			target_x += 1;

			if (target_x < from->x)
			{
				return WalkFail;
			}

			break;

		case DIRECTION_DOWN:
			target_y += 1;

			if (target_x < from->x)
			{
				return WalkFail;
			}

			break;

		case DIRECTION_LEFT:
			target_x -= 1;

			if (target_x > from->x)
			{
				return WalkFail;
			}

			break;
	}

	bool adminghost = (from->ENF().type == ENF::Aggressive || from->parent);

	if (!this->Walkable(target_x, target_y, true) || this->Occupied(target_x, target_y, Map::PlayerAndNPC, adminghost))
	{
		return WalkFail;
	}

	from->x = target_x;
	from->y = target_y;

	int newx;
	int newy;
	int oldx;
	int oldy;

	std::vector<std::pair<int, int>> newcoords;
	std::vector<std::pair<int, int>> oldcoords;

	std::vector<Character *> newchars;
	std::vector<Character *> oldchars;

	switch (direction)
	{
		case DIRECTION_UP:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newy = from->y - seedistance + std::abs(i);
				newx = from->x + i;
				oldy = from->y + seedistance + 1 - std::abs(i);
				oldx = from->x + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case DIRECTION_RIGHT:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newx = from->x + seedistance - std::abs(i);
				newy = from->y + i;
				oldx = from->x - seedistance - 1 + std::abs(i);
				oldy = from->y + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case DIRECTION_DOWN:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newy = from->y + seedistance - std::abs(i);
				newx = from->x + i;
				oldy = from->y - seedistance - 1 + std::abs(i);
				oldx = from->x + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case DIRECTION_LEFT:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newx = from->x - seedistance + std::abs(i);
				newy = from->y + i;
				oldx = from->x + seedistance + 1 - std::abs(i);
				oldy = from->y + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

	}

	from->direction = direction;

	UTIL_FOREACH(this->characters, checkchar)
	{
		for (std::size_t i = 0; i < oldcoords.size(); ++i)
		{
			if (checkchar->x == oldcoords[i].first && checkchar->y == oldcoords[i].second)
			{
				oldchars.push_back(checkchar);
			}
			else if (checkchar->x == newcoords[i].first && checkchar->y == newcoords[i].second)
			{
				newchars.push_back(checkchar);
			}
		}
	}

	PacketBuilder builder(PACKET_RANGE, PACKET_REPLY, 8);
	builder.AddChar(0);
	builder.AddByte(255);
	builder.AddChar(from->index);
	builder.AddShort(from->id);
	builder.AddChar(from->x);
	builder.AddChar(from->y);
	builder.AddChar(from->direction);

	UTIL_FOREACH(newchars, character)
	{
		character->Send(builder);
	}

	builder.Reset(7);
	builder.SetID(PACKET_NPC, PACKET_PLAYER);

	builder.AddChar(from->index);
	builder.AddChar(from->x);
	builder.AddChar(from->y);
	builder.AddChar(from->direction);
	builder.AddByte(255);
	builder.AddByte(255);
	builder.AddByte(255);

	UTIL_FOREACH(this->characters, character)
	{
		if (!character->InRange(from))
		{
			continue;
		}

		character->Send(builder);
	}

	UTIL_FOREACH(oldchars, character)
	{
		from->RemoveFromView(character);
	}

	return WalkOK;
}

void Map::Attack(Character *from, Direction direction)
{
	const EIF_Data& wepdata = this->world->eif->Get(from->paperdoll[Character::Weapon]);
	const EIF_Data& shielddata = this->world->eif->Get(from->paperdoll[Character::Shield]);

	if (wepdata.subtype == EIF::Ranged && shielddata.subtype != EIF::Arrows)
	{
		// Ranged gun hack
		if (wepdata.id != 365 || wepdata.name != "Gun")
		{
			return;
		}
		// / Ranged gun hack
	}

	from->direction = direction;
	from->attacks += 1;

	from->CancelSpell();

	if (from->arena)
	{
		from->arena->Attack(from, direction);
	}

	int wep_graphic = wepdata.dollgraphic;
	bool is_instrument = (wep_graphic != 0 && this->world->IsInstrument(wep_graphic));

	if (!is_instrument && (this->pk || (this->world->config["GlobalPK"] && !this->world->PKExcept(this->id))))
	{
		if (this->AttackPK(from, direction))
		{
			return;
		}
	}

	PacketBuilder builder(PACKET_ATTACK, PACKET_PLAYER, 3);
	builder.AddShort(from->PlayerID());
	builder.AddChar(direction);

	UTIL_FOREACH(this->characters, character)
	{
		if (character == from || !from->InRange(character))
		{
			continue;
		}

		character->Send(builder);
	}

	if (is_instrument)
		return;

	if (!from->CanInteractCombat())
		return;

	int target_x = from->x;
	int target_y = from->y;

	int range = 1;

	if (wepdata.subtype == EIF::Ranged)
	{
		range = static_cast<int>(this->world->config["RangedDistance"]);
	}

	for (int i = 0; i < range; ++i)
	{
		switch (from->direction)
		{
			case DIRECTION_UP:
				target_y -= 1;
				break;

			case DIRECTION_RIGHT:
				target_x += 1;
				break;

			case DIRECTION_DOWN:
				target_y += 1;
				break;

			case DIRECTION_LEFT:
				target_x -= 1;
				break;
		}

		UTIL_FOREACH(this->npcs, npc)
		{
			if ((npc->ENF().type == ENF::Passive || npc->ENF().type == ENF::Aggressive || from->SourceDutyAccess() >= static_cast<int>(this->world->admin_config["killnpc"]))
			 && npc->alive && npc->x == target_x && npc->y == target_y)
			{
				int amount = util::rand(from->mindam, from->maxdam);
				double rand = util::rand(0.0, 1.0);
				// Checks if target is facing you
				bool critical = std::abs(int(npc->direction) - from->direction) != 2 || rand < static_cast<double>(this->world->config["CriticalRate"]);

				if (this->world->config["CriticalFirstHit"] && npc->hp == npc->ENF().hp)
					critical = true;

				std::unordered_map<std::string, double> formula_vars;

				from->FormulaVars(formula_vars);
				npc->FormulaVars(formula_vars, "target_");
				formula_vars["modifier"] = this->world->config["MobRate"];
				formula_vars["damage"] = amount;
				formula_vars["critical"] = critical;

				amount = static_cast<int>(rpn_eval(rpn_parse(this->world->formulas_config["damage"]), formula_vars));
				double hit_rate = rpn_eval(rpn_parse(this->world->formulas_config["hit_rate"]), formula_vars);

				if (rand > hit_rate)
				{
					amount = 0;
				}

				amount = std::max(amount, 0);

				int limitamount = std::min(amount, int(npc->hp));

				if (this->world->config["LimitDamage"])
				{
					amount = limitamount;
				}

				npc->Damage(from, amount);
				// *npc may not be valid here

				return;
			}
		}

		if (!this->Walkable(target_x, target_y, true))
		{
			return;
		}
	}
}

bool Map::AttackPK(Character *from, Direction direction)
{
	if (!from->CanInteractPKCombat())
		return false;

	(void)direction;

	int target_x = from->x;
	int target_y = from->y;

	int range = 1;

	if (this->world->eif->Get(from->paperdoll[Character::Weapon]).subtype == EIF::Ranged)
	{
		range = static_cast<int>(this->world->config["RangedDistance"]);
	}

	for (int i = 0; i < range; ++i)
	{
		switch (from->direction)
		{
			case DIRECTION_UP:
				target_y -= 1;
				break;

			case DIRECTION_RIGHT:
				target_x += 1;
				break;

			case DIRECTION_DOWN:
				target_y += 1;
				break;

			case DIRECTION_LEFT:
				target_x -= 1;
				break;
		}

		UTIL_FOREACH(this->characters, character)
		{
			if (character->mapid == this->id && !character->nowhere && character->x == target_x && character->y == target_y)
			{
				int amount = util::rand(from->mindam, from->maxdam);
				double rand = util::rand(0.0, 1.0);
				// Checks if target is facing you
				bool critical = std::abs(int(character->direction) - from->direction) != 2 || rand < static_cast<double>(this->world->config["CriticalRate"]);

				std::unordered_map<std::string, double> formula_vars;

				from->FormulaVars(formula_vars);
				character->FormulaVars(formula_vars, "target_");
				formula_vars["modifier"] = this->world->config["PKRate"];
				formula_vars["damage"] = amount;
				formula_vars["critical"] = critical;

				amount = static_cast<int>(rpn_eval(rpn_parse(this->world->formulas_config["damage"]), formula_vars));
				double hit_rate = rpn_eval(rpn_parse(this->world->formulas_config["hit_rate"]), formula_vars);

				if (rand > hit_rate)
				{
					amount = 0;
				}

				amount = std::max(amount, 0);

				int limitamount = std::min(amount, int(character->hp));

				if (this->world->config["LimitDamage"])
				{
					amount = limitamount;
				}

				character->hp -= limitamount;

				PacketBuilder from_builder(PACKET_AVATAR, PACKET_REPLY, 10);
				from_builder.AddShort(0);
				from_builder.AddShort(character->PlayerID());
				from_builder.AddThree(amount);
				from_builder.AddChar(from->direction);
				from_builder.AddChar(static_cast<unsigned char>(util::clamp<int>(static_cast<int>(double(character->hp) / double(character->maxhp) * 100.0), 0, 100)));
				from_builder.AddChar(character->hp == 0);

				PacketBuilder builder(PACKET_AVATAR, PACKET_REPLY, 10);
				builder.AddShort(from->PlayerID());
				builder.AddShort(character->PlayerID());
				builder.AddThree(amount);
				builder.AddChar(from->direction);
				builder.AddChar(static_cast<unsigned char>(util::clamp<int>(static_cast<int>(double(character->hp) / double(character->maxhp) * 100.0), 0, 100)));
				builder.AddChar(character->hp == 0);

				from->Send(from_builder);

				UTIL_FOREACH(this->characters, checkchar)
				{
					if (from != checkchar && character->InRange(checkchar))
					{
						checkchar->Send(builder);
					}
				}

				if (character->hp == 0)
				{
					character->DeathRespawn();

					UTIL_FOREACH(from->quests, q)
					{
						if (!q.second || q.second->GetQuest()->Disabled())
							continue;

						q.second->KilledPlayer();
					}
				}

				builder.Reset(4);
				builder.SetID(PACKET_RECOVER, PACKET_PLAYER);

				builder.AddShort(character->hp);
				builder.AddShort(character->tp);
				character->Send(builder);

				return true;
			}
		}

		if (!this->Walkable(target_x, target_y, true))
		{
			return false;
		}
	}

	return false;
}

void Map::Face(Character *from, Direction direction)
{
	from->direction = direction;

	from->CancelSpell();

	PacketBuilder builder(PACKET_FACE, PACKET_PLAYER, 3);
	builder.AddShort(from->PlayerID());
	builder.AddChar(direction);

	UTIL_FOREACH(this->characters, character)
	{
		if (character == from || !from->InRange(character))
		{
			continue;
		}

		character->Send(builder);
	}
}

void Map::Sit(Character *from, SitState sit_type)
{
	from->sitting = sit_type;

	from->CancelSpell();

	PacketBuilder builder((sit_type == SIT_CHAIR) ? PACKET_CHAIR : PACKET_SIT, PACKET_PLAYER, 6);
	builder.AddShort(from->PlayerID());
	builder.AddChar(from->x);
	builder.AddChar(from->y);
	builder.AddChar(from->direction);
	builder.AddChar(0); // ?

	UTIL_FOREACH(this->characters, character)
	{
		if (character == from || !from->InRange(character))
		{
			continue;
		}

		character->Send(builder);
	}
}

void Map::Stand(Character *from)
{
	from->sitting = SIT_STAND;

	from->CancelSpell();

	PacketBuilder builder(PACKET_SIT, PACKET_REMOVE, 4);
	builder.AddShort(from->PlayerID());
	builder.AddChar(from->x);
	builder.AddChar(from->y);

	UTIL_FOREACH(this->characters, character)
	{
		if (character == from || !from->InRange(character))
		{
			continue;
		}

		character->Send(builder);
	}
}

void Map::Emote(Character *from, enum Emote emote, bool echo)
{
	PacketBuilder builder(PACKET_EMOTE, PACKET_PLAYER, 3);
	builder.AddShort(from->PlayerID());
	builder.AddChar(emote);

	UTIL_FOREACH(this->characters, character)
	{
		if (!echo && (character == from || !from->InRange(character)))
		{
			continue;
		}

		character->Send(builder);
	}
}

bool Map::Occupied(unsigned char x, unsigned char y, Map::OccupiedTarget target, bool adminghost) const
{
	if (!InBounds(x, y))
	{
		return false;
	}

	if (target != Map::NPCOnly)
	{
		UTIL_FOREACH(this->characters, character)
		{
			bool ghost = adminghost && (!character->CanInteractCombat() || character->IsHideNpc());

			if (character->x == x && character->y == y && !ghost)
			{
				return true;
			}
		}
	}

	if (target != Map::PlayerOnly)
	{
		UTIL_FOREACH(this->npcs, npc)
		{
			if (npc->alive && npc->x == x && npc->y == y)
			{
				return true;
			}
		}
	}

	return false;
}

Map::~Map()
{
	this->Unload();
}

bool Map::OpenDoor(Character *from, unsigned char x, unsigned char y)
{
	if (!this->InBounds(x, y) || (from && !from->InRange(x, y)))
	{
		return false;
	}

	if (Map_Warp& warp = this->GetWarp(x, y))
	{
		if (warp.spec == Map_Warp::NoDoor || warp.open)
		{
			return false;
		}

		if (from && warp.spec > Map_Warp::Door)
		{
			unsigned int key_item = this->world->eif->GetKey(warp.spec - static_cast<int>(Map_Warp::Door) + 1);
			if (!from->CanInteractDoors() || !from->HasItem(key_item))
			{
				PacketBuilder builder(PACKET_DOOR, PACKET_CLOSE, 3);
				builder.AddChar(key_item);
				from->Send(builder);

				return false;
			}
		}

		PacketBuilder builder(PACKET_DOOR, PACKET_OPEN, 3);
		builder.AddChar(x);
		builder.AddShort(y);

		UTIL_FOREACH(this->characters, character)
		{
			if (character->InRange(x, y))
			{
				character->Send(builder);
			}
		}

		warp.open = true;

		map_close_door_struct *close = new map_close_door_struct;
		close->map = this;
		close->x = x;
		close->y = y;

		TimeEvent *event = new TimeEvent(map_close_door, close, this->world->config["DoorTimer"], 1);
		this->world->timer.Register(event);

		return true;
	}

	return false;
}

void Map::CloseDoor(unsigned char x, unsigned char y)
{
	if (!this->InBounds(x, y))
		return;

	if (Map_Warp& warp = this->GetWarp(x, y))
	{
		if (warp.spec == Map_Warp::NoDoor || !warp.open)
		{
			return;
		}

		warp.open = false;
	}
}

void Map::SpellSelf(Character *from, unsigned short spell_id)
{
	const ESF_Data& spell = from->world->esf->Get(spell_id);

	if (!spell || spell.type != ESF::Heal || from->tp < spell.tp)
		return;

	from->tp -= spell.tp;

	int hpgain = spell.hp;

	if (this->world->config["LimitDamage"])
		hpgain = std::min(hpgain, from->maxhp - from->hp);

	hpgain = std::max(hpgain, 0);

	from->hp += hpgain;

	from->hp = std::min(from->hp, from->maxhp);

	PacketBuilder builder(PACKET_SPELL, PACKET_TARGET_SELF, 15);
	builder.AddShort(from->PlayerID());
	builder.AddShort(spell_id);
	builder.AddInt(spell.hp);
	builder.AddChar(static_cast<unsigned char>(util::clamp<int>(static_cast<int>(double(from->hp) / double(from->maxhp) * 100.0), 0, 100)));

	UTIL_FOREACH(this->characters, character)
	{
		if (character != from && from->InRange(character))
			character->Send(builder);
	}

	builder.AddShort(from->hp);
	builder.AddShort(from->tp);
	builder.AddShort(1);

	from->Send(builder);
}

void Map::SpellAttack(Character *from, NPC *npc, unsigned short spell_id)
{
	if (!from->CanInteractCombat())
		return;

	const ESF_Data& spell = from->world->esf->Get(spell_id);

	if (!spell || spell.type != ESF::Damage || from->tp < spell.tp)
		return;

	if ((npc->ENF().type == ENF::Passive || npc->ENF().type == ENF::Aggressive) && npc->alive)
	{
		from->tp -= spell.tp;

		int amount = util::rand(from->mindam + spell.mindam, from->maxdam + spell.maxdam);
		double rand = util::rand(0.0, 1.0);

		bool critical = rand < static_cast<double>(this->world->config["CriticalRate"]);

		std::unordered_map<std::string, double> formula_vars;

		from->FormulaVars(formula_vars);
		npc->FormulaVars(formula_vars, "target_");
		formula_vars["modifier"] = this->world->config["MobRate"];
		formula_vars["damage"] = amount;
		formula_vars["critical"] = critical;

		amount = static_cast<int>(rpn_eval(rpn_parse(this->world->formulas_config["damage"]), formula_vars));
		double hit_rate = rpn_eval(rpn_parse(this->world->formulas_config["hit_rate"]), formula_vars);

		if (rand > hit_rate)
		{
			amount = 0;
		}

		amount = std::max(amount, 0);

		int limitamount = std::min(amount, int(npc->hp));

		if (this->world->config["LimitDamage"])
		{
			amount = limitamount;
		}

		npc->Damage(from, amount, spell_id);
		// *npc may not be valid here
	}
}

void Map::SpellAttackPK(Character *from, Character *victim, unsigned short spell_id)
{
	const ESF_Data& spell = from->world->esf->Get(spell_id);

	if (!spell || (spell.type != ESF::Heal && spell.type != ESF::Damage) || from->tp < spell.tp)
		return;

	if (spell.type == ESF::Damage && (from->map->pk || (this->world->config["GlobalPK"] && !this->world->PKExcept(this->id))))
	{
		if (!from->CanInteractPKCombat())
			return;

		from->tp -= spell.tp;

		int amount = util::rand(from->mindam + spell.mindam, from->maxdam + spell.maxdam);
		double rand = util::rand(0.0, 1.0);

		bool critical = rand < static_cast<double>(this->world->config["CriticalRate"]);

		std::unordered_map<std::string, double> formula_vars;

		from->FormulaVars(formula_vars);
		victim->FormulaVars(formula_vars, "target_");
		formula_vars["modifier"] = this->world->config["PKRate"];
		formula_vars["damage"] = amount;
		formula_vars["critical"] = critical;

		amount = static_cast<int>(rpn_eval(rpn_parse(this->world->formulas_config["damage"]), formula_vars));
		double hit_rate = rpn_eval(rpn_parse(this->world->formulas_config["hit_rate"]), formula_vars);

		if (rand > hit_rate)
		{
			amount = 0;
		}

		amount = std::max(amount, 0);

		int limitamount = std::min(amount, int(victim->hp));

		if (this->world->config["LimitDamage"])
		{
			amount = limitamount;
		}

		victim->hp -= limitamount;

		PacketBuilder builder(PACKET_AVATAR, PACKET_ADMIN, 12);
		builder.AddShort(from->PlayerID());
		builder.AddShort(victim->PlayerID());
		builder.AddThree(amount);
		builder.AddChar(from->direction);
		builder.AddChar(static_cast<unsigned char>(util::clamp<int>(static_cast<int>(double(victim->hp) / double(victim->maxhp) * 100.0), 0, 100)));
		builder.AddChar(victim->hp == 0);
		builder.AddShort(spell_id);

		UTIL_FOREACH(this->characters, character)
		{
			if (victim->InRange(character))
				character->Send(builder);
		}

		if (victim->hp == 0)
		{
			victim->DeathRespawn();

			UTIL_FOREACH(from->quests, q)
			{
				if (!q.second || q.second->GetQuest()->Disabled())
					continue;

				q.second->KilledPlayer();
			}
		}

		builder.Reset(4);
		builder.SetID(PACKET_RECOVER, PACKET_PLAYER);

		builder.AddShort(victim->hp);
		builder.AddShort(victim->tp);
		victim->Send(builder);

		if (victim->party)
		{
			victim->party->UpdateHP(victim);
		}
	}
	else if (spell.type == ESF::Heal)
	{
		from->tp -= spell.tp;

		int displayhp = spell.hp;
		int hpgain = spell.hp;

		if (this->world->config["LimitDamage"])
			hpgain = std::min(hpgain, victim->maxhp - victim->hp);

		hpgain = std::max(hpgain, 0);

		if (!from->CanInteractCombat() && from != victim && !(from->CanInteractPKCombat() && (from->map->pk || (this->world->config["GlobalPK"] && !this->world->PKExcept(this->id)))))
		{
			displayhp = hpgain = std::min(hpgain, 1);
		}

		victim->hp += hpgain;

		if (!this->world->config["LimitDamage"])
			victim->hp = std::min(victim->hp, victim->maxhp);

		PacketBuilder builder(PACKET_SPELL, PACKET_TARGET_OTHER, 18);
		builder.AddShort(victim->PlayerID());
		builder.AddShort(from->PlayerID());
		builder.AddChar(from->direction);
		builder.AddShort(spell_id);
		builder.AddInt(displayhp);
		builder.AddChar(static_cast<unsigned char>(util::clamp<int>(static_cast<int>(double(victim->hp) / double(victim->maxhp) * 100.0), 0, 100)));

		UTIL_FOREACH(this->characters, character)
		{
			if (character != victim && victim->InRange(character))
				character->Send(builder);
		}

		builder.AddShort(victim->hp);

		victim->Send(builder);

		if (victim->party)
		{
			victim->party->UpdateHP(victim);
		}
	}

	PacketBuilder builder(PACKET_RECOVER, PACKET_PLAYER, 4);
	builder.AddShort(from->hp);
	builder.AddShort(from->tp);
	from->Send(builder);
}

void Map::SpellGroup(Character *from, unsigned short spell_id)
{
	const ESF_Data& spell = from->world->esf->Get(spell_id);

	if (!spell || spell.type != ESF::Heal || !from->party || from->tp < spell.tp)
		return;

	from->tp -= spell.tp;

	int displayhp = spell.hp;

	if (!from->CanInteractCombat() && !(from->CanInteractPKCombat() && (from->map->pk || (this->world->config["GlobalPK"] && !this->world->PKExcept(this->id)))))
	{
		displayhp = std::min(displayhp, 1);
	}

	std::set<Character *> in_range;

	PacketBuilder builder(PACKET_SPELL, PACKET_TARGET_GROUP, 8 + from->party->members.size() * 10);
	builder.AddShort(spell_id);
	builder.AddShort(from->PlayerID());
	builder.AddShort(from->tp);
	builder.AddShort(displayhp);

	UTIL_FOREACH(from->party->members, member)
	{
		if (member->map != from->map)
			continue;

		int hpgain = spell.hp;

		if (this->world->config["LimitDamage"])
			hpgain = std::min(hpgain, member->maxhp - member->hp);

		hpgain = std::max(hpgain, 0);

		if (!from->CanInteractCombat() && !(from->CanInteractPKCombat() && (from->map->pk || (this->world->config["GlobalPK"] && !this->world->PKExcept(this->id)))))
			hpgain = std::min(hpgain, 1);

		member->hp += hpgain;

		if (!this->world->config["LimitDamage"])
			member->hp = std::min(member->hp, member->maxhp);

		// wat?
		builder.AddByte(255);
		builder.AddByte(255);
		builder.AddByte(255);
		builder.AddByte(255);
		builder.AddByte(255);

		builder.AddShort(member->PlayerID());
		builder.AddChar(static_cast<unsigned char>(util::clamp<int>(static_cast<int>(double(member->hp) / double(member->maxhp) * 100.0), 0, 100)));
		builder.AddShort(member->hp);

		UTIL_FOREACH(this->characters, character)
		{
			if (member->InRange(character))
				in_range.insert(character);
		}
	}

	UTIL_FOREACH(in_range, character)
	{
		character->Send(builder);
	}
}

std::shared_ptr<Map_Item> Map::AddItem(short id, int amount, unsigned char x, unsigned char y, Character *from)
{
	std::shared_ptr<Map_Item> newitem(std::make_shared<Map_Item>(0, id, amount, x, y, 0, 0));

	if (from || (from && from->SourceAccess() <= ADMIN_GM))
	{
		int ontile = 0;
		int onmap = 0;

		UTIL_FOREACH(this->items, item)
		{
			++onmap;
			if (item->x == x && item->y == y)
			{
				++ontile;
			}
		}

		if (ontile >= static_cast<int>(this->world->config["MaxTile"]) || onmap >= static_cast<int>(this->world->config["MaxMap"]))
		{
			return newitem;
		}
	}

	newitem->uid = GenerateItemID();

	PacketBuilder builder(PACKET_ITEM, PACKET_ADD, 9);
	builder.AddShort(id);
	builder.AddShort(newitem->uid);
	builder.AddThree(amount);
	builder.AddChar(x);
	builder.AddChar(y);

	UTIL_FOREACH(this->characters, character)
	{
		if ((from && character == from) || !character->InRange(*newitem))
		{
			continue;
		}

		character->Send(builder);
	}

	this->items.push_back(newitem);
	return newitem;
}

std::shared_ptr<Map_Item> Map::GetItem(short uid)
{
	UTIL_FOREACH(this->items, item)
	{
		if (item->uid == uid)
		{
			return item;
		}
	}

	return std::shared_ptr<Map_Item>();
}

std::shared_ptr<const Map_Item> Map::GetItem(short uid) const
{
	UTIL_FOREACH(this->items, item)
	{
		if (item->uid == uid)
		{
			return item;
		}
	}

	return std::shared_ptr<Map_Item>();
}

void Map::DelItem(short uid, Character *from)
{
	UTIL_IFOREACH(this->items, it)
	{
		if ((*it)->uid == uid)
		{
			this->DelItem(it, from);
			break;
		}
	}
}

std::list<std::shared_ptr<Map_Item>>::iterator Map::DelItem(std::list<std::shared_ptr<Map_Item>>::iterator it, Character *from)
{
	PacketBuilder builder(PACKET_ITEM, PACKET_REMOVE, 2);
	builder.AddShort((*it)->uid);

	UTIL_FOREACH(this->characters, character)
	{
		if ((from && character == from) || !character->InRange(**it))
		{
			continue;
		}

		character->Send(builder);
	}

	return this->items.erase(it);
}

void Map::DelSomeItem(short uid, int amount, Character *from)
{
	if (amount < 0)
		return;

	UTIL_IFOREACH(this->items, it)
	{
		if ((*it)->uid == uid)
		{
			if (amount < (*it)->amount)
			{
				(*it)->amount -= amount;

				PacketBuilder builder(PACKET_ITEM, PACKET_REMOVE, 2);
				builder.AddShort((*it)->uid);

				UTIL_FOREACH(this->characters, character)
				{
					if ((from && character == from) || !character->InRange(**it))
					{
						continue;
					}

					character->Send(builder);
				}

				builder.Reset(9);
				builder.SetID(PACKET_ITEM, PACKET_ADD);
				builder.AddShort((*it)->id);
				builder.AddShort((*it)->uid);
				builder.AddThree((*it)->amount);
				builder.AddChar((*it)->x);
				builder.AddChar((*it)->y);

				UTIL_FOREACH(this->characters, character)
				{
					if (!character->InRange(**it))
						continue;

					character->Send(builder);
				}
			}
			else
			{
				this->DelItem(it, from);
			}

			break;
		}
	}
}

bool Map::InBounds(unsigned char x, unsigned char y) const
{
	return !(x >= this->width || y >= this->height);
}

bool Map::Walkable(unsigned char x, unsigned char y, bool npc) const
{
	if (!InBounds(x, y) || !this->GetTile(x, y).Walkable(npc))
		return false;

	if (this->world->config["GhostArena"] && this->GetTile(x, y).tilespec == Map_Tile::Arena && this->Occupied(x, y, PlayerAndNPC))
		return false;

	return true;
}

Map_Tile& Map::GetTile(unsigned char x, unsigned char y)
{
	if (!InBounds(x, y))
		throw std::out_of_range("Map tile out of range");

	return this->tiles[y * this->width + x];
}

const Map_Tile& Map::GetTile(unsigned char x, unsigned char y) const
{
	if (!InBounds(x, y))
		throw std::out_of_range("Map tile out of range");

	return this->tiles[y * this->width + x];
}

Map_Tile::TileSpec Map::GetSpec(unsigned char x, unsigned char y) const
{
	if (!InBounds(x, y))
		return Map_Tile::None;

	return this->GetTile(x, y).tilespec;
}

Map_Warp& Map::GetWarp(unsigned char x, unsigned char y)
{
	return this->GetTile(x, y).warp;
}

const Map_Warp& Map::GetWarp(unsigned char x, unsigned char y) const
{
	return this->GetTile(x, y).warp;
}

std::vector<Character *> Map::CharactersInRange(unsigned char x, unsigned char y, unsigned char range)
{
	std::vector<Character *> characters;

	UTIL_FOREACH(this->characters, character)
	{
		if (util::path_length(character->x, character->y, x, y) <= range)
			characters.push_back(character);
	}

	return characters;
}

std::vector<NPC *> Map::NPCsInRange(unsigned char x, unsigned char y, unsigned char range)
{
	std::vector<NPC *> npcs;

	UTIL_FOREACH(this->npcs, npc)
	{
		if (util::path_length(npc->x, npc->y, x, y) <= range)
			npcs.push_back(npc);
	}

	return npcs;
}

void Map::Effect(MapEffect effect, unsigned char param)
{
	PacketBuilder builder(PACKET_EFFECT, PACKET_USE, 2);
	builder.AddChar(effect);
	builder.AddChar(param);

	UTIL_FOREACH(this->characters, character)
	{
		character->Send(builder);
	}
}

void Map::TileEffect(unsigned char x, unsigned char y, unsigned short effect)
{
	PacketBuilder builder(PACKET_EFFECT, PACKET_AGREE, 4);
	builder.AddChar(x);
	builder.AddChar(y);
	builder.AddShort(effect);

	UTIL_FOREACH(this->characters, character)
	{
		if (character->InRange(x, y))
			character->Send(builder);
	}
}

bool Map::Evacuate()
{
	if (!this->evacuate_lock)
	{
		this->evacuate_lock = true;

		map_evacuate_struct *evac = new map_evacuate_struct;
		evac->map = this;
		evac->step = int(evac->map->world->config["EvacuateLength"]) / int(evac->map->world->config["EvacuateTick"]);

		TimeEvent *event = new TimeEvent(map_evacuate, evac, this->world->config["EvacuateTick"], evac->step);
		this->world->timer.Register(event);

		map_evacuate(evac);
		return true;
	}
	else
	{
		return false;
	}
}

bool Map::Reload()
{
	char namebuf[7];
	char checkrid[4];

	std::string filename = this->world->config["MapDir"];
	std::sprintf(namebuf, "%05i", this->id);
	filename.append(namebuf);
	filename.append(".emf");

	std::FILE *fh = std::fopen(filename.c_str(), "rb");

	if (!fh)
	{
		Console::Err("Could not load file: %s", filename.c_str());
		return false;
	}

	SAFE_SEEK(fh, 0x03, SEEK_SET);
	SAFE_READ(checkrid, sizeof(char), 4, fh);

	std::fclose(fh);

	if (this->rid[0] == checkrid[0] && this->rid[1] == checkrid[1]
	 && this->rid[2] == checkrid[2] && this->rid[3] == checkrid[3])
	{
		return true;
	}

	std::list<Character *> temp = this->characters;

	this->Unload();

	if (!this->Load())
	{
		return false;
	}

	this->LoadArena();
	this->LoadWedding();

	this->characters = temp;

	UTIL_FOREACH(temp, character)
	{
		character->player->client->Upload(FILE_MAP, character->mapid, INIT_MAP_MUTATION);
		character->Refresh(); // TODO: Find a better way to reload NPCs
	}

	this->exists = true;

	return true;
}

void Map::TimedSpikes()
{
	if (!this->has_timed_spikes)
		return;

	PacketBuilder builder(PACKET_EFFECT, PACKET_REPORT, 1);
	builder.AddByte(83); // S

	double spike_damage = this->world->config["SpikeDamage"];

	std::vector<Character*> killed;

	for (Character* character : this->characters)
	{
		if (character->nowhere || character->IsHideInvisible())
			continue;

		Map_Tile::TileSpec spec = this->GetSpec(character->x, character->y);

		if (spike_damage > 0.0 && spec == Map_Tile::Spikes1)
		{
			int amount = static_cast<int>(character->maxhp * spike_damage);

			character->SpikeDamage(amount);

			if (character->hp == 0)
				killed.push_back(character);
		}
		else
		{
			character->Send(builder);
		}
	}

	for (Character* character : killed)
	{
		character->DeathRespawn();
	}
}

void Map::TimedDrains()
{
	if (this->effect == EffectHPDrain)
	{
		double hpdrain_damage = this->world->config["DrainHPDamage"];

		std::vector<int> damage_map;
		damage_map.resize(this->characters.size());

		std::size_t i = 0;

		for (Character* character : this->characters)
		{
			if (character->nowhere || character->IsHideInvisible())
				continue;

			int amount = static_cast<int>(character->maxhp * hpdrain_damage);
			amount = std::max(std::min(amount, int(character->hp - 1)), 0);
			character->hp -= amount;

			damage_map[i++] = amount;
		}

		i = 0;

		for (Character* character : this->characters)
		{
			if (character->nowhere || character->IsHideInvisible())
				continue;

			if (hpdrain_damage > 0.0)
			{
				int damage = damage_map[i++];

				PacketBuilder builder(PACKET_EFFECT, PACKET_TARGET_OTHER, 6 + this->characters.size() * 5);
				builder.AddShort(damage);
				builder.AddShort(character->hp);
				builder.AddShort(character->maxhp);

				std::size_t ii = 0;

				for (Character* other : this->characters)
				{
					if (other->nowhere || other->IsHideInvisible())
						continue;

					int damage = damage_map[ii++];

					if (!character->InRange(other) || other == character)
						continue;

					builder.AddShort(other->PlayerID());
					builder.AddChar(static_cast<unsigned char>(util::clamp<int>(static_cast<int>(double(other->hp) / double(other->maxhp) * 100.0), 0, 100)));
					builder.AddShort(damage);
				}

				character->Send(builder);
			}
		}
	}

	if (this->effect == EffectTPDrain)
	{
		double tpdrain_damage = this->world->config["DrainTPDamage"];

		for (Character* character : this->characters)
		{
			if (character->nowhere || character->IsHideInvisible())
				continue;

			if (tpdrain_damage > 0.0)
			{
				int amount = static_cast<int>(character->maxtp * tpdrain_damage);

				amount = std::min(amount, int(character->tp));

				character->tp -= amount;

				PacketBuilder builder(PACKET_EFFECT, PACKET_SPEC, 7);
				builder.AddChar(1);
				builder.AddShort(amount);
				builder.AddShort(character->tp);
				builder.AddShort(character->maxtp);

				character->Send(builder);
			}
		}
	}
}

void Map::TimedQuakes(bool initialize)
{
	std::string configString;
	switch (this->effect)
	{
		case EffectQuake1: configString = "Quake1"; break;
		case EffectQuake2: configString = "Quake2"; break;
		case EffectQuake3: configString = "Quake3"; break;
		case EffectQuake4: configString = "Quake4"; break;
		default: return;
	}

	this->currentQuakeTick++;

	if (this->currentQuakeTick >= this->nextQuakeTick || initialize)
	{
		auto quakeData = util::explode(",", this->world->config[configString]);
		if (quakeData.size() != 4)
		{
			Console::Dbg("Timed quake on map %d failed - invalid parameters for %s in config (expected 4 comma-delimited values, got: %s)",
				this->id, configString.c_str(), this->world->config[configString].GetString().c_str());
			return;
		}

		this->currentQuakeTick = 0;
		this->nextQuakeTick = util::rand(util::to_int(quakeData[0]), util::to_int(quakeData[1]));

		if (!initialize)
		{
			auto quakeStrength = util::rand(util::to_int(quakeData[2]), util::to_int(quakeData[3]));
			this->Effect(MAP_EFFECT_QUAKE, util::clamp(quakeStrength, 0, 8));
		}
	}
}

Character *Map::GetCharacter(std::string name)
{
	name = util::lowercase(name);

	UTIL_FOREACH(this->characters, character)
	{
		if (character->SourceName().compare(name) == 0)
		{
			return character;
		}
	}

	return 0;
}

Character *Map::GetCharacterPID(unsigned int id)
{
	UTIL_FOREACH(this->characters, character)
	{
		if (character->PlayerID() == id)
		{
			return character;
		}
	}

	return 0;
}

Character *Map::GetCharacterCID(unsigned int id)
{
	UTIL_FOREACH(this->characters, character)
	{
		if (character->id == id)
		{
			return character;
		}
	}

	return 0;
}

NPC *Map::GetNPCIndex(unsigned char index)
{
	UTIL_FOREACH(this->npcs, npc)
	{
		if (npc->index == index)
		{
			return npc;
		}
	}

	return 0;
}

#undef SAFE_SEEK
#undef SAFE_READ
