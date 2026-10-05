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

TEST_F(EODataTests, EIF_Read_LoadsRecordsAfterPlaceholder)
{
    pub::EifRecord sword = EifRecord("Sword");
    sword.graphic_id = 12;
    sword.type = pub::ItemType::Weapon;
    sword.subtype = pub::ItemSubtype::Reserved4;
    sword.special = pub::ItemSpecial::Lore;
    sword.min_damage = 3;
    sword.max_damage = 7;
    sword.str = 4;
    sword.fire_resistance = 5;
    sword.spec1 = 300;
    sword.spec2 = 1;
    sword.spec3 = 2;
    sword.level_requirement = 10;
    sword.cha_requirement = 6;
    sword.weight = 20;
    sword.size = pub::ItemSize::Size1x3;

    pub::Eif file;
    file.rid = {12345, 678};
    file.total_items_count = 2;
    file.items = {EifRecord("Gold"), sword};
    GivenFile(file);

    EIF eif(filename);

    EXPECT_EQ((std::vector<int>{12345, 678}), eif.rid);
    EXPECT_EQ(2, eif.length);
    ASSERT_EQ(3u, eif.data.size());
    EXPECT_FALSE(eif.Get(0));
    EXPECT_EQ("Gold", eif.Get(1).name);

    const EIF_Data& data = eif.Get(2);
    EXPECT_EQ(2, data.id);
    EXPECT_EQ("Sword", data.name);
    EXPECT_EQ(12, data.graphic);
    EXPECT_EQ(EIF::Weapon, data.type);
    EXPECT_EQ(EIF::TwoHanded, data.subtype);
    EXPECT_EQ(EIF::Lore, data.special);
    EXPECT_EQ(3, data.mindam);
    EXPECT_EQ(7, data.maxdam);
    EXPECT_EQ(4, data.str);
    EXPECT_EQ(5, data.fire);
    EXPECT_EQ(300, data.scrollmap);
    EXPECT_EQ(1, data.scrollx);
    EXPECT_EQ(2, data.scrolly);
    EXPECT_EQ(10, data.levelreq);
    EXPECT_EQ(6, data.chareq);
    EXPECT_EQ(20, data.weight);
    EXPECT_EQ(EIF::Size1x3, data.size);
}

TEST_F(EODataTests, EIF_Read_TrailingEofRecord_IsNotLoaded)
{
    pub::Eif file;
    file.rid = {1, 2};
    file.total_items_count = 2;
    file.items = {EifRecord("Gold"), EifRecord("eof")};
    GivenFile(file);

    EIF eif(filename);

    EXPECT_EQ(2, eif.length);
    ASSERT_EQ(2u, eif.data.size());
    EXPECT_EQ("Gold", eif.Get(1).name);
    EXPECT_FALSE(eif.Get(2));
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

    ASSERT_EQ(2u, eif.data.size());
    EXPECT_EQ("Gold", eif.Get(1).name);
    EXPECT_EQ(0, eif.Get(1).weight);
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

TEST_F(EODataTests, ENF_Read_LoadsRecordFields)
{
    pub::EnfRecord record;
    record.name = "Goat";
    record.graphic_id = 5;
    record.boss = true;
    record.type = pub::NpcType::Aggressive;
    record.behavior_id = 9;
    record.hp = 70000;
    record.min_damage = 2;
    record.armor = 8;
    record.experience = 70001;

    pub::Enf file;
    file.rid = {3, 4};
    file.total_npcs_count = 1;
    file.npcs = {record};
    GivenFile(file);

    ENF enf(filename);

    EXPECT_EQ((std::vector<int>{3, 4}), enf.rid);
    EXPECT_EQ(1, enf.length);
    const ENF_Data& data = enf.Get(1);
    EXPECT_EQ("Goat", data.name);
    EXPECT_EQ(5, data.graphic);
    EXPECT_TRUE(data.boss);
    EXPECT_FALSE(data.child);
    EXPECT_EQ(ENF::Aggressive, data.type);
    EXPECT_EQ(9, data.vendor_id);
    EXPECT_EQ(70000, data.hp);
    EXPECT_EQ(2, data.mindam);
    EXPECT_EQ(8, data.armor);
    EXPECT_EQ(70001, data.exp);
}

TEST_F(EODataTests, ESF_Read_LoadsRecordFields)
{
    pub::EsfRecord record;
    record.name = "Heal";
    record.chant = "Heal me";
    record.icon_id = 1;
    record.graphic_id = 2;
    record.tp_cost = 3;
    record.cast_time = 4;
    record.type = pub::SkillType::Heal;
    record.target_restrict = pub::SkillTargetRestrict::Friendly;
    record.target_type = pub::SkillTargetType::Self;
    record.hp_heal = 50;

    pub::Esf file;
    file.rid = {5, 6};
    file.total_skills_count = 1;
    file.skills = {record};
    GivenFile(file);

    ESF esf(filename);

    EXPECT_EQ((std::vector<int>{5, 6}), esf.rid);
    const ESF_Data& data = esf.Get(1);
    EXPECT_EQ("Heal", data.name);
    EXPECT_EQ("Heal me", data.shout);
    EXPECT_EQ(1, data.icon);
    EXPECT_EQ(2, data.graphic);
    EXPECT_EQ(3, data.tp);
    EXPECT_EQ(4, data.cast_time);
    EXPECT_EQ(ESF::Heal, data.type);
    EXPECT_EQ(ESF::Friendly, data.target_restrict);
    EXPECT_EQ(ESF::Self, data.target);
    EXPECT_EQ(50, data.hp);
}

TEST_F(EODataTests, ECF_Read_LoadsRecordFields)
{
    pub::EcfRecord record;
    record.name = "Priest";
    record.parent_type = 1;
    record.stat_group = 2;
    record.str = 3;
    record.cha = 8;

    pub::Ecf file;
    file.rid = {7, 8};
    file.total_classes_count = 1;
    file.classes = {record};
    GivenFile(file);

    ECF ecf(filename);

    EXPECT_EQ((std::vector<int>{7, 8}), ecf.rid);
    const ECF_Data& data = ecf.Get(1);
    EXPECT_EQ("Priest", data.name);
    EXPECT_EQ(1, data.base);
    EXPECT_EQ(2, data.type);
    EXPECT_EQ(3, data.str);
    EXPECT_EQ(8, data.cha);
}
