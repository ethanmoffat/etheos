
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

#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>
#include <ctime>
#include <string>
#include <tuple>
#include <vector>

namespace net = eolib::protocol::net;

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

	player->Send(swap->SelectCharacterReply(false));

	net::server::WelcomeReplyServerPacket::WelcomeCodeDataEnterGame enter_game;
	enter_game.news.resize(9);
	enter_game.weight.current = swap->weight;
	enter_game.weight.max = swap->maxweight;

	UTIL_FOREACH(swap->inventory, item)
	{
		net::Item net_item;
		net_item.id = item.id;
		net_item.amount = item.amount;
		enter_game.items.push_back(net_item);
	}

	UTIL_FOREACH(swap->spells, spell)
	{
		net::Spell net_spell;
		net_spell.id = spell.id;
		net_spell.level = spell.level;
		enter_game.spells.push_back(net_spell);
	}

	enter_game.nearby.characters.push_back(swap->MapInfo());

	net::server::WelcomeReplyServerPacket reply;
	reply.welcome_code = net::server::WelcomeCode::EnterGame;
	reply.welcome_code_data = std::move(enter_game);
	player->Send(reply);

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
