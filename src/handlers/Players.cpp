
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../converters.hpp"
#include "../eoclient.hpp"
#include "../eoserver.hpp"
#include "../i18n.hpp"
#include "../player.hpp"
#include "../world.hpp"

#include "../util.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <string>
#include <utility>
#include <vector>

namespace Handlers
{

// User checking if a player is near (#find)
void Players_Accept(Character* character, const client::PlayersAcceptClientPacket& packet)
{
	Character* victim = character->world->GetCharacter(packet.name);

	if (!victim || victim->IsHideInvisible())
	{
		server::PlayersPingServerPacket reply;
		reply.name = packet.name;
		character->Send(reply);
		return;
	}

	if (victim->mapid == character->mapid && !victim->nowhere)
	{
		server::PlayersPongServerPacket reply;
		reply.name = packet.name;
		character->Send(reply);
		return;
	}

	server::PlayersNet242ServerPacket reply;
	reply.name = packet.name;
	character->Send(reply);
}

// Online characters that client may see. Admins with seehide also see characters hidden with HideOnline, unless
// they're protected by cmdprotect at an equal or higher level.
static std::vector<Character*> VisibleOnlineCharacters(EOClient* client)
{
	const Character* from_character = client->player ? client->player->character : nullptr;
	const protocol::AdminLevel seehide = client->server()->world->AdminLevelSetting("seehide");
	const protocol::AdminLevel cmdprotect = client->server()->world->AdminLevelSetting("cmdprotect");

	auto really_hidden = [&](const Character* character)
	{
		const bool has_character = from_character != nullptr;
		const bool is_match = has_character && from_character == character;
		const bool sees_through = has_character && from_character->admin >= seehide && (character->admin < cmdprotect || from_character->admin > character->admin);
		return !is_match && !sees_through;
	};

	std::vector<Character*> characters;
	characters.reserve(client->server()->world->characters.size());

	UTIL_FOREACH(client->server()->world->characters, character)
	{
		if (character->IsHideOnline() && really_hidden(character))
			continue;

		characters.push_back(character);
	}

	return characters;
}

static bool CanListPlayers(EOClient* client)
{
	const bool logged_in = client->player && client->player->character;
	return logged_in || client->server()->world->config["SLN"] || client->server()->world->config["AllowStats"];
}

// Requested the full online list
void Players_Request(EOClient* client, const client::PlayersRequestClientPacket&)
{
	if (!CanListPlayers(client))
		return;

	// Not a protocol icon: the SLN shows it for bot characters
	constexpr server::CharacterIcon SlnBotIcon = static_cast<server::CharacterIcon>(20);

	World* world = client->server()->world;
	const bool from_character = client->player && client->player->character;
	const std::string hidden_admin_suffix = " " + world->i18n.Format("hidden_admin_suffix");

	server::InitInitServerPacket::ReplyCodeDataPlayersList data;

	for (Character* character : VisibleOnlineCharacters(client))
	{
		server::OnlinePlayer& player = data.players_list.players.emplace_back();
		player.name = character->SourceName();
		player.title = character->title;
		player.level = character->level;
		player.icon = (character->bot && !from_character) ? SlnBotIcon : Converters::CharacterIconFrom(character);
		player.class_id = character->clas;
		player.guild_tag = character->PaddedGuildTag();

		if (character->IsHideOnline())
			player.name += hidden_admin_suffix;
	}

	client->Send(server::InitInitServerPacket::ForPlayersList(std::move(data)));
}

// Requested the online names for the friends and ignore lists
void Players_List(EOClient* client, const client::PlayersListClientPacket&)
{
	if (!CanListPlayers(client))
		return;

	server::InitInitServerPacket::ReplyCodeDataPlayersListFriends data;

	// Names are matched against the client's friends list, so hidden admins are listed without a suffix
	for (Character* character : VisibleOnlineCharacters(client))
	{
		data.players_list.players.push_back(character->SourceName());
	}

	client->Send(server::InitInitServerPacket::ForPlayersListFriends(std::move(data)));
}

PACKET_HANDLER_REGISTER(PACKET_PLAYERS)
	Register(Players_Accept, Playing);
	Register(Players_List, Any);
	Register(Players_Request, Any);
PACKET_HANDLER_REGISTER_END(PACKET_PLAYERS)

}
