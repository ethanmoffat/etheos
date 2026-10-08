
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
#include "../timer.hpp"
#include "../wedding.hpp"
#include "../world.hpp"

#include "../util.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>
#include <string>

namespace Handlers
{

static server::PriestReplyServerPacket PriestReplyFor(server::PriestReply reply_code)
{
	server::PriestReplyServerPacket reply;
	reply.reply_code = reply_code;
	return reply;
}

// Accepting a marriage request
void Priest_Accept(Character* character, const client::PriestAcceptClientPacket& packet)
{
	if (character->npc_session_id != packet.session_id)
		return;

	if (!character->partner.empty())
		return;

	Wedding* wedding = character->map->wedding;

	if (!wedding)
		return;

	if (wedding->Busy())
	{
		character->Send(PriestReplyFor(server::PriestReply::Busy));
		return;
	}

	Character* partner = character->world->GetCharacter(character->fiance);

	if (!partner || partner->map != character->map)
	{
		character->Send(PriestReplyFor(server::PriestReply::PartnerNotPresent));
		return;
	}

	if (character->map->wedding->RequestedWedding(partner))
	{
		character->map->wedding->StartWedding(partner->SourceName(), character->SourceName());
	}
}

// Talking to a priest NPC
void Priest_Open(Character* character, const client::PriestOpenClientPacket& packet)
{
	if (!character->partner.empty())
		return;

	if (!character->map->wedding)
		return;

	const auto match = std::find_if(
		character->map->npcs.begin(),
		character->map->npcs.end(),
		[&packet](const NPC* npc) { return npc->index == packet.npc_index && npc->PubData().type == pub::NpcType::Priest; });

	if (match == character->map->npcs.end())
		return;

	if (character->map->wedding->Busy())
	{
		character->Send(PriestReplyFor(server::PriestReply::Busy));
		return;
	}

	character->npc = *match;
	character->npc_type = pub::NpcType::Priest;

	server::PriestOpenServerPacket reply;
	reply.session_id = character->NewNpcSession();
	character->Send(reply);
}

static bool is_dressed(Character* character)
{
	std::string config_value = character->world->config[
		(character->gender == protocol::Gender::Male) ? "WeddingOutfitMale" : "WeddingOutfitFemale"
	];

	if (config_value == "0")
	{
		return true;
	}

	for (const std::string& part : util::explode(',', config_value))
	{
		if (util::to_int(part) == character->paperdoll[Character::EquipLocation::Armor])
		{
			return true;
		}
	}

	return false;
}

// Requesting marriage at a priest
void Priest_Request(Character* character, const client::PriestRequestClientPacket& packet)
{
	if (character->npc_type != pub::NpcType::Priest)
		return;

	if (character->npc_session_id != packet.session_id)
		return;

	const std::string name = util::lowercase(packet.name);

	if (character->map->wedding->Busy())
	{
		character->Send(PriestReplyFor(server::PriestReply::Busy));
		return;
	}

	if (name != character->fiance || name == character->SourceName())
	{
		character->Send(PriestReplyFor(server::PriestReply::NoPermission));
		return;
	}

	Character* partner = character->world->GetCharacter(character->fiance);

	if (!partner || partner->map != character->map)
	{
		character->Send(PriestReplyFor(server::PriestReply::PartnerNotPresent));
		return;
	}

	if (partner->fiance != character->SourceName())
	{
		character->Send(PriestReplyFor(server::PriestReply::NoPermission));
		return;
	}

	if (!partner->partner.empty())
	{
		character->Send(PriestReplyFor(server::PriestReply::PartnerAlreadyMarried));
		return;
	}

	if (!is_dressed(character))
	{
		character->Send(PriestReplyFor(server::PriestReply::NotDressed));
		return;
	}

	if (!is_dressed(partner))
	{
		character->Send(PriestReplyFor(server::PriestReply::PartnerNotDressed));
		return;
	}

	if (character->level < util::to_int(character->world->config["WeddingMinLevel"]))
	{
		character->Send(PriestReplyFor(server::PriestReply::LowLevel));
		return;
	}

	character->map->wedding->RequestWedding(character);

	server::PriestRequestServerPacket request;
	request.session_id = partner->NewNpcSession();
	request.partner_name = character->SourceName();
	partner->Send(request);
}

// Saying "I do" at a wedding
void Priest_Use(Character* character, const client::PriestUseClientPacket& packet)
{
	if (character->npc_session_id != packet.session_id)
		return;

	if (!character->map->wedding)
		return;

	character->map->wedding->IDo(character);
}

PACKET_HANDLER_REGISTER(PACKET_PRIEST)
	Register(Priest_Accept, Playing);
	Register(Priest_Open, Playing);
	Register(Priest_Request, Playing);
	Register(Priest_Use, Playing);
PACKET_HANDLER_REGISTER_END(PACKET_PRIEST)

}
