#include "../testhelper/playing.hpp"

// include the CPP file with the Guild functions in it for testing
#include "../../handlers/Guild.cpp"

#include "../../npc.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdio>
#include <memory>
#include <string>

namespace client = eolib::protocol::net::client;
namespace pub = eolib::protocol::pub;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short GuildTestServerPort = 38143;
constexpr int GuildTestMapId = 48;
constexpr int GuildNpcId = 1;
constexpr int GuildSessionId = 1234;
constexpr int GoldId = 1;

class GuildTests : public PlayingTest
{
protected:
    NPC* guild_npc = nullptr;
    std::shared_ptr<Guild> guild;

    GuildTests()
        : PlayingTest(GuildTestServerPort, GuildTestMapId)
    {
    }

    void ConfigureServer(Config& config) override
    {
        pub::EnfRecord guild_record;
        guild_record.name = "Guild Master";
        guild_record.type = pub::NpcType::Guild;
        guild_record.hp = 10;

        pub::Enf enf;
        enf.rid = {1, 2};
        enf.total_npcs_count = 1;
        enf.npcs = {guild_record};
        WriteFile("guild_test.enf", enf);

        pub::EifRecord gold;
        gold.name = "Gold";

        pub::Eif eif;
        eif.rid = {1, 2};
        eif.total_items_count = 1;
        eif.items = {gold};
        WriteFile("guild_test.eif", eif);

        config["ENF"] = "guild_test.enf";
        config["EIF"] = "guild_test.eif";
        config["GuildPrice"] = 100;
        config["GuildCreateMembers"] = 2;
        config["RecruitCost"] = 50;
        config["GuildMinDeposit"] = 10;
        config["GuildAnnounce"] = false;
        config["GuildShowRecruiters"] = true;
        config["GuildCustomRanks"] = false;
    }

    void SetUp() override
    {
        PlayingTest::SetUp();

        // Guilds load from and save to the guilds table and members' character rows
        using testing::_;
        using testing::AnyOf;
        using testing::HasSubstr;
        EXPECT_CALL(*dynamic_cast<MockDatabase*>(server->world->db.get()), RawQuery(AnyOf(HasSubstr("guilds"), HasSubstr("UPDATE characters")), _, _))
            .WillRepeatedly(testing::Return(Database_Result()));
    }

    void TearDown() override
    {
        guild.reset();
        PlayingTest::TearDown();
        std::remove("guild_test.enf");
        std::remove("guild_test.eif");
    }

    void GivenGuildNpc()
    {
        GivenMap(EmptyEmf(10, 10));

        eolib::protocol::Coords loc;
        loc.x = 3;
        loc.y = 4;
        guild_npc = new NPC(map, GuildNpcId, loc, NPC::StationarySpawnType, 0, Direction::Down, map->GenerateNPCIndex(), true);
        map->npcs.push_back(guild_npc);
        guild_npc->Spawn();
    }

    PlayingCharacter& GivenCharacterAtGuildNpc(int x = 3, int y = 3)
    {
        if (!map)
            GivenGuildNpc();

        PlayingCharacter& entry = GivenOnlineCharacter(x, y);
        entry.character->npc = guild_npc;
        entry.character->npc_type = pub::NpcType::Guild;
        entry.character->npc_session_id = GuildSessionId;
        entry.character->AddItem(GoldId, 500);
        return entry;
    }

    // A loaded guild with tag TST; the character joins it with the given rank
    void GivenGuild(Character* member, int rank)
    {
        if (!guild)
        {
            guild = std::make_shared<Guild>(server->world->guildmanager);
            guild->tag = "TST";
            guild->name = "testers";
            guild->description = "about us";
            guild->ranks = {"leader", "recruiter", "r3", "r4", "r5", "r6", "r7", "r8", "member"};
            guild->bank = 100;
            server->world->guildmanager->cache[guild->tag] = guild;
            server->world->guildmanager->cache[guild->name] = guild;
        }

        guild->AddMember(member, member, false, rank);
    }
};

}

TEST_F(GuildTests, Open_GuildNpc_SendsNpcSession)
{
    GivenGuildNpc();
    Character* character = GivenOnlineCharacter(3, 3).character;

    client::GuildOpenClientPacket packet;
    packet.npc_index = guild_npc->index;
    Handlers::Guild_Open(character, packet);

    const auto reply = LastSent<server::GuildOpenServerPacket>(*playing.back());
    ASSERT_TRUE(reply);
    EXPECT_NE(0, reply->session_id);
    EXPECT_EQ(character->npc_session_id, reply->session_id);
    EXPECT_EQ(pub::NpcType::Guild, character->npc_type);
}

TEST_F(GuildTests, Request_NewGuild_SendsCreateBeginAndInvitesNearby)
{
    PlayingCharacter& leader = GivenCharacterAtGuildNpc();
    PlayingCharacter& other = GivenCharacterAtGuildNpc(4, 3);

    client::GuildRequestClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.guild_tag = "NEW";
    packet.guild_name = "newbies";
    Handlers::Guild_Request(leader.character, packet);

    const auto replies = AllSent<server::GuildReplyServerPacket>(leader);
    ASSERT_EQ(1u, replies.size());
    EXPECT_EQ(server::GuildReply::CreateBegin, replies[0].reply_code);

    const auto invite = LastSent<server::GuildRequestServerPacket>(other);
    ASSERT_TRUE(invite);
    EXPECT_EQ(leader.character->PlayerID(), invite->player_id);
    EXPECT_EQ("Newbies (NEW)", invite->guild_identity);
    EXPECT_EQ("NEW", other.character->guild_invite);
}

TEST_F(GuildTests, Request_WrongSession_Ignored)
{
    PlayingCharacter& leader = GivenCharacterAtGuildNpc();

    client::GuildRequestClientPacket packet;
    packet.session_id = GuildSessionId + 1;
    packet.guild_tag = "NEW";
    packet.guild_name = "newbies";
    Handlers::Guild_Request(leader.character, packet);

    EXPECT_TRUE(leader.sent.empty());
    EXPECT_FALSE(leader.character->guild_create);
}

TEST_F(GuildTests, Request_ExistingTag_SendsExists)
{
    PlayingCharacter& member = GivenCharacterAtGuildNpc();
    GivenGuild(member.character, 1);
    PlayingCharacter& leader = GivenCharacterAtGuildNpc(4, 3);

    client::GuildRequestClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.guild_tag = "TST";
    packet.guild_name = "other";
    Handlers::Guild_Request(leader.character, packet);

    const auto replies = AllSent<server::GuildReplyServerPacket>(leader);
    ASSERT_EQ(1u, replies.size());
    EXPECT_EQ(server::GuildReply::Exists, replies[0].reply_code);
}

TEST_F(GuildTests, Accept_PendingInvite_ConfirmsToLeader)
{
    PlayingCharacter& leader = GivenCharacterAtGuildNpc();
    PlayingCharacter& invited = GivenCharacterAtGuildNpc(4, 3);
    auto create = server->world->guildmanager->BeginCreate("NEW", "newbies", leader.character);
    invited.character->guild_invite = "NEW";

    client::GuildAcceptClientPacket packet;
    packet.session_id = client::GuildAcceptClientPacket::DEFAULT_SESSION_ID;
    packet.inviter_player_id = leader.character->PlayerID();
    Handlers::Guild_Accept(invited.character, packet);

    const auto replies = AllSent<server::GuildReplyServerPacket>(leader);
    ASSERT_EQ(1u, replies.size());
    ASSERT_TRUE(replies[0].AsCreateAddConfirm());
    EXPECT_EQ(invited.character->real_name, replies[0].AsCreateAddConfirm()->name);
    EXPECT_EQ(2u, create->members.size());
    EXPECT_TRUE(invited.character->guild_invite.empty());
}

TEST_F(GuildTests, Accept_WrongFixedSession_ClosesClient)
{
    PlayingCharacter& leader = GivenCharacterAtGuildNpc();
    PlayingCharacter& invited = GivenCharacterAtGuildNpc(4, 3);
    auto create = server->world->guildmanager->BeginCreate("NEW", "newbies", leader.character);
    invited.character->guild_invite = "NEW";

    client::GuildAcceptClientPacket packet;
    packet.session_id = client::GuildAcceptClientPacket::DEFAULT_SESSION_ID + 1;
    packet.inviter_player_id = leader.character->PlayerID();

    EXPECT_CALL(*invited.client, Close(false)).Times(1);
    Handlers::Guild_Accept(invited.character, packet);

    EXPECT_TRUE(AllSent<server::GuildReplyServerPacket>(leader).empty());
    EXPECT_EQ(1u, create->members.size());
}

TEST_F(GuildTests, Player_RecruiterInGuild_SendsJoinRequestToRecruiter)
{
    PlayingCharacter& recruiter = GivenCharacterAtGuildNpc();
    GivenGuild(recruiter.character, 2);
    PlayingCharacter& joiner = GivenCharacterAtGuildNpc(4, 3);

    client::GuildPlayerClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.guild_tag = "tst";
    packet.recruiter_name = recruiter.character->real_name;
    Handlers::Guild_Player(joiner.character, packet);

    const auto replies = AllSent<server::GuildReplyServerPacket>(recruiter);
    ASSERT_EQ(1u, replies.size());
    ASSERT_TRUE(replies[0].AsJoinRequest());
    EXPECT_EQ(joiner.character->PlayerID(), replies[0].AsJoinRequest()->player_id);
    EXPECT_EQ("TST", joiner.character->guild_join);
}

TEST_F(GuildTests, Player_RecruiterOffline_SendsRecruiterOffline)
{
    PlayingCharacter& joiner = GivenCharacterAtGuildNpc();

    client::GuildPlayerClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.guild_tag = "TST";
    packet.recruiter_name = "nobody";
    Handlers::Guild_Player(joiner.character, packet);

    const auto replies = AllSent<server::GuildReplyServerPacket>(joiner);
    ASSERT_EQ(1u, replies.size());
    EXPECT_EQ(server::GuildReply::RecruiterOffline, replies[0].reply_code);
}

TEST_F(GuildTests, Use_EnoughGuildGold_AddsMember)
{
    PlayingCharacter& recruiter = GivenCharacterAtGuildNpc();
    GivenGuild(recruiter.character, 2);
    PlayingCharacter& joiner = GivenCharacterAtGuildNpc(4, 3);
    joiner.character->guild_join = "TST";

    client::GuildUseClientPacket packet;
    packet.player_id = joiner.character->PlayerID();
    Handlers::Guild_Use(recruiter.character, packet);

    const auto replies = AllSent<server::GuildReplyServerPacket>(recruiter);
    ASSERT_EQ(1u, replies.size());
    EXPECT_EQ(server::GuildReply::Accepted, replies[0].reply_code);
    EXPECT_EQ(50, guild->bank);
    EXPECT_EQ(guild, joiner.character->guild);

    const auto agree = LastSent<server::GuildAgreeServerPacket>(joiner);
    ASSERT_TRUE(agree);
    EXPECT_EQ(recruiter.character->PlayerID(), agree->recruiter_id);
    EXPECT_EQ("TST", agree->guild_tag);
    EXPECT_EQ("member", agree->rank_name);
}

TEST_F(GuildTests, Use_LowGuildGold_SendsAccountLow)
{
    PlayingCharacter& recruiter = GivenCharacterAtGuildNpc();
    GivenGuild(recruiter.character, 2);
    guild->bank = 49;
    PlayingCharacter& joiner = GivenCharacterAtGuildNpc(4, 3);
    joiner.character->guild_join = "TST";

    client::GuildUseClientPacket packet;
    packet.player_id = joiner.character->PlayerID();
    Handlers::Guild_Use(recruiter.character, packet);

    const auto replies = AllSent<server::GuildReplyServerPacket>(recruiter);
    ASSERT_EQ(1u, replies.size());
    EXPECT_EQ(server::GuildReply::AccountLow, replies[0].reply_code);
    EXPECT_FALSE(joiner.character->guild);
}

TEST_F(GuildTests, Use_GuildFull_SendsNothing)
{
    PlayingCharacter& recruiter = GivenCharacterAtGuildNpc();
    GivenGuild(recruiter.character, 2);
    server->world->config["GuildMaxMembers"] = 1;
    PlayingCharacter& joiner = GivenCharacterAtGuildNpc(4, 3);
    joiner.character->guild_join = "TST";

    client::GuildUseClientPacket packet;
    packet.player_id = joiner.character->PlayerID();
    Handlers::Guild_Use(recruiter.character, packet);

    EXPECT_TRUE(AllSent<server::GuildReplyServerPacket>(recruiter).empty());
    EXPECT_FALSE(joiner.character->guild);
    EXPECT_EQ(100, guild->bank);
}

TEST_F(GuildTests, Take_Bank_SendsGuildGold)
{
    PlayingCharacter& member = GivenCharacterAtGuildNpc();
    GivenGuild(member.character, 9);

    client::GuildTakeClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.info_type = client::GuildInfoType::Bank;
    Handlers::Guild_Take(member.character, packet);

    const auto reply = LastSent<server::GuildSellServerPacket>(member);
    ASSERT_TRUE(reply);
    EXPECT_EQ(100, reply->gold_amount);
}

TEST_F(GuildTests, Take_Ranks_SendsRankNames)
{
    PlayingCharacter& leader = GivenCharacterAtGuildNpc();
    GivenGuild(leader.character, 1);

    client::GuildTakeClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.info_type = client::GuildInfoType::Ranks;
    Handlers::Guild_Take(leader.character, packet);

    const auto reply = LastSent<server::GuildRankServerPacket>(leader);
    ASSERT_TRUE(reply);
    ASSERT_EQ(9u, reply->ranks.size());
    EXPECT_EQ("leader", reply->ranks[0]);
    EXPECT_EQ("member", reply->ranks[8]);
}

TEST_F(GuildTests, Take_DescriptionBelowEditRank_Ignored)
{
    PlayingCharacter& member = GivenCharacterAtGuildNpc();
    GivenGuild(member.character, 9);

    client::GuildTakeClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.info_type = client::GuildInfoType::Description;
    Handlers::Guild_Take(member.character, packet);

    EXPECT_TRUE(member.sent.empty());
}

TEST_F(GuildTests, Agree_Ranks_UpdatesRanks)
{
    PlayingCharacter& leader = GivenCharacterAtGuildNpc();
    GivenGuild(leader.character, 1);

    client::GuildAgreeClientPacket::InfoTypeDataRanks ranks;
    ranks.ranks = {"boss", "a", "b", "c", "d", "e", "f", "g", "newbie"};
    client::GuildAgreeClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.info_type = client::GuildInfoType::Ranks;
    packet.info_type_data = ranks;
    Handlers::Guild_Agree(leader.character, packet);

    const auto replies = AllSent<server::GuildReplyServerPacket>(leader);
    ASSERT_EQ(1u, replies.size());
    EXPECT_EQ(server::GuildReply::RanksUpdated, replies[0].reply_code);
    EXPECT_EQ("boss", guild->ranks[0]);
    EXPECT_EQ("newbie", guild->ranks[8]);
}

TEST_F(GuildTests, Agree_Description_UpdatesDescription)
{
    PlayingCharacter& leader = GivenCharacterAtGuildNpc();
    GivenGuild(leader.character, 1);

    client::GuildAgreeClientPacket::InfoTypeDataDescription description;
    description.description = "new words";
    client::GuildAgreeClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.info_type = client::GuildInfoType::Description;
    packet.info_type_data = description;
    Handlers::Guild_Agree(leader.character, packet);

    const auto replies = AllSent<server::GuildReplyServerPacket>(leader);
    ASSERT_EQ(1u, replies.size());
    EXPECT_EQ(server::GuildReply::Updated, replies[0].reply_code);
    EXPECT_EQ("new words", guild->description);
}

TEST_F(GuildTests, Buy_Deposit_SendsRemainingGold)
{
    PlayingCharacter& member = GivenCharacterAtGuildNpc();
    GivenGuild(member.character, 9);

    client::GuildBuyClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.gold_amount = 200;
    Handlers::Guild_Buy(member.character, packet);

    const auto reply = LastSent<server::GuildBuyServerPacket>(member);
    ASSERT_TRUE(reply);
    EXPECT_EQ(300, reply->gold_amount);
    EXPECT_EQ(300, guild->bank);
}

TEST_F(GuildTests, Buy_WrongSession_Ignored)
{
    PlayingCharacter& member = GivenCharacterAtGuildNpc();
    GivenGuild(member.character, 9);

    client::GuildBuyClientPacket packet;
    packet.session_id = GuildSessionId + 1;
    packet.gold_amount = 200;
    Handlers::Guild_Buy(member.character, packet);

    EXPECT_TRUE(member.sent.empty());
    EXPECT_EQ(100, guild->bank);
    EXPECT_EQ(500, member.character->HasItem(GoldId));
}

TEST_F(GuildTests, Tell_KnownTag_SendsMembers)
{
    PlayingCharacter& leader = GivenCharacterAtGuildNpc();
    GivenGuild(leader.character, 1);
    PlayingCharacter& member = GivenCharacterAtGuildNpc(4, 3);
    GivenGuild(member.character, 9);

    client::GuildTellClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.guild_identity = "TST";
    Handlers::Guild_Tell(member.character, packet);

    const auto reply = LastSent<server::GuildTellServerPacket>(member);
    ASSERT_TRUE(reply);
    ASSERT_EQ(2u, reply->members.size());
    EXPECT_EQ(1, reply->members[0].rank);
    EXPECT_EQ(leader.character->real_name, reply->members[0].name);
    EXPECT_EQ("leader", reply->members[0].rank_name);
    EXPECT_EQ("member", reply->members[1].rank_name);
}

TEST_F(GuildTests, Tell_UnknownGuild_SendsNotFound)
{
    PlayingCharacter& entry = GivenCharacterAtGuildNpc();

    client::GuildTellClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.guild_identity = "ZZZ";
    Handlers::Guild_Tell(entry.character, packet);

    const auto replies = AllSent<server::GuildReplyServerPacket>(entry);
    ASSERT_EQ(1u, replies.size());
    EXPECT_EQ(server::GuildReply::NotFound, replies[0].reply_code);
}

TEST_F(GuildTests, Report_KnownName_SendsInfoAndStaff)
{
    PlayingCharacter& founder = GivenCharacterAtGuildNpc();
    GivenGuild(founder.character, 0);
    PlayingCharacter& recruiter = GivenCharacterAtGuildNpc(4, 3);
    GivenGuild(recruiter.character, 2);
    PlayingCharacter& member = GivenCharacterAtGuildNpc(5, 3);
    GivenGuild(member.character, 9);

    client::GuildReportClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.guild_identity = "testers";
    Handlers::Guild_Report(member.character, packet);

    const auto reply = LastSent<server::GuildReportServerPacket>(member);
    ASSERT_TRUE(reply);
    EXPECT_EQ("TST", reply->tag);
    EXPECT_EQ("100", reply->wealth);
    ASSERT_EQ(9u, reply->ranks.size());
    EXPECT_EQ("r3  ", reply->ranks[2]);
    ASSERT_EQ(2u, reply->staff.size());
    EXPECT_EQ(1, reply->staff[0].rank);
    EXPECT_EQ(founder.character->real_name + " (founder)", reply->staff[0].name);
    EXPECT_EQ(2, reply->staff[1].rank);
    EXPECT_EQ(recruiter.character->real_name, reply->staff[1].name);
}

TEST_F(GuildTests, Kick_LowerRankMember_RemovesMember)
{
    PlayingCharacter& leader = GivenCharacterAtGuildNpc();
    GivenGuild(leader.character, 1);
    PlayingCharacter& member = GivenCharacterAtGuildNpc(4, 3);
    GivenGuild(member.character, 9);

    client::GuildKickClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.member_name = member.character->real_name;
    Handlers::Guild_Kick(leader.character, packet);

    const auto replies = AllSent<server::GuildReplyServerPacket>(leader);
    ASSERT_EQ(1u, replies.size());
    EXPECT_EQ(server::GuildReply::Removed, replies[0].reply_code);
    EXPECT_FALSE(guild->GetMember(member.character->real_name));
}

TEST_F(GuildTests, Kick_SameRankMember_SendsRemoveLeader)
{
    PlayingCharacter& leader = GivenCharacterAtGuildNpc();
    GivenGuild(leader.character, 1);
    PlayingCharacter& other = GivenCharacterAtGuildNpc(4, 3);
    GivenGuild(other.character, 1);

    client::GuildKickClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.member_name = other.character->real_name;
    Handlers::Guild_Kick(leader.character, packet);

    const auto replies = AllSent<server::GuildReplyServerPacket>(leader);
    ASSERT_EQ(1u, replies.size());
    EXPECT_EQ(server::GuildReply::RemoveLeader, replies[0].reply_code);
    EXPECT_TRUE(guild->GetMember(other.character->real_name));
}

TEST_F(GuildTests, Rank_PromoteMember_SendsUpdated)
{
    PlayingCharacter& leader = GivenCharacterAtGuildNpc();
    GivenGuild(leader.character, 1);
    PlayingCharacter& member = GivenCharacterAtGuildNpc(4, 3);
    GivenGuild(member.character, 9);

    client::GuildRankClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.rank = 2;
    packet.member_name = member.character->real_name;
    Handlers::Guild_Rank(leader.character, packet);

    const auto replies = AllSent<server::GuildReplyServerPacket>(leader);
    ASSERT_EQ(1u, replies.size());
    EXPECT_EQ(server::GuildReply::Updated, replies[0].reply_code);
    EXPECT_EQ(2, guild->GetMember(member.character->real_name)->rank);
}

TEST_F(GuildTests, Rank_Founder_SendsRankingLeader)
{
    PlayingCharacter& founder = GivenCharacterAtGuildNpc();
    GivenGuild(founder.character, 0);
    PlayingCharacter& leader = GivenCharacterAtGuildNpc(4, 3);
    GivenGuild(leader.character, 1);

    client::GuildRankClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.rank = 5;
    packet.member_name = founder.character->real_name;
    Handlers::Guild_Rank(leader.character, packet);

    const auto replies = AllSent<server::GuildReplyServerPacket>(leader);
    ASSERT_EQ(1u, replies.size());
    EXPECT_EQ(server::GuildReply::RankingLeader, replies[0].reply_code);
    EXPECT_EQ(0, guild->GetMember(founder.character->real_name)->rank);
}

TEST_F(GuildTests, Remove_Member_LeavesGuild)
{
    PlayingCharacter& member = GivenCharacterAtGuildNpc();
    GivenGuild(member.character, 9);
    PlayingCharacter& other = GivenCharacterAtGuildNpc(4, 3);
    GivenGuild(other.character, 1);

    client::GuildRemoveClientPacket packet;
    packet.session_id = GuildSessionId;
    Handlers::Guild_Remove(member.character, packet);

    EXPECT_FALSE(guild->GetMember(member.character->real_name));
}

TEST_F(GuildTests, Remove_WrongSession_Ignored)
{
    PlayingCharacter& member = GivenCharacterAtGuildNpc();
    GivenGuild(member.character, 9);

    client::GuildRemoveClientPacket packet;
    packet.session_id = GuildSessionId + 1;
    Handlers::Guild_Remove(member.character, packet);

    EXPECT_TRUE(guild->GetMember(member.character->real_name));
}

TEST_F(GuildTests, Junk_BelowDisbandRank_Ignored)
{
    PlayingCharacter& leader = GivenCharacterAtGuildNpc();
    GivenGuild(leader.character, 1);

    client::GuildJunkClientPacket packet;
    packet.session_id = GuildSessionId;
    Handlers::Guild_Junk(leader.character, packet);

    EXPECT_TRUE(guild->GetMember(leader.character->real_name));
}

TEST_F(GuildTests, Junk_Founder_DisbandsGuild)
{
    PlayingCharacter& founder = GivenCharacterAtGuildNpc();
    GivenGuild(founder.character, 0);
    PlayingCharacter& member = GivenCharacterAtGuildNpc(4, 3);
    GivenGuild(member.character, 9);

    client::GuildJunkClientPacket packet;
    packet.session_id = GuildSessionId;
    Handlers::Guild_Junk(founder.character, packet);

    EXPECT_TRUE(guild->members.empty());
}

TEST_F(GuildTests, Create_EnoughMembers_CreatesGuild)
{
    PlayingCharacter& leader = GivenCharacterAtGuildNpc();
    PlayingCharacter& invited = GivenCharacterAtGuildNpc(4, 3);
    auto create = server->world->guildmanager->BeginCreate("NEW", "newbies", leader.character);
    create->AddMember(invited.character->real_name);

    std::unordered_map<std::string, util::variant> row;
    row["tag"] = util::variant("NEW");
    row["name"] = util::variant("newbies");
    row["description"] = util::variant("hello");
    row["created"] = util::variant(0);
    row["ranks"] = util::variant("leader,,,,,,,,member");
    row["bank"] = util::variant(0);
    Database_Result guild_row;
    guild_row.push_back(row);
    auto& db = *dynamic_cast<MockDatabase*>(server->world->db.get());
    EXPECT_CALL(db, RawQuery(testing::HasSubstr("FROM guilds WHERE tag"), testing::_, testing::_)).WillRepeatedly(testing::Return(guild_row));
    EXPECT_CALL(db, RawQuery(testing::HasSubstr("FROM characters WHERE guild"), testing::_, testing::_)).WillRepeatedly(testing::Return(Database_Result()));

    client::GuildCreateClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.guild_tag = "NEW";
    packet.guild_name = "newbies";
    packet.description = "hello";
    Handlers::Guild_Create(leader.character, packet);

    const auto reply = LastSent<server::GuildCreateServerPacket>(leader);
    ASSERT_TRUE(reply);
    EXPECT_EQ(leader.character->PlayerID(), reply->leader_player_id);
    EXPECT_EQ("NEW", reply->guild_tag);
    EXPECT_EQ(400, reply->gold_amount);
    ASSERT_TRUE(leader.character->guild);
    EXPECT_EQ(0, leader.character->guild_rank);

    const auto agree = LastSent<server::GuildAgreeServerPacket>(invited);
    ASSERT_TRUE(agree);
    EXPECT_EQ("member", agree->rank_name);

    guild = leader.character->guild;
}

TEST_F(GuildTests, Create_GuildNotStored_KeepsGold)
{
    PlayingCharacter& leader = GivenCharacterAtGuildNpc();
    PlayingCharacter& invited = GivenCharacterAtGuildNpc(4, 3);
    auto create = server->world->guildmanager->BeginCreate("NEW", "newbies", leader.character);
    create->AddMember(invited.character->real_name);

    client::GuildCreateClientPacket packet;
    packet.session_id = GuildSessionId;
    packet.guild_tag = "NEW";
    packet.guild_name = "newbies";
    packet.description = "hello";
    Handlers::Guild_Create(leader.character, packet);

    EXPECT_FALSE(LastSent<server::GuildCreateServerPacket>(leader));
    EXPECT_EQ(500, leader.character->HasItem(GoldId));
}
