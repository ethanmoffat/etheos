
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

		client->NewCreateID();

		reply.AddShort(client->create_id);
		reply.AddChar(client->GetSeqStart());
		reply.AddString("OK");
	}

	client->Send(reply);
}

// Account creation
void Account_Create(EOClient *client, PacketReader &reader)
{
	unsigned short create_id = reader.GetShort(); // Account creation "session ID"
	unsigned char byte255 = reader.GetByte();

	if (create_id != client->create_id || byte255 != 255)
	{
		client->Close();
		return;
	}

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
		// Username is captured by value so the function can safely return without the memory being deallocated / stack corrupted.
		std::string username(accountInfo.username);

		auto successCallback = [username](EOClient* c)
		{
			// The client may disconnect if the password generation takes too long
			if (c->Connected())
			{
				PacketBuilder succeededReply(PACKET_ACCOUNT, PACKET_REPLY, 4);
				succeededReply.AddShort(ACCOUNT_CREATED);
				succeededReply.AddString("OK");

				c->Send(succeededReply);

				c->create_id = 0;
			}

			Console::Out("New account: %s", username.c_str());
		};

		client->server()->world->CreateAccount(client)
			->OnSuccess(successCallback)
			->OnFailure([](EOClient* c, int) { c->Close(); })
			->Execute(std::make_shared<AccountCreateInfo>(std::move(accountInfo)));
	}
}

// Change password
void Account_Agree(Player *player, PacketReader &reader)
{
	PasswordChangeInfo passwordChangeInfo;;
	passwordChangeInfo.username = reader.GetBreakString();
	passwordChangeInfo.oldpassword = std::move(reader.GetBreakString());
	passwordChangeInfo.newpassword = std::move(reader.GetBreakString());

	if (passwordChangeInfo.username.length() < std::size_t(int(player->world->config["AccountMinLength"]))
	 || passwordChangeInfo.username.length() > std::size_t(int(player->world->config["AccountMaxLength"]))
	 || passwordChangeInfo.oldpassword.str().length() < std::size_t(int(player->world->config["PasswordMinLength"]))
	 || passwordChangeInfo.oldpassword.str().length() > std::size_t(int(player->world->config["PasswordMaxLength"]))
	 || passwordChangeInfo.newpassword.str().length() < std::size_t(int(player->world->config["PasswordMinLength"]))
	 || passwordChangeInfo.newpassword.str().length() > std::size_t(int(player->world->config["PasswordMaxLength"])))
	{
		return;
	}

	if (!Player::ValidName(passwordChangeInfo.username))
	{
		PacketBuilder reply(PACKET_ACCOUNT, PACKET_REPLY, 4);
		reply.AddShort(ACCOUNT_NOT_APPROVED);
		reply.AddString("NO");
		player->Send(reply);
		return;
	}
	else if (!player->world->PlayerExists(passwordChangeInfo.username))
	{
		return;
	}

	if (player->world->config["SeoseCompat"])
	{
		passwordChangeInfo.oldpassword = std::move(seose_str_hash(passwordChangeInfo.oldpassword.str(), player->world->config["SeoseCompatKey"]));
		passwordChangeInfo.newpassword = std::move(seose_str_hash(passwordChangeInfo.newpassword.str(), player->world->config["SeoseCompatKey"]));
	}

	auto successCallback = [](EOClient* c)
	{
		// The client may disconnect if the password generation takes too long
		if (!c->Connected())
			return;

		PacketBuilder reply(PACKET_ACCOUNT, PACKET_REPLY, 4);
		reply.AddShort(ACCOUNT_CHANGED);
		reply.AddString("OK");

		c->Send(reply);
	};

	auto failureCallback = [](EOClient* c, int result)
	{
		(void)result;

		// The client may disconnect if the password generation takes too long
		if (!c->Connected())
			return;

		PacketBuilder reply(PACKET_ACCOUNT, PACKET_REPLY, 4);
		reply.AddShort(ACCOUNT_CHANGE_FAILED);
		reply.AddString("NO");

		c->Send(reply);
	};

	player->world->ChangePassword(player->client)
		->OnSuccess(successCallback)
		->OnFailure(failureCallback)
		->Execute(std::make_shared<PasswordChangeInfo>(std::move(passwordChangeInfo)));
}

PACKET_HANDLER_REGISTER(PACKET_ACCOUNT)
	Register(PACKET_REQUEST, Account_Request, Menu, 0.5);
	Register(PACKET_CREATE, Account_Create, Menu, 1.0);
	Register(PACKET_AGREE, Account_Agree, Character_Menu, 1.0);
PACKET_HANDLER_REGISTER_END(PACKET_ACCOUNT)

}
