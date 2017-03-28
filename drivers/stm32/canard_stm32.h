/*
 * Copyright (c) 2017 UAVCAN Team
 *
 * Distributed under the MIT License, available in the file LICENSE.
 *
 * Author: Pavel Kirienko <pavel.kirienko@zubax.com>
 */

#ifndef CANARD_STM32_H
#define CANARD_STM32_H

#include <canard.h>
#include <string.h>


#ifdef __cplusplus
extern "C"
{
#endif

#define CANARD_STM32_ERROR_UNSUPPORTED_BIT_RATE                         1000
#define CANARD_STM32_ERROR_MSR_INAK_NOT_SET                             1001
#define CANARD_STM32_ERROR_MSR_INAK_NOT_CLEARED                         1002
#define CANARD_STM32_ERROR_UNSUPPORTED_FRAME_FORMAT                     1003

/**
 * This is defined by the bxCAN hardware.
 * Actually there is 28 filters, but only 27 are available to one interface at the same time (check this).
 */
#define CANARD_STM32_NUM_ACCEPTANCE_FILTERS                             27


typedef enum
{
    CanardSTM32IfaceModeNormal,
    CanardSTM32IfaceModeSilent
} CanardSTM32IfaceMode;


typedef struct
{
    uint32_t id;
    uint32_t mask;
} CanardSTM32AcceptanceFilterConfiguration;

/**
 * These parameters define the timings of the CAN controller.
 * Please refer to the documentation of the bxCAN macrocell for explanation.
 * These values can be computed by the developed beforehand if ROM size is of a concern,
 * or they can be computed at run time using the function defined below.
 */
typedef struct
{
    uint16_t bit_rate_prescaler;                        /// [1, 1024]
    uint8_t bit_segment_1;                              /// [1, 16]
    uint8_t bit_segment_2;                              /// [1, 8]
    uint8_t max_resynchronization_jump_width;           /// [1, 4] (recommended value is 1)
} CanardSTM32CANTimings;

/**
 * Initializes the CAN controller at the specified bit rate.
 * The mode can be either normal or silent;
 * in silent mode the controller will be only listening, not affecting the state of the bus.
 * This function can be invoked any number of times.
 *
 * WARNING: The clock of the CAN module must be enabled before this function is invoked!
 *
 * WARNING: The driver is not thread-safe!
 *
 * @retval      0               Success
 * @retval      negative        Error
 */
int canardSTM32Init(const CanardSTM32CANTimings* timings,
                    CanardSTM32IfaceMode iface_mode);

/**
 * Pushes one frame into the TX buffer, if there is space.
 * Note that proper care is taken to ensure that no inner priority inversion is taking place.
 * This function does never block.
 *
 * @retval      1               Transmitted successfully
 * @retval      0               No space in the buffer
 * @retval      negative        Error
 */
int canardSTM32Transmit(const CanardCANFrame* frame);

/**
 * Reads one frame from the RX buffer, unless the buffer is empty.
 * This function does never block.
 *
 * @retval      1               Read successfully
 * @retval      0               The buffer is empty
 * @retval      negative        Error
 */
int canardSTM32Receive(CanardCANFrame* out_frame);

/**
 * Sets up acceptance filters according to the provided list of ID and masks.
 * Note that when the interface is reinitialized, hardware acceptance filters are reset.
 *
 * @retval      0               Success
 * @retval      negative        Error
 */
int canardSTM32ConfigureAcceptanceFilters(const CanardSTM32AcceptanceFilterConfiguration* filter_configs,
                                          unsigned num_filter_configs);

/**
 * Given the rate of the clock supplied to the bxCAN macrocell (typically PCLK1) and the desired bit rate,
 * this function iteratively solves for the best possible timing settings. The CAN bus timing parameters,
 * such as the sample point location, the number of time quantas per bit, etc., are optimized according to the
 * recommendations provided in the specifications of UAVCAN, DeviceNet, and CANOpen.
 *
 * The implementation is adapted from libuavcan.
 *
 * This function is defined in the header in order to encourage the linker to discard it if it is not used.
 *
 * The following code has been used to test it (gcc -std=c99 test.c && ./a.out):
 *
 *     static void runOnce(const uint32_t pclk1,
 *                         const uint32_t target_bitrate)
 *     {
 *         CanardSTM32CANTimings timings;
 *         int res = canardSTM32ComputeCANTimings(pclk1, target_bitrate, &timings);
 *         const uint16_t sample_point_permill =
 *             (uint16_t)(1000 * (1 + timings.bit_segment_1) / (1 + timings.bit_segment_1 + timings.bit_segment_2));
 *         printf("target %9u    %s (%d)    presc %4u    bs %u/%u %.1f%%\n",
 *                (unsigned)target_bitrate, (res == 0) ? "OK" : "FAIL", res, timings.bit_rate_prescaler,
 *                timings.bit_segment_1, timings.bit_segment_2, sample_point_permill * 0.1F);
 *     }
 *
 *     static void testPCLK(const uint32_t pclk1)
 *     {
 *         runOnce(pclk1, 1000000);
 *         runOnce(pclk1,  500000);
 *         runOnce(pclk1,  250000);
 *         runOnce(pclk1,  125000);
 *         runOnce(pclk1,  100000);
 *         runOnce(pclk1,   10000);
 *     }
 *
 *     int main(void)
 *     {
 *         testPCLK(36000000);
 *         testPCLK(90000000);
 *         return 0;
 *     }
 *
 * @retval 0            Success
 * @retval negative     Solution could not be found for the provided inputs.
 */
static
int canardSTM32ComputeCANTimings(const uint32_t peripheral_clock_rate,
                                 const uint32_t target_bitrate,
                                 CanardSTM32CANTimings* const out_timings)
{
    if (target_bitrate < 1000)
    {
        return -CANARD_STM32_ERROR_UNSUPPORTED_BIT_RATE;
    }

    assert(out_timings != NULL);
    memset(out_timings, 0, sizeof(*out_timings));

    /*
     * Hardware configuration
     */
    static const int MaxBS1 = 16;
    static const int MaxBS2 = 8;

    /*
     * Ref. "Automatic Baudrate Detection in CANopen Networks", U. Koppe, MicroControl GmbH & Co. KG
     *      CAN in Automation, 2003
     *
     * According to the source, optimal quanta per bit are:
     *   Bitrate        Optimal Maximum
     *   1000 kbps      8       10
     *   500  kbps      16      17
     *   250  kbps      16      17
     *   125  kbps      16      17
     */
    const int max_quanta_per_bit = (target_bitrate >= 1000000) ? 10 : 17;
    assert(max_quanta_per_bit <= (MaxBS1 + MaxBS2));

    static const int MaxSamplePointLocationPermill = 900;

    /*
     * Computing (prescaler * BS):
     *   BITRATE = 1 / (PRESCALER * (1 / PCLK) * (1 + BS1 + BS2))       -- See the Reference Manual
     *   BITRATE = PCLK / (PRESCALER * (1 + BS1 + BS2))                 -- Simplified
     * let:
     *   BS = 1 + BS1 + BS2                                             -- Number of time quanta per bit
     *   PRESCALER_BS = PRESCALER * BS
     * ==>
     *   PRESCALER_BS = PCLK / BITRATE
     */
    const uint32_t prescaler_bs = peripheral_clock_rate / target_bitrate;

    /*
     * Searching for such prescaler value so that the number of quanta per bit is highest.
     */
    uint8_t bs1_bs2_sum = (uint8_t)(max_quanta_per_bit - 1);

    while ((prescaler_bs % (1 + bs1_bs2_sum)) != 0)
    {
        if (bs1_bs2_sum <= 2)
        {
            return -CANARD_STM32_ERROR_UNSUPPORTED_BIT_RATE;          // No solution
        }
        bs1_bs2_sum--;
    }

    const uint32_t prescaler = prescaler_bs / (1 + bs1_bs2_sum);
    if ((prescaler < 1U) || (prescaler > 1024U))
    {
        return -CANARD_STM32_ERROR_UNSUPPORTED_BIT_RATE;              // No solution
    }

    /*
     * Now we have a constraint: (BS1 + BS2) == bs1_bs2_sum.
     * We need to find such values so that the sample point is as close as possible to the optimal value,
     * which is 87.5%, which is 7/8.
     *
     *   Solve[(1 + bs1)/(1 + bs1 + bs2) == 7/8, bs2]  (* Where 7/8 is 0.875, the recommended sample point location *)
     *   {{bs2 -> (1 + bs1)/7}}
     *
     * Hence:
     *   bs2 = (1 + bs1) / 7
     *   bs1 = (7 * bs1_bs2_sum - 1) / 8
     *
     * Sample point location can be computed as follows:
     *   Sample point location = (1 + bs1) / (1 + bs1 + bs2)
     *
     * Since the optimal solution is so close to the maximum, we prepare two solutions, and then pick the best one:
     *   - With rounding to nearest
     *   - With rounding to zero
     */
    uint8_t bs1 = (uint8_t)(((7 * bs1_bs2_sum - 1) + 4) / 8);       // Trying rounding to nearest first
    uint8_t bs2 = (uint8_t)(bs1_bs2_sum - bs1);
    assert(bs1_bs2_sum > bs1);

    {
        const uint16_t sample_point_permill = (uint16_t)(1000 * (1 + bs1) / (1 + bs1 + bs2));

        if (sample_point_permill > MaxSamplePointLocationPermill)   // Strictly more!
        {
            bs1 = (uint8_t)((7 * bs1_bs2_sum - 1) / 8);             // Nope, too far; now rounding to zero
            bs2 = (uint8_t)(bs1_bs2_sum - bs1);
        }
    }

    const bool valid = (bs1 >= 1) && (bs1 <= MaxBS1) && (bs2 >= 1) && (bs2 <= MaxBS2);

    /*
     * Final validation
     * Helpful Python:
     * def sample_point_from_btr(x):
     *     assert 0b0011110010000000111111000000000 & x == 0
     *     ts2,ts1,brp = (x>>20)&7, (x>>16)&15, x&511
     *     return (1+ts1+1)/(1+ts1+1+ts2+1)
     */
    if ((target_bitrate != (peripheral_clock_rate / (prescaler * (1 + bs1 + bs2)))) ||
        !valid)
    {
        // This actually means that the algorithm has a logic error, hence assert(0).
        assert(0);
        return -CANARD_STM32_ERROR_UNSUPPORTED_BIT_RATE;
    }

    out_timings->bit_rate_prescaler = prescaler;
    out_timings->max_resynchronization_jump_width = 1;      // One is recommended by UAVCAN, CANOpen, and DeviceNet
    out_timings->bit_segment_1 = bs1;
    out_timings->bit_segment_2 = bs2;

    return 0;
}

#ifdef __cplusplus
}
#endif
#endif
