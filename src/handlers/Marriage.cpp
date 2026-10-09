
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
#include "../world.hpp"

#include "../util.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>
#include <string>

namespace Handlers
{

static server::MarriageReplyServerPacket::ReplyCodeDataSuccess NewSuccessData(int characterGold)
{
	server::MarriageReplyServerPacket::ReplyCodeDataSuccess success;
	success.gold_amount = characterGold;
	return success;
}

// Talking to a law bob NPC
void Marriage_Open(Character* character, const client::MarriageOpenClientPacket& packet)
{
	const auto match = std::find_if(
		character->map->npcs.begin(),
		character->map->npcs.end(),
		[&packet](const NPC* npc) { return npc->index == packet.npc_index && npc->PubData().type == pub::NpcType::Lawyer; });

	if (match == character->map->npcs.end())
		return;

	character->npc = *match;
	character->npc_type = pub::NpcType::Lawyer;

	server::MarriageOpenServerPacket reply;
	reply.session_id = character->NewNpcSession();
	character->Send(reply);
}

// Requesting marriage approval
void Marriage_Request(Character* character, const client::MarriageRequestClientPacket& packet)
{
	if (character->npc_type != pub::NpcType::Lawyer)
		return;

	if (character->npc_session_id != packet.session_id)
		return;

	const std::string name = util::lowercase(packet.name);

	switch (packet.request_type)
	{
		case client::MarriageRequestType::MarriageApproval:
		{
			if (!character->partner.empty())
			{
				character->Send(server::MarriageReplyServerPacket::ForAlreadyMarried());
				return;
			}

			int marriage_price = character->world->config["MarriagePrice"];

			if (!character->HasItem(1, marriage_price))
			{
				character->Send(server::MarriageReplyServerPacket::ForNotEnoughGold());
				return;
			}

			if (Character::ValidName(name))
			{
				character->DelItem(1, marriage_price);
				character->fiance = name;
				character->Send(
					server::MarriageReplyServerPacket::ForSuccess(
						NewSuccessData(character->HasItem(1))
					)
				);
			}

			break;
		}

		case client::MarriageRequestType::Divorce:
		{
			if (character->partner.empty())
			{
				character->Send(server::MarriageReplyServerPacket::ForNotMarried());
				return;
			}

			int divorce_price = character->world->config["DivorcePrice"];

			if (!character->HasItem(1, divorce_price))
			{
				character->Send(server::MarriageReplyServerPacket::ForNotEnoughGold());
				return;
			}

			if (character->partner != name)
			{
				character->Send(server::MarriageReplyServerPacket::ForWrongName());
				return;
			}

			Character* partner = character->world->GetCharacter(name);

			if (partner)
			{
				partner->partner.clear();

				partner->Send(server::MarriageReplyServerPacket::ForDivorceNotification());
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
			character->Send(
				server::MarriageReplyServerPacket::ForSuccess(
					NewSuccessData(character->HasItem(1))
				)
			);
			break;
		}
	}
}

PACKET_HANDLER_REGISTER(Marriage)
	Register(Marriage_Open, Playing);
	Register(Marriage_Request, Playing);
PACKET_HANDLER_REGISTER_END(Marriage)

}
