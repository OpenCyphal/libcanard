// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#include "canard_dsdl.h"
#include "exposed.hpp"
#include <array>
#include <cmath>

TEST_CASE("canardDSDLFloat16Pack")
{
    // Reference values were generated manually with libuavcan and numpy.float16().
    REQUIRE(0b0000000000000000 == canardDSDLFloat16Pack(0.0F));
    REQUIRE(0b0011110000000000 == canardDSDLFloat16Pack(1.0F));
    REQUIRE(0b1100000000000000 == canardDSDLFloat16Pack(-2.0F));
    REQUIRE(0b0111110000000000 == canardDSDLFloat16Pack(999999.0F));      // +inf
    REQUIRE(0b1111101111111111 == canardDSDLFloat16Pack(-65519.0F));      // -max
    REQUIRE(0b0111111111111111 == canardDSDLFloat16Pack(std::nanf("")));  // nan
}

TEST_CASE("canardDSDLFloat16Unpack")
{
    // Reference values were generated manually with libuavcan and numpy.float16().
    REQUIRE(Approx(0.0F) == canardDSDLFloat16Unpack(0b0000000000000000));
    REQUIRE(Approx(1.0F) == canardDSDLFloat16Unpack(0b0011110000000000));
    REQUIRE(Approx(-2.0F) == canardDSDLFloat16Unpack(0b1100000000000000));
    REQUIRE(Approx(-65504.0F) == canardDSDLFloat16Unpack(0b1111101111111111));
    REQUIRE(std::isinf(canardDSDLFloat16Unpack(0b0111110000000000)));

    REQUIRE(bool(std::isnan(canardDSDLFloat16Unpack(0b0111111111111111))));
}

TEST_CASE("canardDSDLFloat16")
{
    float x = -1000.0F;
    while (x <= 1000.0F)
    {
        REQUIRE(Approx(x) == canardDSDLFloat16Unpack(canardDSDLFloat16Pack(x)));
        x += 0.5F;
    }
}

TEST_CASE("copyBitArray")
{
    using exposed::copyBitArray;

    copyBitArray(0, 0, 0, nullptr, nullptr);

    const auto test = [&](const size_t                     length_bit,
                          const size_t                     src_offset_bit,
                          const size_t                     dst_offset_bit,
                          const std::vector<std::uint8_t>& src,
                          const std::vector<std::uint8_t>& dst,
                          const std::vector<std::uint8_t>& ref) {
        REQUIRE(length_bit <= (dst.size() * 8));
        REQUIRE(length_bit <= (src.size() * 8));
        std::vector<std::uint8_t> result = dst;
        copyBitArray(length_bit, src_offset_bit, dst_offset_bit, src.data(), result.data());
        return std::equal(std::begin(ref), std::end(ref), std::begin(result));
    };

    REQUIRE(test(0, 0, 0, {}, {}, {}));
    REQUIRE(test(8, 0, 0, {0xFF}, {0x00}, {0xFF}));
    REQUIRE(test(16, 0, 0, {0xFF, 0xFF}, {0x00, 0x00}, {0xFF, 0xFF}));
}
