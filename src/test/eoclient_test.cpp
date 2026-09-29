#include "testhelper/mocks.hpp"
#include "testhelper/setup.hpp"

#include "console.hpp"
#include "eoclient.hpp"
#include "packet.hpp"
#include "world.hpp"

#include <eolib/data/number_encoder.hpp>

#include <memory>
#include <string>
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

std::string MakePayload(std::size_t length, std::size_t seed)
{
    static const unsigned char pattern[] = {0x00, 0x80, 0xFE, 0x01, 0x7F, 0xFF, 0x06, 0x0C, 0x30, 0x81};

    std::string payload;
    for (std::size_t i = 0; i < length; ++i)
        payload += char(pattern[(i + seed) % sizeof(pattern)] ^ (i / sizeof(pattern)));

    return payload;
}

// Encodes a sequence number the way the client sends it: 2 bytes from CharMax (253) upwards, otherwise 1
std::string EncodeSequence(int sequence)
{
    auto bytes = eolib::data::NumberEncoder::EncodeNumber(sequence);
    return sequence >= 253 ? std::string{char(bytes[0]), char(bytes[1])} : std::string{char(bytes[0])};
}

std::string ClientPacket(PacketFamily family, PacketAction action, int sequence, const std::string& payload = "")
{
    return std::string{char(action), char(family)} + EncodeSequence(sequence) + payload;
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
                std::string body = std::string{char(id.second), char(id.first)} + MakePayload(length, multiple);
                std::string data = body;
                EOClient::EncryptPacket(data, multiple);
                EOClient::DecryptPacket(data, multiple);

                ASSERT_EQ(body, data)
                    << "multiple=" << multiple << " family=" << id.first << " action=" << id.second << " length=" << length;
            }
        }
    }
}

GTEST_TEST(EOClientEncryptionTests, EncryptPacket_InitInit_IsNotEncrypted)
{
    std::string body = std::string{char(PACKET_A_INIT), char(PACKET_F_INIT)} + MakePayload(20, 0);

    std::string encrypted = body;
    EOClient::EncryptPacket(encrypted, 6);
    EXPECT_EQ(body, encrypted);

    std::string decrypted = body;
    EOClient::DecryptPacket(decrypted, 7);
    EXPECT_EQ(body, decrypted);
}

GTEST_TEST(EOClientEncryptionTests, EncryptPacket_ZeroMultiple_IsNotEncrypted)
{
    std::string body = std::string{char(PACKET_PLAYER), char(PACKET_WALK)} + MakePayload(10, 0);

    std::string encrypted = body;
    EOClient::EncryptPacket(encrypted, 0);
    EXPECT_EQ(body, encrypted);

    std::string decrypted = body;
    EOClient::DecryptPacket(decrypted, 0);
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
        client->Execute(ClientPacket(PACKET_WALK, PACKET_PLAYER, start.Value() + (i % 10)));

    EXPECT_EQ(25u, client->queue.Size());
}

TEST_F(EOClientExecuteTests, Execute_InitPacket_AdvancesSequenceWithoutReadingIt)
{
    EXPECT_CALL(*client, Close(_)).Times(0);

    auto start = client->InitNewSequence();

    client->Execute(std::string{char(PACKET_A_INIT), char(PACKET_F_INIT)});
    client->Execute(ClientPacket(PACKET_WALK, PACKET_PLAYER, start.Value() + 1));

    EXPECT_EQ(2u, client->queue.Size());
}

TEST_F(EOClientExecuteTests, Execute_WrongSequence_ClosesClient)
{
    EXPECT_CALL(*client, Close(false)).Times(1);

    auto start = client->InitNewSequence();

    client->Execute(ClientPacket(PACKET_WALK, PACKET_PLAYER, start.Value() + 1));

    EXPECT_EQ(0u, client->queue.Size());
}

TEST_F(EOClientExecuteTests, Execute_WrongSequenceNotEnforced_IsAccepted)
{
    server->world->config["EnforceSequence"] = false;
    EXPECT_CALL(*client, Close(_)).Times(0);

    auto start = client->InitNewSequence();

    client->Execute(ClientPacket(PACKET_WALK, PACKET_PLAYER, start.Value() + 1));

    EXPECT_EQ(1u, client->queue.Size());
}

TEST_F(EOClientExecuteTests, Execute_PingReply_SwitchesToPingStart)
{
    EXPECT_CALL(*client, Close(_)).Times(0);

    auto init_start = client->InitNewSequence();
    client->Execute(ClientPacket(PACKET_WALK, PACKET_PLAYER, init_start.Value()));

    auto ping_start = client->PingNewSequence();

    // The start only changes when the client replies to the ping
    client->Execute(ClientPacket(PACKET_WALK, PACKET_PLAYER, init_start.Value() + 1));
    client->Execute(ClientPacket(PACKET_CONNECTION, PACKET_PING, ping_start.Value() + 2, "k"));
    client->Execute(ClientPacket(PACKET_WALK, PACKET_PLAYER, ping_start.Value() + 3));

    EXPECT_EQ(4u, client->queue.Size());
}

TEST_F(EOClientExecuteTests, Execute_PingReplyBeforeServerPing_KeepsSequenceStart)
{
    EXPECT_CALL(*client, Close(_)).Times(0);

    auto start = client->InitNewSequence();
    client->Execute(ClientPacket(PACKET_CONNECTION, PACKET_PING, start.Value(), "k"));
    client->Execute(ClientPacket(PACKET_WALK, PACKET_PLAYER, start.Value() + 1));

    EXPECT_EQ(2u, client->queue.Size());
}

TEST_F(EOClientExecuteTests, Execute_AccountReplyStart_IsUsedForFollowingPackets)
{
    EXPECT_CALL(*client, Close(_)).Times(0);

    client->InitNewSequence();
    auto start = client->AccountReplyNewSequence();
    client->Execute(ClientPacket(PACKET_WALK, PACKET_PLAYER, start.Value()));

    EXPECT_EQ(1u, client->queue.Size());
}

TEST_F(EOClientExecuteTests, Execute_EncryptedPacket_IsDecryptedWithClientMultiple)
{
    EXPECT_CALL(*client, Close(_)).Times(0);

    client->server_encryption_multiple = 6;
    client->client_encryption_multiple = 9;
    auto start = client->InitNewSequence();

    std::string data = ClientPacket(PACKET_WALK, PACKET_PLAYER, start.Value(), MakePayload(20, 0));
    EOClient::EncryptPacket(data, 9);
    client->Execute(data);

    EXPECT_EQ(1u, client->queue.Size());
}
