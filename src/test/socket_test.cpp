#include <gtest/gtest.h>

#include "socket.hpp"

#include <string>

namespace
{

class TestClient : public Client
{
public:
    int close_count = 0;

    void Close(bool force) override
    {
        (void)force;
        ++close_count;
    }

    std::size_t SendBufferSize() const
    {
        return this->send_buffer.length();
    }

    // Gets the queued bytes in send order
    std::string PendingSendData() const
    {
        std::string result;
        const std::size_t mask = this->send_buffer.length() - 1;

        for (std::size_t i = 1; i <= this->send_buffer_used; ++i)
            result += this->send_buffer[(this->send_buffer_gpos + i) & mask];

        return result;
    }

    // Drops bytes from the front of the queue, as DoSend does after writing them to the socket
    void ConsumeSendData(std::size_t count)
    {
        this->send_buffer_gpos = (this->send_buffer_gpos + count) & (this->send_buffer.length() - 1);
        this->send_buffer_used -= count;
    }
};

std::string MakeData(std::size_t length, char first)
{
    std::string data;
    for (std::size_t i = 0; i < length; ++i)
        data += char(first + (i % 26));

    return data;
}

}

GTEST_TEST(ClientSendBufferTests, Send_DataFits_DoesNotGrow)
{
    TestClient client;
    client.SetSendBuffer(16);
    client.SetSendBufferLimit(64);

    const std::string data = MakeData(16, 'a');
    client.Send(data);

    EXPECT_EQ(16u, client.SendBufferSize());
    EXPECT_EQ(data, client.PendingSendData());
    EXPECT_EQ(0, client.close_count);
}

GTEST_TEST(ClientSendBufferTests, Send_DataDoesNotFit_GrowsToNextPowerOfTwo)
{
    TestClient client;
    client.SetSendBuffer(16);
    client.SetSendBufferLimit(128);

    const std::string first = MakeData(10, 'a');
    const std::string second = MakeData(30, 'A');
    client.Send(first);
    client.Send(second);

    EXPECT_EQ(64u, client.SendBufferSize());
    EXPECT_EQ(first + second, client.PendingSendData());
    EXPECT_EQ(0, client.close_count);
}

GTEST_TEST(ClientSendBufferTests, Send_QueuedDataWrapsAround_KeepsOrderWhenGrowing)
{
    TestClient client;
    client.SetSendBuffer(16);
    client.SetSendBufferLimit(64);

    const std::string first = MakeData(12, 'a');
    const std::string second = MakeData(8, 'A');
    const std::string third = MakeData(12, '0');

    client.Send(first);
    client.ConsumeSendData(10);
    client.Send(second);
    client.Send(third);

    EXPECT_EQ(32u, client.SendBufferSize());
    EXPECT_EQ(first.substr(10) + second + third, client.PendingSendData());

    client.ConsumeSendData(2 + second.length());
    client.Send(first);

    EXPECT_EQ(third + first, client.PendingSendData());
    EXPECT_EQ(0, client.close_count);
}

GTEST_TEST(ClientSendBufferTests, Send_DataFitsAtLimit_GrowsToLimit)
{
    TestClient client;
    client.SetSendBuffer(16);
    client.SetSendBufferLimit(64);

    const std::string data = MakeData(64, 'a');
    client.Send(data);

    EXPECT_EQ(64u, client.SendBufferSize());
    EXPECT_EQ(data, client.PendingSendData());
    EXPECT_EQ(0, client.close_count);
}

GTEST_TEST(ClientSendBufferTests, Send_DataExceedsLimit_ClosesWithoutQueueing)
{
    TestClient client;
    client.SetSendBuffer(16);
    client.SetSendBufferLimit(64);

    const std::string first = MakeData(10, 'a');
    client.Send(first);
    client.Send(MakeData(55, 'A'));

    EXPECT_EQ(1, client.close_count);
    EXPECT_EQ(16u, client.SendBufferSize());
    EXPECT_EQ(first, client.PendingSendData());
}

GTEST_TEST(ClientSendBufferTests, SetSendBufferLimit_BelowBufferSize_KeepsBufferSize)
{
    TestClient client;
    client.SetSendBuffer(32);
    client.SetSendBufferLimit(8);

    const std::string data = MakeData(32, 'a');
    client.Send(data);
    client.Send("x");

    EXPECT_EQ(1, client.close_count);
    EXPECT_EQ(data, client.PendingSendData());
}
