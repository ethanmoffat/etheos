
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

#include "util/secure_string.hpp"

#include "hash.hpp"
#include "socket.hpp"

#include <string>
#include <vector>

struct AccountCreateInfo
{
	std::string username;
	util::secure_string password;
	std::string fullname;
	std::string location;
	std::string email;
	std::string computer;
	int hdid;
	IPAddress remoteIp;

	AccountCreateInfo()
		: password(""), hdid(0) { }
};

struct PasswordChangeInfo
{
	std::string username;
	util::secure_string oldpassword;
	util::secure_string newpassword;

	PasswordChangeInfo()
		: oldpassword(""), newpassword("") { }
};

struct AccountCredentials
{
	std::string username;
	util::secure_string password;
	HashFunc hashFunc;

	AccountCredentials()
		: username(""), password(""), hashFunc(NONE) { }

	AccountCredentials(const std::string& username, util::secure_string&& password, HashFunc hashFunc)
		: username(username), password(password), hashFunc(hashFunc) { }
};

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
		bool AddCharacter(std::string name, Gender gender, int hairstyle, int haircolor, Skin race);

		void NewCharacterOp();

		AdminLevel Admin() const;

		void Send(const PacketBuilder &);

		void Logout();

		World *world;
		EOClient *client;

		~Player();
};


#endif // PLAYER_HPP_INCLUDED
