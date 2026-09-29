#include "testhelper/mocks.hpp"
#include "testhelper/setup.hpp"

#include "config.hpp"
#include "console.hpp"
#include "eoclient.hpp"
#include "eoserver.hpp"
#include "packet.hpp"
#include "world.hpp"
#include "handlers/handlers.hpp"

#include <eolib/data/eo_writer.hpp>
#include <eolib/errors.hpp>
#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <memory>
#include <stdexcept>
#include <string>

namespace net = eolib::protocol::net;

static constexpr unsigned short PacketDispatchTestServerPort = 38081;

namespace
{

int typed_calls;
int last_player_id;

void Handle_PaperdollRequest(EOClient*, const net::client::PaperdollRequestClientPacket& packet)
{
    ++typed_calls;
    last_player_id = packet.player_id;
}

void Handle_PlayerRangeRequest(Player*, const net::client::PlayerRangeRequestClientPacket&)
{
    ++typed_calls;
}

// A packet that always fails to deserialize
class ThrowingRefreshRequestPacket final : public net::Packet
{
public:
    static constexpr net::PacketFamily FAMILY = net::PacketFamily::Refresh;
    static constexpr net::PacketAction ACTION = net::PacketAction::Request;

    net::PacketFamily Family() const noexcept override { return FAMILY; }
    net::PacketAction Action() const noexcept override { return ACTION; }
    int ByteSize() const noexcept override { return 0; }
    void Serialize(eolib::data::EoWriter&) const override { }
    void Deserialize(eolib::data::EoReader&) override { throw eolib::DeserializationError("test failure"); }
    std::string ToString() const override { return "ThrowingRefreshRequestPacket"; }
};

void Handle_ThrowingRefreshRequest(EOClient*, const ThrowingRefreshRequestPacket&)
{
    ++typed_calls;
}

template <typename TPacket> std::string Payload(const TPacket& packet)
{
    eolib::data::EoWriter writer;
    packet.Serialize(writer);
    return writer.ToByteString();
}

}

class PacketDispatchTests : public ::testing::Test
{
protected:
    Config config, admin_config;
    std::unique_ptr<EOServer> server;
    std::unique_ptr<MockClient> client;

    void SetUp() override
    {
        Console::SuppressOutput(true);

        CreateConfigWithTestDefaults(config, admin_config);

        auto mockDatabase = CreateMockDatabase();
        auto mockDatabaseFactory = CreateMockDatabaseFactory(mockDatabase);

        server = std::make_unique<EOServer>(IPAddress("127.0.0.1"), PacketDispatchTestServerPort, mockDatabaseFactory, config, admin_config);
        client = std::make_unique<MockClient>(server.get());
        EXPECT_CALL(*client, Connected()).WillRepeatedly(Return(true));
        client->state = EOClient::Initialized;

        Handlers::packet_handler_register_helper<PACKET_PAPERDOLL>().Register(Handle_PaperdollRequest, Handlers::Menu);
        Handlers::packet_handler_register_helper<PACKET_PLAYER_RANGE>().Register(Handle_PlayerRangeRequest, Handlers::Menu);
        Handlers::packet_handler_register_helper<PACKET_REFRESH>().Register(Handle_ThrowingRefreshRequest, Handlers::Menu);

        typed_calls = 0;
        last_player_id = 0;
    }

    void TearDown() override
    {
        client.reset();
        server.reset();
    }
};

TEST_F(PacketDispatchTests, Handle_TypedHandler_ReceivesDeserializedPacket)
{
    EXPECT_CALL(*client, Close(_)).Times(0);

    net::client::PaperdollRequestClientPacket packet;
    packet.player_id = 1234;

    Handlers::Handle(client.get(), QueuedPacket{packet.FAMILY, packet.ACTION, Payload(packet)});

    EXPECT_EQ(1, typed_calls);
    EXPECT_EQ(1234, last_player_id);
}

TEST_F(PacketDispatchTests, Handle_TypedHandlerWrongState_IsNotCalled)
{
    EXPECT_CALL(*client, Close(_)).Times(0);
    client->state = EOClient::Uninitialized;

    net::client::PaperdollRequestClientPacket packet;
    Handlers::Handle(client.get(), QueuedPacket{packet.FAMILY, packet.ACTION, Payload(packet)});

    EXPECT_EQ(0, typed_calls);
}

TEST_F(PacketDispatchTests, Handle_PlayerHandlerBeforeLogin_ThrowsRuntimeError)
{
    net::client::PlayerRangeRequestClientPacket packet;

    EXPECT_THROW(Handlers::Handle(client.get(), QueuedPacket{packet.FAMILY, packet.ACTION, Payload(packet)}), std::runtime_error);
    EXPECT_EQ(0, typed_calls);
}

TEST_F(PacketDispatchTests, Handle_DeserializeFails_DropsPacket)
{
    EXPECT_CALL(*client, Close(_)).Times(0);

    Handlers::Handle(client.get(), QueuedPacket{net::PacketFamily::Refresh, net::PacketAction::Request, ""});

    EXPECT_EQ(0, typed_calls);
}

TEST_F(PacketDispatchTests, Handle_DeserializeFailsWithEnforcePacketFormat_ClosesClient)
{
    server->world->config["EnforcePacketFormat"] = true;
    EXPECT_CALL(*client, Close(false)).Times(1);

    Handlers::Handle(client.get(), QueuedPacket{net::PacketFamily::Refresh, net::PacketAction::Request, ""});

    EXPECT_EQ(0, typed_calls);
}

TEST_F(PacketDispatchTests, Handle_UnrecognizedPacketId_DropsPacket)
{
    EXPECT_CALL(*client, Close(_)).Times(0);

    Handlers::Handle(client.get(), QueuedPacket{net::PacketFamily(0), net::PacketAction(128), ""});
    Handlers::Handle(client.get(), QueuedPacket{net::PacketFamily::Paperdoll, net::PacketAction(0), ""});

    EXPECT_EQ(0, typed_calls);
}

TEST_F(PacketDispatchTests, Handle_UnrecognizedPacketIdWithEnforcePacketFormat_ClosesClient)
{
    server->world->config["EnforcePacketFormat"] = true;
    EXPECT_CALL(*client, Close(false)).Times(1);

    Handlers::Handle(client.get(), QueuedPacket{net::PacketFamily(0), net::PacketAction(128), ""});

    EXPECT_EQ(0, typed_calls);
}

TEST_F(PacketDispatchTests, Handle_PlayingHandlerNotFromQueue_IsQueuedWithDelay)
{
    EXPECT_CALL(*client, Close(_)).Times(0);
    client->state = EOClient::Playing;
    Handlers::packet_handler_register_helper<PACKET_PAPERDOLL>().Register(Handle_PaperdollRequest, Handlers::Playing, 0.25);

    net::client::PaperdollRequestClientPacket packet;
    packet.player_id = 7;
    Handlers::Handle(client.get(), QueuedPacket{packet.FAMILY, packet.ACTION, Payload(packet)});
    Handlers::Handle(client.get(), QueuedPacket{packet.FAMILY, packet.ACTION, Payload(packet)});

    EXPECT_EQ(0, typed_calls);

    client->queue.Pump(client.get(), 0.0);

    EXPECT_EQ(1, typed_calls);
    EXPECT_EQ(7, last_player_id);

    // The second packet waits for the first one's delay
    client->queue.Pump(client.get(), 0.2);
    EXPECT_EQ(1, typed_calls);

    client->queue.Pump(client.get(), 0.25);
    EXPECT_EQ(2, typed_calls);
}

TEST_F(PacketDispatchTests, Send_SerializationFails_DropsPacketWithoutClosing)
{
    EXPECT_CALL(*client, Close(_)).Times(0);

    client->SetSendBuffer(1024);

    net::server::InitInitServerPacket packet;
    packet.reply_code = net::server::InitReply::Ok;

    client->EOClient::Send(packet);

    EXPECT_EQ("", client->PendingSendData());
}
