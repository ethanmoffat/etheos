
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "commands.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../converters.hpp"
#include "../eoplus.hpp"
#include "../i18n.hpp"
#include "../map.hpp"
#include "../npc.hpp"
#include "../quest.hpp"
#include "../timer.hpp"
#include "../world.hpp"

#include "../util.hpp"

#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace server = eolib::protocol::net::server;

namespace Commands
{

void SpawnItem(const std::vector<std::string>& arguments, Character* from)
{
	int id = util::to_int(arguments[0]);
	int amount = (arguments.size() >= 2) ? util::to_int(arguments[1]) : 1;

	if (from->AddItem(id, amount))
	{
		server::ItemGetServerPacket reply;
		reply.taken_item.id = id;
		reply.taken_item.amount = amount;
		reply.weight = Converters::WeightFrom(from);
		from->Send(reply);
	}
}

void DropItem(const std::vector<std::string>& arguments, Character* from)
{
	if (from->trading) return;

	int id = util::to_int(arguments[0]);
	int amount = (arguments.size() >= 2) ? util::to_int(arguments[1]) : 1;
	int x = (arguments.size() >= 3) ? util::to_int(arguments[2]) : from->loc.x;
	int y = (arguments.size() >= 4) ? util::to_int(arguments[3]) : from->loc.y;

	if (amount > 0 && from->HasItem(id) >= amount)
	{
		std::shared_ptr<Map_Item> item = from->map->AddItem(id, amount, x, y, from);

		if (item)
		{
			item->owner = from->PlayerID();
			item->unprotecttime = Timer::GetTime() + double(from->world->config["ProtectPlayerDrop"]);
			from->DelItem(id, amount);

			server::ItemDropServerPacket reply;
			reply.dropped_item.id = id;
			reply.dropped_item.amount = amount;
			reply.remaining_amount = from->HasItem(id);
			reply.item_index = item->uid;
			reply.coords = item->loc;
			reply.weight = Converters::WeightFrom(from);
			from->Send(reply);
		}
	}
}

void SpawnNPC(const std::vector<std::string>& arguments, Character* from)
{
	int id = util::to_int(arguments[0]);
	int amount = (arguments.size() >= 2) ? util::to_int(arguments[1]) : 1;
	int speed = (arguments.size() >= 3) ? util::to_int(arguments[2]) : int(from->world->config["SpawnNPCSpeed"]);
	int direction = (arguments.size() >= 4) ? util::to_int(arguments[3]) : static_cast<int>(Direction::Down);

	if (speed < 0 || speed > NPC::StationarySpawnType)
		return;

	if (direction < static_cast<int>(Direction::Down) || direction > static_cast<int>(Direction::Right))
		return;

	if (!from->world->enf->Exists(id))
		return;

	for (int i = 0; i < amount; ++i)
	{
		unsigned char index = from->map->GenerateNPCIndex();

		if (index > 250)
			break;

		NPC* npc = new NPC(from->map, id, from->loc, speed, 0, static_cast<Direction>(direction), index, true);
		from->map->npcs.push_back(npc);
		npc->Spawn();
	}
}

void DespawnNPC(const std::vector<std::string>& arguments, Character* from)
{
	(void)arguments;

	std::vector<NPC*> despawn_npcs;

	for (NPC* npc : from->map->npcs)
	{
		if (npc->temporary)
		{
			despawn_npcs.push_back(npc);
		}
	}

	for (NPC* npc : despawn_npcs)
	{
		npc->Die();
	}
}

void Learn(const std::vector<std::string>& arguments, Character* from)
{
	short skill_id = util::to_int(arguments[0]);
	short level = -1;

	if (arguments.size() >= 2)
		level = std::max(0, std::min<int>(from->world->config["MaxSkillLevel"], util::to_int(arguments[1])));

	if (from->AddSpell(skill_id))
	{
		server::StatSkillTakeServerPacket packet;
		packet.spell_id = skill_id;
		packet.gold_amount = from->HasItem(1);
		from->Send(packet);
	}
	else
	{
		from->ServerMsg(from->SourceWorld()->i18n.Format("skill_not_found"));
	}

	if (level >= 0)
	{
		auto it = std::find_if(UTIL_RANGE(from->spells), [&](const eolib::protocol::net::Spell& spell) { return spell.id == skill_id; });

		if (it != from->spells.end())
		{
			it->level = level;

			server::StatSkillAcceptServerPacket accept_packet;
			accept_packet.skill_points = from->skillpoints;
			accept_packet.spell = *it;
			from->Send(accept_packet);
		}
	}
}

void Teach(const std::vector<std::string>& arguments, Character* from)
{
	Character* victim = from->SourceWorld()->GetCharacter(arguments[0]);

	if (!victim || victim->nowhere)
	{
		from->ServerMsg(from->SourceWorld()->i18n.Format("character_not_found"));
		return;
	}

	if (victim->SourceAccess() >= from->SourceAccess())
	{
		from->ServerMsg(from->SourceWorld()->i18n.Format("command_access_denied"));
		return;
	}

	short skill_id = util::to_int(arguments[1]);
	short level = -1;

	if (arguments.size() >= 3)
		level = std::max(0, std::min<int>(from->world->config["MaxSkillLevel"], util::to_int(arguments[2])));

	if (victim->AddSpell(skill_id))
	{
		server::StatSkillTakeServerPacket packet;
		packet.spell_id = skill_id;
		packet.gold_amount = victim->HasItem(1);
		victim->Send(packet);

		if (level >= 0)
		{
			auto it = std::find_if(UTIL_RANGE(victim->spells), [&](const eolib::protocol::net::Spell& spell) { return spell.id == skill_id; });

			if (it != victim->spells.end())
			{
				it->level = level;

				server::StatSkillAcceptServerPacket accept_packet;
				accept_packet.skill_points = victim->skillpoints;
				accept_packet.spell = *it;
				victim->Send(accept_packet);
			}
		}

		from->world->ServerMsg(
			from->SourceWorld()->i18n.Format(
				"taught_skill",
				victim->SourceName(),
				from->world->esf->Get(skill_id).name,
				from->SourceName()
			)
		);
	}
	else
	{
		from->ServerMsg(from->SourceWorld()->i18n.Format("skill_not_found"));
	}
}

void Forget(const std::vector<std::string>& arguments, Character* from)
{
	Character* victim = nullptr;
	if (arguments.size() >= 2)
	{
		victim = from->SourceWorld()->GetCharacter(arguments[1]);

		if (!victim || victim->nowhere)
		{
			from->ServerMsg(from->SourceWorld()->i18n.Format("character_not_found"));
			return;
		}

		if (victim->SourceAccess() >= from->SourceAccess())
		{
			from->ServerMsg(from->SourceWorld()->i18n.Format("command_access_denied"));
			return;
		}
	}
	else
	{
		victim = from;
	}

	short skill_id = util::to_int(arguments[0]);

	if (victim->DelSpell(skill_id))
	{
		server::StatSkillRemoveServerPacket packet;
		packet.spell_id = skill_id;
		victim->Send(packet);

		if (from != victim)
		{
			from->world->ServerMsg(
				from->SourceWorld()->i18n.Format(
					"forgot_skill",
					victim->SourceName(),
					from->world->esf->Get(skill_id).name,
					from->SourceName()
				)
			);
		}
	}
	else
	{
		from->ServerMsg(from->SourceWorld()->i18n.Format("skill_not_found"));
	}
}

void QuestState(const std::vector<std::string>& arguments, Character* from)
{
	World* world = from->SourceWorld();
	short quest_id = util::to_int(arguments[0]);

	std::shared_ptr<Quest_Context> quest = from->GetQuest(quest_id);

	if (!quest)
	{
		auto it = world->quests.find(quest_id);

		if (it != world->quests.end())
		{
			// WARNING: holds a non-tracked reference to shared_ptr
			quest = std::make_shared<Quest_Context>(from, it->second.get());
			from->quests[it->first] = quest;
			quest->SetState("begin", true);
		}
	}

	if (!quest)
	{
		from->ServerMsg(world->i18n.Format("quest_not_found"));
	}
	else
	{
		try
		{
			quest->SetState(arguments[1]);
		}
		catch (EOPlus::Runtime_Error& e)
		{
			(void)e;
			from->ServerMsg(world->i18n.Format("quest_state_not_found"));
		}
	}
}

COMMAND_HANDLER_REGISTER(debug)
	RegisterCharacter({"sitem", {"item"}, {"amount"}, 2}, SpawnItem, CMD_FLAG_DUTY_RESTRICT);
	RegisterCharacter({"ditem", {"item"}, {"amount", "x", "y"}, 2}, DropItem, CMD_FLAG_DUTY_RESTRICT);
	RegisterCharacter({"snpc", {"npc"}, {"amount", "speed", "direction"}, 2}, SpawnNPC, CMD_FLAG_DUTY_RESTRICT);
	RegisterCharacter({"dnpc", {}, {}, 2}, DespawnNPC, CMD_FLAG_DUTY_RESTRICT);
	RegisterCharacter({"learn", {"skill"}, {"level"}, 4}, Learn, CMD_FLAG_DUTY_RESTRICT);
	RegisterCharacter({"teach", {"victim", "skill"}, {"level"}, 4}, Teach, CMD_FLAG_DUTY_RESTRICT);
	RegisterCharacter({"forget", {"skill"}, {"victim"}, 4}, Forget, CMD_FLAG_DUTY_RESTRICT);
	RegisterCharacter({"qstate", {"quest", "state"}}, QuestState, CMD_FLAG_DUTY_RESTRICT);
	RegisterAlias("si", "sitem");
	RegisterAlias("di", "ditem");
	RegisterAlias("sn", "snpc");
	RegisterAlias("dn", "dnpc");
COMMAND_HANDLER_REGISTER_END(debug)

}
