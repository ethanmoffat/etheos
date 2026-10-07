
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "commands.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../converters.hpp"
#include "../i18n.hpp"
#include "../world.hpp"

#include "../util.hpp"

#include <eolib/protocol/net/server/packets.hpp>

#include <string>
#include <vector>

namespace protocol = eolib::protocol;
namespace server = eolib::protocol::net::server;

namespace Commands
{

static std::string TitledName(Character* victim)
{
	const std::string name = util::ucfirst(victim->SourceName());
	std::string titled_name;

	switch (victim->admin)
	{
		case protocol::AdminLevel::GameMaster: titled_name = victim->world->i18n.Format("high_game_master_title", name); break;
		case protocol::AdminLevel::Guardian:   titled_name = victim->world->i18n.Format("game_master_title", name); break;
		case protocol::AdminLevel::LightGuide: titled_name = victim->world->i18n.Format("guardian_title", name); break;
		case protocol::AdminLevel::Spy:        titled_name = victim->world->i18n.Format("light_guide_title", name); break;
		default:                               titled_name = name; break;
	}

	return titled_name + " " + util::trim(victim->PaddedGuildTag());
}

void Inventory(const std::vector<std::string>& arguments, Character* from)
{
	Character *victim = from->world->GetCharacter(arguments[0]);

	if (!victim)
	{
		from->ServerMsg(from->world->i18n.Format("character_not_found"));
	}
	else
	{
		if (!from->world->CommandProtected(victim->SourceAccess(), from->SourceAccess()))
		{
			server::AdminInteractListServerPacket reply;
			reply.name = TitledName(victim);
			reply.usage = victim->Usage();
			reply.gold_bank = victim->goldbank;
			reply.inventory.assign(victim->inventory.begin(), victim->inventory.end());
			reply.bank = Converters::LockerItemsFrom(victim);
			from->Send(reply);
		}
		else
		{
			from->ServerMsg(from->world->i18n.Format("command_access_denied"));
		}
	}
}

void Paperdoll(const std::vector<std::string>& arguments, Character* from)
{
	Character *victim = from->world->GetCharacter(arguments[0]);

	if (!victim)
	{
		from->ServerMsg(from->world->i18n.Format("character_not_found"));
	}
	else
	{
		server::PaperdollReplyServerPacket reply;
		reply.details = Converters::CharacterDetailsFrom(victim);
		reply.equipment = Converters::EquipmentPaperdollFrom(victim);
		reply.icon = Converters::CharacterIconFrom(victim);
		from->Send(reply);
	}
}

void Book(const std::vector<std::string>& arguments, Character* from)
{
	Character *victim = from->world->GetCharacter(arguments[0]);

	if (!victim)
	{
		from->ServerMsg(from->world->i18n.Format("character_not_found"));
	}
	else
	{
		server::BookReplyServerPacket reply;
		reply.details = Converters::CharacterDetailsFrom(victim);
		reply.icon = Converters::CharacterIconFrom(victim);
		reply.quest_names = Converters::FinishedQuestNamesFrom(victim);
		from->Send(reply);
	}
}

void Info(const std::vector<std::string>& arguments, Character* from)
{
	Character *victim = from->world->GetCharacter(arguments[0]);

	if (!victim)
	{
		from->ServerMsg(from->world->i18n.Format("character_not_found"));
	}
	else
	{
		if (!from->world->CommandProtected(victim->SourceAccess(), from->SourceAccess()))
		{
			server::AdminInteractTellServerPacket reply;
			reply.name = TitledName(victim);
			reply.usage = victim->Usage();
			reply.gold_bank = victim->goldbank;
			reply.exp = victim->exp;
			reply.level = victim->level;
			reply.map_id = victim->mapid;
			reply.map_coords = Converters::BigCoordsFrom(victim->loc);
			reply.stats = Converters::CharacterStatsInfoLookupFrom(victim);
			reply.weight = Converters::WeightFrom(victim);
			from->Send(reply);
		}
		else
		{
			from->ServerMsg(from->world->i18n.Format("command_access_denied"));
		}
	}
}

COMMAND_HANDLER_REGISTER(info)
	RegisterCharacter({"inventory", {"victim"}, {}, 3}, Inventory);
	RegisterCharacter({"paperdoll", {"victim"}, {}}, Paperdoll);
	RegisterCharacter({"book", {"victim"}, {}, 2}, Book);
	RegisterCharacter({"info", {"victim"}, {}}, Info);

	RegisterAlias("p", "paperdoll");
	RegisterAlias("i", "info");
COMMAND_HANDLER_REGISTER_END(info)

}
