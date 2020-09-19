
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../config.hpp"
#include "../eoclient.hpp"
#include "../eoserver.hpp"
#include "../packet.hpp"
#include "../player.hpp"
#include "../world.hpp"
#include "../extra/seose_compat.hpp"

#include "../console.hpp"
#include "../util.hpp"
#include "../util/secure_string.hpp"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace Handlers
{

// Check if a character exists
void Account_Request(EOClient *client, PacketReader &reader)
{
	std::string username = reader.GetEndString();

	username = util::lowercase(username);

	PacketBuilder reply(PACKET_ACCOUNT, PACKET_REPLY, 5);

	if (!Player::ValidName(username))
	{
		reply.AddShort(ACCOUNT_NOT_APPROVED);
		reply.AddString("NO");
	}
	else if (client->server()->world->PlayerExists(username))
	{
		reply.AddShort(ACCOUNT_EXISTS);
		reply.AddString("NO");
	}
	else
	{
		if (client->GetSeqStart() > 240)
			client->AccountReplyNewSequence();

		reply.AddShort(ACCOUNT_CONTINUE);
		reply.AddChar(client->GetSeqStart());
		reply.AddString("OK");
	}

	client->Send(reply);
}

// Account creation
void Account_Create(EOClient *client, PacketReader &reader)
{
	reader.GetShort(); // Account creation "session ID"
	reader.GetByte(); // ?

	AccountCreateInfo accountInfo;

	accountInfo.username = util::lowercase(reader.GetBreakString());
	accountInfo.password = std::move(reader.GetBreakString());
	accountInfo.fullname = reader.GetBreakString();
	accountInfo.location = reader.GetBreakString();
	accountInfo.email = reader.GetBreakString();
	accountInfo.computer = reader.GetBreakString();
	accountInfo.remoteIp = client->GetRemoteAddr();

	try
	{
		accountInfo.hdid = static_cast<int>(util::to_uint_raw(reader.GetBreakString()));
	}
	catch (std::invalid_argument&)
	{
		return;
	}

	if (accountInfo.username.length() < std::size_t(int(client->server()->world->config["AccountMinLength"]))
	 || accountInfo.username.length() > std::size_t(int(client->server()->world->config["AccountMaxLength"]))
	 || accountInfo.password.str().length() < std::size_t(int(client->server()->world->config["PasswordMinLength"]))
	 || accountInfo.password.str().length() > std::size_t(int(client->server()->world->config["PasswordMaxLength"]))
	 || accountInfo.fullname.length() > std::size_t(int(client->server()->world->config["RealNameMaxLength"]))
	 || accountInfo.location.length() > std::size_t(int(client->server()->world->config["LocationMaxLength"]))
	 || accountInfo.email.length() > std::size_t(int(client->server()->world->config["EmailMaxLength"]))
	 || accountInfo.computer.length() > std::size_t(int(client->server()->world->config["ComputerNameLength"])))
	{
		return;
	}

	if (client->server()->world->config["SeoseCompat"])
		accountInfo.password = std::move(seose_str_hash(accountInfo.password.str(), client->server()->world->config["SeoseCompatKey"]));

	PacketBuilder reply(PACKET_ACCOUNT, PACKET_REPLY, 4);

	if (!Player::ValidName(accountInfo.username))
	{
		reply.AddShort(ACCOUNT_NOT_APPROVED);
		reply.AddString("NO");
		client->Send(reply);
	}
	else if (client->server()->world->PlayerExists(accountInfo.username))
	{
		reply.AddShort(ACCOUNT_EXISTS);
		reply.AddString("NO");
		client->Send(reply);
	}
	else
	{
		// Player is created on a background thread. This callback is called on a different thread assuming the operation is successful.
		// Username is captured by value so the function can safely return without the memory being deallocated / stack corrupted.
		std::string username(accountInfo.username);
		auto onSuccess = [client, username]()
		{
			PacketBuilder succeededReply(PACKET_ACCOUNT, PACKET_REPLY, 4);
			succeededReply.AddShort(ACCOUNT_CREATED);
			succeededReply.AddString("OK");

			// TODO: there are potential implications for thread safety of Send (and other client methods)
			client->Send(succeededReply);

			Console::Out("New account: %s", username.c_str());
		};

		client->server()->world->CreatePlayer(std::move(accountInfo), onSuccess);
	}
}

// Change password
void Account_Agree(Player *player, PacketReader &reader)
{
	std::string username = reader.GetBreakString();
	util::secure_string oldpassword(std::move(reader.GetBreakString()));
	util::secure_string newpassword(std::move(reader.GetBreakString()));

	if (username.length() < std::size_t(int(player->world->config["AccountMinLength"]))
	 || username.length() > std::size_t(int(player->world->config["AccountMaxLength"]))
	 || oldpassword.str().length() < std::size_t(int(player->world->config["PasswordMinLength"]))
	 || oldpassword.str().length() > std::size_t(int(player->world->config["PasswordMaxLength"]))
	 || newpassword.str().length() < std::size_t(int(player->world->config["PasswordMinLength"]))
	 || newpassword.str().length() > std::size_t(int(player->world->config["PasswordMaxLength"])))
	{
		return;
	}

	if (!Player::ValidName(username))
	{
		PacketBuilder reply(PACKET_ACCOUNT, PACKET_REPLY, 4);
		reply.AddShort(ACCOUNT_NOT_APPROVED);
		reply.AddString("NO");
		player->Send(reply);
		return;
	}
	else if (!player->world->PlayerExists(username))
	{
		return;
	}

	if (player->world->config["SeoseCompat"])
		oldpassword = std::move(seose_str_hash(oldpassword.str(), player->world->config["SeoseCompatKey"]));

	if (player->world->config["SeoseCompat"])
		newpassword = std::move(seose_str_hash(newpassword.str(), player->world->config["SeoseCompatKey"]));

	{
		std::unique_ptr<Player> changepass(player->world->Login(username, std::move(oldpassword)));

		if (!changepass)
		{
			PacketBuilder reply(PACKET_ACCOUNT, PACKET_REPLY, 4);
			reply.AddShort(ACCOUNT_CHANGE_FAILED);
			reply.AddString("NO");
			player->Send(reply);
			return;
		}

		changepass->ChangePass(std::move(newpassword));
	}

	PacketBuilder reply(PACKET_ACCOUNT, PACKET_REPLY, 4);
	reply.AddShort(ACCOUNT_CHANGED);
	reply.AddString("OK");

	player->Send(reply);
}

PACKET_HANDLER_REGISTER(PACKET_ACCOUNT)
	Register(PACKET_REQUEST, Account_Request, Menu, 0.5);
	Register(PACKET_CREATE, Account_Create, Menu, 1.0);
	Register(PACKET_AGREE, Account_Agree, Character_Menu, 1.0);
PACKET_HANDLER_REGISTER_END(PACKET_ACCOUNT)

}
