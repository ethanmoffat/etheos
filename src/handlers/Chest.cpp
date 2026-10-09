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
#include <memory>

namespace Handlers
{

static std::shared_ptr<Map_Chest> TryGetChest(Character* character, const eolib::protocol::Coords& coords)
{
	if (util::path_length(character->loc, coords) > 1)
		return nullptr;

	if (character->map->GetSpec(coords) != emf::MapTileSpec::Chest)
		return nullptr;

	auto& chests = character->map->chests;
	auto it = std::find_if(chests.begin(), chests.end(), [&](const std::shared_ptr<Map_Chest>& chest) { return chest->loc == coords; });
	return it != chests.end() ? *it : nullptr;
}

void Chest_Add(Character* character, const client::ChestAddClientPacket& packet)
{
	if (character->trading) return;
	if (!character->CanInteractItems()) return;

	const int id = packet.add_item.id;

	if (character->world->eif->Get(id).special == pub::ItemSpecial::Lore)
		return;

	std::shared_ptr<Map_Chest> chest = TryGetChest(character, packet.coords);

	if (!chest)
		return;

	const int amount = std::min(packet.add_item.amount, int(character->world->config["MaxChest"]) - chest->HasItem(id));

	if (character->HasItem(id) < amount || !chest->AddItem(id, amount))
		return;

	character->DelItem(id, amount);
	chest->Update(character->map, character);

	server::ChestReplyServerPacket reply;
	reply.added_item_id = id;
	reply.remaining_amount = character->HasItem(id);
	reply.weight = Converters::WeightFrom(character);
	reply.items = Converters::ChestItemsFrom(*chest);
	character->Send(reply);
}

// Taking an item from a chest
void Chest_Take(Character* character, const client::ChestTakeClientPacket& packet)
{
	std::shared_ptr<Map_Chest> chest = TryGetChest(character, packet.coords);

	if (!chest)
		return;

	const int id = packet.take_item_id;
	const int taken = character->CanHoldItem(id, chest->HasItem(id));

	if (taken <= 0)
		return;

	chest->DelSomeItem(id, taken);
	character->AddItem(id, taken);

	server::ChestGetServerPacket reply;
	reply.taken_item.id = id;
	reply.taken_item.amount = taken;
	reply.weight = Converters::WeightFrom(character);
	reply.items = Converters::ChestItemsFrom(*chest);
	character->Send(reply);

	chest->Update(character->map, character);
}

// Opening a chest
void Chest_Open(Character* character, const client::ChestOpenClientPacket& packet)
{
	if (util::path_length(character->loc, packet.coords) > 1)
		return;

	std::shared_ptr<Map_Chest> chest = TryGetChest(character, packet.coords);

	// A Chest_Close without a key tells the client the chest is broken
	if (!chest)
	{
		character->Send(server::ChestCloseServerPacket());
		return;
	}

	if (chest->key > 0 && !character->HasKey(chest->key))
	{
		server::ChestCloseServerPacket close;

		// Thanks daddy Sausage
		// https://discord.com/channels/723989119503696013/787685796055482368/1046490342834315325
		// A key that no item opens is reported as a broken chest, since the client can't name the key
		if (character->world->eif->GetKey(chest->key))
			close.key = chest->key;

		character->Send(close);
		return;
	}

	server::ChestOpenServerPacket reply;
	reply.coords = packet.coords;
	reply.items = Converters::ChestItemsFrom(*chest);
	character->Send(reply);
}

PACKET_HANDLER_REGISTER(Chest)
	Register(Chest_Add, Playing);
	Register(Chest_Take, Playing);
	Register(Chest_Open, Playing);
PACKET_HANDLER_REGISTER_END(Chest)

}
