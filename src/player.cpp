
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "player.hpp"

#include "character.hpp"
#include "config.hpp"
#include "database.hpp"
#include "eoclient.hpp"
#include "world.hpp"

#include "console.hpp"
#include "hash.hpp"
#include "util.hpp"
#include "util/secure_string.hpp"

#include <algorithm>
#include <ctime>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

Player::Player(const std::string& username)
	: username(username)
{
	this->online = true;
	this->character = nullptr;
	this->client = nullptr;
	this->login_time = static_cast<int>(std::time(0));
	this->char_op_id = 0;
}

Player::Player(const std::string& username, World * world, Database * database)
{
	this->world = world;

	auto dbPointer = database ? database : this->world->db.get();

	Database_Result res = dbPointer->Query("SELECT `username`, `password` FROM `accounts` WHERE `username` = '$'", username.c_str());
	if (res.empty())
	{
		throw std::runtime_error("Player not found (" + username + ")");
	}
	std::unordered_map<std::string, util::variant> row = res.front();

	this->login_time = static_cast<int>(std::time(0));

	this->online = true;
	this->character = nullptr;

	this->username = static_cast<std::string>(row["username"]);

	res = dbPointer->Query("SELECT `name` FROM `characters` WHERE `account` = '$' ORDER BY `exp` DESC", username.c_str());

	UTIL_FOREACH_REF(res, row)
	{
		Character *newchar = new Character(row["name"], world);
		newchar->player = this;
		this->characters.push_back(newchar);
	}

	this->client = nullptr;
	this->char_op_id = 0;
}

bool Player::ValidName(std::string username)
{
	for (std::size_t i = 0; i < username.length(); ++i)
	{
		if (!((username[i] >= 'a' && username[i] <= 'z') || username[i] == ' ' || (username[i] >= '0' && username[i] <= '9')))
		{
			return false;
		}
	}

	return true;
}

bool Player::AddCharacter(std::string name, Gender gender, int hairstyle, int haircolor, Skin race)
{
	if (static_cast<int>(this->characters.size()) > static_cast<int>(this->world->config["MaxCharacters"]))
	{
		return false;
	}

	Character *newchar(this->world->CreateCharacter(this, name, gender, hairstyle, haircolor, race));

	if (!newchar)
	{
		return false;
	}

	newchar->player = this;

	if (this->world->admin_count == 0 && this->world->config["FirstCharacterAdmin"])
	{
		Console::Out("%s has been given HGM admin status!", newchar->real_name.c_str());
		newchar->admin = ADMIN_HGM;
		this->world->IncAdminCount();
	}

	this->characters.push_back(newchar);

	return true;
}

void Player::NewCharacterOp()
{
	this->char_op_id = this->world->GenerateOperationID([](const EOClient * c) { return c->player ? c->player->char_op_id : 0; });
}

AdminLevel Player::Admin() const
{
	AdminLevel admin = ADMIN_PLAYER;

	for (const Character* c : this->characters)
	{
		admin = std::max(admin, c->admin);
	}

	return admin;
}

void Player::Send(const PacketBuilder &builder)
{
	this->client->Send(builder);
}

void Player::Logout()
{
	UTIL_FOREACH(this->characters, character)
	{
		delete character;
	}
	this->characters.clear();

	if (this->client)
	{
#ifdef DEBUG
		Console::Dbg("Saving player '%s' (session lasted %i minutes)", this->username.c_str(), int(std::time(0) - this->login_time) / 60);
#endif // DEBUG
		this->world->db->Query("UPDATE `accounts` SET `lastused` = #, `hdid` = #, `lastip` = '$' WHERE username = '$'", int(std::time(0)), this->client->hdid, static_cast<std::string>(this->client->GetRemoteAddr()).c_str(), this->username.c_str());

		// Disconnect the client to make sure this null pointer is never dereferenced
		this->client->AsyncOpPending(false);
		this->client->Close();
		this->client->player = nullptr;
		this->client = nullptr; // Not reference counted!
	}
}

Player::~Player()
{
	this->Logout();
}
