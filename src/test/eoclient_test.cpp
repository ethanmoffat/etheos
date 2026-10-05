#include "testhelper/mocks.hpp"
#include "testhelper/setup.hpp"

#include "console.hpp"
#include "eoclient.hpp"
#include "packet.hpp"
#include "world.hpp"

#include <eolib/data/number_encoder.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

static constexpr unsigned short EOClientTestServerPort = 38080;

namespace
{

const std::vector<std::pair<PacketFamily, PacketAction>> PacketIds{
    {PACKET_WALK, PACKET_PLAYER},
    {PACKET_TALK, PACKET_REPORT},
    {PACKET_CONNECTION, PACKET_PING},
    {PACKET_ACCOUNT, PACKET_REPLY},
    {PACKET_NPC, PACKET_NET3},
};

const std::vector<std::size_t> PayloadLengths{0, 1, 2, 3, 7, 8, 63, 64, 255, 1000};

using Bytes = std::vector<std::uint8_t>;

Bytes MakePayload(std::size_t length, std::size_t seed)
{
    static const unsigned char pattern[] = {0x00, 0x80, 0xFE, 0x01, 0x7F, 0xFF, 0x06, 0x0C, 0x30, 0x81};

    Bytes payload;
    for (std::size_t i = 0; i < length; ++i)
        payload.push_back(std::uint8_t(pattern[(i + seed) % sizeof(pattern)] ^ (i / sizeof(pattern))));

    return payload;
}

// A packet body: [action][family][data]
Bytes PacketBody(PacketFamily family, PacketAction action, const Bytes& data = {})
{
    Bytes body{std::uint8_t(action), std::uint8_t(family)};
    body.insert(body.end(), data.begin(), data.end());
    return body;
}

// Encodes a sequence number the way the client sends it: 2 bytes from CharMax (253) upwards, otherwise 1
Bytes EncodeSequence(int sequence)
{
    auto bytes = eolib::data::NumberEncoder::EncodeNumber(sequence);
    return sequence >= 253 ? Bytes{bytes[0], bytes[1]} : Bytes{bytes[0]};
}

Bytes ClientPacket(PacketFamily family, PacketAction action, int sequence, const Bytes& payload = {})
{
    Bytes data = EncodeSequence(sequence);
    data.insert(data.end(), payload.begin(), payload.end());
    return PacketBody(family, action, data);
}

void Execute(EOClient* client, Bytes packet)
{
    client->Execute(packet.data(), packet.size());
}

void Encrypt(Bytes& body, int multiple)
{
    EOClient::EncryptPacket(body.data(), body.size(), multiple);
}

void Decrypt(Bytes& data, int multiple)
{
    EOClient::DecryptPacket(data.data(), data.size(), multiple);
}

}

GTEST_TEST(EOClientEncryptionTests, DecryptPacket_EncryptedPacket_RoundTrips)
{
    for (int multiple = 6; multiple <= 12; ++multiple)
    {
        for (auto id : PacketIds)
        {
            for (std::size_t length : PayloadLengths)
            {
                Bytes body = PacketBody(id.first, id.second, MakePayload(length, multiple));
                Bytes data = body;
                Encrypt(data, multiple);
                Decrypt(data, multiple);

                ASSERT_EQ(body, data)
                    << "multiple=" << multiple << " family=" << id.first << " action=" << id.second << " length=" << length;
            }
        }
    }
}

GTEST_TEST(EOClientEncryptionTests, EncryptPacket_InitInit_IsNotEncrypted)
{
    Bytes body = PacketBody(PACKET_F_INIT, PACKET_A_INIT, MakePayload(20, 0));

    Bytes encrypted = body;
    Encrypt(encrypted, 6);
    EXPECT_EQ(body, encrypted);

    Bytes decrypted = body;
    Decrypt(decrypted, 7);
    EXPECT_EQ(body, decrypted);
}

GTEST_TEST(EOClientEncryptionTests, EncryptPacket_ZeroMultiple_IsNotEncrypted)
{
    Bytes body = PacketBody(PACKET_WALK, PACKET_PLAYER, MakePayload(10, 0));

    Bytes encrypted = body;
    Encrypt(encrypted, 0);
    EXPECT_EQ(body, encrypted);

    Bytes decrypted = body;
    Decrypt(decrypted, 0);
    EXPECT_EQ(body, decrypted);
}

class EOClientExecuteTests : public ::testing::Test
{
protected:
    Config config, admin_config;
    std::shared_ptr<EOServer> server;
    std::unique_ptr<MockClient> client;

    void SetUp() override
    {
        Console::SuppressOutput(true);

        CreateConfigWithTestDefaults(config, admin_config);

        auto mockDatabase = CreateMockDatabase();
        auto mockDatabaseFactory = CreateMockDatabaseFactory(mockDatabase);

        server = std::make_shared<EOServer>(IPAddress("127.0.0.1"), EOClientTestServerPort, mockDatabaseFactory, config, admin_config);
        client = std::make_unique<MockClient>(server.get());
        EXPECT_CALL(*client, Connected()).WillRepeatedly(Return(true));
    }

    void TearDown() override
    {
        client.reset();
        server.reset();
    }
};

TEST_F(EOClientExecuteTests, Execute_SequenceFromInitStart_IsAccepted)
{
    EXPECT_CALL(*client, Close(_)).Times(0);

    auto start = client->InitNewSequence();

    for (int i = 0; i < 25; ++i)
        Execute(client.get(), ClientPacket(PACKET_WALK, PACKET_PLAYER, start.Value() + (i % 10)));

    EXPECT_EQ(25u, client->queue.Size());
}

TEST_F(EOClientExecuteTests, Execute_InitPacket_AdvancesSequenceWithoutReadingIt)
{
    EXPECT_CALL(*client, Close(_)).Times(0);

    auto start = client->InitNewSequence();

    Execute(client.get(), PacketBody(PACKET_F_INIT, PACKET_A_INIT));
    Execute(client.get(), ClientPacket(PACKET_WALK, PACKET_PLAYER, start.Value() + 1));

    EXPECT_EQ(2u, client->queue.Size());
}

TEST_F(EOClientExecuteTests, Execute_WrongSequence_ClosesClient)
{
    EXPECT_CALL(*client, Close(false)).Times(1);

    auto start = client->InitNewSequence();

    Execute(client.get(), ClientPacket(PACKET_WALK, PACKET_PLAYER, start.Value() + 1));

    EXPECT_EQ(0u, client->queue.Size());
}

TEST_F(EOClientExecuteTests, Execute_WrongSequenceNotEnforced_IsAccepted)
{
    server->world->config["EnforceSequence"] = false;
    EXPECT_CALL(*client, Close(_)).Times(0);

    auto start = client->InitNewSequence();

    Execute(client.get(), ClientPacket(PACKET_WALK, PACKET_PLAYER, start.Value() + 1));

    EXPECT_EQ(1u, client->queue.Size());
}

TEST_F(EOClientExecuteTests, Execute_PingReply_SwitchesToPingStart)
{
    EXPECT_CALL(*client, Close(_)).Times(0);

    auto init_start = client->InitNewSequence();
    Execute(client.get(), ClientPacket(PACKET_WALK, PACKET_PLAYER, init_start.Value()));

    auto ping_start = client->PingNewSequence();

    // The start only changes when the client replies to the ping
    Execute(client.get(), ClientPacket(PACKET_WALK, PACKET_PLAYER, init_start.Value() + 1));
    Execute(client.get(), ClientPacket(PACKET_CONNECTION, PACKET_PING, ping_start.Value() + 2, Bytes{'k'}));
    Execute(client.get(), ClientPacket(PACKET_WALK, PACKET_PLAYER, ping_start.Value() + 3));

    EXPECT_EQ(4u, client->queue.Size());
}

TEST_F(EOClientExecuteTests, Execute_PingReplyBeforeServerPing_KeepsSequenceStart)
{
    EXPECT_CALL(*client, Close(_)).Times(0);

    auto start = client->InitNewSequence();
    Execute(client.get(), ClientPacket(PACKET_CONNECTION, PACKET_PING, start.Value(), Bytes{'k'}));
    Execute(client.get(), ClientPacket(PACKET_WALK, PACKET_PLAYER, start.Value() + 1));

    EXPECT_EQ(2u, client->queue.Size());
}

TEST_F(EOClientExecuteTests, Execute_AccountReplyStart_IsUsedForFollowingPackets)
{
    EXPECT_CALL(*client, Close(_)).Times(0);

    client->InitNewSequence();
    auto start = client->AccountReplyNewSequence();
    Execute(client.get(), ClientPacket(PACKET_WALK, PACKET_PLAYER, start.Value()));

    EXPECT_EQ(1u, client->queue.Size());
}

TEST_F(EOClientExecuteTests, Execute_EncryptedPacket_IsDecryptedWithClientMultiple)
{
    EXPECT_CALL(*client, Close(_)).Times(0);

    client->server_encryption_multiple = 6;
    client->client_encryption_multiple = 9;
    auto start = client->InitNewSequence();

    Bytes data = ClientPacket(PACKET_WALK, PACKET_PLAYER, start.Value(), MakePayload(20, 0));
    Encrypt(data, 9);
    Execute(client.get(), data);

    EXPECT_EQ(1u, client->queue.Size());
}

class EOClientRecvTests : public ::testing::Test
{
protected:
    Config config, admin_config;
    std::shared_ptr<EOServer> server;
    std::unique_ptr<Client> peer;
    EOClient* client = nullptr;

    // Each test listens on its own port, so a closed connection from an earlier test can't block the bind
    void Start(std::uint16_t port)
    {
        Console::SuppressOutput(true);
        CreateConfigWithTestDefaults(config, admin_config);

        auto mockDatabase = CreateMockDatabase();
        server = std::make_shared<EOServer>(IPAddress("127.0.0.1"), port, CreateMockDatabaseFactory(mockDatabase), config, admin_config);
        server->world->config["EnforceSequence"] = false;
        server->Listen(10);

        peer = std::make_unique<Client>(IPAddress("127.0.0.1"), port);
        ASSERT_TRUE(peer->Connected());

        for (int i = 0; i < 100 && !client; ++i)
        {
            client = static_cast<EOClient*>(server->Poll());
            if (!client)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        ASSERT_NE(nullptr, client);
    }

    void TearDown() override
    {
        peer.reset();
        server.reset();
    }

    void SendFromPeer(const Bytes& data)
    {
        peer->Send(data);
    }

    void SendPacket(std::size_t payload_length)
    {
        const Bytes body = ClientPacket(PACKET_WALK, PACKET_PLAYER, 0, Bytes(payload_length, 'x'));
        const auto length = eolib::data::NumberEncoder::EncodeNumber(int(body.size()));

        Bytes framed{length[0], length[1]};
        framed.insert(framed.end(), body.begin(), body.end());
        SendFromPeer(framed);

        const std::size_t queued = client->queue.Size();
        for (int i = 0; i < 100 && client->queue.Size() == queued; ++i)
        {
            client->Tick();
            if (client->queue.Size() == queued)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        ASSERT_EQ(queued + 1, client->queue.Size());
    }
};

TEST_F(EOClientRecvTests, Tick_SmallPacket_KeepsInitialCapacity)
{
    Start(38098);

    SendPacket(10);

    EXPECT_EQ(EOClient::RecvBufferInitialSize, client->data.capacity());
}

TEST_F(EOClientRecvTests, Tick_LargePacket_GrowsToPacketLength)
{
    Start(38099);

    SendPacket(1997);

    // action, family and sequence bytes plus the payload
    EXPECT_EQ(2000u, client->data.capacity());
}

TEST_F(EOClientRecvTests, Tick_SmallPacketsAfterLargeOne_KeepsCapacity)
{
    Start(38100);

    SendPacket(1997);
    SendPacket(10);
    SendPacket(10);

    EXPECT_EQ(2000u, client->data.capacity());
}

TEST_F(EOClientRecvTests, Tick_PacketSplitAcrossTicks_QueuesOnceComplete)
{
    Start(38101);

    const Bytes body = ClientPacket(PACKET_WALK, PACKET_PLAYER, 0, Bytes(100, 'x'));
    const auto length = eolib::data::NumberEncoder::EncodeNumber(int(body.size()));

    auto send_part = [&](const Bytes& part, std::size_t expected_length_bytes, std::size_t expected_data)
    {
        SendFromPeer(part);
        for (int i = 0; i < 1000 && (client->length_bytes.size() != expected_length_bytes || client->data.size() != expected_data); ++i)
        {
            client->Tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        EXPECT_EQ(expected_length_bytes, client->length_bytes.size());
        EXPECT_EQ(expected_data, client->data.size());
        EXPECT_EQ(0u, client->queue.Size());
    };

    Bytes second_part{length[1]};
    second_part.insert(second_part.end(), body.begin(), body.begin() + 40);

    send_part(Bytes{length[0]}, 1, 0);
    send_part(second_part, 2, 40);
    send_part(Bytes(body.begin() + 40, body.begin() + 90), 2, 90);

    SendFromPeer(Bytes(body.begin() + 90, body.end()));
    for (int i = 0; i < 1000 && client->queue.Size() == 0; ++i)
    {
        client->Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(1u, client->queue.Size());
    EXPECT_TRUE(client->length_bytes.empty());
    EXPECT_TRUE(client->data.empty());
}
