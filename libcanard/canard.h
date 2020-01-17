/*
 * Copyright (c) 2016-2020 UAVCAN Development Team
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

#ifndef CANARD_H
#define CANARD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>

/// Build configuration header. Use it to provide your overrides.
#if defined(CANARD_ENABLE_CUSTOM_BUILD_CONFIG) && CANARD_ENABLE_CUSTOM_BUILD_CONFIG
#    include "canard_build_config.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// Semantic version numbers of this library (not the UAVCAN specification).
/// API will be backwards compatible within the same major version.
#define CANARD_VERSION_MAJOR 1
#define CANARD_VERSION_MINOR 0

/// The version number of the UAVCAN specification implemented by this library.
#define CANARD_UAVCAN_SPECIFICATION_VERSION_MAJOR 1

/// By default this macro resolves to the standard assert(). The user can redefine this if necessary.
#ifndef CANARD_ASSERT
#    define CANARD_ASSERT(x) assert(x)
#endif

#define CANARD_GLUE(a, b) CANARD_GLUE_IMPL_(a, b)
#define CANARD_GLUE_IMPL_(a, b) a##b

/// By default this macro expands to static_assert if supported by the language (C11, C++11, or newer).
/// The user can redefine this if necessary.
#ifndef CANARD_STATIC_ASSERT
#    if (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)) || \
        (defined(__cplusplus) && (__cplusplus >= 201103L))
#        define CANARD_STATIC_ASSERT(...) static_assert(__VA_ARGS__)
#    else
#        define CANARD_STATIC_ASSERT(x, ...) typedef char CANARD_GLUE(_static_assertion_, __LINE__)[(x) ? 1 : -1]
#    endif
#endif

/// Error code definitions; inverse of these values may be returned from API calls.
#define CANARD_OK 0
// Value 1 is omitted intentionally since -1 is often used in 3rd party code
#define CANARD_ERROR_INVALID_ARGUMENT 2
#define CANARD_ERROR_OUT_OF_MEMORY 3
#define CANARD_ERROR_NODE_ID_NOT_SET 4
#define CANARD_ERROR_INTERNAL 9
#define CANARD_ERROR_RX_INCOMPATIBLE_PACKET 10
#define CANARD_ERROR_RX_WRONG_ADDRESS 11
#define CANARD_ERROR_RX_NOT_WANTED 12
#define CANARD_ERROR_RX_MISSED_START 13
#define CANARD_ERROR_RX_WRONG_TOGGLE 14
#define CANARD_ERROR_RX_UNEXPECTED_TID 15
#define CANARD_ERROR_RX_SHORT_FRAME 16
#define CANARD_ERROR_RX_BAD_CRC 17

/// The size of a memory block in bytes.
#define CANARD_MEM_BLOCK_SIZE 32U

/// This will be changed when the support for CAN FD is added
#define CANARD_CAN_FRAME_MAX_DATA_LEN 8U

#define CANARD_CAN_MULTI_FRAME_CRC_LENGTH 2U

/// Node-ID values. Refer to the specification for more info.
#define CANARD_BROADCAST_NODE_ID 255
#define CANARD_MIN_NODE_ID 0
#define CANARD_MAX_NODE_ID 127

/// Refer to the type CanardRxTransfer
#define CANARD_MULTIFRAME_RX_PAYLOAD_HEAD_SIZE (CANARD_MEM_BLOCK_SIZE - offsetof(CanardRxState, buffer_head))

/// Refer to the type CanardBufferBlock
#define CANARD_BUFFER_BLOCK_DATA_SIZE (CANARD_MEM_BLOCK_SIZE - offsetof(CanardBufferBlock, data))

/// Transfer priority definitions
#define CANARD_TRANSFER_PRIORITY_EXCEPTIONAL 0
#define CANARD_TRANSFER_PRIORITY_IMMEDIATE 1
#define CANARD_TRANSFER_PRIORITY_FAST 2
#define CANARD_TRANSFER_PRIORITY_HIGH 3
#define CANARD_TRANSFER_PRIORITY_NOMINAL 4
#define CANARD_TRANSFER_PRIORITY_LOW 5
#define CANARD_TRANSFER_PRIORITY_SLOW 6
#define CANARD_TRANSFER_PRIORITY_OPTIONAL 7

/// Related to CanardCANFrame
#define CANARD_CAN_EXT_ID_MASK 0x1FFFFFFFU
#define CANARD_CAN_STD_ID_MASK 0x000007FFU
#define CANARD_CAN_FRAME_EFF (1UL << 31U)  ///< Extended frame format
#define CANARD_CAN_FRAME_RTR (1UL << 30U)  ///< Remote transmission (not used by UAVCAN)
#define CANARD_CAN_FRAME_ERR (1UL << 29U)  ///< Error frame (not used by UAVCAN)

#define CANARD_TRANSFER_PAYLOAD_LEN_BITS 10U
#define CANARD_MAX_TRANSFER_PAYLOAD_LEN ((1U << CANARD_TRANSFER_PAYLOAD_LEN_BITS) - 1U)

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
    uint8_t  data[CANARD_CAN_FRAME_MAX_DATA_LEN];
    uint8_t  data_len;
} CanardCANFrame;

/**
 * Transfer types are defined by the UAVCAN specification.
 */
typedef enum
{
    CanardTransferKindServiceResponse    = 0,
    CanardTransferKindServiceRequest     = 1,
    CanardTransferKindMessagePublication = 2
} CanardTransferKind;

/**
 * Types of service transfers. These are not applicable to message transfers.
 */
typedef enum
{
    CanardResponse,
    CanardRequest
} CanardRequestResponse;

// Forward declarations.
typedef struct CanardInstance    CanardInstance;
typedef struct CanardRxTransfer  CanardRxTransfer;
typedef struct CanardRxState     CanardRxState;
typedef struct CanardTxQueueItem CanardTxQueueItem;

/**
 * The application shall implement this function and supply a pointer to it to the library during initialization.
 * The library calls this function to determine whether a transfer should be received.
 * @param ins            Library instance.
 * @param port_id        Subject-ID or service-ID of the transfer.
 * @param transfer_kind  Message or service transfer.
 * @param source_node_id Node-ID of the origin; broadcast if anonymous.
 * @returns True if the transfer should be received; false otherwise.
 */
typedef bool (*CanardShouldAcceptTransfer)(const CanardInstance* ins,
                                           uint16_t              port_id,
                                           CanardTransferKind    transfer_kind,
                                           uint8_t               source_node_id);

/**
 * This function will be invoked by the library every time a transfer is successfully received.
 * If the application needs to send another transfer from this callback, it is highly recommended
 * to call @ref canardReleaseRxTransferPayload() first, so that the memory that was used for the block
 * buffer can be released and re-used by the TX queue.
 * @param ins       Library instance.
 * @param transfer  Pointer to the temporary transfer object. Invalidated upon return.
 */
typedef void (*CanardOnTransferReception)(CanardInstance* ins, CanardRxTransfer* transfer);

/**
 * This structure provides the usage statistics of the memory pool allocator.
 * It indicates whether the allocated memory is sufficient for the application.
 */
typedef struct
{
    uint16_t capacity_blocks;       ///< Pool capacity in number of blocks
    uint16_t current_usage_blocks;  ///< Number of blocks that are currently allocated by the library
    uint16_t peak_usage_blocks;     ///< Maximum number of blocks used since initialization
} CanardPoolAllocatorStatistics;

typedef union CanardPoolAllocatorBlock_u
{
    char                              bytes[CANARD_MEM_BLOCK_SIZE];
    union CanardPoolAllocatorBlock_u* next;
} CanardPoolAllocatorBlock;

typedef struct
{
    CanardPoolAllocatorBlock*     free_list;
    CanardPoolAllocatorStatistics statistics;
} CanardPoolAllocator;

typedef struct CanardBufferBlock
{
    struct CanardBufferBlock* next;
    uint8_t                   data[];
} CanardBufferBlock;

struct CanardRxState
{
    struct CanardRxState* next;

    CanardBufferBlock* buffer_blocks;

    uint64_t timestamp_usec;

    const uint32_t session_specifier;

    // We're using plain 'unsigned' here, because C99 doesn't permit explicit field type specification
    unsigned calculated_crc : 16;
    unsigned payload_len : CANARD_TRANSFER_PAYLOAD_LEN_BITS;
    unsigned transfer_id : 5;
    unsigned next_toggle : 1;  // 16+10+5+1 = 32, aligned.

    uint16_t payload_crc;

    uint8_t buffer_head[];
};
CANARD_STATIC_ASSERT(offsetof(CanardRxState, buffer_head) <= 28, "Invalid memory layout");
CANARD_STATIC_ASSERT(CANARD_MULTIFRAME_RX_PAYLOAD_HEAD_SIZE >= 4, "Invalid memory layout");

/**
 * This is the core structure that keeps all of the states and allocated resources of the library instance.
 * The application should never access any of the fields directly! Instead, the API functions should be used.
 */
struct CanardInstance
{
    uint8_t node_id;  ///< Local node-ID; may be zero if the node is anonymous

    CanardShouldAcceptTransfer should_accept;  ///< Function to decide whether the application wants this transfer
    CanardOnTransferReception  on_reception;   ///< Function the library calls after RX transfer is complete

    CanardPoolAllocator allocator;  ///< Pool allocator

    CanardRxState*     rx_states;  ///< RX transfer states
    CanardTxQueueItem* tx_queue;   ///< TX frames awaiting transmission

    void* user_reference;  ///< User pointer that can link this instance with other objects
};

/**
 * This structure represents a received transfer for the application.
 * An instance of it is passed to the application via the callback when a new transfer is received.
 * Pointers to the structure and all its fields are invalidated after the callback returns.
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
     * Otherwise it is advised to use canardDecodePrimitive().
     */
    const uint8_t* payload_head;        ///< Always valid, i.e. not NULL.
                                        ///< For multi frame transfers, the maximum size is defined in the constant
                                        ///< CANARD_MULTIFRAME_RX_PAYLOAD_HEAD_SIZE.
                                        ///< For single-frame transfers, the size is defined in the
                                        ///< field payload_len.
    CanardBufferBlock* payload_middle;  ///< May be NULL if the buffer was not needed. Always NULL for single-frame
                                        ///< transfers.
    const uint8_t* payload_tail;        ///< Last bytes of multi-frame transfers. Always NULL for single-frame
                                        ///< transfers.
    uint16_t payload_len;               ///< Effective length of the payload in bytes.

    /**
     * These fields identify the transfer for the application.
     */
    uint16_t port_id;         ///< 0 to 511 for services, 0 to 32767 for messages
    uint8_t  transfer_kind;   ///< See CanardTransferKind
    uint8_t  transfer_id;     ///< 0 to 31
    uint8_t  priority;        ///< 0 to 7
    uint8_t  source_node_id;  ///< 0 to 127, or 255 if the source is anonymous
};

/**
 * Initialize a library instance. The local node-ID will be initialized as 255, i.e. anonymous by default.
 *
 * Typically, the size of the memory pool should not be less than 1K, although it depends on the application.
 * The recommended way to detect the required pool size is to measure the peak pool usage after a stress-test.
 * Refer to the function @ref canardGetPoolAllocatorStatistics().
 *
 * @param out_ins           Uninitialized library instance.
 * @param mem_arena         Raw memory chunk used for dynamic allocation.
 * @param mem_arena_size    Size of the above, in bytes.
 * @param on_reception      Callback, see @ref CanardOnTransferReception.
 * @param should_accept     Callback, see @ref CanardShouldAcceptTransfer.
 * @param user_reference    Optional application-defined pointer; NULL if not needed.
 */
void canardInit(CanardInstance*            out_ins,
                void*                      mem_arena,
                size_t                     mem_arena_size,
                CanardOnTransferReception  on_reception,
                CanardShouldAcceptTransfer should_accept,
                void*                      user_reference);

/**
 * Read the value of the user pointer.
 * The user pointer is configured once during initialization.
 * It can be used to store references to any user-specific data, or to link the instance object with C++ objects.
 * @returns The application-defined pointer.
 */
void* canardGetUserReference(CanardInstance* ins);

/**
 * Assign a new node-ID value to the current node. Node-ID can be assigned only once.
 * @returns CANARD_OK on success; returns negative error code if the node-ID is outside of the valid range or is already
 * configured.
 */
int16_t canardSetLocalNodeID(CanardInstance* ins, uint8_t self_node_id);

/**
 * @returns Node-ID of the local node; 255 (broadcast) if the node-ID is not set, i.e. if the local node is anonymous.
 */
uint8_t canardGetLocalNodeID(const CanardInstance* ins);

/**
 * Send a broadcast message transfer. If the local node is anonymous, only single frame transfers are be allowed.
 *
 * The pointer to the transfer-ID shall point to a persistent variable (e.g. static or heap-allocated, not on the
 * stack); it will be updated by the library after every transmission. The transfer-ID value cannot be shared
 * between different sessions! Read the transport layer specification for details.
 *
 * @param ins               Library instance.
 * @param subject_id        Refer to the specification.
 * @param inout_transfer_id Pointer to a persistent variable containing the transfer-ID.
 * @param priority          Refer to CANARD_TRANSFER_PRIORITY_*.
 * @param payload           Transfer payload -- the serialized DSDL object.
 * @param payload_len       Length of the above, in bytes.
 * @returns The number of frames enqueued, or a negative error code.
 */
int16_t canardPublishMessage(CanardInstance* ins,
                             uint16_t        subject_id,
                             uint8_t*        inout_transfer_id,
                             uint8_t         priority,
                             const void*     payload,
                             uint16_t        payload_len);

/**
 * Send a request or a response transfer. Fails if the local node is anonymous.
 *
 * For request transfers, the pointer to the transfer-ID shall point to a persistent variable (e.g., static- or
 * heap-allocated, not on the stack); it will be updated by the library after every request. The transfer-ID value
 * cannot be shared between different sessions! Read the transport layer specification for details.
 *
 * For response transfers, the transfer-ID pointer shall point to the transfer_id field of the request transfer
 * structure @ref CanardRxTransfer.
 *
 * @param ins                   Library instance.
 * @param destination_node_id   Node-ID of the server/client.
 * @param service_id            Refer to the specification.
 * @param inout_transfer_id     Pointer to a persistent variable containing the transfer-ID.
 * @param priority              Refer to definitions CANARD_TRANSFER_PRIORITY_*.
 * @param kind                  Refer to CanardRequestResponse.
 * @param payload               Transfer payload -- the serialized DSDL object.
 * @param payload_len           Length of the above, in bytes.
 * @returns The number of frames enqueued, or a negative error code.
 */
int16_t canardRequestOrRespond(CanardInstance*       ins,
                               uint8_t               destination_node_id,
                               uint16_t              service_id,
                               uint8_t*              inout_transfer_id,
                               uint8_t               priority,
                               CanardRequestResponse kind,
                               const void*           payload,
                               uint16_t              payload_len);

/**
 * The application will call this function after @ref canardPublishMessage() or its service counterpart to transmit the
 * generated frames over the CAN bus.
 *
 * @returns A pointer to the top-priority frame in the TX queue; or NULL if the TX queue is empty.
 */
const CanardCANFrame* canardPeekTxQueue(const CanardInstance* ins);

/**
 * Remove the top priority frame from the TX queue.
 * The application will call this function after @ref canardPeekTxQueue() once the obtained frame has been processed.
 * Calling @ref canardPublishMessage() or its service counterpart between @ref canardPeekTxQueue() and this function
 * is NOT allowed, because it may change the frame at the top of the TX queue.
 */
void canardPopTxQueue(CanardInstance* ins);

/**
 * Process a received CAN frame with a timestamp.
 * The application will call this function upon reception of a new frame from the CAN bus.
 *
 * @param ins               Library instance.
 * @param frame             The received CAN frame.
 * @param timestamp_usec    The timestamp. The time system may be arbitrary as long as the clock is monotonic (steady).
 * @returns Zero or a negative error code.
 */
int16_t canardHandleRxFrame(CanardInstance* ins, const CanardCANFrame* frame, uint64_t timestamp_usec);

/**
 * This function may be used to extract values from received UAVCAN transfers. It decodes a scalar value --
 * boolean, integer, character, or floating point -- from the specified bit position in the RX transfer buffer.
 * Simple single-frame transfers can also be parsed manually instead of using this function.
 *
 * Caveat: This function works correctly only on platforms that use two's complement signed integer representation.
 * I am not aware of any modern microarchitecture that uses anything else than two's complement, so it should not
 * limit the portability.
 *
 * The type of the value pointed to by 'out_value' is defined as follows:
 *
 *  | bit_length | value_is_signed | out_value points to                      |
 *  |------------|-----------------|------------------------------------------|
 *  | 1          | false           | bool (may be incompatible with uint8_t!) |
 *  | 1          | true            | N/A                                      |
 *  | [2, 8]     | false           | uint8_t, or char                         |
 *  | [2, 8]     | true            | int8_t, or char                          |
 *  | [9, 16]    | false           | uint16_t                                 |
 *  | [9, 16]    | true            | int16_t                                  |
 *  | [17, 32]   | false           | uint32_t                                 |
 *  | [17, 32]   | true            | int32_t, or 32-bit float                 |
 *  | [33, 64]   | false           | uint64_t                                 |
 *  | [33, 64]   | true            | int64_t, or 64-bit float                 |
 *
 * @param transfer          The RX transfer where the data will be read from.
 * @param bit_offset        Offset, in bits, from the beginning of the transfer payload.
 * @param bit_length        Length of the value, in bits; see the table.
 * @param value_is_signed   True if the value can be negative (i.e., sign bit extension is needed); see the table.
 * @param out_value         Pointer to the output storage; see the table.
 * @returns Same as bit_length, or a negated error code, such as invalid argument.
 */
int16_t canardDecodePrimitive(const CanardRxTransfer* transfer,
                              uint32_t                bit_offset,
                              uint8_t                 bit_length,
                              bool                    value_is_signed,
                              void*                   out_value);

/**
 * This function may be used to encode values for later transmission in a UAVCAN transfer. It encodes a scalar value
 * -- boolean, integer, character, or floating point -- and puts it at the specified bit offset in the specified
 * contiguous buffer. Simple payloads can also be encoded manually instead of using this function.
 *
 * Caveat: This function works correctly only on platforms that use two's complement signed integer representation.
 * I am not aware of any modern microarchitecture that uses anything else than two's complement, so it should not
 * limit the portability.
 *
 * The type of the value pointed to by 'value' is defined as follows:
 *
 *  | bit_length | value points to                          |
 *  |------------|------------------------------------------|
 *  | 1          | bool (may be incompatible with uint8_t!) |
 *  | [2, 8]     | uint8_t, int8_t, or char                 |
 *  | [9, 16]    | uint16_t, int16_t                        |
 *  | [17, 32]   | uint32_t, int32_t, or 32-bit float       |
 *  | [33, 64]   | uint64_t, int64_t, or 64-bit float       |
 *
 * @param destination   Destination buffer where the result will be stored.
 * @param bit_offset    Offset, in bits, from the beginning of the destination buffer.
 * @param bit_length    Length of the value, in bits; see the table.
 * @param value         Pointer to the value; see the table.
 */
void canardEncodePrimitive(void* destination, uint32_t bit_offset, uint8_t bit_length, const void* value);

/**
 * This function may be invoked by the application to release the memory allocated for the received transfer payload.
 *
 * If the application needs to send new transfers from the transfer reception callback, this function should be
 * invoked right before calling @ref canardPublishMessage() or its service counterpart.
 * Not doing that may lead to a higher worst-case memory pool utilization.
 *
 * Failure to call this function will NOT lead to a memory leak because the library checks for it.
 */
void canardReleaseRxTransferPayload(CanardInstance* ins, CanardRxTransfer* transfer);

/**
 * Use this function to determine the worst case memory footprint of your application.
 * See @ref CanardPoolAllocatorStatistics.
 * @returns a copy of the pool allocator usage statistics.
 */
CanardPoolAllocatorStatistics canardGetPoolAllocatorStatistics(CanardInstance* ins);

/**
 * IEEE 754 binary16 marshaling helpers.
 * These functions convert between the native float and the standard IEEE 754 binary16 float (a.k.a. half precision).
 * It is assumed that the native float is IEEE 754 binary32, otherwise, the results may be unpredictable.
 * Majority of modern computers and microcontrollers use IEEE 754, so this limitation should not limit the portability.
 */
uint16_t canardConvertNativeFloatToFloat16(float value);
float    canardConvertFloat16ToNativeFloat(uint16_t value);

// Static checks.
CANARD_STATIC_ASSERT((((uint32_t) CANARD_MULTIFRAME_RX_PAYLOAD_HEAD_SIZE) < 32) &&
                         (((uint32_t) CANARD_MULTIFRAME_RX_PAYLOAD_HEAD_SIZE) >= 6),
                     "Platforms where sizeof(void*) > 4 are not supported. "
                     "On AMD64 use 32-bit mode (e.g. GCC flag -m32).");
CANARD_STATIC_ASSERT(sizeof(float) == 4, "Native float format shall match IEEE 754 binary32");

#ifdef __cplusplus
}
#endif
#endif
