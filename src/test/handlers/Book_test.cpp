#include "../testhelper/playing.hpp"

// include the CPP file with the Book functions in it for testing
#include "../../handlers/Book.cpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

namespace protocol = eolib::protocol;
namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short BookTestServerPort = 38130;
constexpr int BookTestMapId = 35;

class BookTests : public PlayingTest
{
protected:
    BookTests()
        : PlayingTest(BookTestServerPort, BookTestMapId)
    {
    }

    static client::BookRequestClientPacket Request(int player_id)
    {
        client::BookRequestClientPacket packet;
        packet.player_id = player_id;
        return packet;
    }
};

}

TEST_F(BookTests, Request_OtherCharacter_RepliesWithTargetDetails)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& reader = GivenCharacter(2, 2);
    PlayingCharacter& target = GivenCharacter(3, 3);
    target.character->title = "Hero";
    target.character->admin = protocol::AdminLevel::Guardian;

    Handlers::Book_Request(reader.character, Request(target.character->PlayerID()));

    const auto reply = LastSent<server::BookReplyServerPacket>(reader);
    ASSERT_TRUE(reply);
    EXPECT_EQ(target.character->SourceName(), reply->details.name);
    EXPECT_EQ("Hero", reply->details.title);
    EXPECT_EQ(target.character->PlayerID(), reply->details.player_id);
    EXPECT_EQ(protocol::AdminLevel::Guardian, reply->details.admin);
    EXPECT_EQ(server::CharacterIcon::Gm, reply->icon);
    EXPECT_TRUE(reply->quest_names.empty());
}

TEST_F(BookTests, Request_HiddenAdmin_HidesAdminIcon)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& reader = GivenCharacter(2, 2);
    PlayingCharacter& target = GivenCharacter(3, 3);
    target.character->admin = protocol::AdminLevel::GameMaster;
    target.character->hidden = Character::HideAdmin;

    Handlers::Book_Request(reader.character, Request(target.character->PlayerID()));

    const auto reply = LastSent<server::BookReplyServerPacket>(reader);
    ASSERT_TRUE(reply);
    EXPECT_EQ(server::CharacterIcon::Player, reply->icon);
    EXPECT_EQ(protocol::AdminLevel::Player, reply->details.admin);
}

TEST_F(BookTests, Request_UnknownPlayer_RepliesWithOwnDetails)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& reader = GivenCharacter(2, 2);

    Handlers::Book_Request(reader.character, Request(999));

    const auto reply = LastSent<server::BookReplyServerPacket>(reader);
    ASSERT_TRUE(reply);
    EXPECT_EQ(reader.character->PlayerID(), reply->details.player_id);
}
