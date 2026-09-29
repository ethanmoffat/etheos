#pragma once

#include <eolib/protocol/net/packet.hpp>

#include <gmock/gmock.h>

#include <type_traits>

// Matches a typed packet sent through EOClient::Send(const net::Packet&) against an expected packet of the same type
MATCHER_P(TypedPacketEq, expected, "is " + expected.ToString())
{
    using TPacket = std::decay_t<decltype(expected)>;
    const auto* packet = dynamic_cast<const TPacket*>(&arg);

    if (packet == nullptr)
    {
        *result_listener << "is " << arg.ToString();
        return false;
    }

    if (*packet != expected)
    {
        *result_listener << "is " << packet->ToString();
        return false;
    }

    return true;
}

template <typename TPacket> ::testing::Matcher<const eolib::protocol::net::Packet&> PacketEq(const TPacket& expected)
{
    return TypedPacketEq(expected);
}
