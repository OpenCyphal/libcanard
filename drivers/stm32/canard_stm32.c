/*
 * Copyright (c) 2017 UAVCAN Team
 *
 * Distributed under the MIT License, available in the file LICENSE.
 *
 * Author: Pavel Kirienko <pavel.kirienko@zubax.com>
 */

#include "canard_stm32.h"
#include "_internal_bxcan.h"
#include <unistd.h>

/*
 * Build configuration
 */
#if !defined(CANARD_STM32_USE_CAN2)
# define CANARD_STM32_USE_CAN2                                  0
#endif

#if !defined(CANARD_STM32_DEBUG_INNER_PRIORITY_INVERSION)
# define CANARD_STM32_DEBUG_INNER_PRIORITY_INVERSION            1
#endif

#if CANARD_STM32_USE_CAN2
# define BXCAN                                                  CANARD_STM32_CAN2
#else
# define BXCAN                                                  CANARD_STM32_CAN1
#endif

/*
 * State variables
 */
static CanardSTM32Stats g_stats;


static bool isFramePriorityHigher(uint32_t a, uint32_t b)
{
    const uint32_t clean_a = a & CANARD_CAN_EXT_ID_MASK;
    const uint32_t clean_b = b & CANARD_CAN_EXT_ID_MASK;

    /*
     * STD vs EXT - if 11 most significant bits are the same, EXT loses.
     */
    const bool ext_a = a & CANARD_CAN_FRAME_EFF;
    const bool ext_b = b & CANARD_CAN_FRAME_EFF;
    if (ext_a != ext_b)
    {
        const uint32_t arb11_a = ext_a ? (clean_a >> 18) : clean_a;
        const uint32_t arb11_b = ext_b ? (clean_b >> 18) : clean_b;
        if (arb11_a != arb11_b)
        {
            return arb11_a < arb11_b;
        }
        else
        {
            return ext_b;
        }
    }

    /*
     * RTR vs Data frame - if frame identifiers and frame types are the same, RTR loses.
     */
    const bool rtr_a = a & CANARD_CAN_FRAME_RTR;
    const bool rtr_b = b & CANARD_CAN_FRAME_RTR;
    if ((clean_a == clean_b) && (rtr_a != rtr_b))
    {
        return rtr_b;
    }

    /*
     * Plain ID arbitration - greater value loses.
     */
    return clean_a < clean_b;
}

/// Converts libcanard ID value into the bxCAN TX ID register format.
static uint32_t convertFrameIDCanardToRegister(const uint32_t id)
{
    uint32_t out = 0;

    if (id & CANARD_CAN_FRAME_EFF)
    {
        out = ((id & CANARD_CAN_EXT_ID_MASK) << 3) | CANARD_STM32_CAN_TIR_IDE;
    }
    else
    {
        out = ((id & CANARD_CAN_STD_ID_MASK) << 21);
    }

    if (id & CANARD_CAN_FRAME_RTR)
    {
        out |= CANARD_STM32_CAN_TIR_RTR;
    }

    return out;
}

/// Converts bxCAN TX/RX (sic! both RX/TX are supported) ID register value into the libcanard ID format.
static uint32_t convertFrameIDRegisterToCanard(const uint32_t id)
{
#if (CANARD_STM32_CAN_TIR_RTR != CANARD_STM32_CAN_RIR_RTR) ||\
    (CANARD_STM32_CAN_TIR_IDE != CANARD_STM32_CAN_RIR_IDE)
# error "RIR bits do not match TIR bits, TIR --> libcanard conversion is not possible"
#endif

    uint32_t out = 0;

    if ((id & CANARD_STM32_CAN_RIR_IDE) == 0)
    {
        out = (CANARD_CAN_STD_ID_MASK & (id >> 21));
    }
    else
    {
        out = (CANARD_CAN_EXT_ID_MASK & (id >> 3)) | CANARD_CAN_FRAME_EFF;
    }

    if ((id & CANARD_STM32_CAN_RIR_RTR) != 0)
    {
        out |= CANARD_CAN_FRAME_RTR;
    }

    return out;
}


static bool waitMSRINAKBitStateChange(bool target_state)
{
    static const unsigned TimeoutMilliseconds = 2000;

    for (unsigned wait_ack = 0; wait_ack < TimeoutMilliseconds; wait_ack++)
    {
        const bool state = (BXCAN->MSR & CANARD_STM32_CAN_MSR_INAK) != 0;
        if (state == target_state)
        {
            return true;
        }

        // Sleep 1 millisecond
        usleep(1000);           // TODO: This function may be missing on some platforms
    }

    return false;
}


int canardSTM32Init(const CanardSTM32CANTimings* timings,
                    CanardSTM32IfaceMode iface_mode)
{
    /*
     * Paranoia time.
     */
    if ((iface_mode != CanardSTM32IfaceModeNormal) &&
        (iface_mode != CanardSTM32IfaceModeSilent))
    {
        return -CANARD_ERROR_INVALID_ARGUMENT;
    }

    if ((timings == NULL) ||
        (timings->bit_rate_prescaler < 1) || (timings->bit_rate_prescaler > 1024) ||
        (timings->max_resynchronization_jump_width < 1) || (timings->max_resynchronization_jump_width > 4) ||
        (timings->bit_segment_1 < 1) || (timings->bit_segment_1 > 16) ||
        (timings->bit_segment_2 < 1) || (timings->bit_segment_2 > 8))
    {
        return -CANARD_ERROR_INVALID_ARGUMENT;
    }

    /*
     * Initial setup
     */
    BXCAN->IER = 0;                                     // We need no interrupts
    BXCAN->MCR &= ~CANARD_STM32_CAN_MCR_SLEEP;          // Exit sleep mode
    BXCAN->MCR |= CANARD_STM32_CAN_MCR_INRQ;            // Request init

    if (!waitMSRINAKBitStateChange(true))
    {
        BXCAN->MCR = CANARD_STM32_CAN_MCR_RESET;
        return -CANARD_STM32_ERROR_MSR_INAK_NOT_SET;
    }

    memset(&g_stats, 0, sizeof(g_stats));

    /*
     * Hardware initialization (the hardware has already confirmed initialization mode, see above)
     */
    BXCAN->MCR = CANARD_STM32_CAN_MCR_ABOM | CANARD_STM32_CAN_MCR_AWUM | CANARD_STM32_CAN_MCR_INRQ;  // RM page 648

    BXCAN->BTR = (((timings->max_resynchronization_jump_width - 1) &    3U) << 24) |
                 (((timings->bit_segment_1 - 1)                    &   15U) << 16) |
                 (((timings->bit_segment_2 - 1)                    &    7U) << 20) |
                 ((timings->bit_rate_prescaler - 1)                & 1023U) |
                 ((iface_mode == CanardSTM32IfaceModeSilent) ? CANARD_STM32_CAN_BTR_SILM : 0);

    BXCAN->IER = CANARD_STM32_CAN_IER_TMEIE |   // TX mailbox empty
                 CANARD_STM32_CAN_IER_FMPIE0 |  // RX FIFO 0 is not empty
                 CANARD_STM32_CAN_IER_FMPIE1;   // RX FIFO 1 is not empty

    BXCAN->MCR &= ~CANARD_STM32_CAN_MCR_INRQ;   // Leave init mode

    if (!waitMSRINAKBitStateChange(false))
    {
        BXCAN->MCR = CANARD_STM32_CAN_MCR_RESET;
        return -CANARD_STM32_ERROR_MSR_INAK_NOT_CLEARED;
    }

    /*
     * Default filter configuration
     */
    BXCAN->FMR |= CANARD_STM32_CAN_FMR_FINIT;
    BXCAN->FMR &= 0xFFFFC0F1;

#if CANARD_STM32_USE_CAN2
    BXCAN->FMR |= 1 << 8;                                       // All filters are available to CAN2
#else
    BXCAN->FMR |= CANARD_STM32_NUM_ACCEPTANCE_FILTERS << 8;     // All filters are available to CAN1
#endif

    BXCAN->FFA1R = 0;                           // All assigned to FIFO0 by default
    BXCAN->FM1R = 0;                            // Indentifier Mask mode

    BXCAN->FS1R = 0x0FFF;                       // All 32-bit
    BXCAN->FilterRegister[0].FR1 = 0;
    BXCAN->FilterRegister[0].FR2 = 0;
    BXCAN->FA1R = 1;                            // One filter enabled

    BXCAN->FMR &= ~CANARD_STM32_CAN_FMR_FINIT;

    /*
     * Poor man's unit test
     */
#define TEST_FRAME_ID_CONVERSION(x)  assert(convertFrameIDRegisterToCanard(convertFrameIDCanardToRegister(x)) == (x))

    TEST_FRAME_ID_CONVERSION(0                      | CANARD_CAN_FRAME_EFF | CANARD_CAN_FRAME_RTR);
    TEST_FRAME_ID_CONVERSION(CANARD_CAN_EXT_ID_MASK | CANARD_CAN_FRAME_EFF | CANARD_CAN_FRAME_RTR);
    TEST_FRAME_ID_CONVERSION(0                                             | CANARD_CAN_FRAME_RTR);
    TEST_FRAME_ID_CONVERSION(CANARD_CAN_STD_ID_MASK                        | CANARD_CAN_FRAME_RTR);
    TEST_FRAME_ID_CONVERSION(0                      | CANARD_CAN_FRAME_EFF);
    TEST_FRAME_ID_CONVERSION(CANARD_CAN_EXT_ID_MASK | CANARD_CAN_FRAME_EFF);
    TEST_FRAME_ID_CONVERSION(0);
    TEST_FRAME_ID_CONVERSION(CANARD_CAN_STD_ID_MASK);

#undef TEST_FRAME_ID_CONVERSION

    return 0;
}


int canardSTM32Transmit(const CanardCANFrame* frame)
{
    if (frame == NULL)
    {
        return -CANARD_ERROR_INVALID_ARGUMENT;
    }

    if (frame->id & CANARD_CAN_FRAME_ERR)
    {
        return -CANARD_STM32_ERROR_UNSUPPORTED_FRAME_FORMAT;
    }

    /*
     * Seeking an empty slot, checking if priority inversion would occur if we enqueued now.
     * We can always enqueue safely if all TX mailboxes are free and no transmissions are pending.
     */
    uint8_t tx_mailbox = 0xFF;

    static const uint32_t AllTME = CANARD_STM32_CAN_TSR_TME0 | CANARD_STM32_CAN_TSR_TME1 | CANARD_STM32_CAN_TSR_TME2;

    if ((BXCAN->TSR & AllTME) != AllTME)                // At least one TX mailbox is used, detailed check is needed
    {
        const bool tme[3] =
        {
            (BXCAN->TSR & CANARD_STM32_CAN_TSR_TME0) != 0,
            (BXCAN->TSR & CANARD_STM32_CAN_TSR_TME1) != 0,
            (BXCAN->TSR & CANARD_STM32_CAN_TSR_TME2) != 0
        };

        for (uint8_t i = 0; i < 3; i++)
        {
            if (tme[i])                                 // This TX mailbox is free, we can use it
            {
                tx_mailbox = i;
            }
            else                                        // This TX mailbox is pending, check for priority inversion
            {
                if (!isFramePriorityHigher(frame->id, convertFrameIDRegisterToCanard(BXCAN->TxMailbox[i].TIR)))
                {
                    // There's a mailbox whose priority is higher or equal the priority of the new frame.
                    return 0;                           // Priority inversion would occur! Reject transmission.
                }
            }
        }

        if (tx_mailbox == 0xFF)
        {
            /*
             * All TX mailboxes are busy (this is highly unlikely); at the same time we know that there is no
             * higher or equal priority frame that is currently pending. Therefore, priority inversion has
             * just happend (sic!), because we can't enqueue the higher priority frame due to all TX mailboxes
             * being busy. This scenario is extremely unlikely, because in order for it to happen, the application
             * would need to transmit 4 (four) or more CAN frames with different CAN ID ordered from high ID to
             * low ID nearly at the same time. For example:
             *  1. 0x123        <-- Takes mailbox 0 (or any other)
             *  2. 0x122        <-- Takes mailbox 2 (or any other)
             *  3. 0x121        <-- Takes mailbox 1 (or any other)
             *  4. 0x120        <-- INNER PRIORITY INVERSION HERE (only if all three TX mailboxes are still busy)
             * This situation is even less likely to cause any noticeable disruptions on the CAN bus. Despite that,
             * it is better to warn the developer about that during debugging, so we fire an assertion failure here.
             * It is perfectly safe to remove it.
             */
#if CANARD_STM32_DEBUG_INNER_PRIORITY_INVERSION
            assert(!"CAN PRIO INV");
#endif
            return 0;
        }
    }
    else                                                // All TX mailboxes are free, use first
    {
        tx_mailbox = 0;
    }

    assert(tx_mailbox < 3);                             // Index check - the value must be correct here

    /*
     * By this time we've proved that a priority inversion would not occur, and we've also found a free TX mailbox.
     * Therefore it is safe to enqueue the frame now.
     */
    volatile CanardSTM32TxMailboxType* const mb = &BXCAN->TxMailbox[tx_mailbox];

    mb->TDTR = frame->data_len;                         // DLC equals data length except in CAN FD

    mb->TDHR = (((uint32_t)frame->data[7]) << 24) |
               (((uint32_t)frame->data[6]) << 16) |
               (((uint32_t)frame->data[5]) <<  8) |
               (((uint32_t)frame->data[4]) <<  0);
    mb->TDLR = (((uint32_t)frame->data[3]) << 24) |
               (((uint32_t)frame->data[2]) << 16) |
               (((uint32_t)frame->data[1]) <<  8) |
               (((uint32_t)frame->data[0]) <<  0);

    mb->TIR = convertFrameIDCanardToRegister(frame->id) | CANARD_STM32_CAN_TIR_TXRQ;    // Go.

    /*
     * The frame is now enqueued and pending transmission.
     */
    return 1;
}


int canardSTM32Receive(CanardCANFrame* out_frame)
{
    if (out_frame == NULL)
    {
        return -CANARD_ERROR_INVALID_ARGUMENT;
    }

    static volatile uint32_t* const RFxR[2] =
    {
        &BXCAN->RF0R,
        &BXCAN->RF1R
    };

    /*
     * Aborting TX transmissions if abort on error was requested
     * Updating error counter
     */
    // TODO

    /*
     * Reading the TX FIFO
     */
    for (uint_fast8_t i = 0; i < 2; i++)
    {
        volatile CanardSTM32RxMailboxType* const mb = &BXCAN->RxMailbox[i];

        if (((*RFxR[i]) & CANARD_STM32_CAN_RFR_FMP_MASK) != 0)
        {
            if (*RFxR[i] & CANARD_STM32_CAN_RFR_FOVR)
            {
                g_stats.rx_overflow_count++;
            }

            out_frame->id = convertFrameIDRegisterToCanard(mb->RIR);

            out_frame->data_len = mb->RDTR & CANARD_STM32_CAN_RDTR_DLC_MASK;

            // Caching to regular (non volatile) memory for faster reads
            const uint32_t rdlr = mb->RDLR;
            const uint32_t rdhr = mb->RDHR;

            out_frame->data[0] = (uint8_t)(0xFF & (rdlr >>  0));
            out_frame->data[1] = (uint8_t)(0xFF & (rdlr >>  8));
            out_frame->data[2] = (uint8_t)(0xFF & (rdlr >> 16));
            out_frame->data[3] = (uint8_t)(0xFF & (rdlr >> 24));
            out_frame->data[4] = (uint8_t)(0xFF & (rdhr >>  0));
            out_frame->data[5] = (uint8_t)(0xFF & (rdhr >>  8));
            out_frame->data[6] = (uint8_t)(0xFF & (rdhr >> 16));
            out_frame->data[7] = (uint8_t)(0xFF & (rdhr >> 24));

            // Release FIFO entry we just read
            *RFxR[i] = CANARD_STM32_CAN_RFR_RFOM | CANARD_STM32_CAN_RFR_FOVR | CANARD_STM32_CAN_RFR_FULL;

            // Reading successful
            return 1;
        }
    }

    // No frames to read
    return 0;
}


int canardSTM32ConfigureAcceptanceFilters(const CanardSTM32AcceptanceFilterConfiguration* filter_configs,
                                          unsigned num_filter_configs)
{
    // TODO: IMPLEMENT THIS
    assert(0);
    (void) filter_configs;
    (void) num_filter_configs;
    return 0;
}


CanardSTM32Stats canardSTM32GetStats(void)
{
    return g_stats;
}
