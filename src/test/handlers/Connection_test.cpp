#include "../testhelper/mocks.hpp"
#include "../testhelper/setup.hpp"

// include the CPP file with the Connection functions in it for testing
#include "../../handlers/Connection.cpp"

#include "console.hpp"

#include <memory>

namespace net = eolib::protocol::net;

namespace
{

constexpr unsigned short ConnectionTestServerPort = 38083;

class ConnectionTests : public testing::Test
{
protected:
    std::unique_ptr<EOServer> server;

    void SetUp() override
    {
        Console::SuppressOutput(true);

        Config config, admin_config;
        CreateConfigWithTestDefaults(config, admin_config);
        server = std::make_unique<EOServer>(IPAddress("127.0.0.1"), ConnectionTestServerPort, CreateMockDatabaseFactory(CreateMockDatabase()), config, admin_config);
    }

    static net::client::ConnectionAcceptClientPacket AcceptRequest(const EOClient& client)
    {
        net::client::ConnectionAcceptClientPacket packet;
        packet.server_encryption_multiple = client.server_encryption_multiple;
        packet.client_encryption_multiple = client.client_encryption_multiple;
        packet.player_id = client.id;
        return packet;
    }
};

}

TEST_F(ConnectionTests, Accept_MatchingInitData_MarksClientAccepted)
{
    MockClient client(server.get());
    client.server_encryption_multiple = 7;
    client.client_encryption_multiple = 9;

    EXPECT_CALL(client, Close(_)).Times(0);

    Handlers::Connection_Accept(&client, AcceptRequest(client));

    EXPECT_TRUE(client.Accepted());
}

TEST_F(ConnectionTests, Accept_WrongEncryptionMultiple_ClosesClient)
{
    MockClient client(server.get());
    client.server_encryption_multiple = 7;
    client.client_encryption_multiple = 9;

    EXPECT_CALL(client, Close(false)).Times(1);

    auto request = AcceptRequest(client);
    request.client_encryption_multiple = 10;
    Handlers::Connection_Accept(&client, request);

    EXPECT_FALSE(client.Accepted());
}

TEST_F(ConnectionTests, Accept_WrongPlayerId_ClosesClient)
{
    MockClient client(server.get());

    EXPECT_CALL(client, Close(false)).Times(1);

    auto request = AcceptRequest(client);
    request.player_id = static_cast<int>(client.id) + 1;
    Handlers::Connection_Accept(&client, request);

    EXPECT_FALSE(client.Accepted());
}

TEST_F(ConnectionTests, Ping_PongPending_ClearsPong)
{
    MockClient client(server.get());
    client.needpong = true;

    Handlers::Connection_Ping(&client, net::client::ConnectionPingClientPacket());

    EXPECT_FALSE(client.needpong);
}
