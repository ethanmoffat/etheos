
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef EOCLIENT_HPP_INCLUDED
#define EOCLIENT_HPP_INCLUDED

#include "fwd/eoclient.hpp"

#include "fwd/character.hpp"
#include "fwd/eodata.hpp"
#include "fwd/player.hpp"
#include "eoserver.hpp"
#include "packet.hpp"

#include "socket.hpp"

#include <eolib/packet/packet_sequencer.hpp>
#include <eolib/packet/sequence_start.hpp>
#include <eolib/protocol/net/enums.hpp>
#include <eolib/protocol/net/packet.hpp>

#include <cstddef>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <mutex>

/**
 * A packet received from a client, waiting to be handled
 */
struct QueuedPacket
{
	eolib::protocol::net::PacketFamily family;
	eolib::protocol::net::PacketAction action;

	// The packet data after the family, action and sequence number
	std::string payload;
};

/**
 * A packet the server will handle for the client
 */
struct ActionQueue_Action
{
	QueuedPacket packet;
	double time;
	bool auto_queue;

	ActionQueue_Action(QueuedPacket packet_, double time_, bool auto_queue_ = false)
		: packet(std::move(packet_))
		, time(time_)
		, auto_queue(auto_queue_)
	{ }
};

/**
 * A list of packets a client needs to eventually have handled for it
 */
class ActionQueue
{
	private:
		/**
		 * A pause in handling a client's packets, followed by a callback
		 */
		struct HoldState
		{
			// Number of packets queued before the hold, which are handled before the pause starts
			std::size_t ahead;

			// Whether the pause has started
			bool armed;

			double duration;

			// Client states (Handlers::AllowState) in which on_release still runs
			unsigned short allow_states;

			std::function<void(EOClient&)> on_release;
		};

		std::queue<std::unique_ptr<ActionQueue_Action>> queue;
		std::optional<HoldState> hold;

		// Earliest time the next action can be handled
		double next;

		/**
		 * Starts the pause once the packets ahead of the hold are handled, and ends it once the duration has passed.
		 */
		void PumpHold(EOClient* client, double now);

	public:
		void AddAction(QueuedPacket packet, double time, bool auto_queue = false);

		/**
		 * Pauses packet handling for the given number of seconds once the packets already queued are handled, then
		 * calls on_release if the client is still in one of allow_states. Packets queued after this call wait for it.
		 * Throws std::logic_error if a hold is already active.
		 */
		void Hold(double seconds, unsigned short allow_states, std::function<void(EOClient&)> on_release);

		/**
		 * Gets the number of queued packets, which is limited by PacketQueueMax. A hold isn't counted.
		 */
		std::size_t Size() const;

		/**
		 * Handles the next packet for the client, or starts or ends a hold, if it's due at the given time. Exceptions
		 * thrown by the handler or the hold's on_release are passed on to the caller.
		 */
		void Pump(EOClient* client, double now);

		ActionQueue() : next(0) {};

		~ActionQueue();
};

/**
 * A connection between an EO Client and EOSERV
 */
class EOClient : public Client
{
	public:
		enum PacketState
		{
			ReadLen1,
			ReadLen2,
			ReadData
		};

		enum ClientState
		{
			Uninitialized,
			Initialized,
			LoggedIn,
			Playing
		};

	private:
		void Initialize();
		EOClient();

		void LogPacket(PacketFamily family, PacketAction action, size_t sz, const char * const actionStr);

		FileType upload_type;
		std::FILE *upload_fh;
		std::size_t upload_pos;
		std::size_t upload_size;

		std::string send_buffer2;
		std::size_t send_buffer2_gpos;
		std::size_t send_buffer2_ppos;
		std::size_t send_buffer2_used;

		eolib::packet::PacketSequencer sequencer;

		// Sent with the last server ping, and used once the client replies to it
		std::optional<eolib::packet::PingSequenceStart> upcoming_sequence_start;

		std::mutex send_mutex;

		/**
		 * Encrypts, frames and sends a packet body ([action][family][data]).
		 */
		void SendBody(std::string body);

	public:
		EOServer *server() { return static_cast<EOServer *>(Client::server); };
		int version;
		Player *player;
		unsigned int id;
		unsigned short create_id;
		bool needpong;
		int hdid;
		ClientState state;
		double start;
		int login_attempts;

		ActionQueue queue;

		PacketState packet_state;
		unsigned char raw_length[2];
		unsigned int length;
		std::string data;

		// Multiple the server encrypts outgoing packets with (0 until Init_Init is handled)
		int server_encryption_multiple;

		// Multiple the client encrypts its packets with (0 until Init_Init is handled)
		int client_encryption_multiple;

		EOClient(EOServer* server_)
			: Client(server_)
			, sequencer(eolib::packet::ZeroSequenceStart())
		{
			this->Initialize();
		}

		EOClient(const Socket& sock, EOServer* server_)
			: Client(sock, server_)
			, sequencer(eolib::packet::ZeroSequenceStart())
		{
			this->Initialize();
		}

		virtual bool NeedTick();

		void Tick();

		/**
		 * Generates a new sequence start for the Init_Init reply and starts using it.
		 */
		eolib::packet::InitSequenceStart InitNewSequence();

		/**
		 * Generates the sequence start to send in a server ping. It's used once the client replies with Connection_Ping.
		 */
		eolib::packet::PingSequenceStart PingNewSequence();

		/**
		 * Generates a new sequence start for the Account_Reply sent after a successful Account_Request and starts using it.
		 */
		eolib::packet::AccountReplySequenceStart AccountReplyNewSequence();

		void NewCreateID();

		void Execute(const std::string &data);

		/**
		 * Encrypts a packet body ([action][family][data], without the length prefix) in place.
		 * Init_Init packets and a multiple of 0 are left unencrypted.
		 */
		static void EncryptPacket(std::string& body, int multiple);

		/**
		 * Decrypts a received packet (without the length prefix) in place.
		 * Init_Init packets and a multiple of 0 are left as they are.
		 */
		static void DecryptPacket(std::string& data, int multiple);

		bool Upload(FileType type, int id, InitReply init_reply);
		bool Upload(FileType type, const std::string &filename, InitReply init_reply);
		virtual void Send(const PacketBuilder &packet);

		/**
		 * Serializes and sends a packet. Packets that fail to serialize are logged and dropped.
		 */
		virtual void Send(const eolib::protocol::net::Packet& packet);

		virtual ~EOClient();
};

#endif // EOCLIENT_HPP_INCLUDED
