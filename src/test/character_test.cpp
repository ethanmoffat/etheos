#include "testhelper/playing.hpp"

#include <eolib/protocol/net/server/packets.hpp>

#include <memory>

namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short CharacterSpawnTestServerPort = 38109;
constexpr int CharacterSpawnTestMapId = 14;

constexpr unsigned short CharacterAvatarTestServerPort = 38113;
constexpr int CharacterAvatarTestMapId = 18;

class CharacterSpawnTests : public PlayingTest
{
protected:
    CharacterSpawnTests()
        : PlayingTest(CharacterSpawnTestServerPort, CharacterSpawnTestMapId)
    {
    }
};

class CharacterAvatarTests : public PlayingTest
{
protected:
    CharacterAvatarTests()
        : PlayingTest(CharacterAvatarTestServerPort, CharacterAvatarTestMapId)
    {
    }
};

}

TEST_F(CharacterSpawnTests, SpawnLoc_AssignedHome_ReturnsHomeLocation)
{
    auto home = std::make_shared<Home>();
    home->id = "testhome";
    home->map = CharacterSpawnTestMapId;
    home->loc.x = 4;
    home->loc.y = 7;
    server->world->homes.push_back(home);

    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& playing_character = GivenCharacter(1, 1);
    playing_character.character->home = "testhome";

    const eolib::protocol::Coords spawn_loc = playing_character.character->SpawnLoc();

    EXPECT_EQ(CharacterSpawnTestMapId, playing_character.character->SpawnMap());
    EXPECT_EQ(4, spawn_loc.x);
    EXPECT_EQ(7, spawn_loc.y);
}

TEST_F(CharacterAvatarTests, Dress_CosmeticArmor_SendsEquipmentChangeToNearby)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& dresser = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 3);
    PlayingCharacter& far = GivenCharacter(30, 30);

    dresser.character->Dress(Character::Armor, 12);

    for (const PlayingCharacter* entry : {&dresser, &observer})
    {
        const auto avatar_agree = LastSent<server::AvatarAgreeServerPacket>(*entry);
        ASSERT_TRUE(avatar_agree);
        EXPECT_EQ(dresser.character->PlayerID(), avatar_agree->change.player_id);
        EXPECT_FALSE(avatar_agree->change.sound);
        ASSERT_EQ(server::AvatarChangeType::Equipment, avatar_agree->change.change_type);
        EXPECT_EQ(12, avatar_agree->change.AsEquipment()->equipment.armor);
        EXPECT_EQ(0, avatar_agree->change.AsEquipment()->equipment.boots);
    }

    EXPECT_FALSE(LastSent<server::AvatarAgreeServerPacket>(far));
}

TEST_F(CharacterAvatarTests, Undress_SendsBareEquipmentChange)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& dresser = GivenCharacter(2, 2);
    dresser.character->cosmetic_paperdoll[Character::Hat] = 7;

    dresser.character->Undress();

    const auto avatar_agree = LastSent<server::AvatarAgreeServerPacket>(dresser);
    ASSERT_TRUE(avatar_agree);
    ASSERT_EQ(server::AvatarChangeType::Equipment, avatar_agree->change.change_type);
    EXPECT_EQ(0, avatar_agree->change.AsEquipment()->equipment.hat);
}

GTEST_TEST(CharacterSpellsTests, SpellUnserialize_SerializedList_RoundTrips)
{
    const auto spells = SpellUnserialize("7,3;300,0;");

    ASSERT_EQ(2u, spells.size());
    EXPECT_EQ(7, spells.front().id);
    EXPECT_EQ(3, spells.front().level);
    EXPECT_EQ(300, spells.back().id);
    EXPECT_EQ(0, spells.back().level);
    EXPECT_EQ("7,3;300,0;", SpellSerialize(spells));
}

GTEST_TEST(CharacterSpellsTests, SpellUnserialize_InvalidEntries_AreDiscarded)
{
    const auto spells = SpellUnserialize("0,1;70000,1;5,-1;6;8,2;");

    ASSERT_EQ(1u, spells.size());
    EXPECT_EQ(8, spells.front().id);
    EXPECT_EQ(2, spells.front().level);
}
