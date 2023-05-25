
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef WORLD_HPP_INCLUDED
#define WORLD_HPP_INCLUDED

#include "fwd/world.hpp"

#include "fwd/character.hpp"
#include "fwd/command_source.hpp"
#include "fwd/eodata.hpp"
#include "fwd/eoserver.hpp"
#include "fwd/guild.hpp"
#include "fwd/map.hpp"
#include "fwd/npc_data.hpp"
#include "fwd/party.hpp"
#include "fwd/player.hpp"
#include "fwd/quest.hpp"
#include "config.hpp"
#include "database.hpp"
#include "i18n.hpp"
#include "hash.hpp"
#include "map.hpp"
#include "loginmanager.hpp"
#include "timer.hpp"

#include "fwd/socket.hpp"
#include "util/secure_string.hpp"
#include "util/async.hpp"

#include <array>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Board_Post
{
	short id;
	std::string author;
	int author_admin;
	std::string subject;
	std::string body;
	double time;
};

struct Board
{
	int id;
	short last_id;
	std::list<Board_Post *> posts;

	Board(int id_) : id(id_), last_id(0) { }
};

struct Home
{
	std::string id;
	std::string name;
	short map;
	unsigned char x;
	unsigned char y;
	int level;
	int race;
	int innkeeper_vend;
	short sleep_map;
	unsigned char sleep_x;
	unsigned char sleep_y;

	Home() : map(1), x(0), y(0),
			 level(-1), race(-1), innkeeper_vend(0),
			 sleep_map(0), sleep_x(0), sleep_y(0) { }
};

/**
 * Object which holds and manages all maps and characters on the server, as well as timed events
 * Only one of these should exist per server
 */
class World
{
	private:
		std::unordered_map<HashFunc, std::shared_ptr<Hasher>> passwordHashers;
		std::unique_ptr<LoginManager> loginManager;
		std::shared_ptr<DatabaseFactory> databaseFactory;

	protected:
		int last_character_id;

		void UpdateConfig();

	public:
		Timer timer;

		EOServer *server;
		std::shared_ptr<Database> db;

		GuildManager *guildmanager;

		EIF *eif;
		ENF *enf;
		ESF *esf;
		ECF *ecf;

		std::vector<std::unique_ptr<NPC_Data>> npc_data;

		Config config;
		Config admin_config;
		Config drops_config;
		Config shops_config;
		Config arenas_config;
		Config formulas_config;
		Config home_config;
		Config skills_config;
		Config speech_config;

		I18N i18n;

		std::vector<Character *> characters;
		std::vector<Party *> parties;
		std::vector<Map *> maps;
		std::vector<std::shared_ptr<Home>> homes;
		std::map<short, std::shared_ptr<Quest>> quests;

		std::array<Board *, 8> boards;

		std::array<int, 254> exp_table;
		std::vector<int> instrument_ids;

		int admin_count;

		World(std::shared_ptr<DatabaseFactory> databaseFactory, const Config &eoserv_config, const Config &admin_config);

		void Initialize();

		void DumpToFile(const std::string& fileName);
		void RestoreFromDump(const std::string& fileName);

		void UpdateAdminCount(int admin_count);
		void IncAdminCount() { UpdateAdminCount(this->admin_count + 1); }
		void DecAdminCount() { UpdateAdminCount(this->admin_count - 1); }

		void Command(std::string command, const std::vector<std::string>& arguments, Command_Source* from = 0);

		void LoadHome();

		int GenerateCharacterID();
		unsigned short GenerateOperationID(std::function<unsigned short(const EOClient *)> get_id) const;
		int GenerateClientID();

		void Login(Character *);
		void Logout(Character *);

		void Msg(Command_Source *from, std::string message, bool echo = true);
		void AdminMsg(Command_Source *from, std::string message, int minlevel = ADMIN_GUARDIAN, bool echo = true);
		void AnnounceMsg(Command_Source *from, std::string message, bool echo = true);
		void ServerMsg(std::string message);
		void AdminReport(Character *from, std::string reportee, std::string message);
		void AdminRequest(Character *from, std::string message);

		void Reboot();
		void Reboot(int seconds, std::string reason);

		void Rehash();
		void ReloadPub(bool quiet = false);
		void ReloadQuests();

		void Kick(Command_Source *from, Character *victim, bool announce = true);
		void Jail(Command_Source *from, Character *victim, bool announce = true);
		void Unjail(Command_Source *from, Character *victim);
		void Ban(Command_Source *from, Character *victim, int duration, bool announce = true);
		void Mute(Command_Source *from, Character *victim, bool announce = true);

		int CheckBan(const std::string *username, const IPAddress *address, const int *hdid);

		Character *GetCharacter(std::string name);
		Character *GetCharacterReal(std::string real_name);
		Character *GetCharacterPID(unsigned int id);
		Character *GetCharacterCID(unsigned int id);

		Map *GetMap(short id);
		const NPC_Data* GetNpcData(short id) const;
		std::shared_ptr<Home> GetHome(const Character *) const;
		std::shared_ptr<Home> GetHome(std::string);

		bool CharacterExists(std::string name);
		Character *CreateCharacter(Player *, std::string name, Gender, int hairstyle, int haircolor, Skin);
		void DeleteCharacter(std::string name);

		Player *PlayerFactory(std::string username);
		AsyncOperation<AccountCredentials, LoginReply>* CheckCredential(EOClient* client);
		AsyncOperation<PasswordChangeInfo, bool>* ChangePassword(EOClient* client);

		AsyncOperation<AccountCreateInfo, bool>* CreateAccount(EOClient* client);

		bool PlayerExists(std::string username);
		bool PlayerOnline(std::string username);

		bool PKExcept(const Map *map);
		bool PKExcept(int mapid);

		bool IsInstrument(int graphic_id);

		~World();
};

#endif // WORLD_HPP_INCLUDED
