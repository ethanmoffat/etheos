#include "../testhelper/playing.hpp"

// include the CPP file with the Emote functions in it for testing
#include "../../handlers/Emote.cpp"

#include "../../timer.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <tuple>

namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short EmoteTestServerPort = 38107;
constexpr int EmoteTestMapId = 12;

class EmoteTests : public PlayingTest
{
protected:
    EmoteTests()
        : PlayingTest(EmoteTestServerPort, EmoteTestMapId)
    {
    }
};

}


TEST_F(EmoteTests, Report_AllowedEmote_ShownToNearbyOnly)
{
    GivenMap(EmptyEmf(30, 10));
    PlayingCharacter& emoter = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 2);
    PlayingCharacter& far_away = GivenCharacter(20, 2);

    client::EmoteReportClientPacket packet;
    packet.emote = eolib::protocol::Emote::Happy;
    Handlers::Emote_Report(emoter.character, packet);

    EXPECT_FALSE(LastSent<server::EmotePlayerServerPacket>(emoter));
    EXPECT_FALSE(LastSent<server::EmotePlayerServerPacket>(far_away));

    const auto emote = LastSent<server::EmotePlayerServerPacket>(observer);
    ASSERT_TRUE(emote);
    EXPECT_EQ(emoter.character->PlayerID(), emote->player_id);
    EXPECT_EQ(eolib::protocol::Emote::Happy, emote->emote);
}

class EmoteReportTests : public EmoteTests, public testing::WithParamInterface<std::tuple<int, bool>>
{
};

TEST_P(EmoteReportTests, Report_Emote_IsShownWhenAllowed)
{
    const int emote_value = std::get<0>(GetParam());
    const bool allowed = std::get<1>(GetParam());

    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& emoter = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 2);

    client::EmoteReportClientPacket packet;
    packet.emote = static_cast<eolib::protocol::Emote>(emote_value);
    Handlers::Emote_Report(emoter.character, packet);

    const auto emote = LastSent<server::EmotePlayerServerPacket>(observer);
    ASSERT_EQ(allowed, emote.has_value());
    if (allowed)
    {
        EXPECT_EQ(emote_value, static_cast<int>(emote->emote));
    }
}

INSTANTIATE_TEST_SUITE_P(Emotes, EmoteReportTests, testing::Values(
    std::make_tuple(0, false),
    std::make_tuple(1, true),
    std::make_tuple(10, true),
    std::make_tuple(11, false),
    std::make_tuple(12, false),
    std::make_tuple(13, false),
    std::make_tuple(14, true),
    std::make_tuple(15, false)));

TEST_F(EmoteTests, Report_DrunkWhileDrunk_ShownToNearby)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& emoter = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 2);
    emoter.character->drunk_until = Timer::GetTime() + Character::DrunkSeconds(0);

    client::EmoteReportClientPacket packet;
    packet.emote = eolib::protocol::Emote::Drunk;
    Handlers::Emote_Report(emoter.character, packet);

    const auto emote = LastSent<server::EmotePlayerServerPacket>(observer);
    ASSERT_TRUE(emote);
    EXPECT_EQ(eolib::protocol::Emote::Drunk, emote->emote);
}

TEST_F(EmoteTests, Report_DrunkAfterDrunkExpired_Ignored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& emoter = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 2);
    emoter.character->drunk_until = Timer::GetTime() - 1.0;

    client::EmoteReportClientPacket packet;
    packet.emote = eolib::protocol::Emote::Drunk;
    Handlers::Emote_Report(emoter.character, packet);

    EXPECT_FALSE(LastSent<server::EmotePlayerServerPacket>(observer));
}
