// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#include "canard_dsdl.h"
#include "exposed.hpp"
#include <array>
#include <cmath>
#include <iostream>

TEST_CASE("float16Pack")
{
    using exposed::float16Pack;
    // Reference values were generated manually with libuavcan and numpy.float16().
    REQUIRE(0b0000000000000000 == float16Pack(0.0F));
    REQUIRE(0b0011110000000000 == float16Pack(1.0F));
    REQUIRE(0b1100000000000000 == float16Pack(-2.0F));
    REQUIRE(0b0111110000000000 == float16Pack(999999.0F));      // +inf
    REQUIRE(0b1111101111111111 == float16Pack(-65519.0F));      // -max
    REQUIRE(0b0111111111111111 == float16Pack(std::nanf("")));  // nan
}

TEST_CASE("float16Unpack")
{
    using exposed::float16Unpack;
    // Reference values were generated manually with libuavcan and numpy.float16().
    REQUIRE(Approx(0.0F) == float16Unpack(0b0000000000000000));
    REQUIRE(Approx(1.0F) == float16Unpack(0b0011110000000000));
    REQUIRE(Approx(-2.0F) == float16Unpack(0b1100000000000000));
    REQUIRE(Approx(-65504.0F) == float16Unpack(0b1111101111111111));
    REQUIRE(std::isinf(float16Unpack(0b0111110000000000)));

    REQUIRE(bool(std::isnan(float16Unpack(0b0111111111111111))));
}

TEST_CASE("canardDSDLFloat16")
{
    using exposed::float16Pack;
    using exposed::float16Unpack;
    float x = -1000.0F;
    while (x <= 1000.0F)
    {
        REQUIRE(Approx(x) == float16Unpack(float16Pack(x)));
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

TEST_CASE("canardDSDLSerialize_aligned")
{
    // The reference values for the following test have been taken from the PyUAVCAN test suite.
    const std::vector<std::uint8_t> Reference({0xA7, 0xEF, 0xCD, 0xAB, 0x90, 0x78, 0x56, 0x34, 0x12, 0x88, 0xA9, 0xCB,
                                               0xED, 0xFE, 0xFF, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0,
                                               0x3F, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x7C, 0xDA, 0x0E, 0xDA, 0xBE, 0xFE,
                                               0x01, 0xAD, 0xDE, 0xEF, 0xBE, 0xC5, 0x67, 0xC5, 0x0B});

    std::vector<std::uint8_t> dest(std::size(Reference));

    const auto set_b = [&](const std::size_t offset_bit, const bool value) {
        canardDSDLSetBit(dest.data(), offset_bit, value);
    };
    const auto set_u = [&](const std::size_t offset_bit, const std::uint64_t value, const std::uint8_t length_bit) {
        canardDSDLSetUxx(dest.data(), offset_bit, value, length_bit);
    };
    const auto set_i = [&](const std::size_t offset_bit, const std::int64_t value, const std::uint8_t length_bit) {
        canardDSDLSetIxx(dest.data(), offset_bit, value, length_bit);
    };
    const auto set_f16 = [&](const std::size_t offset_bit, const float value) {
        canardDSDLSetF16(dest.data(), offset_bit, value);
    };
    const auto set_f32 = [&](const std::size_t offset_bit, const float value) {
        canardDSDLSetF32(dest.data(), offset_bit, value);
    };
    const auto set_f64 = [&](const std::size_t offset_bit, const double value) {
        canardDSDLSetF64(dest.data(), offset_bit, value);
    };

    set_u(0, 0b1010'0111, 8);
    set_i(8, 0x1234'5678'90ab'cdef, 64);
    set_i(72, -0x1234'5678, 32);
    set_i(104, -2, 16);
    set_u(120, 0, 8);
    set_i(128, 127, 8);
    set_f64(136, 1.0);
    set_f32(200, 1.0F);
    set_f16(232, 99999.9F);
    set_u(248, 0xBEDA, 12);  // Truncation
    set_u(260, 0, 4);
    set_u(264, 0xBEDA, 16);
    set_i(280, -2, 9);
    set_i(289, 0, 7);
    set_u(296, 0xDEAD, 16);
    set_u(312, 0xBEEF, 16);

    std::size_t offset   = 328;
    const auto  push_bit = [&](const bool value) {
        set_b(offset, value);
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

    set_u(357, 0, 3);

    REQUIRE(std::size(dest) == std::size(Reference));
    REQUIRE_THAT(dest, Catch::Matchers::Equals(Reference));
}

TEST_CASE("canardDSDLSerialize_unaligned")
{
    // The reference values for the following test have been taken from the PyUAVCAN test suite.
    const std::vector<std::uint8_t> Reference({
        0xC5, 0x2F, 0x57, 0x82, 0xC6, 0xCA, 0x12, 0x34, 0x56, 0xD9, 0xBF, 0xEC, 0x06, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x80, 0xFF, 0x01, 0x00, 0x00, 0xFC, 0x01, 0xE0, 0x6F, 0xF5, 0x7E, 0xF7, 0x05  //
    });

    std::vector<std::uint8_t> dest(std::size(Reference));

    const auto set_b = [&](const std::size_t offset_bit, const bool value) {
        canardDSDLSetBit(dest.data(), offset_bit, value);
    };
    const auto set_u = [&](const std::size_t offset_bit, const std::uint64_t value, const std::uint8_t length_bit) {
        canardDSDLSetUxx(dest.data(), offset_bit, value, length_bit);
    };
    const auto set_i = [&](const std::size_t offset_bit, const std::int64_t value, const std::uint8_t length_bit) {
        canardDSDLSetIxx(dest.data(), offset_bit, value, length_bit);
    };
    const auto set_f16 = [&](const std::size_t offset_bit, const float value) {
        canardDSDLSetF16(dest.data(), offset_bit, value);
    };
    const auto set_f32 = [&](const std::size_t offset_bit, const float value) {
        canardDSDLSetF32(dest.data(), offset_bit, value);
    };
    const auto set_f64 = [&](const std::size_t offset_bit, const double value) {
        canardDSDLSetF64(dest.data(), offset_bit, value);
    };

    std::size_t offset   = 0;
    const auto  push_bit = [&](const bool value) {
        set_b(offset, value);
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

    set_u(21, 0x12, 8);
    set_u(29, 0x34, 8);
    set_u(37, 0x56, 8);

    REQUIRE_THAT(std::vector<std::uint8_t>(std::begin(dest), std::begin(dest) + 5),
                 Catch::Matchers::Equals(std::vector<std::uint8_t>(std::begin(Reference), std::begin(Reference) + 5)));

    offset = 45;
    push_bit(false);
    push_bit(true);
    push_bit(true);

    set_u(48, 0x12, 8);
    set_u(56, 0x34, 8);
    set_u(64, 0x56, 8);

    offset = 72;
    push_bit(true);
    push_bit(false);
    push_bit(false);
    push_bit(true);
    push_bit(true);

    REQUIRE_THAT(std::vector<std::uint8_t>(std::begin(dest), std::begin(dest) + 9),
                 Catch::Matchers::Equals(std::vector<std::uint8_t>(std::begin(Reference), std::begin(Reference) + 9)));

    set_i(77, -2, 8);
    set_u(85, 0b11101100101, 11);
    set_u(96, 0b1110, 3);  // Truncation

    REQUIRE_THAT(std::vector<std::uint8_t>(std::begin(dest), std::begin(dest) + 12),
                 Catch::Matchers::Equals(std::vector<std::uint8_t>(std::begin(Reference), std::begin(Reference) + 12)));

    set_f64(99, 1.0);
    set_f32(163, 1.0F);
    set_f16(195, -99999.0F);

    set_u(211, 0xDEAD, 16);
    set_u(227, 0xBEEF, 16);
    set_u(243, 0, 5);

    REQUIRE(std::size(dest) == std::size(Reference));
    REQUIRE_THAT(dest, Catch::Matchers::Equals(Reference));
}

TEST_CASE("canardDSDLSerialize_heartbeat")
{
    // The reference values were taken from the PyUAVCAN test.
    const std::vector<std::uint8_t> Reference({239, 190, 173, 222, 234, 255, 255, 0});

    std::vector<std::uint8_t> dest(std::size(Reference));

    const auto set_u = [&](const std::size_t offset_bit, const std::uint64_t value, const std::uint8_t length_bit) {
        canardDSDLSetUxx(dest.data(), offset_bit, value, length_bit);
    };

    set_u(34, 2, 3);           // mode
    set_u(0, 0xdeadbeef, 32);  // uptime
    set_u(37, 0x7FFFF, 19);    // vssc
    set_u(32, 2, 2);           // health

    REQUIRE(std::size(dest) == std::size(Reference));
    REQUIRE_THAT(dest, Catch::Matchers::Equals(Reference));
}

TEST_CASE("canardDSDLDeserialize_aligned")
{
    // The reference values for the following test have been taken from the PyUAVCAN test suite.
    const std::vector<std::uint8_t> Reference({0xA7, 0xEF, 0xCD, 0xAB, 0x90, 0x78, 0x56, 0x34, 0x12, 0x88, 0xA9, 0xCB,
                                               0xED, 0xFE, 0xFF, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0,
                                               0x3F, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x7C, 0xDA, 0x0E, 0xDA, 0xBE, 0xFE,
                                               0x01, 0xAD, 0xDE, 0xEF, 0xBE, 0xC5, 0x67, 0xC5, 0x0B});
    const std::uint8_t* const       buf = Reference.data();

    REQUIRE(canardDSDLGetBit(buf, 1, 0));
    REQUIRE(!canardDSDLGetBit(buf, 1, 3));
    REQUIRE(!canardDSDLGetBit(buf, 0, 0));  // IZER

    REQUIRE(0b1010'0111 == canardDSDLGetU8(buf, 45, 0, 8));

    REQUIRE(0x1234'5678'90ab'cdef == canardDSDLGetI64(buf, 45, 8, 64));
    REQUIRE(0x1234'5678'90ab'cdef == canardDSDLGetU64(buf, 45, 8, 64));
    REQUIRE(0xef == canardDSDLGetU8(buf, 45, 8, 64));

    REQUIRE(-0x1234'5678 == canardDSDLGetI32(buf, 45, 72, 32));
    REQUIRE(-2 == canardDSDLGetI16(buf, 45, 104, 16));
    REQUIRE(0 == canardDSDLGetU8(buf, 45, 120, 8));
    REQUIRE(127 == canardDSDLGetI8(buf, 45, 128, 8));
}
