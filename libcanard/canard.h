// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.
// Contributors: https://github.com/UAVCAN/libcanard/contributors.
// READ THE DOCUMENTATION IN README.md.

#ifndef CANARD_H_INCLUDED
#define CANARD_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Semantic version of this library (not the UAVCAN specification).
/// API will be backward compatible within the same major version.
#define CANARD_VERSION_MAJOR 1

/// The version number of the UAVCAN specification implemented by this library.
#define CANARD_UAVCAN_SPECIFICATION_VERSION_MAJOR 1

/// These error codes may be returned from the library API calls whose return type is a signed integer
/// in the negated form (i.e., code 2 returned as -2).
/// API calls whose return type is not a signer integer cannot fail by contract.
/// No other error states may occur in the library.
/// By contract, a deterministic application with a properly sized heap will never encounter any of the listed errors.
/// The error code 1 is not used because -1 is often used as a generic error code in 3rd-party code.
#define CANARD_ERROR_INVALID_ARGUMENT 2
#define CANARD_ERROR_OUT_OF_MEMORY 3

/// MTU values for supported protocols.
/// Per the recommendations given in the UAVCAN specification, other MTU values should not be used.
#define CANARD_MTU_CAN_CLASSIC 8U
#define CANARD_MTU_CAN_FD 64U

/// Parameter ranges are inclusive; the lower bound is zero for all. Refer to the specification for more info.
#define CANARD_SUBJECT_ID_MAX 32767U
#define CANARD_SERVICE_ID_MAX 511U
#define CANARD_NODE_ID_MAX 127U
#define CANARD_PRIORITY_MAX 7U
#define CANARD_TRANSFER_ID_BIT_LENGTH 5U
#define CANARD_TRANSFER_ID_MAX ((1U << CANARD_TRANSFER_ID_BIT_LENGTH) - 1U)

/// This value represents an undefined node-ID: broadcast destination or anonymous source.
/// Library functions treat all values above CANARD_NODE_ID_MAX as anonymous.
#define CANARD_NODE_ID_UNSET 255U

/// If not specified, the transfer-ID timeout will take this value for all new input sessions.
#define CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC 2000000UL

// Forward declarations.
typedef struct CanardInstance CanardInstance;

/// Transfer priority level mnemonics per the recommendations given in the UAVCAN specification.
typedef enum
{
    CanardPriorityExceptional = 0,
    CanardPriorityImmediate   = 1,
    CanardPriorityFast        = 2,
    CanardPriorityHigh        = 3,
    CanardPriorityNominal     = 4,  ///< Nominal priority level should be the default.
    CanardPriorityLow         = 5,
    CanardPrioritySlow        = 6,
    CanardPriorityOptional    = 7,
} CanardPriority;

/// Transfer kinds are defined by the UAVCAN specification.
typedef enum
{
    CanardTransferKindMessage  = 0,  ///< Multicast, from publisher to all subscribers.
    CanardTransferKindResponse = 1,  ///< Point-to-point, from server to client.
    CanardTransferKindRequest  = 2,  ///< Point-to-point, from client to server.
} CanardTransferKind;

/// CAN data frame with an extended 29-bit ID. RTR/Error frames are not used and therefore not modeled here.
typedef struct
{
    /// For RX frames: reception timestamp.
    /// For TX frames: transmission deadline.
    /// The time system may be arbitrary as long as the clock is monotonic (steady).
    uint64_t timestamp_usec;

    /// 29-bit extended ID. The bits above 29-th are zero/ignored.
    uint32_t extended_can_id;

    /// The useful data in the frame. The length value is not to be confused with DLC!
    size_t      payload_size;
    const void* payload;
} CanardCANFrame;

/// Conversion look-up tables between CAN DLC and data length.
extern const uint8_t CanardCANDLCToLength[16];
extern const uint8_t CanardCANLengthToDLC[65];

typedef struct
{
    /// For RX transfers: reception timestamp.
    /// For TX transfers: transmission deadline.
    /// The time system may be arbitrary as long as the clock is monotonic (steady).
    uint64_t timestamp_usec;

    CanardPriority priority;

    CanardTransferKind transfer_kind;

    /// Subject-ID for message publications; service-ID for service requests/responses.
    uint16_t port_id;

    /// For outgoing message transfers or for incoming anonymous transfers, the value is CANARD_NODE_ID_UNSET.
    uint8_t remote_node_id;

    uint8_t transfer_id;

    /// The const pointer makes it incompatible with free(), but we have to tolerate that due to the limitations of C.
    size_t      payload_size;
    const void* payload;
} CanardTransfer;

/// The application supplies the library with this information when a new transfer should be received.
typedef struct
{
    /// The transfer-ID timeout for this session.
    /// If no specific requirements are defined, the default CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC should be used.
    /// Zero timeout indicates that this transfer should not be received (all its frames will be silently dropped).
    uint64_t transfer_id_timeout_usec;

    /// The maximum payload size of the transfer (i.e., the maximum size of the serialized DSDL object), in bytes.
    /// Payloads larger than this will be silently truncated per the Implicit Truncation Rule defined in Specification.
    /// Per Specification, the transfer CRC of multi-frame transfers is always validated regardless of the
    /// implicit truncation rule.
    /// Zero is also a valid value indicating that the transfer shall be accepted but the payload need not be stored.
    size_t payload_size_max;
} CanardRxMetadata;

/// The application shall implement this function and supply a pointer to it to the library during initialization.
/// The library calls this function to determine whether a transfer should be received.
/// @param ins            Library instance.
/// @param port_id        Subject-ID or service-ID of the transfer.
/// @param transfer_kind  Message or service transfer.
/// @param source_node_id Node-ID of the origin; CANARD_NODE_ID_UNSET if anonymous.
/// @returns CanardTransferReceptionParameters.
typedef CanardRxMetadata (*CanardRxFilter)(const CanardInstance* ins,
                                           uint16_t              port_id,
                                           CanardTransferKind    transfer_kind,
                                           uint8_t               source_node_id);

typedef void* (*CanardHeapAllocate)(CanardInstance* ins, size_t amount);

/// Free as in freedom.
typedef void (*CanardHeapFree)(CanardInstance* ins, void* pointer);

/// This is the core structure that keeps all of the states and allocated resources of the library instance.
/// Fields whose names begin with an underscore SHALL NOT be accessed by the application,
/// they are for internal use only.
struct CanardInstance
{
    /// User pointer that can link this instance with other objects.
    /// This field can be changed arbitrarily, the library does not access it after initialization.
    /// The default value is NULL.
    void* user_reference;

    /// The maximum transmission unit. The value can be changed arbitrarily at any time.
    /// This setting defines the maximum number of bytes per CAN data frame in all outgoing transfers.
    /// Regardless of this setting, CAN frames with any MTU can always be accepted.
    ///
    /// Only the standard values should be used as recommended by the specification;
    /// otherwise, networking interoperability issues may arise. See "CANARD_MTU_*".
    /// Valid values are any valid CAN frame data length not smaller than 8. The default is the maximum valid value.
    /// Invalid values are treated as the nearest valid value.
    size_t mtu_bytes;

    /// The node-ID of the local node. The default value is CANARD_NODE_ID_UNSET.
    /// Per the UAVCAN Specification, the node-ID should not be assigned more than once.
    /// Invalid values are treated as CANARD_NODE_ID_UNSET.
    uint8_t node_id;

    /// Callbacks invoked by the library. See their type documentation for details.
    /// They SHALL be valid function pointers at all times.
    ///
    /// The time complexity parameters given in the API documentation are made on the assumption that
    /// the heap management functions (allocate and free) have constant complexity.
    ///
    /// There are only two API functions that may lead to allocation of heap memory:
    ///     - canardTxPush()
    ///     - canardRxAccept()
    /// Their exact memory requirement model is specified in their documentation.
    CanardHeapAllocate heap_allocate;
    CanardHeapFree     heap_free;
    CanardRxFilter     rx_filter;

    /// These fields are for internal use only. Do not access from the application.
    struct CanardInternalRxSession*   _rx_sessions;
    struct CanardInternalTxQueueItem* _tx_queue;
};

/// Initialize a new library instance.
/// The default values will be assigned as specified in the structure field documentation.
/// The time complexity parameters given in the API documentation are made on the assumption that the heap management
/// functions (allocate and free) have constant complexity.
/// If any of the pointers are NULL, the behavior is undefined.
CanardInstance canardInit(const CanardHeapAllocate heap_allocate,
                          const CanardHeapFree     heap_free,
                          const CanardRxFilter     rx_filter);

/// Serializes a transfer into a sequence of transport frames, and inserts them into the prioritized transmission
/// queue at the appropriate position. Afterwards, the application is supposed to take the enqueued frames from
/// the transmission queue using the function canardTxPeek() and transmit them. Each transmitted (or otherwise
/// discarded, e.g., due to timeout) frame should be removed from the queue using canardTxPop().
///
/// The MTU of the generated frames is dependent on the value of the MTU setting at the time when this function
/// is invoked. The MTU setting can be changed arbitrarily between invocations. No other functions rely on that
/// parameter.
///
/// The timestamp value of the transfer will be used to populate the timestamp values of the resulting transport
/// frames (so all frames will have the same timestamp value). This feature is intended to facilitate transmission
/// deadline tracking, i.e., aborting frames that could not be transmitted before the specified deadline.
/// Therefore, normally, the timestamp value should be in the future.
/// The library itself, however, does not use or check this value itself, so it can be zero if not needed.
///
/// It is the responsibility of the application to ensure that the transfer parameters are managed correctly.
/// In particular, the application shall ensure that the transfer-ID computation rules, as defined in the
/// UAVCAN Specification, are being followed; here is a relevant excerpt:
///     - For service response transfers the transfer-ID value shall be directly copied from the corresponding
///       service request transfer.
///     - A node that is interested in emitting message transfers or service request transfers under a particular
///       session specifier, whether periodically or on an ad-hoc basis, shall allocate a transfer-ID counter state
///       associated with said session specifier exclusively. The transfer-ID value of every emitted transfer is
///       determined by sampling the corresponding counter keyed by the session specifier of the transfer; afterwards,
///       the counter is incremented by one.
/// The recommended approach to storing the transfer-ID counters is to use static or member variables.
/// Sophisticated applications may find this approach unsuitable, in which case O(1) static look-up tables
/// or heap-allocated data structures should be considered.
///
/// Returns the number of frames enqueued into the prioritized TX queue (which is always a positive number)
/// in case of success. Returns a negated error code in case of failure. Zero cannot be returned.
///
/// An invalid argument error may be returned in the following cases:
///     - Any of the input arguments are NULL.
///     - The remote node-ID is not CANARD_NODE_ID_UNSET and the transfer is a message transfer.
///     - The remote node-ID is above CANARD_NODE_ID_MAX and the transfer is a service transfer.
///     - The priority, subject-ID, or service-ID exceed their respective maximums.
///     - The transfer kind is invalid.
///     - The payload pointer is NULL while the payload size is nonzero.
///     - The local node is anonymous and a message transfer is requested that requires a multi-frame transfer.
///     - The local node is anonymous and a service transfer is requested.
/// The following cases are handled without raising an invalid argument error:
///     - If the transfer-ID is above the maximum, the excessive bits are silently masked away
///       (i.e., the modulo is computed automatically, so the caller doesn't have to bother).
///
/// An out-of-memory error is returned if a TX frame could not be allocated due to the heap being exhausted.
/// In that case, all previously allocated frames will be purged automatically.
/// In other words, either all frames of the transfer are enqueued successfully, or none are.
///
/// The time complexity is O(s+t), where s is the amount of payload in the transfer, and t is the number of
/// frames already enqueued in the transmission queue.
///
/// The heap memory requirement is one allocation per transport frame. In other words, a single-frame transfer takes
/// one allocation; a multi-frame transfer of N frames takes N allocations. The maximum size of each allocation is
/// sizeof(CanardInternalTxQueueItem) plus MTU.
int32_t canardTxPush(CanardInstance* const ins, const CanardTransfer* const transfer);

/// Access the top element of the prioritized transmission queue. The queue itself is not modified (i.e., the
/// accessed element is not removed). The application should invoke this function to collect the transport frames
/// of serialized transfers stored into the prioritized transmission queue by canardTxPush().
///
/// Nodes with redundant transports should replicate every frame into each of the transport interfaces.
/// Such replication may require additional buffering in the driver layer, depending on the implementation.
///
/// The timestamp values of returned frames are initialized with the timestamp value of the transfer instance they
/// originate from. Timestamps are used to specify the transmission deadline. It is up to the application and/or
/// the driver layer to implement the discardment of timed-out transport frames. The library does not check it,
/// so a frame that is already timed out may be returned here.
///
/// If the queue is empty, the return value is zero and the out_frame is not modified.
///
/// If the queue is non-empty, the return value is 1 (one) and the out_frame is populated with the data from
/// the top element (i.e., the next frame awaiting transmission).
/// The payload pointer of the out_frame will point to the data buffer of the accessed frame;
/// the pointer retains validity until the element is removed from the queue by calling canardTxPop().
/// The payload pointer retains validity even if more frames are added to the transmission queue.
/// If the returned frame instance is not needed, it can be dropped -- no deinitialization procedures are needed
/// since it does not own any memory itself.
///
/// If either of the arguments are NULL, the negated invalid argument error code is returned and no other
/// actions are performed.
///
/// The time complexity is constant.
int8_t canardTxPeek(const CanardInstance* const ins, CanardCANFrame* const out_frame);

/// Remove and free the top element from the prioritized transmission queue.
/// The application should invoke this function after the top frame obtained through canardTxPeek() has been
/// processed and need not be kept anymore (e.g., transmitted successfully, timed out, errored, etc.).
///
/// WARNING:
///     Invocation of canardTxPush() may add new elements at the top of the prioritized transmission queue.
///     The calling code shall take that into account to eliminate the possibility of data loss due to the frame
///     at the top of the queue being unexpectedly replaced between calls of canardTxPeek() and this function.
///
/// Invocation of this function invalidates the payload pointer of the top frame because the underlying buffer is freed.
///
/// If the input argument is NULL or if the transmission queue is empty, the function has no effect.
///
/// The time complexity is constant.
void canardTxPop(CanardInstance* const ins);

int8_t canardRxAccept(CanardInstance* const       ins,
                      const CanardCANFrame* const frame,
                      const uint8_t               iface_index,
                      CanardTransfer* const       out_transfer);

#ifdef __cplusplus
}
#endif
#endif
