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
 *
 * Documentation: http://uavcan.org/Implementations/Libcanard
 */

#include "canard_internals.h"
#include <string.h>
#include <assert.h>


struct CanardTxQueueItem
{
    CanardTxQueueItem* next;
    CanardCANFrame frame;
};

/*
 * API functions
 */
void canardInit(CanardInstance* out_ins,
                void* mem_arena,
                size_t mem_arena_size,
                CanardOnTransferReception on_reception,
                CanardShouldAcceptTransfer should_accept)
{
    /*
     * Checking memory layout.
     * This condition is supposed to be true for all 32-bit and smaller platforms.
     * If your application fails here, make sure it's not built in 64-bit mode.
     * Refer to the design documentation for more info.
     */
    assert(CANARD_RX_PAYLOAD_HEAD_SIZE >= 6);

    memset(out_ins, 0, sizeof(*out_ins));

    out_ins->node_id = CANARD_BROADCAST_NODE_ID;
    out_ins->on_reception = on_reception;
    out_ins->should_accept = should_accept;
    out_ins->rx_states = NULL;
    out_ins->tx_queue = NULL;

    size_t pool_capacity = mem_arena_size / CANARD_MEM_BLOCK_SIZE;
    if (pool_capacity > 0xFFFFU)
    {
        pool_capacity = 0xFFFFU;
    }

    initPoolAllocator(&out_ins->allocator, mem_arena, (uint16_t)pool_capacity);
}

void canardSetLocalNodeID(CanardInstance* ins, uint8_t self_node_id)
{
    ins->node_id = self_node_id;
}

uint8_t canardGetLocalNodeID(const CanardInstance* ins)
{
    return ins->node_id;
}

int canardBroadcast(CanardInstance* ins,
                    uint64_t data_type_signature,
                    uint16_t data_type_id,
                    uint8_t* inout_transfer_id,
                    uint8_t priority,
                    const void* payload,
                    uint16_t payload_len)
{
    if (payload == NULL)
    {
        return -CANARD_ERROR_INVALID_ARGUMENT;
    }
    if (priority > CANARD_TRANSFER_PRIORITY_LOWEST)
    {
        return -CANARD_ERROR_INVALID_ARGUMENT;
    }

    uint32_t can_id = 0;
    uint16_t crc = 0xFFFFU;

    if (canardGetLocalNodeID(ins) == 0)
    {
        if (payload_len > 7)
        {
            return -CANARD_ERROR_NODE_ID_NOT_SET;
        }
        else
        {
            // anonymous transfer, random discriminator
            const uint16_t discriminator = (uint16_t)((crcAdd(0xFFFFU, payload, payload_len)) & 0x7FFEU);
            can_id = ((uint32_t) priority << 24) | ((uint32_t) discriminator << 9) |
                     ((uint32_t) (data_type_id & 0xFF) << 8) | (uint32_t) canardGetLocalNodeID(ins);
        }
    }
    else
    {
        can_id = ((uint32_t) priority << 24) | ((uint32_t) data_type_id << 8) | (uint32_t) canardGetLocalNodeID(ins);

        if (payload_len > 7)
        {
            crc = crcAddSignature(crc, data_type_signature);
            crc = crcAdd(crc, payload, payload_len);
        }
    }

    const int result = enqueueTxFrames(ins, can_id, inout_transfer_id, crc, payload, payload_len);

    incrementTransferID(inout_transfer_id);

    return result;
}

int canardRequestOrRespond(CanardInstance* ins,
                           uint8_t destination_node_id,
                           uint64_t data_type_signature,
                           uint8_t data_type_id,
                           uint8_t* inout_transfer_id,
                           uint8_t priority,
                           CanardRequestResponse kind,
                           const void* payload,
                           uint16_t payload_len)
{
    if (payload == NULL)
    {
        return -CANARD_ERROR_INVALID_ARGUMENT;
    }
    if (priority > 31)
    {
        return -CANARD_ERROR_INVALID_ARGUMENT;
    }
    if (canardGetLocalNodeID(ins) == 0)
    {
        return -CANARD_ERROR_NODE_ID_NOT_SET;
    }

    const uint32_t can_id = ((uint32_t) priority << 24) | ((uint32_t) data_type_id << 16) |
                            ((uint32_t) kind << 15) | ((uint32_t) destination_node_id << 8) |
                            (1 << 7) | (uint32_t) canardGetLocalNodeID(ins);
    uint16_t crc = 0xFFFFU;

    if (payload_len > 7)
    {
        crc = crcAddSignature(crc, data_type_signature);
        crc = crcAdd(crc, payload, payload_len);
    }

    const int result = enqueueTxFrames(ins, can_id, inout_transfer_id, crc, payload, payload_len);

    incrementTransferID(inout_transfer_id);

    return result;
}

const CanardCANFrame* canardPeekTxQueue(const CanardInstance* ins)
{
    if (ins->tx_queue == NULL)
    {
        return NULL;
    }
    return &ins->tx_queue->frame;
}

void canardPopTxQueue(CanardInstance* ins)
{
    CanardTxQueueItem* item = ins->tx_queue;
    ins->tx_queue = item->next;
    freeBlock(&ins->allocator, item);
}

void canardHandleRxFrame(CanardInstance* ins, const CanardCANFrame* frame, uint64_t timestamp_usec)
{
    const CanardTransferType transfer_type = extractTransferType(frame->id);
    const uint8_t destination_node_id =
            (transfer_type == CanardTransferTypeBroadcast) ? ((uint8_t)0) : DEST_ID_FROM_ID(frame->id);

    // TODO: This function should maintain statistics of transfer errors and such.

    if ((frame->id & CANARD_CAN_FRAME_EFF) == 0 ||
        (frame->id & CANARD_CAN_FRAME_RTR) != 0 ||
        (frame->id & CANARD_CAN_FRAME_ERR) != 0 ||
        (frame->data_len < 1))
    {
        return;     // Unsupported frame, not UAVCAN - ignore
    }

    if (transfer_type != CanardTransferTypeBroadcast &&
        destination_node_id != canardGetLocalNodeID(ins))
    {
        return;     // Address mismatch
    }

    const uint8_t priority = PRIORITY_FROM_ID(frame->id);
    const uint8_t source_node_id = SOURCE_ID_FROM_ID(frame->id);
    const uint16_t data_type_id = extractDataType(frame->id);
    const uint32_t transfer_descriptor =
            MAKE_TRANSFER_DESCRIPTOR(data_type_id, transfer_type, source_node_id, destination_node_id);

    const uint8_t tail_byte = frame->data[frame->data_len - 1];

    CanardRxState* rx_state = NULL;

    if (IS_START_OF_TRANSFER(tail_byte))
    {
        uint64_t data_type_signature = 0;

        if (ins->should_accept(ins, &data_type_signature, data_type_id, transfer_type, source_node_id))
        {
            rx_state = traverseRxStates(ins, transfer_descriptor);
            rx_state->calculated_crc = crcAddSignature(0xFFFFU, data_type_signature);
        }
    }
    else
    {
        rx_state = findRxState(ins->rx_states, transfer_descriptor);
    }

    if (rx_state == NULL)
    {
        return;
    }

    // Resolving the state flags:
    const bool not_initialized = rx_state->timestamp_usec == 0;
    const bool tid_timed_out = (timestamp_usec - rx_state->timestamp_usec) > TRANSFER_TIMEOUT_USEC;
    const bool first_frame = IS_START_OF_TRANSFER(tail_byte);
    const bool not_previous_tid =
        computeTransferIDForwardDistance((uint8_t) rx_state->transfer_id, TRANSFER_ID_FROM_TAIL_BYTE(tail_byte)) > 1;

    const bool need_restart =
            (not_initialized) ||
            (tid_timed_out) ||
            (first_frame && not_previous_tid);

    if (need_restart)
    {
        rx_state->transfer_id = TRANSFER_ID_FROM_TAIL_BYTE(tail_byte);
        rx_state->next_toggle = 0;
        releaseStatePayload(ins, rx_state);
        if (!IS_START_OF_TRANSFER(tail_byte)) // missed the first frame
        {
            rx_state->transfer_id += 1;
            return;
        }
    }

    if (IS_START_OF_TRANSFER(tail_byte) && IS_END_OF_TRANSFER(tail_byte)) // single frame transfer
    {
        rx_state->timestamp_usec = timestamp_usec;
        CanardRxTransfer rx_transfer = {
            .timestamp_usec = timestamp_usec,
            .payload_head = frame->data,
            .payload_len = (uint8_t)(frame->data_len - 1U),
            .data_type_id = data_type_id,
            .transfer_type = transfer_type,
            .transfer_id = TRANSFER_ID_FROM_TAIL_BYTE(tail_byte),
            .priority = priority,
            .source_node_id = source_node_id
        };

        ins->on_reception(ins, &rx_transfer);

        prepareForNextTransfer(rx_state);
        return;
    }

    if (TOGGLE_BIT(tail_byte) != rx_state->next_toggle)
    {
        return; // wrong toggle
    }

    if (TRANSFER_ID_FROM_TAIL_BYTE(tail_byte) != rx_state->transfer_id)
    {
        return; // unexpected tid
    }

    if (IS_START_OF_TRANSFER(tail_byte) && !IS_END_OF_TRANSFER(tail_byte))      // Beginning of multi frame transfer
    {
        if (frame->data_len <= 3)
        {
            return;     // Not enough data
        }

        // take off the crc and store the payload
        rx_state->timestamp_usec = timestamp_usec;
        const int ret = bufferBlockPushBytes(&ins->allocator, rx_state, frame->data + 2,
                                             (uint8_t) (frame->data_len - 3));
        if (ret < 0)
        {
            return;
        }
        rx_state->payload_crc = ((uint16_t) frame->data[0]) | ((uint16_t) frame->data[1] << 8);
        rx_state->calculated_crc = crcAdd(rx_state->calculated_crc, frame->data + 2, (uint8_t)(frame->data_len - 3));
    }
    else if (!IS_START_OF_TRANSFER(tail_byte) && !IS_END_OF_TRANSFER(tail_byte))    // Middle of a multi-frame transfer
    {
        const int ret = bufferBlockPushBytes(&ins->allocator, rx_state, frame->data,
                                             (uint8_t) (frame->data_len - 1));
        if (ret < 0)
        {
            return;
        }
        rx_state->calculated_crc = crcAdd(rx_state->calculated_crc, frame->data, (uint8_t)(frame->data_len - 1));
    }
    else                                                                            // End of a multi-frame transfer
    {
        uint8_t tail_offset = 0;
        uint16_t middle_len = 0;
        if (rx_state->payload_len < CANARD_RX_PAYLOAD_HEAD_SIZE)
        {
            uint16_t i = 0;
            for (i = rx_state->payload_len, tail_offset = 0;
                 i < CANARD_RX_PAYLOAD_HEAD_SIZE && tail_offset < frame->data_len - 1;
                 i++, tail_offset++)
            {
                rx_state->buffer_head[i] = frame->data[tail_offset];
            }
        }

        // for transfers without buffer_blocks
        if (rx_state->buffer_blocks != NULL)
        {
            middle_len = rx_state->payload_len - CANARD_RX_PAYLOAD_HEAD_SIZE;
        }

        CanardRxTransfer rx_transfer = {
            .timestamp_usec = timestamp_usec,
            .payload_head = rx_state->buffer_head,
            .payload_middle = rx_state->buffer_blocks,
            .payload_tail = frame->data + tail_offset,
            .middle_len = middle_len,
            .payload_len = (uint16_t)(rx_state->payload_len + frame->data_len - 1),
            .data_type_id = data_type_id,
            .transfer_type = transfer_type,
            .transfer_id = TRANSFER_ID_FROM_TAIL_BYTE(tail_byte),
            .priority = priority,
            .source_node_id = source_node_id
        };

        // CRC validation
        rx_state->calculated_crc = crcAdd(rx_state->calculated_crc, frame->data, frame->data_len - 1U);
        if (rx_state->calculated_crc == rx_state->payload_crc)
        {
            ins->on_reception(ins, &rx_transfer);
        }

        // Making sure the payload is released even if the application didn't bother with it
        canardReleaseRxTransferPayload(ins, &rx_transfer);
        prepareForNextTransfer(rx_state);
        return;
    }

    rx_state->next_toggle ^= 1;
}

void canardCleanupStaleTransfers(CanardInstance* ins, uint64_t current_time_usec)
{
    CanardRxState* prev = ins->rx_states, * state = ins->rx_states;

    while (state != NULL)
    {
        if ((current_time_usec - state->timestamp_usec) > TRANSFER_TIMEOUT_USEC)
        {
            if (state == ins->rx_states)
            {
                releaseStatePayload(ins, state);
                ins->rx_states = ins->rx_states->next;
                freeBlock(&ins->allocator, state);
                state = ins->rx_states;
                prev = state;
            }
            else
            {
                releaseStatePayload(ins, state);
                prev->next = state->next;
                freeBlock(&ins->allocator, state);
                state = prev->next;
            }
        }
        else
        {
            prev = state;
            state = state->next;
        }
    }
}

uint64_t canardReadRxTransferPayload(const CanardRxTransfer* transfer,
                                     uint16_t bit_offset,
                                     uint8_t bit_length)
{
    uint64_t bits = 0;
    int shift_val = bit_length - 8;

    if (transfer->payload_len > 7)      // Multi frame
    {
        CanardBufferBlock* block = transfer->payload_middle;
        uint16_t index = 0;

        // Head
        for (uint16_t i = 0; i < CANARD_RX_PAYLOAD_HEAD_SIZE && shift_val >= 0; i++, index++)
        {
            if (index >= bit_offset / 8)
            {
                bits |= ((uint64_t) transfer->payload_head[i] << shift_val);
                shift_val -= 8;
            }
        }

        // Middle (buffer blocks)
        for (uint16_t i = 0; index < (CANARD_RX_PAYLOAD_HEAD_SIZE + transfer->middle_len) && shift_val >= 0; index++)
        {
            if (index >= bit_offset / 8)
            {
                bits |= ((uint64_t) block->data[i] << shift_val);
                shift_val -= 8;
            }
            i++;
            if (i >= CANARD_BUFFER_BLOCK_DATA_SIZE)
            {
                i = 0;
                block = block->next;
            }
        }

        // Tail
        const int tail_len = transfer->payload_len - (CANARD_RX_PAYLOAD_HEAD_SIZE + transfer->middle_len);
        for (int i = 0; i < (tail_len) && shift_val >= 0; i++, index++)
        {
            if (index >= bit_offset / 8)
            {
                bits |= ((uint64_t) transfer->payload_tail[i] << shift_val);
                shift_val -= 8;
            }
        }
    }
    else    // Single frame
    {
        for (uint8_t i = 0; i < transfer->payload_len && shift_val >= 0; i++)
        {
            if (i >= bit_offset / 8)
            {
                bits |= ((uint64_t) transfer->payload_head[i] << shift_val);
                shift_val -= 8;
            }
        }
    }

    return bits;
}

void canardReleaseRxTransferPayload(CanardInstance* ins, CanardRxTransfer* transfer)
{
    CanardBufferBlock* temp = NULL;
    while (transfer->payload_middle != NULL)
    {
        temp = transfer->payload_middle->next;
        freeBlock(&ins->allocator, transfer->payload_middle);
        transfer->payload_middle = temp;
    }

    transfer->payload_middle = NULL;
    transfer->payload_head = NULL;
    transfer->payload_tail = NULL;
    transfer->payload_len = 0;
}

CanardPoolAllocatorStatistics canardGetPoolAllocatorStatistics(CanardInstance* ins)
{
    return ins->allocator.statistics;
}

uint16_t canardConvertNativeFloatToFloat16(float value)
{
    assert(sizeof(float) == 4);

    union FP32
    {
        uint32_t u;
        float f;
    };

    const union FP32 f32inf = { 255U << 23 };
    const union FP32 f16inf = { 31U << 23 };
    const union FP32 magic = { 15U << 23 };
    const uint32_t sign_mask = 0x80000000U;
    const uint32_t round_mask = ~0xFFFU;

    union FP32 in;
    in.f = value;
    uint32_t sign = in.u & sign_mask;
    in.u ^= sign;

    uint16_t out = 0;

    if (in.u >= f32inf.u)
    {
        out = (in.u > f32inf.u) ? (uint16_t)0x7FFFU : (uint16_t)0x7C00U;
    }
    else
    {
        in.u &= round_mask;
        in.f *= magic.f;
        in.u -= round_mask;
        if (in.u > f16inf.u)
        {
            in.u = f16inf.u;
        }
        out = (uint16_t)(in.u >> 13);
    }

    out |= (uint16_t)(sign >> 16);

    return out;
}

float canardConvertFloat16ToNativeFloat(uint16_t value)
{
    assert(sizeof(float) == 4);

    union FP32
    {
        uint32_t u;
        float f;
    };

    const union FP32 magic = { (254U - 15U) << 23 };
    const union FP32 was_inf_nan = { (127U + 16U) << 23 };
    union FP32 out;

    out.u = (value & 0x7FFFU) << 13;
    out.f *= magic.f;
    if (out.f >= was_inf_nan.f)
    {
        out.u |= 255U << 23;
    }
    out.u |= (value & 0x8000U) << 16;

    return out.f;
}

/*
 * Internal (static functions)
 */
CANARD_INTERNAL int computeTransferIDForwardDistance(uint8_t a, uint8_t b)
{
    int d = b - a;
    if (d < 0)
    {
        d += 1 << TRANSFER_ID_BIT_LEN;
    }
    return d;
}

CANARD_INTERNAL void incrementTransferID(uint8_t* transfer_id)
{
    assert(transfer_id != NULL);

    *transfer_id += 1;
    if (*transfer_id >= 32)
    {
        *transfer_id = 0;
    }
}

CANARD_INTERNAL int enqueueTxFrames(CanardInstance* ins,
                                    uint32_t can_id,
                                    uint8_t* transfer_id,
                                    uint16_t crc,
                                    const uint8_t* payload,
                                    uint16_t payload_len)
{
    assert(ins != NULL);
    assert(transfer_id != NULL);
    assert((payload_len > 0) ? (payload != NULL) : true);
    assert((can_id & CANARD_CAN_EXT_ID_MASK) == can_id);            // Flags must be cleared

    int result = 0;

    if (payload_len < CANARD_CAN_FRAME_MAX_DATA_LEN)                        // Single frame transfer
    {
        CanardTxQueueItem* queue_item = createTxItem(&ins->allocator);
        if (queue_item == NULL)
        {
            return -CANARD_ERROR_OUT_OF_MEMORY;
        }

        memcpy(queue_item->frame.data, payload, payload_len);

        queue_item->frame.data_len = (uint8_t)(payload_len + 1);
        queue_item->frame.data[payload_len] = (uint8_t)(0xC0 | (*transfer_id & 31));
        queue_item->frame.id = can_id | CANARD_CAN_FRAME_EFF;

        pushTxQueue(ins, queue_item);
        result++;
    }
    else if (payload_len >= CANARD_CAN_FRAME_MAX_DATA_LEN)                  // Multi frame transfer
    {
        uint8_t data_index = 0;
        uint8_t toggle = 0;
        uint8_t sot_eot = 0x80;

        CanardTxQueueItem* queue_item = NULL;

        while (payload_len - data_index != 0)
        {
            queue_item = createTxItem(&ins->allocator);
            if (queue_item == NULL)
            {
                return -CANARD_ERROR_OUT_OF_MEMORY;          // TODO: Purge all frames enqueued so far
            }

            uint8_t i = 0;
            if (data_index == 0)
            {
                // add crc
                queue_item->frame.data[0] = (uint8_t) (crc);
                queue_item->frame.data[1] = (uint8_t) (crc >> 8);
                i = 2;
            }
            else
            {
                i = 0;
            }

            for (; i < (CANARD_CAN_FRAME_MAX_DATA_LEN - 1) && data_index < payload_len; i++, data_index++)
            {
                queue_item->frame.data[i] = payload[data_index];
            }
            // tail byte
            sot_eot = (data_index == payload_len) ? (uint8_t)0x40 : sot_eot;

            queue_item->frame.data[i] = (uint8_t)(sot_eot | (toggle << 5) | (*transfer_id & 31));
            queue_item->frame.id = can_id | CANARD_CAN_FRAME_EFF;
            queue_item->frame.data_len = (uint8_t)(i + 1);
            pushTxQueue(ins, queue_item);

            result++;
            toggle ^= 1;
            sot_eot = 0;
        }
    }

    return result;
}

/**
 * Puts frame on on the TX queue. Higher priority placed first
 */
CANARD_INTERNAL void pushTxQueue(CanardInstance* ins, CanardTxQueueItem* item)
{
    assert(ins != NULL);
    assert(item->frame.data_len > 0);       // UAVCAN doesn't allow zero-payload frames

    if (ins->tx_queue == NULL)
    {
        ins->tx_queue = item;
        return;
    }

    CanardTxQueueItem* queue = ins->tx_queue;
    CanardTxQueueItem* previous = ins->tx_queue;

    while (queue != NULL)
    {
        if (isPriorityHigher(queue->frame.id, item->frame.id)) // lower number wins
        {
            if (queue == ins->tx_queue)
            {
                item->next = queue;
                ins->tx_queue = item;
            }
            else
            {
                previous->next = item;
                item->next = queue;
            }
            return;
        }
        else
        {
            if (queue->next == NULL)
            {
                queue->next = item;
                return;
            }
            else
            {
                previous = queue;
                queue = queue->next;
            }
        }
    }
}

/**
 * Creates new tx queue item from allocator
 */
CANARD_INTERNAL CanardTxQueueItem* createTxItem(CanardPoolAllocator* allocator)
{
    CanardTxQueueItem* item = (CanardTxQueueItem*) allocateBlock(allocator);
    if (item == NULL)
    {
        return NULL;
    }
    memset(item, 0, sizeof(*item));
    return item;
}

/**
 * Returns true if priority of rhs is higher than id
 */
CANARD_INTERNAL bool isPriorityHigher(uint32_t rhs, uint32_t id)
{
    const uint32_t clean_id = id & CANARD_CAN_EXT_ID_MASK;
    const uint32_t rhs_clean_id = rhs & CANARD_CAN_EXT_ID_MASK;

    /*
     * STD vs EXT - if 11 most significant bits are the same, EXT loses.
     */
    const bool ext = (id & CANARD_CAN_FRAME_EFF) != 0;
    const bool rhs_ext = (rhs & CANARD_CAN_FRAME_EFF) != 0;
    if (ext != rhs_ext)
    {
        uint32_t arb11 = ext ? (clean_id >> 18) : clean_id;
        uint32_t rhs_arb11 = rhs_ext ? (rhs_clean_id >> 18) : rhs_clean_id;
        if (arb11 != rhs_arb11)
        {
            return arb11 < rhs_arb11;
        }
        else
        {
            return rhs_ext;
        }
    }

    /*
     * RTR vs Data frame - if frame identifiers and frame types are the same, RTR loses.
     */
    const bool rtr = (id & CANARD_CAN_FRAME_RTR) != 0;
    const bool rhs_rtr = (rhs & CANARD_CAN_FRAME_RTR) != 0;
    if (clean_id == rhs_clean_id && rtr != rhs_rtr)
    {
        return rhs_rtr;
    }

    /*
     * Plain ID arbitration - greater value loses.
     */
    return clean_id < rhs_clean_id;
}

/**
 * preps the rx state for the next transfer. does not delete the state
 */
CANARD_INTERNAL void prepareForNextTransfer(CanardRxState* state)
{
    state->buffer_blocks = NULL; // payload should be empty anyway
    state->transfer_id += 1;
    state->payload_len = 0;
    state->next_toggle = 0;
}

/**
 * returns data type from id
 */
CANARD_INTERNAL uint16_t extractDataType(uint32_t id)
{
    if (extractTransferType(id) == CanardTransferTypeBroadcast)
    {
        return (uint16_t) MSG_TYPE_FROM_ID(id);
    }
    else
    {
        return (uint16_t) SRV_TYPE_FROM_ID(id);
    }
}

/**
 * returns transfer type from id
 */
CANARD_INTERNAL CanardTransferType extractTransferType(uint32_t id)
{
    const bool is_service = SERVICE_NOT_MSG_FROM_ID(id);
    if (!is_service)
    {
        return CanardTransferTypeBroadcast;
    }
    else if (REQUEST_NOT_RESPONSE_FROM_ID(id) == 1)
    {
        return CanardTransferTypeRequest;
    }
    else
    {
        return CanardTransferTypeResponse;
    }
}

/*
 *  CanardRxState functions
 */

/**
 * Traverses the list of CanardRxState's and returns a pointer to the CanardRxState
 * with either the Id or a new one at the end
 */
CANARD_INTERNAL CanardRxState* traverseRxStates(CanardInstance* ins, uint32_t transfer_descriptor)
{
    CanardRxState* states = ins->rx_states;

    if (states == NULL) // initialize CanardRxStates
    {
        states = createRxState(&ins->allocator, transfer_descriptor);
        ins->rx_states = states;
        return states;
    }

    states = findRxState(states, transfer_descriptor);
    if (states != NULL)
    {
        return states;
    }
    else
    {
        return prependRxState(ins, transfer_descriptor);
    }
}

/**
 * returns pointer to the rx state of transfer descriptor or null if not found
 */
CANARD_INTERNAL CanardRxState* findRxState(CanardRxState* state, uint32_t transfer_descriptor)
{
    while (state != NULL)
    {
        if (state->dtid_tt_snid_dnid == transfer_descriptor)
        {
            return state;
        }
        state = state->next;
    }
    return NULL;
}

/**
 * prepends rx state to the canard instance rx_states
 */
CANARD_INTERNAL CanardRxState* prependRxState(CanardInstance* ins, uint32_t transfer_descriptor)
{
    CanardRxState* state = createRxState(&ins->allocator, transfer_descriptor);
    state->next = ins->rx_states;
    ins->rx_states = state;
    return state;
}

CANARD_INTERNAL CanardRxState* createRxState(CanardPoolAllocator* allocator, uint32_t transfer_descriptor)
{
    CanardRxState init = {
        .next = NULL,
        .buffer_blocks = NULL,
        .dtid_tt_snid_dnid = transfer_descriptor
    };

    CanardRxState* state = (CanardRxState*) allocateBlock(allocator);
    if (state == NULL)
    {
        return NULL;
    }
    memcpy(state, &init, sizeof(*state));

    return state;
}

/**
 * This function can be invoked by the application to release pool blocks that are used
 * to store the payload of this transfer
 */
CANARD_INTERNAL uint64_t releaseStatePayload(CanardInstance* ins, CanardRxState* rxstate)
{
    CanardBufferBlock* temp = NULL;
    while (rxstate->buffer_blocks != NULL)
    {
        temp = rxstate->buffer_blocks->next;
        freeBlock(&ins->allocator, rxstate->buffer_blocks);
        rxstate->buffer_blocks = temp;
    }
    rxstate->payload_len = 0;
    return CANARD_OK;
}

/*
 *  CanardBufferBlock functions
 */

/**
 * pushes data into the rx state. Fills the buffer head, then appends data to buffer blocks
 */
CANARD_INTERNAL int bufferBlockPushBytes(CanardPoolAllocator* allocator,
                                         CanardRxState* state,
                                         const uint8_t* data,
                                         uint8_t data_len)
{
    uint16_t data_index = 0;

    // if head is not full, add data to head
    if ((int) CANARD_RX_PAYLOAD_HEAD_SIZE - (int) state->payload_len > 0)
    {
        for (uint16_t i = state->payload_len;
             i < CANARD_RX_PAYLOAD_HEAD_SIZE && data_index < data_len;
             i++, data_index++)
        {
            state->buffer_head[i] = data[data_index];
        }
        if (data_index >= data_len)
        {
            state->payload_len += data_len;
            return 1;
        }
    } // head is full.

    uint8_t index_at_nth_block = (((state->payload_len) - CANARD_RX_PAYLOAD_HEAD_SIZE) % CANARD_BUFFER_BLOCK_DATA_SIZE);

    // get to current block
    CanardBufferBlock* block = NULL;

    // buffer blocks uninitialized
    if (state->buffer_blocks == NULL)
    {
        state->buffer_blocks = createBufferBlock(allocator);
        block = state->buffer_blocks;
        index_at_nth_block = 0;
    }
    else
    {
        uint8_t nth_block = 1;

        // get to block
        block = state->buffer_blocks;
        while (block->next != NULL)
        {
            nth_block++;
            block = block->next;
        }

        const uint8_t num_buffer_blocks =
            (((state->payload_len + data_len) - CANARD_RX_PAYLOAD_HEAD_SIZE) / CANARD_BUFFER_BLOCK_DATA_SIZE) + 1;

        if (num_buffer_blocks > nth_block && index_at_nth_block == 0)
        {
            block->next = createBufferBlock(allocator);
            if (block->next == NULL)
            {
                return -CANARD_ERROR_OUT_OF_MEMORY;
            }
            block = block->next;
        }
    }

    // add data to current block until it becomes full, add new block if necessary
    while (data_index < data_len)
    {
        for (uint16_t i = index_at_nth_block;
             i < CANARD_BUFFER_BLOCK_DATA_SIZE && data_index < data_len;
             i++, data_index++)
        {
            block->data[i] = data[data_index];
        }

        if (data_index < data_len)
        {
            block->next = createBufferBlock(allocator);
            if (block->next == NULL)
            {
                return -CANARD_ERROR_OUT_OF_MEMORY;
            }
            block = block->next;
            index_at_nth_block = 0;
        }
    }

    state->payload_len += data_len;

    return 1;
}

/**
 * creates new buffer block
 */
CANARD_INTERNAL CanardBufferBlock* createBufferBlock(CanardPoolAllocator* allocator)
{
    CanardBufferBlock* block = (CanardBufferBlock*) allocateBlock(allocator);
    if (block == NULL)
    {
        return NULL;
    }
    block->next = NULL;
    return block;
}

/*
 * CRC functions
 */
CANARD_INTERNAL uint16_t crcAddByte(uint16_t crc_val, uint8_t byte)
{
    crc_val ^= (uint16_t) ((uint16_t) (byte) << 8);
    for (int j = 0; j < 8; j++)
    {
        if (crc_val & 0x8000U)
        {
            crc_val = (uint16_t) ((uint16_t) (crc_val << 1) ^ 0x1021U);
        }
        else
        {
            crc_val = (uint16_t) (crc_val << 1);
        }
    }
    return crc_val;
}

CANARD_INTERNAL uint16_t crcAddSignature(uint16_t crc_val, uint64_t data_type_signature)
{
    for (int shift_val = 0; shift_val < 64; shift_val += 8)
    {
        crc_val = crcAddByte(crc_val, (uint8_t) (data_type_signature >> shift_val));
    }
    return crc_val;
}

CANARD_INTERNAL uint16_t crcAdd(uint16_t crc_val, const uint8_t* bytes, size_t len)
{
    while (len--)
    {
        crc_val = crcAddByte(crc_val, *bytes++);
    }
    return crc_val;
}

/*
 *  Pool Allocator functions
 */
CANARD_INTERNAL void initPoolAllocator(CanardPoolAllocator* allocator,
                                       CanardPoolAllocatorBlock* buf,
                                       uint16_t buf_len)
{
    size_t current_index = 0;
    CanardPoolAllocatorBlock** current_block = &(allocator->free_list);
    while (current_index < buf_len)
    {
        *current_block = &buf[current_index];
        current_block = &((*current_block)->next);
        current_index++;
    }
    *current_block = NULL;

    allocator->statistics.capacity_blocks = buf_len;
    allocator->statistics.current_usage_blocks = 0;
    allocator->statistics.peak_usage_blocks = 0;
}

CANARD_INTERNAL void* allocateBlock(CanardPoolAllocator* allocator)
{
    // Check if there are any blocks available in the free list.
    if (allocator->free_list == NULL)
    {
        return NULL;
    }

    // Take first available block and prepares next block for use.
    void* result = allocator->free_list;
    allocator->free_list = allocator->free_list->next;

    // Update statistics
    allocator->statistics.current_usage_blocks++;
    if (allocator->statistics.peak_usage_blocks < allocator->statistics.current_usage_blocks)
    {
        allocator->statistics.peak_usage_blocks = allocator->statistics.current_usage_blocks;
    }

    return result;
}

CANARD_INTERNAL void freeBlock(CanardPoolAllocator* allocator, void* p)
{
    CanardPoolAllocatorBlock* block = (CanardPoolAllocatorBlock*) p;

    block->next = allocator->free_list;
    allocator->free_list = block;

    assert(allocator->statistics.current_usage_blocks > 0);
    allocator->statistics.current_usage_blocks--;
}
