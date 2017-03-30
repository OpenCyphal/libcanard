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

#include <gtest/gtest.h>
#include <drivers/stm32/canard_stm32.h>
#include <cstdio>
#include <string>
#include <stdexcept>


static std::uint32_t computeBDTR(const std::uint32_t pclk1,
                                 const std::uint32_t target_bitrate)
{
    CanardSTM32CANTimings timings = CanardSTM32CANTimings();

    const int res = canardSTM32ComputeCANTimings(pclk1, target_bitrate, &timings);

    const std::uint16_t sample_point_permill =
        std::uint16_t((1000 * (1 + timings.bit_segment_1) / (1 + timings.bit_segment_1 + timings.bit_segment_2)));

    const std::uint32_t bdtr = (((timings.max_resynchronization_jump_width - 1) &    3U) << 24) |
                               (((timings.bit_segment_1 - 1)                    &   15U) << 16) |
                               (((timings.bit_segment_2 - 1)                    &    7U) << 20) |
                               (((timings.bit_rate_prescaler - 1)               & 1023U) << 0);

    std::printf("target %9u    %s (%d)    presc %4u    bs %u/%u %.1f%%\n",
                (unsigned)target_bitrate, (res == 0) ? "OK" : "FAIL", res, timings.bit_rate_prescaler,
                    timings.bit_segment_1, timings.bit_segment_2, sample_point_permill * 0.1F);

    if (res != 0)
    {
        throw std::runtime_error("canardSTM32ComputeCANTimings() returned " + std::to_string(res));
    }

    return bdtr;
}


/*
 * Reference values were validated manually with the help of http://www.bittiming.can-wiki.info/
 */
TEST(STM32, CANTimings)
{
    EXPECT_EQ(0x00060003, computeBDTR(36000000, 1000000));
    EXPECT_EQ(0x00060007, computeBDTR(36000000,  500000));
}
