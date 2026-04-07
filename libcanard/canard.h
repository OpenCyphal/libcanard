///                            ____                   ______            __          __
///                           / __ `____  ___  ____  / ____/_  ______  / /_  ____  / /
///                          / / / / __ `/ _ `/ __ `/ /   / / / / __ `/ __ `/ __ `/ /
///                         / /_/ / /_/ /  __/ / / / /___/ /_/ / /_/ / / / / /_/ / /
///                         `____/ .___/`___/_/ /_/`____/`__, / .___/_/ /_/`__,_/_/
///                             /_/                     /____/_/
///
/// Libcanard is a compact implementation of the Cyphal/CAN transport for high-integrity real-time embedded systems.
/// It is designed for use in robust deterministic embedded systems equipped with at least 32K ROM and RAM.
/// The library is designed to be compatible with any target platform and instruction set architecture, from 8 to 64
/// bit, little- and big-endian, RTOS-based or baremetal, etc., as long as there is a standards-compliant C compiler.
///
/// The library offers a non-blocking callback-based API. It is not thread-safe: if used in a concurrent environment,
/// it is the responsibility of the application to provide adequate synchronization.
///
/// The library supports both Cyphal v1 and the legacy UAVCAN v0 (aka DroneCAN) protocol versions,
/// which can be used simultaneously on the same bus, with one limitation that a single node cannot send transfers
/// of both versions simultaneously (unless certain constraints are satisfied as discussed below). If an application
/// needs to send transfers of both versions simultaneously, the recommended solution is to run two node instances.
///
/// The library is intended to be integrated into the end application by simply copying its source files into the
/// source tree of the project; it does not require any special compilation options and should work out of the box.
/// There are build-time configuration parameters defined near the top of canard.c, but they are safe to ignore.
///
/// --------------------------------------------------------------------------------------------------------------------
/// This software is distributed under the terms of the MIT License.
/// Copyright (c) OpenCyphal.
/// Author: Pavel Kirienko <pavel@opencyphal.org>
/// Contributors: https://github.com/OpenCyphal/libcanard/contributors

#ifndef CANARD_H_INCLUDED
#define CANARD_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define CANARD_VERSION_MAJOR 5
#define CANARD_VERSION_MINOR 0

#define CANARD_CYPHAL_VERSION_MAJOR 1
#define CANARD_CYPHAL_VERSION_MINOR 1

/// The library will support at most this many local redundant network interfaces.
/// This parameter affects the size of several heap-allocated structures.
/// It is safe to pick any large value but the heap memory footprint will increase accordingly.
#ifndef CANARD_IFACE_COUNT
#define CANARD_IFACE_COUNT 2U
#endif
#if (CANARD_IFACE_COUNT < 1) || (CANARD_IFACE_COUNT > 8)
#error "CANARD_IFACE_COUNT must be in the range [1, 8]"
#endif
#define CANARD_IFACE_BITMAP_ALL ((1U << CANARD_IFACE_COUNT) - 1U)

/// Parameter ranges are inclusive; the lower bound is zero for all.
#define CANARD_SUBJECT_ID_MAX     0xFFFFU // Applies to Cyphal v1.1 and UAVCAN v0/DroneCAN message data type IDs.
#define CANARD_SUBJECT_ID_MAX_13b 8191U   // Cyphal v1.0 supports only 13-bit subject-IDs.
#define CANARD_SERVICE_ID_MAX     511U    // Applies to Cyphal, all versions. In v0 this is narrower.
#define CANARD_NODE_ID_MAX        127U
#define CANARD_NODE_ID_CAPACITY   (CANARD_NODE_ID_MAX + 1U)
#define CANARD_TRANSFER_ID_BITS   5U
#define CANARD_TRANSFER_ID_MODULO (1U << CANARD_TRANSFER_ID_BITS)
#define CANARD_TRANSFER_ID_MAX    (CANARD_TRANSFER_ID_MODULO - 1U)

/// This is used only with Cyphal v1.0 and legacy v0 protocols to indicate anonymous messages.
/// This library can receive anonymous messages but it cannot transmit them; it implements a new, simpler stateless
/// node-ID autoconfiguration protocol instead that makes anonymous messages unnecessary.
#define CANARD_NODE_ID_ANONYMOUS 0xFFU

/// This is the recommended transfer-ID timeout value given in the Cyphal Specification. The application may choose
/// different values per subscription (i.e., per data specifier) depending on its timing requirements.
/// Within this timeout, the library will refuse to accept a transfer with the same transfer-ID as the last one
/// received on the same subject from the same source node.
#define CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us 2000000UL

/// MTU values for the supported protocols.
/// Per the recommendations given in the Cyphal/CAN Specification, other MTU values should not be used.
#define CANARD_MTU_CAN_CLASSIC 8U
#define CANARD_MTU_CAN_FD      64U

/// All valid transfer kind and version combinations.
///
/// Distinct message types use separate ID spaces; i.e., any given ID may be used with distinct message kinds without
/// collision/ambiguity. For example, in the original Cyphal v1.0, subject-ID 7509 is assigned to the heartbeat message,
/// while in v1.1 there are no fixed subject-IDs at all and 7509 has no special meaning. Likewise, the data type ID
/// of the legacy UAVCAN v0 is a separate ID space.
///
/// Request and response under the same protocol version share the same ID space.
typedef enum canard_kind_t
{
    canard_kind_message_16b = 0, ///< 16-bit subject-ID message introduced in Cyphal v1.1. Isolated subject-ID space.
    canard_kind_message_13b = 1, ///< 13-bit subject-ID message originally defined in Cyphal v1.0.
    canard_kind_response    = 2, ///< Cyphal v1 RPC-service response.
    canard_kind_request     = 3, ///< Cyphal v1 RPC-service request.
    // Legacy DroneCAN/UAVCAN v0 transfer kinds.
    canard_kind_v0_message  = 4,
    canard_kind_v0_response = 5,
    canard_kind_v0_request  = 6,
} canard_kind_t;
#define CANARD_KIND_COUNT 7

static inline uint_least8_t canard_kind_version(const canard_kind_t kind)
{
    return (kind < canard_kind_v0_message) ? 1 : 0;
}

typedef struct canard_t canard_t;

/// Monotonic time in microseconds; the current time is never negative.
typedef int64_t canard_us_t;

/// Length to DLC rounds up.
extern const uint_least8_t canard_dlc_to_len[16];
extern const uint_least8_t canard_len_to_dlc[65];

typedef enum canard_prio_t
{
    canard_prio_exceptional = 0,
    canard_prio_immediate   = 1,
    canard_prio_fast        = 2,
    canard_prio_high        = 3,
    canard_prio_nominal     = 4, ///< Nominal priority level should be the default.
    canard_prio_low         = 5,
    canard_prio_slow        = 6,
    canard_prio_optional    = 7,
} canard_prio_t;
#define CANARD_PRIO_COUNT 8U
#define CANARD_PRIO_BITS  3U

typedef struct canard_tree_t
{
    struct canard_tree_t* up;
    struct canard_tree_t* lr[2];
    int_fast8_t           bf;
} canard_tree_t;

typedef struct canard_listed_t canard_listed_t;
typedef struct canard_list_t   canard_list_t;
struct canard_listed_t
{
    canard_listed_t* next;
    canard_listed_t* prev;
};
struct canard_list_t
{
    canard_listed_t* head; ///< NULL if list empty
    canard_listed_t* tail; ///< NULL if list empty
};

typedef struct canard_bytes_t       canard_bytes_t;
typedef struct canard_bytes_chain_t canard_bytes_chain_t;
typedef struct canard_bytes_mut_t   canard_bytes_mut_t;
struct canard_bytes_t
{
    size_t      size;
    const void* data;
};
struct canard_bytes_chain_t
{
    canard_bytes_t              bytes;
    const canard_bytes_chain_t* next; ///< NULL in the last fragment.
};
struct canard_bytes_mut_t
{
    size_t size;
    void*  data;
};

/// canard_mem_t models a memory resource for allocating a particular kind of objects used by the library.
/// The semantics are similar to malloc/free. It is designed to be passed by value.
/// Consider using O1Heap: https://github.com/pavel-kirienko/o1heap.
/// The API documentation is written on the assumption that the memory management functions are O(1).
typedef struct canard_mem_t        canard_mem_t;
typedef struct canard_mem_vtable_t canard_mem_vtable_t;
struct canard_mem_vtable_t
{
    void (*free)(canard_mem_t, size_t, void*);
    void* (*alloc)(canard_mem_t, size_t);
};
struct canard_mem_t
{
    const canard_mem_vtable_t* vtable;
    void*                      context;
};

/// Represents received transfer payload.
/// The application shall access the useful payload through view and use origin only for lifetime management.
/// For multi-frame transfers, the view points into dynamically allocated rx_payload storage and origin owns it;
/// release non-empty origin with the same resource used for canard_mem_set_t::rx_payload.
/// For single-frame transfers, the view is pointing into the CAN frame data buffer passed by the application via
/// canard_ingest_frame(), and the storage is NULL/empty. The lifetime of the view ends upon return from the callback.
/// The application must manually copy the data if it needs to outlive the callback.
typedef struct canard_payload_t
{
    canard_bytes_t     view;   ///< Use this to access the data.
    canard_bytes_mut_t origin; ///< Use this to free the memory, unless NULL/empty.
} canard_payload_t;

/// The filter only matches extended CAN IDs on data frames (no std/rtr). Bits above 29 are always zero.
typedef struct canard_filter_t
{
    uint32_t extended_can_id;
    uint32_t extended_mask;
} canard_filter_t;

/// Each resource is used for allocating memory for a specific purpose.
/// This enables fine-tuning in memory-conscious applications.
/// Ordinary applications can use the same resource for everything; alloc/free are assumed O(1) [e.g., use o1heap].
typedef struct canard_mem_set_t
{
    canard_mem_t tx_transfer; ///< TX transfer objects, fixed-size, one per enqueued transfer.
    canard_mem_t tx_frame;    ///< One per enqueued frame, at least one per TX transfer, size MTU+overhead.
    canard_mem_t rx_session;  ///< Remote-associated sessions per subscriber, fixed-size.
    canard_mem_t rx_payload;  ///< Variable-size, max size approx. extent+sizeof(rx_slot_t).
    canard_mem_t rx_filters;  ///< For canard_filter_t[filter_count] temporary storage. Not needed if filters not used.
} canard_mem_set_t;

typedef struct canard_subscription_t        canard_subscription_t;
typedef struct canard_subscription_vtable_t canard_subscription_vtable_t;
struct canard_subscription_vtable_t
{
    /// A new message is received on a subscription.
    /// For the payload ownership notes refer to canard_payload_t.
    /// The timestamp is the arrival timestamp of the first frame of the transfer.
    void (*on_message)(canard_subscription_t* self,
                       canard_us_t            timestamp,
                       canard_prio_t          priority,
                       uint_least8_t          source_node_id,
                       uint_least8_t          transfer_id,
                       canard_payload_t       payload);
};

/// Subscription instances must not be moved while in use.
/// Each subscription is indexed by its port-ID inside the canard instance, and in turn contains a tree of sessions
/// indexed by remote node-ID. Two log-time lookups are thus required to handle an incoming frame.
/// None of the fields may be mutated by the application after initialization except for the user context and extent.
struct canard_subscription_t
{
    canard_tree_t index_port_id; ///< Must be the first member.

    canard_us_t   transfer_id_timeout;
    size_t        extent;   ///< May be changed at any time; in-flight slots retain the old value.
    uint16_t      port_id;  ///< Represents subjects, services, and legacy message- and service data type IDs.
    uint16_t      crc_seed; ///< For v0 this is set at subscription time, for v1 this is always 0xFFFF.
    canard_kind_t kind;

    canard_t*                           owner;
    canard_tree_t*                      sessions;
    const canard_subscription_vtable_t* vtable;

    void* user_context;
};

typedef struct canard_vtable_t
{
    /// The current monotonic time in microseconds. Must be a non-negative non-decreasing value.
    canard_us_t (*now)(const canard_t*);

    /// Submit one CAN frame for transmission via the specified interface.
    /// If the data is empty (size==0), the data pointer may be NULL.
    /// Returns true if the frame was accepted for transmission, false if there is no free mailbox (try again later).
    /// The callback must not mutate the TX pipeline (no publish/cancel/free/etc).
    /// If the can_data needs to be retained for later retransmission, use canard_refcount_inc()/canard_refcount_dec().
    bool (*tx)(canard_t*,
               void*          user_context,
               canard_us_t    deadline,
               uint_least8_t  iface_index,
               bool           fd,
               uint32_t       extended_can_id,
               canard_bytes_t can_data);

    /// Reconfigure the acceptance filters of the CAN controller hardware.
    /// The prior configuration, if any, is replaced entirely.
    /// filter_count is guaranteed to not exceed the value given at initialization.
    /// This function may be NULL if the CAN controller/driver does not support filtering or it is not desired.
    /// This function is only invoked from canard_poll().
    /// Returns true on success, false on failure.
    bool (*filter)(canard_t*, size_t filter_count, const canard_filter_t* filters);
} canard_vtable_t;

/// Main instance object. Usage: new -> subscribe/publish/ingest/poll -> unsubscribe/destroy.
/// Dominant costs are log-time tree operations plus work linear in the number of CAN frames in a transfer.
/// Heap use scales with queued TX transfers/frames and active RX sessions/reassembly slots, not bus history.
/// None of the fields should be mutated by the application, unless explicitly allowed.
struct canard_t
{
    uint64_t node_id_occupancy_bitmap[2];

    /// By default, the node-ID is allocated automatically, with occupancy/collision tracking.
    /// Automatic allocation will avoid using the node-ID of zero to ensure compatibility with legacy v0 nodes, where
    /// zero is reserved for anonymous nodes. Zero can still be assigned manually if compatibility is not needed.
    /// The node-ID can be set manually via the corresponding function.
    uint_least8_t node_id;

    struct
    {
        /// By default, CAN FD mode is used; this flag can be used to change the mode to Classic CAN if needed;
        /// for example, if the local CAN controller does not support CAN FD, or if the remote nodes do not support it.
        /// The flag can be switched at any time. All redundant interfaces share the same mode.
        ///
        /// A valid auto-configuration strategy that could be implemented in the application is to start in FD mode
        /// and switch to Classic if a non-FD frame is observed on the bus.
        ///
        /// The local node can accept both Classic CAN and CAN FD frames regardless of this setting;
        /// the setting only affects the mode used for outgoing frames.
        ///
        /// Legacy v0 transfers (UAVCAN/DroneCAN) are always sent in Classic CAN mode regardless of this flag,
        /// because UAVCAN v0 does not define CAN FD support. CAN FD v0 transfers can still be received though.
        bool fd;

        /// Queue size and capacity are measured in CAN frames for convenience, but the TX pipeline actually operates
        /// on whole transfers for efficiency. The number of enqueued frames is a pretty much synthetic metric for
        /// convenience, that is derived from the number of enqueued transfers and their sizes.
        size_t queue_capacity;
        size_t queue_size;

        /// Incremented with every enqueued transfer. Used internally but also works as a stats counter.
        uint64_t seqno;

        canard_tree_t* pending[CANARD_IFACE_COUNT]; ///< Next to transmit on the left.
        canard_tree_t* deadline;                    ///< Soonest to expire on the left.
        canard_list_t  agewise;                     ///< ALL transfers FIFO, oldest at the head.
    } tx;

    struct
    {
        canard_tree_t* subscriptions[CANARD_KIND_COUNT];
        canard_list_t  list_session_by_animation; ///< Oldest at the head.
        size_t         filter_count;
        bool           filters_dirty; ///< Set when subscribed/unsubscribed or node-ID is changed.
    } rx;

    /// Error counters incremented automatically when the corresponding error condition occurs.
    /// These counters are never decremented by the library but they can be reset by the application if needed.
    struct
    {
        uint64_t oom;           ///< Out of memory; a transfer could have been lost.
        uint64_t tx_capacity;   ///< A transfer could not be enqueued due to queue capacity limit.
        uint64_t tx_sacrifice;  ///< An old pending transfer had to be sacrificed to make room for a new transfer.
        uint64_t tx_expiration; ///< A transfer had to be dequeued due to deadline expiration.
        uint64_t rx_frame;      ///< A received frame was malformed and thus dropped.
        uint64_t rx_transfer;   ///< A transfer could not be reassembled correctly.
        uint64_t collision;     ///< Number of times the local node-ID was changed to repair a collision.
    } err;

    canard_mem_set_t mem;
    uint64_t         prng_state;

    const canard_vtable_t* vtable;

    void* user_context;
};

/// The TX queue is shared between all redundant interfaces with deduplication (each frame is enqueued only once).
/// The capacity set here is therefore the total capacity across all interfaces.
///
/// The PRNG seed must be likely to be distinct per node on the network; it may be a constant value.
/// In the absence of a true RNG, a good way to obtain the seed is to use a unique hardware identifier hashed
/// down to 64 bits with a good hash, e.g., rapidhash.
///
/// The local node-ID will be chosen randomly by default, and the stack will monitor the network for node-ID occupancy
/// and collisions, and will automatically migrate to a free node-ID shall a collision be detected.
/// If manual allocation is desired, use the corresponding function to set the node-ID after initialization.
///
/// The filter count is the number of CAN acceptance filters that the stack can utilize. It is possible to pass zero
/// filters if filtering is unneeded/unsupported. When the number of active subscriptions exceeds the number of
/// available filters, filter coalescence is performed, which however has a high complexity bound; it is thus
/// recommended that the number of filters is either large enough to accommodate all subscriptions,
/// or small enough in the single digits where the coalescence load remains low. The filter configuration is
/// recomputed and applied on every poll() following a change in the subscription set or local node-ID.
///
/// Steady-state heap use is O(queued TX transfers + queued TX frames + active RX sessions + RX slots).
/// Each RX slot is bounded by the subscription extent; filter recomputation may dominate only if coalescence is needed.
///
/// CAN FD mode is selected by default for outgoing frames; override the fd flag to change the mode if needed.
///
/// Returns true on success, false if any of the parameters are invalid.
bool canard_new(canard_t* const              self,
                const canard_vtable_t* const vtable,
                const canard_mem_set_t       memory,
                const size_t                 tx_queue_capacity,
                const uint64_t               prng_seed,
                const size_t                 filter_count);

/// The application MUST destroy all subscriptions before invoking this (this is asserted).
/// The application MUST also release all retained TX frame views before invoking this.
/// The TX queue will be purged automatically if not empty.
void canard_destroy(canard_t* const self);

/// This can be invoked after initialization to manually assign the desired node-ID.
/// This does not disable the occupancy/collision monitoring; the assigned ID will be changed if a collision is found.
/// Started multi-frame TX continuations are canceled and filter reconfiguration is scheduled at next canard_poll().
/// Anonymous node-ID is not allowed. Zero is fine only if compatibility with legacy protocol is not needed.
/// Returns false if any of the arguments are invalid.
bool canard_set_node_id(canard_t* const self, const uint_least8_t node_id);

/// This must be invoked periodically to ensure liveliness.
/// The function must be called asap once any of the interfaces for which there are pending outgoing transfers
/// become writable, and not less frequently than once in a few milliseconds. The invocation rate defines the
/// resolution of deadline handling.
/// Work is proportional to expired/pending TX work; dirty RX filters may add subscription-dependent one-time cost.
/// This is also where deferred hardware filter reconfiguration is attempted.
void canard_poll(canard_t* const self, const uint_least8_t tx_ready_iface_bitmap);

/// Returns a bitmap of interfaces that have pending transmissions. This is useful for IO multiplexing.
uint_least8_t canard_pending_ifaces(const canard_t* const self);

/// True if successfully processed, false if any of the arguments are invalid.
/// Other failures are reported via the counters.
/// This function should not be invoked from the callbacks.
/// Subscription callbacks may run synchronously before this function returns.
/// The lifetime of can_data can end after this function returns.
/// Dominant steady-state cost is log-time subscription/session lookup;
/// RX memory is allocated only when new state is needed, and multi-frame payload ownership is transferred via origin.
bool canard_ingest_frame(canard_t* const      self,
                         const canard_us_t    timestamp,
                         const uint_least8_t  iface_index,
                         const uint32_t       extended_can_id,
                         const canard_bytes_t can_data);

/// Retain a TX frame view obtained from tx() so it may outlive the callback and the TX queue entry.
/// The retained view must be released before canard_destroy() is invoked on the owning instance.
/// This is not applicable to RX payload views.
void canard_refcount_inc(const canard_bytes_t obj);

/// Release a TX frame retained earlier. self shall own the underlying tx_frame resource.
/// This is not applicable to RX payload views.
void canard_refcount_dec(canard_t* const self, const canard_bytes_t obj);

/// Enqueue a message transfer on the specified interfaces. Use CANARD_IFACE_BITMAP_ALL to send on all interfaces.
/// Message ordering observed on the bus is guaranteed per subject as long as the priority of later messages is
/// not higher (numerically not lower) than that of earlier messages.
/// The context is passed into the tx() vtable function.
///
/// Cost is roughly linear in the number of emitted CAN frames plus log-time queue indexing.
/// Memory use is one TX transfer object plus one shared TX frame object per emitted CAN frame.
///
/// Returns true on success; false on invalid arguments, OOM, or TX queue exhaustion.
/// See err.oom and err.tx_capacity for the enqueue failure cause.
bool canard_publish_16b(canard_t* const            self,
                        const canard_us_t          deadline,
                        const uint_least8_t        iface_bitmap,
                        const canard_prio_t        priority,
                        const uint16_t             subject_id,
                        const uint_least8_t        transfer_id,
                        const canard_bytes_chain_t payload,
                        void* const                user_context);
bool canard_publish_13b(canard_t* const            self,
                        const canard_us_t          deadline,
                        const uint_least8_t        iface_bitmap,
                        const canard_prio_t        priority,
                        const uint16_t             subject_id,
                        const uint_least8_t        transfer_id,
                        const canard_bytes_chain_t payload,
                        void* const                user_context);

/// Enqueue a service request on all ifaces; other semantics, failure modes, and memory model match canard_publish().
bool canard_request(canard_t* const            self,
                    const canard_us_t          deadline,
                    const canard_prio_t        priority,
                    const uint16_t             service_id,
                    const uint_least8_t        server_node_id,
                    const uint_least8_t        transfer_id,
                    const canard_bytes_chain_t payload,
                    void* const                user_context);

/// Enqueue a service response on all ifaces; other semantics, failure modes, and memory model match canard_publish().
bool canard_respond(canard_t* const            self,
                    const canard_us_t          deadline,
                    const canard_prio_t        priority,
                    const uint16_t             service_id,
                    const uint_least8_t        client_node_id,
                    const uint_least8_t        transfer_id,
                    const canard_bytes_chain_t payload,
                    void* const                user_context);

/// Register a new subscription on a subject with 13-bit or 16-bit ID.
/// There may be at most one subscription per subject-ID under each ID size; IDs of different sizes do not collide.
/// The subscription instance must not be moved while in use.
///
/// The extent specifies the maximum message size that can be received from the subject; longer messages will be
/// truncated per the implicit truncation rule (see the Spec).
/// Subscription updates are log-time; per-remote RX session state is allocated lazily on demand.
///
/// Returns the passed subscription on success, the incumbent if there is already a subscription for the same subject,
/// or NULL if any of the arguments are invalid. Clobbers the passed subscription on failure.
canard_subscription_t* canard_subscribe_16b(canard_t* const                           self,
                                            canard_subscription_t* const              subscription,
                                            const uint16_t                            subject_id,
                                            const size_t                              extent,
                                            const canard_us_t                         transfer_id_timeout,
                                            const canard_subscription_vtable_t* const vtable);
canard_subscription_t* canard_subscribe_13b(canard_t* const                           self,
                                            canard_subscription_t* const              subscription,
                                            const uint16_t                            subject_id, // [0,8191]
                                            const size_t                              extent,
                                            const canard_us_t                         transfer_id_timeout,
                                            const canard_subscription_vtable_t* const vtable);

/// Unicast transfers in Cyphal/CAN v1.1 are supposed to be modeled as requests to service-ID 511, with a 128-element
/// array of transfer-ID counters, one per remote. Some large nonzero transfer-ID timeout is required to satisfy the
/// deduplication requirement. This is outside of the scope of this library so it's not implemented here.
/// There may be at most one request subscription per service-ID.
/// Subscription updates are log-time; per-remote RX session state is allocated lazily on demand.
/// Return semantics match canard_subscribe_16b().
canard_subscription_t* canard_subscribe_request(canard_t* const                           self,
                                                canard_subscription_t* const              subscription,
                                                const uint16_t                            service_id,
                                                const size_t                              extent,
                                                const canard_us_t                         transfer_id_timeout,
                                                const canard_subscription_vtable_t* const vtable);

/// There may be at most one response subscription per service-ID.
/// Response transfers necessarily have a zero transfer-ID timeout: https://github.com/OpenCyphal/libcanard/issues/247.
/// Subscription updates are log-time; per-remote RX session state is allocated lazily on demand.
/// Return semantics match canard_subscribe_16b().
canard_subscription_t* canard_subscribe_response(canard_t* const                           self,
                                                 canard_subscription_t* const              subscription,
                                                 const uint16_t                            service_id,
                                                 const size_t                              extent,
                                                 const canard_subscription_vtable_t* const vtable);

/// Returns the installed subscription if found, otherwise NULL. Invalid kind values also return NULL.
/// Complexity is log-time in the subscription set of the requested kind.
canard_subscription_t* canard_find_subscription(const canard_t* const self,
                                                const canard_kind_t   kind,
                                                const uint16_t        port_id);

/// This can be used to undo all kinds of subscriptions, incl. v0.
/// Complexity is log-time in the subscription set plus linear in the number of remote sessions owned by it.
void canard_unsubscribe(canard_t* const self, canard_subscription_t* const subscription);

// ---------------------------------   UAVCAN v0 & DroneCAN legacy compatibility API   ---------------------------------

/// ATTENTION: Due to the v0 design, the problem of protocol version detection for correct frame parsing given
/// multi-frame transfers is undecidable without imposing constraints on the network configuration.
///
/// The core problem is that the protocol version is encoded in the initial state of the toggle bit, which is
/// by definition only observable in the first frame of a transfer; if there happen to be concurrent multi-frame
/// transfers emitted by the same node under the same transfer-ID under different versions, and their port-IDs
/// (and in the case of service transfers also the destination node-IDs, with a 1-bit bias) happen to alias
/// pathologically, remote subscribers will observe the two frame sequences as belonging to the same transfer.
///
/// Various heuristics exist, but due to intricate edge cases no robust solution exists for the general case.
/// The recommended solution is to adopt at least one of the following constraints on the network configuration:
///
/// - A single node-ID can only emit transfers of any single protocol version. The disambiguation problem is addressed
///   by the fact that the multi-frame reassembly state machine necessarily indexes states by the remote node-ID
///   (this is not an implementation detail but a requirement from the Specification of both UAVCAN v0 and Cyphal/CAN).
///   The first frame of any transfer is only accepted if the protocol version matches (it is observable reliably
///   in this case); subsequent frames even if aliased will not cause data corruption because without the start frame
///   the transfer will not be accepted. An application may maintain more than one node-ID with multiple canard_t.
///
/// - A single node-ID may emit transfers of both versions simultaneously as long as the resulting CAN IDs do not alias.
///   For example, if a v0 data type ID is chosen such that it maps a 1-bit onto a reserved 0-bit of the v1 CAN ID,
///   no ambiguity will occur.
///
/// The above concerns only data emission. It is always safe to receive transfers of any version from any node as long
/// as the above emission constraints are satisfied.

/// The legacy UAVCAN v0 protocol has 5-bit priority, which is obtained from 3-bit priority by left-shifting.
///
/// All legacy transfers are always sent in Classic CAN mode regardless of the FD flag.
///
/// To obtain the CRC seed, use canard_v0_crc_seed_from_data_type_signature(); if the payload does not exceed 7 bytes,
/// the CRC seed can be arbitrary since it is not needed for single-frame transfers.
/// Returns true on success; false on invalid arguments, OOM, or TX queue exhaustion.
/// A nonzero local node-ID is required.
bool canard_v0_publish(canard_t* const            self,
                       const canard_us_t          deadline,
                       const uint_least8_t        iface_bitmap,
                       const canard_prio_t        priority,
                       const uint16_t             data_type_id,
                       const uint16_t             crc_seed,
                       const uint_least8_t        transfer_id,
                       const canard_bytes_chain_t payload,
                       void* const                user_context);

/// Enqueue a legacy v0 service request on all interfaces. A nonzero local node-ID and a nonzero destination node-ID
/// are required; other TX semantics match canard_v0_publish().
bool canard_v0_request(canard_t* const            self,
                       const canard_us_t          deadline,
                       const canard_prio_t        priority,
                       const uint_least8_t        data_type_id,
                       const uint16_t             crc_seed,
                       const uint_least8_t        server_node_id,
                       const uint_least8_t        transfer_id,
                       const canard_bytes_chain_t payload,
                       void* const                user_context);

/// Enqueue a legacy v0 service response on all interfaces. A nonzero local node-ID and a nonzero destination node-ID
/// are required; other TX semantics match canard_v0_publish().
bool canard_v0_respond(canard_t* const            self,
                       const canard_us_t          deadline,
                       const canard_prio_t        priority,
                       const uint_least8_t        data_type_id,
                       const uint16_t             crc_seed,
                       const uint_least8_t        client_node_id,
                       const uint_least8_t        transfer_id,
                       const canard_bytes_chain_t payload,
                       void* const                user_context);

/// Register a legacy v0 message subscription.
/// Subscription updates are log-time; per-remote RX session state is allocated lazily on demand.
/// Return semantics match canard_subscribe_16b().
canard_subscription_t* canard_v0_subscribe(canard_t* const                           self,
                                           canard_subscription_t* const              subscription,
                                           const uint16_t                            data_type_id,
                                           const uint16_t                            crc_seed,
                                           const size_t                              extent,
                                           const canard_us_t                         transfer_id_timeout,
                                           const canard_subscription_vtable_t* const vtable);

/// Register a legacy v0 request subscription.
/// Subscription updates are log-time; per-remote RX session state is allocated lazily on demand.
/// Return semantics match canard_subscribe_16b().
canard_subscription_t* canard_v0_subscribe_request(canard_t* const                           self,
                                                   canard_subscription_t* const              subscription,
                                                   const uint_least8_t                       data_type_id,
                                                   const uint16_t                            crc_seed,
                                                   const size_t                              extent,
                                                   const canard_us_t                         transfer_id_timeout,
                                                   const canard_subscription_vtable_t* const vtable);

/// Register a legacy v0 response subscription.
/// Response transfers necessarily have a zero transfer-ID timeout.
/// Subscription updates are log-time; per-remote RX session state is allocated lazily on demand.
/// Return semantics match canard_subscribe_16b().
canard_subscription_t* canard_v0_subscribe_response(canard_t* const                           self,
                                                    canard_subscription_t* const              subscription,
                                                    const uint_least8_t                       data_type_id,
                                                    const uint16_t                            crc_seed,
                                                    const size_t                              extent,
                                                    const canard_subscription_vtable_t* const vtable);

/// Computes the CRC-16/CCITT-FALSE checksum of the data type signature in the little-endian byte order.
/// This value is then used to seed the transfer CRC for UAVCAN v0 and DroneCAN transfers.
uint16_t canard_v0_crc_seed_from_data_type_signature(const uint64_t data_type_signature);

#ifdef __cplusplus
}
#endif
#endif
