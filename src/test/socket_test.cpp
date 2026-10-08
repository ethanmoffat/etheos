#include <gtest/gtest.h>

#include "socket.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

// Small enough that the bytes one send leaves behind always fit in the send buffer
constexpr std::size_t FillChunkSize = 16 * 1024;
constexpr std::size_t MaxFillSize = 64 * 1024 * 1024;

class TestClient : public Client
{
public:
    int close_count = 0;

    TestClient(const Socket& sock, Server* server) : Client(sock, server) { }

    void Close(bool force) override
    {
        ++close_count;
        Client::Close(force);
    }

    std::size_t SendBufferCapacity() const
    {
        return this->send_buffer.capacity();
    }

    std::string PendingSendData() const
    {
        return std::string(this->send_buffer.begin(), this->send_buffer.end());
    }
};

class TestServer : public Server
{
public:
    using Server::Server;

    Client* ClientFactory(const Socket& sock) override
    {
        return new TestClient(sock, this);
    }
};

void SendText(Client& client, const std::string& data)
{
    client.Send(std::vector<std::uint8_t>(data.begin(), data.end()));
}

std::size_t RecvText(Client& client, std::string& buffer, std::size_t length)
{
    std::vector<std::uint8_t> received;
    client.Recv(received, length);
    buffer.append(received.begin(), received.end());
    return received.size();
}

std::string MakeData(std::size_t length, char first)
{
    std::string data(length, char());
    for (std::size_t i = 0; i < length; ++i)
        data[i] = char(first + (i % 26));

    return data;
}

}

class ClientSocketTests : public testing::Test
{
protected:
    std::unique_ptr<TestServer> server;
    std::unique_ptr<Client> peer;
    TestClient* accepted = nullptr;

    // Each test listens on its own port, so a closed connection from an earlier test can't block the bind
    void Start(std::uint16_t port)
    {
        server = std::make_unique<TestServer>(IPAddress("127.0.0.1"), port);
        server->Listen(10);

        peer = std::make_unique<Client>(IPAddress("127.0.0.1"), port);
        ASSERT_TRUE(peer->Connected());

        for (int i = 0; i < 100 && !accepted; ++i)
        {
            accepted = static_cast<TestClient*>(server->Poll());
            if (!accepted)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        ASSERT_NE(nullptr, accepted);
    }

    void TearDown() override
    {
        peer.reset();
        server.reset();
    }

    bool WaitUntilAcceptedReadable()
    {
        for (int i = 0; i < 100; ++i)
        {
            auto* selected = server->Select(0.01);
            const bool readable = std::find(selected->begin(), selected->end(), accepted) != selected->end();
            selected->clear();

            if (readable)
                return true;
        }

        return false;
    }

    // Reads from the peer until length bytes arrived, sending the accepted client's waiting bytes meanwhile
    std::string ReadFromPeer(std::size_t length)
    {
        std::string received;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

        while (received.length() < length && std::chrono::steady_clock::now() < deadline)
        {
            accepted->FlushSend();

            if (peer->Select(0.01))
                RecvText(*peer, received, length - received.length());
        }

        return received;
    }

    // Sends until the OS stops taking data, so the rest waits in the send buffer. Returns everything sent.
    std::string FillSendBuffer()
    {
        std::string sent;
        for (char first = 'a'; sent.length() < MaxFillSize && !accepted->HasPendingSend(); ++first)
        {
            const std::string chunk = MakeData(FillChunkSize, first);
            SendText(*accepted, chunk);
            sent += chunk;
        }

        return sent;
    }
};

TEST_F(ClientSocketTests, Send_SocketAcceptsData_SendsImmediately)
{
    Start(38088);

    SendText(*accepted, "hello");

    EXPECT_FALSE(accepted->HasPendingSend());
    EXPECT_EQ("hello", ReadFromPeer(5));
    EXPECT_EQ(0, accepted->close_count);
}

TEST_F(ClientSocketTests, Send_SocketFull_KeepsLeftoverAndSendsItInOrder)
{
    Start(38089);

    std::string sent = FillSendBuffer();
    ASSERT_TRUE(accepted->HasPendingSend());

    // Waiting bytes go first, so later data is queued behind them even if the socket has room again
    SendText(*accepted, "end");
    sent += "end";
    EXPECT_EQ("end", accepted->PendingSendData().substr(accepted->PendingSendData().length() - 3));

    const std::string received = ReadFromPeer(sent.length());

    EXPECT_EQ(sent.length(), received.length());
    EXPECT_TRUE(sent == received);
    EXPECT_FALSE(accepted->HasPendingSend());
    EXPECT_EQ(0, accepted->close_count);
}

TEST_F(ClientSocketTests, Send_LeftoverExceedsLimit_ClosesWithoutQueueing)
{
    Start(38090);

    FillSendBuffer();
    ASSERT_TRUE(accepted->HasPendingSend());

    const std::string pending = accepted->PendingSendData();
    SendText(*accepted, MakeData(Client::SendBufferMaxSize - pending.length() + 1, 'a'));

    EXPECT_EQ(1, accepted->close_count);
    EXPECT_FALSE(accepted->Connected());
    EXPECT_EQ(pending, accepted->PendingSendData());
}

TEST_F(ClientSocketTests, Send_AfterForcedClose_DropsData)
{
    Start(38091);

    accepted->Close(true);
    SendText(*accepted, "hello");

    EXPECT_FALSE(accepted->HasPendingSend());
    EXPECT_FALSE(peer->Select(0.1));
}

TEST_F(ClientSocketTests, Recv_DataArrived_ReadsUpToLength)
{
    Start(38092);

    SendText(*peer, "abcdef");
    ASSERT_TRUE(WaitUntilAcceptedReadable());

    std::string buffer = "x";
    EXPECT_EQ(4u, RecvText(*accepted, buffer, 4));
    EXPECT_EQ("xabcd", buffer);

    EXPECT_EQ(2u, RecvText(*accepted, buffer, 10));
    EXPECT_EQ("xabcdef", buffer);
    EXPECT_EQ(0, accepted->close_count);
}

TEST_F(ClientSocketTests, Recv_NothingArrived_ReturnsZeroWithoutClosing)
{
    Start(38093);

    std::string buffer;
    EXPECT_EQ(0u, RecvText(*accepted, buffer, 4));
    EXPECT_EQ("", buffer);
    EXPECT_EQ(0, accepted->close_count);
    EXPECT_TRUE(accepted->Connected());
}

TEST_F(ClientSocketTests, Recv_PeerClosed_ClosesClient)
{
    Start(38094);

    peer.reset();
    ASSERT_TRUE(WaitUntilAcceptedReadable());

    std::string buffer;
    EXPECT_EQ(0u, RecvText(*accepted, buffer, 4));
    EXPECT_EQ(1, accepted->close_count);
    EXPECT_FALSE(accepted->Connected());
}

TEST_F(ClientSocketTests, Send_NothingLeftOver_AllocatesNoSendBuffer)
{
    Start(38095);

    SendText(*accepted, "hello");

    EXPECT_EQ(0u, accepted->SendBufferCapacity());
}

TEST_F(ClientSocketTests, Send_SocketFull_GrowsSendBufferInSteps)
{
    Start(38096);

    FillSendBuffer();
    const std::size_t pending = accepted->PendingSendData().length();
    const std::size_t capacity = accepted->SendBufferCapacity();

    EXPECT_EQ(0u, capacity % Client::SendBufferGrowthStep);
    EXPECT_GE(capacity, pending);
    EXPECT_LT(capacity - pending, Client::SendBufferGrowthStep);
}

TEST_F(ClientSocketTests, Send_BacklogDrains_FreesSendBuffer)
{
    Start(38097);

    const std::string sent = FillSendBuffer();
    ASSERT_GT(accepted->SendBufferCapacity(), 0u);

    ReadFromPeer(sent.length());

    EXPECT_FALSE(accepted->HasPendingSend());
    EXPECT_EQ(0u, accepted->SendBufferCapacity());
}
