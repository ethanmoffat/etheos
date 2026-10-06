/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../eodata.hpp"
#include "../map.hpp"
#include "../world.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>
#include <vector>

namespace Handlers
{

static server::TradeItemData TradeItemDataFrom(const Character* character)
{
	server::TradeItemData data;
	data.player_id = character->PlayerID();
	data.items.reserve(character->trade_inventory.size());

	for (const Character_Item& item : character->trade_inventory)
	{
		eolib::protocol::net::Item& net_item = data.items.emplace_back();
		net_item.id = item.id;
		net_item.amount = item.amount;
	}

	return data;
}

// Trade_Admin tells a player who had already agreed that the items changed
static void SendTradeItems(Character* recipient, const std::vector<server::TradeItemData>& trade_data)
{
	if (recipient->trade_agree)
	{
		server::TradeAdminServerPacket admin;
		admin.trade_data = trade_data;
		recipient->Send(admin);
		return;
	}

	server::TradeReplyServerPacket reply;
	reply.trade_data = trade_data;
	recipient->Send(reply);
}

static void SendTradeUpdate(Character* character)
{
	Character* partner = character->trade_partner;
	const std::vector<server::TradeItemData> trade_data = {TradeItemDataFrom(character), TradeItemDataFrom(partner)};

	SendTradeItems(character, trade_data);
	SendTradeItems(partner, trade_data);

	character->trade_agree = false;
	partner->trade_agree = false;
}

static void SendAgreement(Character* character, bool agree)
{
	server::TradeSpecServerPacket reply;
	reply.agree = agree;
	character->Send(reply);

	server::TradeAgreeServerPacket partner_agree;
	partner_agree.partner_player_id = character->trade_partner->PlayerID();
	partner_agree.agree = agree;
	character->trade_partner->Send(partner_agree);
}

// Requesting a trade with another player
void Trade_Request(Character* character, const client::TradeRequestClientPacket& packet)
{
	if (packet.request_type != client::TradeRequestClientPacket::DEFAULT_REQUEST_TYPE) return;
	if (character->trading) return;
	if (!character->CanInteractItems()) return;
	if (character->mapid == int(character->world->config["JailMap"])) return;

	Character* victim = character->map->GetCharacterPID(packet.player_id);

	if (!victim || victim == character)
		return;

	if (victim->trading || !victim->CanInteractItems())
		return;

	server::TradeRequestServerPacket request;
	request.partner_player_id = character->PlayerID();
	request.partner_player_name = character->SourceName();
	victim->Send(request);

	character->trade_partner = victim;
}

// Accepting a trade request
void Trade_Accept(Character* character, const client::TradeAcceptClientPacket& packet)
{
	if (packet.request_type != client::TradeAcceptClientPacket::DEFAULT_REQUEST_TYPE) return;
	if (character->trading) return;
	if (!character->CanInteractItems()) return;
	if (character->mapid == int(character->world->config["JailMap"])) return;

	Character* victim = character->map->GetCharacterPID(packet.player_id);

	if (!victim || victim->mapid != character->mapid || victim->trade_partner != character || victim->trading || !victim->CanInteractItems())
	{
		return;
	}

	server::TradeOpenServerPacket open;
	open.partner_player_id = victim->PlayerID();
	open.partner_player_name = victim->SourceName();
	open.your_player_id = character->PlayerID();
	open.your_player_name = character->SourceName();
	character->Send(open);

	std::swap(open.partner_player_id, open.your_player_id);
	std::swap(open.partner_player_name, open.your_player_name);
	victim->Send(open);

	character->trade_partner = victim;
	character->trading = true;
	character->trade_agree = false;
	victim->trading = true;
	victim->trade_agree = false;
}

// Remove an item from the trade screen
void Trade_Remove(Character* character, const client::TradeRemoveClientPacket& packet)
{
	if (!character->trading || character->trade_agree) return;

	if (character->trade_inventory.size() > 5)
		return;

	if (!character->DelTradeItem(packet.item_id))
		return;

	SendTradeUpdate(character);
}

// Mark your (dis)agreeance with the current trade
void Trade_Agree(Character* character, const client::TradeAgreeClientPacket& packet)
{
	if (!character->trading) return;

	character->trade_agree = packet.agree;

	if (!packet.agree)
	{
		SendAgreement(character, false);
		return;
	}

	if (character->trade_inventory.empty())
		return;

	Character* partner = character->trade_partner;

	if (!partner->trade_agree)
	{
		SendAgreement(character, true);
		return;
	}

	server::TradeUseServerPacket use;
	use.trade_data = {TradeItemDataFrom(partner), TradeItemDataFrom(character)};

	for (const Character_Item& item : partner->trade_inventory)
	{
		partner->DelItem(item.id, item.amount);
		character->AddItem(item.id, item.amount);
	}

	for (const Character_Item& item : character->trade_inventory)
	{
		character->DelItem(item.id, item.amount);
		partner->AddItem(item.id, item.amount);
	}

	character->Send(use);
	partner->Send(use);

	character->Emote(Emote::Trade);
	partner->Emote(Emote::Trade);

	character->trading = false;
	character->trade_inventory.clear();
	character->trade_agree = false;

	character->CheckQuestRules();
	partner->CheckQuestRules();

	partner->trading = false;
	partner->trade_inventory.clear();
	character->trade_agree = false;

	partner->trade_partner = nullptr;
	character->trade_partner = nullptr;
}

// Add an item to the trade screen
void Trade_Add(Character* character, const client::TradeAddClientPacket& packet)
{
	if (!character->trading) return;

	const int item_id = packet.add_item.id;

	if (character->world->eif->Get(item_id).special == pub::ItemSpecial::Lore)
		return;

	const bool offered = std::any_of(character->trade_inventory.begin(), character->trade_inventory.end(),
		[&](const Character_Item& item) { return item.id == item_id; });

	if (!offered && character->trade_inventory.size() >= 10)
		return;

	if (!character->AddTradeItem(item_id, packet.add_item.amount))
		return;

	SendTradeUpdate(character);
}

// Cancel the trade
void Trade_Close(Character* character, const client::TradeCloseClientPacket&)
{
	if (!character->trading) return;

	server::TradeCloseServerPacket close;
	close.partner_player_id = character->PlayerID();
	character->trade_partner->Send(close);

	character->trading = false;
	character->trade_inventory.clear();
	character->trade_agree = false;

	character->trade_partner->trading = false;
	character->trade_partner->trade_inventory.clear();
	character->trade_agree = false;

	character->CheckQuestRules();
	character->trade_partner->CheckQuestRules();

	character->trade_partner->trade_partner = nullptr;
	character->trade_partner = nullptr;
}

PACKET_HANDLER_REGISTER(PACKET_TRADE)
	Register(Trade_Request, Playing, 0.5);
	Register(Trade_Accept, Playing);
	Register(Trade_Remove, Playing);
	Register(Trade_Agree, Playing);
	Register(Trade_Add, Playing);
	Register(Trade_Close, Playing);
PACKET_HANDLER_REGISTER_END(PACKET_TRADE)

}
