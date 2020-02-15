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
#define CANARD_VERSION_MAJOR 0
#define CANARD_VERSION_MINOR 1

/// The version number of the UAVCAN specification implemented by this library.
#define CANARD_UAVCAN_SPECIFICATION_VERSION_MAJOR 1
#define CANARD_UAVCAN_SPECIFICATION_VERSION_MINOR 0

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

typedef uint64_t CanardMicrosecond;
typedef uint16_t CanardPortID;
typedef uint8_t  CanardNodeID;
typedef uint8_t  CanardTransferID;

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
#define CANARD_NUM_TRANSFER_KINDS 3

/// CAN data frame with an extended 29-bit ID. RTR/Error frames are not used and therefore not modeled here.
typedef struct
{
    /// For RX frames: reception timestamp.
    /// For TX frames: transmission deadline.
    /// The time system may be arbitrary as long as the clock is monotonic (steady).
    CanardMicrosecond timestamp_usec;

    /// 29-bit extended ID. The bits above 29-th are zero/ignored.
    uint32_t extended_can_id;

    /// The useful data in the frame. The length value is not to be confused with DLC!
    size_t      payload_size;
    const void* payload;
} CanardFrame;

/// Conversion look-up table from CAN DLC to data length.
extern const uint8_t CanardCANDLCToLength[16];

/// Conversion look-up table from data length to CAN DLC; the length is rounded up.
extern const uint8_t CanardCANLengthToDLC[65];

typedef struct
{
    /// For RX transfers: reception timestamp.
    /// For TX transfers: transmission deadline.
    /// The time system may be arbitrary as long as the clock is monotonic (steady).
    CanardMicrosecond timestamp_usec;

    CanardPriority priority;

    CanardTransferKind transfer_kind;

    /// Subject-ID for message publications; service-ID for service requests/responses.
    CanardPortID port_id;

    /// For outgoing message transfers or for incoming anonymous transfers, the value is CANARD_NODE_ID_UNSET.
    CanardNodeID remote_node_id;

    CanardTransferID transfer_id;

    /// The const pointer makes it incompatible with free(), but we have to tolerate that due to the limitations of C.
    size_t      payload_size;
    const void* payload;
} CanardTransfer;

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
    CanardNodeID node_id;

    /// Callbacks invoked by the library. See their type documentation for details.
    /// They SHALL be valid function pointers at all times.
    ///
    /// The time complexity parameters given in the API documentation are made on the assumption that
    /// the heap management functions (allocate and free) have constant time complexity O(1).
    ///
    /// There are only three API functions that may lead to allocation of heap memory:
    ///     - canardTxPush()
    ///     - canardRxAccept()
    ///     - canardRxSubscribe()
    /// Their exact memory requirement model is specified in their documentation.
    ///
    /// By design, the library does not require the application to engage in manual memory management.
    /// All pointers to heap memory are managed entirely by the library itself, thus eliminating the risk of
    /// memory leaks in the application.
    CanardHeapAllocate heap_allocate;
    CanardHeapFree     heap_free;

    /// These fields are for internal use only. Do not access from the application.
    struct CanardInternalRxSubscription* _rx_subscriptions[CANARD_NUM_TRANSFER_KINDS];
    struct CanardInternalTxQueueItem*    _tx_queue;
};

/// Initialize a new library instance.
/// The default values will be assigned as specified in the structure field documentation.
/// The time complexity parameters given in the API documentation are made on the assumption that the heap management
/// functions (allocate and free) have constant complexity.
/// If any of the pointers are NULL, the behavior is undefined.
///
/// The instance does not hold any resources itself except the heap memory. If the instance should be de-initialized,
/// the application shall clear the TX queue by calling the pop function repeatedly, and remove all RX subscriptions.
/// Once that is done, the instance will be holding no memory resources, so it can be discarded freely.
CanardInstance canardInit(const CanardHeapAllocate heap_allocate, const CanardHeapFree heap_free);

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
/// The heap memory requirement is one allocation per transport frame.
/// A single-frame transfer takes one allocation; a multi-frame transfer of N frames takes N allocations.
/// The maximum size of each allocation is sizeof(CanardInternalTxQueueItem) plus MTU,
/// where sizeof(CanardInternalTxQueueItem) is at most 32 bytes on any conventional platform (typically smaller).
/// For example, if the MTU is 64 bytes, the allocation size will never exceed 96 bytes on any conventional platform.
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
int8_t canardTxPeek(const CanardInstance* const ins, CanardFrame* const out_frame);

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

/// The function does nothing and returns a negated invalid argument error immediately if any condition is true:
///     - Any of the input arguments that are pointers are NULL.
///     - The payload pointer of the input frame is NULL while its size is non-zero.
///     - The CAN ID of the input frame is not less than 2**29=0x20000000.
///
/// The function returns zero if any of the following conditions are true; the general policy is that protocol errors
/// are not escalated because they do not construe a node-local error:
///     - The received frame is not a valid UAVCAN/CAN transport frame; in this case the frame is silently ignored.
///     - The received frame is a valid UAVCAN/CAN transport frame, but it belongs to a session that is not
///       relevant to the application (i.e., the application reported via the RX filter callback that the library
///       need not receive transfers from this session).
///     - The received frame is a valid UAVCAN/CAN transport frame, but it did not complete a transfer, or it forms
///       an invalid frame sequence.
///
/// The MTU of the accepted frame is not limited and is not dependent on the MTU setting of the local node;
/// that is, any MTU is accepted.
///
/// Free the payload buffer? Keep it allocated forever, do not require the application to clean anything.
/// This will also relieve us from allocating new storage for single-frame transfers.
///
/// Any value of iface_index is accepted; that is, up to 256 redundant transports are supported.
/// The interface from which the transfer is accepted is always the same as iface_index.
///
/// The time complexity is O(n) where n is the number of subject-IDs or service-IDs subscribed to by the application,
/// depending on whether the frame is of the message kind of of the service kind.
/// Observe that the time complexity is invariant to the network configuration (such as the number of online nodes),
/// which is an important design guarantee for real-time applications.
/// Unicast frames where the destination does not equal the local node-ID are discarded in constant time.
/// Frames that are not valid UAVCAN/CAN frames are discarded in constant time.
///
/// HEAP MEMORY REQUIREMENT MODEL.
int8_t canardRxAccept(CanardInstance* const    ins,
                      const CanardFrame* const frame,
                      const uint8_t            iface_index,
                      CanardTransfer* const    out_transfer);

/// The library allocates large look-up tables to ensure that the temporal properties of its algorithms are
/// invariant to the network configuration (i.e., a node that is validated on a network containing one other node
/// will provably perform identically on a network that contains X nodes).
/// See for context: https://github.com/UAVCAN/libuavcan/issues/185#issuecomment-440354858.
/// This is a conscious time-memory trade-off. It may have adverse effects on RAM-constrained applications,
/// but this is considered tolerable because it is expected that the types of applications leveraging Libcanard
/// will be either real-time function nodes where time determinism is critical, or bootloaders where time determinism
/// is usually not required but the amount of available memory is not an issue (the main constraint is ROM, not RAM).
///
/// If such subscription already exists, it will be removed first as if canardRxUnsubscribe() was
/// invoked by the application, and then re-created anew with the new parameters.
///
/// Once a new RX session is allocated, it will never be removed as long as the subscription is active.
/// The rationale for this behavior is that real-time networks typically do not change their configuration at runtime;
/// hence, it is possible to reduce the worst-case computational complexity of the library routines by never
/// deallocating sessions once allocated. If this behavior is found to be undesirable, the application can force
/// deallocation of all unused states by re-creating the subscription anew.
///
/// HEAP MEMORY REQUIREMENT MODEL.
///
/// The return value is 1 if a new subscription has been created as requested.
/// The return value is 0 if such subscription existed at the time the function was invoked. In this case,
/// the existing subscription is terminated and then a new one is created in its place. Pending transfers may be lost.
/// The return value is negative in case of an error: a negated invalid argument error code if any of the arguments are
/// invalid, or the negated out-of-memory error if the new subscription could not be allocated due to the heap memory
/// being exhausted.
///
/// The time complexity is linear from the number of current subscriptions under the specified transfer kind.
int8_t canardRxSubscribe(CanardInstance* const    ins,
                         const CanardTransferKind transfer_kind,
                         const CanardPortID       port_id,
                         const size_t             payload_size_bytes_max,
                         const CanardMicrosecond  transfer_id_timeout_usec);

/// Reverse the effect of canardRxSubscribe().
/// If the subscription is found, all its heap memory is de-allocated; to determine the amount of memory freed,
/// please refer to the heap memory requirement models of canardRxSubscribe() and canardRxAccept().
/// This function does not allocate new heap memory.
///
/// The return value is 1 if such subscription existed (and, therefore, it was removed).
/// The return value is 0 if such subscription does not exist. In this case, the function has no effect.
/// The return value is a negated invalid argument error if any of the input arguments are invalid.
///
/// The time complexity is linear from the number of current subscriptions under the specified transfer kind.
int8_t canardRxUnsubscribe(CanardInstance* const    ins,
                           const CanardTransferKind transfer_kind,
                           const CanardPortID       port_id);

#ifdef __cplusplus
}
#endif
#endif
