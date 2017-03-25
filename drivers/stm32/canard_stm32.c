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


#if !defined(CANARD_STM32_USE_CAN2)
# define CANARD_STM32_USE_CAN2                  0
#endif

#if CANARD_STM32_USE_CAN2
# define BXCAN          CANARD_STM32_CAN2
#else
# define BXCAN          CANARD_STM32_CAN1
#endif


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
    BXCAN->FA1R = 1;

    BXCAN->FMR &= ~CANARD_STM32_CAN_FMR_FINIT;

    return 0;
}


int canardSTM32Transmit(const CanardCANFrame* frame)
{
}


int canardSTM32Receive(CanardCANFrame* out_frame)
{
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
