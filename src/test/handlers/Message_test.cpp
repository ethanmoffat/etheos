#include "../testhelper/playing.hpp"

// include the CPP file with the Message functions in it for testing
#include "../../handlers/Message.cpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short MessageTestServerPort = 38135;
constexpr int MessageTestMapId = 40;

class MessageTests : public PlayingTest
{
protected:
    MessageTests()
        : PlayingTest(MessageTestServerPort, MessageTestMapId)
    {
    }
};

}

TEST_F(MessageTests, Ping_RepliesPong)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& user = GivenCharacter(2, 2);

    Handlers::Message_Ping(user.character, client::MessagePingClientPacket());

    EXPECT_TRUE(LastSent<server::MessagePongServerPacket>(user));
}

TEST_F(MessageTests, StatusMsg_SendsOpen)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& user = GivenCharacter(2, 2);

    user.character->StatusMsg("status");

    const auto open = LastSent<server::MessageOpenServerPacket>(user);
    ASSERT_TRUE(open);
    EXPECT_EQ("status", open->message);
}

TEST_F(MessageTests, ShowInfoBox_SendsAcceptWithFourMessages)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& user = GivenCharacter(2, 2);

    user.character->ShowInfoBox("title", "content");

    const auto accept = LastSent<server::MessageAcceptServerPacket>(user);
    ASSERT_TRUE(accept);
    const std::vector<std::string> expected{"title", "content", "", ""};
    EXPECT_EQ(expected, accept->messages);
}
