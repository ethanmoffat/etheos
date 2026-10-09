#pragma once

#include <eolib/data/eo_writer.hpp>
#include <eolib/protocol/net/packet.hpp>

#include <gmock/gmock.h>

#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

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

template <typename TPacket> std::vector<std::uint8_t> Payload(const TPacket& packet)
{
    eolib::data::EoWriter writer;
    packet.Serialize(writer);
    return std::move(writer).ToByteArray();
}
