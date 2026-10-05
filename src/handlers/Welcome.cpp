
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../converters.hpp"
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
#include <vector>

namespace Handlers
{

static std::vector<std::string> ReadNews(const std::string& news_file)
{
	std::vector<std::string> news;
	news.reserve(Converters::NewsLineCount);

	std::ifstream file(news_file);

	if (!file)
	{
		Console::Wrn("Could not load news file '%s'", news_file.c_str());
	}

	std::string line;

	while (news.size() < Converters::NewsLineCount && std::getline(file, line))
	{
		std::string str = util::trim(line);

		// The client ignores news lines shorter than 2 characters
		while (str.length() < 2)
			str += ' ';

		news.push_back(std::move(str));
	}

	return news;
}

// Selected a character
void Welcome_Request(Player* player, const client::WelcomeRequestClientPacket& packet)
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

	const bool login_warning = player->character->usage == 0;
	player->Send(server::WelcomeReplyServerPacket::ForSelectCharacter(
		Converters::SelectCharacterFrom(player->character, login_warning)));
}

// Welcome message after you login.
void Welcome_Msg(Player* player, const client::WelcomeMsgClientPacket& packet)
{
	if (packet.session_id != player->id || player->character == nullptr || player->character->id != static_cast<unsigned int>(packet.character_id))
	{
		player->client->Send(server::WelcomeReplyServerPacket::ForServerBusy());
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

	server::NearbyInfo nearby;

	UTIL_FOREACH(player->character->map->characters, character)
	{
		if (player->character->InRange(character))
		{
			nearby.characters.push_back(Converters::CharacterMapInfoFrom(character));
		}
	}

	UTIL_FOREACH(player->character->map->npcs, npc)
	{
		if (npc->alive && player->character->InRange(npc))
		{
			nearby.npcs.push_back(Converters::NpcMapInfoFrom(npc));
		}
	}

	UTIL_FOREACH(player->character->map->items, item)
	{
		if (player->character->InRange(*item))
		{
			nearby.items.push_back(Converters::ItemMapInfoFrom(*item));
		}
	}

	auto news = ReadNews(player->world->config["NewsFile"]);
	player->Send(server::WelcomeReplyServerPacket::ForEnterGame(
		Converters::EnterGameFrom(player->character, std::move(news), std::move(nearby))));
}

// Client wants a file
void Welcome_Agree(Player* player, const client::WelcomeAgreeClientPacket& packet)
{
	if (packet.session_id != player->id)
	{
		player->client->Close();
		return;
	}

	bool result = false;

	switch (packet.file_type)
	{
		case client::FileType::Emf:
			result = player->client->UploadMap(packet.AsEmf()->file_id, server::InitReply::FileEmf);
			break;
		case client::FileType::Eif:
		case client::FileType::Enf:
		case client::FileType::Esf:
		case client::FileType::Ecf:
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
