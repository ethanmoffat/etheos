/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../converters.hpp"
#include "../eodata.hpp"
#include "../map.hpp"
#include "../world.hpp"

#include "../util.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>
#include <cstddef>

namespace Handlers
{

static bool LockerInReach(Character* character, const eolib::protocol::Coords& coords)
{
	return util::path_length(character->loc, coords) <= 1 && character->map->GetSpec(coords) == emf::MapTileSpec::BankVault;
}

static server::LockerReplyServerPacket MakeLockerReply(Character* character, int item_id)
{
	server::LockerReplyServerPacket reply;
	reply.deposited_item.id = item_id;
	reply.deposited_item.amount = character->HasItem(item_id);
	reply.weight = Converters::WeightFrom(character);
	reply.locker_items = Converters::LockerItemsFrom(character);
	return reply;
}

// Placing an item in a bank locker
void Locker_Add(Character* character, const client::LockerAddClientPacket& packet)
{
	if (character->trading) return;

	const int item_id = packet.deposit_item.id;
	int amount = packet.deposit_item.amount;

	if (item_id == 1) return;
	if (amount <= 0) return;
	if (character->HasItem(item_id) < amount) return;

	if (!LockerInReach(character, packet.locker_coords))
		return;

	const int max_bank = static_cast<int>(character->world->config["MaxBank"]);

	auto it = std::find_if(character->bank.begin(), character->bank.end(), [&](const net::Item& item) { return item.id == item_id; });

	if (it != character->bank.end())
	{
		amount = std::min(amount, max_bank - it->amount);

		if (amount <= 0)
			return;

		it->amount += amount;

		character->DelItem(item_id, amount);
		character->CalculateStats();
		character->Send(MakeLockerReply(character, item_id));

		return;
	}

	const std::size_t locker_max = static_cast<int>(character->world->config["BaseBankSize"]) + character->bankmax * static_cast<int>(character->world->config["BankSizeStep"]);

	if (character->bank.size() >= locker_max)
	{
		server::LockerSpecServerPacket reply;
		reply.locker_max_items = static_cast<int>(locker_max);
		character->Send(reply);
		return;
	}

	amount = std::min(amount, max_bank);

	net::Item new_item;
	new_item.id = item_id;
	new_item.amount = amount;
	character->bank.push_back(new_item);

	character->DelItem(item_id, amount);
	character->CalculateStats();
	character->Send(MakeLockerReply(character, item_id));
}

// Taking an item from a bank locker
void Locker_Take(Character* character, const client::LockerTakeClientPacket& packet)
{
	if (character->trading) return;

	if (!LockerInReach(character, packet.locker_coords))
		return;

	const int item_id = packet.take_item_id;
	auto it = std::find_if(character->bank.begin(), character->bank.end(), [&](const net::Item& item) { return item.id == item_id; });

	if (it == character->bank.end())
		return;

	const int taken = character->CanHoldItem(item_id, it->amount);

	character->AddItem(item_id, taken);
	character->CalculateStats();

	it->amount -= taken;

	if (it->amount <= 0)
		character->bank.erase(it);

	server::LockerGetServerPacket reply;
	reply.taken_item.id = item_id;
	reply.taken_item.amount = taken;
	reply.weight = Converters::WeightFrom(character);
	reply.locker_items = Converters::LockerItemsFrom(character);
	character->Send(reply);
}

// Opening a bank locker
void Locker_Open(Character* character, const client::LockerOpenClientPacket& packet)
{
	if (!LockerInReach(character, packet.locker_coords))
		return;

	server::LockerOpenServerPacket reply;
	reply.locker_coords = packet.locker_coords;
	reply.locker_items = Converters::LockerItemsFrom(character);
	character->Send(reply);
}

// Purchasing a locker space upgrade
void Locker_Buy(Character* character, const client::LockerBuyClientPacket&)
{
	if (character->trading) return;

	if (character->npc_type != pub::NpcType::Bank)
		return;

	if (character->bankmax >= static_cast<int>(character->world->config["MaxBankUpgrades"]))
		return;

	const int cost = static_cast<int>(character->world->config["BankUpgradeBase"]) + character->bankmax * static_cast<int>(character->world->config["BankUpgradeStep"]);

	if (character->HasItem(1) < cost)
		return;

	++character->bankmax;
	character->DelItem(1, cost);

	server::LockerBuyServerPacket reply;
	reply.gold_amount = character->HasItem(1);
	reply.locker_upgrades = character->bankmax;
	character->Send(reply);
}

PACKET_HANDLER_REGISTER(Locker)
	Register(Locker_Add, Playing);
	Register(Locker_Take, Playing);
	Register(Locker_Open, Playing);
	Register(Locker_Buy, Playing);
PACKET_HANDLER_REGISTER_END(Locker)

}
