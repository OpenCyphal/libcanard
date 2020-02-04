// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#include "internals.hpp"
#include <cmath>

TEST_CASE("canardDSDLFloat16Serialize")
{
    // Reference values were generated manually with libuavcan and numpy.float16().
    REQUIRE(0b0000000000000000 == canardDSDLFloat16Serialize(0.0F));
    REQUIRE(0b0011110000000000 == canardDSDLFloat16Serialize(1.0F));
    REQUIRE(0b1100000000000000 == canardDSDLFloat16Serialize(-2.0F));
    REQUIRE(0b0111110000000000 == canardDSDLFloat16Serialize(999999.0F));      // +inf
    REQUIRE(0b1111101111111111 == canardDSDLFloat16Serialize(-65519.0F));      // -max
    REQUIRE(0b0111111111111111 == canardDSDLFloat16Serialize(std::nanf("")));  // nan
}

TEST_CASE("canardDSDLFloat16Deserialize")
{
    // Reference values were generated manually with libuavcan and numpy.float16().
    REQUIRE(Approx(0.0F) == canardDSDLFloat16Deserialize(0b0000000000000000));
    REQUIRE(Approx(1.0F) == canardDSDLFloat16Deserialize(0b0011110000000000));
    REQUIRE(Approx(-2.0F) == canardDSDLFloat16Deserialize(0b1100000000000000));
    REQUIRE(Approx(-65504.0F) == canardDSDLFloat16Deserialize(0b1111101111111111));
    REQUIRE(std::isinf(canardDSDLFloat16Deserialize(0b0111110000000000)));

    REQUIRE(bool(std::isnan(canardDSDLFloat16Deserialize(0b0111111111111111))));
}

TEST_CASE("canardDSDLFloat16")
{
    float x = -1000.0F;
    while (x <= 1000.0F)
    {
        REQUIRE(Approx(x) == canardDSDLFloat16Deserialize(canardDSDLFloat16Serialize(x)));
        x += 0.5F;
    }
}
