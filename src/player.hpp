
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef PLAYER_HPP_INCLUDED
#define PLAYER_HPP_INCLUDED

#include "fwd/player.hpp"

#include "fwd/character.hpp"
#include "fwd/database.hpp"
#include "fwd/eoclient.hpp"
#include "fwd/packet.hpp"
#include "fwd/world.hpp"

#include <eolib/protocol/net/packet.hpp>

#include <string>
#include <vector>

/**
 * Object representing a player, but not a character
 */
class Player
{
	public:
		int login_time;
		bool online;
		unsigned int id;
		unsigned short char_op_id;
		std::string username;

		std::string dutylast;

		Player(const std::string& username);
		Player(const std::string& username, World *, Database * = nullptr);

		std::vector<Character *> characters;
		Character *character;

		static bool ValidName(std::string username);
		bool AddCharacter(std::string name, eolib::protocol::Gender gender, int hairstyle, int haircolor, int race);

		void NewCharacterOp();

		eolib::protocol::AdminLevel Admin() const;

		void Send(const PacketBuilder &);
		void Send(const eolib::protocol::net::Packet& packet);

		void Logout();

		World *world;
		EOClient *client;

		~Player();
};


#endif // PLAYER_HPP_INCLUDED
