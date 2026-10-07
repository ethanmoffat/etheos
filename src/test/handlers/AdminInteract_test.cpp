#include "../testhelper/playing.hpp"

// include the CPP file with the AdminInteract functions in it for testing
#include "../../handlers/AdminInteract.cpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

namespace protocol = eolib::protocol;
namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short AdminInteractTestServerPort = 38134;
constexpr int AdminInteractTestMapId = 39;

class AdminInteractTests : public PlayingTest
{
protected:
    AdminInteractTests()
        : PlayingTest(AdminInteractTestServerPort, AdminInteractTestMapId)
    {
    }

    void ConfigureServer(Config& config) override
    {
        config["OldReports"] = "false";
        config["LogReports"] = "false";
    }
};

}

TEST_F(AdminInteractTests, Tell_SendsMessageReplyToAdmins)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& player = GivenOnlineCharacter(2, 2);
    PlayingCharacter& admin = GivenOnlineCharacter(3, 3);
    PlayingCharacter& other = GivenOnlineCharacter(4, 4);
    admin.character->admin = protocol::AdminLevel::GameMaster;

    client::AdminInteractTellClientPacket packet;
    packet.message = "help";
    Handlers::AdminInteract_Tell(player.character, packet);

    const auto reply = LastSent<server::AdminInteractReplyServerPacket>(admin);
    ASSERT_TRUE(reply);
    ASSERT_EQ(server::AdminMessageType::Message, reply->message_type);
    EXPECT_EQ(player.character->SourceName(), reply->AsMessage()->player_name);
    EXPECT_EQ("help", reply->AsMessage()->message);
    EXPECT_FALSE(LastSent<server::AdminInteractReplyServerPacket>(other));
}

TEST_F(AdminInteractTests, Report_SendsReportReplyToAdmins)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& player = GivenOnlineCharacter(2, 2);
    PlayingCharacter& admin = GivenOnlineCharacter(3, 3);
    admin.character->admin = protocol::AdminLevel::GameMaster;

    client::AdminInteractReportClientPacket packet;
    packet.reportee = "griefer";
    packet.message = "spam";
    Handlers::AdminInteract_Report(player.character, packet);

    const auto reply = LastSent<server::AdminInteractReplyServerPacket>(admin);
    ASSERT_TRUE(reply);
    ASSERT_EQ(server::AdminMessageType::Report, reply->message_type);
    EXPECT_EQ(player.character->SourceName(), reply->AsReport()->player_name);
    EXPECT_EQ("spam", reply->AsReport()->message);
    EXPECT_EQ("griefer", reply->AsReport()->reportee_name);
}

TEST_F(AdminInteractTests, Hide_SendsRemoveToMap)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& admin = GivenCharacter(2, 2);
    PlayingCharacter& far = GivenCharacter(35, 35);

    admin.character->Hide(Character::HideInvisible);

    const auto remove = LastSent<server::AdminInteractRemoveServerPacket>(far);
    ASSERT_TRUE(remove);
    EXPECT_EQ(admin.character->PlayerID(), remove->player_id);
}

TEST_F(AdminInteractTests, Unhide_SendsAgreeToMap)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& admin = GivenCharacter(2, 2);
    PlayingCharacter& far = GivenCharacter(35, 35);
    admin.character->hidden = Character::HideInvisible;

    admin.character->Unhide(Character::HideInvisible);

    const auto agree = LastSent<server::AdminInteractAgreeServerPacket>(far);
    ASSERT_TRUE(agree);
    EXPECT_EQ(admin.character->PlayerID(), agree->player_id);
}
