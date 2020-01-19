// Copyright (c) 2016-2020 UAVCAN Development Team
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Contributors: https://github.com/UAVCAN/libcanard/contributors

#ifndef CANARD_H_INCLUDED
#define CANARD_H_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------- BUILD CONFIGURATION ----------------------------------------

/// Build configuration header. Use it to provide your overrides.
/// Alternatively, you can specify each option individually in the preprocessor flags when invoking the compiler.
#if defined(CANARD_CONFIG_HEADER_AVAILABLE) && CANARD_CONFIG_HEADER_AVAILABLE
#    include "canard_build_config.h"
#endif

/// By default, the library is built to support all versions of CAN.
/// The downside of such flexibility is that it increases the memory footprint of the library.
/// If the available CAN hardware supports only Classic CAN, then it might make sense to limit the maximum transmission
/// unit (MTU) supported by the library to reduce the memory footprint.
#ifndef CANARD_CONFIG_MTU_MAX
#    define CANARD_CONFIG_MTU_MAX CANARD_MTU_CAN_FD
#endif

/// By default, this macro resolves to the standard assert(). The user can redefine this if necessary.
#ifndef CANARD_ASSERT
#    define CANARD_ASSERT(x) assert(x)
#endif

/// By default, this macro expands to the standard static_assert() if supported by the language (C11, C++11, or newer).
/// The user can redefine this if necessary.
#ifndef CANARD_STATIC_ASSERT
#    if (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)) || \
        (defined(__cplusplus) && (__cplusplus >= 201103L))
#        define CANARD_STATIC_ASSERT(...) static_assert(__VA_ARGS__)
#    else
#        define CANARD_STATIC_ASSERT(x, ...) typedef char CANARD_GLUE(_static_assertion_, __LINE__)[(x) ? 1 : -1]
#    endif
#endif

// ---------------------------------------- CONSTANTS ----------------------------------------

/// Semantic version numbers of this library (not the UAVCAN specification).
/// API will be backward compatible within the same major version.
#define CANARD_VERSION_MAJOR 1
#define CANARD_VERSION_MINOR 0

/// The version number of the UAVCAN specification implemented by this library.
#define CANARD_UAVCAN_SPECIFICATION_VERSION_MAJOR 1

/// Error code definitions; inverse of these values may be returned from API calls.
#define CANARD_OK 0
// Value 1 is omitted intentionally since -1 is often used in 3rd party code
#define CANARD_ERROR_INVALID_ARGUMENT 2
#define CANARD_ERROR_OUT_OF_MEMORY 3
#define CANARD_ERROR_NODE_ID_NOT_SET 4
#define CANARD_ERROR_INTERNAL 9

/// MTU values for supported protocols.
/// Per the recommendations given in the UAVCAN specification, other MTU values should not be used.
#define CANARD_MTU_CAN_CLASSIC 8U
#define CANARD_MTU_CAN_FD 64U

#if CANARD_CONFIG_MTU_MAX == CANARD_MTU_CAN_FD
#elif CANARD_CONFIG_MTU_MAX == CANARD_MTU_CAN_CLASSIC
#else
#    error "CANARD_CONFIG_MTU_MAX is invalid: one of the standard MTU values shall be used (8, 64...)"
#endif
#define CANARD_MTU_RUNTIME_CONFIGURABLE (CANARD_CONFIG_MTU_MAX > CANARD_MTU_CAN_CLASSIC)

/// Parameter ranges are inclusive; the lower bound is zero for all. Refer to the specification for more info.
#define CANARD_SUBJECT_ID_MAX 32767U
#define CANARD_SERVICE_ID_MAX 511U
#define CANARD_NODE_ID_MAX 127U
#define CANARD_TRANSFER_ID_BIT_LENGTH 5U
#define CANARD_TRANSFER_ID_MAX ((1U << CANARD_TRANSFER_ID_BIT_LENGTH) - 1U)

/// This value represents an undefined node-ID: broadcast destination or anonymous source.
/// Library functions treat all values above @ref CANARD_NODE_ID_MAX as anonymous.
#define CANARD_NODE_ID_UNSET 255U

/// If not specified, the transfer-ID timeout will take this value for all new input sessions.
#define CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC 2000000UL

// ---------------------------------------- TYPES ----------------------------------------

// Forward declarations.
typedef struct CanardInstance         CanardInstance;
typedef struct CanardReceivedTransfer CanardReceivedTransfer;

// These forward declarations are not a part of the library API, but they need to be here due to the limitations of C.
typedef struct CanardInternalInputSession CanardInternalInputSession;
typedef struct CanardInternalTxQueueItem  CanardInternalTxQueueItem;

/// Transfer priority level mnemonics per the recommendations given in the UAVCAN specification.
typedef enum
{
    CanardPriorityExceptional = 0,
    CanardPriorityImmediate   = 1,
    CanardPriorityFast        = 2,
    CanardPriorityHigh        = 3,
    CanardPriorityNominal     = 4,
    CanardPriorityLow         = 5,
    CanardPrioritySlow        = 6,
    CanardPriorityOptional    = 7,
} CanardPriority;

/// CAN data frame with an extended 29-bit ID.
typedef struct
{
    uint32_t id;                           ///< 29-bit extended ID. The bits above 29-th are ignored.
    uint8_t  data_length;                  ///< The amount of useful data in the frame. Not to be confused with DLC!
    uint8_t  data[CANARD_CONFIG_MTU_MAX];  ///< The payload capacity depends on the compile-time MTU setting.
} CanardCANFrame;

/// Transfer kinds are defined by the UAVCAN specification.
typedef enum
{
    CanardTransferKindMessagePublication = 0,  ///< Broadcast, from publisher to all subscribers.
    CanardTransferKindServiceResponse    = 1,  ///< Point-to-point, from server to client.
    CanardTransferKindServiceRequest     = 2   ///< Point-to-point, from client to server.
} CanardTransferKind;

/// The application supplies the library with this information when a new transfer should be received.
typedef struct
{
    bool     should_accept;
    uint32_t transfer_id_timeout_usec;
    size_t   payload_capacity;
} CanardTransferReceptionParameters;

/// The application shall implement this function and supply a pointer to it to the library during initialization.
/// The library calls this function to determine whether a transfer should be received.
/// @param ins            Library instance.
/// @param port_id        Subject-ID or service-ID of the transfer.
/// @param transfer_kind  Message or service transfer.
/// @param source_node_id Node-ID of the origin; broadcast if anonymous.
/// @returns @ref CanardTransferReceptionParameters.
typedef CanardTransferReceptionParameters (*CanardShouldAcceptTransfer)(const CanardInstance* ins,
                                                                        uint16_t              port_id,
                                                                        CanardTransferKind    transfer_kind,
                                                                        uint8_t               source_node_id);

/// This function will be invoked by the library every time a transfer is successfully received.
/// If the application needs to send another transfer from this callback, it is highly recommended
/// to call @ref canardReleaseReceivedTransferPayload() first, so that the memory that was used for the block
/// buffer can be released and re-used by the TX queue.
/// @param ins       Library instance.
/// @param transfer  Pointer to the temporary transfer object. Invalidated upon return.
typedef void (*CanardProcessReceivedTransfer)(CanardInstance* ins, CanardReceivedTransfer* transfer);

/// This structure provides the usage statistics of the dynamic memory allocator.
/// It can be used to determine whether the allocated memory is sufficient for the application.
typedef struct
{
    // TODO
} CanardMemoryAllocatorStatistics;

typedef struct
{
    // TODO
    CanardMemoryAllocatorStatistics statistics;
} CanardInternalMemoryAllocator;

/// This is the core structure that keeps all of the states and allocated resources of the library instance.
/// The application should never access any of the fields directly! Instead, the API functions should be used.
struct CanardInstance
{
    const CanardShouldAcceptTransfer    should_accept_transfer;     ///< Transfer acceptance predicate.
    const CanardProcessReceivedTransfer process_received_transfer;  ///< Received transfer handler.

    CanardInternalMemoryAllocator allocator;       ///< Deterministic constant-complexity dynamic memory allocator.
    CanardInternalInputSession*   input_sessions;  ///< Rx session states.
    CanardInternalTxQueueItem*    tx_queue;        ///< TX frames awaiting transmission.

    void* const user_reference;  ///< User pointer that can link this instance with other objects

    uint8_t node_id;  ///< Local node-ID or @ref CANARD_NODE_ID_UNSET.

#if CANARD_MTU_RUNTIME_CONFIGURABLE
    uint8_t mtu_bytes;  ///< Maximum number of data bytes per CAN frame. Range: [8, CANARD_CONFIG_MTU_MAX].
#endif
};

/// This structure represents a received transfer for the application.
/// An instance of it is passed to the application via the callback when a new transfer is received.
/// Pointers to the structure and all its fields are invalidated after the callback returns.
struct CanardReceivedTransfer
{
    /// Timestamp at which the first frame of this transfer was received.
    const uint64_t timestamp_usec;

    const uint8_t* const payload;
    const size_t         payload_length;

    const CanardPriority     priority;
    const CanardTransferKind transfer_kind;
    const uint16_t           port_id;         ///< Subject-ID or service-ID.
    const uint8_t            source_node_id;  ///< For anonymous transfers it's @ref CANARD_NODE_ID_UNSET.
    const uint8_t            transfer_id;     ///< Bits above @ref CANARD_TRANSFER_ID_BIT_LENGTH are always zero.
};

/// Initialize a library instance.
/// The local node-ID will be initialized as @ref CANARD_NODE_ID_UNSET, i.e. anonymous by default.
///
/// The size of the memory arena should be an integer power of two; otherwise, the size may be rounded down.
/// Typically, the size of the memory arena should not be less than 8 KiB, although it depends on the application.
/// The recommended way to detect the required memory size is to measure the peak pool usage after a stress-test.
/// Refer to the function @ref canardGetPoolAllocatorStatistics().
///
/// @param memory_arena              Raw memory chunk used for the deterministic dynamic memory allocator.
/// @param memory_arena_size         Size of the above, in bytes.
/// @param should_accept_transfer    Callback, see @ref CanardShouldAcceptTransfer.
/// @param process_received_transfer Callback, see @ref CanardOnTransferReception.
/// @param user_reference            Optional application-defined pointer; NULL if not needed.
CanardInstance canardInit(void* const                         memory_arena,
                          const size_t                        memory_arena_size,
                          const CanardShouldAcceptTransfer    should_accept_transfer,
                          const CanardProcessReceivedTransfer process_received_transfer,
                          void* const                         user_reference);

/// Read the value of the user pointer.
/// The user pointer is configured once during initialization.
/// It can be used to store references to any user-specific data, or to link the instance object with C++ objects.
/// @returns The application-defined pointer.
void* canardGetUserReference(const CanardInstance* const ins);

/// Assign a new node-ID value to the current node. Node-ID can be assigned only once.
/// If the supplied value is invalid or the node-ID is already configured, nothing will be done.
void canardSetLocalNodeID(CanardInstance* const ins, const uint8_t self_node_id);

/// @returns Node-ID of the local node; 255 (broadcast) if the node-ID is not set, i.e. if the local node is anonymous.
uint8_t canardGetLocalNodeID(const CanardInstance* const ins);

#if CANARD_MTU_RUNTIME_CONFIGURABLE
/// Configure the maximum transmission unit. This can be done as many times as needed.
/// This setting defines the maximum number of bytes per CAN data frame in all outgoing transfers.
/// Regardless of this setting, CAN frames with any MTU up to CANARD_CONFIG_MTU_MAX bytes can always be accepted.
///
/// Only the standard values should be used as recommended by the specification (8, 64 bytes);
/// otherwise, interoperability issues may arise. See "CANARD_MTU_*".
///
/// Range: [8, CANARD_CONFIG_MTU_MAX]. The default is CANARD_CONFIG_MTU_MAX (i.e., the maximum).
/// Invalid values are rounded to the nearest valid value.
void canardSetMTU(const CanardInstance* const ins, const uint8_t mtu_bytes);
#endif

/// Send a broadcast message transfer. If the local node is anonymous, only single frame transfers are be allowed.
///
/// The pointer to the transfer-ID shall point to a persistent variable (e.g. static or heap-allocated, not on the
/// stack); it will be updated by the library after every transmission. The transfer-ID value cannot be shared
/// between different sessions! Read the transport layer specification for details.
///
/// @param ins               Library instance.
/// @param subject_id        Refer to the specification.
/// @param inout_transfer_id Pointer to a persistent variable containing the transfer-ID.
/// @param priority          Refer to CANARD_TRANSFER_PRIORITY_*.
/// @param payload           Transfer payload -- the serialized DSDL object.
/// @param payload_len       Length of the above, in bytes.
/// @returns The number of frames enqueued, or a negative error code.
int32_t canardPublishMessage(CanardInstance* const ins,
                             const uint16_t        subject_id,
                             uint8_t* const        inout_transfer_id,
                             const uint8_t         priority,
                             const void* const     payload,
                             const size_t          payload_length,
                             uint64_t              deadline_usec);

/// Send a request transfer. Fails if the local node is anonymous.
///
/// The pointer to the transfer-ID shall point to a persistent variable (e.g., static- or heap-allocated,
/// not on the stack); it will be updated by the library after every request. The transfer-ID value
/// cannot be shared between different sessions! Read the transport layer specification for details.
///
/// @param ins                   Library instance.
/// @param destination_node_id   Node-ID of the destination server.
/// @param service_id            Refer to the specification.
/// @param inout_transfer_id     Pointer to a persistent variable containing the transfer-ID.
/// @param priority              @ref CanardPriority.
/// @param payload               Transfer payload -- the serialized DSDL object.
/// @param payload_length        Length of the above, in bytes.
/// @returns The number of frames enqueued, or a negative error code.
int32_t canardSendRequest(CanardInstance* const ins,
                          const uint8_t         server_node_id,
                          const uint16_t        service_id,
                          uint8_t* const        inout_transfer_id,
                          const CanardPriority  priority,
                          const void* const     payload,
                          const size_t          payload_length,
                          uint64_t              deadline_usec);

/// Send a response transfer. Fails if the local node is anonymous.
///
/// The transfer-ID shall be the same as in the corresponding service request.
///
/// @param ins                   Library instance.
/// @param destination_node_id   Node-ID of the destination client.
/// @param service_id            Refer to the specification.
/// @param transfer_id           Same as in the original request transfer.
/// @param priority              @ref CanardPriority.
/// @param payload               Transfer payload -- the serialized DSDL object.
/// @param payload_length        Length of the above, in bytes.
/// @returns The number of frames enqueued, or a negative error code.
int32_t canardSendResponse(CanardInstance* const ins,
                           const uint8_t         client_node_id,
                           const uint16_t        service_id,
                           const uint8_t         transfer_id,
                           const CanardPriority  priority,
                           const void* const     payload,
                           const size_t          payload_length,
                           uint64_t              deadline_usec);

/// The application will call this function after @ref canardPublishMessage() or its service counterpart to transmit the
/// generated frames over the CAN bus.
///
/// @returns A pointer to the top-priority frame in the TX queue; or NULL if the TX queue is empty.
const CanardCANFrame* canardPeekTxQueue(const CanardInstance* const ins, uint64_t* const out_deadline_usec);

/// Remove the top priority frame from the TX queue.
/// The application will call this function after @ref canardPeekTxQueue() once the obtained frame has been processed.
/// Calling @ref canardPublishMessage() or its service counterpart between @ref canardPeekTxQueue() and this function
/// is NOT allowed, because it may change the frame at the top of the TX queue.
void canardPopTxQueue(CanardInstance* const ins);

typedef enum
{
    CanardReceivedFrameProcessingResultOK                    = CANARD_OK,
    CanardReceivedFrameProcessingResultWrongFormat           = 1,
    CanardReceivedFrameProcessingResultWrongDestination      = 2,
    CanardReceivedFrameProcessingResultWrongToggle           = 3,
    CanardReceivedFrameProcessingResultWrongTransferID       = 4,
    CanardReceivedFrameProcessingResultMissedStartOfTransfer = 6,
    CanardReceivedFrameProcessingResultCRCMismatch           = 7
} CanardReceivedFrameProcessingResult;

/// Process a received CAN frame with a timestamp.
/// The application will call this function upon reception of a new frame from the CAN bus.
///
/// @param ins               Library instance.
/// @param frame             The received CAN frame.
/// @param timestamp_usec    The timestamp. The time system may be arbitrary as long as the clock is monotonic (steady).
/// @returns Zero if accepted; negative error code; or a value from @ref CanardReceivedFrameProcessingResult.
int8_t canardProcessReceivedFrame(CanardInstance* const       ins,
                                  const CanardCANFrame* const frame,
                                  const uint64_t              timestamp_usec);

/// This function may be used to extract values from received UAVCAN transfers. It decodes a scalar value --
/// boolean, integer, character, or floating point -- from the specified bit position in the RX transfer buffer.
/// Simple single-frame transfers can also be parsed manually instead of using this function.
///
/// Caveat: This function works correctly only on platforms that use two's complement signed integer representation.
/// I am not aware of any modern microarchitecture that uses anything else than two's complement, so it should not
/// limit the portability.
///
/// The type of the value pointed to by 'out_value' is defined as follows:
///
///  | bit_length | value_is_signed | out_value points to                      |
///  |------------|-----------------|------------------------------------------|
///  | 1          | false           | bool (may be incompatible with uint8_t!) |
///  | 1          | true            | N/A                                      |
///  | [2, 8]     | false           | uint8_t, or char                         |
///  | [2, 8]     | true            | int8_t, or char                          |
///  | [9, 16]    | false           | uint16_t                                 |
///  | [9, 16]    | true            | int16_t                                  |
///  | [17, 32]   | false           | uint32_t                                 |
///  | [17, 32]   | true            | int32_t, or 32-bit float IEEE 754        |
///  | [33, 64]   | false           | uint64_t                                 |
///  | [33, 64]   | true            | int64_t, or 64-bit float IEEE 754        |
///
/// @param transfer          The RX transfer where the data will be read from.
/// @param bit_offset        Offset, in bits, from the beginning of the transfer payload.
/// @param bit_length        Length of the value, in bits; see the table.
/// @param value_is_signed   True if the value can be negative (i.e., sign bit extension is needed); see the table.
/// @param out_value         Pointer to the output storage; see the table.
void canardDeserializePrimitive(const CanardReceivedTransfer* const transfer,
                                const size_t                        bit_offset,
                                const uint8_t                       bit_length,
                                const bool                          value_is_signed,
                                void* const                         out_value);

/// This function may be used to encode values for later transmission in a UAVCAN transfer. It encodes a scalar value
/// -- boolean, integer, character, or floating point -- and puts it at the specified bit offset in the specified
/// contiguous buffer. Simple payloads can also be encoded manually instead of using this function.
///
/// Caveat: This function works correctly only on platforms that use two's complement signed integer representation.
/// I am not aware of any modern microarchitecture that uses anything else than two's complement, so it should not
/// limit the portability.
///
/// The type of the value pointed to by 'value' is defined as follows:
///
///  | bit_length | value points to                          |
///  |------------|------------------------------------------|
///  | 1          | bool (may be incompatible with uint8_t!) |
///  | [2, 8]     | uint8_t, int8_t, or char                 |
///  | [9, 16]    | uint16_t, int16_t                        |
///  | [17, 32]   | uint32_t, int32_t, or 32-bit float       |
///  | [33, 64]   | uint64_t, int64_t, or 64-bit float       |
///
/// @param destination   Destination buffer where the result will be stored.
/// @param bit_offset    Offset, in bits, from the beginning of the destination buffer.
/// @param bit_length    Length of the value, in bits; see the table.
/// @param value         Pointer to the value; see the table.
void canardSerializePrimitive(void* const       destination,
                              const size_t      bit_offset,
                              const uint8_t     bit_length,
                              const void* const value);

/// This function may be invoked by the application to release the memory allocated for the received transfer payload.
///
/// If the application needs to send new transfers from the transfer reception callback, this function should be
/// invoked right before calling @ref canardPublishMessage() or its service counterpart.
/// Not doing that may lead to a higher worst-case dynamic memory utilization.
///
/// Failure to call this function will NOT lead to a memory leak because the library checks for it.
void canardReleaseReceivedTransferPayload(CanardInstance* const ins, CanardReceivedTransfer* const transfer);

/// Use this function to determine the worst case memory footprint of your application.
/// See @ref CanardPoolAllocatorStatistics.
/// @returns a copy of the pool allocator usage statistics.
CanardMemoryAllocatorStatistics canardGetMemoryAllocatorStatistics(const CanardInstance* const ins);

/// IEEE 754 binary16 marshaling helpers.
/// These functions convert between the native float and the standard IEEE 754 binary16 float (a.k.a. half precision).
/// It is assumed that the native float is IEEE 754 binary32, otherwise, the results may be unpredictable.
/// Majority of modern computers and microcontrollers use IEEE 754, so this limitation should not limit the portability.
uint16_t canardConvertNativeFloatToFloat16(float value);
float    canardConvertFloat16ToNativeFloat(uint16_t value);

#define CANARD_GLUE(a, b) CANARD_GLUE_IMPL_(a, b)
#define CANARD_GLUE_IMPL_(a, b) a##b

#ifdef __cplusplus
}
#endif
#endif
