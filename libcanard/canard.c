// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal.
// Author: Pavel Kirienko <pavel@opencyphal.org>
// Contributors: https://github.com/OpenCyphal/libcanard/contributors

#include "canard.h"
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
#if __STDC_VERSION__ >= 202311L
#include <stdbit.h>
#endif

// Define this macro to include build configuration header.
// Usage example with CMake: "-DCANARD_CONFIG_HEADER=\"${CMAKE_CURRENT_SOURCE_DIR}/my_canard_config.h\""
#ifdef CANARD_CONFIG_HEADER
#include CANARD_CONFIG_HEADER // cppcheck suppress "preprocessorErrorDirective"
#endif

// By default, this macro resolves to the standard assert().
// To disable assertion checks completely, make it expand into `(void)(0)`.
#ifndef CANARD_ASSERT
// Intentional violation of MISRA: assertion macro cannot be replaced with a function definition.
#define CANARD_ASSERT(x) assert(x) // NOSONAR
#endif

#if __STDC_VERSION__ < 201112L
// Intentional violation of MISRA: static assertion macro cannot be replaced with a function definition.
#define static_assert(x, ...)   typedef char _static_assert_gl(_static_assertion_, __LINE__)[(x) ? 1 : -1] // NOSONAR
#define _static_assert_gl(a, b) _static_assert_gl_impl(a, b)                                               // NOSONAR
// Intentional violation of MISRA: the paste operator ## cannot be avoided in this context.
#define _static_assert_gl_impl(a, b) a##b // NOSONAR
#endif

#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 199901L)
#error "Unsupported language: ISO C99 or a newer version is required."
#endif

// The internal includes are placed here after the config header is included and CANARD_ASSERT is defined.
#define CAVL2_T         canard_tree_t
#define CAVL2_RELATION  int32_t
#define CAVL2_ASSERT(x) CANARD_ASSERT(x) // NOSONAR
#include <cavl2.h>

typedef unsigned char byte_t;

#define BYTE_MAX 0xFFU

#define BIG_BANG   INT64_MIN
#define HEAT_DEATH INT64_MAX

#define CAN_EXT_ID_MASK ((UINT32_C(1) << 29U) - 1U)

#define MFT_NON_LAST_FRAME_PAYLOAD_MIN 7U

#define PADDING_BYTE_VALUE 0U

#define PRIO_SHIFT 26U
#define PRIO_MASK  7U

#define TAIL_SOT    128U
#define TAIL_EOT    64U
#define TAIL_TOGGLE 32U

#define FOREACH_IFACE(i) for (size_t i = 0; i < CANARD_IFACE_COUNT; i++)

#define TREE_NULL (canard_tree_t){ NULL, { NULL, NULL }, 0 }

typedef enum transfer_kind_t
{
    transfer_kind_message     = 0,
    transfer_kind_response    = 1,
    transfer_kind_request     = 2,
    transfer_kind_v0_message  = 3,
    transfer_kind_v0_response = 4,
    transfer_kind_v0_request  = 5,
} transfer_kind_t;

static bool transfer_kind_is_v0(const transfer_kind_t kind)
{
    return (kind == transfer_kind_v0_message) || //
           (kind == transfer_kind_v0_request) || //
           (kind == transfer_kind_v0_response);
}

#define DLC_BITS 4U

const uint_least8_t canard_dlc_to_len[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 };
const uint_least8_t canard_len_to_dlc[65] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,                              // 0-8
    9,  9,  9,  9,                                                  // 9-12
    10, 10, 10, 10,                                                 // 13-16
    11, 11, 11, 11,                                                 // 17-20
    12, 12, 12, 12,                                                 // 21-24
    13, 13, 13, 13, 13, 13, 13, 13,                                 // 25-32
    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, // 33-48
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, // 49-64
};

static size_t      smaller(const size_t a, const size_t b) { return (a < b) ? a : b; }
static size_t      larger(const size_t a, const size_t b) { return (a > b) ? a : b; }
static int64_t     min_i64(const int64_t a, const int64_t b) { return (a < b) ? a : b; }
static canard_us_t earlier(const canard_us_t a, const canard_us_t b) { return min_i64(a, b); }

// Used if intrinsics are not available.
// http://en.wikipedia.org/wiki/Hamming_weight#Efficient_implementation
static byte_t popcount_emulated(uint64_t x)
{
    const uint64_t m1  = 0x5555555555555555ULL;
    const uint64_t m2  = 0x3333333333333333ULL;
    const uint64_t m4  = 0x0F0F0F0F0F0F0F0FULL;
    const uint64_t h01 = 0x0101010101010101ULL;
    x -= (x >> 1U) & m1;
    x = (x & m2) + ((x >> 2U) & m2);
    x = (x + (x >> 4U)) & m4;
    return (byte_t)((x * h01) >> 56U);
}

static byte_t popcount(const uint64_t x)
{
#ifdef stdc_count_ones
    return (byte_t)stdc_count_ones(x); // C23 feature
#elif defined(__GNUC__) || defined(__clang__) || defined(__CC_ARM)
    return (byte_t)__builtin_popcountll(x);
#else
    return popcount_emulated(x);
#endif
}

static void* mem_alloc(const canard_mem_t memory, const size_t size) { return memory.vtable->alloc(memory, size); }
static void* mem_alloc_zero(const canard_mem_t memory, const size_t size)
{
    void* const ptr = mem_alloc(memory, size);
    if (ptr != NULL) {
        (void)memset(ptr, 0, size); // NOLINT(*-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    }
    return ptr;
}

static void mem_free(const canard_mem_t memory, const size_t size, void* const data)
{
    memory.vtable->free(memory, size, data);
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

static uint16_t crc_add_byte(const uint16_t crc, const byte_t byte)
{
    return (uint16_t)((uint16_t)(crc << 8U) ^ crc_table[(uint16_t)((uint16_t)(crc >> 8U) ^ byte) & BYTE_MAX]);
}

static uint16_t crc_add(const uint16_t crc, const size_t size, const void* const data)
{
    CANARD_ASSERT((data != NULL) || (size == 0U));
    uint16_t      out = crc;
    const byte_t* p   = (const byte_t*)data;
    for (size_t i = 0; i < size; i++) {
        out = crc_add_byte(out, *p);
        ++p;
    }
    return out;
}

static uint16_t crc_add_chain(uint16_t crc, const canard_bytes_chain_t chain) // NOLINT(*-no-recursion)
{
    crc = crc_add(crc, chain.bytes.size, chain.bytes.data);
    return (chain.next == NULL) ? crc : crc_add_chain(crc, *chain.next);
}

// ---------------------------------------------      LIST CONTAINER       ---------------------------------------------

static bool is_listed(const canard_list_t* const list, const canard_listed_t* const member)
{
    return (member->next != NULL) || (member->prev != NULL) || (list->head == member);
}

// No effect if not in the list.
static void delist(canard_list_t* const list, canard_listed_t* const member)
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
    CANARD_ASSERT((list->head != NULL) == (list->tail != NULL));
}

// Insert addendum before anchor. If anchor is NULL, insert at the tail.
// If the item is already in the list, it will be delisted first. Can be used for moving to the specified position.
static void enlist_before(canard_list_t* const list, canard_listed_t* const anchor, canard_listed_t* const addendum)
{
    delist(list, addendum);
    CANARD_ASSERT((addendum->next == NULL) && (addendum->prev == NULL));
    CANARD_ASSERT((list->head != NULL) == (list->tail != NULL));
    if (anchor == NULL) {
        addendum->prev = list->tail;
        if (list->tail != NULL) {
            list->tail->next = addendum;
        }
        list->tail = addendum;
        if (list->head == NULL) {
            list->head = addendum;
        }
    } else {
        addendum->next = anchor;
        addendum->prev = anchor->prev;
        if (anchor->prev != NULL) {
            anchor->prev->next = addendum;
        } else {
            list->head = addendum;
        }
        anchor->prev = addendum;
    }
    CANARD_ASSERT((list->head != NULL) && (list->tail != NULL));
}

// If the item is already in the list, it will be delisted first. Can be used for moving to the front/back.
static void enlist_tail(canard_list_t* const list, canard_listed_t* const member) { enlist_before(list, NULL, member); }

#define LIST_TAIL(list, owner_type, owner_field) LIST_MEMBER((list).tail, owner_type, owner_field)
#define LIST_HEAD(list, owner_type, owner_field) LIST_MEMBER((list).head, owner_type, owner_field)

#define LIST_NEXT(member, owner_type, owner_field) LIST_MEMBER((member)->owner_field.next, owner_type, owner_field)
#define LIST_PREV(member, owner_type, owner_field) LIST_MEMBER((member)->owner_field.prev, owner_type, owner_field)

#define LIST_MEMBER(ptr, owner_type, owner_field) ((owner_type*)ptr_unbias((ptr), offsetof(owner_type, owner_field)))
static void* ptr_unbias(const void* const ptr, const size_t offset)
{
    return (ptr == NULL) ? NULL : (void*)((char*)ptr - offset);
}

#define LIST_NULL                                    \
    (canard_listed_t) { .next = NULL, .prev = NULL }

// ---------------------------------------------  SCATTERED BYTES READER   ---------------------------------------------

typedef struct
{
    const canard_bytes_chain_t* cursor;   // Initially points at the head.
    size_t                      position; // Position within the current fragment, initially zero.
} bytes_chain_reader_t;

// Sequentially reads data from a scattered byte array into a contiguous destination buffer.
// Requires that the total amount of read data does not exceed the total size of the scattered array.
static void bytes_chain_read(bytes_chain_reader_t* const reader, const size_t size, void* const destination)
{
    CANARD_ASSERT((reader != NULL) && (reader->cursor != NULL) && (destination != NULL));
    byte_t* ptr       = (byte_t*)destination;
    size_t  remaining = size;
    while (remaining > 0U) {
        CANARD_ASSERT(reader->position <= reader->cursor->bytes.size);
        while (reader->position == reader->cursor->bytes.size) { // Advance while skipping empty fragments.
            reader->position = 0U;
            reader->cursor   = reader->cursor->next;
            CANARD_ASSERT(reader->cursor != NULL);
        }
        CANARD_ASSERT(reader->position < reader->cursor->bytes.size);
        const size_t progress = smaller(remaining, reader->cursor->bytes.size - reader->position);
        CANARD_ASSERT((progress > 0U) && (progress <= remaining));
        CANARD_ASSERT((reader->position + progress) <= reader->cursor->bytes.size);
        // NOLINTNEXTLINE(*DeprecatedOrUnsafeBufferHandling)
        (void)memcpy(ptr, ((const byte_t*)reader->cursor->bytes.data) + reader->position, progress);
        ptr += progress;
        remaining -= progress;
        reader->position += progress;
    }
}

static size_t bytes_chain_size(const canard_bytes_chain_t head)
{
    size_t                      size    = head.bytes.size;
    const canard_bytes_chain_t* current = head.next;
    while (current != NULL) {
        size += current->bytes.size;
        current = current->next;
    }
    return size;
}

static bool bytes_chain_valid(const canard_bytes_chain_t head)
{
    // Only check the head fragment; if it is valid, the rest are assumed valid as well.
    return (head.bytes.data != NULL) || (head.bytes.size == 0U);
}

// ---------------------------------------------            TX             ---------------------------------------------

// On a 32-bit platform, o1heap has a per-block overhead of sizeof(void*)*2=8 bytes, meaning that the available
// allocation sizes are 8 B (16 B block) 24 B (32 B block), 56 B (64 B block), 120 B (128 B block), etc.
// Size optimization is very important for Classic CAN because of its low MTU.
// Related: https://github.com/OpenCyphal/libcanard/issues/254
typedef struct tx_frame_t
{
    struct tx_frame_t* next;
    size_t             refcount : (sizeof(size_t) * CHAR_BIT) - DLC_BITS; // 268+ million ought to be enough for anybody
    size_t             dlc      : DLC_BITS; // use canard_len_to_dlc[] and canard_dlc_to_len[]
    byte_t             data[];
} tx_frame_t;
static_assert((sizeof(void*) > 4) || ((sizeof(tx_frame_t) + CANARD_MTU_CAN_CLASSIC) <= 24),
              "On a 32-bit platform with a half-fit heap, the full Classic CAN frame should fit in a 32-byte block");
static_assert((sizeof(void*) > 4) || ((sizeof(tx_frame_t) + CANARD_MTU_CAN_FD) <= 120),
              "On a 32-bit platform with a half-fit heap, the full CAN FD frame should fit in a 128-byte block");

static canard_bytes_t tx_frame_view(const tx_frame_t* const frame)
{
    return (canard_bytes_t){ .size = canard_dlc_to_len[frame->dlc], .data = frame->data };
}

static tx_frame_t* tx_frame_from_view(const canard_bytes_t view)
{
    return (tx_frame_t*)ptr_unbias(view.data, offsetof(tx_frame_t, data));
}

static tx_frame_t* tx_frame_new(canard_t* const self, const size_t data_size)
{
    CANARD_ASSERT(data_size <= CANARD_MTU_CAN_FD);
    CANARD_ASSERT(data_size == canard_dlc_to_len[canard_len_to_dlc[data_size]]); // NOLINT(*-security.ArrayBound)
    tx_frame_t* const frame = (tx_frame_t*)mem_alloc(self->mem.tx_frame, sizeof(tx_frame_t) + data_size);
    if (frame != NULL) {
        frame->next     = NULL;
        frame->refcount = 1U;
        frame->dlc      = canard_len_to_dlc[data_size] & 15U; // NOLINT(*-security.ArrayBound)
        // Update the count; this is decremented when the frame is freed upon refcount reaching zero.
        self->tx.queue_size++;
    }
    return frame;
}

void canard_refcount_inc(const canard_bytes_t obj)
{
    if (obj.data != NULL) {
        tx_frame_t* const frame = tx_frame_from_view(obj);
        CANARD_ASSERT(frame->refcount > 0U);
        ++frame->refcount; // TODO: if C11 is enabled, use stdatomic here
    }
}

void canard_refcount_dec(canard_t* const self, const canard_bytes_t obj)
{
    if (obj.data != NULL) {
        tx_frame_t* const frame = tx_frame_from_view(obj);
        CANARD_ASSERT(frame->refcount > 0U); // NOLINT(*-security.ArrayBound)
        CANARD_ASSERT(canard_dlc_to_len[frame->dlc] == obj.size);
        frame->refcount--;
        if (frame->refcount == 0U) {
            CANARD_ASSERT(self->tx.queue_size > 0U);
            self->tx.queue_size--;
            mem_free(self->mem.tx_frame, sizeof(tx_frame_t) + obj.size, frame);
        }
    }
}

// Everything except the local node-ID. The node-ID is not needed because it may be changed while the transfer
// is enqueued if a collision is detected; also, it is easy to add, and it is the same for all enqueued transfers,
// hence it would not affect the ordering.
#define CAN_ID_MSb_BITS (29U - 7U)

// The struct is manually packed to ensure it fits into a 128-byte O1Heap block in common embedded configurations.
struct canard_txfer_t
{
    canard_tree_t   index_pending[CANARD_IFACE_COUNT];
    canard_listed_t list_agewise;

    // Constant transfer properties supplied by the client.
    canard_us_t deadline;
    uint64_t    seqno;
    uint64_t    can_id_msb  : CAN_ID_MSb_BITS;
    uint64_t    transfer_id : CANARD_TRANSFER_ID_BIT_LENGTH;
    uint64_t    fd          : 1;

    // Mutable transmission state. All other fields, except for the index handles, are immutable.
    tx_frame_t* head[CANARD_IFACE_COUNT];
    tx_frame_t* cursor[CANARD_IFACE_COUNT];

    // Application context.
    canard_user_context_t user_context;
};
static_assert((CANARD_IFACE_COUNT > 2) || (sizeof(void*) > 4) || (sizeof(void (*)(void)) > 4) ||
                (sizeof(canard_txfer_t) <= 120),
              "On a 32-bit platform with a half-fit heap, the TX transfer object should fit in a 128-byte block");

static canard_txfer_t* txfer_new(canard_t* const             self,
                                 const canard_us_t           deadline,
                                 const byte_t                transfer_id,
                                 const uint32_t              can_id_template,
                                 const bool                  fd,
                                 const canard_user_context_t user_context)
{
    canard_txfer_t* const tr = mem_alloc_zero(self->mem.tx_transfer, sizeof(canard_txfer_t));
    if (tr != NULL) {
        FOREACH_IFACE (i) {
            tr->index_pending[i] = TREE_NULL;
        }
        tr->list_agewise = LIST_NULL;
        tr->deadline     = deadline;
        tr->seqno        = self->tx.seqno++;
        tr->transfer_id  = transfer_id & CANARD_TRANSFER_ID_MAX;
        tr->can_id_msb   = (can_id_template >> (29U - CAN_ID_MSb_BITS)) & ((1U << CAN_ID_MSb_BITS) - 1U);
        tr->fd           = fd ? 1U : 0U;
        FOREACH_IFACE (i) {
            tr->head[i]   = NULL;
            tr->cursor[i] = NULL;
        }
        tr->user_context = user_context;
    }
    return tr;
}

static canard_prio_t txfer_prio(const canard_txfer_t* const tr)
{
    return (canard_prio_t)((((unsigned)tr->can_id_msb) >> (CAN_ID_MSb_BITS - 3U)) & 7U);
}

static bool txfer_is_pending(const canard_t* const self, const canard_txfer_t* const tr)
{
    FOREACH_IFACE (i) {
        if (cavl2_is_inserted(self->tx.pending[i], &tr->index_pending[i])) {
            CANARD_ASSERT(tr->head[i] != NULL);
            CANARD_ASSERT(tr->cursor[i] != NULL);
            return true;
        }
    }
    return false;
}

static void txfer_free_payload(canard_t* const self, canard_txfer_t* const tr)
{
    CANARD_ASSERT(tr != NULL);
    FOREACH_IFACE (i) {
        const tx_frame_t* frame = tr->head[i];
        while (frame != NULL) {
            const tx_frame_t* const next = frame->next;
            canard_refcount_dec(self, tx_frame_view(frame));
            frame = next;
        }
        tr->head[i]   = NULL;
        tr->cursor[i] = NULL;
    }
}

// Smaller CAN ID on the left, then smaller seqno on the left.
static int32_t tx_cavl_compare_pending_order(const void* const user, const canard_tree_t* const node)
{
    const canard_txfer_t* const lhs = (const canard_txfer_t*)user;
    const canard_txfer_t* const rhs = CAVL2_TO_OWNER(node, canard_txfer_t, index_pending[0]); // clang-format off
    if (lhs->can_id_msb < rhs->can_id_msb) { return -1; }
    if (lhs->can_id_msb > rhs->can_id_msb) { return +1; }
    return (lhs->seqno < rhs->seqno) ? -1 : +1; // clang-format on
}

static void tx_make_pending(canard_t* const self, canard_txfer_t* const tr)
{
    FOREACH_IFACE (i) { // Enqueue for transmission unless it's there already (stalled interface?)
        if (((tr->head[i] != NULL)) && !cavl2_is_inserted(self->tx.pending[i], &tr->index_pending[i])) {
            CANARD_ASSERT(tr->cursor[i] == tr->head[i]); // must have been rewound after last attempt
            const canard_tree_t* const tree = cavl2_find_or_insert(
              &self->tx.pending[i], tr, tx_cavl_compare_pending_order, &tr->index_pending[i], cavl2_trivial_factory);
            CANARD_ASSERT(tree == &tr->index_pending[i]);
            (void)tree;
        }
    }
}

// Retire one transfer and release its resources.
static void txfer_retire(canard_t* const self, canard_txfer_t* const tr)
{
    if (self->tx.iter == tr) {
        self->tx.iter = LIST_NEXT(tr, canard_txfer_t, list_agewise); // May be NULL, is OK.
    }
    FOREACH_IFACE (i) {
        (void)cavl2_remove_if(&self->tx.pending[i], &tr->index_pending[i]);
    }
    delist(&self->tx.agewise, &tr->list_agewise);

    // Free the memory. The payload memory may already be empty depending on where we were invoked from.
    txfer_free_payload(self, tr);
    mem_free(self->mem.tx_transfer, sizeof(canard_txfer_t), tr);
}

static byte_t tx_make_tail_byte(const bool sot, const bool eot, const bool tog, const byte_t transfer_id)
{
    return (byte_t)((sot ? TAIL_SOT : 0U) | (eot ? TAIL_EOT : 0U) | (tog ? TAIL_TOGGLE : 0U) |
                    (transfer_id & CANARD_TRANSFER_ID_MAX));
}

// Takes a frame payload size, returns a new size that is >=x and is rounded up to the nearest valid DLC.
static size_t tx_ceil_frame_payload_size(const size_t x)
{
    CANARD_ASSERT(x < (sizeof(canard_len_to_dlc) / sizeof(canard_len_to_dlc[0])));
    return canard_dlc_to_len[canard_len_to_dlc[x]];
}

// Builds a chain of tx_frame_t instances, or NULL if OOM.
// This version works with Cyphal/CAN transfers. Legacy transfers require a different layout, see dedicated function.
static tx_frame_t* tx_spool(canard_t* const            self,
                            const uint16_t             crc_seed,
                            const size_t               mtu,
                            const byte_t               transfer_id,
                            const canard_bytes_chain_t payload)
{
    bytes_chain_reader_t reader = { .cursor = &payload, .position = 0U };
    tx_frame_t*          head   = NULL;
    const size_t         size   = bytes_chain_size(payload);
    bool                 toggle = true; // Cyphal transfers start with toggle==1, unlike legacy
    if (size < mtu) {                   // Single-frame transfer; no CRC required -- easy case.
        const size_t frame_size = tx_ceil_frame_payload_size(size + 1U);
        head                    = tx_frame_new(self, frame_size);
        if (head != NULL) {
            bytes_chain_read(&reader, size, head->data);
            // NOLINTNEXTLINE(*-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
            memset(&head->data[size], PADDING_BYTE_VALUE, frame_size - size - 1U);
            head->data[frame_size - 1U] = tx_make_tail_byte(true, true, toggle, transfer_id);
        }
    } else {
        const size_t size_with_crc = size + CRC_SIZE_BYTES;
        size_t       offset        = 0U;
        uint16_t     crc           = crc_seed;
        tx_frame_t*  tail          = NULL;
        while (offset < size_with_crc) {
            const size_t frame_size_with_tail =
              ((size_with_crc - offset) < (mtu - 1U))
                ? tx_ceil_frame_payload_size((size_with_crc - offset) + 1U) // padding last frame only
                : mtu;
            tx_frame_t* const item = tx_frame_new(self, frame_size_with_tail);
            if (NULL == head) {
                head = item;
            } else {
                tail->next = item;
            }
            tail = item;
            // On OOM, deallocate the entire chain and quit.
            if (NULL == tail) {
                while (head != NULL) {
                    tx_frame_t* const next = head->next;
                    canard_refcount_dec(self, tx_frame_view(head));
                    head = next;
                }
                break;
            }
            // Populate the frame contents.
            const size_t frame_size   = frame_size_with_tail - 1U;
            size_t       frame_offset = 0U;
            if (offset < size) {
                const size_t move_size = smaller(size - offset, frame_size);
                bytes_chain_read(&reader, move_size, tail->data);
                crc = crc_add(crc, move_size, tail->data);
                frame_offset += move_size;
                offset += move_size;
            }
            // Handle the last frame of the transfer: it is special because it also contains padding and CRC.
            if (offset >= size) {
                // Insert padding -- only in the last frame. Include the padding bytes into the CRC.
                while ((frame_offset + CRC_SIZE_BYTES) < frame_size) {
                    tail->data[frame_offset] = PADDING_BYTE_VALUE;
                    ++frame_offset;
                    crc = crc_add_byte(crc, PADDING_BYTE_VALUE);
                }
                // Insert the CRC.
                if ((frame_offset < frame_size) && (offset == size)) {
                    tail->data[frame_offset] = (byte_t)((crc >> 8U) & BYTE_MAX); // NOLINT(*-signed-bitwise)
                    ++frame_offset;
                    ++offset;
                }
                if ((frame_offset < frame_size) && (offset > size)) {
                    tail->data[frame_offset] = (byte_t)(crc & BYTE_MAX);
                    ++frame_offset;
                    ++offset;
                }
            }
            // Finalize the frame.
            CANARD_ASSERT((frame_offset + 1U) == canard_dlc_to_len[tail->dlc]);
            tail->data[frame_offset] = tx_make_tail_byte(head == tail, offset >= size_with_crc, toggle, transfer_id);
            toggle                   = !toggle;
        }
    }
    return head;
}

// The legacy counterpart of tx_spool() for UAVCAN v0 transfers.
// Always uses Classic CAN MTU because UAVCAN v0 does not support CAN FD.
static tx_frame_t* tx_spool_v0(canard_t* const            self,
                               const uint16_t             crc_seed,
                               const byte_t               transfer_id,
                               const canard_bytes_chain_t payload)
{
    bool         toggle = false; // in v0, toggle starts with zero; that's how v0/v1 can be distinguished
    const size_t size   = bytes_chain_size(payload);
    if (size < CANARD_MTU_CAN_CLASSIC) { // single-frame transfer
        tx_frame_t* const item = tx_frame_new(self, size + 1U);
        if (item != NULL) {
            bytes_chain_reader_t reader = { .cursor = &payload, .position = 0U };
            bytes_chain_read(&reader, size, item->data);
            item->data[size] = tx_make_tail_byte(true, true, toggle, transfer_id);
        }
        return item;
    }
    const uint16_t crc = crc_add_chain(crc_seed, payload);
    // NOLINTNEXTLINE(*-signed-bitwise) v0 CRC is little-endian, which is not the native ordering of CRC-16-CCITT.
    const byte_t crc_bytes[CRC_SIZE_BYTES]   = { (byte_t)((crc >> 0U) & BYTE_MAX), (byte_t)((crc >> 8U) & BYTE_MAX) };
    const size_t size_total                  = size + CRC_SIZE_BYTES;
    const canard_bytes_chain_t payload_total = { .bytes = { .size = CRC_SIZE_BYTES, .data = crc_bytes },
                                                 .next  = &payload };
    bytes_chain_reader_t       reader        = { .cursor = &payload_total, .position = 0U };
    tx_frame_t*                head          = NULL;
    tx_frame_t*                tail          = NULL;
    size_t                     offset        = 0U;
    while (offset < size_total) {
        tx_frame_t* const item = tx_frame_new(self, smaller((size_total - offset) + 1U, CANARD_MTU_CAN_CLASSIC));
        if (NULL == head) {
            head = item;
        } else {
            tail->next = item;
        }
        tail = item;
        // On OOM, deallocate the entire chain and quit.
        if (NULL == tail) {
            while (head != NULL) {
                tx_frame_t* const next = head->next;
                canard_refcount_dec(self, tx_frame_view(head));
                head = next;
            }
            break;
        }
        // Populate the frame contents.
        const size_t progress = smaller(size_total - offset, canard_dlc_to_len[tail->dlc] - 1U);
        bytes_chain_read(&reader, progress, tail->data);
        offset += progress;
        CANARD_ASSERT((progress + 1U) == canard_dlc_to_len[tail->dlc]);
        CANARD_ASSERT(offset <= size_total);
        tail->data[progress] = tx_make_tail_byte(head == tail, offset == size_total, toggle, transfer_id);
        toggle               = !toggle;
    }
    return head;
}

// When the queue is exhausted, finds a transfer to sacrifice using simple heuristics and returns it.
// Will return NULL if there are no transfers worth sacrificing (no queue space can be reclaimed).
// We cannot simply stop accepting new transfers when the queue is full, because it may be caused by a single
// stalled interface holding back progress for all transfers.
// The heuristics are subject to review and improvement.
static canard_txfer_t* tx_sacrifice(const canard_t* const self)
{
    return LIST_HEAD(self->tx.agewise, canard_txfer_t, list_agewise);
}

// True on success, false if not possible to reclaim enough space.
static bool tx_ensure_queue_space(canard_t* const self, const size_t total_frames_needed)
{
    if (total_frames_needed > self->tx.queue_capacity) {
        return false; // not gonna happen
    }
    while (total_frames_needed > (self->tx.queue_capacity - self->tx.queue_size)) {
        canard_txfer_t* const tr = tx_sacrifice(self);
        if (tr == NULL) {
            break; // We may have no transfers anymore but the CAN driver could still be holding some pending frames.
        }
        txfer_retire(self, tr);
        self->err.tx_sacrifice++;
    }
    return total_frames_needed <= (self->tx.queue_capacity - self->tx.queue_size);
}

static size_t tx_predict_frame_count(const size_t transfer_size, const size_t mtu)
{
    const size_t bytes_per_frame = mtu - 1U; // 1 byte is used for the tail byte
    if (transfer_size <= bytes_per_frame) {
        return 1U; // single-frame transfer
    }
    return ((transfer_size + CRC_SIZE_BYTES + bytes_per_frame) - 1U) / bytes_per_frame; // rounding up
}

// Enqueues a transfer for transmission.
static bool tx_push(canard_t* const            self,
                    canard_txfer_t* const      tr,
                    const bool                 v0,
                    const byte_t               iface_bitmap,
                    const canard_bytes_chain_t payload,
                    const uint16_t             crc_seed)
{
    CANARD_ASSERT(tr != NULL);
    CANARD_ASSERT((!tr->fd) || !v0); // The caller must ensure this.

    // Ensure the queue has enough space. v0 transfers always use Classic CAN regardless of tr->fd.
    const size_t mtu      = tr->fd ? CANARD_MTU_CAN_FD : CANARD_MTU_CAN_CLASSIC;
    const size_t size     = bytes_chain_size(payload); // TODO: pass the precomputed size into spool functions
    const size_t n_frames = tx_predict_frame_count(size, mtu);
    CANARD_ASSERT(n_frames > 0);
    if (!tx_ensure_queue_space(self, n_frames)) {
        self->err.tx_capacity++;
        mem_free(self->mem.tx_transfer, sizeof(canard_txfer_t), tr);
        return false;
    }

    // Make a shared frame spool. Unlike the Cyphal/UDP implementation, we require all ifaces to use the same MTU.
    const size_t      queue_size_before = self->tx.queue_size;
    tx_frame_t* const spool             = v0 ? tx_spool_v0(self, crc_seed, tr->transfer_id, payload)
                                             : tx_spool(self, crc_seed, mtu, tr->transfer_id, payload);
    if (spool == NULL) {
        self->err.oom++;
        mem_free(self->mem.tx_transfer, sizeof(canard_txfer_t), tr);
        return false;
    }
    CANARD_ASSERT((self->tx.queue_size - queue_size_before) == n_frames);
    CANARD_ASSERT(self->tx.queue_size <= self->tx.queue_capacity);
    (void)queue_size_before;

    // Adjust the spooled frame refcounts to avoid premature deallocation.
    const byte_t frame_refcount_inc = (byte_t)(popcount(iface_bitmap) - 1U);
    CANARD_ASSERT(frame_refcount_inc < CANARD_IFACE_COUNT);
    if (frame_refcount_inc > 0) {
        tx_frame_t* frame = spool;
        while (frame != NULL) {
            frame->refcount += frame_refcount_inc;
            frame = frame->next;
        }
    }

    // Attach the spool.
    FOREACH_IFACE (i) {
        if ((iface_bitmap & (1U << i)) != 0) {
            tr->head[i]   = spool;
            tr->cursor[i] = spool;
        }
    }

    // Register the transfer and schedule for transmission.
    enlist_tail(&self->tx.agewise, &tr->list_agewise);
    tx_make_pending(self, tr);
    return true;
}

bool canard_publish(canard_t* const             self,
                    const canard_us_t           deadline,
                    const uint_least8_t         iface_bitmap,
                    const canard_prio_t         priority,
                    const uint32_t              subject_id,
                    const uint_least8_t         transfer_id,
                    const canard_bytes_chain_t  payload,
                    const canard_user_context_t context)
{
    bool ok =
      (self != NULL) && (priority < CANARD_PRIO_COUNT) && bytes_chain_valid(payload) &&
      (((iface_bitmap & CANARD_IFACE_BITMAP_ALL) != 0) && ((iface_bitmap & CANARD_IFACE_BITMAP_ALL) == iface_bitmap)) &&
      (subject_id <= CANARD_SUBJECT_ID_MAX);
    if (ok) {
        const uint32_t        can_id = (((uint32_t)priority) << PRIO_SHIFT) | (subject_id << 8U) | (1UL << 7U);
        canard_txfer_t* const tr     = txfer_new(self, deadline, transfer_id, can_id, self->tx.fd, context);
        ok                           = (tr != NULL) && tx_push(self, tr, false, iface_bitmap, payload, CRC_INITIAL);
    }
    return ok;
}

bool canard_1v0_publish(canard_t* const             self,
                        const canard_us_t           deadline,
                        const uint_least8_t         iface_bitmap,
                        const canard_prio_t         priority,
                        const uint16_t              subject_id,
                        const uint_least8_t         transfer_id,
                        const canard_bytes_chain_t  payload,
                        const canard_user_context_t context)
{
    bool ok =
      (self != NULL) && (priority < CANARD_PRIO_COUNT) && bytes_chain_valid(payload) &&
      (((iface_bitmap & CANARD_IFACE_BITMAP_ALL) != 0) && ((iface_bitmap & CANARD_IFACE_BITMAP_ALL) == iface_bitmap)) &&
      (subject_id <= CANARD_SUBJECT_ID_MAX_1v0);
    if (ok) {
        const uint32_t can_id    = (((uint32_t)priority) << PRIO_SHIFT) | (3UL << 21U) | (((uint32_t)subject_id) << 8U);
        canard_txfer_t* const tr = txfer_new(self, deadline, transfer_id, can_id, self->tx.fd, context);
        ok                       = (tr != NULL) && tx_push(self, tr, false, iface_bitmap, payload, CRC_INITIAL);
    }
    return ok;
}

bool canard_0v1_publish(canard_t* const             self,
                        const canard_us_t           deadline,
                        const uint_least8_t         iface_bitmap,
                        const canard_prio_t         priority,
                        const uint16_t              data_type_id,
                        const uint16_t              crc_seed,
                        const uint_least8_t         transfer_id,
                        const canard_bytes_chain_t  payload,
                        const canard_user_context_t context)
{
    bool ok =
      (self != NULL) && (priority < CANARD_PRIO_COUNT) && bytes_chain_valid(payload) &&
      (((iface_bitmap & CANARD_IFACE_BITMAP_ALL) != 0) && ((iface_bitmap & CANARD_IFACE_BITMAP_ALL) == iface_bitmap)) &&
      (self->node_id != 0);
    if (ok) {
        const uint32_t can_id    = (((uint32_t)priority) << PRIO_SHIFT) | (3UL << 24U) | ((uint32_t)data_type_id << 8U);
        canard_txfer_t* const tr = txfer_new(self, deadline, transfer_id, can_id, false, context);
        ok                       = (tr != NULL) && tx_push(self, tr, true, iface_bitmap, payload, crc_seed);
    }
    return ok;
}
