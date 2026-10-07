#include "../testhelper/playing.hpp"

// include the CPP file with the Board functions in it for testing
#include "../../handlers/Board.cpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

namespace protocol = eolib::protocol;
namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short BoardTestServerPort = 38131;
constexpr int BoardTestMapId = 36;
constexpr int BoardX = 3;
constexpr int BoardY = 2;

class BoardTests : public PlayingTest
{
protected:
    BoardTests()
        : PlayingTest(BoardTestServerPort, BoardTestMapId)
    {
    }

    void ConfigureServer(Config& config) override
    {
        config["BoardDatePosts"] = "false";
        config["BoardMaxUserRecentPosts"] = "2";
    }

    void GivenBoardMap()
    {
        eolib::protocol::map::Emf file = EmptyEmf(10, 10);
        AddTileSpec(file, BoardX, BoardY, eolib::protocol::map::MapTileSpec::Board1);
        GivenMap(file);
    }

    static client::BoardOpenClientPacket Open(int board_id)
    {
        client::BoardOpenClientPacket packet;
        packet.board_id = board_id;
        return packet;
    }

    static client::BoardCreateClientPacket Create(const std::string& subject, const std::string& body)
    {
        client::BoardCreateClientPacket packet;
        packet.post_subject = subject;
        packet.post_body = body;
        return packet;
    }
};

}

TEST_F(BoardTests, Open_InRangeOfBoard_RepliesWithPosts)
{
    GivenBoardMap();
    PlayingCharacter& user = GivenCharacter(2, 2);

    Handlers::Board_Open(user.character, Open(0));

    const auto reply = LastSent<server::BoardOpenServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(1, reply->board_id);
    EXPECT_TRUE(reply->posts.empty());
}

TEST_F(BoardTests, Open_NoBoardInRange_Ignored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& user = GivenCharacter(2, 2);

    Handlers::Board_Open(user.character, Open(0));

    EXPECT_TRUE(user.sent.empty());
}

// The trailing space on the author name tells the client that the user can post again
TEST_F(BoardTests, Create_OpenBoard_AddsPostAndShowsBoard)
{
    GivenBoardMap();
    PlayingCharacter& user = GivenCharacter(2, 2);
    Handlers::Board_Open(user.character, Open(0));

    Handlers::Board_Create(user.character, Create("Subject", "Body"));

    const auto reply = LastSent<server::BoardOpenServerPacket>(user);
    ASSERT_TRUE(reply);
    ASSERT_EQ(1u, reply->posts.size());
    EXPECT_EQ(user.character->SourceName() + " ", reply->posts[0].author);
    EXPECT_EQ("Subject", reply->posts[0].subject);
}

TEST_F(BoardTests, Take_ExistingPost_RepliesWithBody)
{
    GivenBoardMap();
    PlayingCharacter& user = GivenCharacter(2, 2);
    Handlers::Board_Open(user.character, Open(0));
    Handlers::Board_Create(user.character, Create("Subject", "Body"));
    const int post_id = LastSent<server::BoardOpenServerPacket>(user)->posts[0].post_id;

    client::BoardTakeClientPacket packet;
    packet.post_id = post_id;
    Handlers::Board_Take(user.character, packet);

    const auto reply = LastSent<server::BoardPlayerServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(post_id, reply->post_id);
    EXPECT_EQ("Body", reply->post_body);
}

TEST_F(BoardTests, Take_UnknownPost_Ignored)
{
    GivenBoardMap();
    PlayingCharacter& user = GivenCharacter(2, 2);
    Handlers::Board_Open(user.character, Open(0));

    client::BoardTakeClientPacket packet;
    packet.post_id = 99;
    Handlers::Board_Take(user.character, packet);

    EXPECT_FALSE(LastSent<server::BoardPlayerServerPacket>(user));
}

TEST_F(BoardTests, Remove_OwnPost_RemovesPostAndShowsBoard)
{
    GivenBoardMap();
    PlayingCharacter& user = GivenCharacter(2, 2);
    user.character->admin = protocol::AdminLevel::GameMaster;
    Handlers::Board_Open(user.character, Open(0));
    Handlers::Board_Create(user.character, Create("Subject", "Body"));

    client::BoardRemoveClientPacket packet;
    packet.post_id = LastSent<server::BoardOpenServerPacket>(user)->posts[0].post_id;
    Handlers::Board_Remove(user.character, packet);

    const auto reply = LastSent<server::BoardOpenServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_TRUE(reply->posts.empty());
}
