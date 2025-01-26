
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../eoclient.hpp"
#include "../map.hpp"
#include "../npc.hpp"
#include "../packet.hpp"
#include "../player.hpp"
#include "../world.hpp"

#include "../console.hpp"
#include "../util.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace Handlers
{

// Selected a character
void Welcome_Request(Player *player, PacketReader &reader)
{
	if (player->character)
		throw std::runtime_error("Character already selected");

	unsigned int id = reader.GetInt(); // Character ID

	auto it = std::find_if(UTIL_CRANGE(player->characters), [&](Character *c) -> bool
	{
		return c->id == id;
	});

	if (it == player->characters.end())
		return;

	player->character = *it;
	player->character->CalculateStats();

	std::string guild_str = player->character->GuildNameString();
	std::string guild_rank = player->character->GuildRankString();

	PacketBuilder reply(PACKET_WELCOME, PACKET_REPLY,
		114 + player->character->paperdoll.size() * 2 + player->character->SourceName().length() + player->character->title.length()
		+ guild_str.length() + guild_rank.length());

	reply.AddShort(WELCOME_GRANTED); // REPLY_WELCOME sub-id
	reply.AddShort(player->id);
	reply.AddInt(player->character->id);
	reply.AddShort(player->character->mapid); // Map ID

	if (player->world->config["GlobalPK"] && !player->world->PKExcept(player->character->mapid))
	{
		reply.AddByte(0xFF);
		reply.AddByte(0x01);
	}
	else
	{
		reply.AddByte(player->world->GetMap(player->character->mapid)->rid[0]);
		reply.AddByte(player->world->GetMap(player->character->mapid)->rid[1]);
	}

	reply.AddByte(player->world->GetMap(player->character->mapid)->rid[2]);
	reply.AddByte(player->world->GetMap(player->character->mapid)->rid[3]);
	reply.AddThree(player->world->GetMap(player->character->mapid)->filesize);
	reply.AddByte(player->world->eif->rid[0]);
	reply.AddByte(player->world->eif->rid[1]);
	reply.AddByte(player->world->eif->rid[2]);
	reply.AddByte(player->world->eif->rid[3]);
	reply.AddByte(player->world->eif->len[0]);
	reply.AddByte(player->world->eif->len[1]);
	reply.AddByte(player->world->enf->rid[0]);
	reply.AddByte(player->world->enf->rid[1]);
	reply.AddByte(player->world->enf->rid[2]);
	reply.AddByte(player->world->enf->rid[3]);
	reply.AddByte(player->world->enf->len[0]);
	reply.AddByte(player->world->enf->len[1]);
	reply.AddByte(player->world->esf->rid[0]);
	reply.AddByte(player->world->esf->rid[1]);
	reply.AddByte(player->world->esf->rid[2]);
	reply.AddByte(player->world->esf->rid[3]);
	reply.AddByte(player->world->esf->len[0]);
	reply.AddByte(player->world->esf->len[1]);
	reply.AddByte(player->world->ecf->rid[0]);
	reply.AddByte(player->world->ecf->rid[1]);
	reply.AddByte(player->world->ecf->rid[2]);
	reply.AddByte(player->world->ecf->rid[3]);
	reply.AddByte(player->world->ecf->len[0]);
	reply.AddByte(player->world->ecf->len[1]);
	reply.AddBreakString(player->character->SourceName());
	reply.AddBreakString(player->character->title);
	reply.AddBreakString(guild_str); // Guild Name
	reply.AddBreakString(guild_rank); // Guild Rank
	reply.AddChar(player->character->clas);
	reply.AddString(player->character->PaddedGuildTag());

	// Tell a player's client they're a higher level admin than they are to enable some features

	AdminLevel lowest_command = ADMIN_HGM;

	UTIL_FOREACH(player->world->admin_config, ac)
	{
		if (ac.first == "killnpc" || ac.first == "reports")
		{
			continue;
		}

		lowest_command = std::min<AdminLevel>(lowest_command, static_cast<AdminLevel>(util::to_int(ac.second)));
	}

	if (player->character->SourceAccess() >= static_cast<int>(player->world->admin_config["seehide"])
	 && player->character->SourceAccess() < ADMIN_HGM)
	{
		reply.AddChar(ADMIN_HGM);
	}
	else if (player->character->SourceDutyAccess() >= static_cast<int>(player->world->admin_config["nowall"])
	 && player->character->SourceDutyAccess() < ADMIN_GM)
	{
		reply.AddChar(ADMIN_GM);
	}
	else if (player->character->SourceAccess() >= lowest_command && player->character->SourceAccess() < ADMIN_GUIDE)
	{
		reply.AddChar(ADMIN_GUIDE);
	}
	else
	{
		reply.AddChar(player->character->SourceAccess());
	}

	reply.AddChar(player->character->level);
	reply.AddInt(player->character->exp);
	reply.AddInt(player->character->usage);
	reply.AddShort(player->character->hp);
	reply.AddShort(player->character->maxhp);
	reply.AddShort(player->character->tp);
	reply.AddShort(player->character->maxtp);
	reply.AddShort(player->character->maxsp);
	reply.AddShort(player->character->statpoints);
	reply.AddShort(player->character->skillpoints);
	reply.AddShort(player->character->karma);
	reply.AddShort(player->character->mindam);
	reply.AddShort(player->character->maxdam);
	reply.AddShort(player->character->accuracy);
	reply.AddShort(player->character->evade);
	reply.AddShort(player->character->armor);

	reply.AddShort(player->character->display_str);
	reply.AddShort(player->character->display_wis);
	reply.AddShort(player->character->display_intl);
	reply.AddShort(player->character->display_agi);
	reply.AddShort(player->character->display_con);
	reply.AddShort(player->character->display_cha);

	UTIL_FOREACH(player->character->paperdoll, item)
	{
		reply.AddShort(item);
	}

	int leader_rank = std::max(std::max(std::max(static_cast<int>(player->world->config["GuildEditRank"]), static_cast<int>(player->world->config["GuildKickRank"])),
							   static_cast<int>(player->world->config["GuildPromoteRank"])), static_cast<int>(player->world->config["GuildDemoteRank"]));

	if (player->character->guild_rank <= leader_rank && player->character->guild)
	{
		reply.AddChar(1); // Allows client access to the guild management tools
	}
	else
	{
		reply.AddChar(player->character->guild_rank);
	}

	reply.AddShort(static_cast<int>(player->world->config["JailMap"]));
	reply.AddShort(4); // Recover map
	reply.AddChar(24); // Recover map x
	reply.AddChar(24); // Recover map y
	reply.AddShort(0); // Light guide admin command flood rate
	reply.AddShort(0); // Guardian admin command flood rate
	reply.AddShort(0); // GM/HGM admin command flood rate
	reply.AddShort(2); // ?
	reply.AddChar((player->character->usage == 0) ? 2 : 0); // Login warning message
	reply.AddByte(255);

	player->Send(reply);
}

// Welcome message after you login.
void Welcome_Msg(Player *player, PacketReader &reader)
{
	auto player_id = reader.GetThree();
	auto character_id = reader.GetInt();

	if (player_id != player->id || player->character == nullptr || player->character->id != character_id)
	{
		PacketBuilder builder = PacketBuilder(PACKET_WELCOME, PACKET_REPLY, 4)
			.AddShort(WELCOME_ERROR)
			.AddString("NO");
		player->client->Send(builder);
		player->client->Close();
		return;
	}

	if (!player->character->world->GetMap(player->character->mapid)->exists)
	{
		if (player->character->world->GetMap(player->character->SpawnMap())->exists)
		{
			Console::Wrn("Player logged in to non-existent map (%s, map %i) - Position reset", player->character->real_name.c_str(), player->character->mapid);
			player->character->Warp(player->character->SpawnMap(), player->character->SpawnX(), player->character->SpawnY());
		}
		else
		{
			Console::Wrn("Player logged in to non-existent map (%s, map %i) - Disconnected", player->character->real_name.c_str(), player->character->mapid);
			player->client->Close();
			return;
		}
	}

	player->world->Login(player->character);

	player->client->state = EOClient::Playing;

	std::vector<Character *> updatecharacters;
	std::vector<NPC *> updatenpcs;
	std::vector<std::shared_ptr<Map_Item>> updateitems;

	UTIL_FOREACH(player->character->map->characters, character)
	{
		if (player->character->InRange(character))
		{
			updatecharacters.push_back(character);
		}
	}

	UTIL_FOREACH(player->character->map->npcs, npc)
	{
		if (player->character->InRange(npc))
		{
			updatenpcs.push_back(npc);
		}
	}

	UTIL_FOREACH(player->character->map->items, item)
	{
		if (player->character->InRange(*item))
		{
			updateitems.push_back(item);
		}
	}

	PacketBuilder reply(PACKET_WELCOME, PACKET_REPLY, 3 + 9);

	reply.AddShort(WELCOME_COMPLETED);
	// MotDs
	reply.AddByte(255);

	std::string news_file = player->world->config["NewsFile"];

	char newsbuf[4096] = "";
	std::FILE *newsfh = std::fopen(news_file.c_str(), "rt");
	bool newseof = (newsfh == 0);

	if (newseof)
	{
		Console::Wrn("Could not load news file '%s'", news_file.c_str());
	}

	for (int i = 0; i < 9; ++i)
	{
		if (!newseof)
		{
			if (!std::fgets(newsbuf, 4096, newsfh))
			{
				newseof = true;
			}
			else
			{
				std::string str = util::trim(newsbuf);

				while (str.length() < 2)
					str += ' ';

				reply.ReserveMore(str.length() + 9 - i);
				reply.AddBreakString(str);
			}
		}

		if (newseof)
		{
			reply.AddByte(255);
		}
	}

	if (newsfh)
	{
		std::fclose(newsfh);
	}

	reply.ReserveMore(7 + player->character->inventory.size() * 6 + player->character->spells.size() * 4
		+ updatecharacters.size() * 60 + updatenpcs.size() * 6 + updateitems.size() * 9);

	// ??
	reply.AddChar(static_cast<unsigned char>(player->character->weight));
	reply.AddChar(static_cast<unsigned char>(player->character->maxweight));
	UTIL_FOREACH(player->character->inventory, item)
	{
		reply.AddShort(item.id);
		reply.AddInt(item.amount);
	}
	reply.AddByte(255);
	UTIL_FOREACH(player->character->spells, spell)
	{
		reply.AddShort(spell.id);
		reply.AddShort(spell.level);
	}
	reply.AddByte(255);

	reply.AddChar(static_cast<unsigned char>(updatecharacters.size()));
	reply.AddByte(255);
	UTIL_FOREACH(updatecharacters, character)
	{
		reply.AddBreakString(character->SourceName());
		reply.AddShort(character->PlayerID());
		reply.AddShort(character->mapid);
		reply.AddShort(character->x);
		reply.AddShort(character->y);
		reply.AddChar(character->direction);
		reply.AddChar(6); // ?
		reply.AddString(character->PaddedGuildTag());
		reply.AddChar(character->level);
		reply.AddChar(character->gender);
		reply.AddChar(character->hairstyle);
		reply.AddChar(character->haircolor);
		reply.AddChar(character->race);
		reply.AddShort(character->maxhp);
		reply.AddShort(character->hp);
		reply.AddShort(character->maxtp);
		reply.AddShort(character->tp);
		// equipment
		character->AddPaperdollData(reply, "B000A0HSW");

		reply.AddChar(character->sitting);
		reply.AddChar(character->IsHideInvisible());
		reply.AddByte(255);
	}
	UTIL_FOREACH(updatenpcs, npc)
	{
		if (npc->alive)
		{
			reply.AddChar(npc->index);
			reply.AddShort(npc->id);
			reply.AddChar(npc->x);
			reply.AddChar(npc->y);
			reply.AddChar(npc->direction);
		}
	}
	reply.AddByte(255);
	UTIL_FOREACH(updateitems, item)
	{
		reply.AddShort(item->uid);
		reply.AddShort(item->id);
		reply.AddChar(item->x);
		reply.AddChar(item->y);
		reply.AddThree(item->amount);
	}
	player->Send(reply);
}

// Client wants a file
void Welcome_Agree(Player *player, PacketReader &reader)
{
	FileType file = FileType(reader.GetChar());

	auto playerId = reader.GetShort();
	if (playerId != player->id)
	{
		player->client->Close();
		return;
	}

	short id = reader.Remaining() == 1
		? reader.GetChar()
		: reader.GetShort();

	bool result = false;

	switch (file)
	{
		case FILE_MAP:
			result = player->client->Upload(FILE_MAP, id, INIT_FILE_MAP); break;
		case FILE_ITEM:
			result = player->client->Upload(FILE_ITEM, id, INIT_FILE_EIF); break;
		case FILE_NPC:
			result = player->client->Upload(FILE_NPC, id, INIT_FILE_ENF); break;
		case FILE_SPELL:
			result = player->client->Upload(FILE_SPELL, id, INIT_FILE_ESF); break;
		case FILE_CLASS:
			result = player->client->Upload(FILE_CLASS, id, INIT_FILE_ECF); break;
		default: return;
	}

	if (!result)
		throw std::runtime_error("Failed to upload file");
}

PACKET_HANDLER_REGISTER(PACKET_WELCOME)
	Register(PACKET_REQUEST, Welcome_Request, Character_Menu);
	Register(PACKET_MSG, Welcome_Msg, Logging_In);
	Register(PACKET_AGREE, Welcome_Agree, Logging_In, 0.15);
PACKET_HANDLER_REGISTER_END(PACKET_WELCOME)

}
