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

#ifndef CANARD_H
#define CANARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/// Error code definitions; inverse of these values may be returned from API calls.
#define CANARD_OK                           0
// Value 1 is omitted intentionally, since -1 is often used in 3rd party code
#define CANARD_ERROR_INVALID_ARGUMENT       2
#define CANARD_ERROR_OUT_OF_MEMORY          3
#define CANARD_ERROR_NODE_ID_NOT_SET        4
#define CANARD_ERROR_INTERNAL               9

/// The size of a memory block in bytes.
#define CANARD_MEM_BLOCK_SIZE               32

/// This will be changed when the support for CAN FD is added
#define CANARD_CAN_FRAME_MAX_DATA_LEN       8

/// Node ID values. Refer to the specification for more info.
#define CANARD_BROADCAST_NODE_ID            0
#define CANARD_MIN_NODE_ID                  1
#define CANARD_MAX_NODE_ID                  127

/// Refer to the type CanardRxTransfer
#define CANARD_RX_PAYLOAD_HEAD_SIZE         (CANARD_MEM_BLOCK_SIZE - offsetof(CanardRxState, buffer_head))

/// Refer to the type CanardBufferBlock
#define CANARD_BUFFER_BLOCK_DATA_SIZE       (CANARD_MEM_BLOCK_SIZE - offsetof(CanardBufferBlock, data))

/// Refer to canardCleanupStaleTransfers() for details.
#define CANARD_RECOMMENDED_STALE_TRANSFER_CLEANUP_INTERVAL_USEC     1000000U

/// Transfer priority definitions
#define CANARD_TRANSFER_PRIORITY_HIGHEST    0
#define CANARD_TRANSFER_PRIORITY_HIGH       8
#define CANARD_TRANSFER_PRIORITY_MEDIUM     16
#define CANARD_TRANSFER_PRIORITY_LOW        24
#define CANARD_TRANSFER_PRIORITY_LOWEST     31

/// Related to CanardCANFrame
#define CANARD_CAN_EXT_ID_MASK                      0x1FFFFFFFU
#define CANARD_CAN_FRAME_EFF                        (1U << 31)          ///< Extended frame format
#define CANARD_CAN_FRAME_RTR                        (1U << 30)          ///< Remote transmission (not used by UAVCAN)
#define CANARD_CAN_FRAME_ERR                        (1U << 29)          ///< Error frame (not used by UAVCAN)

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
    uint8_t data[CANARD_CAN_FRAME_MAX_DATA_LEN];
    uint8_t data_len;
} CanardCANFrame;

/**
 * Transfer types are defined by the UAVCAN specification.
 */
typedef enum
{
    CanardTransferTypeResponse  = 0,
    CanardTransferTypeRequest   = 1,
    CanardTransferTypeBroadcast = 2
} CanardTransferType;

/**
 * Types of service transfers. These are not applicable to message transfers.
 */
typedef enum
{
    CanardResponse,
    CanardRequest
} CanardRequestResponse;

/*
 * Forward declarations.
 */
typedef struct CanardInstance CanardInstance;
typedef struct CanardRxTransfer CanardRxTransfer;
typedef struct CanardRxState CanardRxState;
typedef struct CanardTxQueueItem CanardTxQueueItem;

/**
 * The application must implement this function and supply a pointer to it to the library during initialization.
 * The library calls this function to determine whether the transfer should be received.
 */
typedef bool (* CanardShouldAcceptTransfer)(const CanardInstance* ins,
                                            uint64_t* out_data_type_signature,
                                            uint16_t data_type_id,
                                            CanardTransferType transfer_type,
                                            uint8_t source_node_id);

/**
 * This function will be invoked by the library every time a transfer is successfully received.
 * If the application needs to send another transfer from this callback, it is highly recommended
 * to call canardReleaseRxTransferPayload() first, so that the memory that was used for the block
 * buffer can be released and re-used by the TX queue.
 */
typedef void (* CanardOnTransferReception)(CanardInstance* ins,
                                           CanardRxTransfer* transfer);

/**
 * INTERNAL DEFINITION, DO NOT USE DIRECTLY.
 * A memory block used in the memory block allocator.
 */
typedef union CanardPoolAllocatorBlock_u
{
    char bytes[CANARD_MEM_BLOCK_SIZE];
    union CanardPoolAllocatorBlock_u* next;
} CanardPoolAllocatorBlock;

/**
 * This structure provides usage statistics of the memory pool allocator.
 * This data helps to evaluate whether the allocated memory is sufficient for the application.
 */
typedef struct
{
    uint16_t capacity_blocks;               ///< Pool capacity in number of blocks
    uint16_t current_usage_blocks;          ///< Number of blocks that are currently allocated by the library
    uint16_t peak_usage_blocks;             ///< Maximum number of blocks used since initialization
} CanardPoolAllocatorStatistics;

/**
 * INTERNAL DEFINITION, DO NOT USE DIRECTLY.
 */
typedef struct
{
    CanardPoolAllocatorBlock* free_list;
    CanardPoolAllocatorStatistics statistics;
} CanardPoolAllocator;

/**
 * Buffer block for received data.
 */
typedef struct CanardBufferBlock
{
    struct CanardBufferBlock* next;
    uint8_t data[];
} CanardBufferBlock;

/**
 * INTERNAL DEFINITION, DO NOT USE DIRECTLY.
 */
struct CanardRxState
{
    struct CanardRxState* next;

    CanardBufferBlock* buffer_blocks;

    uint64_t timestamp_usec;

    const uint32_t dtid_tt_snid_dnid;

    uint16_t payload_crc;

    // We're using plain 'unsigned' here, because C99 doesn't permit explicit field type specification
    unsigned calculated_crc : 16;
    unsigned payload_len    : 10;
    unsigned transfer_id    : 5;
    unsigned next_toggle    : 1;    // 16+10+5+1 = 32, aligned.

    uint8_t buffer_head[];
};

/**
 * This is the core structure that keeps all of the states and allocated resources of the library instance.
 * The application should never access any of the fields directly! Instead, API functions should be used.
 */
struct CanardInstance
{
    uint8_t node_id;                                ///< Local node ID; may be zero if the node is anonymous

    CanardShouldAcceptTransfer should_accept;       ///< Function to decide whether the application wants this transfer
    CanardOnTransferReception on_reception;         ///< Function the library calls after RX transfer is complete

    CanardPoolAllocator allocator;                  ///< Pool allocator

    CanardRxState* rx_states;                       ///< RX transfer states
    CanardTxQueueItem* tx_queue;                    ///< TX frames awaiting transmission
};

/**
 * This structure represents a received transfer for the application.
 * An instance of it is passed to the application via callback when the library receives a new transfer.
 */
struct CanardRxTransfer
{
    /**
     * Timestamp at which the first frame of this transfer was received.
     */
    uint64_t timestamp_usec;

    /**
     * Payload is scattered across three storages:
     *  - Head points to CanardRxState.buffer_head (length of which is up to CANARD_PAYLOAD_HEAD_SIZE), or to the
     *    payload field (possibly with offset) of the last received CAN frame.
     *
     *  - Middle is located in the linked list of dynamic blocks (only for multi-frame transfers).
     *
     *  - Tail points to the payload field (possibly with offset) of the last received CAN frame
     *    (only for multi-frame transfers).
     *
     * The tail offset depends on how much data of the last frame was accommodated in the last allocated block.
     *
     * For single-frame transfers, middle and tail will be NULL, and the head will point at first byte
     * of the payload of the CAN frame.
     *
     * In simple cases it should be possible to get data directly from the head and/or tail pointers.
     * Otherwise it is advised to use canardReadScalarFromRxTransfer().
     */
    const uint8_t* payload_head;            ///< Always valid, i.e. not NULL.
                                            ///< For multi frame transfers, the maximum size is defined in the constant
                                            ///< CANARD_RX_PAYLOAD_HEAD_SIZE.
                                            ///< For single-frame transfers, the size is defined in the
                                            ///< field payload_len.
    CanardBufferBlock* payload_middle;      ///< May be NULL if the buffer was not needed. Always NULL for single-frame
                                            ///< transfers.
    const uint8_t* payload_tail;            ///< Last bytes of multi-frame transfers. Always NULL for single-frame
                                            ///< transfers.
    uint16_t payload_len;                   ///< Effective length of the payload in bytes.

    /**
     * These fields identify the transfer for the application.
     */
    uint16_t data_type_id;                  ///< 0 to 255 for services, 0 to 65535 for messages
    uint8_t transfer_type;                  ///< See CanardTransferType
    uint8_t transfer_id;                    ///< 0 to 31
    uint8_t priority;                       ///< 0 to 31
    uint8_t source_node_id;                 ///< 1 to 127, or 0 if the source is anonymous
};

/**
 * Initializes the library state.
 * Local node ID will be set to zero, i.e. the node will be anonymous.
 */
void canardInit(CanardInstance* out_ins,
                void* mem_arena,
                size_t mem_arena_size,
                CanardOnTransferReception on_reception,
                CanardShouldAcceptTransfer should_accept);

/**
 * Assigns a new node ID value to the current node.
 * A node ID can be assigned only once.
 */
void canardSetLocalNodeID(CanardInstance* ins,
                          uint8_t self_node_id);

/**
 * Returns node ID of the local node.
 * Returns zero if the node ID has not been set, i.e. if the local node is anonymous.
 */
uint8_t canardGetLocalNodeID(const CanardInstance* ins);

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
                    uint16_t payload_len);

/**
 * Sends a request or a response transfer.
 * Fails if the node is in passive mode.
 */
int canardRequestOrRespond(CanardInstance* ins,
                           uint8_t destination_node_id,
                           uint64_t data_type_signature,
                           uint8_t data_type_id,
                           uint8_t* inout_transfer_id,
                           uint8_t priority,
                           CanardRequestResponse kind,
                           const void* payload,
                           uint16_t payload_len);

/**
 * Returns a pointer to the top priority frame in the TX queue.
 * Returns NULL if the TX queue is empty.
 */
const CanardCANFrame* canardPeekTxQueue(const CanardInstance* ins);

/**
 * Removes the top priority frame from the TX queue.
 */
void canardPopTxQueue(CanardInstance* ins);

/**
 * Processes a received CAN frame with a timestamp.
 */
void canardHandleRxFrame(CanardInstance* ins,
                         const CanardCANFrame* frame,
                         uint64_t timestamp_usec);

/**
 * Traverses the list of transfers and removes those that were last updated more than timeout_usec microseconds ago.
 * This function must be invoked by the application periodically, about once a second.
 * Also refer to the constant CANARD_RECOMMENDED_STALE_TRANSFER_CLEANUP_INTERVAL_USEC.
 */
void canardCleanupStaleTransfers(CanardInstance* ins,
                                 uint64_t current_time_usec);

/**
 * Reads 1 to 64 bits from the specified RX transfer buffer.
 * This function can be used to decode received transfers.
 * Returns the number of bits copied, which may be less than requested if operation ran out of buffer boundaries,
 * or negated error code, such as invalid argument.
 *
 * The type of value pointed to by 'out_value' is defined as follows:
 *  -----------------------------------------------------------
 *  bit_length  value_is_signed out_value points to
 *  -----------------------------------------------------------
 *  1           false           bool (may be incompatible with uint8_t!)
 *  1           true            N/A
 *  [2, 8]      false           uint8_t, or char
 *  [2, 8]      true            int8_t, or char
 *  [9, 16]     false           uint16_t
 *  [9, 16]     true            int16_t
 *  [17, 32]    false           uint32_t
 *  [17, 32]    true            int32_t, or 32-bit float
 *  [33, 64]    false           uint64_t
 *  [33, 64]    true            int64_t, or 64-bit float
 */
int canardReadScalarFromRxTransfer(const CanardRxTransfer* transfer,
                                   uint32_t bit_offset,
                                   uint8_t bit_length,
                                   bool value_is_signed,
                                   void* out_value);

/**
 * This function can be invoked by the application to release pool blocks that are used
 * to store the payload of this transfer.
 * It is not mandatory to invoke it though - the library will do that if the application didn't.
 */
void canardReleaseRxTransferPayload(CanardInstance* ins,
                                    CanardRxTransfer* transfer);

/**
 * Returns a copy of the pool allocator usage statistics.
 * Refer to the type CanardPoolAllocatorStatistics.
 */
CanardPoolAllocatorStatistics canardGetPoolAllocatorStatistics(CanardInstance* ins);

/**
 * Float16 marshaling helpers.
 * These functions convert between the native float and 16-bit float.
 * It is assumed that the native float is IEEE 754 single precision float.
 */
uint16_t canardConvertNativeFloatToFloat16(float value);
float canardConvertFloat16ToNativeFloat(uint16_t value);


#ifdef __cplusplus
}
#endif
#endif
