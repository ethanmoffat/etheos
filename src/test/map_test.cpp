#include <gtest/gtest.h>

#include "map.hpp"

#include <eolib/data/eo_numeric_limits.hpp>
#include <eolib/data/number_encoder.hpp>
#include <eolib/protocol/map/enums.hpp>

#include <cstdint>
#include <vector>

namespace
{

constexpr std::size_t RidOffset = 0x03;
constexpr std::size_t TypeOffset = 0x1F;

std::vector<std::uint8_t> MakeMapContent(int rid, std::size_t length)
{
    std::vector<std::uint8_t> content(length, 0x01);
    const auto encoded_rid = eolib::data::NumberEncoder::EncodeNumber(rid);
    content[RidOffset] = encoded_rid[0];
    content[RidOffset + 1] = encoded_rid[1];
    return content;
}

}

GTEST_TEST(MapGlobalPKTests, GlobalPKRid_ReturnsNextValue)
{
    EXPECT_EQ(1, Map::GlobalPKRid(0));
    EXPECT_EQ(12346, Map::GlobalPKRid(12345));
}

GTEST_TEST(MapGlobalPKTests, GlobalPKRid_MaxShort_WrapsToZero)
{
    const int max_short = static_cast<int>(eolib::data::EoNumericLimits::ShortMax) - 1;

    EXPECT_EQ(0, Map::GlobalPKRid(max_short));
}

GTEST_TEST(MapGlobalPKTests, PatchGlobalPK_MapContent_PatchesRidAndType)
{
    const int rid = 12345;
    auto content = MakeMapContent(rid, TypeOffset + 10);
    auto expected = content;

    Map::PatchGlobalPK(content);

    const auto encoded_rid = eolib::data::NumberEncoder::EncodeNumber(Map::GlobalPKRid(rid));
    expected[RidOffset] = encoded_rid[0];
    expected[RidOffset + 1] = encoded_rid[1];
    expected[TypeOffset] = eolib::data::NumberEncoder::EncodeNumber(static_cast<int>(eolib::protocol::map::MapType::Pk))[0];

    EXPECT_EQ(expected, content);
    EXPECT_EQ(Map::GlobalPKRid(rid), eolib::data::NumberEncoder::DecodeNumber(&content[RidOffset], 2));
}

GTEST_TEST(MapGlobalPKTests, PatchGlobalPK_ContentTooShort_IsUnchanged)
{
    auto content = MakeMapContent(12345, TypeOffset);
    const auto expected = content;

    Map::PatchGlobalPK(content);

    EXPECT_EQ(expected, content);
}
