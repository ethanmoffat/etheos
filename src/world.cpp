
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "world.hpp"

#include "character.hpp"
#include "command_source.hpp"
#include "config.hpp"
#include "database.hpp"
#include "eoclient.hpp"
#include "eodata.hpp"
#include "eoplus.hpp"
#include "eoserver.hpp"
#include "guild.hpp"
#include "i18n.hpp"
#include "map.hpp"
#include "npc.hpp"
#include "npc_data.hpp"
#include "packet.hpp"
#include "party.hpp"
#include "player.hpp"
#include "quest.hpp"
#include "timer.hpp"
#include "commands/commands.hpp"
#include "handlers/handlers.hpp"

#include "console.hpp"
#include "util.hpp"
#include "util/secure_string.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <fstream>
#include <limits>
#include <list>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "json.hpp"

void world_spawn_npcs(void *world_void)
{
	World *world(static_cast<World *>(world_void));

	double spawnrate = world->config["SpawnRate"];
	double current_time = Timer::GetTime();
	UTIL_FOREACH(world->maps, map)
	{
		UTIL_FOREACH(map->npcs, npc)
		{
			if ((!npc->alive && npc->dead_since + (double(npc->spawn_time) * spawnrate) < current_time)
			 && (!npc->ENF().child || (npc->parent && npc->parent->alive && world->config["RespawnBossChildren"])))
			{
#ifdef DEBUG
				Console::Dbg("Spawning NPC %i on map %i", npc->id, map->id);
#endif // DEBUG
				npc->Spawn();
			}
		}
	}
}

void world_act_npcs(void *world_void)
{
	World *world(static_cast<World *>(world_void));

	double current_time = Timer::GetTime();
	UTIL_FOREACH(world->maps, map)
	{
		UTIL_FOREACH(map->npcs, npc)
		{
			if (npc->alive && npc->last_act + npc->act_speed < current_time)
			{
				npc->Act();
			}
		}
	}
}

void world_talk_npcs(void *world_void)
{
	World *world(static_cast<World *>(world_void));

	double current_time = Timer::GetTime();
	UTIL_FOREACH(world->maps, map)
	{
		UTIL_FOREACH(map->npcs, npc)
		{
			if (npc->alive && npc->last_talk + npc->Data().talk_speed < current_time)
			{
				npc->Talk();
			}
		}
	}
}

void world_recover(void *world_void)
{
	World *world(static_cast<World *>(world_void));

	UTIL_FOREACH(world->characters, character)
	{
		bool updated = false;

		if (character->hp != character->maxhp)
		{
			if (character->sitting != SIT_STAND) character->hp += static_cast<short>(character->maxhp * double(world->config["SitHPRecoverRate"]));
			else                                 character->hp += static_cast<short>(character->maxhp * double(world->config["HPRecoverRate"]));

			character->hp = std::min(character->hp, character->maxhp);
			updated = true;

			if (character->party)
			{
				character->party->UpdateHP(character);
			}
		}

		if (character->tp != character->maxtp)
		{
			if (character->sitting != SIT_STAND) character->tp += static_cast<short>(character->maxtp * double(world->config["SitTPRecoverRate"]));
			else                                 character->tp += static_cast<short>(character->maxtp * double(world->config["TPRecoverRate"]));

			character->tp = std::min(character->tp, character->maxtp);
			updated = true;
		}

		if (updated)
		{
			PacketBuilder builder(PACKET_RECOVER, PACKET_PLAYER, 6);
			builder.AddShort(character->hp);
			builder.AddShort(character->tp);
			builder.AddShort(0); // ?
			character->Send(builder);
		}
	}
}

void world_npc_recover(void *world_void)
{
	World *world(static_cast<World *>(world_void));

	UTIL_FOREACH(world->maps, map)
	{
		UTIL_FOREACH(map->npcs, npc)
		{
			if (npc->alive && npc->hp < npc->ENF().hp)
			{
				npc->hp += static_cast<int>(npc->ENF().hp * double(world->config["NPCRecoverRate"]));

				npc->hp = std::min(npc->hp, npc->ENF().hp);
			}
		}
	}
}

void world_warp_suck(void *world_void)
{
	struct Warp_Suck_Action
	{
		Character *character;
		short map;
		unsigned char x;
		unsigned char y;

		Warp_Suck_Action(Character *character, short map, unsigned char x, unsigned char y)
			: character(character)
			, map(map)
			, x(x)
			, y(y)
		{ }
	};

	std::vector<Warp_Suck_Action> actions;

	World *world(static_cast<World *>(world_void));

	double now = Timer::GetTime();
	double delay = world->config["WarpSuck"];

	UTIL_FOREACH(world->maps, map)
	{
		UTIL_FOREACH(map->characters, character)
		{
			if (character->last_walk + delay >= now)
				continue;

			auto check_warp = [&](bool test, unsigned char x, unsigned char y)
			{
				if (!test || !map->InBounds(x, y))
					return;

				const Map_Warp& warp = map->GetWarp(x, y);

				if (!warp || warp.levelreq > character->level || (warp.spec != Map_Warp::Door && warp.spec != Map_Warp::NoDoor))
					return;

				actions.push_back({character, warp.map, warp.x, warp.y});
			};

			character->last_walk = now;

			check_warp(true,                       character->x,     character->y);
			check_warp(character->x > 0,           character->x - 1, character->y);
			check_warp(character->x < map->width,  character->x + 1, character->y);
			check_warp(character->y > 0,           character->x,     character->y - 1);
			check_warp(character->y < map->height, character->x,     character->y + 1);
		}
	}

	UTIL_FOREACH(actions, act)
	{
		if (act.character->SourceAccess() < ADMIN_GUIDE && world->GetMap(act.map)->evacuate_lock)
		{
			act.character->StatusMsg(world->i18n.Format("map_evacuate_block"));
			act.character->Refresh();
		}
		else
		{
			act.character->Warp(act.map, act.x, act.y);
		}
	}
}

void world_despawn_items(void *world_void)
{
	World *world = static_cast<World *>(world_void);

	UTIL_FOREACH(world->maps, map)
	{
		restart_loop:
		UTIL_FOREACH(map->items, item)
		{
			if (item->unprotecttime < (Timer::GetTime() - static_cast<double>(world->config["ItemDespawnRate"])))
			{
				map->DelItem(item->uid, 0);
				goto restart_loop;
			}
		}
	}
}

void world_timed_save(void *world_void)
{
	World *world = static_cast<World *>(world_void);

	if (!world->config["TimedSave"])
		return;

	world->db->BeginTransaction();

	UTIL_FOREACH(world->characters, character)
	{
		character->Save();
	}

	world->guildmanager->SaveAll();

	try
	{
		world->db->Commit();
	}
	catch (Database_Exception& e)
	{
		(void)e;
		Console::Wrn("Database commit failed - no data was saved!");
		world->db->Rollback();
	}
}

void world_spikes(void *world_void)
{
	World *world = static_cast<World *>(world_void);
	for (Map* map : world->maps)
	{
		if (map->exists)
			map->TimedSpikes();
	}
}

void world_drains(void *world_void)
{
	World *world = static_cast<World *>(world_void);
	for (Map* map : world->maps)
	{
		if (map->exists)
			map->TimedDrains();
	}
}

void world_quakes(void *world_void)
{
	World *world = static_cast<World *>(world_void);
	for (Map* map : world->maps)
	{
		if (map->exists)
			map->TimedQuakes();
	}
}

void World::UpdateConfig()
{
	this->timer.SetMaxDelta(this->config["ClockMaxDelta"]);

	double rate_face = this->config["PacketRateFace"];
	double rate_walk = this->config["PacketRateWalk"];
	double rate_attack = this->config["PacketRateAttack"];

	Handlers::SetDelay(PACKET_FACE, PACKET_PLAYER, rate_face);

	Handlers::SetDelay(PACKET_WALK, PACKET_ADMIN, rate_walk);
	Handlers::SetDelay(PACKET_WALK, PACKET_PLAYER, rate_walk);
	Handlers::SetDelay(PACKET_WALK, PACKET_SPEC, rate_walk);

	Handlers::SetDelay(PACKET_ATTACK, PACKET_USE, rate_attack);

	std::array<double, 7> npc_speed_table;

	std::vector<std::string> rate_list = util::explode(',', this->config["NPCMovementRate"]);

	for (std::size_t i = 0; i < std::min<std::size_t>(7, rate_list.size()); ++i)
	{
		if (i < rate_list.size())
			npc_speed_table[i] = util::tdparse(rate_list[i]);
		else
			npc_speed_table[i] = 1.0;
	}

	NPC::SetSpeedTable(npc_speed_table);

	this->i18n.SetLangFile(this->config["ServerLanguage"]);

	this->instrument_ids.clear();

	std::vector<std::string> instrument_list = util::explode(',', this->config["InstrumentItems"]);
	this->instrument_ids.reserve(instrument_list.size());

	for (std::size_t i = 0; i < instrument_list.size(); ++i)
	{
		this->instrument_ids.push_back(int(util::tdparse(instrument_list[i])));
	}

	if (this->db->Pending() && !this->config["TimedSave"])
	{
		try
		{
			this->db->Commit();
		}
		catch (Database_Exception& e)
		{
			(void)e;
			Console::Wrn("Database commit failed - no data was saved!");
			this->db->Rollback();
		}
	}
}

World::World(std::shared_ptr<DatabaseFactory> databaseFactory, const Config &eoserv_config, const Config &admin_config)
	: databaseFactory(databaseFactory)
	, config(eoserv_config)
	, admin_config(admin_config)
	, i18n(eoserv_config.find("ServerLanguage")->second)
	, admin_count(0)
{
	this->db = databaseFactory->CreateDatabase(this->config, true);
	this->Initialize();
}

void World::Initialize()
{
	if (int(this->timer.resolution * 1000.0) > 1)
	{
		Console::Out("Timers set at approx. %i ms resolution", int(this->timer.resolution * 1000.0));
	}
	else
	{
		Console::Out("Timers set at < 1 ms resolution");
	}

	this->passwordHashers[SHA256].reset(new Sha256Hasher());
	this->passwordHashers[BCRYPT].reset(new BcryptHasher(int(this->config["BcryptWorkload"])));
	this->loginManager.reset(new LoginManager(databaseFactory, this->config, this->passwordHashers));

	try
	{
		this->drops_config.Read(this->config["DropsFile"]);
		this->shops_config.Read(this->config["ShopsFile"]);
		this->arenas_config.Read(this->config["ArenasFile"]);
		this->formulas_config.Read(this->config["FormulasFile"]);
		this->home_config.Read(this->config["HomeFile"]);
		this->skills_config.Read(this->config["SkillsFile"]);
		this->speech_config.Read(this->config["SpeechFile"]);
	}
	catch (std::runtime_error &e)
	{
		Console::Wrn(e.what());
	}

	this->UpdateConfig();
	this->LoadHome();

	this->eif = new EIF(this->config["EIF"]);
	this->enf = new ENF(this->config["ENF"]);
	this->esf = new ESF(this->config["ESF"]);
	this->ecf = new ECF(this->config["ECF"]);

	std::size_t num_npcs = this->enf->data.size();
	this->npc_data.resize(num_npcs);
	for (std::size_t i = 0; i < num_npcs; ++i)
	{
		auto& npc = this->npc_data[i];
		npc.reset(new NPC_Data(this, static_cast<short>(i)));
		if (npc->id != 0)
			npc->Load();
	}

	this->maps.resize(static_cast<int>(this->config["Maps"]));
	int loaded = 0;
	for (int i = 0; i < static_cast<int>(this->config["Maps"]); ++i)
	{
		this->maps[i] = new Map(i + 1, this);
		if (this->maps[i]->exists)
			++loaded;
	}
	Console::Out("%i/%i maps loaded.", loaded, static_cast<int>(this->maps.size()));

	short max_quest = static_cast<int>(this->config["Quests"]);

	UTIL_FOREACH(this->enf->data, npc)
	{
		if (npc.type == ENF::Quest)
			max_quest = std::max(max_quest, npc.vendor_id);
	}

	for (short i = 0; i <= max_quest; ++i)
	{
		try
		{
			std::shared_ptr<Quest> q = std::make_shared<Quest>(i, this);
			this->quests.insert(std::make_pair(i, std::move(q)));
		}
		catch (...)
		{

		}
	}
	Console::Out("%i/%i quests loaded.", static_cast<int>(this->quests.size()), max_quest);

	this->last_character_id = 0;

	TimeEvent *event = new TimeEvent(world_spawn_npcs, this, 1.0, Timer::FOREVER);
	this->timer.Register(event);

	event = new TimeEvent(world_act_npcs, this, 0.05, Timer::FOREVER);
	this->timer.Register(event);

	event = new TimeEvent(world_talk_npcs, this, 1.0, Timer::FOREVER);
	this->timer.Register(event);

	if (int(this->config["RecoverSpeed"]) > 0)
	{
		event = new TimeEvent(world_recover, this, double(this->config["RecoverSpeed"]), Timer::FOREVER);
		this->timer.Register(event);
	}

	if (int(this->config["NPCRecoverSpeed"]) > 0)
	{
		event = new TimeEvent(world_npc_recover, this, double(this->config["NPCRecoverSpeed"]), Timer::FOREVER);
		this->timer.Register(event);
	}

	if (int(this->config["WarpSuck"]) > 0)
	{
		event = new TimeEvent(world_warp_suck, this, 1.0, Timer::FOREVER);
		this->timer.Register(event);
	}

	if (this->config["ItemDespawn"])
	{
		event = new TimeEvent(world_despawn_items, this, static_cast<double>(this->config["ItemDespawnCheck"]), Timer::FOREVER);
		this->timer.Register(event);
	}

	if (this->config["TimedSave"])
	{
		event = new TimeEvent(world_timed_save, this, static_cast<double>(this->config["TimedSave"]), Timer::FOREVER);
		this->timer.Register(event);
	}

	if (this->config["SpikeTime"])
	{
		event = new TimeEvent(world_spikes, this, static_cast<double>(this->config["SpikeTime"]), Timer::FOREVER);
		this->timer.Register(event);
	}

	if (this->config["DrainTime"])
	{
		event = new TimeEvent(world_drains, this, static_cast<double>(this->config["DrainTime"]), Timer::FOREVER);
		this->timer.Register(event);
	}

	if (this->config["QuakeRate"])
	{
		event = new TimeEvent(world_quakes, this, static_cast<double>(this->config["QuakeRate"]), Timer::FOREVER);
		this->timer.Register(event);
	}

	exp_table[0] = 0;
	for (std::size_t i = 1; i < this->exp_table.size(); ++i)
	{
		exp_table[i] = int(util::round(std::pow(double(i), 3.0) * 133.1));
	}

	for (std::size_t i = 0; i < this->boards.size(); ++i)
	{
		this->boards[i] = new Board(i);
	}

	this->guildmanager = new GuildManager(this);
}

void World::DumpToFile(const std::string& fileName)
{
	nlohmann::json dump;

	std::ifstream existing(fileName);
	if (existing.is_open())
	{
		// right now merge means "overwrite file backup with live data if there are duplicates"
		// eventually this could become more advanced but it probably isn't necessary
		existing >> dump;
		existing.close();
	}

	if (dump.find("characters") == dump.end())
		dump["characters"] = nlohmann::json::array();

	UTIL_FOREACH_CREF(this->characters, c)
	{
		auto existing = std::find_if(dump["characters"].begin(), dump["characters"].end(),
			[&c](nlohmann::json check)
			{
				return check.find("name") != check.end() && check["name"].get<std::string>() == c->real_name;
			});
		if (existing != dump["characters"].end())
			dump["characters"].erase(existing);

		auto nextC = nlohmann::json::object();

		nextC["name"] = c->real_name;
		nextC["account"] = c->player->username;
		nextC["title"] = c->title;
		nextC["class"] = c->clas;
		nextC["home"] = c->home;
		nextC["fiance"] = c->fiance;
		nextC["partner"] = c->partner;
		nextC["gender"] = c->gender;
		nextC["race"] = c->race;
		nextC["hairstyle"] = c->hairstyle;
		nextC["haircolor"] = c->haircolor;
		nextC["map"] = c->mapid;
		nextC["x"] = c->x;
		nextC["y"] = c->y;
		nextC["direction"] = c->direction;
		nextC["admin"] = c->admin;
		nextC["level"] = c->level;
		nextC["exp"] = c->exp;
		nextC["hp"] = c->hp;
		nextC["tp"] = c->tp;
		nextC["str"] = c->str;
		nextC["intl"] = c->intl;
		nextC["wis"] = c->wis;
		nextC["agi"] = c->agi;
		nextC["con"] = c->con;
		nextC["cha"] = c->cha;
		nextC["statpoints"] = c->statpoints;
		nextC["skillpoints"] = c->skillpoints;
		nextC["karma"] = c->karma;
		nextC["sitting"] = c->sitting;
		nextC["hidden"] = c->hidden;
		nextC["nointeract"] = c->nointeract;
		nextC["bankmax"] = c->bankmax;
		nextC["goldbank"] = c->goldbank;
		nextC["usage"] = c->usage;
		nextC["inventory"] = ItemSerialize(c->inventory);
		nextC["bank"] = ItemSerialize(c->bank);
		nextC["paperdoll"] = DollSerialize(c->paperdoll);
		nextC["spells"] = SpellSerialize(c->spells);
		nextC["guild"] = c->guild ? c->guild->tag : "";
		nextC["guildrank"] = c->guild_rank;
		nextC["guildrank_str"] = c->guild_rank_string;
		nextC["quest"] = c->quest_string.empty()
			? QuestSerialize(c->quests, c->quests_inactive)
			: c->quest_string;

		dump["characters"].push_back(nextC);
	}

	if (dump.find("mapState") == dump.end())
		dump["mapState"] = nlohmann::json::object();

	// overwrite all existing map item / chest spawns. prevents possibility of item dupes.
	dump["mapState"]["items"] = nlohmann::json::array();
	dump["mapState"]["chests"] = nlohmann::json::array();

	auto& items = dump["mapState"]["items"];
	auto& chests = dump["mapState"]["chests"];

	UTIL_FOREACH_CREF(this->maps, map)
	{
		UTIL_FOREACH_CREF(map->items, item)
		{
			items.push_back(
			{
				{ "mapId", map->id },
				{ "x", item->x },
				{ "y", item->y },
				{ "itemId", item->id },
				{ "amount", item->amount },
				{ "uid", item->uid }
			});
		}

		UTIL_FOREACH_CREF(map->chests, chest)
		{
			UTIL_FOREACH_CREF(chest->items, chestItem)
			{
				chests.push_back(
				{
					{ "mapId", map->id },
					{ "x", chest->x },
					{ "y", chest->y },
					{ "itemId", chestItem.id },
					{ "amount", chestItem.amount },
					{ "slot", chestItem.slot }
				});
			}
		}
	}

	if (dump.find("guilds") == dump.end())
		dump["guilds"] = nlohmann::json::array();

	UTIL_FOREACH_CREF(this->guildmanager->cache, guildPair)
	{
		std::shared_ptr<Guild> guild(guildPair.second);
		if (guild)
		{
			auto existing = std::find_if(dump["guilds"].begin(), dump["guilds"].end(),
				[&guild](nlohmann::json check)
				{
					return check.find("tag") != check.end() && check["tag"].get<std::string>() == guild->tag;
				});
			if (existing != dump["guilds"].end())
				dump["guilds"].erase(existing);

			dump["guilds"].push_back(
			{
				{ "tag", guild->tag },
				{ "name", guild->name },
				{ "description", guild->description },
				{ "ranks", RankSerialize(guild->ranks) },
				{ "bank", guild->bank }
			});
		}
	}

	std::ofstream file(fileName);
	if (file.bad()) {
		Console::Err("Error opening output file stream for world dump");
		return;
	}

	file << dump.dump(2) << std::endl;
	file.close();
}

void World::RestoreFromDump(const std::string& fileName)
{
	std::ifstream file(fileName);
	if (!file.is_open())
	{
#ifdef DEBUG
		Console::Dbg("Unable to open input file stream for world restore.");
#endif
		return;
	}

	nlohmann::json dump;
	file >> dump;
	file.close();

	bool in_tran = this->db->BeginTransaction();
	if (!in_tran)
	{
		Console::Wrn("Transaction open failed when restoring database");
	}

	try
	{
		auto c_iter = dump["characters"].cbegin();
		for (; c_iter != dump["characters"].cend();)
		{
			auto c = *c_iter;
			auto charName = c["name"].get<std::string>();

			auto exists = this->db->Query("SELECT `usage` FROM `characters` WHERE `name` = '$'", charName.c_str());
			if (exists.Error())
			{
				Console::Wrn("Error checking existence of character %s during restore. Skipping restore.", charName.c_str());
				++c_iter;
				continue;
			}

			Database_Result dbRes;
			if (exists.empty())
			{
				dbRes = this->db->Query("INSERT INTO `characters` (`name`, `title`, `account`, `home`, `fiance`, `partner`, `class`, `gender`, `race`, "
					"`hairstyle`, `haircolor`, `map`, `x`, `y`, `direction`, `level`, `admin`, `exp`, `hp`, `tp`, `str`, `int`, `wis`, `agi`, `con`, `cha`, `statpoints`, `skillpoints`, `karma`, `sitting`, `hidden`, "
					"`nointeract`, `bankmax`, `goldbank`, `usage`, `inventory`, `bank`, `paperdoll`, `spells`, `guild`, `guild_rank`, `guild_rank_string`, `quest`) "
					"VALUES ('$', '$', '$', '$', '$', '$', #, #, #, #, #, #, #, #, #, #, #, #, #, #, #, #, #, #, #, #, #, #, #, #, #, #, #, #, #, '$', '$', '$', '$', '$', #, '$', '$')",
					charName.c_str(), c["title"].get<std::string>().c_str(), c["account"].get<std::string>().c_str(),
					c["home"].get<std::string>().c_str(), c["fiance"].get<std::string>().c_str(), c["partner"].get<std::string>().c_str(),
					c["class"].get<int>(), c["gender"].get<int>(), c["race"].get<int>(),
					c["hairstyle"].get<int>(), c["haircolor"].get<int>(), c["map"].get<int>(), c["x"].get<int>(), c["y"].get<int>(), c["direction"].get<int>(),
					c["level"].get<int>(), c["admin"].get<int>(), c["exp"].get<int>(), c["hp"].get<int>(), c["tp"].get<int>(),
					c["str"].get<int>(), c["intl"].get<int>(), c["wis"].get<int>(), c["agi"].get<int>(), c["con"].get<int>(), c["cha"].get<int>(),
					c["statpoints"].get<int>(), c["skillpoints"].get<int>(), c["karma"].get<int>(), c["sitting"].get<int>(), c["hidden"].get<int>(),
					c["nointeract"].get<int>(), c["bankmax"].get<int>(), c["goldbank"].get<int>(), c["usage"].get<int>(),
					c["inventory"].get<std::string>().c_str(), c["bank"].get<std::string>().c_str(), c["paperdoll"].get<std::string>().c_str(),
					c["spells"].get<std::string>().c_str(), c["guild"].get<std::string>().c_str(),
					c["guildrank"].get<int>(), c["guildrank_str"].get<std::string>().c_str(), c["quest"].get<std::string>().c_str());
			}
			// if the database entry is older than the character data in the dump, update the database with the dump's character data
			else if (exists.front()["usage"].GetInt() <= c["usage"].get<int>())
			{
				dbRes = this->db->Query("UPDATE `characters` SET `title` = '$', `home` = '$', `fiance` = '$', `partner` = '$', `class` = #, `gender` = #, `race` = #, "
					"`hairstyle` = #, `haircolor` = #, `map` = #, `x` = #, `y` = #, `direction` = #, `level` = #, `admin` = #, `exp` = #, `hp` = #, `tp` = #, "
					"`str` = #, `int` = #, `wis` = #, `agi` = #, `con` = #, `cha` = #, `statpoints` = #, `skillpoints` = #, `karma` = #, `sitting` = #, `hidden` = #, "
					"`nointeract` = #, `bankmax` = #, `goldbank` = #, `usage` = #, `inventory` = '$', `bank` = '$', `paperdoll` = '$', "
					"`spells` = '$', `guild` = '$', `guild_rank` = #, `guild_rank_string` = '$', `quest` = '$' "
					"WHERE `name` = '$'",
					c["title"].get<std::string>().c_str(), c["home"].get<std::string>().c_str(), c["fiance"].get<std::string>().c_str(), c["partner"].get<std::string>().c_str(),
					c["class"].get<int>(), c["gender"].get<int>(), c["race"].get<int>(),
					c["hairstyle"].get<int>(), c["haircolor"].get<int>(), c["map"].get<int>(), c["x"].get<int>(), c["y"].get<int>(), c["direction"].get<int>(),
					c["level"].get<int>(), c["admin"].get<int>(), c["exp"].get<int>(), c["hp"].get<int>(), c["tp"].get<int>(),
					c["str"].get<int>(), c["intl"].get<int>(), c["wis"].get<int>(), c["agi"].get<int>(), c["con"].get<int>(), c["cha"].get<int>(),
					c["statpoints"].get<int>(), c["skillpoints"].get<int>(), c["karma"].get<int>(), c["sitting"].get<int>(), c["hidden"].get<int>(),
					c["nointeract"].get<int>(), c["bankmax"].get<int>(), c["goldbank"].get<int>(), c["usage"].get<int>(),
					c["inventory"].get<std::string>().c_str(), c["bank"].get<std::string>().c_str(), c["paperdoll"].get<std::string>().c_str(),
					c["spells"].get<std::string>().c_str(), c["guild"].get<std::string>().c_str(),
					c["guildrank"].get<int>(), c["guildrank_str"].get<std::string>().c_str(), c["quest"].get<std::string>().c_str(),
					charName.c_str());
			}

			if (!dbRes.Error())
			{
#ifdef DEBUG
				Console::Dbg("Restored character: %s", charName.c_str());
#endif
				c_iter = dump["characters"].erase(c_iter);
			}
			else
			{
				++c_iter;
			}
		}

		auto g_iter = dump["guilds"].cbegin();
		for (; g_iter != dump["guilds"].cend();)
		{
			auto g = *g_iter;
			auto guildTag = g["tag"].get<std::string>();
			auto guildName = g["name"].get<std::string>();

			auto exists = this->db->Query("SELECT COUNT(1) AS `count` FROM `guilds` WHERE `tag` = '$'", guildTag.c_str());
			if (exists.Error())
			{
				Console::Wrn("Error checking existence of guild %s during restore. Skipping restore.", guildTag.c_str());
				++g_iter;
				continue;
			}

			Database_Result dbRes;
			if (exists.empty() || exists.front()["count"].GetInt() != 1)
			{
				dbRes = this->db->Query("INSERT INTO `guilds` (`tag`, `name`, `description`, `ranks`, `bank`) VALUES ('$', '$', '$', '$', #)",
					guildTag.c_str(),
					guildName.c_str(),
					g["description"].get<std::string>().c_str(),
					g["ranks"].get<std::string>().c_str(),
					g["bank"].get<int>());
			}
			else
			{
				dbRes = this->db->Query("UPDATE `guilds` SET `description` = '$', `ranks` = '$', `bank` = # WHERE tag = '$'",
					g["description"].get<std::string>().c_str(),
					g["ranks"].get<std::string>().c_str(),
					g["bank"].get<int>(),
					guildTag.c_str());
			}

			if (!dbRes.Error())
			{
#ifdef DEBUG
				Console::Dbg("Restored guild:     %s (%s)", guildName.c_str(), guildTag.c_str());
#endif
				g_iter = dump["guilds"].erase(g_iter);
				// cache the guild that was just restored
				(void)this->guildmanager->GetGuild(guildTag);
			}
			else
			{
				++g_iter;
			}
		}

		if (in_tran)
			this->db->Commit();
	}
	catch (Database_Exception& dbe)
	{
		Console::Err("Database operation failed during restore. %s: %s", dbe.what(), dbe.error());
		this->db->Rollback();
	}

	UTIL_FOREACH_CREF(dump["mapState"]["items"], i)
	{
		auto map = std::find_if(this->maps.begin(), this->maps.end(), [&i] (Map* m) { return m->id == i["mapId"].get<int>(); });
		if (map == this->maps.end())
			continue;

		(*map)->items.push_back(
			std::make_shared<Map_Item>(
				i["uid"].get<short>(),
				i["itemId"].get<short>(),
				i["amount"].get<int>(),
				i["x"].get<unsigned char>(),
				i["y"].get<unsigned char>(),
				0, 0));

#ifdef DEBUG
		Console::Dbg("Restored item:     %dx%d", i["itemId"].get<int>(), i["amount"].get<int>());
#endif
	}

	UTIL_FOREACH_CREF(dump["mapState"]["chests"], chest)
	{
		auto map = std::find_if(this->maps.begin(), this->maps.end(), [&chest] (Map* m) { return m->id == chest["mapId"].get<int>(); });
		if (map == this->maps.end())
			continue;

		auto mapChest = std::find_if((*map)->chests.begin(), (*map)->chests.end(),
			[&chest] (std::shared_ptr<Map_Chest> check)
			{
				return check->x == chest["x"].get<int>() && check->y == chest["y"].get<int>();
			});
		if (mapChest == (*map)->chests.end())
			continue;

		(*mapChest)->AddItem(chest["itemId"].get<int>(), chest["amount"].get<int>(), chest["slot"].get<int>());

#ifdef DEBUG
		Console::Dbg("Restored chest:    %dx%d", chest["itemId"].get<int>(), chest["amount"].get<int>());
#endif
	}

	if (dump["characters"].empty() && dump["guilds"].empty())
	{
		std::remove(fileName.c_str());
	}
	else
	{
		std::ofstream rewrite(fileName);
		if (!rewrite.is_open())
		{
			throw std::runtime_error("Unable to update the world dump file after partially restoring world state");
		}

		rewrite << dump.dump(2) << std::endl;
		rewrite.close();
	}
}

void World::UpdateAdminCount(int admin_count)
{
	this->admin_count = admin_count;

	if (admin_count == 0 && this->config["FirstCharacterAdmin"])
	{
		Console::Out("There are no admin characters!");
		Console::Out("The next character created will be given HGM status!");
	}
}

void World::Command(std::string command, const std::vector<std::string>& arguments, Command_Source* from)
{
	std::unique_ptr<System_Command_Source> system_source;

	if (!from)
	{
		system_source.reset(new System_Command_Source(this));
		from = system_source.get();
	}

	Commands::Handle(util::lowercase(command), arguments, from);
}

void World::LoadHome()
{
	this->homes.clear();

	std::unordered_map<std::string, std::shared_ptr<Home>> temp_homes;

	UTIL_FOREACH(this->home_config, hc)
	{
		std::vector<std::string> parts = util::explode('.', hc.first);

		if (parts.size() < 2)
		{
			continue;
		}

		bool is_level = parts[0] == "level";
		bool is_race = parts[0] == "race";

		if (is_level || is_race)
		{
			int value = util::to_int(parts[1]);

			auto home_iter = temp_homes.find(hc.second);

			if (home_iter == temp_homes.end())
			{
				auto home = std::make_shared<Home>();
				home->id = static_cast<std::string>(hc.second);
				temp_homes[hc.second] = home;

				if (is_level)
				{
					home->level = value;
				}
				else if (is_race)
				{
					home->race = value;
				}
			}
			else if (is_level)
			{
				home_iter->second->level = value;
			}
			else if (is_race)
			{
				home_iter->second->race = value;
			}

			continue;
		}

		std::shared_ptr<Home> home;
		if (temp_homes.find(parts[0]) == temp_homes.end())
		{
			temp_homes[parts[0]] = home = std::make_shared<Home>();
			home->id = parts[0];
		}
		else
		{
			home = temp_homes[parts[0]];
		}

		if (parts[1] == "name")
		{
			home->name = home->name = static_cast<std::string>(hc.second);
		}
		else if (parts[1] == "location")
		{
			std::vector<std::string> locparts = util::explode(',', hc.second);
			home->map = locparts.size() >= 1 ? util::to_int(locparts[0]) : 1;
			home->x = locparts.size() >= 2 ? util::to_int(locparts[1]) : 0;
			home->y = locparts.size() >= 3 ? util::to_int(locparts[2]) : 0;
		}
	}

	UTIL_FOREACH(temp_homes, home)
	{
		this->homes.push_back(home.second);
	}
}

int World::GenerateCharacterID()
{
	return ++this->last_character_id;
}

unsigned short World::GenerateOperationID(std::function<unsigned short(const EOClient *)> get_id) const
{
	unsigned short candidate_id = static_cast<unsigned int>(util::rand(20000, 60000)) - 1;
	std::list<Client*>::const_iterator matching_client;

	do
	{
		candidate_id++;
		matching_client = std::find_if(this->server->clients.cbegin(),
			this->server->clients.cend(),
			[&candidate_id, get_id](const Client * c)
			{
				auto client = dynamic_cast<const EOClient*>(c);
				return get_id(client) == candidate_id;
			});
	} while (matching_client != this->server->clients.cend());

	return candidate_id;
}

int World::GenerateClientID()
{
	unsigned int lowest_free_id = 1;
	restart_loop:
	UTIL_FOREACH(this->server->clients, client)
	{
		EOClient *eoclient = static_cast<EOClient *>(client);

		if (eoclient->id == lowest_free_id)
		{
			lowest_free_id = eoclient->id + 1;
			goto restart_loop;
		}
	}
	return lowest_free_id;
}

void World::Login(Character *character)
{
	this->characters.push_back(character);

	if (this->GetMap(character->mapid)->relog_x || this->GetMap(character->mapid)->relog_y)
	{
		character->x = this->GetMap(character->mapid)->relog_x;
		character->y = this->GetMap(character->mapid)->relog_y;
	}

	Map* map = this->GetMap(character->mapid);

	if (character->sitting == SIT_CHAIR)
	{
		Map_Tile::TileSpec spec = map->GetSpec(character->x, character->y);

		if (spec == Map_Tile::ChairDown)
			character->direction = DIRECTION_DOWN;
		else if (spec == Map_Tile::ChairUp)
			character->direction = DIRECTION_UP;
		else if (spec == Map_Tile::ChairLeft)
			character->direction = DIRECTION_LEFT;
		else if (spec == Map_Tile::ChairRight)
			character->direction = DIRECTION_RIGHT;
		else if (spec == Map_Tile::ChairDownRight)
			character->direction = character->direction == DIRECTION_RIGHT ? DIRECTION_RIGHT : DIRECTION_DOWN;
		else if (spec == Map_Tile::ChairUpLeft)
			character->direction = character->direction == DIRECTION_LEFT ? DIRECTION_LEFT : DIRECTION_UP;
		else if (spec != Map_Tile::ChairAll)
			character->sitting = SIT_STAND;
	}

	map->Enter(character);
	character->Login();
}

void World::Logout(Character *character)
{
	if (this->GetMap(character->mapid)->exists)
	{
		this->GetMap(character->mapid)->Leave(character);
	}

	this->characters.erase(
		std::remove(UTIL_RANGE(this->characters), character),
		this->characters.end()
	);
}

void World::Msg(Command_Source *from, std::string message, bool echo)
{
	std::string from_str = from ? from->SourceName() : "server";

	message = util::text_cap(message, static_cast<int>(this->config["ChatMaxWidth"]) - util::text_width(util::ucfirst(from_str) + "  "));

	PacketBuilder builder(PACKET_TALK, PACKET_MSG, 2 + from_str.length() + message.length());
	builder.AddBreakString(from_str);
	builder.AddBreakString(message);

	UTIL_FOREACH(this->characters, character)
	{
		character->AddChatLog("~", from_str, message);

		if (!echo && character == from)
		{
			continue;
		}

		character->Send(builder);
	}
}

void World::AdminMsg(Command_Source *from, std::string message, int minlevel, bool echo)
{
	std::string from_str = from ? from->SourceName() : "server";

	message = util::text_cap(message, static_cast<int>(this->config["ChatMaxWidth"]) - util::text_width(util::ucfirst(from_str) + "  "));

	PacketBuilder builder(PACKET_TALK, PACKET_ADMIN, 2 + from_str.length() + message.length());
	builder.AddBreakString(from_str);
	builder.AddBreakString(message);

	UTIL_FOREACH(this->characters, character)
	{
		character->AddChatLog("+", from_str, message);

		if ((!echo && character == from) || character->SourceAccess() < minlevel)
		{
			continue;
		}

		character->Send(builder);
	}
}

void World::AnnounceMsg(Command_Source *from, std::string message, bool echo)
{
	std::string from_str = from ? from->SourceName() : "server";

	message = util::text_cap(message, static_cast<int>(this->config["ChatMaxWidth"]) - util::text_width(util::ucfirst(from_str) + "  "));

	PacketBuilder builder(PACKET_TALK, PACKET_ANNOUNCE, 2 + from_str.length() + message.length());
	builder.AddBreakString(from_str);
	builder.AddBreakString(message);

	UTIL_FOREACH(this->characters, character)
	{
		character->AddChatLog("@", from_str, message);

		if (!echo && character == from)
		{
			continue;
		}

		character->Send(builder);
	}
}

void World::ServerMsg(std::string message)
{
	message = util::text_cap(message, static_cast<int>(this->config["ChatMaxWidth"]) - util::text_width("Server  "));

	PacketBuilder builder(PACKET_TALK, PACKET_SERVER, message.length());
	builder.AddString(message);

	UTIL_FOREACH(this->characters, character)
	{
		character->Send(builder);
	}
}

void World::AdminReport(Character *from, std::string reportee, std::string message)
{
	message = util::text_cap(message, static_cast<int>(this->config["ChatMaxWidth"]) - util::text_width(util::ucfirst(from->SourceName()) + "  reports: " + reportee + ", "));

	PacketBuilder builder(PACKET_ADMININTERACT, PACKET_REPLY, 5 + from->SourceName().length() + message.length() + reportee.length());
	builder.AddChar(2); // message type
	builder.AddByte(255);
	builder.AddBreakString(from->SourceName());
	builder.AddBreakString(message);
	builder.AddBreakString(reportee);

	UTIL_FOREACH(this->characters, character)
	{
		if (character->SourceAccess() >= static_cast<int>(this->admin_config["reports"]))
		{
			character->Send(builder);
		}
	}

	short boardid = static_cast<int>(this->config["AdminBoard"]) - 1;

	if (static_cast<std::size_t>(boardid) < this->boards.size())
	{
		std::string chat_log_dump;
		Board *admin_board = this->boards[boardid];

		Board_Post *newpost = new Board_Post;
		newpost->id = ++admin_board->last_id;
		newpost->author = from->SourceName();
		newpost->author_admin = from->admin;
		newpost->subject = std::string(" [Report] ") + util::ucfirst(from->SourceName()) + " reports: " + reportee;
		newpost->body = message;
		newpost->time = Timer::GetTime();

		if (int(this->config["ReportChatLogSize"]) > 0)
		{
			chat_log_dump = from->GetChatLogDump();
			newpost->body += "\r\n\r\n";
			newpost->body += chat_log_dump;
		}

		if (this->config["LogReports"])
		{
			try
			{
				this->db->Query("INSERT INTO `reports` (`reporter`, `reported`, `reason`, `time`, `chat_log`) VALUES ('$', '$', '$', #, '$')",
					from->SourceName().c_str(),
					reportee.c_str(),
					message.c_str(),
					int(std::time(0)),
					chat_log_dump.c_str()
				);
			}
			catch (Database_Exception& e)
			{
				Console::Err("Could not save report to database.");
				Console::Err("%s", e.error());
			}
		}

		admin_board->posts.push_front(newpost);

		if (admin_board->posts.size() > static_cast<std::size_t>(static_cast<int>(this->config["AdminBoardLimit"])))
		{
			admin_board->posts.pop_back();
		}
	}
}

void World::AdminRequest(Character *from, std::string message)
{
	message = util::text_cap(message, static_cast<int>(this->config["ChatMaxWidth"]) - util::text_width(util::ucfirst(from->SourceName()) + "  needs help: "));

	PacketBuilder builder(PACKET_ADMININTERACT, PACKET_REPLY, 4 + from->SourceName().length() + message.length());
	builder.AddChar(1); // message type
	builder.AddByte(255);
	builder.AddBreakString(from->SourceName());
	builder.AddBreakString(message);

	UTIL_FOREACH(this->characters, character)
	{
		if (character->SourceAccess() >= static_cast<int>(this->admin_config["reports"]))
		{
			character->Send(builder);
		}
	}

	short boardid = static_cast<int>(this->server->world->config["AdminBoard"]) - 1;

	if (static_cast<std::size_t>(boardid) < this->server->world->boards.size())
	{
		Board *admin_board = this->server->world->boards[boardid];

		Board_Post *newpost = new Board_Post;
		newpost->id = ++admin_board->last_id;
		newpost->author = from->SourceName();
		newpost->author_admin = from->admin;
		newpost->subject = std::string(" [Request] ") + util::ucfirst(from->SourceName()) + " needs help";
		newpost->body = message;
		newpost->time = Timer::GetTime();

		admin_board->posts.push_front(newpost);

		if (admin_board->posts.size() > static_cast<std::size_t>(static_cast<int>(this->server->world->config["AdminBoardLimit"])))
		{
			admin_board->posts.pop_back();
		}
	}
}

void World::Rehash()
{
	try
	{
		this->config.Read("config.ini");
		this->admin_config.Read("admin.ini");
		this->drops_config.Read(this->config["DropsFile"]);
		this->shops_config.Read(this->config["ShopsFile"]);
		this->arenas_config.Read(this->config["ArenasFile"]);
		this->formulas_config.Read(this->config["FormulasFile"]);
		this->home_config.Read(this->config["HomeFile"]);
		this->skills_config.Read(this->config["SkillsFile"]);
		this->speech_config.Read(this->config["SpeechFile"]);
	}
	catch (std::runtime_error &e)
	{
		Console::Err(e.what());
	}

	this->UpdateConfig();
	this->LoadHome();

	UTIL_FOREACH(this->maps, map)
	{
		map->LoadArena();
	}

	UTIL_FOREACH_CREF(this->npc_data, npc)
	{
		if (npc->id != 0)
			npc->Load();
	}
}

void World::ReloadPub(bool quiet)
{
	auto eif_id = this->eif->rid;
	auto enf_id = this->enf->rid;
	auto esf_id = this->esf->rid;
	auto ecf_id = this->ecf->rid;

	this->eif->Read(this->config["EIF"]);
	this->enf->Read(this->config["ENF"]);
	this->esf->Read(this->config["ESF"]);
	this->ecf->Read(this->config["ECF"]);

	if (eif_id != this->eif->rid || enf_id != this->enf->rid
	 || esf_id != this->esf->rid || ecf_id != this->ecf->rid)
	{
		if (!quiet)
		{
			UTIL_FOREACH(this->characters, character)
			{
				character->ServerMsg("The server has been reloaded, please log out and in again.");
			}
		}
	}

	std::size_t current_npcs = this->npc_data.size();
	std::size_t new_npcs = this->enf->data.size();

	this->npc_data.resize(new_npcs);

	for (std::size_t i = current_npcs; i < new_npcs; ++i)
	{
		npc_data[i]->Load();
	}
}

void World::ReloadQuests()
{
	// Back up character quest states
	UTIL_FOREACH(this->characters, c)
	{
		UTIL_FOREACH(c->quests, q)
		{
			if (!q.second)
				continue;

			short quest_id = q.first;
			std::string quest_name = q.second->StateName();
			std::string progress = q.second->SerializeProgress();

			c->quests_inactive.insert({quest_id, quest_name, progress});
		}
	}

	// Clear character quest states
	UTIL_FOREACH(this->characters, c)
	{
		c->quests.clear();
	}

	// Reload all quests
	short max_quest = static_cast<int>(this->config["Quests"]);

	UTIL_FOREACH(this->enf->data, npc)
	{
		if (npc.type == ENF::Quest)
			max_quest = std::max(max_quest, npc.vendor_id);
	}

	for (short i = 0; i <= max_quest; ++i)
	{
		try
		{
			std::shared_ptr<Quest> q = std::make_shared<Quest>(i, this);
			this->quests[i] = std::move(q);
		}
		catch (...)
		{
			this->quests.erase(i);
		}
	}

	// Reload quests that might still be loaded above the highest quest npc ID
	UTIL_IFOREACH(this->quests, it)
	{
		if (it->first > max_quest)
		{
			try
			{
				std::shared_ptr<Quest> q = std::make_shared<Quest>(it->first, this);
				std::swap(it->second, q);
			}
			catch (...)
			{
				it = this->quests.erase(it);
			}
		}
	}

	// Restore character quest states
	UTIL_FOREACH(this->characters, c)
	{
		c->quests.clear();

		UTIL_FOREACH(c->quests_inactive, state)
		{
			auto quest_it = this->quests.find(state.quest_id);

			if (quest_it == this->quests.end())
			{
				Console::Wrn("Quest not found: %i. Marking as inactive.", state.quest_id);
				continue;
			}

			// WARNING: holds a non-tracked reference to shared_ptr
			Quest* quest = quest_it->second.get();
			auto quest_context(std::make_shared<Quest_Context>(c, quest));

			try
			{
				quest_context->SetState(state.quest_state, false);
				quest_context->UnserializeProgress(UTIL_CRANGE(state.quest_progress));
			}
			catch (EOPlus::Runtime_Error& ex)
			{
				Console::Wrn(ex.what());
				Console::Wrn("Could not resume quest: %i. Marking as inactive.", state.quest_id);

				if (!c->quests_inactive.insert(std::move(state)).second)
					Console::Wrn("Duplicate inactive quest record dropped for quest: %i", state.quest_id);

				continue;
			}

			auto result = c->quests.insert(std::make_pair(state.quest_id, std::move(quest_context)));

			if (!result.second)
			{
				Console::Wrn("Duplicate quest record dropped for quest: %i", state.quest_id);
				continue;
			}
		}
	}

	// Check new quest rules
	UTIL_FOREACH(this->characters, c)
	{
		// TODO: If a character is removed by a quest rule...
		c->CheckQuestRules();
	}

	Console::Out("%i/%i quests loaded.", this->quests.size(), max_quest);
}

Character *World::GetCharacter(std::string name)
{
	name = util::lowercase(name);

	UTIL_FOREACH(this->characters, character)
	{
		if (character->SourceName() == name)
		{
			return character;
		}
	}

	return 0;
}

Character *World::GetCharacterReal(std::string real_name)
{
	real_name = util::lowercase(real_name);

	UTIL_FOREACH(this->characters, character)
	{
		if (character->real_name == real_name)
		{
			return character;
		}
	}

	return 0;
}

Character *World::GetCharacterPID(unsigned int id)
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

Character *World::GetCharacterCID(unsigned int id)
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

Map *World::GetMap(short id)
{
	try
	{
		return this->maps.at(id - 1);
	}
	catch (...)
	{
		try
		{
			return this->maps.at(0);
		}
		catch (...)
		{
			throw std::runtime_error("Map #" + util::to_string(id) + " and fallback map #1 are unavailable");
		}
	}
}

const NPC_Data* World::GetNpcData(short id) const
{
	if (id >= 0 && static_cast<unsigned short>(id) < npc_data.size())
		return npc_data[id].get();
	else
		return npc_data[0].get();
}

std::shared_ptr<Home> World::GetHome(const Character *character) const
{
	auto home = std::shared_ptr<Home>(nullptr);
	static std::shared_ptr<Home> null_home = std::make_shared<Home>();

	UTIL_FOREACH(this->homes, h)
	{
		if (h->id == character->home)
		{
			return h;
		}
	}

	int current_home_level = -2;
	UTIL_FOREACH(this->homes, h)
	{
		if (h->level <= character->level &&
			h->level > current_home_level &&
			(h->race < 0 || h->race == character->race))
		{
			home = h;
			current_home_level = h->level;
		}
	}

	if (!home)
	{
		home = null_home;
	}

	return home;
}

std::shared_ptr<Home> World::GetHome(std::string id)
{
	UTIL_FOREACH(this->homes, h)
	{
		if (h->id == id)
		{
			return h;
		}
	}

	return 0;
}

bool World::CharacterExists(std::string name)
{
	Database_Result res = this->db->Query("SELECT 1 FROM `characters` WHERE `name` = '$'", name.c_str());
	return !res.empty();
}

Character *World::CreateCharacter(Player *player, std::string name, Gender gender, int hairstyle, int haircolor, Skin race)
{
	char buffer[1024];
	std::string startmapinfo;
	std::string startmapval;

	if (static_cast<int>(this->config["StartMap"]))
	{
		startmapinfo = ", `map`, `x`, `y`";
		std::snprintf(buffer, 1024, ",%i,%i,%i", static_cast<int>(this->config["StartMap"]), static_cast<int>(this->config["StartX"]), static_cast<int>(this->config["StartY"]));
		startmapval = buffer;
	}

	std::shared_ptr<Home> home;
	UTIL_FOREACH(this->homes, h)
	{
		if (h->race == race && h->level <= 0)
		{
			home = h;
		}
	}

	if (home)
	{
		startmapinfo = ", `map`, `x`, `y`";
		std::snprintf(buffer, 1024, ",%i,%i,%i", home->map, home->x, home->y);
		startmapval = buffer;
	}

	this->db->Query("INSERT INTO `characters` (`name`, `account`, `gender`, `hairstyle`, `haircolor`, `race`, `inventory`, `bank`, `paperdoll`, `spells`, `quest`, `vars`@) VALUES ('$','$',#,#,#,#,'$','','$','$','',''@)",
		startmapinfo.c_str(), name.c_str(), player->username.c_str(), gender, hairstyle, haircolor, race,
		static_cast<std::string>(this->config["StartItems"]).c_str(), static_cast<std::string>(gender?this->config["StartEquipMale"]:this->config["StartEquipFemale"]).c_str(),
		static_cast<std::string>(this->config["StartSpells"]).c_str(), startmapval.c_str());

	return new Character(name, this);
}

void World::DeleteCharacter(std::string name)
{
	this->db->Query("DELETE FROM `characters` WHERE name = '$'", name.c_str());
}

Player *World::PlayerFactory(std::string username)
{
	auto database = this->databaseFactory->CreateDatabase(this->config);
	return new Player(username, this, database.get());
}

AsyncOperation<AccountCredentials, LoginReply>* World::CheckCredential(EOClient* client)
{
	if (this->loginManager->LoginBusy())
	{
		return AsyncOperation<AccountCredentials, LoginReply>::FromResult(LOGIN_BUSY, client, LOGIN_OK);
	}

	return this->loginManager->CheckLoginAsync(client);
}

AsyncOperation<PasswordChangeInfo, bool>* World::ChangePassword(EOClient* client)
{
	return this->loginManager->SetPasswordAsync(client);
}

AsyncOperation<AccountCreateInfo, bool>* World::CreateAccount(EOClient * client)
{
	return this->loginManager->CreateAccountAsync(client);
}

bool World::PlayerExists(std::string username)
{
	Database_Result res = this->db->Query("SELECT 1 FROM `accounts` WHERE `username` = '$'", username.c_str());
	return !res.empty();
}

bool World::PlayerOnline(std::string username)
{
	if (!Player::ValidName(username))
	{
		return false;
	}

	UTIL_FOREACH(this->server->clients, client)
	{
		EOClient *eoclient = static_cast<EOClient *>(client);

		if (eoclient->player)
		{
			if (eoclient->player->username.compare(username) == 0)
			{
				return true;
			}
		}
	}

	return false;
}

void World::Kick(Command_Source *from, Character *victim, bool announce)
{
	if (announce)
		this->ServerMsg(i18n.Format("announce_removed", victim->SourceName(), from ? from->SourceName() : "server", i18n.Format("kicked")));

	victim->player->client->Close();
}

void World::Jail(Command_Source *from, Character *victim, bool announce)
{
	if (announce)
		this->ServerMsg(i18n.Format("announce_removed", victim->SourceName(), from ? from->SourceName() : "server", i18n.Format("jailed")));

	bool bubbles = this->config["WarpBubbles"] && !victim->IsHideWarp();

	Character* charfrom = dynamic_cast<Character*>(from);

	if (charfrom && charfrom->IsHideWarp())
		bubbles = false;

	victim->Warp(static_cast<int>(this->config["JailMap"]), static_cast<int>(this->config["JailX"]), static_cast<int>(this->config["JailY"]), bubbles ? WARP_ANIMATION_ADMIN : WARP_ANIMATION_NONE);
}

void World::Unjail(Command_Source *from, Character *victim)
{
	bool bubbles = this->config["WarpBubbles"] && !victim->IsHideWarp();

	Character* charfrom = dynamic_cast<Character*>(from);

	if (charfrom && charfrom->IsHideWarp())
		bubbles = false;

	if (victim->mapid != static_cast<int>(this->config["JailMap"]))
		return;

	victim->Warp(static_cast<int>(this->config["JailMap"]), static_cast<int>(this->config["UnJailX"]), static_cast<int>(this->config["UnJailY"]), bubbles ? WARP_ANIMATION_ADMIN : WARP_ANIMATION_NONE);
}

void World::Ban(Command_Source *from, Character *victim, int duration, bool announce)
{
	std::string from_str = from ? from->SourceName() : "server";

	if (announce)
		this->ServerMsg(i18n.Format("announce_removed", victim->SourceName(), from_str, i18n.Format("banned")));

	std::string query("INSERT INTO bans (username, ip, hdid, expires, setter) VALUES ");

	query += "('" + db->Escape(victim->player->username) + "', ";
	query += util::to_string(static_cast<int>(victim->player->client->GetRemoteAddr())) + ", ";
	query += util::to_string(victim->player->client->hdid) + ", ";

	if (duration == -1)
	{
		query += "0";
	}
	else
	{
		query += util::to_string(int(std::time(0) + duration));
	}

	query += ", '" + db->Escape(from_str) + "')";

	try
	{
		this->db->Query(query.c_str());
	}
	catch (Database_Exception& e)
	{
		Console::Err("Could not save ban to database.");
		Console::Err("%s", e.error());
	}

	victim->player->client->Close();
}

void World::Mute(Command_Source *from, Character *victim, bool announce)
{
	if (announce && !this->config["SilentMute"])
		this->ServerMsg(i18n.Format("announce_muted", victim->SourceName(), from ? from->SourceName() : "server", i18n.Format("banned")));

	victim->Mute(from);
}

int World::CheckBan(const std::string *username, const IPAddress *address, const int *hdid)
{
	std::string query("SELECT COALESCE(MAX(expires),-1) AS expires FROM bans WHERE (");

	if (!username && !address && !hdid)
	{
		return -1;
	}

	if (username)
	{
		query += "username = '";
		query += db->Escape(*username);
		query += "' OR ";
	}

	if (address)
	{
		query += "ip = ";
		query += util::to_string(static_cast<int>(*const_cast<IPAddress *>(address)));
		query += " OR ";
	}

	if (hdid)
	{
		query += "hdid = ";
		query += util::to_string(*hdid);
		query += " OR ";
	}

	Database_Result res = db->Query((query.substr(0, query.length()-4) + ") AND (expires > # OR expires = 0)").c_str(), int(std::time(0)));

	return static_cast<int>(res[0]["expires"]);
}

static std::list<int> PKExceptUnserialize(std::string serialized)
{
	std::list<int> list;
	std::size_t p = 0;
	std::size_t lastp = std::numeric_limits<std::size_t>::max();

	if (!serialized.empty() && *(serialized.end()-1) != ',')
	{
		serialized.push_back(',');
	}

	while ((p = serialized.find_first_of(',', p+1)) != std::string::npos)
	{
		list.push_back(util::to_int(serialized.substr(lastp+1, p-lastp-1)));
		lastp = p;
	}

	return list;
}

bool World::PKExcept(const Map *map)
{
	return this->PKExcept(map->id);
}

bool World::PKExcept(int mapid)
{
	if (mapid == static_cast<int>(this->config["JailMap"]))
	{
		return true;
	}

	if (this->GetMap(mapid)->arena)
	{
		return true;
	}

	std::list<int> except_list = PKExceptUnserialize(this->config["PKExcept"]);

	return std::find(except_list.begin(), except_list.end(), mapid) != except_list.end();
}

bool World::IsInstrument(int graphic_id)
{
	return std::find(UTIL_RANGE(this->instrument_ids), graphic_id) != this->instrument_ids.end();
}

World::~World()
{
	UTIL_FOREACH(this->maps, map)
	{
		delete map;
	}

	UTIL_FOREACH(this->boards, board)
	{
		delete board;
	}

	delete this->eif;
	delete this->enf;
	delete this->esf;
	delete this->ecf;

	delete this->guildmanager;
}
