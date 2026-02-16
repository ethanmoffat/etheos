
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "commands.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../eoclient.hpp"
#include "../eodata.hpp"
#include "../i18n.hpp"
#include "../map.hpp"
#include "../npc.hpp"
#include "../packet.hpp"
#include "../player.hpp"
#include "../world.hpp"

#include "../util.hpp"

#include <algorithm>
#include <ctime>
#include <string>
#include <tuple>
#include <vector>

static bool get_admin_level(World* world, const std::string& name, AdminLevel& level)
{
	Character* victim = world->GetCharacter(name);
	if (victim)
	{
		level = victim->SourceAccess();
		return true;
	}
	else
	{
		Database_Result query_res = world->db->Query("SELECT `admin` FROM `characters` WHERE `name` = '$'", name.c_str());
		if (query_res.size() > 0)
		{
			level = static_cast<AdminLevel>(query_res[0]["admin"].GetInt());
			return true;
		}
	}

	return false;
}

namespace Commands
{

void Duty(const std::vector<std::string>& arguments, Character* from)
{
	World* world = from->world;

	if (!world->config["UseDutyAdmin"])
	{
		from->ServerMsg(from->SourceWorld()->i18n.Format("unknown_command"));
		return;
	}

	Player* player = from->player;
	Character* swap = nullptr;

	bool dutyoff = !player->dutylast.empty();

	for (Character* c : from->player->characters)
	{
		if ((dutyoff && c->real_name == player->dutylast)
		 || (!dutyoff && c != from && c->admin))
		{
			if (player->dutylast.empty())
				player->dutylast = from->real_name;
			else
				player->dutylast.clear();

			swap = c;
			break;
		}
	}

	if (!swap)
	{
		from->ServerMsg(from->SourceWorld()->i18n.Format("character_not_found"));
		return;
	}

	from->Logout();
	player->character = swap;

	if (!dutyoff && arguments.size() > 0 && util::lowercase(arguments[0]) == "here")
	{
		swap->mapid = from->mapid;
		swap->x = from->x;
		swap->y = from->y;
		swap->direction = from->direction;
		swap->sitting = from->sitting;
	}

	swap->faux_name = from->SourceName();

	world->Login(swap);
	swap->CalculateStats();

	{
		std::string guild_str = swap->GuildNameString();
		std::string guild_rank = swap->GuildRankString();

		PacketBuilder reply(PACKET_WELCOME, PACKET_REPLY,
			114 + swap->paperdoll.size() * 2 + swap->SourceName().length() + swap->title.length()
			+ guild_str.length() + guild_rank.length());

		reply.AddShort(1); // REPLY_WELCOME sub-id
		reply.AddShort(swap->PlayerID());
		reply.AddInt(swap->id);
		reply.AddShort(swap->mapid); // Map ID

		if (world->config["GlobalPK"] && !world->PKExcept(swap->mapid))
		{
			reply.AddByte(0xFF);
			reply.AddByte(0x01);
		}
		else
		{
			reply.AddByte(world->GetMap(swap->mapid)->rid[0]);
			reply.AddByte(world->GetMap(swap->mapid)->rid[1]);
		}

		reply.AddByte(world->GetMap(swap->mapid)->rid[2]);
		reply.AddByte(world->GetMap(swap->mapid)->rid[3]);
		reply.AddThree(world->GetMap(swap->mapid)->filesize);
		reply.AddByte(world->eif->rid[0]);
		reply.AddByte(world->eif->rid[1]);
		reply.AddByte(world->eif->rid[2]);
		reply.AddByte(world->eif->rid[3]);
		reply.AddByte(world->eif->len[0]);
		reply.AddByte(world->eif->len[1]);
		reply.AddByte(world->enf->rid[0]);
		reply.AddByte(world->enf->rid[1]);
		reply.AddByte(world->enf->rid[2]);
		reply.AddByte(world->enf->rid[3]);
		reply.AddByte(world->enf->len[0]);
		reply.AddByte(world->enf->len[1]);
		reply.AddByte(world->esf->rid[0]);
		reply.AddByte(world->esf->rid[1]);
		reply.AddByte(world->esf->rid[2]);
		reply.AddByte(world->esf->rid[3]);
		reply.AddByte(world->esf->len[0]);
		reply.AddByte(world->esf->len[1]);
		reply.AddByte(world->ecf->rid[0]);
		reply.AddByte(world->ecf->rid[1]);
		reply.AddByte(world->ecf->rid[2]);
		reply.AddByte(world->ecf->rid[3]);
		reply.AddByte(world->ecf->len[0]);
		reply.AddByte(world->ecf->len[1]);
		reply.AddBreakString(swap->SourceName());
		reply.AddBreakString(swap->title);
		reply.AddBreakString(guild_str); // Guild Name
		reply.AddBreakString(guild_rank); // Guild Rank
		reply.AddChar(swap->clas);
		reply.AddString(swap->PaddedGuildTag());

		// Tell a player's client they're a higher level admin than they are to enable some features

		AdminLevel lowest_command = ADMIN_HGM;

		UTIL_FOREACH(world->admin_config, ac)
		{
			if (ac.first == "killnpc" || ac.first == "reports")
			{
				continue;
			}

			lowest_command = std::min<AdminLevel>(lowest_command, static_cast<AdminLevel>(util::to_int(ac.second)));
		}

		if (swap->SourceAccess() >= static_cast<int>(world->admin_config["seehide"])
		 && swap->SourceAccess() < ADMIN_HGM)
		{
			reply.AddChar(ADMIN_HGM);
		}
		else if (swap->SourceDutyAccess() >= static_cast<int>(world->admin_config["nowall"])
		 && swap->SourceDutyAccess() < ADMIN_GM)
		{
			reply.AddChar(ADMIN_GM);
		}
		else if (swap->SourceAccess() >= lowest_command
		 && swap->SourceAccess() < ADMIN_GUIDE)
		{
			reply.AddChar(ADMIN_GUIDE);
		}
		else
		{
			reply.AddChar(swap->SourceAccess());
		}

		reply.AddChar(swap->level);
		reply.AddInt(swap->exp);
		reply.AddInt(swap->usage);
		reply.AddShort(swap->hp);
		reply.AddShort(swap->maxhp);
		reply.AddShort(swap->tp);
		reply.AddShort(swap->maxtp);
		reply.AddShort(swap->maxsp);
		reply.AddShort(swap->statpoints);
		reply.AddShort(swap->skillpoints);
		reply.AddShort(swap->karma);
		reply.AddShort(swap->mindam);
		reply.AddShort(swap->maxdam);
		reply.AddShort(swap->accuracy);
		reply.AddShort(swap->evade);
		reply.AddShort(swap->armor);

		reply.AddShort(swap->display_str);
		reply.AddShort(swap->display_wis);
		reply.AddShort(swap->display_intl);
		reply.AddShort(swap->display_agi);
		reply.AddShort(swap->display_con);
		reply.AddShort(swap->display_cha);

		UTIL_FOREACH(swap->paperdoll, item)
		{
			reply.AddShort(item);
		}

		int leader_rank = std::max(std::max(std::max(static_cast<int>(world->config["GuildEditRank"]), static_cast<int>(world->config["GuildKickRank"])),
								   static_cast<int>(world->config["GuildPromoteRank"])), static_cast<int>(world->config["GuildDemoteRank"]));

		if (swap->guild_rank <= leader_rank && swap->guild)
		{
			reply.AddChar(1); // Allows client access to the guild management tools
		}
		else
		{
			reply.AddChar(swap->guild_rank);
		}

		reply.AddShort(static_cast<int>(world->config["JailMap"]));
		reply.AddShort(4); // ?
		reply.AddChar(24); // ?
		reply.AddChar(24); // ?
		reply.AddShort(0); // Light guide admin command flood rate
		reply.AddShort(0); // Guardian admin command flood rate
		reply.AddShort(0); // GM/HGM admin command flood rate
		reply.AddShort(2); // ?
		reply.AddChar(0); // Login warning message
		reply.AddByte(255);

		player->Send(reply);
	}

	{
		PacketBuilder reply(PACKET_WELCOME, PACKET_REPLY, 65 + swap->SourceName().length() + swap->inventory.size() * 6 + swap->spells.size() * 4);

		reply.AddShort(2); // REPLY_WELCOME sub-id
		// MotDs
		reply.AddByte(255);

		for (int i = 0; i < 9; ++i)
		{
			reply.AddByte(255);
		}

		// ??
		reply.AddChar(static_cast<unsigned char>(swap->weight));
		reply.AddChar(static_cast<unsigned char>(swap->maxweight));
		UTIL_FOREACH(swap->inventory, item)
		{
			reply.AddShort(item.id);
			reply.AddInt(item.amount);
		}
		reply.AddByte(255);
		UTIL_FOREACH(swap->spells, spell)
		{
			reply.AddShort(spell.id);
			reply.AddShort(spell.level);
		}
		reply.AddByte(255);

		reply.AddChar(1); // Number of players
		reply.AddByte(255);

		reply.AddBreakString(swap->SourceName());
		reply.AddShort(swap->PlayerID());
		reply.AddShort(swap->mapid);
		reply.AddShort(swap->x);
		reply.AddShort(swap->y);
		reply.AddChar(swap->direction);
		reply.AddChar(6); // ?
		reply.AddString(swap->PaddedGuildTag());
		reply.AddChar(swap->level);
		reply.AddChar(swap->gender);
		reply.AddChar(swap->hairstyle);
		reply.AddChar(swap->haircolor);
		reply.AddChar(swap->race);
		reply.AddShort(swap->maxhp);
		reply.AddShort(swap->hp);
		reply.AddShort(swap->maxtp);
		reply.AddShort(swap->tp);
		swap->AddPaperdollData(reply, "B000A0HSW");
		reply.AddChar(swap->sitting);
		reply.AddChar(swap->IsHideInvisible());
		reply.AddByte(255);

		reply.AddByte(255);

		player->Send(reply);
	}

	swap->Warp(swap->mapid, swap->x, swap->y, WARP_ANIMATION_NONE);
}

void Audit(const std::vector<std::string>& arguments, Character* from)
{
	World* world = from->SourceWorld();

	bool has_target = !arguments.empty();
	std::string target_name = has_target ? util::lowercase(arguments[0]) : "";

	std::unordered_map<std::string, AdminLevel> admin_levels_cache;

	if (has_target)
	{
		AdminLevel victim_level;
		if (get_admin_level(world, target_name, victim_level))
		{
			admin_levels_cache[target_name] = victim_level;
		}
		else
		{
			from->ServerMsg(world->i18n.Format("character_not_found"));
			return;
		}

		if (admin_levels_cache[target_name] >= int(world->admin_config["cmdprotect"]) && admin_levels_cache[target_name] > from->SourceAccess())
		{
			from->ServerMsg(world->i18n.Format("command_access_denied"));
			return;
		}
	}

	std::vector<std::tuple<std::time_t, std::string, std::string, std::vector<std::string>>> all_commands;
	all_commands.reserve(world->command_audit.size() + world->command_audit_uncommitted.size());
	all_commands.insert(all_commands.end(), world->command_audit.begin(), world->command_audit.end());
	all_commands.insert(all_commands.end(), world->command_audit_uncommitted.begin(), world->command_audit_uncommitted.end());

	std::reverse(all_commands.begin(), all_commands.end());

	const int max_display = 100;
	int shown_count = 0;
	int month = -1;
	int day = -1;

	std::vector<std::string> message_lines;
	for (const auto& entry : all_commands)
	{
		std::time_t timestamp = std::get<0>(entry);
		const std::string& source = std::get<1>(entry);
		const std::string& command = std::get<2>(entry);
		const std::vector<std::string>& audit_cmd_args = std::get<3>(entry);

		std::string source_lower = util::lowercase(source);

		if (has_target && source_lower != target_name)
			continue;

		if (!has_target)
		{
			AdminLevel target_admin_level = ADMIN_PLAYER;
			if (admin_levels_cache.find(source_lower) != admin_levels_cache.end())
			{
				target_admin_level = admin_levels_cache[source_lower];
			}
			else if (!get_admin_level(world, source_lower, target_admin_level))
			{
				continue;
			}
			else
			{
				admin_levels_cache[source_lower] = target_admin_level;
			}

			if (target_admin_level >= int(world->admin_config["cmdprotect"]) && target_admin_level > from->SourceAccess())
			{
				continue;
			}
		}

		char timestr[16] = { 0 };
		auto tm = std::localtime(&timestamp);
		if (month < 0 || day < 0)
		{
			month = tm->tm_mon;
			day = tm->tm_mday;
			std::strftime(timestr, 16, "%Y-%m-%d", tm);
			message_lines.push_back(std::string(timestr) + ": ");
		}
		else if (tm->tm_mon != month || tm->tm_mday != day)
		{
			message_lines.push_back("");
			month = tm->tm_mon;
			day = tm->tm_mday;
			std::strftime(timestr, 16, "%Y-%m-%d", tm);
			message_lines.push_back(std::string(timestr) + ": ");
		}

		// Format command with arguments
		std::string full_command = command;
		if (!audit_cmd_args.empty())
		{
			full_command += " ";
			for (std::size_t i = 0; i < audit_cmd_args.size(); ++i)
			{
				if (i > 0)
					full_command += " ";
				full_command += audit_cmd_args[i];
			}
		}

		const int source_name_reserve = 16;
		while (source_lower.size() < source_name_reserve) source_lower += " ";
		message_lines.push_back(source_lower + full_command);

		shown_count++;
		if (shown_count >= max_display - 1)
		{
			message_lines.insert(message_lines.begin(), "... (output truncated, showing most recent " + util::to_string(max_display) + " entries)");
			break;
		}
	}

	from->ShowInfoBox(
		"Admin command audit" + std::string(has_target ? (" for " + arguments[0]) : "") + " [" + util::to_string(shown_count) + "]",
		message_lines
	);
}

COMMAND_HANDLER_REGISTER(admin)
	RegisterCharacter({"duty", {}, {"here"}}, Duty);
	RegisterCharacter({"audit", {}, {"target"}}, Audit);

	RegisterAlias("d", "duty");
COMMAND_HANDLER_REGISTER_END(admin)

}
