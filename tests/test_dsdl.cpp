// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#include "canard_dsdl.h"
#include "exposed.hpp"
#include <array>
#include <cmath>
#include <iostream>

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

    {
        uint8_t a = 0;
        uint8_t b = 0;
        copyBitArray(0, 0, 0, &a, &b);
    }

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

    REQUIRE(test(8, 0, 0, {0xFF}, {0x00}, {0xFF}));
    REQUIRE(test(16, 0, 0, {0xFF, 0xFF}, {0x00, 0x00}, {0xFF, 0xFF}));
    REQUIRE(test(12, 0, 0, {0xFF, 0x0A}, {0x55, 0x00}, {0xFF, 0x0A}));
    REQUIRE(test(12, 0, 0, {0xFF, 0x0A}, {0x00, 0xF0}, {0xFF, 0xFA}));
    REQUIRE(test(12, 0, 4, {0xFF, 0x0A}, {0x53, 0x55}, {0xF3, 0xAF}));
    REQUIRE(test(8, 4, 4, {0x55, 0x55}, {0xAA, 0xAA}, {0x5A, 0xA5}));
}

TEST_CASE("canardDSDLPrimitiveSerialize_aligned")
{
    // The reference values for the following test have been taken from the PyUAVCAN test suite.
    const std::vector<std::uint8_t> Reference({0xA7, 0xEF, 0xCD, 0xAB, 0x90, 0x78, 0x56, 0x34, 0x12, 0x88, 0xA9, 0xCB,
                                               0xED, 0xFE, 0xFF, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0,
                                               0x3F, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x7C, 0xDA, 0x0E, 0xDA, 0xBE, 0xFE,
                                               0x01, 0xAD, 0xDE, 0xEF, 0xBE, 0xC5, 0x67, 0xC5, 0x0B});

    std::vector<std::uint8_t> dest(std::size(Reference));

    std::uint8_t u8 = 0b1010'0111;
    REQUIRE(8 == canardDSDLPrimitiveSerialize(dest.data(), 0, 8, &u8));

    std::int64_t i64 = 0x1234'5678'90ab'cdef;
    REQUIRE(72 == canardDSDLPrimitiveSerialize(dest.data(), 8, 64, &i64));

    std::int32_t i32 = -0x1234'5678;
    REQUIRE(104 == canardDSDLPrimitiveSerialize(dest.data(), 72, 32, &i32));

    std::int32_t i16 = -2;
    u8               = 0;
    std::int8_t i8   = 127;
    REQUIRE(120 == canardDSDLPrimitiveSerialize(dest.data(), 104, 16, &i16));
    REQUIRE(128 == canardDSDLPrimitiveSerialize(dest.data(), 120, 8, &u8));
    REQUIRE(136 == canardDSDLPrimitiveSerialize(dest.data(), 128, 8, &i8));

    double        f64 = 1.0;
    float         f32 = 1.0F;
    std::uint16_t f16 = canardDSDLFloat16Pack(99999.9F);
    REQUIRE(200 == canardDSDLPrimitiveSerialize(dest.data(), 136, 64, &f64));
    REQUIRE(232 == canardDSDLPrimitiveSerialize(dest.data(), 200, 32, &f32));
    REQUIRE(248 == canardDSDLPrimitiveSerialize(dest.data(), 232, 16, &f16));

    std::uint16_t u16 = 0xBEDA;
    u8                = 0;
    REQUIRE(260 == canardDSDLPrimitiveSerialize(dest.data(), 248, 12, &u16));
    REQUIRE(264 == canardDSDLPrimitiveSerialize(dest.data(), 260, 4, &u8));
    REQUIRE(280 == canardDSDLPrimitiveSerialize(dest.data(), 264, 16, &u16));

    i16 = -2;
    u8  = 0;
    REQUIRE(289 == canardDSDLPrimitiveSerialize(dest.data(), 280, 9, &i16));
    REQUIRE(296 == canardDSDLPrimitiveSerialize(dest.data(), 289, 7, &u8));

    u16 = 0xDEAD;
    REQUIRE(312 == canardDSDLPrimitiveSerialize(dest.data(), 296, 16, &u16));
    u16 = 0xBEEF;
    REQUIRE(328 == canardDSDLPrimitiveSerialize(dest.data(), 312, 16, &u16));

    std::size_t offset   = 328;
    const auto  push_bit = [&](const bool value) {
        REQUIRE((offset + 1U) == canardDSDLPrimitiveSerialize(dest.data(), offset, 1, &value));
        ++offset;
    };

    push_bit(true);
    push_bit(false);
    push_bit(true);
    push_bit(false);
    push_bit(false);
    push_bit(false);
    push_bit(true);
    push_bit(true);
    push_bit(true);
    push_bit(true);
    push_bit(true);
    push_bit(false);
    push_bit(false);
    push_bit(true);
    push_bit(true);
    push_bit(false);

    push_bit(true);
    push_bit(false);
    push_bit(true);
    push_bit(false);
    push_bit(false);
    push_bit(false);
    push_bit(true);
    push_bit(true);
    push_bit(true);
    push_bit(true);
    push_bit(false);
    push_bit(true);
    push_bit(false);

    u8 = 0;
    REQUIRE(360 == canardDSDLPrimitiveSerialize(dest.data(), 357, 3, &u8));

    REQUIRE(std::size(dest) == std::size(Reference));
    REQUIRE_THAT(dest, Catch::Matchers::Equals(Reference));
}

TEST_CASE("canardDSDLPrimitiveSerialize_unaligned")
{
    // The reference values for the following test have been taken from the PyUAVCAN test suite.
    const std::vector<std::uint8_t> Reference({
        0xC5, 0x2F, 0x57, 0x82, 0xC6, 0xCA, 0x12, 0x34, 0x56, 0xD9, 0xBF, 0xEC, 0x06, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x80, 0xFF, 0x01, 0x00, 0x00, 0xFC, 0x01, 0xE0, 0x6F, 0xF5, 0x7E, 0xF7, 0x05  //
    });

    std::vector<std::uint8_t> dest(std::size(Reference));

    std::size_t offset   = 0;
    const auto  push_bit = [&](const bool value) {
        REQUIRE((offset + 1U) == canardDSDLPrimitiveSerialize(dest.data(), offset, 1, &value));
        ++offset;
    };

    push_bit(true);
    push_bit(false);
    push_bit(true);
    push_bit(false);
    push_bit(false);
    push_bit(false);
    push_bit(true);
    push_bit(true);
    push_bit(true);
    push_bit(true);
    push_bit(true);

    push_bit(true);
    push_bit(false);
    push_bit(true);
    push_bit(false);
    push_bit(false);
    push_bit(true);
    push_bit(true);
    push_bit(true);
    push_bit(false);
    push_bit(true);

    REQUIRE_THAT(std::vector<std::uint8_t>(std::begin(dest), std::begin(dest) + 2),
                 Catch::Matchers::Equals(std::vector<std::uint8_t>(std::begin(Reference), std::begin(Reference) + 2)));

    std::uint8_t u8 = 0x12;
    REQUIRE(29 == canardDSDLPrimitiveSerialize(dest.data(), 21, 8, &u8));
    u8 = 0x34;
    REQUIRE(37 == canardDSDLPrimitiveSerialize(dest.data(), 29, 8, &u8));
    u8 = 0x56;
    REQUIRE(45 == canardDSDLPrimitiveSerialize(dest.data(), 37, 8, &u8));

    REQUIRE_THAT(std::vector<std::uint8_t>(std::begin(dest), std::begin(dest) + 5),
                 Catch::Matchers::Equals(std::vector<std::uint8_t>(std::begin(Reference), std::begin(Reference) + 5)));

    offset = 45;
    push_bit(false);
    push_bit(true);
    push_bit(true);

    u8 = 0x12;
    REQUIRE(56 == canardDSDLPrimitiveSerialize(dest.data(), 48, 8, &u8));
    u8 = 0x34;
    REQUIRE(64 == canardDSDLPrimitiveSerialize(dest.data(), 56, 8, &u8));
    u8 = 0x56;
    REQUIRE(72 == canardDSDLPrimitiveSerialize(dest.data(), 64, 8, &u8));

    offset = 72;
    push_bit(true);
    push_bit(false);
    push_bit(false);
    push_bit(true);
    push_bit(true);

    REQUIRE_THAT(std::vector<std::uint8_t>(std::begin(dest), std::begin(dest) + 9),
                 Catch::Matchers::Equals(std::vector<std::uint8_t>(std::begin(Reference), std::begin(Reference) + 9)));

    std::int8_t i8 = -2;
    REQUIRE(85 == canardDSDLPrimitiveSerialize(dest.data(), 77, 8, &i8));

    std::uint16_t u16 = 0b11101100101;
    REQUIRE(96 == canardDSDLPrimitiveSerialize(dest.data(), 85, 11, &u16));

    u8 = 0b1110;
    REQUIRE(99 == canardDSDLPrimitiveSerialize(dest.data(), 96, 3, &u8));

    REQUIRE_THAT(std::vector<std::uint8_t>(std::begin(dest), std::begin(dest) + 12),
                 Catch::Matchers::Equals(std::vector<std::uint8_t>(std::begin(Reference), std::begin(Reference) + 12)));

    double        f64 = 1.0;
    float         f32 = 1.0F;
    std::uint16_t f16 = canardDSDLFloat16Pack(-99999.0F);
    REQUIRE(163 == canardDSDLPrimitiveSerialize(dest.data(), 99, 64, &f64));
    REQUIRE(195 == canardDSDLPrimitiveSerialize(dest.data(), 163, 32, &f32));
    REQUIRE(211 == canardDSDLPrimitiveSerialize(dest.data(), 195, 16, &f16));

    u16 = 0xDEAD;
    REQUIRE(227 == canardDSDLPrimitiveSerialize(dest.data(), 211, 16, &u16));
    u16 = 0xBEEF;
    REQUIRE(243 == canardDSDLPrimitiveSerialize(dest.data(), 227, 16, &u16));

    u8 = 0;
    REQUIRE(248 == canardDSDLPrimitiveSerialize(dest.data(), 243, 5, &u8));

    REQUIRE(std::size(dest) == std::size(Reference));
    REQUIRE_THAT(dest, Catch::Matchers::Equals(Reference));
}

TEST_CASE("canardDSDLPrimitiveSerialize_heartbeat")
{
    // The reference values were taken from the PyUAVCAN test.
    const std::vector<std::uint8_t> Reference({239, 190, 173, 222, 234, 255, 255, 0});

    std::vector<std::uint8_t> dest(std::size(Reference));

    std::uint32_t uptime = 0xdeadbeef;
    std::uint8_t  health = 2;
    std::uint8_t  mode   = 2;
    std::uint32_t vssc   = 0x7FFFF;

    REQUIRE(37 == canardDSDLPrimitiveSerialize(dest.data(), 34, 3, &mode));
    REQUIRE(32 == canardDSDLPrimitiveSerialize(dest.data(), 0, 32, &uptime));
    REQUIRE(56 == canardDSDLPrimitiveSerialize(dest.data(), 37, 19, &vssc));
    REQUIRE(34 == canardDSDLPrimitiveSerialize(dest.data(), 32, 2, &health));

    REQUIRE(std::size(dest) == std::size(Reference));
    REQUIRE_THAT(dest, Catch::Matchers::Equals(Reference));
}
