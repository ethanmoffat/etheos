
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

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/net/server/structs.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace Handlers
{

// Creates a Login_Reply for a reply code that has no data
static net::server::LoginReplyServerPacket MakeLoginReply(net::server::LoginReply reply_code)
{
	net::server::LoginReplyServerPacket reply;
	reply.reply_code = reply_code;

	switch (reply_code)
	{
		case net::server::LoginReply::WrongUser: reply.reply_code_data = net::server::LoginReplyServerPacket::ReplyCodeDataWrongUser(); break;
		case net::server::LoginReply::WrongUserPassword: reply.reply_code_data = net::server::LoginReplyServerPacket::ReplyCodeDataWrongUserPassword(); break;
		case net::server::LoginReply::Banned: reply.reply_code_data = net::server::LoginReplyServerPacket::ReplyCodeDataBanned(); break;
		case net::server::LoginReply::LoggedIn: reply.reply_code_data = net::server::LoginReplyServerPacket::ReplyCodeDataLoggedIn(); break;
		case net::server::LoginReply::Busy: reply.reply_code_data = net::server::LoginReplyServerPacket::ReplyCodeDataBusy(); break;
		default: throw std::invalid_argument("Login reply code " + net::server::ToString(reply_code) + " has data");
	}

	return reply;
}

// Log in to an account
void Login_Request(EOClient* client, const net::client::LoginRequestClientPacket& packet)
{
	std::string username = packet.username;
	util::secure_string password(std::string(packet.password));

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
		if (static_cast<bool>(client->server()->world->config["InitLoginBan"]))
		{
			net::server::InitInitServerPacket::ReplyCodeDataBanned banned;
			banned.ban_type = net::server::InitBanType::Permanent;

			net::server::InitInitServerPacket reply;
			reply.reply_code = net::server::InitReply::Banned;
			reply.reply_code_data = banned;
			client->Send(reply);
		}
		else
		{
			client->Send(MakeLoginReply(net::server::LoginReply::Banned));
		}

		client->Close();
		return;
	}

	if (username.length() < std::size_t(int(client->server()->world->config["AccountMinLength"])))
	{
		client->Send(MakeLoginReply(net::server::LoginReply::WrongUser));
		return;
	}

	if (password.str().length() < std::size_t(int(client->server()->world->config["PasswordMinLength"])))
	{
		client->Send(MakeLoginReply(net::server::LoginReply::WrongUserPassword));
		return;
	}

	if (client->server()->world->characters.size() >= static_cast<std::size_t>(static_cast<int>(client->server()->world->config["MaxPlayers"])))
	{
		client->Send(MakeLoginReply(net::server::LoginReply::Busy));
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
			c->Send(MakeLoginReply(net::server::LoginReply::WrongUser));
		}
		else
		{
			c->player->id = c->id;
			c->player->client = c;
			c->state = EOClient::LoggedIn;

			net::server::LoginReplyServerPacket::ReplyCodeDataOk ok;

			UTIL_FOREACH(c->player->characters, character)
			{
				ok.characters.push_back(character->SelectionListEntry());
			}

			net::server::LoginReplyServerPacket reply;
			reply.reply_code = net::server::LoginReply::Ok;
			reply.reply_code_data = std::move(ok);
			c->Send(reply);
		}
	};

	auto failureCallback = [username](EOClient* c, int failureReason)
	{
		c->server()->world->SetPendingLogin(username, false);

		c->Send(MakeLoginReply(static_cast<net::server::LoginReply>(failureReason)));

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
	Register(Login_Request, Menu, 1.0);
PACKET_HANDLER_REGISTER_END(PACKET_LOGIN)

}
