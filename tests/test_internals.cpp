// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#include "internals.hpp"

TEST_CASE("SessionSpecifier")
{
    REQUIRE(0b000'00'0011001100110011'0'1010101 ==
            internals::makeMessageSessionSpecifier(0b0011001100110011, 0b1010101));
    REQUIRE(0b000'11'0100110011'0101010'1010101 ==
            internals::makeServiceSessionSpecifier(0b0100110011, true, 0b1010101, 0b0101010));
    REQUIRE(0b000'10'0100110011'1010101'0101010 ==
            internals::makeServiceSessionSpecifier(0b0100110011, false, 0b0101010, 0b1010101));
}

TEST_CASE("TransferCRC")
{
    using internals::crcAdd;
    std::uint16_t crc = 0xFFFFU;
    crc = crcAdd(crc, reinterpret_cast<const uint8_t*>("1"), 1);
    crc = crcAdd(crc, reinterpret_cast<const uint8_t*>("2"), 1);
    crc = crcAdd(crc, reinterpret_cast<const uint8_t*>("3"), 1);
    REQUIRE(0x5BCEU == crc);                                     // Using Libuavcan as reference
    crc = crcAdd(crc, reinterpret_cast<const uint8_t*>("456789"), 6);
    REQUIRE(0x29B1U == crc);
}
