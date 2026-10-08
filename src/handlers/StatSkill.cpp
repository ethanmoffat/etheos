/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../converters.hpp"
#include "../map.hpp"
#include "../npc.hpp"
#include "../npc_data.hpp"
#include "../world.hpp"

#include "../util.hpp"

#include <algorithm>

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

namespace Handlers
{

static bool IsTrainerSession(Character* character, int session_id)
{
	return character->npc_type == pub::NpcType::Trainer
	    && character->npc_session_id == session_id;
}

static void SendWrongClass(Character* character, int class_id)
{
	server::StatSkillReplyServerPacket::ReplyCodeDataWrongClass wrong_class;
	wrong_class.class_id = class_id;
	character->Send(server::StatSkillReplyServerPacket::ForWrongClass(wrong_class));
}

// Talking to a skill master NPC
void StatSkill_Open(Character* character, const client::StatSkillOpenClientPacket& packet)
{
	const auto match = std::find_if(
		character->map->npcs.begin(),
		character->map->npcs.end(),
		[&packet](const NPC* npc) { return npc->index == packet.npc_index && !npc->Data().skill_learn.empty(); });

	if (match != character->map->npcs.end())
	{
		auto npc = *match;
		character->npc = npc;
		character->npc_type = pub::NpcType::Trainer;

		server::StatSkillOpenServerPacket reply;
		reply.session_id = character->NewNpcSession();
		reply.shop_name = npc->Data().skill_name;

		reply.skills = npc->Data().skill_learn;

		character->Send(reply);
	}
}

// Learning a skill
void StatSkill_Take(Character* character, const client::StatSkillTakeClientPacket& packet)
{
	const int spell_id = packet.spell_id;

	if (character->HasSpell(spell_id))
		return;

	if (!IsTrainerSession(character, packet.session_id))
		return;

	const auto& skill_learn = character->npc->Data().skill_learn;
	const auto match = std::find_if(
		skill_learn.begin(),
		skill_learn.end(),
		[spell_id](const server::SkillLearn& spell) { return spell.id == spell_id; });

	if (match == skill_learn.end())
		return;

	const server::SkillLearn& spell = *match;
	const server::SkillStatRequirements& stats = spell.stat_requirements;

	if (character->level < spell.level_requirement || character->HasItem(1) < spell.cost
	 || character->display_str < stats.str || character->display_intl < stats.intl
	 || character->display_wis < stats.wis || character->display_agi < stats.agi
	 || character->display_con < stats.con || character->display_cha < stats.cha)
	{
		// No correct reply for these
		SendWrongClass(character, character->clas);
		return;
	}

	if (!character->world->ecf->ClassMatches(character->clas, spell.class_requirement))
	{
		SendWrongClass(character, spell.class_requirement);
		return;
	}

	const bool missing_skill = std::any_of(
		spell.skill_requirements.begin(),
		spell.skill_requirements.end(),
		[character](int req) { return req != 0 && !character->HasSpell(req); });

	if (missing_skill)
	{
		// No correct reply for this
		SendWrongClass(character, character->clas);
		return;
	}

	character->DelItem(1, spell.cost);
	character->AddSpell(spell_id);

	server::StatSkillTakeServerPacket reply;
	reply.spell_id = spell_id;
	reply.gold_amount = character->HasItem(1);
	character->Send(reply);
}

// Forgeting a skill
void StatSkill_Remove(Character* character, const client::StatSkillRemoveClientPacket& packet)
{
	if (!IsTrainerSession(character, packet.session_id))
		return;

	if (character->DelSpell(packet.spell_id))
	{
		server::StatSkillRemoveServerPacket reply;
		reply.spell_id = packet.spell_id;
		character->Send(reply);
	}
}

// Spending a stat point on a skill
void StatSkill_Add(Character* character, const client::StatSkillAddClientPacket& packet)
{
	if (const auto* stat_data = packet.AsStat())
	{
		if (character->statpoints <= 0)
			return;

		short* stat;

		switch (stat_data->stat_id)
		{
			case client::StatId::Str: stat = &character->str; break;
			case client::StatId::Int: stat = &character->intl; break;
			case client::StatId::Wis: stat = &character->wis; break;
			case client::StatId::Agi: stat = &character->agi; break;
			case client::StatId::Con: stat = &character->con; break;
			case client::StatId::Cha: stat = &character->cha; break;
			default: return;
		}

		++(*stat);
		--character->statpoints;

		character->CalculateStats();

		server::StatSkillPlayerServerPacket stat_skill_packet;
		stat_skill_packet.stat_points = character->statpoints;
		stat_skill_packet.stats = Converters::CharacterStatsUpdateFrom(character);
		character->Send(stat_skill_packet);
	}
	else if (const auto* skill_data = packet.AsSkill())
	{
		const int spell_id = skill_data->spell_id;

		if (character->skillpoints <= 0 || !character->HasSpell(spell_id)
		 || character->SpellLevel(spell_id) >= int(character->world->config["MaxSkillLevel"]))
		{
			return;
		}

		const auto spell = std::find_if(
			character->spells.begin(),
			character->spells.end(),
			[spell_id](const eolib::protocol::net::Spell& learned) { return learned.id == spell_id; });

		if (spell == character->spells.end())
			return;

		++spell->level;
		--character->skillpoints;

		server::StatSkillAcceptServerPacket accept_packet;
		accept_packet.skill_points = character->skillpoints;
		accept_packet.spell = *spell;
		character->Send(accept_packet);
	}
}

// Reseting character's skills
void StatSkill_Junk(Character* character, const client::StatSkillJunkClientPacket& packet)
{
	if (!IsTrainerSession(character, packet.session_id))
		return;

	character->Reset();

	server::StatSkillJunkServerPacket reply;
	reply.stats = Converters::CharacterStatsResetFrom(character);
	character->Send(reply);
}

PACKET_HANDLER_REGISTER(PACKET_STATSKILL)
	Register(StatSkill_Open, Playing);
	Register(StatSkill_Take, Playing);
	Register(StatSkill_Remove, Playing);
	Register(StatSkill_Add, Playing);
	Register(StatSkill_Junk, Playing);
PACKET_HANDLER_REGISTER_END(PACKET_STATSKILL)

}
