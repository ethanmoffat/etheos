#include "../testhelper/playing.hpp"

// include the CPP file with the Paperdoll functions in it for testing
#include "../../handlers/Paperdoll.cpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <cstdio>

namespace protocol = eolib::protocol;
namespace client = eolib::protocol::net::client;
namespace pub = eolib::protocol::pub;
namespace server = eolib::protocol::net::server;

namespace
{

constexpr unsigned short PaperdollTestServerPort = 38124;
constexpr int PaperdollTestMapId = 29;
constexpr int HatId = 2;
constexpr int RingId = 3;
constexpr int CursedHatId = 4;
constexpr int HatGraphic = 7;

class PaperdollTests : public PlayingTest
{
protected:
    PaperdollTests()
        : PlayingTest(PaperdollTestServerPort, PaperdollTestMapId)
    {
    }

    void ConfigureServer(Config& config) override
    {
        pub::EifRecord gold;
        gold.name = "Gold";

        pub::EifRecord hat;
        hat.name = "Hat";
        hat.type = pub::ItemType::Hat;
        hat.spec1 = HatGraphic;

        pub::EifRecord ring;
        ring.name = "Ring";
        ring.type = pub::ItemType::Ring;

        pub::EifRecord cursed_hat = hat;
        cursed_hat.name = "Cursed Hat";
        cursed_hat.special = pub::ItemSpecial::Cursed;

        pub::Eif eif;
        eif.rid = {1, 2};
        eif.total_items_count = 4;
        eif.items = {gold, hat, ring, cursed_hat};
        WriteFile("paperdoll_test.eif", eif);

        config["EIF"] = "paperdoll_test.eif";
    }

    void TearDown() override
    {
        PlayingTest::TearDown();
        std::remove("paperdoll_test.eif");
    }

    static client::PaperdollRequestClientPacket Request(int player_id)
    {
        client::PaperdollRequestClientPacket packet;
        packet.player_id = player_id;
        return packet;
    }

    static client::PaperdollAddClientPacket Add(int item_id)
    {
        client::PaperdollAddClientPacket packet;
        packet.item_id = item_id;
        return packet;
    }

    static client::PaperdollRemoveClientPacket Remove(int item_id)
    {
        client::PaperdollRemoveClientPacket packet;
        packet.item_id = item_id;
        return packet;
    }
};

}

TEST_F(PaperdollTests, Request_Player_RepliesWithDetailsEquipmentAndIcon)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenCharacter(2, 2);
    PlayingCharacter& target = GivenCharacter(3, 2);
    target.character->admin = protocol::AdminLevel::Guardian;
    target.character->paperdoll[Character::Hat] = HatId;

    Handlers::Paperdoll_Request(requester.character, Request(target.character->PlayerID()));

    const auto reply = LastSent<server::PaperdollReplyServerPacket>(requester);
    ASSERT_TRUE(reply);
    EXPECT_EQ(target.character->PlayerID(), static_cast<unsigned int>(reply->details.player_id));
    EXPECT_EQ(target.character->SourceName(), reply->details.name);
    EXPECT_EQ(protocol::AdminLevel::Guardian, reply->details.admin);
    EXPECT_EQ(HatId, reply->equipment.hat);
    EXPECT_EQ(server::CharacterIcon::Gm, reply->icon);
}

TEST_F(PaperdollTests, Request_HiddenAdmin_ShownAsPlayer)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenCharacter(2, 2);
    PlayingCharacter& target = GivenCharacter(3, 2);
    target.character->admin = protocol::AdminLevel::GameMaster;
    target.character->hidden = Character::HideAdmin;

    Handlers::Paperdoll_Request(requester.character, Request(target.character->PlayerID()));

    const auto reply = LastSent<server::PaperdollReplyServerPacket>(requester);
    ASSERT_TRUE(reply);
    EXPECT_EQ(protocol::AdminLevel::Player, reply->details.admin);
    EXPECT_EQ(server::CharacterIcon::Player, reply->icon);
}

TEST_F(PaperdollTests, Request_UnknownPlayer_Ignored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& requester = GivenCharacter(2, 2);

    Handlers::Paperdoll_Request(requester.character, Request(999));

    EXPECT_TRUE(requester.sent.empty());
}

TEST_F(PaperdollTests, Add_VisibleItem_RepliesAgreeAndTellsNearby)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& user = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 2);
    user.character->AddItem(HatId, 3);

    Handlers::Paperdoll_Add(user.character, Add(HatId));

    const auto reply = LastSent<server::PaperdollAgreeServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(HatId, reply->item_id);
    EXPECT_EQ(2, reply->remaining_amount);
    ASSERT_TRUE(reply->change.AsEquipment());
    EXPECT_EQ(HatGraphic, reply->change.AsEquipment()->equipment.hat);

    const auto avatar = LastSent<server::AvatarAgreeServerPacket>(observer);
    ASSERT_TRUE(avatar);
    EXPECT_EQ(user.character->PlayerID(), static_cast<unsigned int>(avatar->change.player_id));
}

TEST_F(PaperdollTests, Add_InvisibleItem_DoesNotTellNearby)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& user = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 2);
    user.character->AddItem(RingId, 1);

    Handlers::Paperdoll_Add(user.character, Add(RingId));

    EXPECT_TRUE(LastSent<server::PaperdollAgreeServerPacket>(user));
    EXPECT_EQ(RingId, user.character->paperdoll[Character::Ring1]);
    EXPECT_FALSE(LastSent<server::AvatarAgreeServerPacket>(observer));
}

TEST_F(PaperdollTests, Remove_EquippedItem_RepliesRemoveAndTellsNearby)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& user = GivenCharacter(2, 2);
    PlayingCharacter& observer = GivenCharacter(3, 2);
    user.character->AddItem(HatId, 1);
    ASSERT_TRUE(user.character->Equip(HatId, 0));

    Handlers::Paperdoll_Remove(user.character, Remove(HatId));

    const auto reply = LastSent<server::PaperdollRemoveServerPacket>(user);
    ASSERT_TRUE(reply);
    EXPECT_EQ(HatId, reply->item_id);
    EXPECT_EQ(1, user.character->HasItem(HatId));
    EXPECT_TRUE(LastSent<server::AvatarAgreeServerPacket>(observer));
}

TEST_F(PaperdollTests, Remove_CursedItem_Ignored)
{
    GivenMap(EmptyEmf(10, 10));
    PlayingCharacter& user = GivenCharacter(2, 2);
    user.character->AddItem(CursedHatId, 1);
    ASSERT_TRUE(user.character->Equip(CursedHatId, 0));

    Handlers::Paperdoll_Remove(user.character, Remove(CursedHatId));

    EXPECT_TRUE(user.sent.empty());
    EXPECT_EQ(CursedHatId, user.character->paperdoll[Character::Hat]);
}
