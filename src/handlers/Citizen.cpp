
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../eodata.hpp"
#include "../map.hpp"
#include "../npc.hpp"
#include "../npc_data.hpp"
#include "../party.hpp"
#include "../world.hpp"

#include "../util.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>
#include <string>

namespace Handlers
{

static bool IsInnSessionValid(Character* character, int session_id)
{
	return character->npc_type == pub::NpcType::Inn && character->npc_session_id == session_id;
}

// Sleeping at an inn
void Citizen_Request(Character* character, const client::CitizenRequestClientPacket& packet)
{
	if (!IsInnSessionValid(character, packet.session_id))
		return;

	if (character->hp < character->maxhp || character->tp < character->maxtp)
	{
		unsigned int missing_hp = static_cast<unsigned int>(character->maxhp - character->hp);
		unsigned int missing_tp = static_cast<unsigned int>(character->maxtp - character->tp);

		unsigned int sleep_cost =
			static_cast<int>(character->world->config["InnSleepCostBase"]) +
			missing_hp * static_cast<int>(character->world->config["InnSleepCostPerHP"]) +
			missing_tp * static_cast<int>(character->world->config["InnSleepCostPerTP"]);

		server::CitizenRequestServerPacket reply;
		reply.cost = sleep_cost;
		character->Send(reply);
	}
}

// Accepting cost to sleep at an inn
void Citizen_Accept(Character* character, const client::CitizenAcceptClientPacket& packet)
{
	if (IsInnSessionValid(character, packet.session_id))
	{
		unsigned int missing_hp = static_cast<unsigned int>(character->maxhp - character->hp);
		unsigned int missing_tp = static_cast<unsigned int>(character->maxtp - character->tp);

		unsigned int sleep_cost =
			static_cast<int>(character->world->config["InnSleepCostBase"]) +
			missing_hp * static_cast<int>(character->world->config["InnSleepCostPerHP"]) +
			missing_tp * static_cast<int>(character->world->config["InnSleepCostPerTP"]);

		if (static_cast<unsigned int>(character->HasItem(1)) >= sleep_cost)
		{
			character->DelItem(1, sleep_cost);

			character->hp = character->maxhp;
			character->tp = character->maxtp;

			if (character->party)
			{
				character->party->UpdateHP(character);
			}

			server::CitizenAcceptServerPacket reply;
			reply.gold_amount = character->HasItem(1);
			character->Send(reply);

			auto home_name = character->npc->Data().citizenship->home;
			auto home_info = character->world->GetHome(home_name);

			if (home_info)
			{
				character->Warp(home_info->sleep_map, home_info->sleep_loc);
			}

			character->npc = nullptr;
			character->npc_type = pub::NpcType::Friendly;
		}
	}
}

// Player subscribing to a town
void Citizen_Reply(Character* character, const client::CitizenReplyClientPacket& packet)
{
	if (!IsInnSessionValid(character, packet.session_id))
		return;

	if (character->home.size() == 0 || character->world->config["CitizenSubscribeAnytime"])
	{
		int questions_wrong = 0;

		for (int i = 0; i < 3; ++i)
		{
			if (util::lowercase(packet.answers[i]) != util::lowercase(character->npc->Data().citizenship->answers[i]))
			{
				++questions_wrong;
			}
		}

		if (questions_wrong == 0)
		{
			character->home = character->npc->Data().citizenship->home;
		}

		server::CitizenReplyServerPacket reply;
		reply.questions_wrong = questions_wrong;
		character->Send(reply);
	}
}

// Player giving up citizenship of a town
void Citizen_Remove(Character* character, const client::CitizenRemoveClientPacket& packet)
{
	(void)packet;

	if (character->npc_type == pub::NpcType::Inn)
	{
		server::CitizenRemoveServerPacket reply;

		if (character->home == character->npc->Data().citizenship->home || character->world->config["CitizenUnsubscribeAnywhere"])
		{
			character->home = "";
			reply.reply_code = server::InnUnsubscribeReply::Unsubscribed;
		}
		else
		{
			reply.reply_code = server::InnUnsubscribeReply::NotCitizen;
		}

		character->Send(reply);
	}
}

// Talked to a citizenship NPC
void Citizen_Open(Character* character, const client::CitizenOpenClientPacket& packet)
{
	const auto match = std::find_if(
		character->map->npcs.begin(),
		character->map->npcs.end(),
		[&packet](const NPC* npc) { return npc->index == packet.npc_index && npc->PubData().type == pub::NpcType::Inn && npc->Data().citizenship; });

	if (match == character->map->npcs.end())
		return;

	NPC* npc = *match;
	character->npc = npc;
	character->npc_type = pub::NpcType::Inn;

	auto home = character->world->GetHome(character);
	int innkeeper_vend = 0;

	if (home)
		innkeeper_vend = home->innkeeper_vend;

	server::CitizenOpenServerPacket reply;
	reply.behavior_id = npc->PubData().behavior_id;

	if (character->world->config["CitizenSubscribeAnytime"] && innkeeper_vend != npc->PubData().behavior_id)
		reply.current_home_id = 0;
	else
		reply.current_home_id = innkeeper_vend;

	reply.session_id = character->NewNpcSession();
	reply.questions.assign(npc->Data().citizenship->questions.begin(), npc->Data().citizenship->questions.end());
	character->Send(reply);
}

PACKET_HANDLER_REGISTER(Citizen)
	Register(Citizen_Request, Playing);
	Register(Citizen_Accept, Playing);
	Register(Citizen_Reply, Playing);
	Register(Citizen_Remove, Playing);
	Register(Citizen_Open, Playing);
PACKET_HANDLER_REGISTER_END(Citizen)

}
