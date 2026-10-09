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
#include "../npc.hpp"
#include "../npc_data.hpp"
#include "../world.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>
#include <cstdint>

namespace Handlers
{

static bool IsShopSessionValid(Character* character, int session_id)
{
	return character->npc_type == pub::NpcType::Shop
	    && character->npc_session_id == session_id;
}

// Crafting an item
void Shop_Create(Character* character, const client::ShopCreateClientPacket& packet)
{
	if (character->trading) return;

	if (!IsShopSessionValid(character, packet.session_id))
		return;

	const auto& shop_craft = character->npc->Data().shop_craft;
	const auto match = std::find_if(shop_craft.begin(), shop_craft.end(),
		[&packet](const server::ShopCraftItem& craft) { return craft.item_id == packet.craft_item_id; });

	if (match == shop_craft.end())
		return;

	const bool has_ingredients = std::all_of(match->ingredients.begin(), match->ingredients.end(),
		[character](const eolib::protocol::net::CharItem& ingredient) { return character->HasItem(ingredient.id) >= ingredient.amount; });

	if (!has_ingredients)
		return;

	server::ShopCreateServerPacket reply;
	reply.craft_item_id = match->item_id;

	for (const eolib::protocol::net::CharItem& ingredient : match->ingredients)
	{
		character->DelItem(ingredient.id, ingredient.amount);

		eolib::protocol::net::Item& remaining = reply.ingredients.emplace_back();
		remaining.id = ingredient.id;
		remaining.amount = character->HasItem(ingredient.id);
	}

	character->AddItem(match->item_id, 1);
	reply.weight = Converters::WeightFrom(character);
	character->Send(reply);
}

// Purchasing an item from a store
void Shop_Buy(Character* character, const client::ShopBuyClientPacket& packet)
{
	if (character->trading) return;

	const int item_id = packet.buy_item.id;
	const int amount = packet.buy_item.amount;

	if (amount <= 0 || amount > static_cast<int>(character->world->config["MaxShopBuy"])) return;
	if (character->weight >= character->maxweight) return;

	// TODO: Limit number of items bought to under weight

	if (!IsShopSessionValid(character, packet.session_id))
		return;

	const auto& shop_trade = character->npc->Data().shop_trade;
	const auto match = std::find_if(shop_trade.begin(), shop_trade.end(),
		[item_id](const server::ShopTradeItem& trade) { return trade.item_id == item_id && trade.buy_price != 0; });

	if (match == shop_trade.end())
		return;

	const std::int_least64_t cost64 = std::int_least64_t(amount) * std::int_least64_t(match->buy_price);

	if (cost64 < 0 || cost64 > int(character->world->config["MaxItem"]))
		return;

	const int cost = int(cost64);

	if (character->HasItem(1) < cost)
		return;

	character->DelItem(1, cost);
	character->AddItem(item_id, amount);

	server::ShopBuyServerPacket reply;
	reply.gold_amount = character->HasItem(1);
	reply.bought_item.id = item_id;
	reply.bought_item.amount = amount;
	reply.weight = Converters::WeightFrom(character);
	character->Send(reply);
}

// Selling an item to a store
void Shop_Sell(Character* character, const client::ShopSellClientPacket& packet)
{
	if (character->trading) return;

	const int item_id = packet.sell_item.id;
	const int amount = packet.sell_item.amount;

	if (amount <= 0 || amount > static_cast<int>(character->world->config["MaxItem"])) return;

	if (!IsShopSessionValid(character, packet.session_id))
		return;

	const auto& shop_trade = character->npc->Data().shop_trade;
	const auto match = std::find_if(shop_trade.begin(), shop_trade.end(),
		[item_id](const server::ShopTradeItem& trade) { return trade.item_id == item_id && trade.sell_price != 0; });

	if (match == shop_trade.end())
		return;

	const std::int_least64_t owed64 = std::int_least64_t(amount) * std::int_least64_t(match->sell_price);

	if (owed64 < 0 || owed64 > (int(character->world->config["MaxItem"]) - character->HasItem(1)))
		return;

	const int owed = int(owed64);

	if (character->HasItem(item_id) < amount)
		return;

	character->DelItem(item_id, amount);
	character->AddItem(1, owed);

	server::ShopSellServerPacket reply;
	reply.sold_item.amount = character->HasItem(item_id);
	reply.sold_item.id = item_id;
	reply.gold_amount = character->HasItem(1);
	reply.weight = Converters::WeightFrom(character);
	character->Send(reply);
}

// Talking to a store NPC
void Shop_Open(Character* character, const client::ShopOpenClientPacket& packet)
{
	const auto match = std::find_if(
		character->map->npcs.begin(),
		character->map->npcs.end(),
		[&packet](const NPC* npc) { return npc->index == packet.npc_index && (!npc->Data().shop_trade.empty() || !npc->Data().shop_craft.empty()); });

	if (match == character->map->npcs.end())
		return;

	NPC* npc = *match;
	character->npc = npc;
	character->npc_type = pub::NpcType::Shop;

	server::ShopOpenServerPacket reply;
	reply.session_id = character->NewNpcSession();
	reply.shop_name = npc->Data().shop_name;
	reply.trade_items = npc->Data().shop_trade;
	reply.craft_items = npc->Data().shop_craft;
	character->Send(reply);
}

PACKET_HANDLER_REGISTER(Shop)
	Register(Shop_Create, Playing);
	Register(Shop_Buy, Playing);
	Register(Shop_Sell, Playing);
	Register(Shop_Open, Playing);
PACKET_HANDLER_REGISTER_END(Shop)

}
