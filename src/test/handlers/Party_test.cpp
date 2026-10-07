#include "../testhelper/playing.hpp"

// include the CPP file with the Party functions in it for testing
#include "../../handlers/Party.cpp"

#include "party.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <vector>

namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short PartyHandlerTestServerPort = 38129;
constexpr int PartyHandlerTestMapId = 34;

class PartyHandlerTests : public PlayingTest
{
protected:
    PartyHandlerTests()
        : PlayingTest(PartyHandlerTestServerPort, PartyHandlerTestMapId)
    {
    }

    void TearDown() override
    {
        const std::vector<Party*> parties = server->world->parties;
        for (Party* party : parties)
            delete party;

        PlayingTest::TearDown();
    }

    static client::PartyRequestClientPacket Request(PartyRequestType type, const PlayingCharacter& target)
    {
        client::PartyRequestClientPacket packet;
        packet.request_type = type;
        packet.player_id = target.character->PlayerID();
        return packet;
    }

    static client::PartyAcceptClientPacket Accept(PartyRequestType type, const PlayingCharacter& inviter)
    {
        client::PartyAcceptClientPacket packet;
        packet.request_type = type;
        packet.inviter_player_id = inviter.character->PlayerID();
        return packet;
    }

    static client::PartyRemoveClientPacket Remove(const PlayingCharacter& target)
    {
        client::PartyRemoveClientPacket packet;
        packet.player_id = target.character->PlayerID();
        return packet;
    }

    void GivenParty(PlayingCharacter& leader, PlayingCharacter& member)
    {
        Handlers::Party_Request(leader.character, Request(PartyRequestType::Invite, member));
        Handlers::Party_Accept(member.character, Accept(PartyRequestType::Invite, leader));
        ASSERT_TRUE(leader.character->party);
    }
};

}

TEST_F(PartyHandlerTests, Request_Invite_SendsRequestToInvitee)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& inviter = GivenCharacter(2, 2);
    PlayingCharacter& invitee = GivenCharacter(3, 3);

    Handlers::Party_Request(inviter.character, Request(PartyRequestType::Invite, invitee));

    const auto request = LastSent<server::PartyRequestServerPacket>(invitee);
    ASSERT_TRUE(request);
    EXPECT_EQ(PartyRequestType::Invite, request->request_type);
    EXPECT_EQ(inviter.character->PlayerID(), request->inviter_player_id);
    EXPECT_EQ(inviter.character->SourceName(), request->player_name);
}

TEST_F(PartyHandlerTests, Request_OutOfRange_Ignored)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& inviter = GivenCharacter(2, 2);
    PlayingCharacter& invitee = GivenCharacter(35, 35);

    Handlers::Party_Request(inviter.character, Request(PartyRequestType::Join, invitee));

    EXPECT_TRUE(invitee.sent.empty());
}

TEST_F(PartyHandlerTests, Accept_Invite_CreatesPartyLedByInviter)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& inviter = GivenCharacter(2, 2);
    PlayingCharacter& invitee = GivenCharacter(3, 3);
    Handlers::Party_Request(inviter.character, Request(PartyRequestType::Invite, invitee));

    Handlers::Party_Accept(invitee.character, Accept(PartyRequestType::Invite, inviter));

    const auto create = LastSent<server::PartyCreateServerPacket>(invitee);
    ASSERT_TRUE(create);
    ASSERT_EQ(2u, create->members.size());
    EXPECT_EQ(inviter.character->PlayerID(), create->members[0].player_id);
    EXPECT_TRUE(create->members[0].leader);
    EXPECT_EQ(invitee.character->PlayerID(), create->members[1].player_id);
    EXPECT_FALSE(create->members[1].leader);
    EXPECT_TRUE(LastSent<server::PartyCreateServerPacket>(inviter));
}

TEST_F(PartyHandlerTests, Accept_WithoutRequest_Ignored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& inviter = GivenCharacter(2, 2);
    PlayingCharacter& invitee = GivenCharacter(3, 3);

    Handlers::Party_Accept(invitee.character, Accept(PartyRequestType::Invite, inviter));

    EXPECT_FALSE(invitee.character->party);
}

TEST_F(PartyHandlerTests, Accept_JoinLargeParty_SendsAddToMembers)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& leader = GivenOnlineCharacter(2, 2);
    PlayingCharacter& member = GivenOnlineCharacter(3, 3);
    PlayingCharacter& joiner = GivenOnlineCharacter(4, 4);
    GivenParty(leader, member);

    Handlers::Party_Request(joiner.character, Request(PartyRequestType::Join, leader));
    Handlers::Party_Accept(leader.character, Accept(PartyRequestType::Join, joiner));

    const auto add = LastSent<server::PartyAddServerPacket>(member);
    ASSERT_TRUE(add);
    EXPECT_EQ(joiner.character->PlayerID(), add->member.player_id);
    EXPECT_FALSE(add->member.leader);
    EXPECT_EQ(joiner.character->SourceName(), add->member.name);
    EXPECT_EQ(leader.character->party, joiner.character->party);
}

TEST_F(PartyHandlerTests, Remove_MemberOfLargeParty_SendsRemoveToOthersAndCloseToLeaver)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& leader = GivenOnlineCharacter(2, 2);
    PlayingCharacter& member = GivenOnlineCharacter(3, 3);
    PlayingCharacter& leaver = GivenOnlineCharacter(4, 4);
    GivenParty(leader, member);
    Handlers::Party_Request(leader.character, Request(PartyRequestType::Invite, leaver));
    Handlers::Party_Accept(leaver.character, Accept(PartyRequestType::Invite, leader));

    Handlers::Party_Remove(leaver.character, Remove(leaver));

    const auto remove = LastSent<server::PartyRemoveServerPacket>(member);
    ASSERT_TRUE(remove);
    EXPECT_EQ(leaver.character->PlayerID(), remove->player_id);
    EXPECT_TRUE(LastSent<server::PartyCloseServerPacket>(leaver));
    EXPECT_FALSE(leaver.character->party);
    EXPECT_TRUE(member.character->party);
}

TEST_F(PartyHandlerTests, Remove_OtherMemberAsNonLeader_Ignored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& leader = GivenOnlineCharacter(2, 2);
    PlayingCharacter& member = GivenOnlineCharacter(3, 3);
    GivenParty(leader, member);

    Handlers::Party_Remove(member.character, Remove(leader));

    EXPECT_TRUE(leader.character->party);
}

TEST_F(PartyHandlerTests, Take_InParty_SendsList)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& leader = GivenCharacter(2, 2);
    PlayingCharacter& member = GivenCharacter(3, 3);
    GivenParty(leader, member);

    Handlers::Party_Take(member.character, client::PartyTakeClientPacket());

    const auto list = LastSent<server::PartyListServerPacket>(member);
    ASSERT_TRUE(list);
    ASSERT_EQ(2u, list->members.size());
    EXPECT_EQ(leader.character->PlayerID(), list->members[0].player_id);
}
