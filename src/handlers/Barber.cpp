
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../eodata.hpp"
#include "../map.hpp"
#include "../npc.hpp"
#include "../world.hpp"

#include "../util.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>

namespace Handlers
{

// Talked to a barber NPC
void Barber_Open(Character* character, const client::BarberOpenClientPacket& packet)
{
	const auto match = std::find_if(
		character->map->npcs.begin(),
		character->map->npcs.end(),
		[&packet](const NPC* npc) { return npc->index == packet.npc_index && npc->PubData().type == pub::NpcType::Barber; });

	if (match == character->map->npcs.end())
		return;

	character->npc = *match;
	character->npc_type = pub::NpcType::Barber;

	server::BarberOpenServerPacket reply;
	reply.session_id = character->NewNpcSession();
	character->Send(reply);
}

// Purchased a hair style
void Barber_Buy(Character* character, const client::BarberBuyClientPacket& packet)
{
	if (character->trading) return;

	if (packet.hair_style > static_cast<int>(character->world->config["MaxHairStyle"])
	 || packet.hair_color > static_cast<int>(character->world->config["MaxHairColor"]))
	{
		return;
	}

	if (character->npc_type != pub::NpcType::Barber)
		return;

	if (character->npc_session_id != packet.session_id)
		return;

	int price = character->world->config["BarberBase"];
	price += std::max(1, int(character->level)) * static_cast<int>(character->world->config["BarberStep"]);

	if (character->HasItem(1) < price)
		return;

	character->DelItem(1, price);

	character->hairstyle = packet.hair_style;
	character->haircolor = packet.hair_color;

	server::AvatarChange::ChangeTypeDataHair hair;
	hair.hair_style = packet.hair_style;
	hair.hair_color = packet.hair_color;

	server::AvatarAgreeServerPacket avatar_agree;
	avatar_agree.change = server::AvatarChange::ForHair(hair);
	avatar_agree.change.player_id = character->PlayerID();

	character->map->SendToNearby(character, avatar_agree, false);

	server::BarberAgreeServerPacket reply;
	reply.gold_amount = character->HasItem(1);
	reply.change = avatar_agree.change;
	character->Send(reply);
}

PACKET_HANDLER_REGISTER(PACKET_BARBER)
	Register(Barber_Open, Playing);
	Register(Barber_Buy, Playing);
PACKET_HANDLER_REGISTER_END(PACKET_BARBER)

}
