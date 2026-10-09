
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../guild.hpp"
#include "../i18n.hpp"
#include "../map.hpp"
#include "../party.hpp"
#include "../world.hpp"
#include "../commands/commands.hpp"

#include "../console.hpp"
#include "../util.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <csignal>
#include <ctime>
#include <string>
#include <vector>

static void limit_message(std::string &message, std::size_t chatlength)
{
	if (message.length() > chatlength)
	{
		message = message.substr(0, chatlength - 6) + " [...]";
	}
}

namespace Handlers
{

// Guild chat message
void Talk_Request(Character* character, const client::TalkRequestClientPacket& packet)
{
	if (!character->guild) return;
	if (character->muted_until > time(0)) return;

	std::string message = packet.message;
	limit_message(message, static_cast<int>(character->world->config["ChatLength"]));

	character->guild->Msg(character, message, false);
}

// Party chat messagea
void Talk_Open(Character* character, const client::TalkOpenClientPacket& packet)
{
	if (!character->party) return;
	if (character->muted_until > time(0)) return;

	std::string message = packet.message;
	limit_message(message, static_cast<int>(character->world->config["ChatLength"]));

	character->party->Msg(character, message, false);
}

// Global chat message
void Talk_Msg(Character* character, const client::TalkMsgClientPacket& packet)
{
	if (character->muted_until > time(0)) return;

	if (character->mapid == static_cast<int>(character->world->config["JailMap"]))
	{
		return;
	}

	std::string message = packet.message;
	limit_message(message, static_cast<int>(character->world->config["ChatLength"]));

	character->world->Msg(character, message, false);
}

// Private chat message
void Talk_Tell(Character* character, const client::TalkTellClientPacket& packet)
{
	if (character->muted_until > time(0)) return;

	std::string message = packet.message;
	limit_message(message, static_cast<int>(character->world->config["ChatLength"]));
	Character *to = character->world->GetCharacter(packet.name);

	if (to && !to->IsHideOnline())
	{
		if (to->whispers)
		{
			to->Msg(character, message);
		}
		else
		{
			character->Msg(to, character->world->i18n.Format("whisper_blocked", to->SourceName()));
		}
	}
	else
	{
		server::TalkReplyServerPacket reply;
		reply.reply_code = server::TalkReply::NotFound;
		reply.name = packet.name;
		character->Send(reply);
	}
}

// Public chat message
void Talk_Report(Character* character, const client::TalkReportClientPacket& packet)
{
	if (character->muted_until > time(0)) return;

	std::string message = packet.message;
	limit_message(message, static_cast<int>(character->world->config["ChatLength"]));

	if (message.empty())
	{
		return;
	}

	if (character->SourceAccess() != protocol::AdminLevel::Player && message[0] == '$')
	{
		if (character->world->config["LogCommands"])
		{
			Console::Out("%s: %s", character->real_name.c_str(), message.c_str());
		}

		std::string command;
		std::vector<std::string> arguments = util::explode(' ', message);
		command = arguments.front().substr(1);
		arguments.erase(arguments.begin());

		character->world->Command(command, arguments, character);
	}
	else
	{
		character->map->Msg(character, message, false);
	}
}

// Admin chat message
void Talk_Admin(Character* character, const client::TalkAdminClientPacket& packet)
{
	if (character->SourceAccess() < protocol::AdminLevel::LightGuide) return;
	if (character->muted_until > time(0)) return;

	std::string message = packet.message;
	limit_message(message, static_cast<int>(character->world->config["ChatLength"]));

	character->world->AdminMsg(character, message, protocol::AdminLevel::LightGuide, false);
}

// Announcement message
void Talk_Announce(Character* character, const client::TalkAnnounceClientPacket& packet)
{
	if (character->SourceAccess() < protocol::AdminLevel::LightGuide) return;
	if (character->muted_until > time(0)) return;

	std::string message = packet.message;
	limit_message(message, static_cast<int>(character->world->config["ChatLength"]));

	character->world->AnnounceMsg(character, message, false);
}

PACKET_HANDLER_REGISTER(Talk)
	Register(Talk_Request, Playing);
	Register(Talk_Open, Playing);
	Register(Talk_Msg, Playing);
	Register(Talk_Tell, Playing);
	Register(Talk_Report, Playing);
	Register(Talk_Admin, Playing);
	Register(Talk_Announce, Playing);
PACKET_HANDLER_REGISTER_END(Talk)

}
