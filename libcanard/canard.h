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
#define CANARD_IFACE_COUNT_MAX  3U
#define CANARD_IFACE_BITMAP_ALL ((1U << CANARD_IFACE_COUNT_MAX) - 1U)

/// Parameter ranges are inclusive; the lower bound is zero for all.
#define CANARD_SUBJECT_ID_MAX         0x1FFFFUL
#define CANARD_SUBJECT_ID_MAX_1v0     8191U // Cyphal v1.0 supports only 13-bit subject-IDs.
#define CANARD_SERVICE_ID_MAX         511U
#define CANARD_NODE_ID_MAX            127U
#define CANARD_NODE_ID_CAPACITY       (CANARD_NODE_ID_MAX + 1U)
#define CANARD_TRANSFER_ID_BIT_LENGTH 5U
#define CANARD_TRANSFER_ID_MAX        ((1U << CANARD_TRANSFER_ID_BIT_LENGTH) - 1U)

/// This is used only with Cyphal v1.0 and legacy v0 protocols to indicate anonymous messages.
/// Cyphal v1.1 does not support anonymous messages so this value is never used there.
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

/// All v1.1 transfers have payload headers, handled by the library transparently for the application,
/// that carry additional metadata pertaining to named topics and P2P traffic. v1.0 transfers have no such headers.
///
/// Together, the 17-bit subject-ID plus the 29-bit topic hash MSB provide a total of 46 bits for topic discrimination,
/// which is sufficient to avoid collisions. CRC seeding is not used because it adds little value in CAN FD, where
/// multi-frame transfers are relatively rare due to the large MTU.
///
/// v1.1 message transfers (smaller hash because topics are also discriminated by subject-ID, collisions less likely):
///     uint2  version          # =0
///     bool   reliable         # Set if the sender needs acknowledgment; false for best-effort messages.
///     uint29 topic_hash_msb   # The most significant bits of the topic hash for collision detection.
///     # Payload follows.
///
/// v1.1 P2P transfers (7 bytes to fit into a single Classic CAN frame):
///     uint2  version          # =0
///     bool   ack              # 1=acknowledgment, 0=response reliable (no unreliable responses currently exist)
///     uint5  transfer_id      # The original transfer-ID this P2P message relates to.
///     uint48 topic_hash_msb   # The most significant bits of the original topic hash this P2P message relates to.
///     # Payload follows (unless ack).
///
/// v1.0 messages are always best-effort (no delivery ack) because there is no header to communicate the ack request
/// flag, and cannot be P2P-replied to because only the most significant bits of the topic hash are included in the
/// P2P header (it is possible to dedicate some bits for the topic hash lsb, but it slightly complicates the lookup).
///
/// A single-frame v1.1 transfer can carry at most 59 bytes of payload in CAN FD, and at most 3 bytes in Classic CAN.
#define CANARD_HEADER_MESSAGE_BYTES 4U
#define CANARD_HEADER_P2P_BYTES     7U

typedef struct canard_t canard_t;

/// Monotonic time in microseconds; the current time is never negative.
typedef int64_t canard_us_t;

/// Length to DLC rounds up.
extern const uint_fast8_t canard_dlc_to_len[16];
extern const uint_fast8_t canard_len_to_dlc[65];

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
    void (*free)(canard_mem_t*, size_t, void*);
    void* (*alloc)(canard_mem_t*, size_t);
};
struct canard_mem_t
{
    const canard_mem_vtable_t* vtable;
    void*                      context;
};

/// The library carries the user-provided context from inputs to outputs without interpreting it,
/// allowing the application to associate its own data with various entities inside the library.
/// The size can be changed arbitrarily. This value is compromise between copy size and footprint and utility.
#define CANARD_USER_CONTEXT_PTR_COUNT 4
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

/// The filter only matches extended CAN IDs on data frames (no std/rtr). Bits above 29 are always zero.
typedef struct canard_filter_t
{
    uint32_t extended_can_id;
    uint32_t extended_mask;
} canard_filter_t;

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

    /// A topic hash mismatch is detected on this subject: another node is publishing data under the same
    /// subject-ID but with a different topic hash. This observation should be reported to the consensus protocol
    /// for a corrective action to be taken.
    /// This is not needed for v1.0 or v0 legacy subscriptions, in which case it may be NULL.
    void (*on_collision)(canard_subscription_t* self);
};

/// Subscription instances must not be moved while in use.
/// Each subscription is indexed by its port-ID inside the canard instance, and in turn contains a tree of sessions
/// indexed by remote node-ID. Two log-time lookups are thus required to handle an incoming frame.
struct canard_subscription_t
{
    canard_tree_t index_port_id; ///< Must be the first member.

    canard_us_t transfer_id_timeout;
    uint64_t    topic_hash;
    uint32_t    port_id; ///< Represents subjects, services, and legacy message- and service type IDs.
    size_t      extent;
    uint16_t    crc_seed; ///< The default seed for CRC-16/CCITT-FALSE is 0xFFFF.

    canard_tree_t* index_session_by_node_id;

    const canard_subscription_vtable_t* vtable;

    uint_fast8_t index_index_port_id; ///< Which of the indexes in canard_rx_t this subscription is part of.

    canard_filter_t filter; ///< Precomputed for quick acceptance filter configuration.

    canard_user_context_t user_context;
};

typedef struct canard_vtable_t
{
    /// A new P2P message is received.
    ///
    /// The topic hash is left-aligned, i.e., all bits are on their right positions within the 64-bit field,
    /// and the absent least significant bits are zeroed. This enables easy lookup using lower bounds.
    /// In other words, the topic hash lower bound is slightly less than or equal the true topic hash of the message.
    ///
    /// The handler takes ownership of the payload; it must free it after use using the corresponding memory resource.
    void (*on_p2p)(canard_t*,
                   canard_us_t        timestamp,
                   canard_prio_t      priority,
                   uint_fast8_t       source_node_id,
                   uint64_t           topic_hash_lower_bound,
                   uint_fast8_t       transfer_id,
                   canard_bytes_mut_t payload);

    /// Submit one CAN frame for transmission via the specified interface. It is guaranteed that now<=deadline.
    /// If the data is empty (size==0), the data pointer may be NULL.
    /// Returns true if the frame was accepted for transmission, false if there is no free mailbox (try again later).
    /// The callback must not mutate the TX pipeline (no publish/cancel/free/etc).
    /// If the can_data needs to be retained for later retransmission, use canard_refcount_inc()/canard_refcount_dec().
    bool (*tx)(canard_t*,
               const canard_user_context_t*,
               canard_us_t    now,
               canard_us_t    deadline,
               uint_fast8_t   iface_index,
               bool           fd,
               uint32_t       extended_can_id,
               canard_bytes_t can_data);

    /// Invoked immediately before tx() to obtain the subject-ID for the given transfer.
    /// The application is expected to rely on the user context to access the topic context for subject-ID derivation.
    /// This is the same user context that was passed to canard_publish().
    /// The callback must not mutate the TX pipeline (no publish/cancel/free/etc).
    /// The transmission will be cancelled if the returned subject-ID exceeds CANARD_SUBJECT_ID_MAX.
    uint32_t (*tx_subject_id)(canard_t*, const canard_user_context_t*);

    /// Reconfigure the acceptance filters of the CAN controller hardware.
    /// The prior configuration, if any, is replaced entirely.
    /// Returns true on success, false if the filters could not be applied; another attempt will be made later.
    bool (*filter)(canard_t*, size_t filter_count, const canard_filter_t* filters);
} canard_vtable_t;

/// None of the fields should be mutated by the application (unless explicitly allowed).
struct canard_t
{
    uint64_t     node_id_occupancy_bitmap[2];
    uint_fast8_t node_id;

    struct
    {
        /// By default, CAN FD mode is used; this flag can be used to change the mode to Classic CAN if needed;
        /// for example, if the local CAN controller does not support CAN FD, or if the remote nodes do not support it.
        /// The flag can be switched at any time.
        ///
        /// A valid auto-configuration strategy that could be implemented in the application is to start in FD mode
        /// and switch to Classic if a non-FD frame is observed on the bus.
        ///
        /// The local node can accept both Classic CAN and CAN FD frames regardless of this setting;
        /// the setting only affects the mode used for outgoing frames.
        ///
        /// Legacy v0 transfers (UAVCAN/DroneCAN) are always sent in Classic CAN mode regardless of this flag,
        /// because UAVCAN v0 does not define CAN FD support.
        bool fd;

        size_t queue_capacity;
        size_t queue_size;

        canard_tree_t* index_staged;                         ///< Soonest retry time on the left.
        canard_tree_t* index_deadline;                       ///< Soonest deadline on the left.
        canard_tree_t* index_transfer;                       ///< Ordered by (topic hash, transfer-ID).
        canard_tree_t* index_transfer_ack;                   ///< Ordered by (remote topic hash, remote transfer-ID).
        canard_tree_t* index_can_id[CANARD_IFACE_COUNT_MAX]; ///< Lowest CAN ID on the left.
        canard_list_t  list_agewise;                         ///< Oldest transfer at the tail.
    } tx;

    struct
    {
        canard_tree_t* subscriptions[6];
        canard_list_t  list_session_by_animation; ///< Oldest at the tail.

        size_t           filter_count;
        canard_filter_t* filters; ///< Storage provided by the user.
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

    canard_subscription_t p2p_subscription;
    uint_least8_t         p2p_transfer_id[CANARD_NODE_ID_CAPACITY];

    const canard_vtable_t* vtable;
};

/// Notification about the outcome of a reliable transfer previously submitted for transmission.
typedef struct canard_tx_feedback_t
{
    uint64_t     topic_hash;
    uint32_t     subject_id;
    uint_fast8_t transfer_id;

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
/// down to 64 bits with a good hash, e.g., rapidhash.
///
/// The node_id parameter should be CANARD_NODE_ID_ANONYMOUS to enable automatic stateless allocation;
/// however, if the application has a preferred node-ID (e.g., restored from a non-volatile memory),
/// it may be passed here directly. Regardless of whether it is assigned automatically or manually,
/// if another node with the same node-ID is detected, a re-allocation will be done automatically.
/// Even if the node-ID is allocated automatically, it is recommended to save it in non-volatile memory for
/// faster startup next time and to avoid the risk of unnecessary perturbations to the network.
/// The same node-ID is used for both v1 and legacy v0 communications.
///
/// The filter storage is an array of filters that is used by the library to automatically set up the acceptance
/// filters when the RX pipeline is reconfigured. The number of available filters is limited by the CAN hardware.
/// Pass zero filters to disable this functionality.
///
/// CAN FD mode is selected by default for outgoing frames; override the fd flag to change the mode if needed.
///
/// Returns true on success, false if any of the parameters are invalid.
bool canard_new(canard_t* const              self,
                const canard_vtable_t* const vtable,
                const canard_mem_set_t       memory,
                const size_t                 tx_queue_capacity,
                const uint_fast8_t           node_id,
                const uint64_t               prng_seed,
                const size_t                 filter_count,
                canard_filter_t* const       filter_storage);

void canard_free(canard_t* const self);

/// This must be invoked periodically to ensure liveliness.
/// The function must be called asap once any of the interfaces for which there are pending outgoing transfers
/// become writable, and not less frequently than once in a few milliseconds. The invocation rate defines the
/// resolution of deadline handling.
void canard_poll(canard_t* const self, const canard_us_t now, const uint16_t tx_ready_iface_bitmap);

/// Returns a bitmap of interfaces that have pending transmissions. This is useful for IO multiplexing.
uint16_t canard_pending_ifaces(const canard_t* const self);

/// True if successfully processed, false if any of the arguments are invalid.
/// A malformed frame is not considered an error; it is simply dropped and the corresponding counter is incremented.
/// The can_data is copied and thus can be discarded by the caller after this function returns.
bool canard_ingest_frame(canard_t* const      self,
                         const canard_us_t    timestamp,
                         const uint_fast8_t   iface_index,
                         const uint32_t       extended_can_id,
                         const canard_bytes_t can_data);

/// Cancel a pending outgoing message transfer on a subject.
/// Returns true if a transfer was found and cancelled, false if no such transfer was found.
/// Cyphal v1.0 service transfers cannot be canceled.
/// For pinned topics or v1.0 message transfers, pass the subject-ID as the topic_hash.
bool canard_cancel(canard_t* const self, const uint64_t topic_hash, const uint_fast8_t transfer_id);

/// Like canard_cancel(), but cancels all pending transfers on the given subject.
/// Returns the number of matched transfers.
/// This is important to invoke when destroying a topic to ensure no dangling callbacks remain.
size_t canard_cancel_all(canard_t* const self, const uint64_t topic_hash);

void canard_refcount_inc(const canard_bytes_t obj);
void canard_refcount_dec(const canard_bytes_t obj);

/// The subject-ID will be obtained using the dedicated vtable function immediately before transmission.
/// This is because the topic->subject allocation protocol may change the subject-ID for already enqueued messages.
/// The application is expected to rely on the user context to access the topic context for subject-ID derivation
/// (e.g., store a topic pointer in there).
bool canard_publish(canard_t* const               self,
                    const canard_us_t             now,
                    const canard_us_t             deadline,
                    const uint16_t                iface_bitmap,
                    const canard_prio_t           priority,
                    const uint64_t                topic_hash,
                    const uint_fast8_t            transfer_id,
                    const canard_bytes_chain_t    payload,
                    const canard_user_context_t   context,
                    const canard_on_tx_feedback_t feedback);

bool canard_respond(canard_t* const               self,
                    const canard_us_t             now,
                    const canard_us_t             deadline,
                    const uint_fast8_t            destination_node_id,
                    const canard_prio_t           priority,
                    const uint64_t                request_topic_hash,
                    const uint_fast8_t            request_transfer_id,
                    const canard_bytes_chain_t    payload,
                    const canard_user_context_t   context,
                    const canard_on_tx_feedback_t feedback);

bool canard_subscribe(canard_t* const                           self,
                      canard_subscription_t* const              subscription,
                      const uint64_t                            topic_hash,
                      const uint32_t                            subject_id,
                      const size_t                              extent,
                      const canard_us_t                         transfer_id_timeout,
                      const canard_subscription_vtable_t* const vtable);

/// This can be used to undo all kinds of subscriptions, incl. all v1.0 ones.
void canard_unsubscribe(canard_t* const self, canard_subscription_t* const subscription);

// -----------------------------------------   Cyphal v1.0 compatibility API   -----------------------------------------

bool canard_1v0_publish(canard_t* const            self,
                        const canard_us_t          now,
                        const canard_us_t          deadline,
                        const uint16_t             iface_bitmap,
                        const canard_prio_t        priority,
                        const uint16_t             subject_id,
                        const uint_fast8_t         transfer_id,
                        const canard_bytes_chain_t payload);

bool canard_1v0_request(canard_t* const            self,
                        const canard_us_t          now,
                        const canard_us_t          deadline,
                        const canard_prio_t        priority,
                        const uint16_t             service_id,
                        const uint_fast8_t         server_node_id,
                        const uint_fast8_t         transfer_id,
                        const canard_bytes_chain_t payload);

bool canard_1v0_respond(canard_t* const            self,
                        const canard_us_t          now,
                        const canard_us_t          deadline,
                        const canard_prio_t        priority,
                        const uint16_t             service_id,
                        const uint_fast8_t         client_node_id,
                        const uint_fast8_t         transfer_id,
                        const canard_bytes_chain_t payload);

bool canard_1v0_subscribe(canard_t* const                           self,
                          canard_subscription_t* const              subscription,
                          const uint16_t                            subject_id, // Narrower than in v1.1
                          const size_t                              extent,
                          const canard_us_t                         transfer_id_timeout,
                          const canard_subscription_vtable_t* const vtable);

bool canard_1v0_subscribe_request(canard_t* const                           self,
                                  canard_subscription_t* const              subscription,
                                  const uint16_t                            service_id,
                                  const size_t                              extent,
                                  const canard_us_t                         transfer_id_timeout,
                                  const canard_subscription_vtable_t* const vtable);

bool canard_1v0_subscribe_response(canard_t* const                           self,
                                   canard_subscription_t* const              subscription,
                                   const uint16_t                            service_id,
                                   const size_t                              extent,
                                   const canard_subscription_vtable_t* const vtable);

// ---------------------------------   UAVCAN v0 & DroneCAN legacy compatibility API   ---------------------------------

/// The legacy UAVCAN v0 protocol has 5-bit priority, which is obtained by shifting the 3-bit priority left by 2 bits.
bool canard_0v1_publish(canard_t* const            self,
                        const canard_us_t          now,
                        const canard_us_t          deadline,
                        const uint16_t             iface_bitmap,
                        const canard_prio_t        priority,
                        const uint16_t             data_type_id,
                        const uint16_t             crc_seed,
                        const uint_fast8_t         transfer_id,
                        const canard_bytes_chain_t payload);

bool canard_0v1_request(canard_t* const            self,
                        const canard_us_t          now,
                        const canard_us_t          deadline,
                        const canard_prio_t        priority,
                        const uint_fast8_t         data_type_id,
                        const uint16_t             crc_seed,
                        const uint_fast8_t         server_node_id,
                        const uint_fast8_t         transfer_id,
                        const canard_bytes_chain_t payload);

bool canard_0v1_respond(canard_t* const            self,
                        const canard_us_t          now,
                        const canard_us_t          deadline,
                        const canard_prio_t        priority,
                        const uint_fast8_t         data_type_id,
                        const uint16_t             crc_seed,
                        const uint_fast8_t         client_node_id,
                        const uint_fast8_t         transfer_id,
                        const canard_bytes_chain_t payload);

bool canard_0v1_subscribe(canard_t* const                           self,
                          canard_subscription_t* const              subscription,
                          const uint16_t                            data_type_id,
                          const uint16_t                            crc_seed,
                          const size_t                              extent,
                          const canard_us_t                         transfer_id_timeout,
                          const canard_subscription_vtable_t* const vtable);

bool canard_0v1_subscribe_request(canard_t* const                           self,
                                  canard_subscription_t* const              subscription,
                                  const uint_fast8_t                        data_type_id,
                                  const uint16_t                            crc_seed,
                                  const size_t                              extent,
                                  const canard_us_t                         transfer_id_timeout,
                                  const canard_subscription_vtable_t* const vtable);

bool canard_0v1_subscribe_response(canard_t* const                           self,
                                   canard_subscription_t* const              subscription,
                                   const uint_fast8_t                        data_type_id,
                                   const uint16_t                            crc_seed,
                                   const size_t                              extent,
                                   const canard_subscription_vtable_t* const vtable);

/// Computes the CRC-16/CCITT-FALSE checksum of the data type signature in the little-endian byte order.
/// This value is then used to seed the transfer CRC for UAVCAN v0 and DroneCAN transfers.
uint16_t canard_0v1_crc_seed_from_data_type_signature(const uint64_t data_type_signature);

#ifdef __cplusplus
}
#endif
#endif
