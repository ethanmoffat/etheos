
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../eoclient.hpp"
#include "../map.hpp"
#include "../npc.hpp"
#include "../packet.hpp"
#include "../player.hpp"
#include "../world.hpp"

#include "../console.hpp"
#include "../util.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/net/server/structs.hpp>

#include <algorithm>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace Handlers
{

static constexpr std::size_t news_line_count = 9;

static std::vector<std::string> ReadNews(const std::string& news_file)
{
	std::vector<std::string> news;
	news.reserve(news_line_count);

	std::ifstream file(news_file);

	if (!file)
	{
		Console::Wrn("Could not load news file '%s'", news_file.c_str());
	}

	std::string line;

	while (news.size() < news_line_count && std::getline(file, line))
	{
		std::string str = util::trim(line);

		// The client ignores news lines shorter than 2 characters
		while (str.length() < 2)
			str += ' ';

		news.push_back(std::move(str));
	}

	news.resize(news_line_count);

	return news;
}

// Selected a character
void Welcome_Request(Player* player, const net::client::WelcomeRequestClientPacket& packet)
{
	if (player->character)
		throw std::runtime_error("Character already selected");

	auto it = std::find_if(UTIL_CRANGE(player->characters), [&](Character* c) -> bool
	{
		return c->id == static_cast<unsigned int>(packet.character_id);
	});

	if (it == player->characters.end())
		return;

	player->character = *it;
	player->character->CalculateStats();

	player->Send(player->character->SelectCharacterReply(player->character->usage == 0));
}

// Welcome message after you login.
void Welcome_Msg(Player* player, const net::client::WelcomeMsgClientPacket& packet)
{
	if (packet.session_id != player->id || player->character == nullptr || player->character->id != static_cast<unsigned int>(packet.character_id))
	{
		net::server::WelcomeReplyServerPacket reply;
		reply.welcome_code = net::server::WelcomeCode::ServerBusy;
		player->client->Send(reply);
		player->client->Close();
		return;
	}

	if (!player->character->world->GetMap(player->character->mapid)->exists)
	{
		if (player->character->world->GetMap(player->character->SpawnMap())->exists)
		{
			Console::Wrn("Player logged in to non-existent map (%s, map %i) - Position reset", player->character->real_name.c_str(), player->character->mapid);
			player->character->Warp(player->character->SpawnMap(), player->character->SpawnX(), player->character->SpawnY());
		}
		else
		{
			Console::Wrn("Player logged in to non-existent map (%s, map %i) - Disconnected", player->character->real_name.c_str(), player->character->mapid);
			player->client->Close();
			return;
		}
	}

	player->world->Login(player->character);

	player->client->state = EOClient::Playing;

	net::server::WelcomeReplyServerPacket::WelcomeCodeDataEnterGame enter_game;
	enter_game.news = ReadNews(player->world->config["NewsFile"]);
	enter_game.weight.current = player->character->weight;
	enter_game.weight.max = player->character->maxweight;

	UTIL_FOREACH(player->character->inventory, item)
	{
		net::Item net_item;
		net_item.id = item.id;
		net_item.amount = item.amount;
		enter_game.items.push_back(net_item);
	}

	UTIL_FOREACH(player->character->spells, spell)
	{
		net::Spell net_spell;
		net_spell.id = spell.id;
		net_spell.level = spell.level;
		enter_game.spells.push_back(net_spell);
	}

	UTIL_FOREACH(player->character->map->characters, character)
	{
		if (player->character->InRange(character))
		{
			enter_game.nearby.characters.push_back(character->MapInfo());
		}
	}

	UTIL_FOREACH(player->character->map->npcs, npc)
	{
		if (npc->alive && player->character->InRange(npc))
		{
			net::server::NpcMapInfo npc_info;
			npc_info.index = npc->index;
			npc_info.id = npc->id;
			npc_info.coords.x = npc->x;
			npc_info.coords.y = npc->y;
			npc_info.direction = static_cast<eolib::protocol::Direction>(npc->direction);
			enter_game.nearby.npcs.push_back(npc_info);
		}
	}

	UTIL_FOREACH(player->character->map->items, item)
	{
		if (player->character->InRange(*item))
		{
			net::server::ItemMapInfo item_info;
			item_info.uid = item->uid;
			item_info.id = item->id;
			item_info.coords.x = item->x;
			item_info.coords.y = item->y;
			item_info.amount = item->amount;
			enter_game.nearby.items.push_back(item_info);
		}
	}

	net::server::WelcomeReplyServerPacket reply;
	reply.welcome_code = net::server::WelcomeCode::EnterGame;
	reply.welcome_code_data = std::move(enter_game);
	player->Send(reply);
}

// Client wants a file
void Welcome_Agree(Player* player, const net::client::WelcomeAgreeClientPacket& packet)
{
	if (packet.session_id != player->id)
	{
		player->client->Close();
		return;
	}

	bool result = false;

	switch (packet.file_type)
	{
		case net::client::FileType::Emf:
		{
			const auto& data = std::get<net::client::WelcomeAgreeClientPacket::FileTypeDataEmf>(packet.file_type_data);
			result = player->client->UploadMap(data.file_id, net::server::InitReply::FileEmf);
			break;
		}
		case net::client::FileType::Eif:
		case net::client::FileType::Enf:
		case net::client::FileType::Esf:
		case net::client::FileType::Ecf:
			result = player->client->UploadPubFile(packet.file_type); break;
		default: return;
	}

	if (!result)
		throw std::runtime_error("Failed to upload file");
}

PACKET_HANDLER_REGISTER(PACKET_WELCOME)
	Register(Welcome_Request, Character_Menu);
	Register(Welcome_Msg, Logging_In);
	Register(Welcome_Agree, Logging_In, 0.15);
PACKET_HANDLER_REGISTER_END(PACKET_WELCOME)

}
