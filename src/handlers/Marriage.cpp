
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
#include "../world.hpp"

#include "../util.hpp"

namespace Handlers
{

// Talking to a law bob NPC
void Marriage_Open(Character *character, PacketReader &reader)
{
	short id = reader.GetShort();

	UTIL_FOREACH(character->map->npcs, npc)
	{
		if (npc->index == id && (npc->ENF().type == ENF::Law))
		{
			character->npc = npc;
			character->npc_type = ENF::Law;

			PacketBuilder reply(PACKET_MARRIAGE, PACKET_OPEN, 3);
			reply.AddThree(npc->id);

			character->Send(reply);

			break;
		}
	}
}

enum MarriageRequestType : unsigned char
{
	MarriageApproval = 1,
	Divorce = 2,
};

// Requesting marriage approval
void Marriage_Request(Character *character, PacketReader &reader)
{
	MarriageRequestType request_type = static_cast<MarriageRequestType>(reader.GetChar());
	/*int session_id = */reader.GetInt();
	reader.GetByte();
	std::string name = util::lowercase(reader.GetEndString());

	if (character->npc_type != ENF::Law)
		return;

	switch (request_type)
	{
		case MarriageApproval:
		{
			if (!character->partner.empty())
			{
				PacketBuilder builder(PACKET_MARRIAGE, PACKET_REPLY, 2);
				builder.AddShort(MARRIAGE_ALREADY_HAVE_PARTNER);
				character->Send(builder);
				return;
			}

			int marriage_price = character->world->config["MarriagePrice"];

			if (!character->HasItem(1, marriage_price))
			{
				PacketBuilder builder(PACKET_MARRIAGE, PACKET_REPLY, 2);
				builder.AddShort(MARRIAGE_NOT_ENOUGH_GP);
				character->Send(builder);
				return;
			}

			if (Character::ValidName(name))
			{
				character->DelItem(1, marriage_price);

				character->fiance = name;

				PacketBuilder builder(PACKET_MARRIAGE, PACKET_REPLY, 6);
				builder.AddShort(MARRIAGE_SUCCESS);
				builder.AddInt(character->HasItem(1));
				character->Send(builder);
			}

			break;
		}

		case Divorce:
		{
			if (character->partner.empty())
			{
				PacketBuilder builder(PACKET_MARRIAGE, PACKET_REPLY, 2);
				builder.AddShort(MARRIAGE_DIVORCE_NOT_MARRIED);
				character->Send(builder);
				return;
			}

			int divorce_price = character->world->config["DivorcePrice"];

			if (!character->HasItem(1, divorce_price))
			{
				PacketBuilder builder(PACKET_MARRIAGE, PACKET_REPLY, 2);
				builder.AddShort(MARRIAGE_NOT_ENOUGH_GP);
				character->Send(builder);
				return;
			}

			if (character->partner != name)
			{
				PacketBuilder builder(PACKET_MARRIAGE, PACKET_REPLY, 2);
				builder.AddShort(MARRIAGE_DIVORCE_WRONG_NAME);
				character->Send(builder);
				return;
			}

			Character* partner = character->world->GetCharacter(name);

			if (partner)
			{
				partner->partner.clear();

				PacketBuilder builder(PACKET_MARRIAGE, PACKET_REPLY, 2);
				builder.AddShort(MARRIAGE_DIVORCE_NOTIFICATION);
				partner->Send(builder);
			}
			else
			{
				character->world->db->Query(
					"UPDATE `characters` SET `partner` = '' WHERE `name` = '$' AND partner = '$'",
					name.c_str(),
					character->SourceName().c_str()
				);
			}

			character->partner.clear();

			character->DelItem(1, divorce_price);

			PacketBuilder builder(PACKET_MARRIAGE, PACKET_REPLY, 6);
			builder.AddShort(MARRIAGE_SUCCESS);
			builder.AddInt(character->HasItem(1));
			character->Send(builder);
			break;
		}
	}
}

PACKET_HANDLER_REGISTER(PACKET_MARRIAGE)
	Register(PACKET_OPEN, Marriage_Open, Playing);
	Register(PACKET_REQUEST, Marriage_Request, Playing);
PACKET_HANDLER_REGISTER_END(PACKET_MARRIAGE)

}
