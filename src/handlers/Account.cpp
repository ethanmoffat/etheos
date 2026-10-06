
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

#include "../console.hpp"
#include "../util.hpp"
#include "../util/secure_string.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>

namespace Handlers
{

void Account_Request(EOClient* client, const net::client::AccountRequestClientPacket& packet)
{
	std::string username = util::lowercase(packet.username);

	if (!Player::ValidName(username))
	{
		client->Send(net::server::AccountReplyServerPacket::ForNotApproved());
	}
	else if (client->server()->world->PlayerExists(username))
	{
		client->Send(net::server::AccountReplyServerPacket::ForExists());
	}
	else
	{
		auto sequence_start = client->AccountReplyNewSequence();

		client->NewCreateID();

		// A reply code above 9 is the account creation session ID
		net::server::AccountReplyServerPacket::ReplyCodeDataDefault data;
		data.sequence_start = sequence_start.Value();

		client->Send(net::server::AccountReplyServerPacket::ForReplyCodeDefault(static_cast<net::server::AccountReply>(client->create_id), data));
	}
}

// Account creation
void Account_Create(EOClient* client, const net::client::AccountCreateClientPacket& packet)
{
	if (!client->server()->world->CheckSession(client->create_id, packet.session_id))
	{
		client->Close();
		return;
	}

	std::string username = util::lowercase(packet.username);

	try
	{
		util::to_uint_raw(packet.hdid);
	}
	catch (std::invalid_argument&)
	{
		return;
	}

	if (username.length() < std::size_t(int(client->server()->world->config["AccountMinLength"]))
	 || username.length() > std::size_t(int(client->server()->world->config["AccountMaxLength"]))
	 || packet.password.length() < std::size_t(int(client->server()->world->config["PasswordMinLength"]))
	 || packet.password.length() > std::size_t(int(client->server()->world->config["PasswordMaxLength"]))
	 || packet.full_name.length() > std::size_t(int(client->server()->world->config["RealNameMaxLength"]))
	 || packet.location.length() > std::size_t(int(client->server()->world->config["LocationMaxLength"]))
	 || packet.email.length() > std::size_t(int(client->server()->world->config["EmailMaxLength"]))
	 || packet.computer.length() > std::size_t(int(client->server()->world->config["ComputerNameLength"])))
	{
		return;
	}

	if (!Player::ValidName(username))
	{
		client->Send(net::server::AccountReplyServerPacket::ForNotApproved());
	}
	else if (client->server()->world->PlayerExists(username))
	{
		client->Send(net::server::AccountReplyServerPacket::ForExists());
	}
	else
	{
		// Username is captured by value so the function can safely return without the memory being deallocated / stack corrupted.
		auto successCallback = [username](EOClient* c)
		{
			// The client may disconnect if the password generation takes too long
			if (c->Connected())
			{
				c->Send(net::server::AccountReplyServerPacket::ForCreated());

				c->create_id = 0;
			}

			Console::Out("New account: %s", username.c_str());
		};

		client->server()->world->CreateAccount(client)
			->OnSuccess(successCallback)
			->OnFailure([](EOClient* c, bool) { c->Close(); })
			->Execute(util::make_secure_shared(packet, &net::client::AccountCreateClientPacket::password));
	}
}

// Change password
void Account_Agree(Player* player, const net::client::AccountAgreeClientPacket& packet)
{
	if (packet.username.length() < std::size_t(int(player->world->config["AccountMinLength"]))
	 || packet.username.length() > std::size_t(int(player->world->config["AccountMaxLength"]))
	 || packet.old_password.length() < std::size_t(int(player->world->config["PasswordMinLength"]))
	 || packet.old_password.length() > std::size_t(int(player->world->config["PasswordMaxLength"]))
	 || packet.new_password.length() < std::size_t(int(player->world->config["PasswordMinLength"]))
	 || packet.new_password.length() > std::size_t(int(player->world->config["PasswordMaxLength"])))
	{
		return;
	}

	if (!Player::ValidName(packet.username))
	{
		player->Send(net::server::AccountReplyServerPacket::ForNotApproved());
		return;
	}
	else if (!player->world->PlayerExists(packet.username))
	{
		return;
	}

	auto successCallback = [](EOClient* c)
	{
		// The client may disconnect if the password generation takes too long
		if (!c->Connected())
			return;

		c->Send(net::server::AccountReplyServerPacket::ForChanged());
	};

	auto failureCallback = [](EOClient* c, bool)
	{
		// The client may disconnect if the password generation takes too long
		if (!c->Connected())
			return;

		c->Send(net::server::AccountReplyServerPacket::ForChangeFailed());
	};

	player->world->ChangePassword(player->client)
		->OnSuccess(successCallback)
		->OnFailure(failureCallback)
		->Execute(util::make_secure_shared(packet, &net::client::AccountAgreeClientPacket::old_password, &net::client::AccountAgreeClientPacket::new_password));
}

PACKET_HANDLER_REGISTER(PACKET_ACCOUNT)
	Register(Account_Request, Menu, 0.5);
	Register(Account_Create, Menu, 1.0);
	Register(Account_Agree, Character_Menu, 1.0);
PACKET_HANDLER_REGISTER_END(PACKET_ACCOUNT)

}
