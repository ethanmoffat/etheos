#pragma once

#include "character.hpp"
#include "eoclient.hpp"
#include "player.hpp"
#include "world.hpp"

#include <ctime>
#include <memory>
#include <string>

// A logged in player attached to a client, without the database lookups of the full Player constructor
class TestPlayer
{
public:
    std::unique_ptr<Player> player;

    TestPlayer(World* world, EOClient* client, const std::string& username = "test_user")
        : player(new Player(username))
    {
        player->world = world;
        player->client = client;
        player->id = client->id;
        client->player = player.get();
    }

    ~TestPlayer()
    {
        // Logout would save the account and close the (mock) client
        player->client->player = nullptr;
        player->client = nullptr;
    }

    // Adds a character to the player's character list. The player owns (and deletes) it.
    Character* AddCharacter(unsigned int id, const std::string& name)
    {
        Character* character = new Character(player->world);
        character->id = id;
        character->real_name = name;
        character->player = player.get();
        character->level = 0;
        character->exp = 0;
        character->clas = 0;
        character->str = character->intl = character->wis = character->agi = character->con = character->cha = 0;
        character->adj_str = character->adj_intl = character->adj_wis = 0;
        character->adj_agi = character->adj_con = character->adj_cha = 0;
        character->statpoints = character->skillpoints = 0;
        character->karma = 0;
        character->weight = character->maxweight = 0;
        character->nointeract = 0;
        character->hp = character->maxhp = 0;
        character->tp = character->maxtp = 0;
        character->gender = eolib::protocol::Gender::Female;
        character->hairstyle = 1;
        character->haircolor = 0;
        character->race = 0;
        character->admin = eolib::protocol::AdminLevel::Player;
        character->party = nullptr;
        character->arena = nullptr;
        character->next_arena = nullptr;
        character->arena_kills = 0;
        character->trading = false;
        character->trade_partner = nullptr;
        character->party_trust_send = nullptr;
        character->party_trust_recv = nullptr;
        character->muted_until = 0;
        character->npc = nullptr;
        character->board = nullptr;
        character->spell_event = nullptr;
        character->spell_ready = false;
        character->spell_id = 0;
        character->spell_target = Character::TargetInvalid;
        character->spell_target_id = 0;
        character->online = false;
        character->nowhere = false;
        character->whispers = true;
        character->bot = false;
        character->hidden = 0;
        character->trade_agree = false;
        character->jukebox_open = false;
        character->usage = 0;
        character->login_time = static_cast<int>(std::time(nullptr));
        character->bankmax = 0;
        character->goldbank = 0;
        character->mindam = character->maxdam = 0;
        character->accuracy = character->evade = character->armor = 0;
        character->warp_effect = Character::NoPendingWarp;
        character->warp_session_id = 0;
        character->npc_session_id = 0;
        character->map = nullptr;
        character->paperdoll.fill(0);
        character->cosmetic_paperdoll.fill(0);
        player->characters.push_back(character);
        return character;
    }
};
