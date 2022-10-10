
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
#include "../packet.hpp"
#include "../timer.hpp"
#include "../wedding.hpp"
#include "../world.hpp"

#include "../util.hpp"

namespace Handlers
{

// Accepting a marriage request
void Priest_Accept(Character *character, PacketReader &reader)
{
	(void) reader;

	if (!character->partner.empty())
		return;

	Wedding* wedding = character->map->wedding;

	if (!wedding)
		return;

	if (wedding->Busy())
	{
		PacketBuilder reply(PACKET_PRIEST, PACKET_REPLY, 2);
		reply.AddShort(PRIEST_BUSY);
		character->Send(reply);
	}

	Character* partner = character->world->GetCharacter(character->fiance);

	if (!partner || partner->map != character->map)
	{
		PacketBuilder reply(PACKET_PRIEST, PACKET_REPLY, 2);
		reply.AddShort(PRIEST_PARTNER_NOT_PRESENT);
		character->Send(reply);
		return;
	}

	if (character->map->wedding->RequestedWedding(partner))
	{
		character->map->wedding->StartWedding(partner->SourceName(), character->SourceName());
	}
}

// Talking to a priest NPC
void Priest_Open(Character *character, PacketReader &reader)
{
	short id = reader.GetShort();

	if (!character->partner.empty())
		return;

	if (!character->map->wedding)
		return;

	UTIL_FOREACH(character->map->npcs, npc)
	{
		if (npc->index == id && npc->ENF().type == ENF::Priest)
		{
			if (character->map->wedding->Busy())
			{
				PacketBuilder reply(PACKET_PRIEST, PACKET_REPLY, 2);
				reply.AddShort(PRIEST_BUSY);
				character->Send(reply);
			}
			else
			{
				character->npc = npc;
				character->npc_type = ENF::Priest;

				PacketBuilder reply(PACKET_PRIEST, PACKET_OPEN, 4);
				reply.AddInt(npc->id);
				character->Send(reply);
			}
			break;
		}
	}
}

static bool is_dressed(Character* character)
{
	std::string config_value = character->world->config[
		(character->gender == GENDER_MALE) ? "WeddingOutfitMale" : "WeddingOutfitFemale"
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
void Priest_Request(Character *character, PacketReader &reader)
{
	/*int session_id = */reader.GetInt();
	reader.GetByte();
	std::string name = util::lowercase(reader.GetEndString());

	if (character->npc_type == ENF::Priest)
	{
		if (character->map->wedding->Busy())
		{
			PacketBuilder reply(PACKET_PRIEST, PACKET_REPLY, 2);
			reply.AddShort(PRIEST_BUSY);
			character->Send(reply);
		}

		if (name != character->fiance || name == character->SourceName())
		{
			PacketBuilder reply(PACKET_PRIEST, PACKET_REPLY, 2);
			reply.AddShort(PRIEST_NO_PERMISSION);
			character->Send(reply);
			return;
		}

		Character* partner = character->world->GetCharacter(character->fiance);

		if (!partner || partner->map != character->map)
		{
			PacketBuilder reply(PACKET_PRIEST, PACKET_REPLY, 2);
			reply.AddShort(PRIEST_PARTNER_NOT_PRESENT);
			character->Send(reply);
			return;
		}

		if (partner->fiance != character->SourceName())
		{
			PacketBuilder reply(PACKET_PRIEST, PACKET_REPLY, 2);
			reply.AddShort(PRIEST_NO_PERMISSION);
			character->Send(reply);
			return;
		}

		if (!partner->partner.empty())
		{
			PacketBuilder reply(PACKET_PRIEST, PACKET_REPLY, 2);
			reply.AddShort(PRIEST_PARTNER_ALREADY_MARRIED);
			character->Send(reply);
			return;
		}


		if (!is_dressed(character))
		{
			PacketBuilder reply(PACKET_PRIEST, PACKET_REPLY, 2);
			reply.AddShort(PRIEST_NOT_DRESSED);
			character->Send(reply);
			return;
		}

		if (!is_dressed(partner))
		{
			PacketBuilder reply(PACKET_PRIEST, PACKET_REPLY, 2);
			reply.AddShort(PRIEST_PARTNER_NOT_DRESSED);
			character->Send(reply);
			return;
		}

		if (character->level < util::to_int(character->world->config["WeddingMinLevel"]))
		{
			PacketBuilder reply;
			reply.SetID(PACKET_PRIEST, PACKET_REPLY);
			reply.AddChar(PRIEST_LOW_LEVEL);
			character->Send(reply);
			return;
		}

		character->map->wedding->RequestWedding(character);

		PacketBuilder builder(PACKET_PRIEST, PACKET_REQUEST, 2 + character->SourceName().length());
		builder.AddShort(1);
		builder.AddString(character->SourceName());
		partner->Send(builder);
	}
}

// Saying "I do" at a wedding
void Priest_Use(Character *character, PacketReader &reader)
{
	/*int session_id = */reader.GetInt();

	if (!character->map->wedding)
		return;

	character->map->wedding->IDo(character);
}

PACKET_HANDLER_REGISTER(PACKET_PRIEST)
	Register(PACKET_ACCEPT, Priest_Accept, Playing);
	Register(PACKET_OPEN, Priest_Open, Playing);
	Register(PACKET_REQUEST, Priest_Request, Playing);
	Register(PACKET_USE, Priest_Use, Playing);
PACKET_HANDLER_REGISTER_END(PACKET_PRIEST)

}
