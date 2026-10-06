#include "../testhelper/playing.hpp"

// include the CPP file with the Attack functions in it for testing
#include "../../handlers/Attack.cpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short AttackTestServerPort = 38118;
constexpr int AttackTestMapId = 23;

class AttackTests : public PlayingTest
{
protected:
    AttackTests()
        : PlayingTest(AttackTestServerPort, AttackTestMapId)
    {
    }

    void ConfigureServer(Config& config) override
    {
        config["EnforceTimestamps"] = false;
    }

    static client::AttackUseClientPacket Use(Direction direction)
    {
        client::AttackUseClientPacket packet;
        packet.direction = direction;
        return packet;
    }
};

}

TEST_F(AttackTests, Use_TimestampsNotEnforced_SendsFacingDirectionToOthersInRange)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& attacker = GivenCharacter(3, 3);
    PlayingCharacter& near = GivenCharacter(4, 4);
    PlayingCharacter& far = GivenCharacter(35, 35);

    Handlers::Attack_Use(attacker.character, Use(Direction::Left));

    const auto packet = LastSent<server::AttackPlayerServerPacket>(near);
    ASSERT_TRUE(packet);
    EXPECT_EQ(attacker.character->PlayerID(), packet->player_id);
    EXPECT_EQ(Direction::Down, packet->direction);
    EXPECT_FALSE(LastSent<server::AttackPlayerServerPacket>(attacker));
    EXPECT_FALSE(LastSent<server::AttackPlayerServerPacket>(far));
}

TEST_F(AttackTests, Use_Sitting_Ignored)
{
    GivenMap(EmptyEmf(40, 40));
    PlayingCharacter& attacker = GivenCharacter(3, 3);
    PlayingCharacter& near = GivenCharacter(4, 4);
    attacker.character->sitting = SitState::Floor;

    Handlers::Attack_Use(attacker.character, Use(Direction::Left));

    EXPECT_FALSE(LastSent<server::AttackPlayerServerPacket>(near));
    EXPECT_EQ(0, attacker.character->attacks);
}
