/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../converters.hpp"
#include "../eodata.hpp"
#include "../map.hpp"
#include "../world.hpp"

#include <eolib/data/eo_numeric_limits.hpp>
#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>

namespace Handlers
{

static void SendEquipmentChangeIfVisible(Character* character, const server::AvatarChange& before)
{
	server::AvatarAgreeServerPacket avatar_agree;
	avatar_agree.change = Converters::EquipmentChangeFrom(character);

	if (avatar_agree.change != before)
	{
		character->map->SendToNearby(character, avatar_agree, false);
	}
}

// Request for currently equipped items
void Paperdoll_Request(Character* character, const client::PaperdollRequestClientPacket& packet)
{
	Character* target = character->map->GetCharacterPID(packet.player_id);

	if (!target)
		return;

	server::PaperdollReplyServerPacket reply;
	reply.details = Converters::CharacterDetailsFrom(target);
	reply.equipment = Converters::EquipmentPaperdollFrom(target);
	reply.icon = Converters::CharacterIconFrom(target);
	character->Send(reply);
}

// Unequipping an item
void Paperdoll_Remove(Character* character, const client::PaperdollRemoveClientPacket& packet)
{
	if (character->world->eif->Get(packet.item_id).special == pub::ItemSpecial::Cursed)
	{
		return;
	}

	const server::AvatarChange before = Converters::EquipmentChangeFrom(character);

	if (character->Unequip(packet.item_id, packet.sub_loc))
	{
		server::PaperdollRemoveServerPacket reply;
		reply.change = Converters::EquipmentChangeFrom(character);
		reply.item_id = packet.item_id;
		reply.sub_loc = packet.sub_loc;
		reply.stats = Converters::CharacterStatsEquipmentChangeFrom(character);
		character->Send(reply);
	}

	SendEquipmentChangeIfVisible(character, before);
}

// Equipping an item
void Paperdoll_Add(Character* character, const client::PaperdollAddClientPacket& packet)
{
	if (character->trading) return;

	const server::AvatarChange before = Converters::EquipmentChangeFrom(character);

	if (character->Equip(packet.item_id, packet.sub_loc))
	{
		server::PaperdollAgreeServerPacket reply;
		reply.change = Converters::EquipmentChangeFrom(character);
		reply.item_id = packet.item_id;
		// The client can't be told about more than a three's worth of the item
		reply.remaining_amount = std::min(character->HasItem(packet.item_id), static_cast<int>(eolib::data::EoNumericLimits::ThreeMax) - 1);
		reply.sub_loc = packet.sub_loc;
		reply.stats = Converters::CharacterStatsEquipmentChangeFrom(character);
		character->Send(reply);
	}

	SendEquipmentChangeIfVisible(character, before);
}

PACKET_HANDLER_REGISTER(Paperdoll)
	Register(Paperdoll_Request, Playing);
	Register(Paperdoll_Remove, Playing);
	Register(Paperdoll_Add, Playing);
PACKET_HANDLER_REGISTER_END(Paperdoll)

}
