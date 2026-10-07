#include "../testhelper/playing.hpp"

// include the CPP file with the Talk functions in it for testing
#include "../../handlers/Talk.cpp"

#include "party.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <memory>

namespace protocol = eolib::protocol;
namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short TalkTestServerPort = 38128;
constexpr int TalkTestMapId = 33;

class TalkTests : public PlayingTest
{
protected:
    TalkTests()
        : PlayingTest(TalkTestServerPort, TalkTestMapId)
    {
    }

    void TearDown() override
    {
        party.reset();
        PlayingTest::TearDown();
    }

    std::unique_ptr<Party> party;
};

}

TEST_F(TalkTests, Tell_OnlineCharacter_SendsTellToRecipient)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& sender = GivenOnlineCharacter(2, 2);
    PlayingCharacter& recipient = GivenOnlineCharacter(3, 3);
    recipient.character->whispers = true;

    client::TalkTellClientPacket packet;
    packet.name = recipient.character->SourceName();
    packet.message = "hello";
    Handlers::Talk_Tell(sender.character, packet);

    const auto tell = LastSent<server::TalkTellServerPacket>(recipient);
    ASSERT_TRUE(tell);
    EXPECT_EQ(sender.character->SourceName(), tell->player_name);
    EXPECT_EQ("hello", tell->message);
}

TEST_F(TalkTests, Tell_UnknownCharacter_RepliesNotFound)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& sender = GivenOnlineCharacter(2, 2);

    client::TalkTellClientPacket packet;
    packet.name = "nobody";
    packet.message = "hello";
    Handlers::Talk_Tell(sender.character, packet);

    const auto reply = LastSent<server::TalkReplyServerPacket>(sender);
    ASSERT_TRUE(reply);
    EXPECT_EQ(server::TalkReply::NotFound, reply->reply_code);
    EXPECT_EQ("nobody", reply->name);
}

TEST_F(TalkTests, Report_PublicMessage_SendsPlayerToOthersInRange)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& speaker = GivenCharacter(2, 2);
    PlayingCharacter& nearby = GivenCharacter(3, 3);
    PlayingCharacter& far = GivenCharacter(35, 35);

    client::TalkReportClientPacket packet;
    packet.message = "hello";
    Handlers::Talk_Report(speaker.character, packet);

    const auto message = LastSent<server::TalkPlayerServerPacket>(nearby);
    ASSERT_TRUE(message);
    EXPECT_EQ(speaker.character->PlayerID(), message->player_id);
    EXPECT_EQ("hello", message->message);
    EXPECT_FALSE(LastSent<server::TalkPlayerServerPacket>(speaker));
    EXPECT_FALSE(LastSent<server::TalkPlayerServerPacket>(far));
}

TEST_F(TalkTests, Msg_GlobalMessage_SendsMsgToOthersOnline)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& speaker = GivenOnlineCharacter(2, 2);
    PlayingCharacter& other = GivenOnlineCharacter(3, 3);

    client::TalkMsgClientPacket packet;
    packet.message = "hello";
    Handlers::Talk_Msg(speaker.character, packet);

    const auto message = LastSent<server::TalkMsgServerPacket>(other);
    ASSERT_TRUE(message);
    EXPECT_EQ(speaker.character->SourceName(), message->player_name);
    EXPECT_EQ("hello", message->message);
    EXPECT_FALSE(LastSent<server::TalkMsgServerPacket>(speaker));
}

TEST_F(TalkTests, Open_InParty_SendsOpenToOtherMembers)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& speaker = GivenCharacter(2, 2);
    PlayingCharacter& member = GivenCharacter(3, 3);
    party = std::make_unique<Party>(server->world, speaker.character, member.character);

    client::TalkOpenClientPacket packet;
    packet.message = "hello";
    Handlers::Talk_Open(speaker.character, packet);

    const auto message = LastSent<server::TalkOpenServerPacket>(member);
    ASSERT_TRUE(message);
    EXPECT_EQ(speaker.character->PlayerID(), message->player_id);
    EXPECT_EQ("hello", message->message);
}

TEST_F(TalkTests, Admin_Player_Ignored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& speaker = GivenOnlineCharacter(2, 2);
    PlayingCharacter& admin = GivenOnlineCharacter(3, 3);
    admin.character->admin = protocol::AdminLevel::GameMaster;

    client::TalkAdminClientPacket packet;
    packet.message = "hello";
    Handlers::Talk_Admin(speaker.character, packet);

    EXPECT_FALSE(LastSent<server::TalkAdminServerPacket>(admin));
}

TEST_F(TalkTests, Admin_Guardian_SendsAdminToAdmins)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& speaker = GivenOnlineCharacter(2, 2);
    PlayingCharacter& admin = GivenOnlineCharacter(3, 3);
    PlayingCharacter& player = GivenOnlineCharacter(4, 4);
    speaker.character->admin = protocol::AdminLevel::LightGuide;
    admin.character->admin = protocol::AdminLevel::LightGuide;

    client::TalkAdminClientPacket packet;
    packet.message = "hello";
    Handlers::Talk_Admin(speaker.character, packet);

    const auto message = LastSent<server::TalkAdminServerPacket>(admin);
    ASSERT_TRUE(message);
    EXPECT_EQ("hello", message->message);
    EXPECT_FALSE(LastSent<server::TalkAdminServerPacket>(player));
}

TEST_F(TalkTests, Announce_Guardian_SendsAnnounceToOthersOnline)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& speaker = GivenOnlineCharacter(2, 2);
    PlayingCharacter& other = GivenOnlineCharacter(3, 3);
    speaker.character->admin = protocol::AdminLevel::LightGuide;

    client::TalkAnnounceClientPacket packet;
    packet.message = "hello";
    Handlers::Talk_Announce(speaker.character, packet);

    const auto message = LastSent<server::TalkAnnounceServerPacket>(other);
    ASSERT_TRUE(message);
    EXPECT_EQ(speaker.character->SourceName(), message->player_name);
    EXPECT_EQ("hello", message->message);
}
