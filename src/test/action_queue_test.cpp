#include "testhelper/mocks.hpp"
#include "testhelper/setup.hpp"

#include "console.hpp"
#include "eoclient.hpp"
#include "eoserver.hpp"
#include "packet.hpp"
#include "handlers/handlers.hpp"

#include <array>
#include <string>
#include <utility>
#include <vector>

static constexpr unsigned short ActionQueueTestServerPort = 38079;
static constexpr std::size_t QueueMax = 40;

// The queue is pumped once per tick. Ticks are recorded instead of times so the expectations don't depend on floating
// point rounding.
static constexpr double TickSeconds = 0.01;
static constexpr int TickCount = 250;

static constexpr double ReceiveDelaySeconds = 0.02;
static constexpr double DeathHoldSeconds = 1.5;
static constexpr double P3DelaySeconds = 0.5;

static constexpr int ToTicks(double seconds)
{
    return int((seconds / TickSeconds) + 0.5);
}

static constexpr int ReceiveDelayTicks = ToTicks(ReceiveDelaySeconds);
static constexpr int DeathHoldTicks = ToTicks(DeathHoldSeconds);
static constexpr int P3DelayTicks = ToTicks(P3DelaySeconds);

// An action with no delay sets the next pump time to the current tick, so the next action is handled on the following
// tick
static constexpr int NextTick = 1;

static int current_tick;
static std::vector<std::pair<std::string, int>> events;

static void RecordEvent(const char* name)
{
    events.emplace_back(name, current_tick);
}

static void Handle_P1(EOClient*, PacketReader&) { RecordEvent("P1"); }
static void Handle_P2(EOClient*, PacketReader&) { RecordEvent("P2"); }
static void Handle_P3(EOClient*, PacketReader&) { RecordEvent("P3"); }
static void Handle_P4(EOClient*, PacketReader&) { RecordEvent("P4"); }
static void Handle_InternalNull(EOClient*, PacketReader&) { }
static void Handle_InternalWarp(EOClient*, PacketReader&) { RecordEvent("respawn"); }

static void RegisterTestHandler(PacketFamily family, PacketAction action, Handlers::client_handler_t f, unsigned short allow_states, double delay)
{
    Handlers::packet_handler_register_instance->Register(family, action,
        Handlers::packet_handler(Handlers::packet_handler::ClientFn, reinterpret_cast<Handlers::void_fn_t>(f), allow_states, delay));
}

// Mirrors what EOClient::Execute queues for a received packet
static void Receive(EOClient& client, PacketFamily family, PacketAction action)
{
    client.queue.AddAction(PacketReader(std::string{char(action), char(family)}), ReceiveDelaySeconds, true);
}

// Mirrors what Character::DeathRespawn queues
static void Die(EOClient& client)
{
    client.queue.AddAction(PacketReader(std::array<char, 2>{{char(PACKET_INTERNAL_NULL), char(PACKET_INTERNAL)}}.data()), DeathHoldSeconds);
    client.queue.AddAction(PacketReader(std::array<char, 2>{{char(PACKET_INTERNAL_WARP), char(PACKET_INTERNAL)}}.data()), 0.0);
}

GTEST_TEST(ActionQueueTests, DeathRespawn_PacketsQueuedAround_KeepOrderAndTiming)
{
    Console::SuppressOutput(true);

    Config config, admin_config;
    CreateConfigWithTestDefaults(config, admin_config);

    auto mockDatabase = CreateMockDatabase();
    auto mockDatabaseFactory = CreateMockDatabaseFactory(mockDatabase);

    EOServer server(IPAddress("127.0.0.1"), ActionQueueTestServerPort, mockDatabaseFactory, config, admin_config);

    // P1 and P4 are re-queued with no delay, P2 runs immediately (out of band), P3 is re-queued with a delay
    RegisterTestHandler(PACKET_WALK, PACKET_PLAYER, Handle_P1, Handlers::Playing, 0.0);
    RegisterTestHandler(PACKET_FACE, PACKET_PLAYER, Handle_P2, Handlers::Playing | Handlers::OutOfBand, 0.0);
    RegisterTestHandler(PACKET_EMOTE, PACKET_REPORT, Handle_P3, Handlers::Playing, P3DelaySeconds);
    RegisterTestHandler(PACKET_SIT, PACKET_REQUEST, Handle_P4, Handlers::Playing, 0.0);
    RegisterTestHandler(PACKET_INTERNAL, PACKET_INTERNAL_NULL, Handle_InternalNull, Handlers::Any, 0.0);
    RegisterTestHandler(PACKET_INTERNAL, PACKET_INTERNAL_WARP, Handle_InternalWarp, Handlers::Playing, 0.0);

    MockClient client(&server);
    EXPECT_CALL(client, Connected()).WillRepeatedly(Return(true));
    EXPECT_CALL(client, Close(_)).Times(0);
    client.state = EOClient::Playing;

    events.clear();

    const int p4_receive_tick = 5;

    for (current_tick = 0; current_tick < TickCount; ++current_tick)
    {
        if (current_tick == 0)
        {
            Receive(client, PACKET_WALK, PACKET_PLAYER);
            Receive(client, PACKET_FACE, PACKET_PLAYER);
            Receive(client, PACKET_EMOTE, PACKET_REPORT);
            Die(client);
        }
        else if (current_tick == p4_receive_tick)
        {
            Receive(client, PACKET_SIT, PACKET_REQUEST);
        }

        server_pump_client_queue(&client, current_tick * TickSeconds, QueueMax);
    }

    // Each received packet is taken from the queue once its receive delay has passed. P1 and P3 are then queued again,
    // behind the death hold, and P2 is handled right away.
    const int p2_tick = ReceiveDelayTicks;
    const int hold_start_tick = 3 * ReceiveDelayTicks;
    const int respawn_tick = hold_start_tick + DeathHoldTicks;
    const int p1_tick = respawn_tick + NextTick;
    const int p3_tick = p1_tick + NextTick;
    // P4 (received during the hold) is taken from the queue after P3's delay, and handled after its receive delay
    const int p4_tick = p3_tick + P3DelayTicks + ReceiveDelayTicks;

    const std::vector<std::pair<std::string, int>> expected_events{
        {"P2", p2_tick},
        {"respawn", respawn_tick},
        {"P1", p1_tick},
        {"P3", p3_tick},
        {"P4", p4_tick},
    };
    EXPECT_EQ(expected_events, events);
}
