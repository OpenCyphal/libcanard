/// This software is distributed under the terms of the MIT License.
/// Copyright (c) OpenCyphal.
/// Author: Pavel Kirienko <pavel@opencyphal.org>

#include "canard.h"
#include <stddef.h>
#include <string.h>
#include <assert.h>

/// Define this macro to include build configuration header.
/// Usage example with CMake: "-DCANARD_CONFIG_HEADER=\"${CMAKE_CURRENT_SOURCE_DIR}/my_canard_config.h\""
#ifdef CANARD_CONFIG_HEADER
#include CANARD_CONFIG_HEADER
#endif

/// By default, this macro resolves to the standard assert().
/// To disable assertion checks completely, make it expand into `(void)(0)`.
#ifndef CANARD_ASSERT
// Intentional violation of MISRA: assertion macro cannot be replaced with a function definition.
#define CANARD_ASSERT(x) assert(x) // NOSONAR
#endif

#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 199901L)
#error "Unsupported language: ISO C99 or a newer version is required."
#endif

// The internal includes are placed here after the config header is included and CANARD_ASSERT is defined.
#define CAVL2_T         canard_tree_t
#define CAVL2_ASSERT(x) CANARD_ASSERT(x) // NOSONAR
#include <cavl2.h>

typedef unsigned char byte_t;

#define BITS_PER_BYTE 8U
#define BYTE_MAX      0xFFU

#define CAN_EXT_ID_MASK ((UINT32_C(1) << 29U) - 1U)

#define MFT_NON_LAST_FRAME_PAYLOAD_MIN 7U

#define PADDING_BYTE_VALUE 0U

#define TAIL_START_OF_TRANSFER 128U
#define TAIL_END_OF_TRANSFER   64U
#define TAIL_TOGGLE            32U

typedef enum transfer_kind_t
{
    transfer_kind_message     = 0,
    transfer_kind_request     = 1,
    transfer_kind_response    = 2,
    transfer_kind_v0_message  = 3,
    transfer_kind_v0_request  = 4,
    transfer_kind_v0_response = 5,
} transfer_kind_t;

static const uint8_t canard_dlc_to_len[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 };
static const uint8_t canard_len_to_dlc[65] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,                              // 0-8
    9,  9,  9,  9,                                                  // 9-12
    10, 10, 10, 10,                                                 // 13-16
    11, 11, 11, 11,                                                 // 17-20
    12, 12, 12, 12,                                                 // 21-24
    13, 13, 13, 13, 13, 13, 13, 13,                                 // 25-32
    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, // 33-48
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, // 49-64
};

static void* mem_alloc(const canard_mem_t memory, const size_t size)
{
    return memory.vtable->alloc(memory.context, size);
}

static void mem_free(const canard_mem_t memory, const size_t size, void* const data)
{
    memory.vtable->free(memory.context, size, data);
}

static byte_t* serialize_u32(byte_t* ptr, const uint32_t value)
{
    for (size_t i = 0; i < sizeof(value); i++) {
        *ptr++ = (byte_t)((byte_t)(value >> (i * 8U)) & 0xFFU);
    }
    return ptr;
}

static byte_t* serialize_u64(byte_t* ptr, const uint64_t value)
{
    for (size_t i = 0; i < sizeof(value); i++) {
        *ptr++ = (byte_t)((byte_t)(value >> (i * 8U)) & 0xFFU);
    }
    return ptr;
}

static const byte_t* deserialize_u32(const byte_t* ptr, uint32_t* const out_value)
{
    CANARD_ASSERT((ptr != NULL) && (out_value != NULL));
    *out_value = 0;
    for (size_t i = 0; i < sizeof(*out_value); i++) {
        *out_value |= (uint32_t)((uint32_t)*ptr << (i * 8U)); // NOLINT(google-readability-casting) NOSONAR
        ptr++;
    }
    return ptr;
}

static const byte_t* deserialize_u64(const byte_t* ptr, uint64_t* const out_value)
{
    CANARD_ASSERT((ptr != NULL) && (out_value != NULL));
    *out_value = 0;
    for (size_t i = 0; i < sizeof(*out_value); i++) {
        *out_value |= ((uint64_t)*ptr << (i * 8U));
        ptr++;
    }
    return ptr;
}

// ---------------------------------------------            CRC            ---------------------------------------------

#define CRC_INITIAL    0xFFFFU
#define CRC_RESIDUE    0x0000U
#define CRC_SIZE_BYTES 2U

static const uint16_t crc_table[256] = {
    0x0000U, 0x1021U, 0x2042U, 0x3063U, 0x4084U, 0x50A5U, 0x60C6U, 0x70E7U, 0x8108U, 0x9129U, 0xA14AU, 0xB16BU, 0xC18CU,
    0xD1ADU, 0xE1CEU, 0xF1EFU, 0x1231U, 0x0210U, 0x3273U, 0x2252U, 0x52B5U, 0x4294U, 0x72F7U, 0x62D6U, 0x9339U, 0x8318U,
    0xB37BU, 0xA35AU, 0xD3BDU, 0xC39CU, 0xF3FFU, 0xE3DEU, 0x2462U, 0x3443U, 0x0420U, 0x1401U, 0x64E6U, 0x74C7U, 0x44A4U,
    0x5485U, 0xA56AU, 0xB54BU, 0x8528U, 0x9509U, 0xE5EEU, 0xF5CFU, 0xC5ACU, 0xD58DU, 0x3653U, 0x2672U, 0x1611U, 0x0630U,
    0x76D7U, 0x66F6U, 0x5695U, 0x46B4U, 0xB75BU, 0xA77AU, 0x9719U, 0x8738U, 0xF7DFU, 0xE7FEU, 0xD79DU, 0xC7BCU, 0x48C4U,
    0x58E5U, 0x6886U, 0x78A7U, 0x0840U, 0x1861U, 0x2802U, 0x3823U, 0xC9CCU, 0xD9EDU, 0xE98EU, 0xF9AFU, 0x8948U, 0x9969U,
    0xA90AU, 0xB92BU, 0x5AF5U, 0x4AD4U, 0x7AB7U, 0x6A96U, 0x1A71U, 0x0A50U, 0x3A33U, 0x2A12U, 0xDBFDU, 0xCBDCU, 0xFBBFU,
    0xEB9EU, 0x9B79U, 0x8B58U, 0xBB3BU, 0xAB1AU, 0x6CA6U, 0x7C87U, 0x4CE4U, 0x5CC5U, 0x2C22U, 0x3C03U, 0x0C60U, 0x1C41U,
    0xEDAEU, 0xFD8FU, 0xCDECU, 0xDDCDU, 0xAD2AU, 0xBD0BU, 0x8D68U, 0x9D49U, 0x7E97U, 0x6EB6U, 0x5ED5U, 0x4EF4U, 0x3E13U,
    0x2E32U, 0x1E51U, 0x0E70U, 0xFF9FU, 0xEFBEU, 0xDFDDU, 0xCFFCU, 0xBF1BU, 0xAF3AU, 0x9F59U, 0x8F78U, 0x9188U, 0x81A9U,
    0xB1CAU, 0xA1EBU, 0xD10CU, 0xC12DU, 0xF14EU, 0xE16FU, 0x1080U, 0x00A1U, 0x30C2U, 0x20E3U, 0x5004U, 0x4025U, 0x7046U,
    0x6067U, 0x83B9U, 0x9398U, 0xA3FBU, 0xB3DAU, 0xC33DU, 0xD31CU, 0xE37FU, 0xF35EU, 0x02B1U, 0x1290U, 0x22F3U, 0x32D2U,
    0x4235U, 0x5214U, 0x6277U, 0x7256U, 0xB5EAU, 0xA5CBU, 0x95A8U, 0x8589U, 0xF56EU, 0xE54FU, 0xD52CU, 0xC50DU, 0x34E2U,
    0x24C3U, 0x14A0U, 0x0481U, 0x7466U, 0x6447U, 0x5424U, 0x4405U, 0xA7DBU, 0xB7FAU, 0x8799U, 0x97B8U, 0xE75FU, 0xF77EU,
    0xC71DU, 0xD73CU, 0x26D3U, 0x36F2U, 0x0691U, 0x16B0U, 0x6657U, 0x7676U, 0x4615U, 0x5634U, 0xD94CU, 0xC96DU, 0xF90EU,
    0xE92FU, 0x99C8U, 0x89E9U, 0xB98AU, 0xA9ABU, 0x5844U, 0x4865U, 0x7806U, 0x6827U, 0x18C0U, 0x08E1U, 0x3882U, 0x28A3U,
    0xCB7DU, 0xDB5CU, 0xEB3FU, 0xFB1EU, 0x8BF9U, 0x9BD8U, 0xABBBU, 0xBB9AU, 0x4A75U, 0x5A54U, 0x6A37U, 0x7A16U, 0x0AF1U,
    0x1AD0U, 0x2AB3U, 0x3A92U, 0xFD2EU, 0xED0FU, 0xDD6CU, 0xCD4DU, 0xBDAAU, 0xAD8BU, 0x9DE8U, 0x8DC9U, 0x7C26U, 0x6C07U,
    0x5C64U, 0x4C45U, 0x3CA2U, 0x2C83U, 0x1CE0U, 0x0CC1U, 0xEF1FU, 0xFF3EU, 0xCF5DU, 0xDF7CU, 0xAF9BU, 0xBFBAU, 0x8FD9U,
    0x9FF8U, 0x6E17U, 0x7E36U, 0x4E55U, 0x5E74U, 0x2E93U, 0x3EB2U, 0x0ED1U, 0x1EF0U,
};

static uint16_t crc_add_byte(const uint16_t crc, const uint8_t byte)
{
    return (uint16_t)((uint16_t)(crc << BITS_PER_BYTE) ^
                      crc_table[(uint16_t)((uint16_t)(crc >> BITS_PER_BYTE) ^ byte) & BYTE_MAX]);
}

static uint16_t crc_add(const uint16_t crc, const size_t size, const void* const data)
{
    CANARD_ASSERT((data != NULL) || (size == 0U));
    uint16_t       out = crc;
    const uint8_t* p   = (const uint8_t*)data;
    for (size_t i = 0; i < size; i++) {
        out = crc_add_byte(out, *p);
        ++p;
    }
    return out;
}

// ---------------------------------------------      LIST CONTAINER       ---------------------------------------------

static bool is_listed(const canard_list_t* const list, const canard_list_member_t* const member)
{
    return (member->next != NULL) || (member->prev != NULL) || (list->head == member);
}

/// No effect if not in the list.
static void delist(canard_list_t* const list, canard_list_member_t* const member)
{
    if (member->next != NULL) {
        member->next->prev = member->prev;
    }
    if (member->prev != NULL) {
        member->prev->next = member->next;
    }
    if (list->head == member) {
        list->head = member->next;
    }
    if (list->tail == member) {
        list->tail = member->prev;
    }
    member->next = NULL;
    member->prev = NULL;
    assert((list->head != NULL) == (list->tail != NULL));
}

/// If the item is already in the list, it will be delisted first. Can be used for moving to the front.
static void enlist_head(canard_list_t* const list, canard_list_member_t* const member)
{
    delist(list, member);
    assert((member->next == NULL) && (member->prev == NULL));
    assert((list->head != NULL) == (list->tail != NULL));
    member->next = list->head;
    if (list->head != NULL) {
        list->head->prev = member;
    }
    list->head = member;
    if (list->tail == NULL) {
        list->tail = member;
    }
    assert((list->head != NULL) && (list->tail != NULL));
}

#define LIST_MEMBER(ptr, owner_type, owner_field) ((owner_type*)ptr_unbias((ptr), offsetof(owner_type, owner_field)))
static void* ptr_unbias(const void* const ptr, const size_t offset)
{
    return (ptr == NULL) ? NULL : (void*)((char*)ptr - offset);
}
#define LIST_TAIL(list, owner_type, owner_field) LIST_MEMBER((list).tail, owner_type, owner_field)

// ---------------------------------------------            TX             ---------------------------------------------

typedef struct tx_frame_t
{
    size_t             refcount;
    size_t*            objcount;
    canard_mem_t       mem;
    struct tx_frame_t* next;
    size_t             size;
    byte_t             data[];
} tx_frame_t;

static canard_bytes_t tx_frame_view(const tx_frame_t* const frame)
{
    return (canard_bytes_t){ .size = frame->size, .data = frame->data };
}

static tx_frame_t* tx_frame_from_view(const canard_bytes_t view)
{
    return (tx_frame_t*)ptr_unbias(view.data, offsetof(tx_frame_t, data));
}

static tx_frame_t* tx_frame_new(canard_t* const self, const canard_mem_t mem, const size_t data_size)
{
    tx_frame_t* const frame = (tx_frame_t*)mem_alloc(mem, sizeof(tx_frame_t) + data_size);
    if (frame != NULL) {
        frame->refcount = 1U;
        frame->objcount = &self->tx.queue_size;
        frame->mem      = mem;
        frame->next     = NULL;
        frame->size     = data_size;
        // Update the count; this is decremented when the frame is freed upon refcount reaching zero.
        self->tx.queue_size++;
        CANARD_ASSERT(self->tx.queue_size <= self->tx.queue_capacity);
    }
    return frame;
}

/// The ordering is by topic hash first, then by transfer-ID.
/// Therefore, it orders all transfers by topic hash, allowing quick lookup by topic with an arbitrary transfer-ID.
typedef struct
{
    uint64_t topic_hash;
    byte_t   transfer_id;
} tx_transfer_key_t;

/// The CAN ID index only contains transfers that are ready for transmission; the ordering is based not exactly on the
/// CAN ID, but rather on its approximation called "skeleton" because we don't know v1.1 subject-ID bits until
/// transmission time.
///
/// The staged index contains transfers ordered by readiness for retransmission;
/// transfers that will no longer be transmitted but are retained waiting for the ack are in neither of these.
/// The deadline index contains ALL transfers, ordered by their deadlines, used for purging expired transfers.
/// The transfer index contains ALL transfers, used for lookup by (topic_hash, transfer_id).
///
/// The fields are weakly arranged to reduce padding, but logical grouping is prioritized.
/// If o1heap is used and the platform is 32-bit, a struct up to 240 bytes large fits into a 256-byte block;
/// reducing the footprint does not bring any benefit until <=112 bytes (which needs a 128-byte block).
typedef struct tx_transfer_t
{
    canard_tree_t        index_staged;       ///< Soonest to be ready on the left. Key: staged_until
    canard_tree_t        index_deadline;     ///< Soonest to expire on the left. Key: deadline
    canard_tree_t        index_transfer;     ///< Specific transfer lookup for ack management. Key: tx_transfer_key_t
    canard_tree_t        index_transfer_ack; ///< Only for outgoing ack transfers. Same key but referencing remote_*.
    canard_tree_t        index_can_id[CANARD_IFACE_COUNT_MAX]; ///< Inserted when ready for transmission.
    canard_list_member_t list_agewise;                         ///< Listed when created; oldest at the tail.

    /// We always keep a pointer to the head of the spool, plus a cursor that scans the frames during transmission.
    /// Both are NULL if the payload is destroyed (i.e., after the last attempt is done and we're waiting for ack).
    /// The head points to the first frame unless it is known that no (further) retransmissions are needed,
    /// in which case the old head is deleted and the head points to the next frame to transmit.
    tx_frame_t* head[CANARD_IFACE_COUNT_MAX];

    /// Mutable transmission state. All other fields, except for the index handles, are immutable.
    tx_frame_t* cursor[CANARD_IFACE_COUNT_MAX];
    canard_us_t staged_until; ///< When the transfer becomes eligible for retransmission.
    byte_t      epoch;        ///< Does not overflow due to exponential backoff; e.g. 1us with epoch=48 => 9 years.

    /// Constant transfer properties supplied by the client.
    /// The remote_* fields are identical to the local ones except in the case of P2P transfers, where
    /// they contain the values encoded in the P2P header. This is needed to find pending acks (to minimize duplicates),
    /// and to report the correct values via the feedback callback for P2P transfers.
    /// By default, upon construction, the remote_* fields equal the local ones, which is valid for ordinary messages.
    uint16_t    iface_bitmap;    ///< Guaranteed to have at least one bit set within CANARD_IFACE_COUNT_MAX.
    byte_t      p2p_destination; ///< Only for P2P transfers.
    bool        reliable;
    bool        subject_id_unresolved; ///< If subject-ID is to be resolved before transmission.
    byte_t      transfer_id;
    byte_t      remote_transfer_id;
    uint32_t    can_id; ///< For v1.1 messages, the subject-ID bits are zeroed (resolved at tx time).
    canard_us_t deadline;
    uint64_t    topic_hash;
    uint64_t    remote_topic_hash;

    /// Application closure.
    canard_user_context_t user;
    void (*feedback)(canard_t*, canard_tx_feedback_t);
} tx_transfer_t;
