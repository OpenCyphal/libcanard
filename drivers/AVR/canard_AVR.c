/*
 * Copyright (c) 2016 UAVCAN Team
 *
 * Distributed under the MIT License, available in the file LICENSE.
 *
 */

#include <canard_AVR.h>
#include <string.h>

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <can.h>

int canardAVRInit(uint32_t id)
{
    can_init(BITRATE_500_KBPS);
    can_set_mode(NORMAL_MODE);

    // todo: filter messages (let only messages for this id pass)
    //       by hardware filter
    // create a new filter for receiving all messages
    can_filter_t filter = {
        .id = 0,
        .mask = 0,
        .flags = {
            .rtr = 0,
            .extended = 0
        }
    };

    can_set_filter(0, &filter);

    // enable interrupts
    sei();

    return 0;
}

int canardAVRClose(void)
{
    return 0;
}

int canardAVRTransmit(const CanardCANFrame* frame)
{
    const int poll_result = can_check_free_buffer();
    if (poll_result <= 0)
    {
        return 0;
    }

    can_t transmit_msg;
    memset(&transmit_msg, 0, sizeof(transmit_msg));
    transmit_msg.id = frame->id & CANARD_CAN_EXT_ID_MASK;
    transmit_msg.length = frame->data_len;
    transmit_msg.flags.extended = (frame->id & CANARD_CAN_FRAME_EFF) != 0;
    memcpy(transmit_msg.data, frame->data, frame->data_len);

    const uint8_t res = can_send_message(&transmit_msg);
    if (res <= 0)
    {
        return -1;
    }

    return 1;
}

int canardAVRReceive(CanardCANFrame* out_frame)
{
    const int poll_result = can_check_message();
    if (poll_result <= 0)
    {
        return 0;
    }

    can_t receive_msg;
    const uint8_t res = can_get_message(&receive_msg);
    if (res <= 0)
    {
        return -1;
    }

    out_frame->id = receive_msg.id;
    out_frame->data_len = receive_msg.length;
    memcpy(out_frame->data, receive_msg.data, receive_msg.length);

    if (receive_msg.flags.extended != 0)
    {
        out_frame->id |= CANARD_CAN_FRAME_EFF;
    }

    return 1;
}
