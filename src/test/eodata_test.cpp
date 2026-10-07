#include <gtest/gtest.h>

#include "eodata.hpp"

#include "console.hpp"

#include <eolib/data/eo_writer.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace pub = eolib::protocol::pub;

namespace
{

class EODataTests : public testing::Test
{
protected:
    const std::string filename = "eodata_test.pub";

    void SetUp() override
    {
        Console::SuppressOutput(true);
    }

    void TearDown() override
    {
        std::remove(filename.c_str());
    }

    void GivenFile(const std::vector<std::uint8_t>& content)
    {
        std::ofstream file(filename, std::ios::binary);
        file.write(reinterpret_cast<const char*>(content.data()), content.size());
    }

    void GivenFile(const eolib::protocol::Serializable& object)
    {
        eolib::data::EoWriter writer;
        object.Serialize(writer);
        GivenFile(writer.Data());
    }
};

pub::EifRecord EifRecord(const std::string& name)
{
    pub::EifRecord record;
    record.name = name;
    return record;
}

}

TEST_F(EODataTests, EIF_Read_LoadsRecordsFromId1)
{
    pub::EifRecord sword = EifRecord("Sword");
    sword.graphic_id = 12;
    sword.type = pub::ItemType::Weapon;
    sword.subtype = ItemSubtypeTwoHanded;
    sword.min_damage = 3;
    sword.spec1 = 300;
    sword.weight = 20;
    sword.size = pub::ItemSize::Size1x3;

    pub::Eif file;
    file.rid = {12345, 678};
    file.total_items_count = 2;
    file.items = {EifRecord("Gold"), sword};
    GivenFile(file);

    EIF eif(filename);

    EXPECT_EQ((std::vector<int>{12345, 678}), eif.Rid());
    EXPECT_EQ(2, eif.Length());
    EXPECT_EQ(2, eif.Count());
    EXPECT_EQ(file.items[0], eif.Get(1));
    EXPECT_EQ(sword, eif.Get(2));
}

TEST_F(EODataTests, EIF_Exists_IdOutOfRange_ReturnsFalse)
{
    pub::Eif file;
    file.rid = {1, 2};
    file.total_items_count = 2;
    file.items = {EifRecord("Gold"), EifRecord("Sword")};
    GivenFile(file);

    EIF eif(filename);

    EXPECT_FALSE(eif.Exists(-1));
    EXPECT_FALSE(eif.Exists(0));
    EXPECT_TRUE(eif.Exists(1));
    EXPECT_TRUE(eif.Exists(2));
    EXPECT_FALSE(eif.Exists(3));
}

TEST_F(EODataTests, EIF_TryGet_KnownId_SetsRecordAndReturnsTrue)
{
    pub::Eif file;
    file.rid = {1, 2};
    file.total_items_count = 2;
    file.items = {EifRecord("Gold"), EifRecord("Sword")};
    GivenFile(file);

    EIF eif(filename);

    const pub::EifRecord* record = nullptr;
    ASSERT_TRUE(eif.TryGet(2, record));
    EXPECT_EQ(&eif.Get(2), record);
    EXPECT_EQ("Sword", record->name);
}

TEST_F(EODataTests, EIF_TryGet_UnknownId_SetsNullAndReturnsFalse)
{
    pub::Eif file;
    file.rid = {1, 2};
    file.total_items_count = 1;
    file.items = {EifRecord("Gold")};
    GivenFile(file);

    EIF eif(filename);

    const pub::EifRecord* record = &eif.Get(1);
    EXPECT_FALSE(eif.TryGet(0, record));
    EXPECT_EQ(nullptr, record);

    record = &eif.Get(1);
    EXPECT_FALSE(eif.TryGet(2, record));
    EXPECT_EQ(nullptr, record);
}

TEST_F(EODataTests, EIF_Get_UnknownId_ReturnsEmptyLoreItem)
{
    pub::Eif file;
    file.rid = {1, 2};
    file.total_items_count = 1;
    file.items = {EifRecord("Gold")};
    GivenFile(file);

    EIF eif(filename);

    pub::EifRecord expected;
    expected.special = pub::ItemSpecial::Lore;
    EXPECT_EQ(expected, eif.Get(0));
    EXPECT_EQ(expected, eif.Get(2));
}

TEST_F(EODataTests, EIF_Read_ItemNamedGunAt365_IsRanged)
{
    pub::Eif file;
    file.rid = {1, 2};
    file.items.resize(366);
    file.items[364] = EifRecord("Gun");
    file.items[365] = EifRecord("Gun");
    file.total_items_count = static_cast<int>(file.items.size());
    GivenFile(file);

    EIF eif(filename);

    EXPECT_EQ(pub::ItemSubtype::Ranged, eif.Get(365).subtype);
    EXPECT_EQ(pub::ItemSubtype::None, eif.Get(366).subtype);
}

TEST_F(EODataTests, EIF_Read_TrailingEofRecord_IsNotLoaded)
{
    pub::Eif file;
    file.rid = {1, 2};
    file.total_items_count = 2;
    file.items = {EifRecord("Gold"), EifRecord("eof")};
    GivenFile(file);

    EIF eif(filename);

    EXPECT_EQ(2, eif.Length());
    EXPECT_EQ(1, eif.Count());
    EXPECT_EQ("Gold", eif.Get(1).name);
    EXPECT_FALSE(eif.Exists(2));
}

TEST_F(EODataTests, EIF_Read_TruncatedRecord_LoadsWithDefaults)
{
    pub::Eif file;
    file.rid = {1, 2};
    file.total_items_count = 1;
    file.items = {EifRecord("Gold")};
    file.items[0].weight = 1;

    eolib::data::EoWriter writer;
    file.Serialize(writer);
    std::vector<std::uint8_t> content = writer.Data();
    content.resize(content.size() - 4);
    GivenFile(content);

    EIF eif(filename);

    ASSERT_EQ(1, eif.Count());
    EXPECT_EQ("Gold", eif.Get(1).name);
    EXPECT_EQ(0, eif.Get(1).weight);
}

TEST_F(EODataTests, EIF_GetKey_KeyItem_ReturnsItsId)
{
    pub::EifRecord key = EifRecord("Key");
    key.type = pub::ItemType::Key;
    key.spec1 = 7;

    pub::Eif file;
    file.rid = {1, 2};
    file.total_items_count = 2;
    file.items = {EifRecord("Gold"), key};
    GivenFile(file);

    EIF eif(filename);

    EXPECT_EQ(2, eif.GetKey(7));
    EXPECT_EQ(0, eif.GetKey(8));
}

TEST(ItemHelperTests, AsScroll_ReadsMapXYFromSpec1To3)
{
    pub::EifRecord item;
    item.spec1 = 300;
    item.spec2 = 10;
    item.spec3 = 20;

    ItemScrollSpec scroll = AsScroll(item);

    EXPECT_EQ(300, scroll.map);
    EXPECT_EQ(10, scroll.x);
    EXPECT_EQ(20, scroll.y);
}

TEST(ItemHelperTests, AsEquipmentAndTwoHanded_ReadSpec1To3)
{
    pub::EifRecord item;
    item.spec1 = 42;
    item.spec2 = 1;
    item.spec3 = 7;

    EXPECT_EQ(42, AsEquipment(item).doll_graphic);
    EXPECT_EQ(eolib::protocol::Gender::Male, AsEquipment(item).gender);
    EXPECT_EQ(7, AsTwoHanded(item).dual_wield_doll_graphic);
}

TEST(ItemHelperTests, SingleFieldSpecs_ReadSpec1)
{
    pub::EifRecord item;
    item.spec1 = 5000;

    EXPECT_EQ(5000, AsExpReward(item).exp);
    EXPECT_EQ(5000, AsHairDye(item).hair_color);
    EXPECT_EQ(5000, AsEffectPotion(item).effect);
    EXPECT_EQ(5000, AsKey(item).key);
}

TEST_F(EODataTests, EIF_Read_MissingFile_Exits)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");

    EXPECT_EXIT(EIF("missing.eif"), testing::ExitedWithCode(1), "");
}

TEST_F(EODataTests, EIF_Read_ShortHeader_Exits)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");

    GivenFile(std::vector<std::uint8_t>{'E', 'I', 'F', 1, 1});

    EXPECT_EXIT(EIF eif(filename), testing::ExitedWithCode(1), "");
}

TEST_F(EODataTests, ENF_Read_LoadsRecords)
{
    pub::EnfRecord record;
    record.name = "Goat";
    record.graphic_id = 5;
    record.type = pub::NpcType::Aggressive;
    record.behavior_id = 9;
    record.hp = 70000;

    pub::Enf file;
    file.rid = {3, 4};
    file.total_npcs_count = 1;
    file.npcs = {record};
    GivenFile(file);

    ENF enf(filename);

    EXPECT_EQ((std::vector<int>{3, 4}), enf.Rid());
    EXPECT_EQ(1, enf.Length());
    EXPECT_EQ(1, enf.Count());
    EXPECT_EQ(record, enf.Get(1));
    EXPECT_FALSE(enf.Exists(2));
    EXPECT_EQ(pub::EnfRecord{}, enf.Get(2));
}

TEST_F(EODataTests, ESF_Read_LoadsRecords)
{
    pub::EsfRecord record;
    record.name = "Heal";
    record.chant = "Heal me";
    record.tp_cost = 3;
    record.type = pub::SkillType::Heal;
    record.target_type = pub::SkillTargetType::Self;
    record.hp_heal = 50;

    pub::Esf file;
    file.rid = {5, 6};
    file.total_skills_count = 1;
    file.skills = {record};
    GivenFile(file);

    ESF esf(filename);

    EXPECT_EQ((std::vector<int>{5, 6}), esf.Rid());
    EXPECT_EQ(1, esf.Count());
    EXPECT_EQ(record, esf.Get(1));
}

TEST_F(EODataTests, ESF_Get_UnknownId_ReturnsEmptyAttackSpell)
{
    pub::Esf file;
    file.rid = {1, 2};
    GivenFile(file);

    ESF esf(filename);

    pub::EsfRecord expected;
    expected.type = pub::SkillType::Attack;
    EXPECT_FALSE(esf.Exists(1));
    EXPECT_EQ(expected, esf.Get(1));
}

TEST_F(EODataTests, ECF_Read_LoadsRecords)
{
    pub::EcfRecord record;
    record.name = "Priest";
    record.parent_type = 1;
    record.stat_group = 2;
    record.str = 3;

    pub::Ecf file;
    file.rid = {7, 8};
    file.total_classes_count = 1;
    file.classes = {record};
    GivenFile(file);

    ECF ecf(filename);

    EXPECT_EQ((std::vector<int>{7, 8}), ecf.Rid());
    EXPECT_EQ(1, ecf.Count());
    EXPECT_EQ(record, ecf.Get(1));
}

TEST_F(EODataTests, ECF_Get_UnknownId_ReturnsPeasantClass)
{
    pub::Ecf file;
    file.rid = {1, 2};
    GivenFile(file);

    ECF ecf(filename);

    EXPECT_EQ(4, ecf.Get(0).parent_type);
    EXPECT_EQ(4, ecf.Get(0).stat_group);
}

TEST_F(EODataTests, ECF_ClassMatches_FollowsParentChain)
{
    pub::EcfRecord priest;
    priest.name = "Priest";

    pub::EcfRecord high_priest;
    high_priest.name = "High Priest";
    high_priest.parent_type = 1;

    pub::EcfRecord rogue;
    rogue.name = "Rogue";

    pub::Ecf file;
    file.rid = {1, 2};
    file.total_classes_count = 3;
    file.classes = {priest, high_priest, rogue};
    GivenFile(file);

    ECF ecf(filename);

    EXPECT_TRUE(ecf.ClassMatches(3, 0));
    EXPECT_TRUE(ecf.ClassMatches(1, 1));
    EXPECT_TRUE(ecf.ClassMatches(2, 1));
    EXPECT_FALSE(ecf.ClassMatches(1, 2));
    EXPECT_FALSE(ecf.ClassMatches(3, 1));
    EXPECT_FALSE(ecf.ClassMatches(0, 1));
    EXPECT_FALSE(ecf.ClassMatches(9, 1));
}

TEST_F(EODataTests, ECF_ClassMatches_ParentCycle_ReturnsFalse)
{
    pub::EcfRecord first;
    first.name = "First";
    first.parent_type = 2;

    pub::EcfRecord second;
    second.name = "Second";
    second.parent_type = 1;

    pub::Ecf file;
    file.rid = {1, 2};
    file.total_classes_count = 2;
    file.classes = {first, second};
    GivenFile(file);

    ECF ecf(filename);

    EXPECT_FALSE(ecf.ClassMatches(1, 3));
}
