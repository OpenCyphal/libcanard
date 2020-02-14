// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#include "canard_dsdl.h"
#include "internals.hpp"
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
