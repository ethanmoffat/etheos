
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../config.hpp"
#include "../eoclient.hpp"
#include "../eoserver.hpp"
#include "../player.hpp"
#include "../world.hpp"

#include "../console.hpp"

#include <eolib/data/eo_reader.hpp>
#include <eolib/protocol/net/client/packet_factory.hpp>

#include <exception>
#include <stdexcept>
#include <string>

namespace Handlers
{

static std::string PacketName(net::PacketFamily family, net::PacketAction action)
{
	return net::ToString(family) + "_" + net::ToString(action);
}

void HandleMalformedPacket(EOClient* client, net::PacketFamily family, net::PacketAction action, const std::string& reason)
{
	std::string packet_name = PacketName(family, action);
	std::string address = static_cast<std::string>(client->GetRemoteAddr());

	if (!client->server()->world->config["EnforcePacketFormat"])
	{
		Console::Wrn("Dropping malformed packet %s from %s: %s", packet_name.c_str(), address.c_str(), reason.c_str());
		return;
	}

	Console::Wrn("Closing client connection sending malformed packet %s: %s: %s", packet_name.c_str(), address.c_str(), reason.c_str());
	client->Close();
}

bool DeserializePacket(EOClient* client, const QueuedPacket& packet, net::Packet& typed_packet)
{
	try
	{
		eolib::data::EoReader reader(packet.payload.data(), packet.payload.size());
		typed_packet.Deserialize(reader);
	}
	catch (const std::exception& e)
	{
		HandleMalformedPacket(client, packet.family, packet.action, e.what());
		return false;
	}

	return true;
}

template <> EOClient* GetHandlerTarget<EOClient>(EOClient* client)
{
	return client;
}

template <> Player* GetHandlerTarget<Player>(EOClient* client)
{
	if (!client->player)
		throw std::runtime_error("Player-handled packet before login");

	return client->player;
}

template <> Character* GetHandlerTarget<Character>(EOClient* client)
{
	if (!client->player)
		throw std::runtime_error("Character-handled packet before login");

	if (!client->player->character)
		throw std::runtime_error("Character-handled packet before character selection");

	return client->player->character;
}

bool packet_handler_register::StateCheck(EOClient *client, unsigned short allow_states)
{
	switch (client->state)
	{
		case EOClient::Uninitialized:
			if (allow_states & Uninitialized)
				return true;

			break;

		case EOClient::Initialized:
			if (allow_states & Menu)
				return true;

			break;

		case EOClient::LoggedIn:
			if (client->player->character)
			{
				if (allow_states & Logging_In)
					return true;
			}
			else
			{
				if (allow_states & Character_Menu)
					return true;
			}

			break;

		case EOClient::Playing:
			if (allow_states & Playing)
				return true;

			break;
	};

	return false;
}

void packet_handler_register::Register(net::PacketFamily family, net::PacketAction action, packet_handler handler)
{
	if ((handler.allow_states & Uninitialized) && handler.fn_type != packet_handler::ClientFn)
		Console::Wrn("Uninitialized state handler accepting non-client type: %s", PacketName(family, action).c_str());
	else if ((handler.allow_states & Menu) && handler.fn_type != packet_handler::ClientFn)
		Console::Wrn("Menu state handler accepting non-client type: %s", PacketName(family, action).c_str());
	else if ((handler.allow_states & Character_Menu) && handler.fn_type == packet_handler::CharacterFn)
		Console::Wrn("Character_Menu state handler accepting character type: %s", PacketName(family, action).c_str());

	if (handlers[(unsigned char)family][(unsigned char)action])
		Console::Wrn("Overriding previously registered handler: %s", PacketName(family, action).c_str());

	handlers[(unsigned char)family][(unsigned char)action] = handler;
}

void packet_handler_register::Handle(EOClient* client, const QueuedPacket& packet, bool from_queue) const
{
	if (!client::PacketFactory::Contains(packet.family, packet.action))
	{
		HandleMalformedPacket(client, packet.family, packet.action, "unrecognized packet family/action");
		return;
	}

	const packet_handler& handler = handlers[(unsigned char)packet.family][(unsigned char)packet.action];

	if (!handler)
	{
#ifdef DEBUG
		Console::Dbg("Unhandled packet: %s (not registered)", PacketName(packet.family, packet.action).c_str());
#endif // DEBUG
		return;
	}

	if (!StateCheck(client, handler.allow_states))
	{
#ifdef DEBUG
		Console::Dbg("Unhandled packet: %s (wrong client state)", PacketName(packet.family, packet.action).c_str());
#endif // DEBUG
		return;
	}

	if (!from_queue && (handler.allow_states & Playing) && !(handler.allow_states & OutOfBand))
	{
		client->queue.AddAction(packet, handler.delay);
		return;
	}

	handler.typed_invoker(handler.f, client, packet);
}

void packet_handler_register::SetDelay(net::PacketFamily family, net::PacketAction action, double delay)
{
	if (handlers[(unsigned char)family][(unsigned char)action])
	{
		handlers[(unsigned char)family][(unsigned char)action].delay = delay;
	}
}

packet_handler_register *packet_handler_register_instance;

void packet_handler_register_init::init() const
{
	packet_handler_register_instance = new packet_handler_register();
}

packet_handler_register_init::~packet_handler_register_init()
{
	if (master)
		delete packet_handler_register_instance;
}

static packet_handler_register_init packet_handler_register_init_instance(true);

}
