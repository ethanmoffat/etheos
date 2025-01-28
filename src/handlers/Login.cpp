
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../eoclient.hpp"
#include "../eodata.hpp"
#include "../eoserver.hpp"
#include "../hash.hpp"
#include "../packet.hpp"
#include "../player.hpp"
#include "../world.hpp"
#include "../extra/seose_compat.hpp"

#include "../util.hpp"
#include "../util/secure_string.hpp"

#include <cstddef>
#include <string>
#include <utility>

namespace Handlers
{

// Log in to an account
void Login_Request(EOClient *client, PacketReader &reader)
{
	std::string username = reader.GetBreakString();
	util::secure_string password(std::move(reader.GetBreakString()));

	if (username.length() > std::size_t(int(client->server()->world->config["AccountMaxLength"]))
	 || password.str().length() > std::size_t(int(client->server()->world->config["PasswordMaxLength"])))
	{
		return;
	}

	username = util::lowercase(username);

	if (client->server()->world->config["SeoseCompat"])
		password = std::move(seose_str_hash(password.str(), client->server()->world->config["SeoseCompatKey"]));

	if (client->server()->world->CheckBan(&username, 0, 0) != -1)
	{
		PacketBuilder reply(PACKET_F_INIT, PACKET_A_INIT, 2);

		if (static_cast<bool>(client->server()->world->config["InitLoginBan"]))
		{
			reply.AddByte(INIT_BANNED);
			reply.AddByte(INIT_BAN_PERM);
		}
		else
		{
			reply.SetID(PACKET_LOGIN, PACKET_REPLY);
			reply.AddShort(LOGIN_ACCOUNT_BANNED);
		}

		client->Send(reply);
		client->Close();
		return;
	}

	if (username.length() < std::size_t(int(client->server()->world->config["AccountMinLength"])))
	{
		PacketBuilder reply(PACKET_LOGIN, PACKET_REPLY, 2);
		reply.AddShort(LOGIN_WRONG_USER);
		client->Send(reply);
		return;
	}

	if (password.str().length() < std::size_t(int(client->server()->world->config["PasswordMinLength"])))
	{
		PacketBuilder reply(PACKET_LOGIN, PACKET_REPLY, 2);
		reply.AddShort(LOGIN_WRONG_USERPASS);
		client->Send(reply);
		return;
	}

	if (client->server()->world->characters.size() >= static_cast<std::size_t>(static_cast<int>(client->server()->world->config["MaxPlayers"])))
	{
		PacketBuilder reply(PACKET_LOGIN, PACKET_REPLY, 2);
		reply.AddShort(LOGIN_BUSY);
		client->Send(reply);
		client->Close();
		return;
	}

	auto successCallback = [username](EOClient* c)
	{
		c->player = c->server()->world->PlayerFactory(username);
		c->server()->world->SetPendingLogin(username, false);

		// The client may disconnect if the password generation takes too long
		if (!c->Connected())
			return;

		if (!c->player)
		{
			// Someone deleted the account between checking it and logging in
			PacketBuilder reply(PACKET_LOGIN, PACKET_REPLY, 2);
			reply.AddShort(LOGIN_WRONG_USER);
			c->Send(reply);
		}
		else
		{
			c->player->id = c->id;
			c->player->client = c;
			c->state = EOClient::LoggedIn;

			PacketBuilder reply(PACKET_LOGIN, PACKET_REPLY, 5 + c->player->characters.size() * 34);
			reply.AddShort(LOGIN_OK);
			reply.AddChar(static_cast<unsigned char>(c->player->characters.size()));
			reply.AddByte(2);
			reply.AddByte(255);

			UTIL_FOREACH(c->player->characters, character)
			{
				reply.AddBreakString(character->SourceName());
				reply.AddInt(character->id);
				reply.AddChar(character->level);
				reply.AddChar(character->gender);
				reply.AddChar(character->hairstyle);
				reply.AddChar(character->haircolor);
				reply.AddChar(character->race);
				reply.AddChar(character->admin);
				character->AddPaperdollData(reply, "BAHSW");

				reply.AddByte(255);
			}

			c->Send(reply);
		}
	};

	auto failureCallback = [username](EOClient* c, int failureReason)
	{
		c->server()->world->SetPendingLogin(username, false);

		PacketBuilder reply(PACKET_LOGIN, PACKET_REPLY, 2);
		reply.AddShort(failureReason);
		c->Send(reply);

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
		failureCallback(client, LOGIN_LOGGEDIN);
		return;
	}

	client->server()->world->SetPendingLogin(username, true);

	client->server()->world->CheckCredential(client)
		->OnSuccess(successCallback)
		->OnFailure(failureCallback)
		->Execute(std::shared_ptr<AccountCredentials>(new AccountCredentials { username, std::move(password), HashFunc::NONE }));
}

PACKET_HANDLER_REGISTER(PACKET_LOGIN)
	Register(PACKET_REQUEST, Login_Request, Menu, 1.0);
PACKET_HANDLER_REGISTER_END(PACKET_LOGIN)

}
