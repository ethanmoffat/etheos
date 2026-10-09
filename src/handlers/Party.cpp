
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../map.hpp"
#include "../party.hpp"
#include "../world.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

namespace Handlers
{

// Invite/Join request
void Party_Request(Character* character, const client::PartyRequestClientPacket& packet)
{
	const PartyRequestType type = packet.request_type;

	if (type != PartyRequestType::Join && type != PartyRequestType::Invite)
	{
		return;
	}

	Character *invitee = character->map->GetCharacterPID(packet.player_id);

	if (!invitee || character == invitee || !character->InRange(invitee))
		return;

	server::PartyRequestServerPacket request;
	request.request_type = type;
	request.inviter_player_id = character->PlayerID();
	request.player_name = character->SourceName();
	invitee->Send(request);

	character->party_trust_send = invitee;
	invitee->party_trust_recv = character;
	character->party_send_type = type;
}

// Accept invite/join request
void Party_Accept(Character* character, const client::PartyAcceptClientPacket& packet)
{
	Character *inviter = character->map->GetCharacterPID(packet.inviter_player_id);

	if (!inviter || !character->InRange(inviter)
	 || inviter->party_trust_send != character
	 || character->party_trust_recv != inviter)
	{
		return;
	}

	if (inviter->party_send_type == PartyRequestType::Join)
	{
		if (inviter->party)
		{
			inviter->party->Leave(inviter);
		}

		if (!character->party)
		{
			new Party(character->world, character, inviter);
		}
		else
		{
			character->party->Join(inviter);
		}
	}
	else if (inviter->party_send_type == PartyRequestType::Invite)
	{
		if (character->party)
		{
			character->party->Leave(character);
		}

		if (!inviter->party)
		{
			new Party(character->world, inviter, character);
		}
		else
		{
			inviter->party->Join(character);
		}
	}

	character->party_trust_recv = 0;
	inviter->party_trust_send = 0;
}

// Remove a player from a party
void Party_Remove(Character* character, const client::PartyRemoveClientPacket& packet)
{
	if (!character->party) return;

	const unsigned int id = packet.player_id;

	if (id == character->PlayerID() || character == character->party->leader)
	{
		Character *leaver = character->world->GetCharacterPID(id);

		if (!leaver)
			return;

		character->party->Leave(leaver);
	}
	else
	{
		return;
	}
}

// Requested list of party members
void Party_Take(Character* character, const client::PartyTakeClientPacket&)
{
	if (!character->party) return;

	character->party->RefreshMembers(character);
}

PACKET_HANDLER_REGISTER(Party)
	Register(Party_Request, Playing, 0.5);
	Register(Party_Accept, Playing);
	Register(Party_Remove, Playing);
	Register(Party_Take, Playing);
PACKET_HANDLER_REGISTER_END(Party)

}
