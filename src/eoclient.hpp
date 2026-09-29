
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

#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <mutex>

/**
 * An action the server will execute for the client
 */
struct ActionQueue_Action
{
	PacketReader reader;
	double time;
	bool auto_queue;

	ActionQueue_Action(PacketReader reader_, double time_, bool auto_queue_ = false)
		: reader(reader_)
		, time(time_)
		, auto_queue(auto_queue_)
	{ }
};

/**
 * A list of actions a client needs to eventually have executed for it
 */
class ActionQueue
{
	private:
		std::queue<std::unique_ptr<ActionQueue_Action>> queue;

		// Earliest time the next action can be handled
		double next;

	public:
		void AddAction(const PacketReader& reader, double time, bool auto_queue = false);

		/**
		 * Gets the number of queued actions, which is limited by PacketQueueMax.
		 */
		std::size_t Size() const;

		/**
		 * Handles the next action for the client if it's due at the given time. Exceptions thrown by the handler are
		 * passed on to the caller.
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

		virtual ~EOClient();
};

#endif // EOCLIENT_HPP_INCLUDED
