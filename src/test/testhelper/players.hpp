#pragma once

#include "character.hpp"
#include "eoclient.hpp"
#include "player.hpp"
#include "world.hpp"

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
        character->gender = GENDER_FEMALE;
        character->hairstyle = 1;
        character->haircolor = 0;
        character->race = SKIN_WHITE;
        character->admin = ADMIN_PLAYER;
        character->paperdoll.fill(0);
        character->cosmetic_paperdoll.fill(0);
        player->characters.push_back(character);
        return character;
    }
};
