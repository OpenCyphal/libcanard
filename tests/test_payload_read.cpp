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
#include <stdexcept>
#include <gtest/gtest.h>
#include "canard_internals.h"


TEST(BigEndian, Check)
{
    // Assuming that unit tests can only be run on little-endian platforms!
    ASSERT_FALSE(isBigEndian());
}


template <typename T>
static inline T read(CanardRxTransfer* transfer, uint16_t bit_offset, uint8_t bit_length)
{
    auto value = T();

    const int res = canardReadScalarFromRxTransfer(transfer, bit_offset, bit_length, std::is_signed<T>::value, &value);
    if (res != bit_length)
    {
        throw std::runtime_error("Unexpected return value; expected " +
                                 std::to_string(bit_length) + ", got " + std::to_string(res));
    }

    return value;
}


TEST(PayloadRead, SingleFrame)
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

    ASSERT_EQ(0b10100101, read<uint8_t>(&transfer, 0, 8));
    ASSERT_EQ(0b01011100, read<uint8_t>(&transfer, 4, 8));
    ASSERT_EQ(0b00000101, read<uint8_t>(&transfer, 4, 4));

    ASSERT_EQ(true,  read<bool>(&transfer, 9, 1));
    ASSERT_EQ(false, read<bool>(&transfer, 10, 1));

    ASSERT_EQ(0b11101000101010100101010101111110, read<uint32_t>(&transfer, 24, 32));

    /*
     * Raw bit stream with offset 21:
     *   111 01111110 01010101 10101010 11101
     * New byte segmentation:
     *   11101111 11001010 10110101 01011101
     * Which is little endian representation of:
     *   0b01011101101101011100101011101111
     */
    ASSERT_EQ(0b01011101101101011100101011101111, read<uint32_t>(&transfer, 21, 32));

    // Should fail
    ASSERT_THROW(read<uint32_t>(&transfer, 25, 32), std::runtime_error);

    // Inexact size
    ASSERT_EQ(0b010111101101011100101011101111, read<uint32_t>(&transfer, 21, 30));
}
