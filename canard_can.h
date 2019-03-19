/*
 * Copyright (c) 2016-2018 UAVCAN Team
 *
 * Distributed under the MIT License, available in the file LICENSE.
 *
 */

#ifndef CANARD_CAN_H
#define CANARD_CAN_H

#ifdef __cplusplus
extern "C"
{
#endif

#define CANARD_CAN_FRAME_MAX_DATA_LEN               8U

/**
 * This data type holds a standard CAN 2.0B data frame with 29-bit ID.
 */
typedef struct
{
    /**
     * Refer to the following definitions:
     *  - CANARD_CAN_FRAME_EFF
     *  - CANARD_CAN_FRAME_RTR
     *  - CANARD_CAN_FRAME_ERR
     */
    uint32_t id;
    uint8_t data_len;
    
    /* The size of data depends on the transport frame in use */
    uint8_t data[CANARD_CAN_FRAME_MAX_DATA_LEN];
} CanardCANFrame;

/**
 * Returns a pointer to the top priority frame in the TX queue.
 * Returns NULL if the TX queue is empty.
 * The application will call this function after canardBroadcast() or canardRequestOrRespond() to transmit generated
 * frames over the CAN bus.
 */
int16_t canardCANPeekTxQueue(const CanardInstance* ins, 
                             CanardCANFrame** frame);

/**
 * Processes a received CAN frame with a timestamp.
 * The application will call this function when it receives a new frame from the CAN bus.
 *
 * Return value will report any errors in decoding packets.
 */
int16_t canardCANHandleRxFrame(CanardInstance* ins,
                               const CanardCANFrame* frame,
                               uint64_t timestamp_usec);


#ifdef __cplusplus
}
#endif

#endif
