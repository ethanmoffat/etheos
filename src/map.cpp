
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "map.hpp"

#include "arena.hpp"
#include "character.hpp"
#include "config.hpp"
#include "converters.hpp"
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
#include "eodata_file.hpp"
#include "util.hpp"
#include "util/rpn.hpp"

#include <eolib/data/eo_numeric_limits.hpp>
#include <eolib/data/number_encoder.hpp>
#include <eolib/protocol/map/structs.hpp>
#include <eolib/protocol/net/server/enums.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
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

namespace emf = eolib::protocol::map;
namespace server = eolib::protocol::net::server;
namespace pub = eolib::protocol::pub;

// Offsets in an EMF file
static constexpr std::size_t emf_rid_offset = 0x03;
static constexpr std::size_t emf_type_offset = 0x1F;

// Everything before the NPC spawn list
static constexpr std::size_t emf_header_size = 0x2E;

template <typename TPacket>
static TPacket SitPacketFor(const Character *from)
{
	TPacket packet;
	packet.player_id = from->PlayerID();
	packet.coords = from->loc;
	packet.direction = from->direction;
	return packet;
}

void map_spawn_chests(void *map_void)
{
	Map *map(static_cast<Map *>(map_void));

	double current_time = Timer::GetTime();
	UTIL_FOREACH(map->chests, chest)
	{
		bool needs_update = false;

		std::vector<std::list<Map_Chest_Spawn*>> spawns;
		spawns.resize(chest->slots);

		UTIL_FOREACH_REF(chest->spawns, spawn)
		{
			if (spawn.last_taken + spawn.spawn_random + spawn.time < current_time)
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
					spawns[spawn.slot - 1].emplace_back(&spawn);
				}
			}
		}

		double spawn_random = static_cast<double>(map->world->config["ChestSpawnRandomization"]);
		bool spawn_random_is_pct = *static_cast<std::string>(map->world->config["ChestSpawnRandomization"]).rbegin() == '%';

		UTIL_FOREACH(spawns, slot_spawns)
		{
			if (!slot_spawns.empty())
			{
				Map_Chest_Spawn* spawn = *std::next(slot_spawns.begin(), util::rand(0, slot_spawns.size() - 1));


				chest->AddItem(spawn->item.id, spawn->item.amount, spawn->slot);
				needs_update = true;

#ifdef DEBUG
				Console::Dbg("Spawning chest item %i (x%i) on map %i (timer %is | random %.2fs)", spawn->item.id, spawn->item.amount, map->id, static_cast<int>(spawn->time), spawn->spawn_random);
#endif // DEBUG

				double spawn_modifier = util::rand(double(0), spawn_random);
				spawn->spawn_random = spawn_random_is_pct ? spawn_modifier * spawn->time : spawn_modifier;
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
	eolib::protocol::Coords loc;
};

void map_close_door(void *map_close_void)
{
	map_close_door_struct *close(static_cast<map_close_door_struct *>(map_close_void));

	close->map->CloseDoor(close->loc.x, close->loc.y);

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
	server::ChestAgreeServerPacket agree;
	agree.items = Converters::ChestItemsFrom(*this);

	UTIL_FOREACH(map->characters, character)
	{
		if (character == exclude)
		{
			continue;
		}

		if (util::path_length(character->loc.x, character->loc.y, this->loc.x, this->loc.y) <= 1)
		{
			character->Send(agree);
		}
	}
}

Map::Map(int id, World *world)
{
	this->id = id;
	this->world = world;
	this->exists = false;
	this->rid = {0, 0};
	this->pk = false;
	this->effect = emf::MapTimedEffect::None;
	this->filesize = 0;
	this->width = 0;
	this->height = 0;
	this->scroll = false;
	this->relog_x = 0;
	this->relog_y = 0;
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
		TimeEvent *event = new TimeEvent(map_spawn_chests, this, 1.0, Timer::FOREVER);
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
		if (npc->PubData().type == pub::NpcType::Priest)
		{
			this->wedding = new Wedding(this, npc->index);
			break;
		}
	}
}

std::string Map::Filename() const
{
	char namebuf[7];
	std::snprintf(namebuf, sizeof namebuf, "%05i", this->id);
	return std::string(this->world->config["MapDir"]) + namebuf + ".emf";
}

bool Map::Load()
{
	if (this->id < 0)
	{
		return false;
	}

	const std::string filename = this->Filename();
	std::vector<std::uint8_t> content;

	if (!EODataFile::Read(filename, content))
		return false;

	if (content.size() < emf_header_size)
	{
		Console::Err("Invalid file: %s", filename.c_str());
		return false;
	}

	emf::Emf file;
	EODataFile::Deserialize(filename, content, file);

	this->has_timed_spikes = false;

	this->rid = {file.rid[0], file.rid[1]};
	this->pk = file.type == emf::MapType::Pk;
	this->effect = file.timed_effect;
	this->width = file.width + 1;
	this->height = file.height + 1;

	this->tiles.resize(this->height * this->width);

	this->scroll = file.can_scroll;
	this->relog_x = file.relog_x;
	this->relog_y = file.relog_y;

	for (const emf::MapTileSpecRow& row : file.tile_spec_rows)
	{
		for (const emf::MapTileSpecRowTile& tile : row.tiles)
		{
			const unsigned char xloc = tile.x;
			const unsigned char yloc = row.y;
			const emf::MapTileSpec spec = tile.tile_spec;

			if (!this->InBounds(xloc, yloc))
			{
				Console::Wrn("Tile spec on map %i is outside of map bounds (%ix%i)", this->id, xloc, yloc);
				continue;
			}

			this->GetTile(xloc, yloc).tilespec = spec;

			if (spec == emf::MapTileSpec::Chest)
			{
				Map_Chest chest;
				chest.maxchest = static_cast<int>(this->world->config["MaxChest"]);
				chest.chestslots = static_cast<int>(this->world->config["ChestSlots"]);
				chest.loc.x = xloc;
				chest.loc.y = yloc;
				chest.slots = 0;
				chest.key = 0;
				this->chests.push_back(std::make_shared<Map_Chest>(chest));
			}

			if (spec == emf::MapTileSpec::TimedSpikes)
			{
				this->has_timed_spikes = true;
			}
		}
	}

	for (const emf::MapWarpRow& row : file.warp_rows)
	{
		for (const emf::MapWarpRowTile& tile : row.tiles)
		{
			const unsigned char xloc = tile.x;
			const unsigned char yloc = row.y;

			Map_Warp newwarp;
			newwarp.map = tile.warp.destination_map;
			newwarp.loc = tile.warp.destination_coords;
			newwarp.levelreq = tile.warp.level_required;
			newwarp.spec = static_cast<Map_Warp::WarpSpec>(tile.warp.door);

			if (!this->InBounds(xloc, yloc))
			{
				Console::Wrn("Warp on map %i is outside of map bounds (%ix%i)", this->id, xloc, yloc);
				continue;
			}

			this->GetTile(xloc, yloc).warp = newwarp;
		}
	}

	int index = 0;
	for (const emf::MapNpc& spawn : file.npcs)
	{
		const unsigned char x = spawn.coords.x;
		const unsigned char y = spawn.coords.y;
		const short npc_id = spawn.id;

		// Stationary spawns store the facing direction in spawn_time instead of a respawn time
		const bool stationary = spawn.spawn_type >= NPC::StationarySpawnType;
		const short respawn_time = stationary ? 0 : spawn.spawn_time;
		const Direction direction = stationary ? static_cast<Direction>(spawn.spawn_time & 0x03) : Direction::Down;

		if (!this->world->enf->Exists(npc_id))
		{
			Console::Wrn("An NPC spawn on map %i uses a non-existent NPC (#%i at %ix%i)", this->id, npc_id, x, y);
		}

		for (int ii = 0; ii < spawn.amount; ++ii)
		{
			if (!this->InBounds(x, y))
			{
				Console::Wrn("An NPC spawn on map %i is outside of map bounds (%s at %ix%i)", this->id, this->world->enf->Get(npc_id).name.c_str(), x, y);
				continue;
			}

			NPC *newnpc = new NPC(this, npc_id, spawn.coords, spawn.spawn_type, respawn_time, direction, index++);
			this->npcs.push_back(newnpc);

			newnpc->Spawn();
		}
	}

	double spawn_random = static_cast<double>(this->world->config["ChestSpawnRandomization"]);
	bool spawn_random_is_pct = *static_cast<std::string>(this->world->config["ChestSpawnRandomization"]).rbegin() == '%';

	for (const emf::MapItem& item : file.items)
	{
		const unsigned char x = item.coords.x;
		const unsigned char y = item.coords.y;
		const short key = item.key;
		const short slot = item.chest_slot;
		const short itemid = item.item_id;
		const short time = item.spawn_time;
		const int amount = item.amount;

		if (!this->world->eif->Exists(itemid))
		{
			Console::Wrn("A chest spawn on map %i uses a non-existent item (#%i at %ix%i)", this->id, itemid, x, y);
		}

		if (amount == 0)
		{
			Console::Wrn("A chest spawn on map %i has an amount of 0 (#%i at %ix%i) and will be ignored", this->id, itemid, x, y);
			continue;
		}

		auto chest = std::find_if(UTIL_CRANGE(this->chests), [&](const std::shared_ptr<Map_Chest>& c)
		{
			return c->loc.x == x && c->loc.y == y;
		});

		if (chest == this->chests.end())
		{
			Console::Wrn("A chest spawn on map %i points to a non-chest (%s x%i at %ix%i)", this->id, this->world->eif->Get(itemid).name.c_str(), amount, x, y);
			continue;
		}

		Map_Chest_Spawn spawn;

		double spawn_modifier = util::rand(double(0), spawn_random);

		spawn.slot = slot+1;
		spawn.time = time * 60.0;
		spawn.spawn_random = spawn_random_is_pct ? spawn_modifier * spawn.time : spawn_modifier;
		spawn.last_taken = Timer::GetTime();
		spawn.item.id = itemid;
		spawn.item.amount = amount;

		(*chest)->key = key > 0 ? key : (*chest)->key;

		(*chest)->spawns.push_back(spawn);
		(*chest)->slots = std::max((*chest)->slots, slot+1);
	}

	this->filesize = static_cast<int>(content.size());

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

void Map::Enter(Character *character, WarpEffect effect)
{
	this->characters.push_back(character);
	character->map = this;
	character->last_walk = Timer::GetTime();
	character->attacks = 0;
	character->CancelSpell();

	server::CharacterMapInfo info = Converters::CharacterMapInfoFrom(character);
	info.warp_effect = effect;

	server::PlayersAgreeServerPacket players_agree;
	players_agree.nearby.characters.push_back(std::move(info));

	this->SendToNearby(character, players_agree, false);

	character->CheckQuestRules();
}

void Map::Leave(Character *character, WarpEffect effect, bool silent)
{
	if (!silent)
	{
		server::AvatarRemoveServerPacket avatar_remove;
		avatar_remove.player_id = character->PlayerID();

		if (effect != WarpEffect::None)
		{
			avatar_remove.warp_effect = effect;
		}

		this->SendToNearby(character, avatar_remove, false);
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

void Map::SendToNearby(const Character *from, const eolib::protocol::net::Packet& packet, bool echo)
{
	UTIL_FOREACH(this->characters, character)
	{
		if ((echo || character != from) && from->InRange(character))
		{
			character->Send(packet);
		}
	}
}

void Map::SendToNearby(const NPC *from, const eolib::protocol::net::Packet& packet, const Character *except)
{
	UTIL_FOREACH(this->characters, character)
	{
		if (character != except && character->InRange(from))
		{
			character->Send(packet);
		}
	}
}

void Map::SendToNearby(const eolib::protocol::Coords& coords, const eolib::protocol::net::Packet& packet)
{
	UTIL_FOREACH(this->characters, character)
	{
		if (character->InRange(coords.x, coords.y))
		{
			character->Send(packet);
		}
	}
}

void Map::SendToNearby(const Map_Item& item, const eolib::protocol::net::Packet& packet, const Character *except)
{
	UTIL_FOREACH(this->characters, character)
	{
		if (character != except && character->InRange(item))
		{
			character->Send(packet);
		}
	}
}

void Map::SendToMap(const eolib::protocol::net::Packet& packet)
{
	UTIL_FOREACH(this->characters, character)
	{
		character->Send(packet);
	}
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
	message = util::text_cap(message, static_cast<int>(this->world->config["ChatMaxWidth"]) - util::text_width(util::ucfirst(from->PubData().name) + "  "));

	server::NpcUpdateChat chat;
	chat.npc_index = from->index;
	chat.message = message;

	server::NpcPlayerServerPacket packet;
	packet.chats.push_back(chat);

	this->SendToNearby(from, packet);
}

Map::WalkResult Map::Walk(Character *from, Direction direction, bool admin)
{
	int seedistance = this->world->config["SeeDistance"];

	unsigned char target_x = from->loc.x;
	unsigned char target_y = from->loc.y;

	switch (direction)
	{
		case Direction::Up:
			target_y -= 1;

			if (target_y > from->loc.y)
			{
				return WalkFail;
			}

			break;

		case Direction::Right:
			target_x += 1;

			if (target_x < from->loc.x)
			{
				return WalkFail;
			}

			break;

		case Direction::Down:
			target_y += 1;

			if (target_x < from->loc.x)
			{
				return WalkFail;
			}

			break;

		case Direction::Left:
			target_x -= 1;

			if (target_x > from->loc.x)
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
				from->Warp(warp.map, warp.loc.x, warp.loc.y);
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

	from->loc.x = target_x;
	from->loc.y = target_y;

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
		case Direction::Up:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newy = from->loc.y - seedistance + std::abs(i);
				newx = from->loc.x + i;
				oldy = from->loc.y + seedistance + 1 - std::abs(i);
				oldx = from->loc.x + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case Direction::Right:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newx = from->loc.x + seedistance - std::abs(i);
				newy = from->loc.y + i;
				oldx = from->loc.x - seedistance - 1 + std::abs(i);
				oldy = from->loc.y + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case Direction::Down:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newy = from->loc.y + seedistance - std::abs(i);
				newx = from->loc.x + i;
				oldy = from->loc.y - seedistance - 1 + std::abs(i);
				oldx = from->loc.x + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case Direction::Left:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newx = from->loc.x - seedistance + std::abs(i);
				newy = from->loc.y + i;
				oldx = from->loc.x + seedistance + 1 - std::abs(i);
				oldy = from->loc.y + i;

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
			if (checkchar->loc.x == oldcoords[i].first && checkchar->loc.y == oldcoords[i].second)
			{
				oldchars.push_back(checkchar);
			}
			else if (checkchar->loc.x == newcoords[i].first && checkchar->loc.y == newcoords[i].second)
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
			if (checknpc->loc.x == oldcoords[i].first && checknpc->loc.y == oldcoords[i].second)
			{
				oldnpcs.push_back(checknpc);
			}
			else if (checknpc->loc.x == newcoords[i].first && checknpc->loc.y == newcoords[i].second)
			{
				newnpcs.push_back(checknpc);
			}
		}
	}

	UTIL_FOREACH(this->items, checkitem)
	{
		for (std::size_t i = 0; i < oldcoords.size(); ++i)
		{
			if (checkitem->loc.x == newcoords[i].first && checkitem->loc.y == newcoords[i].second)
			{
				newitems.push_back(checkitem);
			}
		}
	}

	server::AvatarRemoveServerPacket avatar_remove;
	avatar_remove.player_id = from->PlayerID();

	UTIL_FOREACH(oldchars, character)
	{
		server::AvatarRemoveServerPacket old_avatar_remove;
		old_avatar_remove.player_id = character->PlayerID();

		character->Send(avatar_remove);
		from->Send(old_avatar_remove);
	}

	server::PlayersAgreeServerPacket players_agree;
	players_agree.nearby.characters.push_back(Converters::CharacterMapInfoFrom(from));

	UTIL_FOREACH(newchars, character)
	{
		server::PlayersAgreeServerPacket new_players_agree;
		new_players_agree.nearby.characters.push_back(Converters::CharacterMapInfoFrom(character));

		character->Send(players_agree);
		from->Send(new_players_agree);
	}

	server::WalkPlayerServerPacket walk_player;
	walk_player.player_id = from->PlayerID();
	walk_player.direction = direction;
	walk_player.coords = from->loc;

	this->SendToNearby(from, walk_player, false);

	server::WalkReplyServerPacket walk_reply;
	walk_reply.items.reserve(newitems.size());
	UTIL_FOREACH(newitems, item)
	{
		walk_reply.items.push_back(Converters::ItemMapInfoFrom(*item));
	}
	from->Send(walk_reply);

	UTIL_FOREACH(newnpcs, npc)
	{
		server::RangeReplyServerPacket range_reply;
		range_reply.nearby.npcs.push_back(Converters::NpcMapInfoFrom(npc));
		from->Send(range_reply);
	}

	UTIL_FOREACH(oldnpcs, npc)
	{
		npc->RemoveFromView(from);
	}

	from->CheckQuestRules();

	emf::MapTileSpec spec = this->GetSpec(from->loc.x, from->loc.y);

	double spike_damage = this->world->config["SpikeDamage"];

	if (spike_damage > 0.0 && (spec == emf::MapTileSpec::Spikes || spec == emf::MapTileSpec::HiddenSpikes) && !from->IsHideInvisible())
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

	unsigned char target_x = from->loc.x;
	unsigned char target_y = from->loc.y;

	switch (direction)
	{
		case Direction::Up:
			target_y -= 1;

			if (target_y > from->loc.y)
			{
				return WalkFail;
			}

			break;

		case Direction::Right:
			target_x += 1;

			if (target_x < from->loc.x)
			{
				return WalkFail;
			}

			break;

		case Direction::Down:
			target_y += 1;

			if (target_x < from->loc.x)
			{
				return WalkFail;
			}

			break;

		case Direction::Left:
			target_x -= 1;

			if (target_x > from->loc.x)
			{
				return WalkFail;
			}

			break;
	}

	bool adminghost = (from->PubData().type == pub::NpcType::Aggressive || from->parent);

	if (!this->Walkable(target_x, target_y, true) || this->Occupied(target_x, target_y, Map::PlayerAndNPC, adminghost))
	{
		return WalkFail;
	}

	from->loc.x = target_x;
	from->loc.y = target_y;

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
		case Direction::Up:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newy = from->loc.y - seedistance + std::abs(i);
				newx = from->loc.x + i;
				oldy = from->loc.y + seedistance + 1 - std::abs(i);
				oldx = from->loc.x + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case Direction::Right:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newx = from->loc.x + seedistance - std::abs(i);
				newy = from->loc.y + i;
				oldx = from->loc.x - seedistance - 1 + std::abs(i);
				oldy = from->loc.y + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case Direction::Down:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newy = from->loc.y + seedistance - std::abs(i);
				newx = from->loc.x + i;
				oldy = from->loc.y - seedistance - 1 + std::abs(i);
				oldx = from->loc.x + i;

				newcoords.push_back(std::make_pair(newx, newy));
				oldcoords.push_back(std::make_pair(oldx, oldy));
			}
			break;

		case Direction::Left:
			for (int i = -seedistance; i <= seedistance; ++i)
			{
				newx = from->loc.x - seedistance + std::abs(i);
				newy = from->loc.y + i;
				oldx = from->loc.x + seedistance + 1 - std::abs(i);
				oldy = from->loc.y + i;

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
			if (checkchar->loc.x == oldcoords[i].first && checkchar->loc.y == oldcoords[i].second)
			{
				oldchars.push_back(checkchar);
			}
			else if (checkchar->loc.x == newcoords[i].first && checkchar->loc.y == newcoords[i].second)
			{
				newchars.push_back(checkchar);
			}
		}
	}

	server::RangeReplyServerPacket range_reply;
	range_reply.nearby.npcs.push_back(Converters::NpcMapInfoFrom(from));

	UTIL_FOREACH(newchars, character)
	{
		character->Send(range_reply);
	}

	server::NpcUpdatePosition position;
	position.npc_index = from->index;
	position.coords = from->loc;
	position.direction = from->direction;

	server::NpcPlayerServerPacket packet;
	packet.positions.push_back(position);

	this->SendToNearby(from, packet);

	UTIL_FOREACH(oldchars, character)
	{
		from->RemoveFromView(character);
	}

	return WalkOK;
}

void Map::Attack(Character *from, Direction direction)
{
	const pub::EifRecord& wepdata = this->world->eif->Get(from->paperdoll[Character::Weapon]);
	const pub::EifRecord& shielddata = this->world->eif->Get(from->paperdoll[Character::Shield]);

	if (wepdata.subtype == pub::ItemSubtype::Ranged && shielddata.subtype != pub::ItemSubtype::Arrows)
	{
		// Ranged gun hack
		if (from->paperdoll[Character::Weapon] != 365 || wepdata.name != "Gun")
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

	int wep_graphic = AsEquipment(wepdata).doll_graphic;
	bool is_instrument = (wep_graphic != 0 && this->world->IsInstrument(wep_graphic));

	if (!is_instrument && (this->pk || (this->world->config["GlobalPK"] && !this->world->PKExcept(this->id))))
	{
		if (this->AttackPK(from, direction))
		{
			return;
		}
	}

	server::AttackPlayerServerPacket attack;
	attack.player_id = from->PlayerID();
	attack.direction = direction;

	this->SendToNearby(from, attack, false);

	if (is_instrument)
		return;

	if (!from->CanInteractCombat())
		return;

	int target_x = from->loc.x;
	int target_y = from->loc.y;

	int range = 1;

	if (wepdata.subtype == pub::ItemSubtype::Ranged)
	{
		range = static_cast<int>(this->world->config["RangedDistance"]);
	}

	for (int i = 0; i < range; ++i)
	{
		switch (from->direction)
		{
			case Direction::Up:
				target_y -= 1;
				break;

			case Direction::Right:
				target_x += 1;
				break;

			case Direction::Down:
				target_y += 1;
				break;

			case Direction::Left:
				target_x -= 1;
				break;
		}

		UTIL_FOREACH(this->npcs, npc)
		{
			if ((npc->PubData().type == pub::NpcType::Passive || npc->PubData().type == pub::NpcType::Aggressive || from->SourceDutyAccess() >= static_cast<int>(this->world->admin_config["killnpc"]))
			 && npc->alive && npc->loc.x == target_x && npc->loc.y == target_y)
			{
				int amount = util::rand(from->mindam, from->maxdam);
				double rand = util::rand(0.0, 1.0);
				// Checks if target is facing you
				bool critical = std::abs(int(npc->direction) - int(from->direction)) != 2 || rand < static_cast<double>(this->world->config["CriticalRate"]);

				if (this->world->config["CriticalFirstHit"] && npc->hp == npc->PubData().hp)
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

	int target_x = from->loc.x;
	int target_y = from->loc.y;

	int range = 1;

	if (this->world->eif->Get(from->paperdoll[Character::Weapon]).subtype == pub::ItemSubtype::Ranged)
	{
		range = static_cast<int>(this->world->config["RangedDistance"]);
	}

	for (int i = 0; i < range; ++i)
	{
		switch (from->direction)
		{
			case Direction::Up:
				target_y -= 1;
				break;

			case Direction::Right:
				target_x += 1;
				break;

			case Direction::Down:
				target_y += 1;
				break;

			case Direction::Left:
				target_x -= 1;
				break;
		}

		UTIL_FOREACH(this->characters, character)
		{
			if (character->mapid == this->id && !character->nowhere && character->loc.x == target_x && character->loc.y == target_y)
			{
				int amount = util::rand(from->mindam, from->maxdam);
				double rand = util::rand(0.0, 1.0);
				// Checks if target is facing you
				bool critical = std::abs(int(character->direction) - int(from->direction)) != 2 || rand < static_cast<double>(this->world->config["CriticalRate"]);

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

				server::AvatarReplyServerPacket avatar_reply;
				avatar_reply.victim_id = character->PlayerID();
				avatar_reply.damage = amount;
				avatar_reply.direction = from->direction;
				avatar_reply.hp_percentage = util::clamp<int>(static_cast<int>(double(character->hp) / double(character->maxhp) * 100.0), 0, 100);
				avatar_reply.dead = character->hp == 0;

				// The attacker's own copy has no attacker id
				from->Send(avatar_reply);

				avatar_reply.player_id = from->PlayerID();

				UTIL_FOREACH(this->characters, checkchar)
				{
					if (from != checkchar && character->InRange(checkchar))
					{
						checkchar->Send(avatar_reply);
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

				server::RecoverPlayerServerPacket recover_packet;
				recover_packet.hp = character->hp;
				recover_packet.tp = character->tp;
				character->Send(recover_packet);

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

	server::FacePlayerServerPacket packet;
	packet.player_id = from->PlayerID();
	packet.direction = direction;

	this->SendToNearby(from, packet, false);
}

void Map::Sit(Character *from, SitState sit_type)
{
	from->sitting = sit_type;

	from->CancelSpell();

	if (sit_type == SitState::Chair)
	{
		this->SendToNearby(from, SitPacketFor<server::ChairPlayerServerPacket>(from), false);
	}
	else
	{
		this->SendToNearby(from, SitPacketFor<server::SitPlayerServerPacket>(from), false);
	}
}

void Map::Stand(Character *from)
{
	from->sitting = SitState::Stand;

	from->CancelSpell();

	server::SitRemoveServerPacket packet;
	packet.player_id = from->PlayerID();
	packet.coords = from->loc;

	this->SendToNearby(from, packet, false);
}

void Map::Emote(Character *from, eolib::protocol::Emote emote, bool echo)
{
	server::EmotePlayerServerPacket packet;
	packet.player_id = from->PlayerID();
	packet.emote = emote;

	this->SendToNearby(from, packet, echo);
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

			if (character->loc.x == x && character->loc.y == y && !ghost)
			{
				return true;
			}
		}
	}

	if (target != Map::PlayerOnly)
	{
		UTIL_FOREACH(this->npcs, npc)
		{
			if (npc->alive && npc->loc.x == x && npc->loc.y == y)
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

bool Map::OpenDoor(Character *from, const eolib::protocol::Coords& coords)
{
	if (!this->InBounds(coords.x, coords.y) || (from && !from->InRange(coords.x, coords.y)))
	{
		return false;
	}

	if (Map_Warp& warp = this->GetWarp(coords.x, coords.y))
	{
		if (warp.spec == Map_Warp::NoDoor || warp.open)
		{
			return false;
		}

		if (from && warp.spec > Map_Warp::Door)
		{
			int keynum = warp.spec - static_cast<int>(Map_Warp::Door) + 1;
			if (!from->CanInteractDoors() || !from->HasItem(this->world->eif->GetKey(keynum)))
			{
				server::DoorCloseServerPacket packet;
				packet.key = keynum;
				from->Send(packet);

				return false;
			}
		}

		server::DoorOpenServerPacket packet;
		packet.coords = coords;

		this->SendToNearby(coords, packet);

		warp.open = true;

		map_close_door_struct *close = new map_close_door_struct;
		close->map = this;
		close->loc = coords;

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
	const pub::EsfRecord* spell;

	if (!from->world->esf->TryGet(spell_id, spell) || spell->type != pub::SkillType::Heal || from->tp < spell->tp_cost)
		return;

	from->tp -= spell->tp_cost;

	int hpgain = spell->hp_heal;

	if (this->world->config["LimitDamage"])
		hpgain = std::min(hpgain, from->maxhp - from->hp);

	hpgain = std::max(hpgain, 0);

	from->hp += hpgain;

	from->hp = std::min(from->hp, from->maxhp);

	server::SpellTargetSelfServerPacket packet;
	packet.player_id = from->PlayerID();
	packet.spell_id = spell_id;
	packet.spell_heal_hp = spell->hp_heal;
	packet.hp_percentage = util::clamp<int>(static_cast<int>(double(from->hp) / double(from->maxhp) * 100.0), 0, 100);

	this->SendToNearby(from, packet, false);

	packet.hp = from->hp;
	packet.tp = from->tp;

	from->Send(packet);
}

void Map::SpellAttack(Character *from, NPC *npc, unsigned short spell_id)
{
	if (!from->CanInteractCombat())
		return;

	const pub::EsfRecord* spell;

	if (!from->world->esf->TryGet(spell_id, spell) || spell->type != pub::SkillType::Attack || from->tp < spell->tp_cost)
		return;

	if ((npc->PubData().type == pub::NpcType::Passive || npc->PubData().type == pub::NpcType::Aggressive) && npc->alive)
	{
		from->tp -= spell->tp_cost;

		int amount = util::rand(from->mindam + spell->min_damage, from->maxdam + spell->max_damage);
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
	const pub::EsfRecord* spell;

	if (!from->world->esf->TryGet(spell_id, spell) || (spell->type != pub::SkillType::Heal && spell->type != pub::SkillType::Attack) || from->tp < spell->tp_cost)
		return;

	if (spell->type == pub::SkillType::Attack && (from->map->pk || (this->world->config["GlobalPK"] && !this->world->PKExcept(this->id))))
	{
		if (!from->CanInteractPKCombat())
			return;

		from->tp -= spell->tp_cost;

		int amount = util::rand(from->mindam + spell->min_damage, from->maxdam + spell->max_damage);
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

		server::AvatarAdminServerPacket avatar_admin;
		avatar_admin.caster_id = from->PlayerID();
		avatar_admin.victim_id = victim->PlayerID();
		avatar_admin.damage = amount;
		avatar_admin.caster_direction = from->direction;
		avatar_admin.hp_percentage = util::clamp<int>(static_cast<int>(double(victim->hp) / double(victim->maxhp) * 100.0), 0, 100);
		avatar_admin.victim_died = victim->hp == 0;
		avatar_admin.spell_id = spell_id;

		this->SendToNearby(victim, avatar_admin, true);

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

		server::RecoverPlayerServerPacket recover_packet;
		recover_packet.hp = victim->hp;
		recover_packet.tp = victim->tp;
		victim->Send(recover_packet);

		if (victim->party)
		{
			victim->party->UpdateHP(victim);
		}
	}
	else if (spell->type == pub::SkillType::Heal)
	{
		from->tp -= spell->tp_cost;

		int displayhp = spell->hp_heal;
		int hpgain = spell->hp_heal;

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

		server::SpellTargetOtherServerPacket packet;
		packet.victim_id = victim->PlayerID();
		packet.caster_id = from->PlayerID();
		packet.target_type = eolib::protocol::net::client::SpellTargetType::Player;
		packet.spell_id = spell_id;
		packet.spell_heal_hp = displayhp;
		packet.hp_percentage = util::clamp<int>(static_cast<int>(double(victim->hp) / double(victim->maxhp) * 100.0), 0, 100);

		this->SendToNearby(victim, packet, false);

		packet.hp = victim->hp;

		victim->Send(packet);

		if (victim->party)
		{
			victim->party->UpdateHP(victim);
		}
	}

	server::RecoverPlayerServerPacket recover_packet;
	recover_packet.hp = from->hp;
	recover_packet.tp = from->tp;
	from->Send(recover_packet);
}

void Map::SpellGroup(Character *from, unsigned short spell_id)
{
	const pub::EsfRecord* spell;

	if (!from->world->esf->TryGet(spell_id, spell) || spell->type != pub::SkillType::Heal || !from->party || from->tp < spell->tp_cost)
		return;

	from->tp -= spell->tp_cost;

	int displayhp = spell->hp_heal;

	if (!from->CanInteractCombat() && !(from->CanInteractPKCombat() && (from->map->pk || (this->world->config["GlobalPK"] && !this->world->PKExcept(this->id)))))
	{
		displayhp = std::min(displayhp, 1);
	}

	std::set<Character *> in_range;

	server::SpellTargetGroupServerPacket packet;
	packet.spell_id = spell_id;
	packet.caster_id = from->PlayerID();
	packet.caster_tp = from->tp;
	packet.spell_heal_hp = displayhp;

	UTIL_FOREACH(from->party->members, member)
	{
		if (member->map != from->map)
			continue;

		int hpgain = spell->hp_heal;

		if (this->world->config["LimitDamage"])
			hpgain = std::min(hpgain, member->maxhp - member->hp);

		hpgain = std::max(hpgain, 0);

		if (!from->CanInteractCombat() && !(from->CanInteractPKCombat() && (from->map->pk || (this->world->config["GlobalPK"] && !this->world->PKExcept(this->id)))))
			hpgain = std::min(hpgain, 1);

		member->hp += hpgain;

		if (!this->world->config["LimitDamage"])
			member->hp = std::min(member->hp, member->maxhp);

		server::GroupHealTargetPlayer target;
		target.player_id = member->PlayerID();
		target.hp_percentage = util::clamp<int>(static_cast<int>(double(member->hp) / double(member->maxhp) * 100.0), 0, 100);
		target.hp = member->hp;
		packet.players.push_back(target);

		UTIL_FOREACH(this->characters, character)
		{
			if (member->InRange(character))
				in_range.insert(character);
		}
	}

	UTIL_FOREACH(in_range, character)
	{
		character->Send(packet);
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
			if (item->loc.x == x && item->loc.y == y)
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

	server::ItemAddServerPacket packet;
	packet.item_id = newitem->id;
	packet.item_index = newitem->uid;
	packet.item_amount = newitem->amount;
	packet.coords = newitem->loc;

	this->SendToNearby(*newitem, packet, from);

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
	server::ItemRemoveServerPacket remove;
	remove.item_index = (*it)->uid;
	this->SendToNearby(**it, remove, from);

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

				server::ItemRemoveServerPacket remove;
				remove.item_index = (*it)->uid;
				this->SendToNearby(**it, remove, from);

				server::ItemAddServerPacket packet;
				packet.item_id = (*it)->id;
				packet.item_index = (*it)->uid;
				packet.item_amount = (*it)->amount;
				packet.coords = (*it)->loc;

				this->SendToNearby(**it, packet);
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

	if (this->world->config["GhostArena"] && this->GetTile(x, y).tilespec == emf::MapTileSpec::Arena && this->Occupied(x, y, PlayerAndNPC))
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

emf::MapTileSpec Map::GetSpec(unsigned char x, unsigned char y) const
{
	if (!InBounds(x, y))
		return Map_Tile::NoSpec;

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
		if (util::path_length(character->loc.x, character->loc.y, x, y) <= range)
			characters.push_back(character);
	}

	return characters;
}

std::vector<NPC *> Map::NPCsInRange(unsigned char x, unsigned char y, unsigned char range)
{
	std::vector<NPC *> npcs;

	UTIL_FOREACH(this->npcs, npc)
	{
		if (util::path_length(npc->loc.x, npc->loc.y, x, y) <= range)
			npcs.push_back(npc);
	}

	return npcs;
}

void Map::Quake(int strength)
{
	server::EffectUseServerPacket::EffectDataQuake quake;
	quake.quake_strength = strength;
	this->SendToMap(server::EffectUseServerPacket::ForQuake(quake));
}

void Map::TileEffect(unsigned char x, unsigned char y, unsigned short effect)
{
	server::TileEffect tile_effect;
	tile_effect.coords.x = x;
	tile_effect.coords.y = y;
	tile_effect.effect_id = effect;

	server::EffectAgreeServerPacket packet;
	packet.effects.push_back(tile_effect);

	this->SendToNearby(tile_effect.coords, packet);
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

std::array<int, 2> Map::ClientRid() const
{
	std::array<int, 2> result = this->rid;

	if (this->world->config["GlobalPK"] && !this->world->PKExcept(this->id))
		result[0] = Map::GlobalPKRid(result[0]);

	return result;
}

int Map::GlobalPKRid(int rid)
{
	return (rid + 1) % static_cast<int>(eolib::data::EoNumericLimits::ShortMax);
}

void Map::PatchGlobalPK(std::vector<std::uint8_t>& content)
{
	if (content.size() <= emf_type_offset)
		return;

	const int rid = eolib::data::NumberEncoder::DecodeNumber(&content[emf_rid_offset], 2);
	const auto encoded_rid = eolib::data::NumberEncoder::EncodeNumber(Map::GlobalPKRid(rid));
	content[emf_rid_offset] = encoded_rid[0];
	content[emf_rid_offset + 1] = encoded_rid[1];

	content[emf_type_offset] = eolib::data::NumberEncoder::EncodeNumber(static_cast<int>(emf::MapType::Pk))[0];
}

bool Map::Reload()
{
	const std::string filename = this->Filename();
	std::vector<std::uint8_t> content;

	if (!EODataFile::Read(filename, content))
	{
		Console::Err("Could not load file: %s", filename.c_str());
		return false;
	}

	if (content.size() < emf_header_size)
	{
		Console::Err("Invalid file: %s", filename.c_str());
		return false;
	}

	const std::array<int, 2> checkrid{
		eolib::data::NumberEncoder::DecodeNumber(&content[emf_rid_offset], 2),
		eolib::data::NumberEncoder::DecodeNumber(&content[emf_rid_offset + 2], 2)
	};

	if (this->rid == checkrid)
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
		character->player->client->UploadMap(character->mapid, server::InitReply::MapMutation);
		character->Refresh(); // TODO: Find a better way to reload NPCs
	}

	this->exists = true;

	return true;
}

void Map::TimedSpikes()
{
	if (!this->has_timed_spikes)
		return;

	const server::EffectReportServerPacket spike_timer;

	double spike_damage = this->world->config["SpikeDamage"];

	std::vector<Character*> killed;

	for (Character* character : this->characters)
	{
		if (character->nowhere || character->IsHideInvisible())
			continue;

		emf::MapTileSpec spec = this->GetSpec(character->loc.x, character->loc.y);

		if (spike_damage > 0.0 && spec == emf::MapTileSpec::TimedSpikes)
		{
			int amount = static_cast<int>(character->maxhp * spike_damage);

			character->SpikeDamage(amount);

			if (character->hp == 0)
				killed.push_back(character);
		}
		else
		{
			character->Send(spike_timer);
		}
	}

	for (Character* character : killed)
	{
		character->DeathRespawn();
	}
}

void Map::TimedDrains()
{
	if (this->effect == emf::MapTimedEffect::HpDrain)
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

				server::EffectTargetOtherServerPacket packet;
				packet.damage = damage;
				packet.hp = character->hp;
				packet.max_hp = character->maxhp;

				std::size_t ii = 0;

				for (Character* other : this->characters)
				{
					if (other->nowhere || other->IsHideInvisible())
						continue;

					int damage = damage_map[ii++];

					if (!character->InRange(other) || other == character)
						continue;

					server::MapDrainDamageOther drained;
					drained.player_id = other->PlayerID();
					drained.hp_percentage = util::clamp<int>(static_cast<int>(double(other->hp) / double(other->maxhp) * 100.0), 0, 100);
					drained.damage = damage;
					packet.others.push_back(drained);
				}

				character->Send(packet);
			}
		}
	}

	if (this->effect == emf::MapTimedEffect::TpDrain)
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

				server::EffectSpecServerPacket::MapDamageTypeDataTpDrain tp_drain;
				tp_drain.tp_damage = amount;
				tp_drain.tp = character->tp;
				tp_drain.max_tp = character->maxtp;

				character->Send(server::EffectSpecServerPacket::ForTpDrain(tp_drain));
			}
		}
	}
}

void Map::TimedQuakes(bool initialize)
{
	std::string configString;
	switch (this->effect)
	{
		case emf::MapTimedEffect::Quake1: configString = "Quake1"; break;
		case emf::MapTimedEffect::Quake2: configString = "Quake2"; break;
		case emf::MapTimedEffect::Quake3: configString = "Quake3"; break;
		case emf::MapTimedEffect::Quake4: configString = "Quake4"; break;
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
			this->Quake(util::clamp(quakeStrength, 0, 8));
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
