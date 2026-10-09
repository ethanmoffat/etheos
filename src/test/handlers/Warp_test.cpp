#include "../testhelper/playing.hpp"

// include the CPP file with the Warp functions in it for testing
#include "../../handlers/Warp.cpp"
#include "../../converters.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <cstdint>
#include <vector>

namespace client = eolib::protocol::net::client;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short WarpTestServerPort = 38110;
constexpr int WarpTestMapId = 15;

class WarpTests : public PlayingTest
{
protected:
    WarpTests()
        : PlayingTest(WarpTestServerPort, WarpTestMapId)
    {
    }

    static client::WarpAcceptClientPacket WarpAccept(int map_id, int session_id)
    {
        client::WarpAcceptClientPacket packet;
        packet.map_id = map_id;
        packet.session_id = session_id;
        return packet;
    }

    static client::WarpTakeClientPacket WarpTake(int map_id, int session_id)
    {
        client::WarpTakeClientPacket packet;
        packet.map_id = map_id;
        packet.session_id = session_id;
        return packet;
    }

    static const SentPacket* LastSentRaw(const PlayingCharacter& entry, eolib::protocol::net::PacketFamily family, eolib::protocol::net::PacketAction action)
    {
        for (auto it = entry.sent.rbegin(); it != entry.sent.rend(); ++it)
        {
            if (it->family == family && it->action == action)
                return &*it;
        }

        return nullptr;
    }
};

}

TEST_F(WarpTests, Warp_SameMap_SendsLocalRequestWithSessionId)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& warper = GivenCharacter(2, 2);

    warper.character->Warp(WarpTestMapId, Converters::CoordsFrom(5, 6));

    const auto request = LastSent<server::WarpRequestServerPacket>(warper);
    ASSERT_TRUE(request);
    EXPECT_EQ(server::WarpType::Local, request->warp_type);
    EXPECT_EQ(WarpTestMapId, request->map_id);
    EXPECT_NE(0, request->session_id);
    EXPECT_EQ(warper.character->warp_session_id, request->session_id);
    EXPECT_EQ(5, warper.character->loc.x);
    EXPECT_EQ(6, warper.character->loc.y);
    EXPECT_EQ(WarpEffect::None, warper.character->warp_effect);
}

TEST_F(WarpTests, Warp_FromNowhere_SendsMapSwitchRequestWithMapFile)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& warper = GivenCharacter(2, 2);
    warper.character->nowhere = true;

    warper.character->Warp(WarpTestMapId, Converters::CoordsFrom(5, 6));

    const auto request = LastSent<server::WarpRequestServerPacket>(warper);
    ASSERT_TRUE(request);
    ASSERT_EQ(server::WarpType::MapSwitch, request->warp_type);
    const auto rid = map->ClientRid();
    EXPECT_EQ((std::vector<int>{rid[0], rid[1]}), request->AsMapSwitch()->map_rid);
    EXPECT_EQ(map->filesize, request->AsMapSwitch()->map_file_size);
    EXPECT_EQ(warper.character->warp_session_id, request->session_id);
}

TEST_F(WarpTests, Warp_ObserverInRange_SeesAvatarRemoveThenPlayersAgreeWithEffect)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& warper = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 3);

    warper.character->Warp(WarpTestMapId, Converters::CoordsFrom(4, 4), WarpEffect::Scroll);

    const auto avatar_remove = LastSent<server::AvatarRemoveServerPacket>(observer);
    ASSERT_TRUE(avatar_remove);
    EXPECT_EQ(warper.character->PlayerID(), avatar_remove->player_id);
    EXPECT_EQ(WarpEffect::Scroll, avatar_remove->warp_effect);

    const auto players_agree = LastSent<server::PlayersAgreeServerPacket>(observer);
    ASSERT_TRUE(players_agree);
    ASSERT_EQ(1u, players_agree->nearby.characters.size());
    const server::CharacterMapInfo& info = players_agree->nearby.characters.front();
    EXPECT_EQ(warper.character->PlayerID(), info.player_id);
    EXPECT_EQ(4, info.coords.x);
    EXPECT_EQ(4, info.coords.y);
    EXPECT_EQ(WarpEffect::Scroll, info.warp_effect);
}

TEST_F(WarpTests, Warp_NoEffect_AvatarRemoveOmitsEffect)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& warper = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 3);

    warper.character->Warp(WarpTestMapId, Converters::CoordsFrom(4, 4));

    const auto avatar_remove = LastSent<server::AvatarRemoveServerPacket>(observer);
    ASSERT_TRUE(avatar_remove);
    EXPECT_FALSE(avatar_remove->warp_effect);
}

// The official server sends FF <info> FF 01 here, which its client only reads by accident. This is the spec form.
TEST_F(WarpTests, Warp_ObserverInRange_PlayersAgreeUsesSpecChunkLayout)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& warper = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 3);

    warper.character->Warp(WarpTestMapId, Converters::CoordsFrom(4, 4));

    const SentPacket* players_agree = LastSentRaw(observer, server::PlayersAgreeServerPacket::FAMILY, server::PlayersAgreeServerPacket::ACTION);
    ASSERT_NE(nullptr, players_agree);
    const std::vector<std::uint8_t>& payload = players_agree->payload;
    ASSERT_GE(payload.size(), 4u);
    EXPECT_EQ(0x02, payload[0]);
    EXPECT_EQ(0xFF, payload[1]);
    EXPECT_EQ(0xFF, payload[payload.size() - 2]);
    EXPECT_EQ(0xFF, payload[payload.size() - 1]);
}

TEST_F(WarpTests, Accept_PendingWarp_SendsAgreeWithEffectAndNearby)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& warper = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 3);
    warper.character->Warp(WarpTestMapId, Converters::CoordsFrom(4, 4), WarpEffect::Admin);

    Handlers::Warp_Accept(warper.character, WarpAccept(WarpTestMapId, warper.character->warp_session_id));

    const auto agree = LastSent<server::WarpAgreeServerPacket>(warper);
    ASSERT_TRUE(agree);
    ASSERT_EQ(server::WarpType::MapSwitch, agree->warp_type);
    EXPECT_EQ(WarpTestMapId, agree->AsMapSwitch()->map_id);
    EXPECT_EQ(WarpEffect::Admin, agree->AsMapSwitch()->warp_effect);
    ASSERT_EQ(2u, agree->nearby.characters.size());
    EXPECT_EQ(observer.character->PlayerID(), agree->nearby.characters[0].player_id);
    EXPECT_EQ(warper.character->PlayerID(), agree->nearby.characters[1].player_id);
    EXPECT_EQ(Character::NoPendingWarp, warper.character->warp_effect);
}

TEST_F(WarpTests, Warp_WhilePending_KeepsSessionId)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& warper = GivenCharacter(2, 2);
    warper.character->Warp(WarpTestMapId, Converters::CoordsFrom(4, 4));
    const int session_id = warper.character->warp_session_id;

    warper.character->Warp(WarpTestMapId, Converters::CoordsFrom(5, 5));

    const auto request = LastSent<server::WarpRequestServerPacket>(warper);
    ASSERT_TRUE(request);
    EXPECT_EQ(session_id, request->session_id);
}

TEST_F(WarpTests, Accept_MismatchedSession_ClosesClient)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& warper = GivenCharacter(2, 2);
    warper.character->Warp(WarpTestMapId, Converters::CoordsFrom(4, 4));

    EXPECT_CALL(*warper.client, Close(false)).Times(1);

    Handlers::Warp_Accept(warper.character, WarpAccept(WarpTestMapId, warper.character->warp_session_id + 1));

    EXPECT_FALSE(LastSent<server::WarpAgreeServerPacket>(warper));
    EXPECT_EQ(WarpEffect::None, warper.character->warp_effect);
}

TEST_F(WarpTests, Accept_MismatchedMap_ClosesClient)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& warper = GivenCharacter(2, 2);
    warper.character->Warp(WarpTestMapId, Converters::CoordsFrom(4, 4));

    EXPECT_CALL(*warper.client, Close(false)).Times(1);

    Handlers::Warp_Accept(warper.character, WarpAccept(WarpTestMapId + 1, warper.character->warp_session_id));

    EXPECT_FALSE(LastSent<server::WarpAgreeServerPacket>(warper));
}

TEST_F(WarpTests, Accept_MismatchedSessionAndMapWithEnforceSessionsOff_SendsAgree)
{
    server->world->config["EnforceSessions"] = false;
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& warper = GivenCharacter(2, 2);
    warper.character->Warp(WarpTestMapId, Converters::CoordsFrom(4, 4));

    EXPECT_CALL(*warper.client, Close(testing::_)).Times(0);

    Handlers::Warp_Accept(warper.character, WarpAccept(WarpTestMapId + 1, warper.character->warp_session_id + 1));

    EXPECT_TRUE(LastSent<server::WarpAgreeServerPacket>(warper));
}

TEST_F(WarpTests, Accept_NoPendingWarp_IsIgnored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& warper = GivenCharacter(2, 2);
    warper.character->Warp(WarpTestMapId, Converters::CoordsFrom(4, 4));
    const int session_id = warper.character->warp_session_id;
    Handlers::Warp_Accept(warper.character, WarpAccept(WarpTestMapId, session_id));
    warper.sent.clear();

    EXPECT_CALL(*warper.client, Close(testing::_)).Times(0);

    Handlers::Warp_Accept(warper.character, WarpAccept(WarpTestMapId, session_id));

    EXPECT_FALSE(LastSent<server::WarpAgreeServerPacket>(warper));
}

TEST_F(WarpTests, Take_PendingWarp_UploadsMap)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& warper = GivenCharacter(2, 2);
    warper.character->nowhere = true;
    warper.character->Warp(WarpTestMapId, Converters::CoordsFrom(4, 4));

    Handlers::Warp_Take(warper.character, WarpTake(WarpTestMapId, warper.character->warp_session_id));

    const auto upload = LastSent<server::InitInitServerPacket>(warper);
    ASSERT_TRUE(upload);
    EXPECT_EQ(server::InitReply::WarpMap, upload->reply_code);
}

TEST_F(WarpTests, Take_MismatchedSession_ClosesClient)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& warper = GivenCharacter(2, 2);
    warper.character->nowhere = true;
    warper.character->Warp(WarpTestMapId, Converters::CoordsFrom(4, 4));

    EXPECT_CALL(*warper.client, Close(false)).Times(1);

    Handlers::Warp_Take(warper.character, WarpTake(WarpTestMapId, warper.character->warp_session_id + 1));

    EXPECT_FALSE(LastSent<server::InitInitServerPacket>(warper));
}
