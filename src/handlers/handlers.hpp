
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef HANDLERS_HPP_INCLUDED
#define HANDLERS_HPP_INCLUDED

#include "../fwd/character.hpp"
#include "../fwd/eoclient.hpp"
#include "../fwd/player.hpp"
#include "../packet.hpp"

#include <eolib/protocol/net/enums.hpp>
#include <eolib/protocol/net/packet.hpp>

#include <array>
#include <string>

#define PACKET_HANDLER_PASTE_AUX2(base, id) base##id
#define PACKET_HANDLER_PASTE_AUX(base, id) PACKET_HANDLER_PASTE_AUX2(base, id)

#define PACKET_HANDLER_REGISTER(family) \
namespace { struct PACKET_HANDLER_PASTE_AUX(packet_handler_register_helper_, family) : public packet_handler_register_helper<family> \
{ \
	PACKET_HANDLER_PASTE_AUX(packet_handler_register_helper_, family)() \
	{ \
		packet_handler_register_init _init; \

#define PACKET_HANDLER_REGISTER_END(family) ; \
	} \
} PACKET_HANDLER_PASTE_AUX(packet_handler_register_helper_instance_, family); }

namespace Handlers
{

namespace net = eolib::protocol::net;

typedef void (*void_fn_t)();
typedef void (*client_handler_t)(EOClient *, PacketReader &);
typedef void (*player_handler_t)(Player *, PacketReader &);
typedef void (*character_handler_t)(Character *, PacketReader &);

// Deserializes a packet and calls a typed handler (f) with it
typedef void (*typed_invoker_t)(void_fn_t f, EOClient* client, const QueuedPacket& packet);

enum AllowState
{
	None = 0,

	// Packet encryption initialized
	Uninitialized = 1,
	Menu = 2,

	// player != NULL
	Character_Menu = 4,

	// character != NULL
	Logging_In = 8,
	Playing = 16,

	// Flags (for Playing state handlers)
	OutOfBand = 32, // Does not queue packet

	Any = 0xFFFF
};

class packet_handler
{
	public:
		enum FunctionType
		{
			Invalid = 0,
			ClientFn,
			PlayerFn,
			CharacterFn
		};

		FunctionType fn_type;
		unsigned short allow_states;
		double delay;
		void (*f)();

		// Set for handlers that take a typed eolib packet, instead of a PacketReader
		typed_invoker_t typed_invoker;

		packet_handler(FunctionType fn_type = Invalid, void_fn_t f = 0, unsigned short allow_states = 0, double delay = 0.0, typed_invoker_t typed_invoker = nullptr)
			: fn_type(fn_type)
			, allow_states(allow_states)
			, delay(delay)
			, f(f)
			, typed_invoker(typed_invoker)
		{ }

		operator bool() const
		{
			return fn_type != Invalid;
		}
};

class packet_handler_register
{
	private:
		std::array<std::array<packet_handler, 256>, 256> handlers;

	public:
		void Register(PacketFamily family, PacketAction action, packet_handler handler);

		static bool StateCheck(EOClient *client, unsigned short allow_states);

		void Handle(EOClient* client, const QueuedPacket& packet, bool from_queue = false) const;

		void SetDelay(PacketFamily family, PacketAction action, double delay);
};

extern packet_handler_register *packet_handler_register_instance;

class packet_handler_register_init
{
	private:
		bool master;

	public:
		packet_handler_register_init(bool master = false)
			: master(master)
		{
			static bool initialized;

			if (!initialized)
			{
				initialized = true;
				init();
			}
		}

		void init() const;

		~packet_handler_register_init();
};

/**
 * Logs a malformed packet, and disconnects the client if EnforcePacketFormat is on.
 */
void HandleMalformedPacket(EOClient* client, net::PacketFamily family, net::PacketAction action, const std::string& reason);

/**
 * Deserializes a packet's payload. Returns false, after handling the packet as malformed, if that fails.
 */
bool DeserializePacket(EOClient* client, const QueuedPacket& packet, net::Packet& typed_packet);

/**
 * Gets the object a handler is called with. Throws std::runtime_error if the client doesn't have one yet.
 */
template <typename TTarget> TTarget* GetHandlerTarget(EOClient* client);
template <> EOClient* GetHandlerTarget<EOClient>(EOClient* client);
template <> Player* GetHandlerTarget<Player>(EOClient* client);
template <> Character* GetHandlerTarget<Character>(EOClient* client);

template <typename TTarget> struct handler_function_type;
template <> struct handler_function_type<EOClient> { static constexpr auto value = packet_handler::ClientFn; };
template <> struct handler_function_type<Player> { static constexpr auto value = packet_handler::PlayerFn; };
template <> struct handler_function_type<Character> { static constexpr auto value = packet_handler::CharacterFn; };

template <typename TTarget, typename TPacket> void invoke_typed_handler(void_fn_t f, EOClient* client, const QueuedPacket& packet)
{
	TTarget* target = GetHandlerTarget<TTarget>(client);

	TPacket typed_packet;

	if (!DeserializePacket(client, packet, typed_packet))
		return;

	reinterpret_cast<void (*)(TTarget*, const TPacket&)>(f)(target, typed_packet);
}

template <PacketFamily family> class packet_handler_register_helper
{
	public:
		/**
		 * Registers a handler that takes a typed eolib client packet. The family and action come from the packet type.
		 */
		template <typename TTarget, typename TPacket>
		void Register(void (*f)(TTarget*, const TPacket&), unsigned short allow_states, double delay = 0.0) const
		{
			static_assert(static_cast<int>(TPacket::FAMILY) == static_cast<int>(family), "Packet type is registered under a different family");

			packet_handler_register_instance->Register(PacketFamily(TPacket::FAMILY), PacketAction(TPacket::ACTION),
				packet_handler(handler_function_type<TTarget>::value, reinterpret_cast<void_fn_t>(f), allow_states, delay, invoke_typed_handler<TTarget, TPacket>));
		}

		void Register(PacketAction action, client_handler_t f, unsigned short allow_states, double delay = 0.0) const
		{
			packet_handler_register_instance->Register(family, action, packet_handler(packet_handler::ClientFn, reinterpret_cast<void_fn_t>(f), allow_states, delay));
		}

		void Register(PacketAction action, player_handler_t f, unsigned short allow_states, double delay = 0.0) const
		{
			packet_handler_register_instance->Register(family, action, packet_handler(packet_handler::PlayerFn, reinterpret_cast<void_fn_t>(f), allow_states, delay));
		}

		void Register(PacketAction action, character_handler_t f, unsigned short allow_states, double delay = 0.0) const
		{
			packet_handler_register_instance->Register(family, action, packet_handler(packet_handler::CharacterFn, reinterpret_cast<void_fn_t>(f), allow_states, delay));
		}
};

inline void Handle(EOClient* client, const QueuedPacket& packet, bool from_queue = false)
{
	packet_handler_register_instance->Handle(client, packet, from_queue);
}

inline void SetDelay(PacketFamily family, PacketAction action, double delay)
{
	packet_handler_register_instance->SetDelay(family, action, delay);
}

}

#endif // HANDLERS_HPP_INCLUDED
