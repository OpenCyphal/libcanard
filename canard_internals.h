/*
 * The MIT License (MIT)
 *
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

#ifndef CANARD_INTERNALS_H
#define CANARD_INTERNALS_H

#include "canard.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CANARD_INTERNAL
# define CANARD_INTERNAL static
#endif


#define TRANSFER_TIMEOUT_USEC                       2000000

#define TRANSFER_ID_BIT_LEN                         5

#define ANON_MSG_DATA_TYPE_ID_BIT_LEN               2

#define SOURCE_ID_FROM_ID(x)                        ((uint8_t) (((x) >> 0)  & 0x7F))
#define SERVICE_NOT_MSG_FROM_ID(x)                  ((bool)    (((x) >> 7)  & 0x1))
#define REQUEST_NOT_RESPONSE_FROM_ID(x)             ((bool)    (((x) >> 15) & 0x1))
#define DEST_ID_FROM_ID(x)                          ((uint8_t) (((x) >> 8)  & 0x7F))
#define PRIORITY_FROM_ID(x)                         ((uint8_t) (((x) >> 24) & 0x1F))
#define MSG_TYPE_FROM_ID(x)                         ((uint16_t)(((x) >> 8)  & 0xFFFF))
#define SRV_TYPE_FROM_ID(x)                         ((uint8_t) (((x) >> 16) & 0xFF))

#define MAKE_TRANSFER_DESCRIPTOR(data_type_id, transfer_type, src_node_id, dst_node_id)             \
    (((uint32_t)data_type_id) | (((uint32_t)transfer_type) << 16) |                                 \
    (((uint32_t)src_node_id) << 18) | (((uint32_t)dst_node_id) << 25))

#define TRANSFER_ID_FROM_TAIL_BYTE(x)               ((uint8_t)((x) & 0x1F))

#define IS_START_OF_TRANSFER(x)                     ((bool)(((x) >> 7) & 0x1))
#define IS_END_OF_TRANSFER(x)                       ((bool)(((x) >> 6) & 0x1))
#define TOGGLE_BIT(x)                               ((bool)(((x) >> 5) & 0x1))


CANARD_INTERNAL CanardRxState* traverseRxStates(CanardInstance* ins,
                                                uint32_t transfer_descriptor);

CANARD_INTERNAL CanardRxState* createRxState(CanardPoolAllocator* allocator,
                                             uint32_t transfer_descriptor);

CANARD_INTERNAL CanardRxState* prependRxState(CanardInstance* ins,
                                              uint32_t transfer_descriptor);

CANARD_INTERNAL CanardRxState* findRxState(CanardRxState* state,
                                           uint32_t transfer_descriptor);

CANARD_INTERNAL int bufferBlockPushBytes(CanardPoolAllocator* allocator,
                                         CanardRxState* state,
                                         const uint8_t* data,
                                         uint8_t data_len);

CANARD_INTERNAL CanardBufferBlock* createBufferBlock(CanardPoolAllocator* allocator);

CANARD_INTERNAL CanardTransferType extractTransferType(uint32_t id);

CANARD_INTERNAL uint16_t extractDataType(uint32_t id);

CANARD_INTERNAL void pushTxQueue(CanardInstance* ins,
                                 CanardTxQueueItem* item);

CANARD_INTERNAL bool isPriorityHigher(uint32_t id,
                                      uint32_t rhs);

CANARD_INTERNAL CanardTxQueueItem* createTxItem(CanardPoolAllocator* allocator);

CANARD_INTERNAL void prepareForNextTransfer(CanardRxState* state);

CANARD_INTERNAL int computeTransferIDForwardDistance(uint8_t a,
                                                     uint8_t b);

CANARD_INTERNAL void incrementTransferID(uint8_t* transfer_id);

CANARD_INTERNAL uint64_t releaseStatePayload(CanardInstance* ins,
                                             CanardRxState* rxstate);

/// Returns the number of frames enqueued
CANARD_INTERNAL int enqueueTxFrames(CanardInstance* ins,
                                    uint32_t can_id,
                                    uint8_t* transfer_id,
                                    uint16_t crc,
                                    const uint8_t* payload,
                                    uint16_t payload_len);

CANARD_INTERNAL uint16_t crcAddByte(uint16_t crc_val,
                                    uint8_t byte);

CANARD_INTERNAL uint16_t crcAddSignature(uint16_t crc_val,
                                         uint64_t data_type_signature);

CANARD_INTERNAL uint16_t crcAdd(uint16_t crc_val,
                                const uint8_t* bytes,
                                size_t len);

/**
 * Inits a memory allocator.
 *
 * @param [in] allocator The memory allocator to initialize.
 * @param [in] buf The buffer used by the memory allocator.
 * @param [in] buf_len The number of blocks in buf.
 */
CANARD_INTERNAL void initPoolAllocator(CanardPoolAllocator* allocator,
                                       CanardPoolAllocatorBlock* buf,
                                       uint16_t buf_len);

/**
 * Allocates a block from the given pool allocator.
 */
CANARD_INTERNAL void* allocateBlock(CanardPoolAllocator* allocator);

/**
 * Frees a memory block previously returned by canardAllocateBlock.
 */
CANARD_INTERNAL void freeBlock(CanardPoolAllocator* allocator,
                               void* p);


#ifdef __cplusplus
}
#endif
#endif
