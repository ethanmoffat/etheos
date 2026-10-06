
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef MAP_HPP_INCLUDED
#define MAP_HPP_INCLUDED

#include "fwd/map.hpp"

#include "fwd/arena.hpp"
#include "fwd/character.hpp"
#include "fwd/npc.hpp"
#include "fwd/wedding.hpp"
#include "fwd/world.hpp"

#include <eolib/protocol/map/enums.hpp>
#include <eolib/protocol/net/packet.hpp>
#include <eolib/protocol/structs.hpp>

#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

/**
 * Object representing an item on the floor of a map
 */
struct Map_Item
{
	short uid;
	short id;
	int amount;
	eolib::protocol::Coords loc;
	unsigned int owner; // Player ID
	double unprotecttime;

	Map_Item(short uid_, short id_, int amount_, unsigned char x_, unsigned char y_, unsigned int owner_, double unprotecttime_)
	 : uid(uid_), id(id_), amount(amount_), owner(owner_), unprotecttime(unprotecttime_)
	{
		loc.x = x_;
		loc.y = y_;
	}
};

/**
 * Object representing a warp tile on a map, as well as storing door state
 */
struct Map_Warp
{
	short map;
	eolib::protocol::Coords loc;
	unsigned char levelreq;

	enum WarpSpec
	{
		NoDoor,
		Door,
		LockedSilver,
		LockedCrystal,
		LockedWraith
	};

	WarpSpec spec;
	bool open;

	Map_Warp() : map(0), levelreq(0), spec(Map_Warp::NoDoor), open(false) {}

	explicit operator bool() const
	{
		return map != 0;
	}
};

/**
 * Object representing one tile on a map
 */
struct Map_Tile
{
	// Tiles without a spec in the map file; eolib has no value for this
	static constexpr eolib::protocol::map::MapTileSpec NoSpec = static_cast<eolib::protocol::map::MapTileSpec>(-1);

	eolib::protocol::map::MapTileSpec tilespec;

	Map_Warp warp;

	Map_Tile() : tilespec(Map_Tile::NoSpec) { }

	bool Walkable(bool npc = false) const
	{
		if (this->warp && npc)
		{
			return false;
		}

		switch (this->tilespec)
		{
			case eolib::protocol::map::MapTileSpec::Wall:
			case eolib::protocol::map::MapTileSpec::ChairDown:
			case eolib::protocol::map::MapTileSpec::ChairLeft:
			case eolib::protocol::map::MapTileSpec::ChairRight:
			case eolib::protocol::map::MapTileSpec::ChairUp:
			case eolib::protocol::map::MapTileSpec::ChairDownRight:
			case eolib::protocol::map::MapTileSpec::ChairUpLeft:
			case eolib::protocol::map::MapTileSpec::ChairAll:
			case eolib::protocol::map::MapTileSpec::Chest:
			case eolib::protocol::map::MapTileSpec::BankVault:
			case eolib::protocol::map::MapTileSpec::Edge:
			case eolib::protocol::map::MapTileSpec::Board1:
			case eolib::protocol::map::MapTileSpec::Board2:
			case eolib::protocol::map::MapTileSpec::Board3:
			case eolib::protocol::map::MapTileSpec::Board4:
			case eolib::protocol::map::MapTileSpec::Board5:
			case eolib::protocol::map::MapTileSpec::Board6:
			case eolib::protocol::map::MapTileSpec::Board7:
			case eolib::protocol::map::MapTileSpec::Board8:
			case eolib::protocol::map::MapTileSpec::Jukebox:
				return false;
			case eolib::protocol::map::MapTileSpec::NpcBoundary:
				return !npc;
			default:
				return true;
		}
	}
};

/**
 * Object representing an item in a chest on a map
 */
struct Map_Chest_Item
{
	short id;
	int amount;
	int slot;
};

/**
 * Object representing an item spawn in a chest on a map
 */
struct Map_Chest_Spawn
{
	int spawnid;
	Map_Chest_Item item;
	int slot;
	double time;
	double last_taken;
	double spawn_random;
};

/**
 * Object representing a chest on a map
 */
struct Map_Chest
{
	eolib::protocol::Coords loc;

	std::list<Map_Chest_Item> items;
	std::list<Map_Chest_Spawn> spawns;
	int slots;

	int maxchest;
	int chestslots;
	short key;

	int HasItem(short item) const;
	int AddItem(short item, int amount, int slot = 0);
	int DelItem(short item);
	int DelSomeItem(short item, int amount);

	void Update(Map *map, Character *exclude = 0) const;
};

/**
 * Contains all information about a map, holds reference to contained Characters and manages NPCs on it
 */
class Map
{
	private:
		bool Load();
		void Unload();

	public:
		enum WalkResult
		{
			WalkFail = 0,
			WalkOK = 1,
			WalkWarped = 2
		};

		World *world;
		short id;
		std::array<int, 2> rid;
		bool pk;
		eolib::protocol::map::MapTimedEffect effect;
		int filesize;
		unsigned char width;
		unsigned char height;
		bool scroll;
		unsigned char relog_x;
		unsigned char relog_y;
		std::list<Character *> characters;
		std::vector<NPC *> npcs;
		std::vector<std::shared_ptr<Map_Chest>> chests;
		std::list<std::shared_ptr<Map_Item>> items;
		std::vector<Map_Tile> tiles;
		bool exists;
		double jukebox_protect;
		std::string jukebox_player;
		bool evacuate_lock;
		bool has_timed_spikes;

		int currentQuakeTick, nextQuakeTick;

		Arena *arena;
		Wedding *wedding;

		Map(int id, World *world);
		void LoadArena();
		void LoadWedding();

		int GenerateItemID() const;
		unsigned char GenerateNPCIndex() const;

		std::string Filename() const;

		/**
		 * Gets the map's RID as sent to clients. With GlobalPK, the first half is replaced with GlobalPKRid.
		 */
		std::array<int, 2> ClientRid() const;

		/**
		 * Gets the value that replaces the first half of a map's RID under GlobalPK. It's never equal to rid, so
		 * clients cache the PK-patched map file separately from the original.
		 */
		static int GlobalPKRid(int rid);

		/**
		 * Patches map file content sent to clients under GlobalPK: the map type is set to PK and the first half of the
		 * RID is replaced with GlobalPKRid. Content too short to be a map file is left as it is.
		 */
		static void PatchGlobalPK(std::vector<std::uint8_t>& content);

		void Enter(Character *, WarpEffect effect = WarpEffect::None);
		void Leave(Character *, WarpEffect effect = WarpEffect::None, bool silent = false);

		/**
		 * Sends packet to every character on the map that can see from. echo includes from itself; except is skipped.
		 */
		void SendToNearby(const Character *from, const eolib::protocol::net::Packet& packet, bool echo);
		void SendToNearby(const NPC *from, const eolib::protocol::net::Packet& packet, const Character *except = nullptr);
		void SendToNearby(const eolib::protocol::Coords& coords, const eolib::protocol::net::Packet& packet);

		/**
		 * Sends packet to every character on the map.
		 */
		void SendToMap(const eolib::protocol::net::Packet& packet);

		void Msg(Character *from, std::string message, bool echo = true);
		void Msg(NPC *from, std::string message);
		WalkResult Walk(Character *from, Direction direction, bool admin = false);
		void Attack(Character *from, Direction direction);
		bool AttackPK(Character *from, Direction direction);
		void Face(Character *from, Direction direction);
		void Sit(Character *from, SitState sit_type);
		void Stand(Character *from);
		void Emote(Character *from, eolib::protocol::Emote emote, bool echo = true);
		bool OpenDoor(Character *from, const eolib::protocol::Coords& coords);
		void CloseDoor(unsigned char x, unsigned char y);

		void SpellSelf(Character *from, unsigned short spell_id);
		void SpellAttack(Character *from, NPC *victim, unsigned short spell_id);
		void SpellAttackPK(Character *from, Character *victim, unsigned short spell_id);
		void SpellGroup(Character *from, unsigned short spell_id);

		WalkResult Walk(NPC *from, Direction direction);

		std::shared_ptr<Map_Item> AddItem(short id, int amount, unsigned char x, unsigned char y, Character *from = 0);

		std::shared_ptr<Map_Item> GetItem(short uid);
		std::shared_ptr<const Map_Item> GetItem(short uid) const;

		void DelItem(short uid, Character *from = 0);
		void DelSomeItem(short uid, int amount, Character *from = 0);
		std::list<std::shared_ptr<Map_Item>>::iterator DelItem(std::list<std::shared_ptr<Map_Item>>::iterator it, Character *from = 0);

		bool InBounds(unsigned char x, unsigned char y) const;
		bool Walkable(unsigned char x, unsigned char y, bool npc = false) const;
		Map_Tile& GetTile(unsigned char x, unsigned char y);
		const Map_Tile& GetTile(unsigned char x, unsigned char y) const;
		eolib::protocol::map::MapTileSpec GetSpec(unsigned char x, unsigned char y) const;
		Map_Warp& GetWarp(unsigned char x, unsigned char y);
		const Map_Warp& GetWarp(unsigned char x, unsigned char y) const;

		std::vector<Character *> CharactersInRange(unsigned char x, unsigned char y, unsigned char range);
		std::vector<NPC *> NPCsInRange(unsigned char x, unsigned char y, unsigned char range);

		void Quake(int strength);

		void TileEffect(unsigned char x, unsigned char y, unsigned short effect);

		bool Evacuate();

		bool Reload();

		void TimedSpikes();
		void TimedDrains();
		void TimedQuakes(bool initialize = false);

		Character *GetCharacter(std::string name);
		Character *GetCharacterPID(unsigned int id);
		Character *GetCharacterCID(unsigned int id);
		NPC *GetNPCIndex(unsigned char index);

		enum OccupiedTarget
		{
			PlayerOnly,
			NPCOnly,
			PlayerAndNPC
		};
		bool Occupied(unsigned char x, unsigned char y, Map::OccupiedTarget target, bool adminghost = false) const;

		~Map();
};

#endif // MAP_HPP_INCLUDED
