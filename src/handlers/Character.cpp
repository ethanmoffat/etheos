
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../eodata.hpp"
#include "../packet.hpp"
#include "../player.hpp"
#include "../world.hpp"

#include "../console.hpp"
#include "../util.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/net/server/structs.hpp>

#include <algorithm>
#include <cstddef>
#include <string>

namespace Handlers
{

template <typename TData> static net::server::CharacterReplyServerPacket MakeCharacterReply(net::server::CharacterReply reply_code, TData data = TData())
{
	net::server::CharacterReplyServerPacket reply;
	reply.reply_code = reply_code;
	reply.reply_code_data = std::move(data);
	return reply;
}

void Character_Request(Player* player, const net::client::CharacterRequestClientPacket& packet)
{
	(void)packet;

	player->NewCharacterOp();

	// A reply code above 9 is the character creation session ID
	player->Send(MakeCharacterReply<net::server::CharacterReplyServerPacket::ReplyCodeDataDefault>(static_cast<net::server::CharacterReply>(player->char_op_id)));
}

void Character_Create(Player* player, const net::client::CharacterCreateClientPacket& packet)
{
	if (packet.session_id != player->char_op_id)
	{
		player->client->Close();
		return;
	}

	Gender gender = static_cast<Gender>(packet.gender);
	int hairstyle = packet.hair_style;
	int haircolor = packet.hair_color;
	Skin race = static_cast<Skin>(packet.skin);
	std::string name = util::lowercase(packet.name);

	if ((gender != GENDER_MALE && gender != GENDER_FEMALE)
		|| hairstyle < static_cast<int>(player->world->config["CreateMinHairStyle"])
		|| hairstyle > static_cast<int>(player->world->config["CreateMaxHairStyle"])
		|| haircolor < static_cast<int>(player->world->config["CreateMinHairColor"])
		|| haircolor > static_cast<int>(player->world->config["CreateMaxHairColor"])
		|| race < static_cast<int>(player->world->config["CreateMinSkin"])
		|| race > static_cast<int>(player->world->config["CreateMaxSkin"]))
	 {
		player->client->Close();
		return;
	 }

	if (player->characters.size() >= static_cast<std::size_t>(static_cast<int>(player->world->config["MaxCharacters"])))
	{
		player->Send(MakeCharacterReply<net::server::CharacterReplyServerPacket::ReplyCodeDataFull>(net::server::CharacterReply::Full));
	}
	else if (!Character::ValidName(name))
	{
		player->Send(MakeCharacterReply<net::server::CharacterReplyServerPacket::ReplyCodeDataNotApproved>(net::server::CharacterReply::NotApproved));
	}
	else if (player->world->CharacterExists(name))
	{
		player->Send(MakeCharacterReply<net::server::CharacterReplyServerPacket::ReplyCodeDataExists>(net::server::CharacterReply::Exists));
	}
	else
	{
		player->AddCharacter(name, gender, hairstyle, haircolor, race);
		Console::Out("New character: %s (%s)", name.c_str(), player->username.c_str());

		net::server::CharacterReplyServerPacket::ReplyCodeDataOk ok;

		UTIL_FOREACH(player->characters, character)
		{
			ok.characters.push_back(character->SelectionListEntry());
		}

		player->Send(MakeCharacterReply(net::server::CharacterReply::Ok, std::move(ok)));
	}

	player->char_op_id = 0;
}

// Delete a character from an account
void Character_Remove(Player* player, const net::client::CharacterRemoveClientPacket& packet)
{
	auto it = std::find_if(UTIL_RANGE(player->characters), [&](Character *c) -> bool
	{
		return (c->id == static_cast<unsigned int>(packet.character_id));
	});

	if (packet.session_id != player->char_op_id || it == player->characters.end())
	{
		player->client->Close();
		return;
	}

	std::string partner_name = (*it)->partner;
	Character* partner = player->world->GetCharacter(partner_name);

	if (partner)
	{
		partner->partner.clear();
	}
	else
	{
		player->world->db->Query(
			"UPDATE `characters` SET `partner` = '' WHERE `name` = '$' AND partner = '$'",
			partner_name.c_str(),
			(*it)->SourceName().c_str()
		);
	}

	player->world->DeleteCharacter((*it)->real_name);
	Console::Out("Deleted character: %s (%s)", (*it)->real_name.c_str(), player->username.c_str());

	if ((*it)->admin > 0)
		player->world->DecAdminCount();

	player->characters.erase(it);

	net::server::CharacterReplyServerPacket::ReplyCodeDataDeleted deleted;

	UTIL_FOREACH(player->characters, character)
	{
		deleted.characters.push_back(character->SelectionListEntry());
	}

	player->Send(MakeCharacterReply(net::server::CharacterReply::Deleted, std::move(deleted)));
	player->char_op_id = 0;
}

// Request to delete a character from an account
void Character_Take(Player* player, const net::client::CharacterTakeClientPacket& packet)
{
	auto it = std::find_if(UTIL_CRANGE(player->characters), [&](Character *c) -> bool
	{
		return (c->id == static_cast<unsigned int>(packet.character_id));
	});

	if (it == player->characters.end())
	{
		player->client->Close();
		return;
	}

	player->NewCharacterOp();

	net::server::CharacterPlayerServerPacket reply;
	reply.session_id = player->char_op_id;
	reply.character_id = packet.character_id;
	player->Send(reply);
}

PACKET_HANDLER_REGISTER(PACKET_CHARACTER)
	Register(Character_Request, Character_Menu);
	Register(Character_Create, Character_Menu, 1.0);
	Register(Character_Remove, Character_Menu, 1.0);
	Register(Character_Take, Character_Menu);
PACKET_HANDLER_REGISTER_END(PACKET_CHARACTER)

}
