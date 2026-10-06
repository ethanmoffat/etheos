#include "testhelper/playing.hpp"

#include "party.hpp"

#include <eolib/protocol/net/server/packets.hpp>

#include <memory>

namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short PartyTestServerPort = 38120;
constexpr int PartyTestMapId = 25;
constexpr int EqualShareMode = 1;

class PartyTests : public PlayingTest
{
protected:
    PartyTests()
        : PlayingTest(PartyTestServerPort, PartyTestMapId)
    {
    }

    void TearDown() override
    {
        party.reset();
        PlayingTest::TearDown();
    }

    Party& GivenParty(PlayingCharacter& leader, PlayingCharacter& other)
    {
        party = std::make_unique<Party>(server->world, leader.character, other.character);
        return *party;
    }

    void GivenLevel(PlayingCharacter& entry, int level)
    {
        entry.character->level = level;
        entry.character->exp = server->world->exp_table[level];
    }

    std::unique_ptr<Party> party;
};

TEST_F(PartyTests, ShareEXP_NoLevelUp_SendsGainsToPartyOnly)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& leader = GivenCharacter(3, 3);
    PlayingCharacter& member = GivenCharacter(4, 4);
    PlayingCharacter& bystander = GivenCharacter(5, 5);
    GivenLevel(leader, 50);
    GivenLevel(member, 50);

    GivenParty(leader, member).ShareEXP(10, EqualShareMode, map);

    const auto gains = LastSent<server::PartyTargetGroupServerPacket>(member);
    ASSERT_TRUE(gains);
    ASSERT_EQ(2u, gains->gains.size());
    EXPECT_EQ(leader.character->PlayerID(), gains->gains[0].player_id);
    EXPECT_EQ(5, gains->gains[0].experience);
    EXPECT_EQ(0, gains->gains[0].level_up);
    EXPECT_EQ(member.character->PlayerID(), gains->gains[1].player_id);
    EXPECT_TRUE(LastSent<server::PartyTargetGroupServerPacket>(leader));
    EXPECT_FALSE(LastSent<server::PartyTargetGroupServerPacket>(bystander));
    EXPECT_FALSE(LastSent<server::RecoverTargetGroupServerPacket>(member));
}

TEST_F(PartyTests, ShareEXP_LevelUp_SendsGainsToWholeMapAndStatsToLeveler)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& leader = GivenCharacter(3, 3);
    PlayingCharacter& member = GivenCharacter(4, 4);
    PlayingCharacter& bystander = GivenCharacter(35, 35);
    GivenLevel(leader, 50);
    GivenLevel(member, 0);

    GivenParty(leader, member).ShareEXP(server->world->exp_table[1] * 2, EqualShareMode, map);

    const auto gains = LastSent<server::PartyTargetGroupServerPacket>(bystander);
    ASSERT_TRUE(gains);
    ASSERT_EQ(2u, gains->gains.size());
    EXPECT_EQ(0, gains->gains[0].level_up);
    EXPECT_EQ(member.character->level, gains->gains[1].level_up);

    const auto stats = LastSent<server::RecoverTargetGroupServerPacket>(member);
    ASSERT_TRUE(stats);
    EXPECT_EQ(member.character->statpoints, stats->stat_points);
    EXPECT_EQ(member.character->skillpoints, stats->skill_points);
    EXPECT_EQ(member.character->maxhp, stats->max_hp);
    EXPECT_FALSE(LastSent<server::RecoverTargetGroupServerPacket>(leader));
}

TEST_F(PartyTests, ShareEXP_MultipleLevels_ReportsFinalLevel)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& leader = GivenCharacter(3, 3);
    PlayingCharacter& member = GivenCharacter(4, 4);
    GivenLevel(leader, 50);
    GivenLevel(member, 0);

    GivenParty(leader, member).ShareEXP(server->world->exp_table[5] * 2, EqualShareMode, map);

    EXPECT_EQ(5, member.character->level);
    const auto gains = LastSent<server::PartyTargetGroupServerPacket>(member);
    ASSERT_TRUE(gains);
    ASSERT_EQ(2u, gains->gains.size());
    EXPECT_EQ(5, gains->gains[1].level_up);
}

}
