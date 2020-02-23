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
#define CANARD_VERSION_MINOR 100

/// The version number of the UAVCAN specification implemented by this library.
#define CANARD_UAVCAN_SPECIFICATION_VERSION_MAJOR 1
#define CANARD_UAVCAN_SPECIFICATION_VERSION_MINOR 0

/// These error codes may be returned from the library API calls whose return type is a signed integer
/// in the negated form (e.g., code 2 returned as -2).
/// API calls whose return type is not a signer integer cannot fail by contract.
/// No other error states may occur in the library.
/// By contract, a deterministic application with a properly sized memory pool will never encounter errors.
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

    /// Per the Specification, all frames belonging to a given transfer shall share the same priority level.
    /// If this is not the case, then this field contains the priority level of the last frame to arrive.
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

/// Transfer subscription state. The application can register its interest in a particular kind of data exchanged
/// over the bus by creating such subscription objects. Frames that carry data for which there is no active
/// subscription will be dropped by the library.
///
/// WARNING: SUBSCRIPTION INSTANCES SHALL NOT BE COPIED OR MUTATED BY THE APPLICATION.
/// Every field is named starting with an underscore to emphasize that the application shall not mess with it.
/// Unfortunately, C, being such a limited language, does not allow us to construct a better API.
///
/// The memory footprint of a subscription is large. On a 32-bit platform it slightly exceeds half a KiB.
/// This is what we call a time-memory trade-off: we use a large look-up table to ensure deterministic runtime behavior.
typedef struct CanardRxSubscription
{
    struct CanardRxSubscription* _next;

    /// The current architecture is a sort of an acceptable middle ground between worst-case execution time and memory
    /// consumption. Instead of statically pre-allocating a dedicated RX session for each remote node-ID here in
    /// this table, we only keep pointers, which are NULL by default, populating a new RX session dynamically
    /// on an ad-hoc basis when we first receive a transfer from that node. This is still deterministic because our
    /// memory allocation routines are assumed to be deterministic and we make at most one allocation per remote node,
    /// but the disadvantage is that these additional operations increase the upper bound on the execution time.
    /// Further, the pointers here add an extra indirection, which is bad for systems that leverage cached memory,
    /// plus a pointer itself takes about 2-8 bytes of memory, too.
    ///
    /// A far more predictable and a much simpler approach is to pre-allocate states here statically instead of keeping
    /// just pointers, but it would push the size of this instance from about 0.5 KiB to ~3 KiB for a typical 32-bit
    /// system. Since this is a general-purpose library, we have to pick a middle ground so we use the more complex
    /// but more memory-efficient approach. Implementations that are more optimized for low-jitter real-time
    /// applications may prefer the other approach.
    struct CanardInternalRxSession* _sessions[CANARD_NODE_ID_MAX + 1U];

    CanardMicrosecond _transfer_id_timeout_usec;
    size_t            _payload_size_max;
    CanardPortID      _port_id;
} CanardRxSubscription;

typedef void* (*CanardMemoryAllocate)(CanardInstance* ins, size_t amount);

/// Free as in freedom.
typedef void (*CanardMemoryFree)(CanardInstance* ins, void* pointer);

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
    /// the memory management functions (allocate and free) have constant time complexity O(1).
    ///
    /// There are only two API functions that may lead to allocation of memory:
    ///     - canardTxPush()
    ///     - canardRxAccept()
    /// Their exact memory requirement model is specified in their documentation.
    CanardMemoryAllocate memory_allocate;
    CanardMemoryFree     memory_free;

    /// These fields are for internal use only. Do not access from the application.
    CanardRxSubscription*             _rx_subscriptions[CANARD_NUM_TRANSFER_KINDS];
    struct CanardInternalTxQueueItem* _tx_queue;
};

/// Initialize a new library instance.
/// The default values will be assigned as specified in the structure field documentation.
/// The time complexity parameters given in the API documentation are made on the assumption that the memory management
/// functions (allocate and free) have constant complexity.
/// If any of the pointers are NULL, the behavior is undefined.
///
/// The instance does not hold any resources itself except the allocated memory.
/// If the instance should be de-initialized, the application shall clear the TX queue by calling the pop function
/// repeatedly, and remove all RX subscriptions. Once that is done, the instance will be holding no memory resources,
/// so it can be discarded freely.
CanardInstance canardInit(const CanardMemoryAllocate memory_allocate, const CanardMemoryFree memory_free);

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
/// or other deterministic data structures should be considered.
///
/// Returns the number of frames enqueued into the prioritized TX queue (which is always a positive number)
/// in case of success (so that the application can track the number of items in the TX queue if necessary).
/// Returns a negated error code in case of failure. Zero is never returned.
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
/// An out-of-memory error is returned if a TX frame could not be allocated due to the memory being exhausted.
/// In that case, all previously allocated frames will be purged automatically.
/// In other words, either all frames of the transfer are enqueued successfully, or none are.
///
/// The time complexity is O(s+t), where s is the amount of payload in the transfer, and t is the number of
/// frames already enqueued in the transmission queue.
///
/// The memory allocation requirement is one allocation per transport frame.
/// A single-frame transfer takes one allocation; a multi-frame transfer of N frames takes N allocations.
/// The maximum size of each allocation is sizeof(CanardFrame) plus a pointer size plus MTU size.
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
/// If the queue is empty or if the argument is NULL, the returned value is NULL.
///
/// If the queue is non-empty, the returned value is a pointer to its top element (i.e., the next frame to transmit).
/// The returned pointer points to an object allocated in the dynamic storage; it should be freed by the application
/// by calling CanardInstance::memory_free(). The memory shall not be freed before the entry is removed from the
/// queue by calling canardTxPop(); this is because until canardTxPop() is executed, the library retains ownership
/// of the object. The payload pointer retains validity until explicitly freed by the application; in other words,
/// calling canardTxPop() does not invalidate the object.
///
/// The payload buffer is located shortly after the object itself, in the same memory fragment. The application shall
/// not attempt to free its memory.
///
/// The time complexity is constant.
const CanardFrame* canardTxPeek(const CanardInstance* const ins);

/// Transfer the ownership of the top element of the prioritized transmission queue to the application.
/// The application should invoke this function to remove the top element from the prioritized transmission queue.
/// While the element is removed, it is not invalidated: it is the responsibility of the application to deallocate
/// the memory used by the object later. The object SHALL NOT be deallocated UNTIL this function is invoked.
///
/// WARNING:
///     Invocation of canardTxPush() may add new elements at the top of the prioritized transmission queue.
///     The calling code shall take that into account to eliminate the possibility of data loss due to the frame
///     at the top of the queue being unexpectedly replaced between calls of canardTxPeek() and this function.
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
/// The function returns 1 (one) if the new frame completed a transfer. In this case, the details of the transfer
/// are stored into out_transfer, and the payload ownership is passed into that object. This means that the application
/// is responsible for deallocating the payload buffer when the processing is done by invoking memory_free.
/// This design is chosen to facilitate almost zero-copy data exchange across the protocol stack: once a buffer is
/// allocated, its data is never copied around but only passed by reference. This design allows us to reduce the
/// worst-case execution time and reduce jitter caused by the linear time complexity of memcpy().
/// There is a special case, however: if the payload_size_max is zero, the payload pointer will be NULL, since there
/// is no data to store and so a buffer is not needed.
///
/// One data copy still has to take place, though: it's the copy from the frame payload into the contiguous buffer.
/// In CAN, the MTU is small (at most 64 bytes for CAN FD), so the extra copy does not cost us much here,
/// but it allows us to completely decouple the lifetime of the input frame buffer from the lifetime of the final
/// transfer object, regardless of whether it's a single-frame or a multi-frame transfer.
/// If we were building, say, an UAVCAN/UDP library, then we would likely resort to a different design, where the
/// frame buffer is allocated once from the heap (which may be done from the interrupt handler if the heap is
/// sufficiently deterministic; an example of a suitable real-time heap is https://github.com/pavel-kirienko/o1heap),
/// and in the case of single-frame transfer it is then carried over to the application without copying.
/// This design somewhat complicates the driver layer though.
///
/// The MTU of the accepted frame is not limited and is not dependent on the MTU setting of the local node;
/// that is, any MTU is accepted. The DLC compliance is not checked -- payload of any length (unlimited) is accepted.
///
/// Any value of redundant_transport_index is accepted; that is, up to 256 redundant transports are supported.
/// The index of the transport from which the transfer is accepted is always the same as redundant_transport_index.
///
/// The time complexity is O(n+s) where n is the number of subject-IDs or service-IDs subscribed to by the application,
/// depending on whether the frame is of the message kind of of the service kind, and s is the amount of payload in the
/// received transport frame (because it will be copied into an internal contiguous buffer).
/// Observe that the time complexity is invariant to the network configuration (such as the number of online nodes),
/// which is an important design guarantee for real-time applications.
/// The execution time is only dependent on the number of active subscriptions for a given transfer kind,
/// and the MTU, both of which are easy to predict and account for. Excepting the subscription search and the
/// payload data copying, the entire RX pipeline contains neither loops nor recursion.
///
/// Unicast frames where the destination does not equal the local node-ID are discarded in constant time.
/// Frames that are not valid UAVCAN/CAN frames are discarded in constant time.
///
/// MEMORY ALLOCATION REQUIREMENT MODEL.
int8_t canardRxAccept(CanardInstance* const    ins,
                      const CanardFrame* const frame,
                      const uint8_t            redundant_transport_index,
                      CanardTransfer* const    out_transfer);

/// Subscription instances have large look-up tables to ensure that the temporal properties of the algorithms are
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
/// deallocating sessions once allocated. The size of an RX state is at most 48 bytes on any conventional platform.
/// If this behavior is found to be undesirable, the application can force deallocation of all unused states by
/// re-creating the subscription anew.
///
/// The transport fail-over timeout (if redundant transports are used) is the same as the transfer-ID timeout.
///
/// The return value is 1 if a new subscription has been created as requested.
/// The return value is 0 if such subscription existed at the time the function was invoked. In this case,
/// the existing subscription is terminated and then a new one is created in its place. Pending transfers may be lost.
/// The return value is a negated invalid argument error if any of the input arguments are invalid.
///
/// The time complexity is linear from the number of current subscriptions under the specified transfer kind.
/// This function does not allocate new memory. The function may deallocate memory if such subscription already existed.
int8_t canardRxSubscribe(CanardInstance* const       ins,
                         const CanardTransferKind    transfer_kind,
                         const CanardPortID          port_id,
                         const size_t                payload_size_max,
                         const CanardMicrosecond     transfer_id_timeout_usec,
                         CanardRxSubscription* const out_subscription);

/// Reverse the effect of canardRxSubscribe().
/// If the subscription is found, all its memory is de-allocated; to determine the amount of memory freed,
/// please refer to the memory allocation requirement model of canardRxAccept().
///
/// The return value is 1 if such subscription existed (and, therefore, it was removed).
/// The return value is 0 if such subscription does not exist. In this case, the function has no effect.
/// The return value is a negated invalid argument error if any of the input arguments are invalid.
///
/// The time complexity is linear from the number of current subscriptions under the specified transfer kind.
/// This function does not allocate new memory.
int8_t canardRxUnsubscribe(CanardInstance* const    ins,
                           const CanardTransferKind transfer_kind,
                           const CanardPortID       port_id);

#ifdef __cplusplus
}
#endif
#endif
