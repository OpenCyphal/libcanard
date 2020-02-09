// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#include "internals.hpp"
#include "helpers.hpp"

TEST_CASE("TransferCRC")
{
    using internals::crcAdd;
    std::uint16_t crc = 0xFFFFU;

    crc = crcAdd(crc, 1, "1");
    crc = crcAdd(crc, 1, "2");
    crc = crcAdd(crc, 1, "3");
    REQUIRE(0x5BCEU == crc);  // Using Libuavcan as reference
    crc = crcAdd(crc, 6, "456789");
    REQUIRE(0x29B1U == crc);
}

TEST_CASE("SessionSpecifier")
{
    REQUIRE(0b000'00'0011001100110011'0'1010101 ==
            internals::makeMessageSessionSpecifier(0b0011001100110011, 0b1010101));
    REQUIRE(0b000'11'0100110011'0101010'1010101 ==
            internals::makeServiceSessionSpecifier(0b0100110011, true, 0b1010101, 0b0101010));
    REQUIRE(0b000'10'0100110011'1010101'0101010 ==
            internals::makeServiceSessionSpecifier(0b0100110011, false, 0b0101010, 0b1010101));
}

TEST_CASE("getPresentationLayerMTU")
{
    auto ins =
        canardInit(&helpers::dummy_allocator::allocate, &helpers::dummy_allocator::free, &helpers::rejectAllRxFilter);
    REQUIRE(63 == internals::getPresentationLayerMTU(&ins));  // This is the default.
    ins.mtu_bytes = 0;
    REQUIRE(7 == internals::getPresentationLayerMTU(&ins));
    ins.mtu_bytes = 255;
    REQUIRE(63 == internals::getPresentationLayerMTU(&ins));
    ins.mtu_bytes = 32;
    REQUIRE(31 == internals::getPresentationLayerMTU(&ins));
    ins.mtu_bytes = 30;  // Round up.
    REQUIRE(31 == internals::getPresentationLayerMTU(&ins));
}

TEST_CASE("makeCANID") {}

TEST_CASE("makeTailByte") {}

TEST_CASE("findTxQueueSupremum") {}
