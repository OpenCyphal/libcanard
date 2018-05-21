/*
 * Copyright (c) 2016 UAVCAN Team
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Contributors: https://github.com/UAVCAN/libcanard/contributors
 */

#include <type_traits>
#include <algorithm>
#include <stdexcept>
#include <memory>
#include <catch.hpp>
#include <iostream>
#include "canard_internals.h"


TEST_CASE("BigEndian, Check")
{
    // Assuming that unit tests can only be run on little-endian platforms!
    REQUIRE_FALSE(isBigEndian());
}


template <typename T>
static inline T read(CanardRxTransfer* transfer, uint32_t bit_offset, uint8_t bit_length)
{
    auto value = T();

    const int res = canardDecodeScalar(transfer, uint16_t(bit_offset), bit_length, std::is_signed<T>::value, &value);
    if (res != bit_length)
    {
        throw std::runtime_error("Unexpected return value; expected " +
                                 std::to_string(unsigned(bit_length)) + ", got " + std::to_string(res));
    }

    return value;
}


TEST_CASE("ScalarDecode, SingleFrame")
{
    auto transfer = CanardRxTransfer();

    static const uint8_t buf[7] =
    {
        0b10100101, // 0
        0b11000011, // 8
        0b11100111, // 16
        0b01111110, // 24
        0b01010101,
        0b10101010,
        0b11101000
    };

    transfer.payload_head = &buf[0];
    transfer.payload_len = sizeof(buf);

    REQUIRE(0b10100101 == read<uint8_t>(&transfer, 0, 8));
    REQUIRE(0b01011100 == read<uint8_t>(&transfer, 4, 8));
    REQUIRE(0b00000101 == read<uint8_t>(&transfer, 4, 4));

    REQUIRE(read<bool>(&transfer, 9, 1));
    REQUIRE_FALSE(read<bool>(&transfer, 10, 1));

    REQUIRE(0b11101000101010100101010101111110 == read<uint32_t>(&transfer, 24, 32));

    /*
     * Raw bit stream with offset 21:
     *   111 01111110 01010101 10101010 11101
     * New byte segmentation:
     *   11101111 11001010 10110101 01011101
     * Which is little endian representation of:
     *   0b01011101101101011100101011101111
     */
    REQUIRE(0b01011101101101011100101011101111 == read<uint32_t>(&transfer, 21, 32));

    // Should fail
    REQUIRE_THROWS_AS(read<uint32_t>(&transfer, 25, 32), std::runtime_error);

    // Inexact size
    REQUIRE(0b010111101101011100101011101111 == read<uint32_t>(&transfer, 21, 30));

    // Negatives; reference values taken from libuavcan test suite or computed manually
    REQUIRE(-1 == read<int8_t>(&transfer, 16, 3));  // 0b111
    REQUIRE(-4 == read<int8_t>(&transfer, 2, 3));   // 0b100

    REQUIRE(-91    == read<int8_t>(&transfer, 0, 8));       //         0b10100101
    REQUIRE(-15451 == read<int16_t>(&transfer, 0, 16));     // 0b1100001110100101
    REQUIRE(-7771  == read<int16_t>(&transfer, 0, 15));     //  0b100001110100101
}


TEST_CASE("ScalarDecode, MultiFrame")
{
    /*
     * Configuring allocator
     */
    CanardPoolAllocatorBlock allocator_blocks[2];
    CanardPoolAllocator allocator;
    initPoolAllocator(&allocator, &allocator_blocks[0], 2);

    /*
     * Configuring the transfer object
     */
    auto transfer = CanardRxTransfer();

    uint8_t head[CANARD_MULTIFRAME_RX_PAYLOAD_HEAD_SIZE];
    for (auto& x : head)
    {
        x = 0b10100101;
    }
    static_assert(CANARD_MULTIFRAME_RX_PAYLOAD_HEAD_SIZE == 6, "Assumption is not met, are we on a 32-bit x86 machine?");

    auto middle_a = createBufferBlock(&allocator);
    auto middle_b = createBufferBlock(&allocator);

    std::fill_n(&middle_a->data[0], CANARD_BUFFER_BLOCK_DATA_SIZE, 0b01011010);
    std::fill_n(&middle_b->data[0], CANARD_BUFFER_BLOCK_DATA_SIZE, 0b11001100);

    middle_a->next = middle_b;
    middle_b->next = nullptr;

    const uint8_t tail[4] =
    {
        0b00010001,
        0b00100010,
        0b00110011,
        0b01000100
    };

    transfer.payload_head   = &head[0];
    transfer.payload_middle = middle_a;
    transfer.payload_tail   = &tail[0];

    transfer.payload_len =
        uint16_t(CANARD_MULTIFRAME_RX_PAYLOAD_HEAD_SIZE + CANARD_BUFFER_BLOCK_DATA_SIZE * 2 + sizeof(tail));

    std::cout << "Payload size: " << transfer.payload_len << std::endl;

    /*
     * Testing
     */
    REQUIRE(0b10100101 == read<uint8_t>(&transfer, 0, 8));
    REQUIRE(0b01011010 == read<uint8_t>(&transfer, 4, 8));
    REQUIRE(0b00000101 == read<uint8_t>(&transfer, 4, 4));

    REQUIRE_FALSE(read<bool>(&transfer, CANARD_MULTIFRAME_RX_PAYLOAD_HEAD_SIZE * 8, 1));
    REQUIRE(read<bool>(&transfer, CANARD_MULTIFRAME_RX_PAYLOAD_HEAD_SIZE * 8 + 1, 1));

    // 64 from beginning, 48 bits from head, 16 bits from the middle
    REQUIRE(0b0101101001011010101001011010010110100101101001011010010110100101ULL == read<uint64_t>(&transfer, 0, 64));

    // 64 from two middle blocks, 32 from the first, 32 from the second
    REQUIRE(0b1100110011001100110011001100110001011010010110100101101001011010ULL ==
            read<uint64_t>(&transfer,
                           CANARD_MULTIFRAME_RX_PAYLOAD_HEAD_SIZE * 8 + CANARD_BUFFER_BLOCK_DATA_SIZE * 8 - 32, 64));

    // Last 64
    REQUIRE(0b0100010000110011001000100001000111001100110011001100110011001100ULL ==
            read<uint64_t>(&transfer, transfer.payload_len * 8U - 64U, 64));

    /*
     * Testing without the middle
     */
    transfer.payload_middle = nullptr;
    transfer.payload_len = uint16_t(transfer.payload_len - CANARD_BUFFER_BLOCK_DATA_SIZE * 2U);

    // Last 64
    REQUIRE(0b0100010000110011001000100001000110100101101001011010010110100101ULL ==
            read<uint64_t>(&transfer, transfer.payload_len * 8U - 64U, 64));
}


TEST_CASE("ScalarEncode, Basic")
{
    uint8_t buffer[32];
    std::fill_n(std::begin(buffer), sizeof(buffer), 0);

    uint8_t u8 = 123;
    canardEncodeScalar(buffer, 0, 8, &u8);
    REQUIRE(123 == buffer[0]);
    REQUIRE(0 == buffer[1]);

    u8 = 0b1111;
    canardEncodeScalar(buffer, 5, 4, &u8);
    REQUIRE((123U | 0b111U) == buffer[0]);
    REQUIRE(0b10000000 == buffer[1]);

    int16_t s16 = -1;
    canardEncodeScalar(buffer, 9, 15, &s16);
    REQUIRE((123U | 0b111U) == buffer[0]);
    REQUIRE(0b11111111 == buffer[1]);
    REQUIRE(0b11111111 == buffer[2]);
    REQUIRE(0b00000000 == buffer[3]);

    auto s64 = int64_t(0b0000000100100011101111000110011110001001101010111100110111101111L);
    canardEncodeScalar(buffer, 16, 60, &s64);
    REQUIRE((123U | 0b111U) == buffer[0]);  // 0
    REQUIRE(0b11111111 == buffer[1]);    // 8
    REQUIRE(0b11101111 == buffer[2]);    // 16
    REQUIRE(0b11001101 == buffer[3]);
    REQUIRE(0b10101011 == buffer[4]);
    REQUIRE(0b10001001 == buffer[5]);
    REQUIRE(0b01100111 == buffer[6]);
    REQUIRE(0b10111100 == buffer[7]);
    REQUIRE(0b00100011 == buffer[8]);
    REQUIRE(0b00010000 == buffer[9]);
}
