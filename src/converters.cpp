/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "converters.hpp"

#include "character.hpp"
#include "config.hpp"
#include "eodata.hpp"
#include "map.hpp"
#include "npc.hpp"
#include "world.hpp"

#include "util.hpp"

#include <algorithm>
#include <utility>

namespace Converters
{

server::BigCoords BigCoordsFrom(const eolib::protocol::Coords& coords)
{
	server::BigCoords big_coords;
	big_coords.x = coords.x;
	big_coords.y = coords.y;
	return big_coords;
}

server::CharacterSelectionListEntry CharacterSelectionListEntryFrom(Character* character)
{
	const Character::PaperdollGraphics graphics = character->GetPaperdollGraphics();

	server::CharacterSelectionListEntry entry;
	entry.name = character->SourceName();
	entry.id = character->id;
	entry.level = character->level;
	entry.gender = static_cast<eolib::protocol::Gender>(character->gender);
	entry.hair_style = character->hairstyle;
	entry.hair_color = character->haircolor;
	entry.skin = character->race;
	entry.admin = static_cast<eolib::protocol::AdminLevel>(character->admin);
	entry.equipment.boots = graphics.boots;
	entry.equipment.armor = graphics.armor;
	entry.equipment.hat = graphics.hat;
	entry.equipment.shield = graphics.shield;
	entry.equipment.weapon = graphics.weapon;
	return entry;
}

server::CharacterMapInfo CharacterMapInfoFrom(Character* character)
{
	const Character::PaperdollGraphics graphics = character->GetPaperdollGraphics();

	server::CharacterMapInfo info;
	info.name = character->SourceName();
	info.player_id = character->PlayerID();
	info.map_id = character->mapid;
	info.coords = BigCoordsFrom(character->loc);
	info.direction = character->direction;
	info.class_id = character->clas;
	info.guild_tag = character->PaddedGuildTag();
	info.level = character->level;
	info.gender = static_cast<eolib::protocol::Gender>(character->gender);
	info.hair_style = character->hairstyle;
	info.hair_color = character->haircolor;
	info.skin = character->race;
	info.max_hp = character->maxhp;
	info.hp = character->hp;
	info.max_tp = character->maxtp;
	info.tp = character->tp;
	info.equipment.boots = graphics.boots;
	info.equipment.armor = graphics.armor;
	info.equipment.hat = graphics.hat;
	info.equipment.shield = graphics.shield;
	info.equipment.weapon = graphics.weapon;
	info.sit_state = character->sitting;
	info.invisible = character->IsHideInvisible();
	return info;
}

server::NpcMapInfo NpcMapInfoFrom(const NPC* npc)
{
	server::NpcMapInfo info;
	info.index = npc->index;
	info.id = npc->id;
	info.coords = npc->loc;
	info.direction = npc->direction;
	return info;
}

server::ItemMapInfo ItemMapInfoFrom(const Map_Item& item)
{
	server::ItemMapInfo info;
	info.uid = item.uid;
	info.id = item.id;
	info.coords = item.loc;
	info.amount = item.amount;
	return info;
}

server::NearbyInfo NearbyInfoFrom(Character* viewer)
{
	server::NearbyInfo nearby;

	UTIL_FOREACH(viewer->map->characters, character)
	{
		if (viewer->InRange(character))
		{
			nearby.characters.push_back(CharacterMapInfoFrom(character));
		}
	}

	UTIL_FOREACH(viewer->map->npcs, npc)
	{
		if (npc->alive && viewer->InRange(npc))
		{
			nearby.npcs.push_back(NpcMapInfoFrom(npc));
		}
	}

	UTIL_FOREACH(viewer->map->items, item)
	{
		if (viewer->InRange(*item))
		{
			nearby.items.push_back(ItemMapInfoFrom(*item));
		}
	}

	return nearby;
}

server::AvatarChange EquipmentChangeFrom(Character* character)
{
	const Character::PaperdollGraphics graphics = character->GetPaperdollGraphics();

	server::AvatarChange::ChangeTypeDataEquipment data;
	data.equipment.boots = graphics.boots;
	data.equipment.armor = graphics.armor;
	data.equipment.hat = graphics.hat;
	data.equipment.weapon = graphics.weapon;
	data.equipment.shield = graphics.shield;

	server::AvatarChange change = server::AvatarChange::ForEquipment(std::move(data));
	change.player_id = character->PlayerID();
	return change;
}

// The admin level the client is told the character has. Some lower levels are raised to unlock client features that
// the character's commands need.
static AdminLevel ClientAdminLevel(Character* character)
{
	World* world = character->world;

	AdminLevel lowest_command = ADMIN_HGM;

	UTIL_FOREACH(world->admin_config, ac)
	{
		if (ac.first == "killnpc" || ac.first == "reports")
		{
			continue;
		}

		lowest_command = std::min<AdminLevel>(lowest_command, static_cast<AdminLevel>(util::to_int(ac.second)));
	}

	if (character->SourceAccess() >= static_cast<int>(world->admin_config["seehide"])
	 && character->SourceAccess() < ADMIN_HGM)
	{
		return ADMIN_HGM;
	}

	if (character->SourceDutyAccess() >= static_cast<int>(world->admin_config["nowall"])
	 && character->SourceDutyAccess() < ADMIN_GM)
	{
		return ADMIN_GM;
	}

	if (character->SourceAccess() >= lowest_command && character->SourceAccess() < ADMIN_GUIDE)
	{
		return ADMIN_GUIDE;
	}

	return character->SourceAccess();
}

server::WelcomeReplyServerPacket::WelcomeCodeDataSelectCharacter SelectCharacterFrom(Character* character, bool login_warning)
{
	World* world = character->world;
	const Map* map = world->GetMap(character->mapid);

	server::WelcomeReplyServerPacket::WelcomeCodeDataSelectCharacter data;
	data.session_id = character->PlayerID();
	data.character_id = character->id;
	data.map_id = character->mapid;
	const auto map_rid = map->ClientRid();
	data.map_rid = {map_rid[0], map_rid[1]};
	data.map_file_size = map->filesize;
	data.eif_rid = world->eif->Rid();
	data.eif_length = world->eif->Length();
	data.enf_rid = world->enf->Rid();
	data.enf_length = world->enf->Length();
	data.esf_rid = world->esf->Rid();
	data.esf_length = world->esf->Length();
	data.ecf_rid = world->ecf->Rid();
	data.ecf_length = world->ecf->Length();
	data.name = character->SourceName();
	data.title = character->title;
	data.guild_name = character->GuildNameString();
	data.guild_rank_name = character->GuildRankString();
	data.class_id = character->clas;
	data.guild_tag = character->PaddedGuildTag();
	data.admin = static_cast<eolib::protocol::AdminLevel>(ClientAdminLevel(character));
	data.level = character->level;
	data.experience = character->exp;
	data.usage = character->usage;

	data.stats.hp = character->hp;
	data.stats.max_hp = character->maxhp;
	data.stats.tp = character->tp;
	data.stats.max_tp = character->maxtp;
	data.stats.max_sp = character->maxsp;
	data.stats.stat_points = character->statpoints;
	data.stats.skill_points = character->skillpoints;
	data.stats.karma = character->karma;
	data.stats.secondary.min_damage = character->mindam;
	data.stats.secondary.max_damage = character->maxdam;
	data.stats.secondary.accuracy = character->accuracy;
	data.stats.secondary.evade = character->evade;
	data.stats.secondary.armor = character->armor;
	data.stats.base.str = character->display_str;
	data.stats.base.wis = character->display_wis;
	data.stats.base.intl = character->display_intl;
	data.stats.base.agi = character->display_agi;
	data.stats.base.con = character->display_con;
	data.stats.base.cha = character->display_cha;

	// Filled by wire position. eo-protocol names positions 1-4 gloves, accessory, armor and belt, where etheos (and
	// EndlessClient) have Accessory, Gloves, Belt and Armor.
	data.equipment.boots = character->paperdoll[0];
	data.equipment.gloves = character->paperdoll[1];
	data.equipment.accessory = character->paperdoll[2];
	data.equipment.armor = character->paperdoll[3];
	data.equipment.belt = character->paperdoll[4];
	data.equipment.necklace = character->paperdoll[5];
	data.equipment.hat = character->paperdoll[6];
	data.equipment.shield = character->paperdoll[7];
	data.equipment.weapon = character->paperdoll[8];
	data.equipment.ring = {character->paperdoll[9], character->paperdoll[10]};
	data.equipment.armlet = {character->paperdoll[11], character->paperdoll[12]};
	data.equipment.bracer = {character->paperdoll[13], character->paperdoll[14]};

	const int leader_rank = std::max({
		static_cast<int>(world->config["GuildEditRank"]),
		static_cast<int>(world->config["GuildKickRank"]),
		static_cast<int>(world->config["GuildPromoteRank"]),
		static_cast<int>(world->config["GuildDemoteRank"])
	});

	// A guild rank of 1 allows client access to the guild management tools
	data.guild_rank = (character->guild_rank <= leader_rank && character->guild) ? 1 : character->guild_rank;

	data.settings.jail_map = static_cast<int>(world->config["JailMap"]);
	data.settings.rescue_map = 4;
	data.settings.rescue_coords.x = 24;
	data.settings.rescue_coords.y = 24;
	data.settings.spy_and_light_guide_flood_rate = 0;
	data.settings.guardian_flood_rate = 0;
	data.settings.game_master_flood_rate = 0;
	data.settings.high_game_master_flood_rate = 2;
	data.login_message_code = login_warning ? server::LoginMessageCode::Yes : server::LoginMessageCode::No;
	return data;
}

server::WelcomeReplyServerPacket::WelcomeCodeDataEnterGame EnterGameFrom(Character* character, std::vector<std::string> news, server::NearbyInfo nearby)
{
	server::WelcomeReplyServerPacket::WelcomeCodeDataEnterGame data;
	data.news = std::move(news);
	data.news.resize(NewsLineCount);
	data.weight.current = character->weight;
	data.weight.max = character->maxweight;

	data.items.reserve(character->inventory.size());
	UTIL_FOREACH(character->inventory, item)
	{
		eolib::protocol::net::Item& net_item = data.items.emplace_back();
		net_item.id = item.id;
		net_item.amount = item.amount;
	}

	data.spells.reserve(character->spells.size());
	UTIL_FOREACH(character->spells, spell)
	{
		eolib::protocol::net::Spell& net_spell = data.spells.emplace_back();
		net_spell.id = spell.id;
		net_spell.level = spell.level;
	}

	data.nearby = std::move(nearby);
	return data;
}

}
