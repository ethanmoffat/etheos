
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../converters.hpp"
#include "../eoclient.hpp"
#include "../eodata.hpp"
#include "../eoserver.hpp"
#include "../player.hpp"
#include "../world.hpp"

#include "../util.hpp"
#include "../util/secure_string.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/net/server/structs.hpp>

#include <cstddef>
#include <string>
#include <utility>

namespace Handlers
{

// Log in to an account
void Login_Request(EOClient* client, const client::LoginRequestClientPacket& packet)
{
	if (packet.username.length() > std::size_t(int(client->server()->world->config["AccountMaxLength"]))
	 || packet.password.length() > std::size_t(int(client->server()->world->config["PasswordMaxLength"])))
	{
		return;
	}

	std::string username = util::lowercase(packet.username);

	if (client->server()->world->CheckBan(&username, 0, 0) != -1)
	{
		if (static_cast<bool>(client->server()->world->config["InitLoginBan"]))
		{
			client->Send(server::InitInitServerPacket::ForBannedPermanent());
		}
		else
		{
			client->Send(server::LoginReplyServerPacket::ForBanned());
		}

		client->Close();
		return;
	}

	if (username.length() < std::size_t(int(client->server()->world->config["AccountMinLength"])))
	{
		client->Send(server::LoginReplyServerPacket::ForWrongUser());
		return;
	}

	// The password used to be SeoseCompat hashed (now done by LoginManager) before this check. The hash is far longer than
	// any practical minimum, so the check is skipped for SeoseCompat to keep accepting short passwords.
	if (!client->server()->world->config["SeoseCompat"]
	 && packet.password.length() < std::size_t(int(client->server()->world->config["PasswordMinLength"])))
	{
		client->Send(server::LoginReplyServerPacket::ForWrongUserPassword());
		return;
	}

	if (client->server()->world->characters.size() >= static_cast<std::size_t>(static_cast<int>(client->server()->world->config["MaxPlayers"])))
	{
		client->Send(server::LoginReplyServerPacket::ForBusy());
		client->Close();
		return;
	}

	auto successCallback = [username](EOClient* c)
	{
		c->player = c->server()->world->PlayerFactory(username);

		// The client may disconnect if the password generation takes too long
		if (!c->Connected())
			return;

		if (!c->player)
		{
			// Someone deleted the account between checking it and logging in
			c->Send(server::LoginReplyServerPacket::ForWrongUser());
		}
		else
		{
			c->player->id = c->id;
			c->player->client = c;
			c->state = EOClient::LoggedIn;

			server::LoginReplyServerPacket::ReplyCodeDataOk ok;

			UTIL_FOREACH(c->player->characters, character)
			{
				ok.characters.push_back(Converters::CharacterSelectionListEntryFrom(character));
			}

			c->Send(server::LoginReplyServerPacket::ForOk(std::move(ok)));
		}
	};

	auto failureCallback = [](EOClient* c, const LoginResult& reply)
	{
		c->Send(*reply);

		int max_login_attempts = int(c->server()->world->config["MaxLoginAttempts"]);

		if (max_login_attempts != 0 && ++c->login_attempts >= max_login_attempts)
		{
			c->Close();
		}
	};

	// if the player is already logged in, do the max_login_attempts logic above instead of just d/cing the client
	// this check used to be in World::CheckCredentials, but needs access to username so I'm just putting it here
	//
	// PlayerOnline will also check for a pending login of 'username', preventing concurrent logins for the same account
	if (client->server()->world->PlayerOnline(username))
	{
		failureCallback(client, server::LoginReplyServerPacket::ForLoggedIn());
		return;
	}

	World* world = client->server()->world;
	world->SetPendingLogin(username, true);

	world->CheckCredential(client)
		->OnSuccess(successCallback)
		->OnFailure(failureCallback)
		->OnComplete([world, username]() { world->SetPendingLogin(username, false); })
		->Execute(util::make_secure_shared(packet, &client::LoginRequestClientPacket::password));
}

PACKET_HANDLER_REGISTER(Login)
	Register(Login_Request, Menu, 1.0);
PACKET_HANDLER_REGISTER_END(Login)

}
