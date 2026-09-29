
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

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace Handlers
{

template <typename TData> static net::server::AccountReplyServerPacket MakeAccountReply(net::server::AccountReply reply_code)
{
	net::server::AccountReplyServerPacket reply;
	reply.reply_code = reply_code;
	reply.reply_code_data = TData();
	return reply;
}

// Check if a character exists
void Account_Request(EOClient* client, const net::client::AccountRequestClientPacket& packet)
{
	std::string username = util::lowercase(packet.username);

	if (!Player::ValidName(username))
	{
		client->Send(MakeAccountReply<net::server::AccountReplyServerPacket::ReplyCodeDataNotApproved>(net::server::AccountReply::NotApproved));
	}
	else if (client->server()->world->PlayerExists(username))
	{
		client->Send(MakeAccountReply<net::server::AccountReplyServerPacket::ReplyCodeDataExists>(net::server::AccountReply::Exists));
	}
	else
	{
		auto sequence_start = client->AccountReplyNewSequence();

		client->NewCreateID();

		// A reply code above 9 is the account creation session ID
		net::server::AccountReplyServerPacket::ReplyCodeDataDefault data;
		data.sequence_start = sequence_start.Value();

		net::server::AccountReplyServerPacket reply;
		reply.reply_code = static_cast<net::server::AccountReply>(client->create_id);
		reply.reply_code_data = data;
		client->Send(reply);
	}
}

// Account creation
void Account_Create(EOClient* client, const net::client::AccountCreateClientPacket& packet)
{
	if (packet.session_id != client->create_id)
	{
		client->Close();
		return;
	}

	AccountCreateInfo accountInfo;

	accountInfo.username = util::lowercase(packet.username);
	accountInfo.password = util::secure_string(std::string(packet.password));
	accountInfo.fullname = packet.full_name;
	accountInfo.location = packet.location;
	accountInfo.email = packet.email;
	accountInfo.computer = packet.computer;
	accountInfo.remoteIp = client->GetRemoteAddr();

	try
	{
		accountInfo.hdid = static_cast<int>(util::to_uint_raw(packet.hdid));
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

	if (!Player::ValidName(accountInfo.username))
	{
		client->Send(MakeAccountReply<net::server::AccountReplyServerPacket::ReplyCodeDataNotApproved>(net::server::AccountReply::NotApproved));
	}
	else if (client->server()->world->PlayerExists(accountInfo.username))
	{
		client->Send(MakeAccountReply<net::server::AccountReplyServerPacket::ReplyCodeDataExists>(net::server::AccountReply::Exists));
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
				c->Send(MakeAccountReply<net::server::AccountReplyServerPacket::ReplyCodeDataCreated>(net::server::AccountReply::Created));

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
void Account_Agree(Player* player, const net::client::AccountAgreeClientPacket& packet)
{
	PasswordChangeInfo passwordChangeInfo;
	passwordChangeInfo.username = packet.username;
	passwordChangeInfo.oldpassword = util::secure_string(std::string(packet.old_password));
	passwordChangeInfo.newpassword = util::secure_string(std::string(packet.new_password));

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
		player->Send(MakeAccountReply<net::server::AccountReplyServerPacket::ReplyCodeDataNotApproved>(net::server::AccountReply::NotApproved));
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

		c->Send(MakeAccountReply<net::server::AccountReplyServerPacket::ReplyCodeDataChanged>(net::server::AccountReply::Changed));
	};

	auto failureCallback = [](EOClient* c, int result)
	{
		(void)result;

		// The client may disconnect if the password generation takes too long
		if (!c->Connected())
			return;

		c->Send(MakeAccountReply<net::server::AccountReplyServerPacket::ReplyCodeDataChangeFailed>(net::server::AccountReply::ChangeFailed));
	};

	player->world->ChangePassword(player->client)
		->OnSuccess(successCallback)
		->OnFailure(failureCallback)
		->Execute(std::make_shared<PasswordChangeInfo>(std::move(passwordChangeInfo)));
}

PACKET_HANDLER_REGISTER(PACKET_ACCOUNT)
	Register(Account_Request, Menu, 0.5);
	Register(Account_Create, Menu, 1.0);
	Register(Account_Agree, Character_Menu, 1.0);
PACKET_HANDLER_REGISTER_END(PACKET_ACCOUNT)

}
