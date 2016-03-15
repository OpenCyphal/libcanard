/*
 * Copyright (c) 2016 UAVCAN Team
 *
 * Distributed under the MIT License, available in the file LICENSE.
 *
 * Author: Michael Sierra <sierramichael.a@gmail.com>
 *
 */

#include "canard.h"
#include "canard_internals.h"
#include <inttypes.h>


#define CANARD_MAKE_TRANSFER_DESCRIPTOR(data_type_id, transfer_type, \
                                        src_node_id, dst_node_id) \
    ((data_type_id) | ((transfer_type) << 16) | \
     ((src_node_id) << 18) | ((dst_node_id) << 25))

struct CanardTxQueueItem
{
    CanardTxQueueItem* next;
    CanardCANFrame frame;
};

/**
 *  API functions
 */

/**
 * Initializes the library state.
 * Local node ID will be set to zero, i.e. the node will be anonymous.
 */
void canardInit(CanardInstance* out_ins,  void* mem_arena, size_t mem_arena_size,
                CanardOnTransferReception on_reception, CanardShouldAcceptTransfer should_accept)
{
    out_ins->node_id = CANARD_BROADCAST_NODE_ID;
    out_ins->on_reception = on_reception;
    out_ins->should_accept = should_accept;
    out_ins->rx_states = NULL;
    out_ins->tx_queue = NULL;
    canardInitPoolAllocator(&out_ins->allocator, mem_arena, mem_arena_size / CANARD_MEM_BLOCK_SIZE);
}

/**
 * Assigns a new node ID value to the current node.
 */
void canardSetLocalNodeID(CanardInstance* ins, uint8_t self_node_id)
{
    ins->node_id = self_node_id;
}

/**
 * Returns node ID of the local node.
 * Returns zero if the node ID has not been set.
 */
uint8_t canardGetLocalNodeID(const CanardInstance* ins)
{
    return ins->node_id;
}

/**
 * Sends a broadcast transfer.
 * If the node is in passive mode, only single frame transfers will be allowed.
 */
int canardBroadcast(CanardInstance* ins,
                    uint64_t data_type_signature,
                    uint16_t data_type_id,
                    uint8_t* inout_transfer_id,
                    uint8_t priority,
                    const void* payload,
                    uint16_t payload_len)
{
    uint32_t can_id;
    uint16_t crc = 0xFFFFU;

    if (payload == NULL)
    {
        return -1;
    }
    if (priority > 31)
    {
        return -1;
    }
    if (canardGetLocalNodeID(ins) == 0)
    {
        if (payload_len > 7)
        {
            return -1;
        }
        else
        {
            // anonymous transfer, random discriminator
            uint16_t discriminator = (crcAdd(0xFFFFU, payload, payload_len)) & 0x7FFEU;
            can_id = ((uint32_t)priority << 24) | ((uint32_t)discriminator << 9) |
                     ((uint32_t)(data_type_id & 0xFF) << 8) | (uint32_t)canardGetLocalNodeID(ins);
        }
    }
    else
    {
        can_id = ((uint32_t)priority << 24) | ((uint32_t)data_type_id << 8) | (uint32_t)canardGetLocalNodeID(ins);

        if (payload_len > 7)
        {
            crc = crcAddSignature(crc, data_type_signature);
            crc = crcAdd(crc, payload, payload_len);
        }
    }

    canardEnqueueData(ins, can_id, inout_transfer_id, crc, payload, payload_len);

    tidIncrement(inout_transfer_id);
    return 1;
}

/**
 * Sends a request or a response transfer.
 * Fails if the node is in passive mode.
 */
int canardRequestOrRespond(CanardInstance* ins,
                           uint8_t destination_node_id,
                           uint64_t data_type_signature,
                           uint16_t data_type_id,
                           uint8_t* inout_transfer_id,
                           uint8_t priority,
                           CanardRequestResponse kind,
                           const void* payload,
                           uint16_t payload_len)
{
    if (payload == NULL)
    {
        return -1;
    }
    if (canardGetLocalNodeID(ins) == 0)
    {
        return -1;
    }
    if (priority > 31)
    {
        return -1;
    }

    const uint32_t can_id = ((uint32_t)priority << 24) | ((uint32_t)data_type_id << 16) |
                            ((uint32_t)kind << 15) | ((uint32_t)destination_node_id << 8) |
                            (1 << 7) | (uint32_t)canardGetLocalNodeID(ins);
    uint16_t crc = 0xFFFFU;

    if (payload_len > 7)
    {
        crc = crcAddSignature(crc, data_type_signature);
        crc = crcAdd(crc, payload, payload_len);
    }

    canardEnqueueData(ins, can_id, inout_transfer_id, crc, payload, payload_len);

    tidIncrement(inout_transfer_id);

    return 1;
}

/**
 * Returns a pointer to the top priority frame in the TX queue.
 * Returns NULL if the TX queue is empty.
 */
const CanardCANFrame* canardPeekTxQueue(const CanardInstance* ins)
{
    if (ins->tx_queue == NULL)
    {
        return NULL;
    }
    return &ins->tx_queue->frame;
}

/**
 * Removes the top priority frame from the TX queue.
 */
void canardPopTxQueue(CanardInstance* ins)
{
    CanardTxQueueItem* item = ins->tx_queue;
    ins->tx_queue = item->next;
    canardFreeBlock(&ins->allocator, item);
}

/**
 * Processes a received CAN frame with a timestamp.
 */
void canardHandleRxFrame(CanardInstance* ins, const CanardCANFrame* frame, uint64_t timestamp_usec)
{
    uint8_t transfer_type = canardTransferType(frame->id);
    uint8_t priority = CANARD_PRIORITY_FROM_ID(frame->id);
    uint8_t source_node_id = CANARD_SOURCE_ID_FROM_ID(frame->id);
    uint8_t destination_node_id = (transfer_type == CanardTransferTypeBroadcast) ? 0 : CANARD_DEST_ID_FROM_ID(frame->id);
    uint16_t data_type_id = canardDataType(frame->id);
    uint32_t transfer_descriptor = CANARD_MAKE_TRANSFER_DESCRIPTOR(data_type_id, transfer_type, source_node_id,
                                                                   destination_node_id);
    
    if(transfer_type != CanardTransferTypeBroadcast && destination_node_id != CanardGetLocalNodeID(ins))
    {
        return;
    }

    CanardRxState* rxstate = NULL;
    unsigned char tail_byte = frame->data[frame->data_len - 1];

    if (IS_START_OF_TRANSFER(tail_byte))
    {
        uint64_t data_type_signature;

        if (ins->should_accept(ins, &data_type_signature, data_type_id, transfer_type, source_node_id))
        {
            rxstate = canardRxStateTraversal(ins, transfer_descriptor);
            rxstate->calculated_crc = crcAddSignature(0xFFFFU, data_type_signature);
        }
    }
    else
    {
        rxstate = canardFindRxState(ins->rx_states, transfer_descriptor);
    }

    if (rxstate == NULL)
    {
        return;
    }

    // Resolving the state flags:
    const bool not_initialized = (rxstate->timestamp_usec == 0) ? true : false;
    const bool tid_timed_out = (timestamp_usec - rxstate->timestamp_usec) > TRANSFER_TIMEOUT_USEC;
    const bool first_frame = IS_START_OF_TRANSFER(tail_byte);
    const bool not_previous_tid =
        computeForwardDistance(rxstate->transfer_id, TRANSFER_ID_FROM_TAIL_BYTE(tail_byte)) > 1;

    bool need_restart =
        (not_initialized) ||
        (tid_timed_out) ||
        (first_frame && not_previous_tid);

    if (need_restart)
    {
        rxstate->transfer_id = TRANSFER_ID_FROM_TAIL_BYTE(tail_byte);
        rxstate->next_toggle = 0;
        canardReleaseStatePayload(ins, rxstate);
        if (!IS_START_OF_TRANSFER(tail_byte)) // missed the first frame
        {
            rxstate->transfer_id += 1;
            return;
        }
    }

    if (IS_START_OF_TRANSFER(tail_byte) && IS_END_OF_TRANSFER(tail_byte)) // single frame transfer
    {
        rxstate->timestamp_usec = timestamp_usec;
        CanardRxTransfer rxtransfer = {
            .payload_head = frame->data,
            .payload_len = frame->data_len - 1,
            .data_type_id = data_type_id,
            .transfer_type = transfer_type,
            .priority = priority,
            .source_node_id = source_node_id
        };
        ins->on_reception(ins, &rxtransfer);

        canardPrepareForNextTransfer(rxstate);
        return;
    }

    if (TOGGLE_BIT(tail_byte) != rxstate->next_toggle)
    {
        return; // wrong toggle
    }

    if (TRANSFER_ID_FROM_TAIL_BYTE(tail_byte) != rxstate->transfer_id)
    {
        return; // unexpected tid
    }

    if (IS_START_OF_TRANSFER(tail_byte) && !IS_END_OF_TRANSFER(tail_byte)) // beginning of multi frame transfer
    { // take off the crc and store the payload
        rxstate->timestamp_usec = timestamp_usec;
        int ret = canardBufferBlockPushBytes(&ins->allocator, rxstate, frame->data + 2, frame->data_len - 3);
        if (ret != 1)
        {
            return;
        }
        rxstate->payload_crc = ((uint16_t)frame->data[0] << 8) | ((uint16_t)frame->data[1]);
        rxstate->calculated_crc = crcAdd(rxstate->calculated_crc, frame->data + 2, frame->data_len - 3);
    }
    else if (!IS_START_OF_TRANSFER(tail_byte) && !IS_END_OF_TRANSFER(tail_byte))
    {
        int ret = canardBufferBlockPushBytes(&ins->allocator, rxstate, frame->data, frame->data_len - 1);
        if (ret != 1)
        {
            return;
        }
        rxstate->calculated_crc = crcAdd(rxstate->calculated_crc, frame->data, frame->data_len - 1);
    }
    else
    {   
        uint8_t tail_offset = 0;
        uint16_t middle_len = 0;
        if (rxstate->payload_len < CANARD_RX_PAYLOAD_HEAD_SIZE)
        {
            int i;
            for (i=rxstate->payload_len, tail_offset=0; i<CANARD_RX_PAYLOAD_HEAD_SIZE && tail_offset<frame->data_len-1; i++, tail_offset++)
            {
                rxstate->buffer_head[i] = frame->data[tail_offset];
            }
        }

        // for transfers without buffer_blocks
        if (rxstate->buffer_blocks != NULL)
        {
            middle_len = rxstate->payload_len - CANARD_RX_PAYLOAD_HEAD_SIZE;
        }

        CanardRxTransfer rxtransfer = {
            .timestamp_usec = timestamp_usec,
            .payload_head = rxstate->buffer_head,
            .payload_middle = rxstate->buffer_blocks,
            .payload_tail = frame->data + tail_offset,
            .middle_len = middle_len,
            .payload_len = rxstate->payload_len + frame->data_len - 1,
            .data_type_id = data_type_id,
            .transfer_type = transfer_type,
            .priority = priority,
            .source_node_id = source_node_id
        };
        rxstate->calculated_crc = crcAdd(rxstate->calculated_crc, frame->data, frame->data_len - 1);
        // crc validation
        if (rxstate->calculated_crc == rxstate->payload_crc)
        {
            ins->on_reception(ins, &rxtransfer);
        }
        else
        {
            canardReleaseRxTransferPayload(ins, &rxtransfer);
        }
        canardPrepareForNextTransfer(rxstate);
        return;
    }
    rxstate->next_toggle ^= 1;
}

/**
 * Traverses the list of transfers and removes those that were last updated more than
 * timeout_usec microseconds ago
 */
void canardCleanupStaleTransfers(CanardInstance* ins, uint64_t current_time_usec)
{
    CanardRxState* prev = ins->rx_states, * state = ins->rx_states;
    while (state != NULL)
    {
        if ((current_time_usec - state->timestamp_usec)>TRANSFER_TIMEOUT_USEC) // two seconds
        {
            if (state==ins->rx_states)
            {
                canardReleaseStatePayload(ins, state);
                ins->rx_states = ins->rx_states->next;
                canardFreeBlock(&ins->allocator, state);
                state = ins->rx_states;
                prev = state;
            }
            else
            {
                canardReleaseStatePayload(ins, state);
                prev->next = state->next;
                canardFreeBlock(&ins->allocator, state);
                state = prev->next;
            }
            continue;
        }
        prev = state;
        state = state->next;
    }
}

/**
 * Reads bits (1 to 64) from RX transfer buffer.
 * This function can be used to decode received transfers.
 * Note that this function does not need the library's instance object.
 */
uint64_t canardReadRxTransferPayload(const CanardRxTransfer* transfer,
                                     uint16_t bit_offset,
                                     uint8_t bit_length)
{
    uint64_t bits = 0;
    int shift_val =  bit_length - 8;
    if (transfer->payload_len > 7)      //multi frame
    {
        CanardBufferBlock* block = transfer->payload_middle;
        uint16_t i;
        uint16_t index = 0;

        //head
        for (i = 0; i<CANARD_RX_PAYLOAD_HEAD_SIZE && index<bit_length && shift_val>=0; i++, index++)
        {
            if (index>=bit_offset / 8)
            {
                bits |= ((uint64_t)transfer->payload_head[i] << shift_val);
                shift_val -= 8;
            }
        }
        //middle (buffer blocks)
        for (i = 0; index<(CANARD_RX_PAYLOAD_HEAD_SIZE + transfer->middle_len) &&
             index<bit_length && shift_val>=0; i++, index++)
        {
            if (index>=bit_offset / 8)
            {
                bits |= ((uint64_t)block->data[i] << shift_val);
                shift_val -= 8;
            }
            if (i==CANARD_BUFFER_BLOCK_DATA_SIZE - 1)
            {
                i = 0;
                block = block->next;
            }
        }
        // tail
        int tail_len = transfer->payload_len - (CANARD_RX_PAYLOAD_HEAD_SIZE + transfer->middle_len);
        for (i = 0; i<(tail_len) && index<bit_length && shift_val>=0; i++, index++)
        {
            if (index>=bit_offset / 8)
            {
                bits |= ((uint64_t)transfer->payload_tail[i] << shift_val);
                shift_val -= 8;
            }
        }
    }
    else    //single frame
    {
        uint8_t i;
        for (i = 0; i<transfer->payload_len && i<bit_length && shift_val>=0; i++)
        {
            if (i>=bit_offset / 8)
            {
                bits |= ((uint64_t)transfer->payload_head[i] << shift_val);
                shift_val -= 8;
            }
        }
    }
    return bits;
}

/**
 * This function can be invoked by the application to release pool blocks that are used
 * to store the payload of this transfer
 */
uint64_t canardReleaseRxTransferPayload(CanardInstance* ins, CanardRxTransfer* transfer)
{
    CanardBufferBlock* temp = transfer->payload_middle;
    while (transfer->payload_middle != NULL)
    {
        temp = transfer->payload_middle->next;
        canardFreeBlock(&ins->allocator, transfer->payload_middle);
        transfer->payload_middle = temp;
    }
    transfer->payload_middle = NULL;

    transfer->payload_head = NULL;
    transfer->payload_tail = NULL;
    transfer->payload_len = 0;
    return 0;
}

/**
 *  internal (static functions)
 *
 *
 */

/**
 * TransferID
 */
CANARD_INTERNAL int computeForwardDistance(uint8_t a, uint8_t b)
{
    int d = b - a;
    if (d < 0)
    {
        d += 1 << TRANSFER_ID_BIT_LEN;
    }
    return d;
}

CANARD_INTERNAL void tidIncrement(uint8_t* transfer_id)
{
    *transfer_id += 1;
    if (*transfer_id >= 32)
    {
        *transfer_id = 0;
    }
}

CANARD_INTERNAL int canardEnqueueData(CanardInstance* ins, uint32_t can_id, uint8_t* transfer_id,
                                      uint16_t crc, const uint8_t* payload, uint16_t payload_len)
{
    // single frame transfer
    if (payload_len < CANARD_CAN_FRAME_MAX_DATA_LEN)
    {
        CanardTxQueueItem* queue_item = canardCreateTxItem(&ins->allocator);

        if (queue_item == NULL)
        {
            return -1;
        }

        memcpy(queue_item->frame.data, payload, payload_len);

        queue_item->frame.data_len = payload_len + 1;

        queue_item->frame.data[payload_len] = 0xC0 | (*transfer_id & 31);

        queue_item->frame.id = can_id;

        canardPushTxQueue(ins, queue_item);
    }
    else if (payload_len >= CANARD_CAN_FRAME_MAX_DATA_LEN)
    {
        // multiframe transfer
        uint8_t i = 2, data_index = 0, toggle = 0, sot_eot = 0x80;

        CanardTxQueueItem* queue_item;

        while (payload_len - data_index != 0)
        {
            queue_item = canardCreateTxItem(&ins->allocator);

            if (queue_item == NULL)
            {
                return -1;
            }

            if (data_index == 0)
            {
                // add crc
                queue_item->frame.data[0] = (uint8_t)(crc >> 8);
                queue_item->frame.data[1] = (uint8_t)(crc);
                i = 2;
            }
            else
            {
                i = 0;
            }

            for (; i<7 && data_index<payload_len; i++, data_index++)
            {
                queue_item->frame.data[i] = payload[data_index];
            }
            // tail byte
            sot_eot = (data_index==payload_len) ? 0x40 : sot_eot;

            queue_item->frame.data[i] = sot_eot | (toggle << 5) | (*transfer_id & 31);
            queue_item->frame.id = can_id;
            queue_item->frame.data_len = i + 1;
            canardPushTxQueue(ins, queue_item);

            toggle ^= 1;
            sot_eot = 0;
        }
    }
    return 1;
}

/**
 * Puts frame on on the TX queue. Higher priority placed first
 */
CANARD_INTERNAL void canardPushTxQueue(CanardInstance* ins, CanardTxQueueItem* item)
{
    if (ins->tx_queue == NULL)
    {
        ins->tx_queue = item;
        return;
    }
    CanardTxQueueItem* queue = ins->tx_queue;
    CanardTxQueueItem* previous = ins->tx_queue;
    while (queue != NULL)
    {
        if (priorityHigherThan(queue->frame.id, item->frame.id)) // lower number wins
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
 * creates new tx queue item from allocator
 */
CANARD_INTERNAL CanardTxQueueItem* canardCreateTxItem(CanardPoolAllocator* allocator)
{
    CanardTxQueueItem* item = (CanardTxQueueItem*)canardAllocateBlock(allocator);
    if (item == NULL)
    {
        return NULL;
    }
    memset(item, 0, sizeof *item);

    return item;
}

/**
 * returns true if priority of rhs is higher than id
 */
CANARD_INTERNAL bool priorityHigherThan(uint32_t rhs, uint32_t id)
{
    const uint32_t clean_id     = id    & CANARD_EXT_ID_MASK;
    const uint32_t rhs_clean_id = rhs   & CANARD_EXT_ID_MASK;
    /*
     * STD vs EXT - if 11 most significant bits are the same, EXT loses.
     */
    bool ext     = id     & CANARD_CAN_FRAME_EFF;
    bool rhs_ext = rhs & CANARD_CAN_FRAME_EFF;
    if (ext != rhs_ext)
    {
        uint32_t arb11     = ext     ? (clean_id >> 18)     : clean_id;
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
    bool rtr     = id     & CANARD_CAN_FRAME_RTR;
    bool rhs_rtr = rhs & CANARD_CAN_FRAME_RTR;
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
CANARD_INTERNAL void canardPrepareForNextTransfer(CanardRxState* state)
{
    state->buffer_blocks = NULL; // payload should be empty anyway
    state->transfer_id += 1;
    state->payload_len = 0;
    state->next_toggle = 0;
    return;
}

/**
 * returns data type from id
 */
CANARD_INTERNAL uint16_t canardDataType(uint32_t id)
{
    uint8_t transfer_type = canardTransferType(id);
    if (transfer_type == CanardTransferTypeBroadcast)
    {
        return (uint16_t)CANARD_MSG_TYPE_FROM_ID(id);
    }
    else
    {
        return (uint16_t)CANARD_SRV_TYPE_FROM_ID(id);
    }
}

/**
 * returns transfer type from id
 */
CANARD_INTERNAL uint8_t canardTransferType(uint32_t id)
{
    uint8_t is_service = (uint8_t)CANARD_SERVICE_NOT_MSG_FROM_ID(id);
    if (is_service == 0)
    {
        return CanardTransferTypeBroadcast;
    }
    else if (CANARD_REQUEST_NOT_RESPONSE_FROM_ID(id) == 1)
    {
        return CanardTransferTypeRequest;
    }
    else
    {
        return CanardTransferTypeResponse;
    }
}

/**
 *  CanardRxState functions
 */

/**
 * Traverses the list of CanardRxState's and returns a pointer to the CanardRxState
 * with either the Id or a new one at the end
 */
CANARD_INTERNAL CanardRxState* canardRxStateTraversal(CanardInstance* ins, uint32_t transfer_descriptor)
{
    CanardRxState* states = ins->rx_states;

    if (states==NULL) // initialize CanardRxStates
    {
        states = canardCreateRxState(&ins->allocator, transfer_descriptor);
        ins->rx_states = states;
        return states;
    }

    states = canardFindRxState(states, transfer_descriptor);
    if (states != NULL)
    {
        return states;
    }
    else
    {
        return canardPrependRxState(ins, transfer_descriptor);
    }
}

/**
 * returns pointer to the rx state of transfer descriptor or null if not found
 */
CANARD_INTERNAL CanardRxState* canardFindRxState(CanardRxState* state, uint32_t transfer_descriptor)
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
CANARD_INTERNAL CanardRxState* canardPrependRxState(CanardInstance* ins, uint32_t transfer_descriptor)
{
    CanardRxState* state = canardCreateRxState(&ins->allocator, transfer_descriptor);
    state->next = ins->rx_states;
    ins->rx_states = state;
    return state;
}

CANARD_INTERNAL CanardRxState* canardCreateRxState(CanardPoolAllocator* allocator, uint32_t transfer_descriptor)
{
    CanardRxState init = { .next = NULL, .buffer_blocks = NULL, .dtid_tt_snid_dnid = transfer_descriptor };
    CanardRxState* state = (CanardRxState*)canardAllocateBlock(allocator);

    if (state == NULL)
    {
        return NULL;
    }
    memcpy(state, &init, sizeof *state);

    return state;
}

/**
 * This function can be invoked by the application to release pool blocks that are used
 * to store the payload of this transfer
 */
CANARD_INTERNAL uint64_t canardReleaseStatePayload(CanardInstance* ins, CanardRxState* rxstate)
{
    CanardBufferBlock* temp = rxstate->buffer_blocks;
    while (rxstate->buffer_blocks != NULL)
    {
        temp = rxstate->buffer_blocks->next;
        canardFreeBlock(&ins->allocator, rxstate->buffer_blocks);
        rxstate->buffer_blocks = temp;
    }
    rxstate->payload_len = 0;
    return 0;
}

/**
 *  CanardBufferBlock functions
 */

/**
 * pushes data into the rx state. Fills the buffer head, then appends data to buffer blocks
 */
CANARD_INTERNAL int canardBufferBlockPushBytes(CanardPoolAllocator* allocator, CanardRxState* state,
                                                const uint8_t* data,
                                                uint8_t data_len)
{
    uint16_t data_index = 0;
    uint16_t i;

    // if head is not full, add data to head
    if ((int)CANARD_RX_PAYLOAD_HEAD_SIZE - (int)state->payload_len > 0)
    {
        for (i = state->payload_len; i<CANARD_RX_PAYLOAD_HEAD_SIZE && data_index<data_len; i++, data_index++)
        {
            state->buffer_head[i] = data[data_index];
        }
        if (data_index >= data_len /*- 1*/)
        {
            state->payload_len += data_len;
            return 1;
        }
    } // head is full.

    uint8_t num_buffer_blocks =
        (((state->payload_len + data_len) - CANARD_RX_PAYLOAD_HEAD_SIZE) / CANARD_BUFFER_BLOCK_DATA_SIZE) + 1;
    uint8_t index_at_nth_block = (((state->payload_len) - CANARD_RX_PAYLOAD_HEAD_SIZE) % CANARD_BUFFER_BLOCK_DATA_SIZE);

    // get to current block
    CanardBufferBlock* block;
    uint8_t nth_block = 1;

    // buffer blocks uninitialized
    if (state->buffer_blocks == NULL)
    {
        state->buffer_blocks = canardCreateBufferBlock(allocator);
        block = state->buffer_blocks;
        index_at_nth_block = 0;
    }
    else
    {
        // get to block
        block = state->buffer_blocks;
        while (block->next != NULL)
        {
            nth_block++;
            block = block->next;
        }
        if (num_buffer_blocks > nth_block && index_at_nth_block == 0)
        {
            block->next = canardCreateBufferBlock(allocator);
            if (block->next == NULL)
            {
                return -1;
            }
            block = block->next;
            nth_block++;
        }
    }

    // add data to current block until it becomes full, add new block if necessary
    while (data_index < data_len)
    {
        for (i = index_at_nth_block; i<CANARD_BUFFER_BLOCK_DATA_SIZE && data_index<data_len; i++, data_index++)
        {
            block->data[i] = data[data_index];
        }
        if (data_index < data_len)
        {
            block->next = canardCreateBufferBlock(allocator);
            if (block->next == NULL)
            {
                return -1;
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
CANARD_INTERNAL CanardBufferBlock* canardCreateBufferBlock(CanardPoolAllocator* allocator)
{
    CanardBufferBlock* block = (CanardBufferBlock*)canardAllocateBlock(allocator);
    if (block == NULL)
    {
        return NULL;
    }
    block->next = NULL;
    return block;
}

/**
 * CRC functions
 */
CANARD_INTERNAL uint16_t crcAddByte(uint16_t crc_val, uint8_t byte)
{
    crc_val ^= (uint16_t)((uint16_t)(byte) << 8);
    int j;
    for (j = 0; j<8; j++)
    {
        if (crc_val & 0x8000U)
        {
            crc_val = (uint16_t)((uint16_t)(crc_val << 1) ^ 0x1021U);
        }
        else
        {
            crc_val = (uint16_t)(crc_val << 1);
        }
    }
    return crc_val;
}

CANARD_INTERNAL uint16_t crcAddSignature(uint16_t crc_val, uint64_t data_type_signature)
{
    int shift_val;
    for (shift_val = 56; shift_val>0; shift_val -= 8)
    {
        crc_val = crcAddByte(crc_val, (uint8_t)(data_type_signature >> shift_val));
    }
    return crc_val;
}

CANARD_INTERNAL uint16_t crcAdd(uint16_t crc_val, const uint8_t* bytes, uint16_t len)
{
    while (len--)
    {
        crc_val = crcAddByte(crc_val, *bytes++);
    }
    return crc_val;
}

/**
 *  Pool Allocator functions
 */
CANARD_INTERNAL void canardInitPoolAllocator(CanardPoolAllocator* allocator, CanardPoolAllocatorBlock* buf,
                                             unsigned int buf_len)
{
    unsigned int current_index = 0;
    CanardPoolAllocatorBlock** current_block = &(allocator->free_list);
    while (current_index < buf_len)
    {
        *current_block = &buf[current_index];
        current_block = &((*current_block)->next);
        current_index++;
    }
    *current_block = NULL;
}

CANARD_INTERNAL void* canardAllocateBlock(CanardPoolAllocator* allocator)
{
    /* Check if there are any blocks available in the free list. */
    if (allocator->free_list == NULL)
    {
        return NULL;
    }

    /* Take first available block and prepares next block for use. */
    void* result = allocator->free_list;
    allocator->free_list = allocator->free_list->next;

    return result;
}

CANARD_INTERNAL void canardFreeBlock(CanardPoolAllocator* allocator, void* p)
{
    CanardPoolAllocatorBlock* block = (CanardPoolAllocatorBlock*)p;

    block->next = allocator->free_list;
    allocator->free_list = block;
}
