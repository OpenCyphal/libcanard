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
/// The library is intended to be integrated into the end application by simply copying its source files into the
/// source tree of the project; it does not require any special compilation options and should work out of the box.
/// There are build-time configuration parameters defined near the top of canard.c, but they are safe to ignore.
///
/// The library is not thread-safe: if used in a concurrent environment, it is the responsibility of the application
/// to provide adequate synchronization.
///
/// --------------------------------------------------------------------------------------------------------------------
/// This software is distributed under the terms of the MIT License.
/// Copyright (c) OpenCyphal.
/// Author: Pavel Kirienko <pavel@opencyphal.org>
/// Contributors: https://github.com/OpenCyphal/libcanard/contributors.

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

/// The library supports at most this many local redundant network interfaces.
#define CANARD_IFACE_COUNT_MAX 3U
#define CANARD_IFACE_MASK_ALL  ((1U << CANARD_IFACE_COUNT_MAX) - 1U)

/// Parameter ranges are inclusive; the lower bound is zero for all.
#define CANARD_SUBJECT_ID_MAX         0x1FFFFU
#define CANARD_SUBJECT_ID_MAX_1v0     8191U // Cyphal v1.0 supports only 13-bit subject-IDs.
#define CANARD_SERVICE_ID_MAX         511U
#define CANARD_NODE_ID_MAX            127U
#define CANARD_NODE_ID_CAPACITY       (CANARD_NODE_ID_MAX + 1U)
#define CANARD_TRANSFER_ID_BIT_LENGTH 5U
#define CANARD_TRANSFER_ID_MAX        ((1U << CANARD_TRANSFER_ID_BIT_LENGTH) - 1U)

/// This is the recommended transfer-ID timeout value given in the Cyphal Specification. The application may choose
/// different values per subscription (i.e., per data specifier) depending on its timing requirements.
#define CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us 2000000UL

/// MTU values for the supported protocols.
/// Per the recommendations given in the Cyphal/CAN Specification, other MTU values should not be used.
#define CANARD_MTU_CAN_CLASSIC 8U
#define CANARD_MTU_CAN_FD      64U

/// All v1.1 transfers have payload headers, handled by the library transparently for the application,
/// that carry additional metadata pertaining to named topics and P2P traffic. v1.0 transfers have no such headers.
///
/// v1.1 message transfers (only 3 bytes because topics are also discriminated by subject-ID, collisions less likely):
///     uint1  version          # =0
///     bool   reliable         # Set if the sender needs acknowledgment; false for best-effort messages.
///     uint22 topic_hash_msb   # The most significant bits of the topic hash.
///     # Message payload follows.
///
/// v1.1 P2P transfers (7 bytes to fit into a single Classic CAN frame):
///     uint1  version          # =0
///     uint2  kind             # 0=response reliable, 1=acknowledgment (all P2P responses are reliable for now)
///     uint5  transfer_id      # The original transfer-ID this P2P message relates to.
///     uint48 topic_hash_msb   # The most significant bits of the original topic hash this P2P message relates to.
#define CANARD_HEADER_MESSAGE_BYTES 3U
#define CANARD_HEADER_P2P_BYTES     7U

typedef struct canard_t canard_t;

/// Monotonic time in microseconds; the current time is never negative.
typedef int64_t canard_us_t;

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

typedef enum canard_transfer_kind_t
{
    canard_transfer_kind_message  = 0,
    canard_transfer_kind_response = 1,
    canard_transfer_kind_request  = 2,
} canard_transfer_kind_t;
#define CANARD_NUM_TRANSFER_KINDS 3

typedef struct canard_tree_t
{
    struct canard_tree_t* up;
    struct canard_tree_t* lr[2];
    int_fast8_t           bf;
} canard_tree_t;

typedef struct canard_list_member_t canard_list_member_t;
typedef struct canard_list_t        canard_list_t;
struct canard_list_member_t
{
    canard_list_member_t* next;
    canard_list_member_t* prev;
};
struct canard_list_t
{
    canard_list_member_t* head; ///< NULL if list empty
    canard_list_member_t* tail; ///< NULL if list empty
};

typedef struct canard_bytes_t           canard_bytes_t;
typedef struct canard_bytes_scattered_t canard_bytes_scattered_t;
typedef struct canard_bytes_mut_t       canard_bytes_mut_t;
struct canard_bytes_t
{
    size_t      size;
    const void* data;
};
struct canard_bytes_scattered_t
{
    canard_bytes_t                  bytes;
    const canard_bytes_scattered_t* next; ///< NULL in the last fragment.
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
    void* (*alloc)(canard_mem_t*, size_t);
    void (*free)(canard_mem_t*, size_t, void*);
};
struct canard_mem_t
{
    const canard_mem_vtable_t* vtable;
    void*                      context;
};

/// The library carries the user-provided context from inputs to outputs without interpreting it,
/// allowing the application to associate its own data with various entities inside the library.
/// The size can be changed arbitrarily. This value is compromise between copy size and footprint and utility.
#define CANARD_USER_CONTEXT_PTR_COUNT 6
typedef union canard_user_context_t
{
    void*         ptr[CANARD_USER_CONTEXT_PTR_COUNT];
    unsigned char bytes[sizeof(void*) * CANARD_USER_CONTEXT_PTR_COUNT];
} canard_user_context_t;
#ifdef __cplusplus
#define CANARD_USER_CONTEXT_NULL \
    canard_user_context_t {}
#else
#define CANARD_USER_CONTEXT_NULL ((canard_user_context_t){ .ptr = { NULL } })
#endif

/// Each resource is used for allocating memory for a specific purpose.
/// This enables fine-tuning in memory-conscious applications.
/// Ordinary applications can use the same resource for everything.
typedef struct canard_mem_set_t
{
    canard_mem_t tx_transfer;
    canard_mem_t tx_frame;
    canard_mem_t rx_session;
    canard_mem_t rx_payload;
} canard_mem_set_t;

typedef struct canard_subscription_t        canard_subscription_t;
typedef struct canard_subscription_vtable_t canard_subscription_vtable_t;
struct canard_subscription_vtable_t
{
    /// A new message is received on a subscription.
    /// The handler takes ownership of the payload; it must free it after use.
    void (*on_message)(canard_subscription_t* self,
                       canard_us_t            timestamp,
                       canard_prio_t          priority,
                       uint_fast8_t           source_node_id,
                       uint_fast8_t           transfer_id,
                       canard_bytes_mut_t     payload);

    /// There is probably another topic using the same subject-ID as this subscription.
    /// This may need to be signaled to the consensus protocol for a corrective action to be taken.
    void (*on_collision)(canard_subscription_t* self);
};

/// Subscription instances must not be moved while in use.
/// Each subscription is indexed by its port-ID inside the canard instance, and in turn contains a tree of sessions
/// indexed by remote node-ID. Two tree lookups are thus required to handle an incoming frame.
struct canard_subscription_t
{
    canard_tree_t index_port_id; ///< Must be the first member.

    canard_us_t transfer_id_timeout;
    uint64_t    topic_hash;
    uint32_t    port_id;
    size_t      extent;

    canard_tree_t* index_session_by_node_id;

    const canard_subscription_vtable_t* vtable;

    canard_user_context_t user_context;
};

typedef struct canard_vtable_t
{
    /// Submit one CAN frame for transmission via the specified interface. It is guaranteed that now<=deadline.
    /// If the data is empty (size==0), the data pointer may be NULL.
    /// Returns true if the frame was accepted for transmission, false if there is no free mailbox (try again later).
    /// The callback must not mutate the TX pipeline (no push/cancel/free).
    bool (*transmit)(canard_t*,
                     canard_user_context_t,
                     canard_us_t    now,
                     canard_us_t    deadline,
                     uint_fast8_t   iface_index,
                     uint32_t       extended_can_id,
                     canard_bytes_t can_data);
} canard_vtable_t;

/// None of the fields should be mutated by the application unless explicitly allowed.
struct canard_t
{
    uint64_t     node_id_occupancy_bitmap[2];
    uint_fast8_t node_id;

    struct
    {
        canard_tree_t* index_priority;
        canard_tree_t* index_deadline;
        size_t         queue_capacity;
        size_t         queue_size;
        uint_least8_t  p2p_transfer_id[CANARD_NODE_ID_CAPACITY];
        bool           fd; ///< Can be changed by the application to switch between Classic CAN and CAN FD.
    } tx;

    struct
    {
        canard_tree_t* subscriptions[CANARD_NUM_TRANSFER_KINDS];
        canard_list_t  list_session_by_animation; ///< Oldest at the tail.
    } rx;

    /// Error counters incremented automatically when the corresponding error condition occurs.
    /// These counters are never decremented by the library but they can be reset by the application if needed.
    struct
    {
        uint64_t oom;           ///< Out of memory; a transfer could have been lost.
        uint64_t ack;           ///< An ack could not be enqueued. Other counters may provide more details.
        uint64_t tx_capacity;   ///< A transfer could not be enqueued due to queue capacity limit.
        uint64_t tx_sacrifice;  ///< A transfer had to be sacrificed to make room for a new transfer.
        uint64_t tx_expiration; ///< A transfer had to be dequeued due to deadline expiration.
        uint64_t rx_frame;      ///< A received frame was malformed and thus dropped.
        uint64_t rx_transfer;   ///< A transfer could not be reassembled correctly.
    } err;

    canard_mem_set_t mem;
    uint64_t         prng_state;

    const canard_vtable_t* vtable;
};

/// Notification about the outcome of a reliable transfer previously submitted for transmission.
typedef struct canard_tx_feedback_t
{
    uint64_t               topic_hash;
    uint32_t               port_id;
    canard_transfer_kind_t kind;
    uint_fast8_t           transfer_id;

    /// The number of remote nodes that acknowledged the reception of the transfer.
    /// For P2P transfers, this value is either 0 (failure) or 1 (success).
    uint_fast8_t acknowledgements;

    canard_user_context_t user_context;
} canard_tx_feedback_t;
typedef void (*canard_on_tx_feedback_t)(canard_t*, canard_tx_feedback_t);

/// The TX queue is shared between all redundant interfaces with deduplication (each frame is enqueued only once).
/// The capacity set here is therefore the total capacity across all interfaces.
///
/// The PRNG seed must be likely to be distinct per node on the network; it may be a constant value.
/// In the absence of a true RNG, a good way to obtain the seed is to use a unique hardware identifier hashed
/// down to 64 bits. The PRNG is used for node-ID allocation, incl. reallocation on collision.
///
/// The application can assign the preferred node-ID immediately after initialization by setting the node_id field.
/// If a collision is discovered later, the node may be moved to a different node-ID automatically,
/// since it is mandatory that each online node has a unique node-ID in the network.
///
/// The node will be configured to emit CAN FD by default. This can be changed by modifying the corresponding field.
///
/// Returns true on success, false if any of the parameters are invalid.
bool canard_new(canard_t* const              self,
                const canard_vtable_t* const vtable,
                const canard_mem_set_t       memory,
                const size_t                 tx_queue_capacity,
                const uint64_t               prng_seed);

uint32_t canard_tx_message(canard_t* const                self,
                           const canard_us_t              now,
                           const canard_us_t              deadline,
                           const canard_prio_t            priority,
                           const uint64_t                 topic_hash,
                           const uint32_t                 subject_id,
                           const uint_fast8_t             transfer_id,
                           const canard_bytes_scattered_t payload,
                           const canard_user_context_t    context,
                           const canard_on_tx_feedback_t  feedback);

uint32_t canard_tx_p2p(canard_t* const                self,
                       const canard_us_t              now,
                       const canard_us_t              deadline,
                       const uint_fast8_t             destination_node_id,
                       const canard_prio_t            priority,
                       const uint64_t                 request_topic_hash,
                       const uint_fast8_t             request_transfer_id,
                       const canard_bytes_scattered_t payload,
                       const canard_user_context_t    context,
                       const canard_on_tx_feedback_t  feedback);

uint32_t canard_tx_1v0_message(canard_t* const                self,
                               const canard_us_t              now,
                               const canard_us_t              deadline,
                               const canard_prio_t            priority,
                               const uint16_t                 subject_id, // Narrower than in v1.1
                               const uint_fast8_t             transfer_id,
                               const canard_bytes_scattered_t payload);

uint32_t canard_tx_1v0_request(canard_t* const                self,
                               const canard_us_t              now,
                               const canard_us_t              deadline,
                               const canard_prio_t            priority,
                               const uint_fast8_t             service_id,
                               const uint_fast8_t             client_node_id,
                               const uint_fast8_t             transfer_id,
                               const canard_bytes_scattered_t payload);

uint32_t canard_tx_1v0_respond(canard_t* const                self,
                               const canard_us_t              now,
                               const canard_us_t              deadline,
                               const canard_prio_t            priority,
                               const uint_fast8_t             service_id,
                               const uint_fast8_t             server_node_id,
                               const uint_fast8_t             transfer_id,
                               const canard_bytes_scattered_t payload);

#ifdef __cplusplus
}
#endif
#endif
