#pragma once

#include "mocks.hpp"
#include "players.hpp"
#include "setup.hpp"

#include "character.hpp"
#include "console.hpp"
#include "eoclient.hpp"
#include "eoserver.hpp"
#include "map.hpp"
#include "world.hpp"

#include <eolib/data/eo_reader.hpp>
#include <eolib/data/eo_writer.hpp>
#include <eolib/protocol/map/structs.hpp>
#include <eolib/protocol/net/packet.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// A server with one test map, and characters that are in game on it
class PlayingTest : public testing::Test
{
protected:
    struct SentPacket
    {
        eolib::protocol::net::PacketFamily family;
        eolib::protocol::net::PacketAction action;
        std::vector<std::uint8_t> payload;
    };

    struct PlayingCharacter
    {
        std::unique_ptr<testing::NiceMock<MockClient>> client;
        std::unique_ptr<TestPlayer> test_player;
        Character* character;
        std::vector<SentPacket> sent;
    };

    const unsigned short port;
    const int map_id;
    std::unique_ptr<EOServer> server;
    Map* map = nullptr;
    std::vector<std::unique_ptr<PlayingCharacter>> playing;

    PlayingTest(unsigned short port, int map_id)
        : port(port)
        , map_id(map_id)
    {
    }

    void SetUp() override
    {
        Console::SuppressOutput(true);

        Config config, admin_config;
        CreateConfigWithTestDefaults(config, admin_config);
        ConfigureServer(config);
        server = std::make_unique<EOServer>(IPAddress("127.0.0.1"), port, CreateMockDatabaseFactory(CreateMockDatabase()), config, admin_config);
        server->world->config["MapDir"] = "./";
    }

    virtual void ConfigureServer(Config&)
    {
    }

    void TearDown() override
    {
        for (const auto& entry : playing)
        {
            map->characters.remove(entry->character);
            auto& online = server->world->characters;
            online.erase(std::remove(online.begin(), online.end(), entry->character), online.end());
        }

        playing.clear();
        server.reset();
        std::remove(MapFile().c_str());
    }

    std::string MapFile() const
    {
        char name[16];
        std::snprintf(name, sizeof name, "%05i.emf", map_id);
        return name;
    }

    static eolib::protocol::map::Emf EmptyEmf(int width, int height)
    {
        eolib::protocol::map::Emf file;
        // Map::Reload skips loading a file with the same RID as the unloaded map
        file.rid = {1, 2};
        file.width = width - 1;
        file.height = height - 1;
        file.graphic_layers.resize(9);
        return file;
    }

    static void AddTileSpec(eolib::protocol::map::Emf& file, int x, int y, eolib::protocol::map::MapTileSpec spec)
    {
        eolib::protocol::map::MapTileSpecRowTile tile;
        tile.x = x;
        tile.tile_spec = spec;
        eolib::protocol::map::MapTileSpecRow row;
        row.y = y;
        row.tiles = {tile};
        file.tile_spec_rows.push_back(row);
    }

    // door is 0 for no door, 1 for an unlocked door, and 2+ for a door that needs key number door
    void AddWarp(eolib::protocol::map::Emf& file, int x, int y, int door) const
    {
        eolib::protocol::map::MapWarpRowTile tile;
        tile.x = x;
        tile.warp.destination_map = map_id;
        tile.warp.door = door;
        eolib::protocol::map::MapWarpRow row;
        row.y = y;
        row.tiles = {tile};
        file.warp_rows.push_back(row);
    }

    template <typename TFile> static void WriteFile(const std::string& path, const TFile& file)
    {
        eolib::data::EoWriter writer;
        file.Serialize(writer);
        const std::vector<std::uint8_t> content = std::move(writer).ToByteArray();

        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(content.data()), content.size());
    }

    void GivenMap(const eolib::protocol::map::Emf& file)
    {
        WriteFile(MapFile(), file);

        map = server->world->GetMap(map_id);
        ASSERT_TRUE(map->Reload());
    }

    // Adds a character standing on the test map. Typed packets sent to it are recorded in sent.
    PlayingCharacter& GivenCharacter(int x, int y)
    {
        auto entry = std::make_unique<PlayingCharacter>();
        const int number = static_cast<int>(playing.size()) + 1;

        entry->client = std::make_unique<testing::NiceMock<MockClient>>(server.get());
        entry->client->state = EOClient::Playing;
        entry->client->id = number;
        entry->test_player = std::make_unique<TestPlayer>(server->world, entry->client.get(), "user" + std::to_string(number));

        Character* character = entry->test_player->AddCharacter(number, "character" + std::to_string(number));
        character->player->character = character;
        character->map = map;
        character->mapid = map_id;
        character->loc.x = x;
        character->loc.y = y;
        character->direction = Direction::Down;
        character->sitting = SitState::Stand;
        character->nowhere = false;
        character->hidden = 0;
        character->last_walk = 0.0;
        character->attacks = 0;
        entry->character = character;
        map->characters.push_back(character);

        std::vector<SentPacket>& sent = entry->sent;
        ON_CALL(*entry->client, Send(testing::An<const eolib::protocol::net::Packet&>()))
            .WillByDefault([&sent](const eolib::protocol::net::Packet& packet)
            {
                eolib::data::EoWriter writer;
                packet.Serialize(writer);
                sent.push_back(SentPacket{packet.Family(), packet.Action(), std::move(writer).ToByteArray()});
            });

        playing.push_back(std::move(entry));
        return *playing.back();
    }

    // Adds a character to the world's online list, for world-wide lookups and messages
    PlayingCharacter& GivenOnlineCharacter(int x, int y)
    {
        PlayingCharacter& entry = GivenCharacter(x, y);
        server->world->characters.push_back(entry.character);
        return entry;
    }

    // Gets the last packet of the given type that was sent to a character
    template <typename TPacket> static std::optional<TPacket> LastSent(const PlayingCharacter& entry)
    {
        for (auto it = entry.sent.rbegin(); it != entry.sent.rend(); ++it)
        {
            if (it->family != TPacket::FAMILY || it->action != TPacket::ACTION)
                continue;

            eolib::data::EoReader reader(it->payload);
            TPacket packet;
            packet.Deserialize(reader);
            return packet;
        }

        return std::nullopt;
    }

    // Gets every packet of the given type that was sent to a character, oldest first
    template <typename TPacket> static std::vector<TPacket> AllSent(const PlayingCharacter& entry)
    {
        std::vector<TPacket> packets;

        for (const SentPacket& sent : entry.sent)
        {
            if (sent.family != TPacket::FAMILY || sent.action != TPacket::ACTION)
                continue;

            eolib::data::EoReader reader(sent.payload);
            packets.emplace_back().Deserialize(reader);
        }

        return packets;
    }
};
