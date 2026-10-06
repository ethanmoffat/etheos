
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../eoclient.hpp"
#include "../eodata.hpp"
#include "../map.hpp"
#include "../npc.hpp"
#include "../player.hpp"
#include "../world.hpp"

#include "../util.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>

namespace Handlers
{

static bool IsBankSessionValid(Character* character, int session_id)
{
	if (character->npc_type != pub::NpcType::Bank)
		return false;

	if (!character->world->CheckSession(character->npc->id, session_id))
	{
		character->player->client->Close();
		return false;
	}

	return true;
}

// Talked to a banker NPC
void Bank_Open(Character* character, const client::BankOpenClientPacket& packet)
{
	const auto match = std::find_if(
		character->map->npcs.begin(),
		character->map->npcs.end(),
		[&packet](const NPC* npc) { return npc->index == packet.npc_index && npc->PubData().type == pub::NpcType::Bank; });

	if (match == character->map->npcs.end())
		return;

	character->npc = *match;
	character->npc_type = pub::NpcType::Bank;

	server::BankOpenServerPacket reply;
	reply.gold_bank = character->goldbank;
	reply.session_id = character->npc->id;
	reply.locker_upgrades = character->bankmax;
	character->Send(reply);
}

// Depositing gold
void Bank_Add(Character* character, const client::BankAddClientPacket& packet)
{
	if (character->trading) return;

	if (packet.amount <= 0) return;

	if (!IsBankSessionValid(character, packet.session_id))
		return;

	const int amount = std::min(packet.amount, character->HasItem(1));

	if (amount > static_cast<int>(character->world->config["MaxBankGold"]) - character->goldbank)
		return;

	character->DelItem(1, amount);
	character->goldbank += amount;

	server::BankReplyServerPacket reply;
	reply.gold_inventory = character->HasItem(1);
	reply.gold_bank = character->goldbank;
	character->Send(reply);
}

// Withdrawing gold
void Bank_Take(Character* character, const client::BankTakeClientPacket& packet)
{
	if (packet.amount <= 0) return;

	if (!IsBankSessionValid(character, packet.session_id))
		return;

	if (packet.amount > character->goldbank)
		return;

	// AddItem caps the stack at MaxItem, so withdrawing past it would destroy the excess gold
	if (packet.amount > static_cast<int>(character->world->config["MaxItem"]) - character->HasItem(1))
		return;

	character->goldbank -= packet.amount;
	character->AddItem(1, packet.amount);

	server::BankReplyServerPacket reply;
	reply.gold_inventory = character->HasItem(1);
	reply.gold_bank = character->goldbank;
	character->Send(reply);
}

PACKET_HANDLER_REGISTER(PACKET_BANK)
	Register(Bank_Open, Playing);
	Register(Bank_Add, Playing);
	Register(Bank_Take, Playing);
PACKET_HANDLER_REGISTER_END(PACKET_BANK)

}
