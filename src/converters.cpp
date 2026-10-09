/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "converters.hpp"

#include "character.hpp"
#include "config.hpp"
#include "dialog.hpp"
#include "eodata.hpp"
#include "map.hpp"
#include "npc.hpp"
#include "quest.hpp"
#include "world.hpp"

#include "util.hpp"

#include <algorithm>
#include <utility>

namespace Converters
{

static bool ShouldHaveAdminIcon(const Character* character, protocol::AdminLevel minLevel)
{
	return character->SourceDutyAccess() >= minLevel && !character->IsHideAdmin();
}

protocol::Coords CoordsFrom(int x, int y)
{
	protocol::Coords coords;
	coords.x = x;
	coords.y = y;
	return coords;
}

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
	entry.gender = character->gender;
	entry.hair_style = character->hairstyle;
	entry.hair_color = character->haircolor;
	entry.skin = character->race;
	entry.admin = character->admin;
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
	info.gender = character->gender;
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

server::CharacterBaseStats CharacterBaseStatsFrom(const Character* character)
{
	server::CharacterBaseStats stats;
	stats.str = character->display_str;
	stats.intl = character->display_intl;
	stats.wis = character->display_wis;
	stats.agi = character->display_agi;
	stats.con = character->display_con;
	stats.cha = character->display_cha;
	return stats;
}

server::CharacterSecondaryStats CharacterSecondaryStatsFrom(const Character* character)
{
	server::CharacterSecondaryStats stats;
	stats.min_damage = character->mindam;
	stats.max_damage = character->maxdam;
	stats.accuracy = character->accuracy;
	stats.evade = character->evade;
	stats.armor = character->armor;
	return stats;
}

server::CharacterStatsUpdate CharacterStatsUpdateFrom(const Character* character)
{
	server::CharacterStatsUpdate stats;
	stats.base_stats = CharacterBaseStatsFrom(character);
	stats.max_hp = character->maxhp;
	stats.max_tp = character->maxtp;
	stats.max_sp = character->maxsp;
	stats.max_weight = character->maxweight;
	stats.secondary_stats = CharacterSecondaryStatsFrom(character);
	return stats;
}

server::CharacterStatsReset CharacterStatsResetFrom(const Character* character)
{
	server::CharacterStatsReset stats;
	stats.stat_points = character->statpoints;
	stats.skill_points = character->skillpoints;
	stats.hp = character->hp;
	stats.max_hp = character->maxhp;
	stats.tp = character->tp;
	stats.max_tp = character->maxtp;
	stats.max_sp = character->maxsp;
	stats.base = CharacterBaseStatsFrom(character);
	stats.secondary = CharacterSecondaryStatsFrom(character);
	return stats;
}

server::CharacterStatsEquipmentChange CharacterStatsEquipmentChangeFrom(const Character* character)
{
	server::CharacterStatsEquipmentChange stats;
	stats.max_hp = character->maxhp;
	stats.max_tp = character->maxtp;
	stats.base_stats = CharacterBaseStatsFrom(character);
	stats.secondary_stats = CharacterSecondaryStatsFrom(character);
	return stats;
}

server::CharacterStatsInfoLookup CharacterStatsInfoLookupFrom(const Character* character)
{
	server::CharacterStatsInfoLookup stats;
	stats.hp = character->hp;
	stats.max_hp = character->maxhp;
	stats.tp = character->tp;
	stats.max_tp = character->maxtp;
	stats.base_stats = CharacterBaseStatsFrom(character);
	stats.secondary_stats.min_damage = character->mindam;
	stats.secondary_stats.max_damage = character->maxdam;
	stats.secondary_stats.accuracy = character->accuracy;
	stats.secondary_stats.evade = character->evade;
	stats.secondary_stats.armor = character->armor;
	return stats;
}

server::ItemReplyServerPacket::ItemTypeDataExpReward ItemExpRewardFrom(const Character* character, bool level_up)
{
	server::ItemReplyServerPacket::ItemTypeDataExpReward exp_reward;
	exp_reward.experience = character->exp;
	exp_reward.level_up = level_up ? character->level : 0;
	exp_reward.stat_points = character->statpoints;
	exp_reward.skill_points = character->skillpoints;
	exp_reward.max_hp = character->maxhp;
	exp_reward.max_tp = character->maxtp;
	exp_reward.max_sp = character->maxsp;
	return exp_reward;
}

server::CharacterDetails CharacterDetailsFrom(Character* character)
{
	server::CharacterDetails details;
	details.name = character->SourceName();
	details.home = character->HomeString();
	details.partner = character->partner;
	details.title = character->title;
	details.guild = character->GuildNameString();
	details.guild_rank = character->GuildRankString();
	details.player_id = character->PlayerID();
	details.class_id = character->clas;
	details.gender = character->gender;
	details.admin = ShouldHaveAdminIcon(character, protocol::AdminLevel::Spy) ? character->SourceDutyAccess() : protocol::AdminLevel::Player;
	return details;
}

server::EquipmentPaperdoll EquipmentPaperdollFrom(const Character* character)
{
	const auto& paperdoll = character->paperdoll;

	server::EquipmentPaperdoll equipment;
	equipment.boots = paperdoll[Character::Boots];
	equipment.accessory = paperdoll[Character::Accessory];
	equipment.gloves = paperdoll[Character::Gloves];
	equipment.belt = paperdoll[Character::Belt];
	equipment.armor = paperdoll[Character::Armor];
	equipment.necklace = paperdoll[Character::Necklace];
	equipment.hat = paperdoll[Character::Hat];
	equipment.shield = paperdoll[Character::Shield];
	equipment.weapon = paperdoll[Character::Weapon];
	equipment.ring = {paperdoll[Character::Ring1], paperdoll[Character::Ring2]};
	equipment.armlet = {paperdoll[Character::Armlet1], paperdoll[Character::Armlet2]};
	equipment.bracer = {paperdoll[Character::Bracer1], paperdoll[Character::Bracer2]};
	return equipment;
}

server::CharacterIcon CharacterIconFrom(const Character* character)
{
	const bool party = character->party != nullptr;

	if (ShouldHaveAdminIcon(character, protocol::AdminLevel::GameMaster))
		return party ? server::CharacterIcon::HgmParty : server::CharacterIcon::Hgm;

	if (ShouldHaveAdminIcon(character, protocol::AdminLevel::Spy))
		return party ? server::CharacterIcon::GmParty : server::CharacterIcon::Gm;

	return party ? server::CharacterIcon::Party : server::CharacterIcon::Player;
}

std::vector<std::string> FinishedQuestNamesFrom(const Character* character)
{
	std::vector<std::string> names;

	for (const auto& quest : character->quests)
	{
		if (quest.second && quest.second->Finished() && !quest.second->IsHidden())
			names.push_back(quest.second->GetQuest()->Name());
	}

	return names;
}

std::vector<server::QuestProgressEntry> QuestProgressEntriesFrom(const Character* character)
{
	std::vector<server::QuestProgressEntry> entries;

	for (const auto& quest : character->quests)
	{
		if (!quest.second || quest.second->Finished() || quest.second->GetQuest()->Disabled() || quest.second->IsHidden())
			continue;

		const Quest_Context::ProgressInfo progress = quest.second->Progress();

		server::QuestProgressEntry& entry = entries.emplace_back();
		entry.name = quest.second->GetQuest()->Name();
		entry.description = quest.second->Desc();
		entry.icon = progress.icon;
		entry.progress = progress.progress;
		entry.target = progress.target;
	}

	return entries;
}

std::vector<server::DialogEntry> DialogEntriesFrom(const Dialog& dialog)
{
	std::vector<server::DialogEntry> entries;
	entries.reserve(dialog.Pages().size() + dialog.Links().size());

	for (const std::string& page : dialog.Pages())
	{
		server::DialogEntry& entry = entries.emplace_back(server::DialogEntry::ForText());
		entry.line = page;
	}

	for (const auto& link : dialog.Links())
	{
		server::DialogEntry::EntryTypeDataLink link_data;
		link_data.link_id = link.first;

		server::DialogEntry& entry = entries.emplace_back(server::DialogEntry::ForLink(link_data));
		entry.line = link.second;
	}

	return entries;
}

server::LevelUpStats LevelUpStatsFrom(const Character* character)
{
	server::LevelUpStats stats;
	stats.level = character->level;
	stats.stat_points = character->statpoints;
	stats.skill_points = character->skillpoints;
	stats.max_hp = character->maxhp;
	stats.max_tp = character->maxtp;
	stats.max_sp = character->maxsp;
	return stats;
}

eolib::protocol::net::Weight WeightFrom(const Character* character)
{
	eolib::protocol::net::Weight weight;
	weight.current = character->weight;
	weight.max = character->maxweight;
	return weight;
}

std::vector<eolib::protocol::net::ThreeItem> ChestItemsFrom(const Map_Chest& chest)
{
	std::vector<eolib::protocol::net::ThreeItem> items;
	items.reserve(chest.items.size());

	for (const Map_Chest_Item& item : chest.items)
	{
		if (item.id == 0)
			continue;

		eolib::protocol::net::ThreeItem& net_item = items.emplace_back();
		net_item.id = item.id;
		net_item.amount = item.amount;
	}

	return items;
}

std::vector<eolib::protocol::net::ThreeItem> LockerItemsFrom(const Character* character)
{
	std::vector<eolib::protocol::net::ThreeItem> items;
	items.reserve(character->bank.size());

	for (const protocol::net::Item& item : character->bank)
	{
		eolib::protocol::net::ThreeItem& net_item = items.emplace_back();
		net_item.id = item.id;
		net_item.amount = item.amount;
	}

	return items;
}

// The admin level the client is told the character has. Some lower levels are raised to unlock client features that
// the character's commands need.
static protocol::AdminLevel ClientAdminLevel(Character* character)
{
	World* world = character->world;

	protocol::AdminLevel lowest_command = protocol::AdminLevel::GameMaster;

	UTIL_FOREACH(world->admin_config, ac)
	{
		if (ac.first == "killnpc" || ac.first == "reports")
		{
			continue;
		}

		lowest_command = std::min<protocol::AdminLevel>(lowest_command, static_cast<protocol::AdminLevel>(util::to_int(ac.second)));
	}

	if (character->SourceAccess() >= world->AdminLevelSetting("seehide")
	 && character->SourceAccess() < protocol::AdminLevel::GameMaster)
	{
		return protocol::AdminLevel::GameMaster;
	}

	if (character->SourceDutyAccess() >= world->AdminLevelSetting("nowall")
	 && character->SourceDutyAccess() < protocol::AdminLevel::Guardian)
	{
		return protocol::AdminLevel::Guardian;
	}

	if (character->SourceAccess() >= lowest_command && character->SourceAccess() < protocol::AdminLevel::Spy)
	{
		return protocol::AdminLevel::Spy;
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
	data.admin = ClientAdminLevel(character);
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
	data.stats.secondary = CharacterSecondaryStatsFrom(character);
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
	data.weight = WeightFrom(character);

	data.items.assign(character->inventory.begin(), character->inventory.end());

	data.spells.assign(character->spells.begin(), character->spells.end());

	data.nearby = std::move(nearby);
	return data;
}

}
