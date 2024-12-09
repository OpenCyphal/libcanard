///                            ____                   ______            __          __
///                           / __ `____  ___  ____  / ____/_  ______  / /_  ____  / /
///                          / / / / __ `/ _ `/ __ `/ /   / / / / __ `/ __ `/ __ `/ /
///                         / /_/ / /_/ /  __/ / / / /___/ /_/ / /_/ / / / / /_/ / /
///                         `____/ .___/`___/_/ /_/`____/`__, / .___/_/ /_/`__,_/_/
///                             /_/                     /____/_/
///
/// Libcanard is a compact implementation of the Cyphal/CAN protocol for high-integrity real-time embedded systems.
/// It is designed for use in robust deterministic embedded systems equipped with at least 32K ROM and 8K RAM.
/// The codebase follows the MISRA C rules, has 100% test coverage, and is validated by at least two static analyzers.
/// The library is designed to be compatible with any target platform and instruction set architecture, from 8 to 64
/// bit, little- and big-endian, RTOS-based or baremetal, etc., as long as there is a standards-compliant compiler.
///
///          INTEGRATION
///
/// The library is intended to be integrated into the end application by simply copying its source files into the
/// source tree of the project; it does not require any special compilation options and should work out of the box.
/// There are build-time configuration parameters defined near the top of canard.c, but they are safe to ignore.
///
/// As explained in this documentation, the library requires a deterministic constant-time bounded-fragmentation dynamic
/// memory allocator. If your target platform does not provide a deterministic memory manager (most platforms don't),
/// it is recommended to use O1Heap (MIT licensed): https://github.com/pavel-kirienko/o1heap.
///
/// There are no specific requirements to the underlying I/O layer. Some low-level drivers maintained by the
/// OpenCyphal team may be found at https://github.com/OpenCyphal-Garage/platform_specific_components.
///
/// If your application requires a MISRA C compliance report, please get in touch with the maintainers via the forum
/// at https://forum.opencyphal.org.
///
///          ARCHITECTURE
///
/// Cyphal, as a protocol stack, is composed of two layers: TRANSPORT and PRESENTATION. The transport layer is portable
/// across different transport protocols, one of which is CAN (FD), formally referred to as Cyphal/CAN. This library
/// is focused on Cyphal/CAN only and it will not support other transports. The presentation layer is implemented
/// through the DSDL language and the associated data type regulation policies; these parts are out of the scope of
/// this library as it is focused purely on the transport.
///
/// This library consists of two components: the transmission (TX) pipeline and the reception (RX) pipeline.
/// The pipelines are completely independent from each other except that they both rely on the same dynamic memory
/// manager. The TX pipeline uses the dynamic memory to store outgoing CAN frames in the prioritized transmission
/// queue. The RX pipeline uses the dynamic memory to store contiguous payload buffers for received transfers and
/// for keeping the transfer reassembly state machine data. The exact memory consumption model is defined for both
/// pipelines, so it is possible to statically determine the minimum size of the dynamic memory pool required to
/// guarantee that a given application will never encounter an out-of-memory error at runtime.
///
/// Much like with dynamic memory, the time complexity of every API function is well-characterized, allowing the
/// application to guarantee predictable real-time performance.
///
/// The TX pipeline is managed with the help of five API functions. The first one -- canardTxInit() -- is used for
/// constructing a new TX queue, of which there should be as many as there are redundant CAN interfaces;
/// each queue is managed independently. When the application needs to emit a transfer, it invokes canardTxPush()
/// on each queue separately. The function splits the transfer into CAN frames and stores them into the queue.
/// The application then picks the produced CAN frames from the queue one-by-one by calling canardTxPeek() followed
/// by canardTxPop() -- the former allows the application to look at the next frame scheduled for transmission,
/// and the latter tells the library that the frame shall be removed from the queue.
/// Popped frames need to be manually deallocated by the application upon transmission by calling canardTxFree().
///
/// The RX pipeline is managed with the help of three API functions; unlike the TX pipeline, there is one shared
/// state for all redundant interfaces that manages deduplication transparently. The main function canardRxAccept()
/// takes a received CAN frame and updates the appropriate transfer reassembly state machine. The functions
/// canardRxSubscribe() and its counterpart canardRxUnsubscribe() instruct the library which transfers should be
/// received (by default, all transfers are ignored); also, the subscription function specifies vital transfer
/// reassembly parameters such as the maximum payload size (i.e., the maximum size of a serialized representation
/// of a DSDL object) and the transfer-ID timeout. Transfers that carry more payload than the configured maximum per
/// subscription are truncated following the Implicit Truncation Rule (ITR) defined by the Cyphal Specification --
/// the rule is implemented to facilitate backward-compatible DSDL data type extensibility.
///
/// The library supports a practically unlimited number of redundant interfaces.
///
/// The library is not thread-safe: if used in a concurrent environment, it is the responsibility of the application
/// to provide adequate synchronization.
///
/// The library is purely reactive: it does not perform any background processing and does not require periodic
/// servicing. Its internal state is only updated as a response to well-specified external events.
///
/// --------------------------------------------------------------------------------------------------------------------
///
/// This software is distributed under the terms of the MIT License.
/// Copyright (c) 2016 OpenCyphal.
/// Author: Pavel Kirienko <pavel@opencyphal.org>
/// Contributors: https://github.com/OpenCyphal/libcanard/contributors.

#ifndef CANARD_H_INCLUDED
#define CANARD_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Semantic version of this library (not the Cyphal specification).
/// API will be backward compatible within the same major version.
#define CANARD_VERSION_MAJOR 4
#define CANARD_VERSION_MINOR 0

/// The version number of the Cyphal specification implemented by this library.
#define CANARD_CYPHAL_SPECIFICATION_VERSION_MAJOR 1
#define CANARD_CYPHAL_SPECIFICATION_VERSION_MINOR 0

/// These error codes may be returned from the library API calls whose return type is a signed integer in the negated
/// form (e.g., error code 2 returned as -2). A non-negative return value represents success.
/// API calls whose return type is not a signed integer cannot fail by contract.
/// No other error states may occur in the library.
/// By contract, a well-characterized application with a properly sized memory pool will never encounter errors.
/// The error code 1 is not used because -1 is often used as a generic error code in 3rd-party code.
#define CANARD_ERROR_INVALID_ARGUMENT 2
#define CANARD_ERROR_OUT_OF_MEMORY 3

/// MTU values for the supported protocols.
/// Per the recommendations given in the Cyphal/CAN Specification, other MTU values should not be used.
#define CANARD_MTU_CAN_CLASSIC 8U
#define CANARD_MTU_CAN_FD 64U
#define CANARD_MTU_MAX CANARD_MTU_CAN_FD

/// Parameter ranges are inclusive; the lower bound is zero for all. See Cyphal/CAN Specification for background.
#define CANARD_SUBJECT_ID_MAX 8191U
#define CANARD_SERVICE_ID_MAX 511U
#define CANARD_NODE_ID_MAX 127U
#define CANARD_PRIORITY_MAX 7U
#define CANARD_TRANSFER_ID_BIT_LENGTH 5U
#define CANARD_TRANSFER_ID_MAX ((1U << CANARD_TRANSFER_ID_BIT_LENGTH) - 1U)

/// This value represents an undefined node-ID: broadcast destination or anonymous source.
/// Library functions treat all values above CANARD_NODE_ID_MAX as anonymous.
#define CANARD_NODE_ID_UNSET 255U

/// This is the recommended transfer-ID timeout value given in the Cyphal Specification. The application may choose
/// different values per subscription (i.e., per data specifier) depending on its timing requirements.
#define CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC 2000000UL

// Forward declarations.
typedef uint64_t CanardMicrosecond;
typedef uint16_t CanardPortID;
typedef uint8_t  CanardNodeID;
typedef uint8_t  CanardTransferID;

/// Transfer priority level mnemonics per the recommendations given in the Cyphal Specification.
enum CanardPriority
{
    CanardPriorityExceptional = 0,
    CanardPriorityImmediate   = 1,
    CanardPriorityFast        = 2,
    CanardPriorityHigh        = 3,
    CanardPriorityNominal     = 4,  ///< Nominal priority level should be the default.
    CanardPriorityLow         = 5,
    CanardPrioritySlow        = 6,
    CanardPriorityOptional    = 7,
};

/// Transfer kinds as defined by the Cyphal Specification.
enum CanardTransferKind
{
    CanardTransferKindMessage  = 0,  ///< Multicast, from publisher to all subscribers.
    CanardTransferKindResponse = 1,  ///< Point-to-point, from server to client.
    CanardTransferKindRequest  = 2,  ///< Point-to-point, from client to server.
};
#define CANARD_NUM_TRANSFER_KINDS 3

/// The AVL tree node structure is exposed here to avoid pointer casting/arithmetics inside the library.
/// The user code is not expected to interact with this type except if advanced introspection is required.
struct CanardTreeNode
{
    struct CanardTreeNode* up;     ///< Do not access this field.
    struct CanardTreeNode* lr[2];  ///< Left and right children of this node may be accessed for tree traversal.
    int8_t                 bf;     ///< Do not access this field.
};

struct CanardPayload
{
    /// Size of the payload data in bytes.
    /// If the payload is empty (size = 0), the payload pointer may be NULL.
    size_t size;

    /// Pointer to the payload data buffer.
    const void* data;
};

struct CanardMutablePayload
{
    /// Size of the payload data in bytes.
    /// The value is always less than or equal to the extent specified in the subscription.
    /// If the payload is empty (`size` = 0), the `data` pointer may be NULL.
    size_t size;

    /// The application is required to deallocate the payload buffer after the transfer is processed.
    /// Allocated buffer size (`allocated_size`, not `size`) should be used to deallocate the buffer.
    void* data;

    /// Size of the allocated data buffer in bytes.
    /// Normally equal to the extent specified in the subscription, but could be less (equal to `size`)
    /// in case of single frame transfer, or even zero if the `data` pointer is NULL.
    size_t allocated_size;
};

/// CAN data frame with an extended 29-bit ID. RTR/Error frames are not used and therefore not modeled here.
/// CAN frames with 11-bit ID are not used by Cyphal/CAN and so they are not supported by the library.
struct CanardFrame
{
    /// 29-bit extended ID. The bits above 29-th shall be zero.
    uint32_t extended_can_id;

    /// The useful data in the frame. The length value is not to be confused with DLC!
    /// If the payload is empty (payload.size = 0), the payload.data pointer may be NULL.
    /// For RX frames: the library does not expect the lifetime of the pointee to extend beyond the point of return
    /// from the API function. That is, the pointee can be invalidated immediately after the frame has been processed.
    /// For TX frames: the frame and the payload are allocated within the same dynamic memory fragment, so their
    /// lifetimes are identical; when the frame is freed, the payload is invalidated.
    /// A more detailed overview of the dataflow and related resource management issues is provided in the API docs.
    struct CanardPayload payload;
};

/// Similar to the `CanardFrame` structure, but with a mutable payload (including `allocated_size` of the payload).
/// In use when payload memory ownership might be transferred.
struct CanardMutableFrame
{
    uint32_t                    extended_can_id;
    struct CanardMutablePayload payload;
};

/// Conversion look-up table from CAN DLC to data length.
extern const uint8_t CanardCANDLCToLength[16];

/// Conversion look-up table from data length to CAN DLC; the length is rounded up.
extern const uint8_t CanardCANLengthToDLC[65];

/// A Cyphal transfer metadata (everything except the payload).
/// Per Specification, a transfer is represented on the wire as a non-empty set of transport frames (i.e., CAN frames).
/// The library is responsible for serializing transfers into transport frames when transmitting, and reassembling
/// transfers from an incoming stream of frames (possibly duplicated if redundant interfaces are used) during reception.
struct CanardTransferMetadata
{
    /// Per the Specification, all frames belonging to a given transfer shall share the same priority level.
    /// If this is not the case, then this field contains the priority level of the last frame to arrive.
    enum CanardPriority priority;

    enum CanardTransferKind transfer_kind;

    /// Subject-ID for message publications; service-ID for service requests/responses.
    CanardPortID port_id;

    /// For outgoing message transfers the value shall be CANARD_NODE_ID_UNSET (otherwise the state is invalid).
    /// For outgoing service transfers this is the destination address (invalid if unset).
    /// For incoming non-anonymous transfers this is the node-ID of the origin.
    /// For incoming anonymous transfers the value is reported as CANARD_NODE_ID_UNSET.
    CanardNodeID remote_node_id;

    /// When responding to a service request, the response transfer SHALL have the same transfer-ID value as the
    /// request because the client will match the response with the request based on that.
    ///
    /// When publishing a message transfer, the value SHALL be one greater than the previous transfer under the same
    /// subject-ID; the initial value should be zero.
    ///
    /// When publishing a service request transfer, the value SHALL be one greater than the previous transfer under
    /// the same service-ID addressed to the same server node-ID; the initial value should be zero.
    ///
    /// Upon overflow, the value SHALL be reset back to zero.
    /// Values above CANARD_TRANSFER_ID_MAX are permitted -- the library will compute the modulo automatically.
    /// For received transfers, the values never exceed CANARD_TRANSFER_ID_MAX.
    ///
    /// A simple and robust way of managing transfer-ID counters is to keep a separate static variable per subject-ID
    /// and per (service-ID, server-node-ID) pair.
    CanardTransferID transfer_id;
};

/// A pointer to the memory allocation function. The semantics are similar to malloc():
///     - The returned pointer shall point to an uninitialized block of memory that is at least `size` bytes large.
///     - If there is not enough memory, the returned pointer shall be NULL.
///     - The memory shall be aligned at least at max_align_t.
///     - The execution time should be constant (O(1)).
///     - The worst-case memory fragmentation should be bounded and easily predictable.
/// If the standard dynamic memory manager of the target platform does not satisfy the above requirements,
/// consider using O1Heap: https://github.com/pavel-kirienko/o1heap.
///
/// The value of the user reference is taken from the corresponding field of the memory resource structure.
typedef void* (*CanardMemoryAllocate)(void* const user_reference, const size_t size);

/// The counterpart of the above -- this function is invoked to return previously allocated memory to the allocator.
/// The semantics are similar to free(), but with additional `size` parameter:
///     - The pointer was previously returned by the allocation function.
///     - The pointer may be NULL, in which case the function shall have no effect.
///     - The execution time should be constant (O(1)).
///     - The size is the same as it was during allocation.
///
/// The value of the user reference is taken from the corresponding field of the memory resource structure.
typedef void (*CanardMemoryDeallocate)(void* const user_reference, const size_t size, void* const pointer);

/// A kind of memory resource that can only be used to free memory previously allocated by the user.
/// Instances are mostly intended to be passed by value.
struct CanardMemoryDeleter
{
    void*                  user_reference;  ///< Passed as the first argument.
    CanardMemoryDeallocate deallocate;      ///< Shall be a valid pointer.
};

/// A memory resource encapsulates the dynamic memory allocation and deallocation facilities.
/// Note that the library allocates a large amount of small fixed-size objects for bookkeeping purposes;
/// allocators for them can be implemented using fixed-size block pools to eliminate extrinsic memory fragmentation.
/// Instances are mostly intended to be passed by value.
struct CanardMemoryResource
{
    void*                  user_reference;  ///< Passed as the first argument.
    CanardMemoryDeallocate deallocate;      ///< Shall be a valid pointer.
    CanardMemoryAllocate   allocate;        ///< Shall be a valid pointer.
};

/// Holds the statistics of a transmission queue.
struct CanardTxQueueStats
{
    /// Holds number of dropped TX frames due to timeout (when `now > deadline`) or b/c of transmission failures.
    size_t dropped_frames;
};

/// The handler function is intended to be invoked from Canard TX polling (see details for the `canardTxPoll()`).
///
/// The user reference parameter what was passed to canardTxPoll.
/// The return result of the handling operation:
/// - Any positive value: the frame was successfully handled.
///   This indicates that the frame payload was accepted (and its payload ownership could be potentially moved,
///   see `canardTxPeek` for the details), and the frame can be safely removed from the queue.
/// - Zero: the frame was not handled, and so the frame should be kept in the queue.
///   It will be retried on some future `canardTxPoll()` call according to the queue state in the future.
///   This case is useful when TX hardware is busy, and the frame should be retried later.
/// - Any negative value: the frame was rejected due to an unrecoverable failure.
///   This indicates to the caller (`canardTxPoll`) that the frame should be dropped from the queue,
///   as well as all other frames belonging to the same transfer. The `dropped_frames` counter in the TX queue stats
///   will be incremented for each frame dropped in this way.
///
typedef int8_t (*CanardTxFrameHandler)(void* const                      user_reference,
                                       const CanardMicrosecond          deadline_usec,
                                       struct CanardMutableFrame* const frame);

/// Prioritized transmission queue that keeps CAN frames destined for transmission via one CAN interface.
/// Applications with redundant interfaces are expected to have one instance of this type per interface.
/// Applications that are not interested in transmission may have zero queues.
/// All operations (push, peek, pop) are O(log n); there are exactly two heap allocations per element:
/// - the first for bookkeeping purposes (CanardTxQueueItem)
/// - second for payload storage (the frame data)
/// API functions that work with this type are named "canardTx*()", find them below.
struct CanardTxQueue
{
    /// The maximum number of frames this queue is allowed to contain. An attempt to push more will fail with an
    /// out-of-memory error even if the memory is not exhausted. This value can be changed by the user at any moment.
    /// The purpose of this limitation is to ensure that a blocked queue does not exhaust the heap memory.
    size_t capacity;

    /// The transport-layer maximum transmission unit (MTU). The value can be changed arbitrarily at any time between
    /// pushes. It defines the maximum number of data bytes per CAN data frame in outgoing transfers via this queue.
    ///
    /// Only the standard values should be used as recommended by the specification;
    /// otherwise, networking interoperability issues may arise. See recommended values CANARD_MTU_*.
    ///
    /// Valid values are any valid CAN frame data length value not smaller than 8.
    /// Invalid values are treated as the nearest valid value. The default is the maximum valid value.
    size_t mtu_bytes;

    /// The number of frames that are currently contained in the queue, initially zero.
    /// Do not modify this field!
    size_t size;

    /// The root of the priority queue is NULL if the queue is empty. Do not modify this field!
    struct CanardTreeNode* priority_root;

    /// The root of the deadline queue is NULL if the queue is empty. Do not modify this field!
    struct CanardTreeNode* deadline_root;

    /// The memory resource used by this queue for allocating the payload data (CAN frames).
    /// There is exactly one allocation of payload buffer per enqueued item (not considering the item itself
    /// b/c it is allocated from different memory resource - the instance one; see CanardInstance::memory).
    /// The size of the allocation is equal (or might be less for the last frame) to the MTU of the queue.
    /// Memory for the queue item is allocated separately from the instance memory resource.
    /// In a simple application, there would be just one memory resource shared by all parts of the library.
    /// If the application knows its MTU, it can use block allocation to avoid extrinsic fragmentation,
    /// as well as a dedicated memory pool specifically for the TX queue payload for transmission.
    /// Dedicated memory resources could be useful also for systems with special memory requirements for payload data.
    /// For example, such a memory resource could be integrated with the CAN message RAM. So that memory
    /// is allocated directly in the peripheral's memory space. Then it will be filled with payload data by
    /// the Canard, and finally it will be ready to be directly transmitted by the HW (avoiding the need for copying).
    struct CanardMemoryResource memory;

    /// This field can be arbitrarily mutated by the user. It is never accessed by the library.
    /// Its purpose is to simplify integration with OOP interfaces.
    void* user_reference;

    /// Holds the statistics of this TX queue.
    struct CanardTxQueueStats stats;
};

/// One frame stored in the transmission queue along with its metadata.
struct CanardTxQueueItem
{
    /// Internal use only; do not access this field.
    struct CanardTreeNode priority_base;

    /// Internal use only; do not access this field.
    struct CanardTreeNode deadline_base;

    /// Points to the next frame in this transfer or NULL. This field is mostly intended for own needs of the library.
    /// Normally, the application would not use it because transfer frame ordering is orthogonal to global TX ordering.
    /// It can be useful though for pulling pending frames from the TX queue if at least one frame of their transfer
    /// failed to transmit; the idea is that if at least one frame is missing, the transfer will not be received by
    /// remote nodes anyway, so all its remaining frames can be dropped from the queue at once using canardTxPop().
    struct CanardTxQueueItem* next_in_transfer;

    /// This is the same value that is passed to canardTxPush().
    /// Frames whose transmission deadline is in the past shall be dropped.
    CanardMicrosecond tx_deadline_usec;

    /// The actual CAN frame data.
    struct CanardMutableFrame frame;
};

/// Transfer subscription state. The application can register its interest in a particular kind of data exchanged
/// over the bus by creating such subscription objects. Frames that carry data for which there is no active
/// subscription will be silently dropped by the library. The entire RX pipeline is invariant to the number of
/// redundant CAN interfaces used.
///
/// SUBSCRIPTION INSTANCES SHALL NOT BE MOVED WHILE IN USE.
///
/// The memory footprint of a subscription is large. On a 32-bit platform it slightly exceeds half a KiB.
/// This is an intentional time-memory trade-off: use a large look-up table to ensure predictable temporal properties.
struct CanardRxSubscription
{
    struct CanardTreeNode base;  ///< Read-only DO NOT MODIFY THIS

    CanardMicrosecond transfer_id_timeout_usec;
    size_t            extent;   ///< Read-only DO NOT MODIFY THIS
    CanardPortID      port_id;  ///< Read-only DO NOT MODIFY THIS

    /// This field can be arbitrarily mutated by the user. It is never accessed by the library.
    /// Its purpose is to simplify integration with OOP interfaces.
    void* user_reference;

    /// The current architecture is an acceptable middle ground between worst-case execution time and memory
    /// consumption. Instead of statically pre-allocating a dedicated RX session for each remote node-ID here in
    /// this table, we only keep pointers, which are NULL by default, populating a new RX session dynamically
    /// on an ad-hoc basis when we first receive a transfer from that node. This is O(1) because our memory
    /// allocation routines are assumed to be O(1) and we make at most one allocation per remote node.
    ///
    /// A more predictable and simpler approach is to pre-allocate states here statically instead of keeping
    /// just pointers, but it would push the size of this instance from about 0.5 KiB to ~3 KiB for a typical 32-bit
    /// system. Since this is a general-purpose library, we have to pick a middle ground so we use the more complex
    /// but more memory-efficient approach.
    struct CanardInternalRxSession* sessions[CANARD_NODE_ID_MAX + 1U];  ///< Read-only DO NOT MODIFY THIS
};

/// Reassembled incoming transfer returned by canardRxAccept().
struct CanardRxTransfer
{
    struct CanardTransferMetadata metadata;

    /// The timestamp of the first received CAN frame of this transfer.
    /// The time system may be arbitrary as long as the clock is monotonic (steady).
    CanardMicrosecond timestamp_usec;

    /// The application is required to deallocate the payload after the transfer is processed.
    struct CanardMutablePayload payload;
};

/// This is the core structure that keeps all of the states and allocated resources of the library instance.
struct CanardInstance
{
    /// User pointer that can link this instance with other objects.
    /// This field can be changed arbitrarily, the library does not access it after initialization.
    /// The default value is NULL.
    void* user_reference;

    /// The node-ID of the local node.
    /// Per the Cyphal Specification, the node-ID should not be assigned more than once.
    /// Invalid values are treated as CANARD_NODE_ID_UNSET. The default value is CANARD_NODE_ID_UNSET.
    CanardNodeID node_id;

    /// Dynamic memory management callbacks. See their type documentation for details.
    /// They SHALL be valid function pointers at all times.
    /// The time complexity models given in the API documentation are made on the assumption that the memory management
    /// functions have constant complexity O(1).
    ///
    /// The following API functions may allocate memory:   canardTxPush(), canardRxAccept().
    /// The following API functions may deallocate memory: canardTxPush(), canardTxFree(), canardRxAccept(),
    /// canardRxSubscribe(), canardRxUnsubscribe().
    /// The exact memory requirement and usage model is specified for each function in its documentation.
    struct CanardMemoryResource memory;

    /// Read-only DO NOT MODIFY THIS
    struct CanardTreeNode* rx_subscriptions[CANARD_NUM_TRANSFER_KINDS];
};

/// CAN acceptance filter configuration with an extended 29-bit ID utilizing an ID + mask filter scheme.
/// Filter configuration can be programmed into a CAN controller to filter out irrelevant messages in hardware.
/// This allows the software application to reduce CPU load spent on processing irrelevant messages.
struct CanardFilter
{
    /// 29-bit extended ID. Defines the extended CAN ID to filter incoming frames against.
    /// The bits above 29-th shall be zero.
    uint32_t extended_can_id;
    /// 29-bit extended mask. Defines the bitmask used to enable/disable bits used to filter messages.
    /// Only bits that are enabled are compared to the extended_can_id for filtering.
    /// The bits above 29-th shall be zero.
    uint32_t extended_mask;
};

/// Construct a new library instance.
/// The default values will be assigned as specified in the structure field documentation.
/// If any of the memory resource pointers are NULL, the behavior is undefined.
///
/// The instance does not hold any resources itself except for the allocated memory.
/// To safely discard it, simply remove all existing subscriptions, and don't forget about the TX queues.
///
/// The time complexity is constant. This function does not invoke the dynamic memory manager.
struct CanardInstance canardInit(const struct CanardMemoryResource memory);

/// Construct a new transmission queue instance with the specified values for capacity and mtu_bytes.
/// No memory allocation is going to take place until the queue is actually pushed to.
/// Applications are expected to have one instance of this type per redundant interface.
///
/// The instance does not hold any resources itself except for the allocated memory.
/// To safely discard it, simply pop all items from the queue and free them.
///
/// The time complexity is constant. This function does not invoke the dynamic memory manager.
struct CanardTxQueue canardTxInit(const size_t                      capacity,
                                  const size_t                      mtu_bytes,
                                  const struct CanardMemoryResource memory);

/// This function serializes a transfer into a sequence of transport frames and inserts them into the prioritized
/// transmission queue at the appropriate position. Afterwards, the application is supposed to take the enqueued frames
/// from the transmission queue using the function canardTxPeek() and transmit them. Each transmitted (or otherwise
/// discarded, e.g., due to timeout) frame should be removed from the queue using canardTxPop(). The queue is
/// prioritized following the normal CAN frame arbitration rules to avoid the inner priority inversion. The transfer
/// payload will be copied into the transmission queue so that the lifetime of the frames is not related to the
/// lifetime of the input payload buffer.
///
/// The MTU of the generated frames is dependent on the value of the MTU setting at the time when this function
/// is invoked. The MTU setting can be changed arbitrarily between invocations.
///
/// The tx_deadline_usec will be used to populate the timestamp values of the resulting transport
/// frames (so all frames will have the same timestamp value). This feature is intended to facilitate transmission
/// deadline tracking, i.e., aborting frames that could not be transmitted before the specified deadline.
/// Therefore, normally, the timestamp value should be in the future.
/// The library uses `now > deadline` comparison to determine which frames timed out, and so could
/// be dropped (incrementing `CanardTxQueueStats::dropped_frames` field per such a frame).
/// If this timeout behavior is not needed, the timestamp value can be set to zero.
///
/// The described above automatic dropping of timed-out frames was added in the v4 of the library as an optional
/// feature. It is applied only to the frames that are already in the TX queue (not the new ones that are being pushed
/// in this call). The feature can be disabled by passing zero time in the `now_usec` parameter,
/// so that it will be up to the application to track the `tx_deadline_usec` (see `canardTxPeek`).
///
/// The function returns the number of frames enqueued into the prioritized TX queue (which is always a positive
/// number) in case of success (so that the application can track the number of items in the TX queue if necessary).
/// In case of failure, the function returns a negated error code: either invalid argument or out-of-memory.
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
/// An out-of-memory error is returned if a TX frame could not be allocated due to the memory being exhausted,
/// or if the capacity of the queue would be exhausted by this operation. In such cases, all frames allocated for
/// this transfer (if any) will be deallocated automatically. In other words, either all frames of the transfer are
/// enqueued successfully, or none are.
///
/// The time complexity is O(p + log e), where p is the amount of payload in the transfer, and e is the number of
/// frames already enqueued in the transmission queue.
///
/// The memory allocation requirement is two allocations per transport frame. A single-frame transfer takes two
/// allocations; a multi-frame transfer of N frames takes N*2 allocations. In each pair of allocations:
/// - the first allocation is for CanardTxQueueItem; the size is `sizeof(CanardTxQueueItem)`;
///   the Canard instance memory resource is used for this allocation (and later for deallocation);
/// - the second allocation is for payload storage (the frame data) - size is normally MTU but could be less for
///   the last frame of the transfer; the TX queue memory resource is used for this allocation.
int32_t canardTxPush(struct CanardTxQueue* const                que,
                     const struct CanardInstance* const         ins,
                     const CanardMicrosecond                    tx_deadline_usec,
                     const struct CanardTransferMetadata* const metadata,
                     const struct CanardPayload                 payload,
                     const CanardMicrosecond                    now_usec);

/// This function accesses the top element of the prioritized transmission queue. The queue itself is not modified
/// (i.e., the accessed element is not removed). The application should invoke this function to collect the transport
/// frames of serialized transfers pushed into the prioritized transmission queue by canardTxPush().
///
/// The timestamp values of returned frames are initialized with tx_deadline_usec from canardTxPush().
/// Timestamps are used to specify the transmission deadline. It is up to the application and/or the media layer
/// to implement the discardment of timed-out transport frames. The library does not check it in this call,
/// so a frame that is already timed out may be returned here.
///
/// If the queue is empty or if the argument is NULL, the returned value is NULL.
///
/// If the queue is non-empty, the returned value is a pointer to its top element (i.e., the next frame to transmit).
/// The returned pointer points to an object allocated in the dynamic storage; it should be eventually freed by the
/// application by calling `canardTxFree`. The memory shall not be freed before the entry is removed
/// from the queue by calling canardTxPop(); this is because until canardTxPop() is executed, the library retains
/// ownership of the object. The pointer retains validity until explicitly freed by the application; in other words,
/// calling canardTxPop() does not invalidate the object.
///
/// The payload buffer is allocated in the dynamic storage of the queue. The application may transfer ownership of
/// the payload to a different application component (f.e. to transmission media) by copying the pointer and then
/// (if the ownership transfer was accepted) by nullifying payload fields of the frame (`data` & `allocated_size`).
/// If these fields stay with their original values, the `canardTxFree` (after proper `canardTxPop` of course) will
/// deallocate the payload buffer. In any case, the payload has to be eventually deallocated by the TX queue memory
/// resource. It will be automatically done by the `canardTxFree` (if the payload still stays in the item),
/// OR if moved, it is the responsibility of the application to eventually (f.e. at the end of transmission) deallocate
/// the memory with TX queue memory resource. Note that the mentioned above nullification of the payload fields is the
/// only reason why a returned TX item pointer is mutable. It was constant in the past (before v4),
/// but it was changed to be mutable to allow the payload ownership transfer.
///
/// The time complexity is logarithmic of the queue size. This function does not invoke the dynamic memory manager.
struct CanardTxQueueItem* canardTxPeek(const struct CanardTxQueue* const que);

/// This function transfers the ownership of the specified element of the prioritized transmission queue from the queue
/// to the application. The element does not necessarily need to be the top one -- it is safe to dequeue any element.
/// The element is dequeued but not invalidated; it is the responsibility of the application to deallocate the
/// memory used by the object later (use `canardTxFree` helper). The memory SHALL NOT be deallocated UNTIL this function
/// is invoked. The function returns the same pointer that it is given.
///
/// If any of the arguments are NULL, the function has no effect and returns NULL.
///
/// The time complexity is logarithmic of the queue size. This function does not invoke the dynamic memory manager.
struct CanardTxQueueItem* canardTxPop(struct CanardTxQueue* const que, struct CanardTxQueueItem* const item);

/// This is a helper that frees the memory allocated (from the instance memory) for the item,
/// as well as the internal frame payload buffer (if any) associated with it (using TX queue memory).
/// If the item argument is NULL, the function has no effect. The time complexity is constant.
/// If the item frame payload is NULL then it is assumed that the payload buffer was already freed,
/// or moved to a different owner (f.e. to media layer).
void canardTxFree(struct CanardTxQueue* const        que,
                  const struct CanardInstance* const ins,
                  struct CanardTxQueueItem* const    item);

/// This is a helper that combines several Canard TX calls (`canardTxPeek`, `canardTxPop` and `canardTxFree`)
/// into one "polling" algorithm. It simplifies the whole process of transmitting frames to just two function calls:
/// - `canardTxPush` to enqueue the frames
/// - `canardTxPoll` to dequeue, transmit and free a single frame
///
/// The algorithm implements a typical pattern of de-queuing, transmitting and freeing a TX queue item,
/// as well as handling transmission failures, retries, and deadline timeouts.
///
/// The function is intended to be periodically called, most probably on a signal that the previous TX frame
/// transmission has been completed, and so the next TX frame (if any) could be polled from the TX queue.
///
/// The current time is used to determine if the frame has timed out. Use zero value to disable automatic dropping
/// of timed-out frames. The user reference will be passed to the frame handler (see CanardTxFrameHandler), which
/// will be called to transmit the frame.
///
/// Return value is zero if the queue is empty,
/// or `-CANARD_ERROR_INVALID_ARGUMENT` if there is no (NULL) queue, instance or handler.
/// Otherwise, the value will be from the result of the frame handler call (see CanardTxFrameHandler).
///
int8_t canardTxPoll(struct CanardTxQueue* const        que,
                    const struct CanardInstance* const ins,
                    const CanardMicrosecond            now_usec,
                    void* const                        user_reference,
                    const CanardTxFrameHandler         frame_handler);

/// This function implements the transfer reassembly logic. It accepts a transport frame from any of the redundant
/// interfaces, locates the appropriate subscription state, and, if found, updates it. If the frame completed a
/// transfer, the return value is 1 (one) and the out_transfer pointer is populated with the parameters of the
/// newly reassembled transfer. The transfer reassembly logic is defined in the Cyphal specification.
///
/// The MTU of the accepted frame can be arbitrary; that is, any MTU is accepted. The DLC validity is irrelevant.
///
/// Any value of redundant_iface_index is accepted; that is, up to 256 redundant interfaces are supported.
/// The index of the interface from which the transfer is accepted is always the same as redundant_iface_index
/// of the current invocation, so the application can always determine which interface has delivered the transfer.
///
/// Upon return, the out_subscription pointer will point to the instance of CanardRxSubscription that accepted this
/// frame; if no matching subscription exists (i.e., frame discarded), the pointer will be NULL.
/// If this information is not relevant, set out_subscription to NULL.
/// The purpose of this argument is to allow integration with OOP adapters built on top of libcanard; see also the
/// user_reference provided in CanardRxSubscription.
///
/// The function invokes the dynamic memory manager in the following cases only:
///
///     1. New memory for a session state object is allocated when a new session is initiated.
///        This event occurs when a transport frame that matches a known subscription is received from a node that
///        did not emit matching frames since the subscription was created.
///        Once a new session is created, it is not destroyed until the subscription is terminated by invoking
///        canardRxUnsubscribe(). The number of sessions is bounded and the bound is low (at most the number of nodes
///        in the network minus one), also the size of a session instance is very small, so the removal is unnecessary.
///        Real-time networks typically do not change their configuration at runtime, so it is possible to reduce
///        the time complexity by never deallocating sessions.
///        The size of a session instance is at most 48 bytes on any conventional platform (typically much smaller).
///
///     2. New memory for the transfer payload buffer is allocated when a new transfer is initiated, unless the buffer
///        was already allocated at the time.
///        This event occurs when a transport frame that matches a known subscription is received and it begins a
///        new transfer (that is, the start-of-frame flag is set and it is not a duplicate).
///        The amount of the allocated memory equals the extent as configured via canardRxSubscribe(); please read
///        its documentation for further information about the extent and related edge cases.
///        The worst case occurs when every node on the bus initiates a multi-frame transfer for which there is a
///        matching subscription: in this case, the library will allocate number_of_nodes allocations, where each
///        allocation is the same size as the configured extent.
///
///     3. Memory allocated for the transfer payload buffer may be deallocated at the discretion of the library.
///        This operation does not increase the worst case execution time and does not improve the worst case memory
///        consumption, so a deterministic application need not consider this behavior in the resource analysis.
///        This behavior is implemented for the benefit of applications where rigorous characterization is unnecessary.
///
/// The worst case dynamic memory consumption per subscription is:
///
///     (sizeof(session instance) + extent) * number_of_nodes
///
/// Where sizeof(session instance) and extent are defined above, and number_of_nodes is the number of remote
/// nodes emitting transfers that match the subscription (which cannot exceed (CANARD_NODE_ID_MAX-1) by design).
/// If the dynamic memory pool is sized correctly, the application is guaranteed to never encounter an
/// out-of-memory (OOM) error at runtime. The actual size of the dynamic memory pool is typically larger;
/// for a detailed treatment of the problem and the related theory please refer to the documentation of O1Heap --
/// a deterministic memory allocator for hard real-time embedded systems.
///
/// The time complexity is O(p + log n) where n is the number of subject-IDs or service-IDs subscribed to by the
/// application, depending on the transfer kind of the supplied frame, and p is the amount of payload in the received
/// frame (because it will be copied into an internal contiguous buffer). Observe that the time complexity is
/// invariant to the network configuration (such as the number of online nodes) -- this is a very important
/// design guarantee for real-time applications because the execution time is dependent only on the number of
/// active subscriptions for a given transfer kind, and the MTU, both of which are easy to predict and account for.
/// Excepting the subscription search and the payload data copying, the entire RX pipeline contains neither loops
/// nor recursion.
/// Misaddressed and malformed frames are discarded in constant time.
///
/// The function returns 1 (one) if the new frame completed a transfer. In this case, the details of the transfer
/// are stored into out_transfer, and the transfer payload buffer ownership is passed to that object. The lifetime
/// of the resulting transfer object is not related to the lifetime of the input transport frame (that is, even if
/// it is a single-frame transfer, its payload is copied out into a new dynamically allocated buffer storage).
/// If the extent is zero, the payload pointer may be NULL, since there is no data to store and so a
/// buffer is not needed. The application is responsible for deallocating the payload buffer when the processing
/// is done by invoking memory_free on the transfer payload pointer.
///
/// The function returns a negated out-of-memory error if it was unable to allocate dynamic memory.
///
/// The function does nothing and returns a negated invalid argument error immediately if any condition is true:
///     - Any of the input arguments that are pointers are NULL.
///     - The payload pointer of the input frame is NULL while its size is non-zero.
///     - The CAN ID of the input frame is not less than 2**29=0x20000000.
///
/// The function returns zero if any of the following conditions are true (the general policy is that protocol
/// errors are not escalated because they do not construe a node-local error):
///     - The received frame is not a valid Cyphal/CAN transport frame.
///     - The received frame is a valid Cyphal/CAN transport frame, but there is no matching subscription,
///       the frame did not complete a transfer, the frame forms an invalid frame sequence, the frame is a duplicate,
///       the frame is unicast to a different node (address mismatch).
int8_t canardRxAccept(struct CanardInstance* const        ins,
                      const CanardMicrosecond             timestamp_usec,
                      const struct CanardFrame* const     frame,
                      const uint8_t                       redundant_iface_index,
                      struct CanardRxTransfer* const      out_transfer,
                      struct CanardRxSubscription** const out_subscription);

/// This function creates a new subscription, allowing the application to register its interest in a particular
/// category of transfers. The library will reject all transport frames for which there is no active subscription.
/// The reference out_subscription shall retain validity until the subscription is terminated (the referred object
/// cannot be moved or destroyed).
///
/// If such subscription already exists, it will be removed first as if canardRxUnsubscribe() was
/// invoked by the application, and then re-created anew with the new parameters.
///
/// The extent defines the size of the transfer payload memory buffer; or, in other words, the maximum possible size
/// of received objects, considering also possible future versions with new fields. It is safe to pick larger values.
/// Note well that the extent is not the same thing as the maximum size of the object, it is usually larger!
/// Transfers that carry payloads that exceed the specified extent will be accepted anyway but the excess payload
/// will be truncated away, as mandated by the Specification. The transfer CRC is always validated regardless of
/// whether its payload is truncated.
///
/// The default transfer-ID timeout value is defined as CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC; use it if not sure.
/// The redundant interface fail-over timeout (if redundant interfaces are used) is the same as the transfer-ID timeout.
/// It may be reduced in a future release of the library, but it will not affect the backward compatibility.
///
/// The return value is 1 if a new subscription has been created as requested.
/// The return value is 0 if such subscription existed at the time the function was invoked. In this case,
/// the existing subscription is terminated and then a new one is created in its place. Pending transfers may be lost.
/// The return value is a negated invalid argument error if any of the input arguments are invalid.
///
/// The time complexity is logarithmic from the number of current subscriptions under the specified transfer kind.
/// This function does not allocate new memory. The function may deallocate memory if such subscription already
/// existed; the deallocation behavior is specified in the documentation for canardRxUnsubscribe().
///
/// Subscription instances have large look-up tables to ensure that the temporal properties of the algorithms are
/// invariant to the network configuration (i.e., a node that is validated on a network containing one other node
/// will provably perform identically on a network that contains X nodes). This is a conscious time-memory trade-off.
int8_t canardRxSubscribe(struct CanardInstance* const       ins,
                         const enum CanardTransferKind      transfer_kind,
                         const CanardPortID                 port_id,
                         const size_t                       extent,
                         const CanardMicrosecond            transfer_id_timeout_usec,
                         struct CanardRxSubscription* const out_subscription);

/// This function reverses the effect of canardRxSubscribe().
/// If the subscription is found, all its memory is de-allocated (session states and payload buffers); to determine
/// the amount of memory freed, please refer to the memory allocation requirement model of canardRxAccept().
///
/// The return value is 1 if such subscription existed (and, therefore, it was removed).
/// The return value is 0 if such subscription does not exist. In this case, the function has no effect.
/// The return value is a negated invalid argument error if any of the input arguments are invalid.
///
/// The time complexity is logarithmic from the number of current subscriptions under the specified transfer kind.
/// This function does not allocate new memory.
int8_t canardRxUnsubscribe(struct CanardInstance* const  ins,
                           const enum CanardTransferKind transfer_kind,
                           const CanardPortID            port_id);

/// This function allows to check the effect of canardRxSubscribe() and canardRxUnsubscribe().
///
/// The return value is 1 if the specified subscription exists, 0 otherwise.
/// The return value is a negated invalid argument error if any of the input arguments are invalid.
/// Output out_subscription could be NULL, but if it is not, it will be populated with the pointer to the existing
/// subscription. In case the subscription does not exist (or error), out_subscription won't be touched.
/// Result pointer to the subscription is valid until the subscription is terminated.
///
/// The time complexity is logarithmic from the number of current subscriptions under the specified transfer kind.
/// This function does not allocate new memory.
int8_t canardRxGetSubscription(struct CanardInstance* const        ins,
                               const enum CanardTransferKind       transfer_kind,
                               const CanardPortID                  port_id,
                               struct CanardRxSubscription** const out_subscription);

/// Utilities for generating CAN controller hardware acceptance filter configurations
/// to accept specific subjects, services, or nodes.
///
/// Complex applications will likely subscribe to more subject IDs than there are
/// acceptance filters available in the CAN hardware. In this case, the application
/// should implement filter consolidation. See canardConsolidateFilters()
/// as well as the Cyphal specification for details.

/// Generate an acceptance filter configuration to accept a specific subject ID.
struct CanardFilter canardMakeFilterForSubject(const CanardPortID subject_id);

/// Generate an acceptance filter configuration to accept both requests and responses for a specific service.
///
/// Users may prefer to instead use a catch-all acceptance filter configuration for accepting
/// all service requests and responses targeted at the specified local node ID.
/// See canardMakeFilterForServices() for this.
struct CanardFilter canardMakeFilterForService(const CanardPortID service_id, const CanardNodeID local_node_id);

/// Generate an acceptance filter configuration to accept all service
/// requests and responses targeted to the specified local node ID.
///
/// Due to the relatively low frequency of service transfers expected on a network,
/// and the fact that a service directed at a specific node is not likely to be rejected by that node,
/// a user may prefer to use this over canardMakeFilterForService()
/// in order to simplify the API usage and reduce the number of required hardware CAN acceptance filters.
struct CanardFilter canardMakeFilterForServices(const CanardNodeID local_node_id);

/// Consolidate two acceptance filter configurations into a single configuration.
///
/// Complex applications will likely subscribe to more subject IDs than there are
/// acceptance filters available in the CAN hardware. In this case, the application
/// should implement filter consolidation. While this may make it impossible to create
/// a 'perfect' filter that only accepts desired subject IDs, the application should apply
/// consolidation in a manner that minimizes the number of undesired messages that pass
/// through the hardware acceptance filters and require software filtering (implemented by canardRxSubscribe).
///
/// While optimal choice of filter consolidation is a function of the number of available hardware filters,
/// the set of transfers needed by the application, and the expected frequency of occurrence
/// of all possible distinct transfers on the bus, it is possible to generate a quasi-optimal configuration
/// if information about the frequency of occurrence of different transfers is not known.
/// For details, see the "Automatic hardware acceptance filter configuration" note under the Cyphal/CAN section
/// in the Transport Layer chapter of the Cyphal specification.
struct CanardFilter canardConsolidateFilters(const struct CanardFilter* const a, const struct CanardFilter* const b);

#ifdef __cplusplus
}
#endif
#endif
