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

#define PADDING_BYTE_VALUE 0U

#define PRIO_SHIFT 26U
#define PRIO_MASK  7U

#define TAIL_SOT    128U
#define TAIL_EOT    64U
#define TAIL_TOGGLE 32U

#define FOREACH_IFACE(i) for (size_t i = 0; (i) < CANARD_IFACE_COUNT; (i)++)
#define FOREACH_PRIO(i)  for (size_t i = 0; (i) < CANARD_PRIO_COUNT; (i)++)

#if CANARD_IFACE_COUNT <= 2
#define IFACE_INDEX_BITS 1U
#elif CANARD_IFACE_COUNT <= 4
#define IFACE_INDEX_BITS 2U
#elif CANARD_IFACE_COUNT <= 8
#define IFACE_INDEX_BITS 3U
#else
#error "Too many interfaces"
#endif

#define TREE_NULL (canard_tree_t){ NULL, { NULL, NULL }, 0 }

#define KILO 1000LL
#define MEGA (KILO * KILO)

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
static uint64_t    min_u64(const uint64_t a, const uint64_t b) { return (a < b) ? a : b; }
static uint64_t    max_u64(const uint64_t a, const uint64_t b) { return (a > b) ? a : b; }
static int64_t     min_i64(const int64_t a, const int64_t b) { return (a < b) ? a : b; }
static int64_t     max_i64(const int64_t a, const int64_t b) { return (a > b) ? a : b; }
static canard_us_t sooner(const canard_us_t a, const canard_us_t b) { return min_i64(a, b); }
static canard_us_t later(const canard_us_t a, const canard_us_t b) { return max_i64(a, b); }

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
    if (data != NULL) {
        memory.vtable->free(memory, size, data);
    }
}

static bool mem_valid(const canard_mem_t memory)
{
    return (memory.vtable != NULL) && (memory.vtable->alloc != NULL) && (memory.vtable->free != NULL);
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
    size_t             dlc : DLC_BITS; // use canard_len_to_dlc[] and canard_dlc_to_len[]
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

// The struct must fit into a 128-byte O1Heap block in common embedded configurations.
typedef struct tx_transfer_t
{
    canard_tree_t   index_pending[CANARD_IFACE_COUNT];
    canard_tree_t   index_deadline;
    canard_listed_t list_agewise;

    // Constant transfer properties supplied by the client.
    canard_us_t deadline;
    uint64_t    seqno;
    uint64_t    can_id_msb : CAN_ID_MSb_BITS;
    uint64_t    transfer_id : CANARD_TRANSFER_ID_BITS; // TODO remove, not needed.
    uint64_t    fd : 1;

    // Mutable transmission state. All other fields, except for the index handles, are immutable.
    tx_frame_t* head[CANARD_IFACE_COUNT];
    tx_frame_t* cursor[CANARD_IFACE_COUNT];

    // Application context.
    canard_user_context_t user_context;
} tx_transfer_t;
static_assert((CANARD_IFACE_COUNT > 2) || (sizeof(void*) > 4) || (sizeof(tx_transfer_t) <= 120),
              "On a 32-bit platform with a half-fit heap, the TX transfer object should fit in a 128-byte block");

static tx_transfer_t* tx_transfer_new(canard_t* const             self,
                                      const canard_us_t           deadline,
                                      const byte_t                transfer_id,
                                      const uint32_t              can_id_template,
                                      const bool                  fd,
                                      const canard_user_context_t user_context)
{
    tx_transfer_t* const tr = mem_alloc_zero(self->mem.tx_transfer, sizeof(tx_transfer_t));
    if (tr != NULL) {
        FOREACH_IFACE (i) {
            tr->index_pending[i] = TREE_NULL;
        }
        tr->index_deadline = TREE_NULL;
        tr->list_agewise   = LIST_NULL;
        tr->deadline       = deadline;
        tr->seqno          = self->tx.seqno++;
        tr->transfer_id    = transfer_id & CANARD_TRANSFER_ID_MAX;
        tr->can_id_msb     = (can_id_template >> (29U - CAN_ID_MSb_BITS)) & ((1U << CAN_ID_MSb_BITS) - 1U);
        tr->fd             = fd ? 1U : 0U;
        FOREACH_IFACE (i) {
            tr->head[i]   = NULL;
            tr->cursor[i] = NULL;
        }
        tr->user_context = user_context;
    }
    return tr;
}

static bool tx_is_pending(const canard_t* const self, const tx_transfer_t* const tr)
{
    FOREACH_IFACE (i) {
        if (cavl2_is_inserted(self->tx.pending[i], &tr->index_pending[i])) {
            CANARD_ASSERT((tr->head[i] != NULL) && (tr->cursor[i] != NULL));
            return true;
        }
    }
    return false;
}

static void tx_free_payload(canard_t* const self, tx_transfer_t* const tr)
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
    const tx_transfer_t* const lhs = (const tx_transfer_t*)user;
    const tx_transfer_t* const rhs = CAVL2_TO_OWNER(node, tx_transfer_t, index_pending[0]); // clang-format off
    if (lhs->can_id_msb < rhs->can_id_msb) { return -1; }
    if (lhs->can_id_msb > rhs->can_id_msb) { return +1; }
    return (lhs->seqno < rhs->seqno) ? -1 : +1; // clang-format on
}

// Soonest to expire (smallest deadline) on the left, then smaller seqno on the left.
static int32_t tx_cavl_compare_deadline(const void* const user, const canard_tree_t* const node)
{
    const tx_transfer_t* const lhs = (const tx_transfer_t*)user;
    const tx_transfer_t* const rhs = CAVL2_TO_OWNER(node, tx_transfer_t, index_deadline); // clang-format off
    if (lhs->deadline < rhs->deadline) { return -1; }
    if (lhs->deadline > rhs->deadline) { return +1; }
    return (lhs->seqno < rhs->seqno) ? -1 : +1; // clang-format on
}

static void tx_make_pending(canard_t* const self, tx_transfer_t* const tr)
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
static void tx_retire(canard_t* const self, tx_transfer_t* const tr)
{
    FOREACH_IFACE (i) {
        (void)cavl2_remove_if(&self->tx.pending[i], &tr->index_pending[i]);
    }
    CANARD_ASSERT(cavl2_is_inserted(self->tx.deadline, &tr->index_deadline));
    cavl2_remove(&self->tx.deadline, &tr->index_deadline);
    delist(&self->tx.agewise, &tr->list_agewise);
    tx_free_payload(self, tr);
    mem_free(self->mem.tx_transfer, sizeof(tx_transfer_t), tr);
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
                            const size_t               size,
                            const canard_bytes_chain_t payload)
{
    bytes_chain_reader_t reader = { .cursor = &payload, .position = 0U };
    tx_frame_t*          head   = NULL;
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
                               const size_t               size,
                               const canard_bytes_chain_t payload)
{
    bool toggle = false;                 // in v0, toggle starts with zero; that's how v0/v1 can be distinguished
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
        // On OOM, deallocate the entire chain and quit.
        if (NULL == item) {
            while (head != NULL) {
                tx_frame_t* const next = head->next;
                canard_refcount_dec(self, tx_frame_view(head));
                head = next;
            }
            break;
        }
        // Append the new item.
        if (NULL == head) {
            head = item;
        } else {
            CANARD_ASSERT(NULL != tail);
            tail->next = item;
        }
        tail = item;
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
static tx_transfer_t* tx_sacrifice(const canard_t* const self)
{
    return LIST_HEAD(self->tx.agewise, tx_transfer_t, list_agewise);
}

// True on success, false if not possible to reclaim enough space.
static bool tx_ensure_queue_space(canard_t* const self, const size_t total_frames_needed)
{
    if (total_frames_needed > self->tx.queue_capacity) {
        return false; // not gonna happen
    }
    while (total_frames_needed > (self->tx.queue_capacity - self->tx.queue_size)) {
        tx_transfer_t* const tr = tx_sacrifice(self);
        if (tr == NULL) {
            break; // We may have no transfers anymore but the CAN driver could still be holding some pending frames.
        }
        tx_retire(self, tr);
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

static void tx_expire(canard_t* const self, const canard_us_t now)
{
    tx_transfer_t* tr = CAVL2_TO_OWNER(cavl2_min(self->tx.deadline), tx_transfer_t, index_deadline);
    while ((tr != NULL) && (now > tr->deadline)) {
        tx_transfer_t* const tr_next =
          CAVL2_TO_OWNER(cavl2_next_greater(&tr->index_deadline), tx_transfer_t, index_deadline);
        tx_retire(self, tr);
        self->err.tx_expiration++;
        tr = tr_next;
    }
}

// Enqueues a transfer for transmission.
static bool tx_push(canard_t* const            self,
                    tx_transfer_t* const       tr,
                    const bool                 v0,
                    const byte_t               iface_bitmap,
                    const canard_bytes_chain_t payload,
                    const uint16_t             crc_seed)
{
    CANARD_ASSERT(tr != NULL);
    CANARD_ASSERT((!tr->fd) || !v0); // The caller must ensure this.
    CANARD_ASSERT(iface_bitmap != 0);

    const canard_us_t now = self->vtable->now(self);

    // Expire old transfers first to free up queue space.
    tx_expire(self, now);

    // Ensure the queue has enough space. v0 transfers always use Classic CAN regardless of tr->fd.
    const size_t mtu      = tr->fd ? CANARD_MTU_CAN_FD : CANARD_MTU_CAN_CLASSIC;
    const size_t size     = bytes_chain_size(payload);
    const size_t n_frames = tx_predict_frame_count(size, mtu);
    CANARD_ASSERT(n_frames > 0);
    if (!tx_ensure_queue_space(self, n_frames)) {
        self->err.tx_capacity++;
        mem_free(self->mem.tx_transfer, sizeof(tx_transfer_t), tr);
        return false;
    }

    // Make a shared frame spool. Unlike the Cyphal/UDP implementation, we require all ifaces to use the same MTU.
    const size_t      queue_size_before = self->tx.queue_size;
    tx_frame_t* const spool             = v0 ? tx_spool_v0(self, crc_seed, tr->transfer_id, size, payload)
                                             : tx_spool(self, crc_seed, mtu, tr->transfer_id, size, payload);
    if (spool == NULL) {
        self->err.oom++;
        mem_free(self->mem.tx_transfer, sizeof(tx_transfer_t), tr);
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
    const canard_tree_t* const deadline_tree = cavl2_find_or_insert(
      &self->tx.deadline, tr, tx_cavl_compare_deadline, &tr->index_deadline, cavl2_trivial_factory);
    CANARD_ASSERT(deadline_tree == &tr->index_deadline);
    (void)deadline_tree;
    enlist_tail(&self->tx.agewise, &tr->list_agewise);
    tx_make_pending(self, tr);
    return true;
}

static tx_transfer_t* tx_pending_node_to_transfer(const canard_tree_t* const node, const byte_t iface_index)
{
    return (tx_transfer_t*)ptr_unbias(
      node, offsetof(tx_transfer_t, index_pending) + (((size_t)iface_index) * sizeof(canard_tree_t)));
}

static void tx_eject_pending(canard_t* const self, const byte_t iface_index)
{
    while (true) {
        const canard_tree_t* const pending = cavl2_min(self->tx.pending[iface_index]);
        if (pending == NULL) {
            break;
        }
        tx_transfer_t* const tr = tx_pending_node_to_transfer(pending, iface_index);
        CANARD_ASSERT((tr->head[iface_index] != NULL) && (tr->cursor[iface_index] != NULL));

        // Try to eject one frame.
        tx_frame_t* const frame      = tr->cursor[iface_index];
        tx_frame_t* const frame_next = frame->next;
        const bool        ejected    = self->vtable->tx(self,
                                              tr->user_context,
                                              tr->deadline,
                                              iface_index,
                                              tr->fd != 0U,
                                              (((uint32_t)tr->can_id_msb) << 7U) | self->node_id,
                                              tx_frame_view(frame));
        if (!ejected) {
            break;
        }

        // Commit successful ejection by advancing this interface cursor.
        tr->head[iface_index]   = frame_next;
        tr->cursor[iface_index] = frame_next;
        canard_refcount_dec(self, tx_frame_view(frame));

        // If this interface is done with the transfer, remove it from this pending tree.
        if (frame_next == NULL) {
            (void)cavl2_remove_if(&self->tx.pending[iface_index], &tr->index_pending[iface_index]);
            if (!tx_is_pending(self, tr)) {
                tx_retire(self, tr);
            }
        }
    }
}

uint_least8_t canard_pending_ifaces(const canard_t* const self)
{
    uint_least8_t out = 0;
    if (self != NULL) {
        FOREACH_IFACE (i) {
            out |= (self->tx.pending[i] != NULL) ? (uint_least8_t)(1U << i) : 0U;
        }
    }
    return out;
}

static bool tx_1v0_service(canard_t* const             self,
                           const canard_us_t           deadline,
                           const canard_prio_t         priority,
                           const uint16_t              service_id,
                           const uint_least8_t         destination_node_id,
                           const bool                  request_not_response,
                           const uint_least8_t         transfer_id,
                           const canard_bytes_chain_t  payload,
                           const canard_user_context_t context)
{
    bool ok = (self != NULL) && (priority < CANARD_PRIO_COUNT) && bytes_chain_valid(payload) &&
              (service_id <= CANARD_SERVICE_ID_MAX) && (destination_node_id <= CANARD_NODE_ID_MAX);
    if (ok) {
        const uint32_t can_id = (((uint32_t)priority) << PRIO_SHIFT) | (UINT32_C(1) << 25U) |
                                (request_not_response ? (UINT32_C(1) << 24U) : 0U) | (((uint32_t)service_id) << 14U) |
                                (((uint32_t)destination_node_id) << 7U);
        tx_transfer_t* const tr = tx_transfer_new(self, deadline, transfer_id, can_id, self->tx.fd, context);
        ok = (tr != NULL) && tx_push(self, tr, false, CANARD_IFACE_BITMAP_ALL, payload, CRC_INITIAL);
    }
    return ok;
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
        const uint32_t       can_id = (((uint32_t)priority) << PRIO_SHIFT) | (subject_id << 8U) | (UINT32_C(1) << 7U);
        tx_transfer_t* const tr     = tx_transfer_new(self, deadline, transfer_id, can_id, self->tx.fd, context);
        ok                          = (tr != NULL) && tx_push(self, tr, false, iface_bitmap, payload, CRC_INITIAL);
    }
    return ok;
}

bool canard_unicast(canard_t* const             self,
                    const canard_us_t           deadline,
                    const uint_least8_t         destination_node_id,
                    const canard_prio_t         priority,
                    const canard_bytes_chain_t  payload,
                    const canard_user_context_t context)
{
    const bool ok = (self != NULL) && (priority < CANARD_PRIO_COUNT) && bytes_chain_valid(payload) &&
                    (destination_node_id <= CANARD_NODE_ID_MAX);
    return ok && tx_1v0_service(self,
                                deadline,
                                priority,
                                CANARD_SERVICE_ID_UNICAST,
                                destination_node_id,
                                true,
                                self->unicast_transfer_id[destination_node_id]++,
                                payload,
                                context);
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
        const uint32_t can_id =
          (((uint32_t)priority) << PRIO_SHIFT) | (UINT32_C(3) << 21U) | (((uint32_t)subject_id) << 8U);
        tx_transfer_t* const tr = tx_transfer_new(self, deadline, transfer_id, can_id, self->tx.fd, context);
        ok                      = (tr != NULL) && tx_push(self, tr, false, iface_bitmap, payload, CRC_INITIAL);
    }
    return ok;
}

bool canard_1v0_request(canard_t* const             self,
                        const canard_us_t           deadline,
                        const canard_prio_t         priority,
                        const uint16_t              service_id,
                        const uint_least8_t         server_node_id,
                        const uint_least8_t         transfer_id,
                        const canard_bytes_chain_t  payload,
                        const canard_user_context_t context)
{
    return tx_1v0_service(self, deadline, priority, service_id, server_node_id, true, transfer_id, payload, context);
}

bool canard_1v0_respond(canard_t* const             self,
                        const canard_us_t           deadline,
                        const canard_prio_t         priority,
                        const uint16_t              service_id,
                        const uint_least8_t         client_node_id,
                        const uint_least8_t         transfer_id,
                        const canard_bytes_chain_t  payload,
                        const canard_user_context_t context)
{
    return tx_1v0_service(self, deadline, priority, service_id, client_node_id, false, transfer_id, payload, context);
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
        const uint32_t can_id =
          (((uint32_t)priority) << PRIO_SHIFT) | (UINT32_C(3) << 24U) | ((uint32_t)data_type_id << 8U);
        tx_transfer_t* const tr = tx_transfer_new(self, deadline, transfer_id, can_id, false, context);
        ok                      = (tr != NULL) && tx_push(self, tr, true, iface_bitmap, payload, crc_seed);
    }
    return ok;
}

static bool tx_0v1_service(canard_t* const             self,
                           const canard_us_t           deadline,
                           const canard_prio_t         priority,
                           const uint_least8_t         data_type_id,
                           const uint16_t              crc_seed,
                           const uint_least8_t         destination_node_id,
                           const bool                  request_not_response,
                           const uint_least8_t         transfer_id,
                           const canard_bytes_chain_t  payload,
                           const canard_user_context_t context)
{
    bool ok = (self != NULL) && (priority < CANARD_PRIO_COUNT) && bytes_chain_valid(payload) && (self->node_id != 0U) &&
              (destination_node_id > 0U) && (destination_node_id <= CANARD_NODE_ID_MAX);
    if (ok) {
        const uint32_t can_id = (((((uint32_t)priority) << 2U) | UINT32_C(3)) << 24U) |
                                (((uint32_t)data_type_id) << 16U) | (request_not_response ? (UINT32_C(1) << 15U) : 0U) |
                                (((uint32_t)destination_node_id) << 8U) | (UINT32_C(1) << 7U);
        tx_transfer_t* const tr = tx_transfer_new(self, deadline, transfer_id, can_id, false, context);
        ok                      = (tr != NULL) && tx_push(self, tr, true, CANARD_IFACE_BITMAP_ALL, payload, crc_seed);
    }
    return ok;
}

bool canard_0v1_request(canard_t* const             self,
                        const canard_us_t           deadline,
                        const canard_prio_t         priority,
                        const uint_least8_t         data_type_id,
                        const uint16_t              crc_seed,
                        const uint_least8_t         server_node_id,
                        const uint_least8_t         transfer_id,
                        const canard_bytes_chain_t  payload,
                        const canard_user_context_t context)
{
    return tx_0v1_service(
      self, deadline, priority, data_type_id, crc_seed, server_node_id, true, transfer_id, payload, context);
}

bool canard_0v1_respond(canard_t* const             self,
                        const canard_us_t           deadline,
                        const canard_prio_t         priority,
                        const uint_least8_t         data_type_id,
                        const uint16_t              crc_seed,
                        const uint_least8_t         client_node_id,
                        const uint_least8_t         transfer_id,
                        const canard_bytes_chain_t  payload,
                        const canard_user_context_t context)
{
    return tx_0v1_service(
      self, deadline, priority, data_type_id, crc_seed, client_node_id, false, transfer_id, payload, context);
}

// ---------------------------------------------            RX             ---------------------------------------------

typedef struct
{
    canard_prio_t priority;
    canard_kind_t kind;

    uint32_t port_id; // in v0 this stores the data type ID.

    byte_t dst;
    byte_t src;

    byte_t transfer_id;

    bool start;
    bool end;
    bool toggle;

    canard_bytes_t payload;
} frame_t;

// The protocol version is only unambiguously detectable in the first frame of a transfer.
// In non-first frames, we attempt to parse the frame as both versions simultaneously and then let the caller
// decide which one is correct by checking for incomplete multi-frame reassembly states.
// The return value is a bitmask indicating which of the versions have been parsed at this level.
static byte_t rx_parse(const uint32_t       can_id,
                       const canard_bytes_t payload_raw,
                       frame_t* const       out_v0,
                       frame_t* const       out_v1)
{
    CANARD_ASSERT(can_id < (UINT32_C(1) << 29U));
    CANARD_ASSERT(out_v0 != NULL);
    CANARD_ASSERT(out_v1 != NULL);
    memset(out_v0, 0, sizeof(*out_v0));
    memset(out_v1, 0, sizeof(*out_v1));
    if (payload_raw.size < 1) {
        return 0;
    }
    CANARD_ASSERT(payload_raw.data != NULL);

    // Parse the tail byte.
    const byte_t tail        = ((const byte_t*)payload_raw.data)[payload_raw.size - 1U];
    const bool   start       = (tail & TAIL_SOT) != 0U;
    const bool   end         = (tail & TAIL_EOT) != 0U;
    const bool   toggle      = (tail & TAIL_TOGGLE) != 0U;
    const byte_t transfer_id = tail & CANARD_TRANSFER_ID_MAX;

    // Common items.
    const canard_prio_t priority = (canard_prio_t)(can_id >> PRIO_SHIFT);
    const byte_t        src      = (byte_t)(can_id & CANARD_NODE_ID_MAX);

    // Validate the payload.
    const canard_bytes_t payload = { .size = payload_raw.size - 1U, .data = payload_raw.data };
    const bool payload_ok = (end || (payload_raw.size >= CANARD_MTU_CAN_CLASSIC)) && // non-last must use full MTU
                            ((start && end) || (payload.size > 0));                  // multi-frame cannot be empty

    // Version detection: v1 requires the toggle to start from 1, v0 starts from 0.
    // If this is not the first frame of a transfer, the version is not detectable, so we attempt to parse both.
    bool is_v1 = !(start && !toggle) && payload_ok;
    bool is_v0 = !(start && toggle) && payload_ok;
    if (is_v1) {
        out_v1->priority    = priority;
        out_v1->src         = src;
        out_v1->transfer_id = transfer_id;
        out_v1->start       = start;
        out_v1->end         = end;
        out_v1->toggle      = toggle;
        out_v1->payload     = payload;
        const bool svc      = (can_id & (UINT32_C(1) << 25U)) != 0U;
        const bool bit_23   = (can_id & (UINT32_C(1) << 23U)) != 0U;
        if (svc) {
            out_v1->dst     = (byte_t)((can_id >> 7U) & CANARD_NODE_ID_MAX);
            out_v1->port_id = (can_id >> 14U) & CANARD_SERVICE_ID_MAX;
            const bool req  = (can_id & (UINT32_C(1) << 24U)) != 0U;
            out_v1->kind    = req ? canard_kind_1v0_request : canard_kind_1v0_response;
            is_v1           = is_v1 && !bit_23 && (out_v1->src != out_v1->dst); // self-addressing not allowed
        } else {
            out_v1->dst       = CANARD_NODE_ID_ANONYMOUS;
            const bool is_1v1 = (can_id & (UINT32_C(1) << 7U)) != 0U;
            if (is_1v1) {
                out_v1->port_id = (can_id >> 8U) & CANARD_SUBJECT_ID_MAX;
                out_v1->kind    = canard_kind_1v1_message;
            } else {
                is_v1           = is_v1 && !bit_23;
                out_v1->port_id = (can_id >> 8U) & CANARD_SUBJECT_ID_MAX_1v0;
                out_v1->kind    = canard_kind_1v0_message;
                if ((can_id & (UINT32_C(1) << 24U)) != 0U) {
                    out_v1->src = CANARD_NODE_ID_ANONYMOUS;
                    is_v1       = is_v1 && start && end; // anonymous can only be single-frame
                }
            }
        }
    }
    if (is_v0) {
        out_v0->priority    = priority;
        out_v0->src         = src;
        out_v0->transfer_id = transfer_id;
        out_v0->start       = start;
        out_v0->end         = end;
        out_v0->toggle      = toggle;
        out_v0->payload     = payload;
        const bool svc      = (can_id & (UINT32_C(1) << 7U)) != 0U;
        if (svc) {
            const byte_t dst = (byte_t)((can_id >> 8U) & CANARD_NODE_ID_MAX);
            out_v0->dst      = dst;
            out_v0->port_id  = (can_id >> 16U) & 0xFFU;
            const bool req   = (can_id & (UINT32_C(1) << 15U)) != 0U;
            out_v0->kind     = req ? canard_kind_0v1_request : canard_kind_0v1_response;
            // Node-ID 0 reserved for anonymous/broadcast, invalid for services. Self-addressing not allowed.
            is_v0 = is_v0 && (dst != 0) && (src != 0) && (src != dst);
        } else {
            out_v0->dst     = CANARD_NODE_ID_ANONYMOUS;
            out_v0->port_id = (can_id >> 8U) & 0xFFFFU;
            out_v0->kind    = canard_kind_0v1_message;
            if (src == 0) {
                out_v0->src = CANARD_NODE_ID_ANONYMOUS;
                is_v0       = is_v0 && start && end; // anonymous can only be single-frame
            }
        }
    }
    return (is_v0 ? 1U : 0U) | (is_v1 ? 2U : 0U);
}

// Idle sessions are removed after this timeout even if reassembly is not finished.
// This is not related to the transfer-ID timeout and does not affect the correctness;
// it is only needed to improve the memory footprint when remotes cease sending messages.
// This could be made configurable but it is not a tuning-sensitive parameter.
#define RX_SESSION_TIMEOUT (30 * MEGA)

// Reassembly state at a specific priority level.
// Maintaining separate state per priority level allows preemption of higher-priority transfers without loss.
// Interface affinity is required because frames duplicated across redundant interfaces may arrive with a significant
// delay, which may cause the receiver to accept more frames than necessary.
typedef struct
{
    canard_us_t start_ts;
    uint32_t    total_size; // The raw payload size seen before the implicit truncation and CRC removal.
    uint16_t    crc;
    byte_t      transfer_id : CANARD_TRANSFER_ID_BITS;
    byte_t      iface_index : IFACE_INDEX_BITS;
    byte_t      expected_toggle : 1;
    byte_t      payload[]; // Extent-sized.
} rx_slot_t;
#define RX_SLOT_OVERHEAD (offsetof(rx_slot_t, payload))
static_assert(RX_SLOT_OVERHEAD <= 16, "unexpected layout");

static rx_slot_t* rx_slot_new(const canard_subscription_t* const sub,
                              const canard_us_t                  start_ts,
                              const byte_t                       transfer_id,
                              const byte_t                       iface_index)
{
    rx_slot_t* const slot = mem_alloc(sub->owner->mem.rx_payload, RX_SLOT_OVERHEAD + sub->extent);
    if (slot != NULL) {
        memset(slot, 0, RX_SLOT_OVERHEAD);
        slot->start_ts        = start_ts;
        slot->crc             = sub->crc_seed;
        slot->transfer_id     = transfer_id & CANARD_TRANSFER_ID_MAX;
        slot->expected_toggle = canard_kind_version(sub->kind) & 1U;
        slot->iface_index     = iface_index & ((1U << IFACE_INDEX_BITS) - 1U);
    }
    return slot;
}

static void rx_slot_destroy(const canard_subscription_t* const sub, rx_slot_t* const slot)
{
    mem_free(sub->owner->mem.rx_payload, RX_SLOT_OVERHEAD + sub->extent, slot);
}

static void rx_slot_advance(rx_slot_t* const slot, const size_t extent, const canard_bytes_t payload)
{
    if (slot->total_size < extent) {
        const size_t copy_size = smaller(payload.size, (size_t)(extent - slot->total_size));
        (void)memcpy(&slot->payload[slot->total_size], payload.data, copy_size);
    }
    slot->total_size = (uint32_t)(slot->total_size + payload.size); // Before truncation.
    slot->expected_toggle ^= 1U;
}

// Up to libcanard v4 we used a fixed-capacity array of pointers for per-remote sessions for constant-time lookup,
// but it was too costly on MCUs: with a 32-bit pointer it took 512 bytes for the array plus overheads,
// resulting in 1 KiB o1heap blocks per session, very expensive. Here we use a much less RAM-heavy approach with
// sparse nodes in a tree with log-time lookup.
//
// Design goals:
//
//  - Admit frames from a single interface only, arbitrarily chosen, until it has been observed to be silent for
//    at least one transfer-ID timeout period. This is because redundant interfaces may carry frames with a significant
//    delay, which may cause a receiver to admit the same transfer multiple times without interface affinity.
//
//  - Allow preemption of transfers by higher-priority ones, without loss of the preempted transfer's state.
//
//  - The case of a zero transfer-ID timeout is a first-class use case. In this mode, duplication is tolerated by
//    the application, but multi-frame transfers must still follow interface affinity to avoid incorrect reassembly.
//
// Assumptions:
//  - Frames within a transfer arrive in order;
//    see https://forum.opencyphal.org/t/uavcan-can-tx-buffer-management-in-can-fd-controllers/1215
//  - A frame may be duplicated (a well-known CAN PHY edge case), but duplicates immediately follow the original.
//
// Core invariants:
//  - Only start-of-transfer may create/replace a slot.
//  - Non-start frames never create state.
//  - Session dedup state is updated on admitted start-of-transfer, not on transfer completion.
//  - Timeout is consulted only for start-of-transfer admission.
//  - Slot matching for continuation uses exact match: priority, transfer-ID/seqno, toggle, and iface.
typedef struct
{
    canard_tree_t          index;
    canard_listed_t        list_animation; // On update, session moved to the tail; oldest pushed to the head.
    canard_us_t            last_admission_ts;
    rx_slot_t*             slots[CANARD_PRIO_COUNT]; // Indexed by priority level to allow preemption.
    canard_subscription_t* owner;
    byte_t                 node_id;
    byte_t                 last_admitted_transfer_id;
    byte_t                 last_admitted_priority;
    byte_t                 iface_index;
} rx_session_t;
static_assert((sizeof(void*) > 4) || (sizeof(rx_session_t) <= 120), "too large");

static int32_t rx_session_cavl_compare(const void* const user, const canard_tree_t* const node)
{
    return ((int32_t)(*(byte_t*)user)) - ((int32_t)CAVL2_TO_OWNER(node, rx_session_t, index)->node_id);
}

typedef struct
{
    canard_subscription_t* owner;
    byte_t                 iface_index; // Start with the affinity to the iface that delivered the first frame.
    byte_t                 node_id;
} rx_session_factory_context_t;

static canard_tree_t* rx_session_factory(void* const user)
{
    const rx_session_factory_context_t* const ctx = (rx_session_factory_context_t*)user;
    rx_session_t* const ses = mem_alloc_zero(ctx->owner->owner->mem.rx_session, sizeof(rx_session_t));
    if (ses == NULL) {
        return NULL;
    }
    FOREACH_PRIO (i) {
        ses->slots[i] = NULL;
    }
    ses->last_admission_ts = BIG_BANG;
    ses->owner             = ctx->owner;
    ses->iface_index       = ctx->iface_index;
    ses->node_id           = ctx->node_id;
    enlist_tail(&ctx->owner->owner->rx.list_session_by_animation, &ses->list_animation);
    return &ses->index;
}

static void rx_session_destroy(rx_session_t* const ses)
{
    canard_subscription_t* const sub = ses->owner;
    FOREACH_PRIO (i) {
        rx_slot_destroy(sub, ses->slots[i]);
    }
    CANARD_ASSERT(cavl2_is_inserted(sub->sessions, &ses->index));
    cavl2_remove(&sub->sessions, &ses->index);
    CANARD_ASSERT(is_listed(&sub->owner->rx.list_session_by_animation, &ses->list_animation));
    delist(&sub->owner->rx.list_session_by_animation, &ses->list_animation);
    mem_free(sub->owner->mem.rx_session, sizeof(rx_session_t), ses);
}

// Checks the state and purges stale slots to reclaim memory early. Returns the number of in-progress slots remaining.
static size_t rx_session_cleanup(rx_session_t* const ses, const canard_us_t now)
{
    const canard_us_t deadline = now - later(RX_SESSION_TIMEOUT, ses->owner->transfer_id_timeout);
    size_t            n_slots  = 0;
    FOREACH_PRIO (i) {
        const rx_slot_t* const slot = ses->slots[i];
        if (slot == NULL) {
            continue;
        }
        CANARD_ASSERT((0 <= slot->start_ts) && (slot->start_ts <= ses->last_admission_ts));
        if (slot->start_ts < deadline) { // Too old, destroy even if in progress -- unlikely to complete anyway.
            rx_slot_destroy(ses->owner, ses->slots[i]);
            ses->slots[i] = NULL;
        } else {
            n_slots++;
        }
    }
    return n_slots;
}

static void rx_session_complete(rx_session_t* const ses, const canard_us_t ts_frame, const frame_t* const fr)
{
    canard_subscription_t* const sub = ses->owner;
    CANARD_ASSERT((fr->end) && (fr->port_id == sub->port_id) && (fr->kind == sub->kind));
    CANARD_ASSERT(sub->vtable->on_message != NULL);
    rx_slot_t* const slot = ses->slots[fr->priority];
    if (slot == NULL) {
        CANARD_ASSERT(fr->start && fr->end); // Only single-frame can complete without a slot.
        const canard_payload_t payload = { .view = fr->payload, .origin = { .data = NULL, .size = 0 } };
        sub->vtable->on_message(sub, ts_frame, fr->priority, fr->src, fr->transfer_id, payload);
    } else {
        CANARD_ASSERT(!fr->start && fr->end);
        CANARD_ASSERT((slot->transfer_id == fr->transfer_id) && (slot->iface_index == ses->iface_index));
        ses->slots[fr->priority] = NULL; // Slot memory ownership transferred to the application, or destroyed.
        const bool     v1        = canard_kind_version(sub->kind) == 1;
        const uint16_t crc_ref = v1 ? CRC_RESIDUE : (uint16_t)(slot->payload[0] | (((unsigned)slot->payload[1]) << 8U));
        CANARD_ASSERT(v1 || (sub->extent >= 2)); // In v0, the CRC size is included in the extent.
        if (slot->crc == crc_ref) {
            const size_t           size    = smaller(slot->total_size - 2, sub->extent - (v1 ? 0 : 2));
            const canard_payload_t payload = {
                .view   = { .data = v1 ? slot->payload : &slot->payload[2], .size = size },
                .origin = { .data = slot, .size = RX_SLOT_OVERHEAD + sub->extent },
            };
            sub->vtable->on_message(sub, slot->start_ts, fr->priority, fr->src, fr->transfer_id, payload);
        } else {
            sub->owner->err.rx_transfer++;
            rx_slot_destroy(ses->owner, slot);
        }
    }
}

static void rx_session_accept(rx_session_t* const ses, const canard_us_t ts_frame, const frame_t* const fr)
{
    const canard_subscription_t* const sub = ses->owner;
    CANARD_ASSERT((fr->port_id == sub->port_id) && (fr->kind == sub->kind));
    rx_slot_t* const slot = ses->slots[fr->priority];
    if (slot != NULL) {
        CANARD_ASSERT((!fr->start || !fr->end) && (slot->expected_toggle == fr->toggle));
        CANARD_ASSERT((slot->transfer_id == fr->transfer_id) && (slot->iface_index == ses->iface_index));
        rx_slot_advance(slot, sub->extent, fr->payload);
        // Multi-frame transfers place CRC differently in v1 and v0.
        // The v1 handling is trivial: simply compute the full payload CRC and ensure the residue is correct.
        // The payload may be truncated to the subscription extent, but the CRC is computed over the full payload.
        // Legacy v0 is messy because the CRC is in the beginning, which we need to handle specially.
        // The CRC initial state is constant for v1, data-type-dependent for v0; this is managed outside of this scope.
        const canard_bytes_t crc_input =
          ((canard_kind_version(sub->kind) == 0) && fr->start)
            ? (canard_bytes_t){ .size = fr->payload.size - 2, .data = ((byte_t*)fr->payload.data) + 2 }
            : fr->payload;
        slot->crc = crc_add(slot->crc, crc_input.size, crc_input.data);
    }
    if (fr->end) {
        rx_session_complete(ses, ts_frame, fr);
    }
}

static void rx_session_record_admission(rx_session_t* const ses,
                                        const canard_prio_t priority,
                                        const byte_t        transfer_id,
                                        const canard_us_t   ts,
                                        const byte_t        iface_index)
{
    ses->last_admission_ts         = ts;
    ses->last_admitted_transfer_id = transfer_id & CANARD_TRANSFER_ID_MAX;
    ses->last_admitted_priority    = ((byte_t)priority) & ((1U << CANARD_PRIO_BITS) - 1U);
    ses->iface_index               = iface_index & ((1U << IFACE_INDEX_BITS) - 1U);
}

// Frame admittance solver. A complex piece, redesigned after v4 to support priority preemption & parallel reassembly.
static bool rx_session_solve_admission(const rx_session_t* const ses,
                                       const canard_us_t         ts,
                                       const canard_prio_t       priority,
                                       const bool                start,
                                       const bool                toggle,
                                       const byte_t              transfer_id,
                                       const byte_t              iface_index)
{
    // Continuation frames cannot create new state so their handling is simpler.
    // They are only accepted if there is a slot with an exact match of all transfer parameters.
    // We ignore the transfer-ID timeout to avoid breaking transfers that are preempted for a long time,
    // and especially to allow reassembly of multi-frame transfers even when the transfer-ID timeout is zero.
    if (!start) {
        const rx_slot_t* const slot = ses->slots[priority];
        return (slot != NULL) && (slot->transfer_id == transfer_id) && (slot->iface_index == iface_index) &&
               (slot->expected_toggle == toggle);
    }
    // Duplicate start frames do not require special treatment because a duplicate frame can only follow the original
    // without any frames belonging to the same transfer in between (see the assumptions). If we get a duplicate start,
    // with a nonzero TID timeout it will be rejected as not-new. Zero transfer-ID timeout means the application
    // accepts duplicates; with zero timeout duplicate rejection is not possible given this protocol design.
    //
    // The original design had a special case that enabled admittance of a transfer with the next transfer-ID from a
    // different interface if there is no pending reassembly in progress; see details here and the original v4 code:
    // https://github.com/OpenCyphal/libcanard/issues/228. This behavior is no longer present here mostly because we
    // now update the session state only on admittance and not upon completion of a transfer, which changes the logic
    // considerably. One side effect is that even after a timeout (potentially a very long time as long as the session
    // survives) we will still reject a new transfer arriving from a different interface if it happened to roll the
    // same transfer-ID. This is not an issue because we would still accept new transfers on the same iface,
    // and after the RX_SESSION_TIMEOUT the session is destroyed and all new transfers will be accepted unconditionally.
    //
    // Merely comparing against the last admitted transfer-ID is not enough because there is a preemption-related edge
    // case. Suppose the transfer-ID modulo is 4, i.e., the values are {0, 1, 2, 3}; a low-priority transfer is sent
    // but preempted by higher-priority peers before it hits the bus as illustrated below.
    //
    //             |SENDER                |
    //  P=low      |0                     |
    //  P=high     |    1   2   3   0     | <-- preempt the low-priority frame
    //
    //             |RECEIVER              |
    //  P=low      |                    0 | <-- arrived late, rejected as duplicate!
    //  P=high     |    1   2   3   0     |
    //
    // One solution is to track the last admitted priority along with the transfer-ID:
    // preemption alters the admitted priority, which prevents false rejection.
    //
    // There is one critical edge case: if a low-priority frame is duplicated (due to the CAN ACK glitch effect)
    // AND at least one higher-priority frame is admitted between the original frame and its duplicate,
    // then the duplicate will be accepted as a new transfer.
    // Example: low-prio tid=1, high-prio preemption tid=2, high-prio tid=2...X, low-prio duplicate tid=1.
    // In general, the problem of duplicate detection under these conditions is believed to be undecidable.
    // We inherit the CAN PHY design limitation here by accepting the risk of spurious duplication, recognizing that
    // it is very low, as it requires both unlikely events to occur simultaneously: CAN ACK glitch and a high-priority
    // preemption exactly between the original transmission and its duplicate.
    const bool fresh = (transfer_id != ses->last_admitted_transfer_id) || // always accept if transfer-ID is different
                       (priority != ses->last_admitted_priority);         // or we switched the priority level
    const bool affine = ses->iface_index == iface_index;
    const bool stale  = (ts - ses->owner->transfer_id_timeout) > ses->last_admission_ts;
    return (fresh && affine) || (affine && stale) || (stale && fresh);
}

// The caller must ensure the frame is of the correct version that matches the subscription (v0/v1).
// The caller must ensure the frame is directed to the local node (broadcast or unicast to the local node-ID).
static void rx_session_update(canard_subscription_t* const sub,
                              const canard_us_t            ts,
                              const frame_t* const         frame,
                              const byte_t                 iface_index)
{
    CANARD_ASSERT((sub != NULL) && (frame != NULL) && (frame->payload.data != NULL) && (ts >= 0));
    CANARD_ASSERT(frame->end || (frame->payload.size >= 7));
    CANARD_ASSERT(!frame->start || (frame->toggle == canard_kind_version(sub->kind)));
    CANARD_ASSERT((frame->dst == CANARD_NODE_ID_ANONYMOUS) || (frame->dst == sub->owner->node_id));

    // Only start frames may create new states.
    // The protocol version is observable on start frames by design, which makes this robust.
    // At this point we also ensured the frame is not misaddressed.
    rx_session_factory_context_t factory_context = { .owner = sub, .iface_index = iface_index, .node_id = frame->src };
    rx_session_t* const          ses =
      CAVL2_TO_OWNER(frame->start ? cavl2_find_or_insert(&sub->sessions, //
                                                         &frame->src,
                                                         rx_session_cavl_compare,
                                                         &factory_context,
                                                         rx_session_factory)
                                  : cavl2_find(sub->sessions, &frame->src, rx_session_cavl_compare),
                     rx_session_t,
                     index);
    if (ses == NULL) {
        sub->owner->err.oom += frame->start;
        return;
    }

    // Decide admit or drop.
    const bool admit = rx_session_solve_admission(
      ses, ts, frame->priority, frame->start, frame->toggle, frame->transfer_id, iface_index);
    if (!admit) {
        return;
    }

    // The frame must be accepted. If this is the start of a new transfer, we must update state.
    if (frame->start) {
        // Animate only when a new transfer is started to manage load. Correctness-wise there is not much difference.
        enlist_tail(&sub->owner->rx.list_session_by_animation, &ses->list_animation);
        // Destroy the old slot if it exists, meaning we're discarding stale transfer.
        if (ses->slots[frame->priority] != NULL) {
            rx_slot_destroy(sub, ses->slots[frame->priority]);
            ses->slots[frame->priority] = NULL;
        }
        // If there are more frames to follow, we must store in-progress state for reassembly.
        if (!frame->end) {
            (void)rx_session_cleanup(ses, ts); // Cleanup before allocating a new slot; don't do too often, is costly.
            ses->slots[frame->priority] = rx_slot_new(sub, ts, frame->transfer_id, iface_index);
            if (ses->slots[frame->priority] == NULL) {
                sub->owner->err.oom++;
                return;
            }
            CANARD_ASSERT(ses->slots[frame->priority]->transfer_id == frame->transfer_id);
            CANARD_ASSERT(ses->slots[frame->priority]->expected_toggle == frame->toggle);
        }
        // Register the new state only after we have a confirmation that we have memory to store the frame.
        rx_session_record_admission(ses, frame->priority, frame->transfer_id, ts, iface_index);
    }

    // Accept the frame.
    rx_session_accept(ses, ts, frame);
    CANARD_ASSERT(!frame->end || (ses->slots[frame->priority] == NULL));
}

static int32_t rx_subscription_cavl_compare(const void* const user, const canard_tree_t* const node)
{
    return ((int32_t)(*(uint32_t*)user)) - ((int32_t)((canard_subscription_t*)node)->port_id);
}

// Locates the appropriate subscription if the destination is matching and there is a subscription.
static canard_subscription_t* rx_route(const canard_t* const self, const frame_t* const fr)
{
    CANARD_ASSERT((self != NULL) && (fr != NULL));
    if ((fr->dst != CANARD_NODE_ID_ANONYMOUS) && (fr->dst != self->node_id)) {
        return NULL; // misfiltered
    }
    return (canard_subscription_t*)cavl2_find(
      self->rx.subscriptions[fr->kind], &fr->port_id, rx_subscription_cavl_compare);
}

// Recompute the filter configuration and apply.
// Must be invoked after modification of the subscription set and after the local node-ID is changed.
static void rx_filter_configure(const canard_t* const self)
{
    if (self->vtable->filter == NULL) {
        return; // No filtering support, nothing to do.
    }
    (void)self;
    // TODO not implemented.
}

// ---------------------------------------------           MISC            ---------------------------------------------

// The splitmix64 PRNG algorithm. Original work by Sebastiano Vigna, released under CC0-1.0 (public domain dedication).
// Source http://xoshiro.di.unimi.it/splitmix64.c.
static uint64_t splitmix64(uint64_t* const state)
{
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z          = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z          = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
}

// Obtains a decent-quality pseudo-random number in [0, cardinality).
static uint64_t random(canard_t* const self, const uint64_t cardinality)
{
    CANARD_ASSERT(cardinality > 0);
    return splitmix64(&self->prng_state) % cardinality;
}

static void node_id_occupancy_reset(canard_t* const self)
{
    self->node_id_occupancy_bitmap[0] = 1; // Reserve 0 for compatibility with v0
    self->node_id_occupancy_bitmap[1] = 0;
}

// Records the seen node-ID and reallocates the local node if a collision is found.
static void node_id_occupancy_update(canard_t* const self, const byte_t src, const byte_t dst)
{
    (void)self;
    (void)src;
    (void)dst;
    // TODO not implemented.
    // TODO filtering.
    rx_filter_configure(self);
}

bool canard_new(canard_t* const              self,
                const canard_vtable_t* const vtable,
                const canard_mem_set_t       memory,
                const size_t                 tx_queue_capacity,
                const uint64_t               prng_seed,
                const size_t                 filter_count,
                canard_filter_t* const       filter_storage)
{
    const bool ok = (self != NULL) && (vtable != NULL) && (vtable->now != NULL) && (vtable->tx != NULL) &&
                    mem_valid(memory.tx_transfer) && mem_valid(memory.tx_frame) && mem_valid(memory.rx_session) &&
                    mem_valid(memory.rx_payload) && ((filter_count == 0U) || (filter_storage != NULL));
    if (ok) {
        (void)memset(self, 0, sizeof(*self));
        self->tx.fd                     = true;
        self->tx.queue_capacity         = tx_queue_capacity;
        self->rx.filter_count           = filter_count;
        self->rx.filters                = filter_storage;
        self->mem                       = memory;
        self->prng_state                = prng_seed ^ (uintptr_t)self;
        self->vtable                    = vtable;
        self->unicast_sub.index_port_id = TREE_NULL;
        self->node_id                   = (byte_t)(random(self, CANARD_NODE_ID_MAX) + 1U); // [1, 127]
        node_id_occupancy_reset(self);
    }
    return ok;
}

void canard_destroy(canard_t* const self)
{
    CANARD_ASSERT(self != NULL);
    // The application MUST destroy all subscriptions before destroying the instance.
    for (size_t i = 0; i < (sizeof(self->rx.subscriptions) / sizeof(self->rx.subscriptions[0])); i++) {
        CANARD_ASSERT(self->rx.subscriptions[i] == NULL);
    }
    CANARD_ASSERT(self->rx.list_session_by_animation.head == NULL);
    CANARD_ASSERT(self->rx.list_session_by_animation.tail == NULL);
    while (self->tx.agewise.head != NULL) {
        tx_transfer_t* const tr = LIST_HEAD(self->tx.agewise, tx_transfer_t, list_agewise);
        tx_retire(self, tr);
    }
    (void)memset(self, 0, sizeof(*self)); // UAF safety
}

bool canard_set_node_id(canard_t* const self, const uint_least8_t node_id)
{
    const bool ok = (self != NULL) && (node_id <= CANARD_NODE_ID_MAX);
    if (ok && (node_id != self->node_id)) {
        self->node_id = node_id;
        node_id_occupancy_reset(self);
        rx_filter_configure(self);
    }
    return ok;
}

void canard_poll(canard_t* const self, const uint_least8_t tx_ready_iface_bitmap)
{
    if (self != NULL) {
        const canard_us_t now = self->vtable->now(self);
        // Drop stale sessions to reclaim memory. This happens when remote peers cease sending data.
        // The oldest is held alive until its session timeout has expired, but notice that it may be different
        // depending on the subscription instance if large transfer-ID values are used.
        // This means that a stale session that belongs to a subscription with a long timeout may keep other sessions
        // with a shorter timeout alive beyond their expiration time.
        // We accept this because it does not affect correctness (the transfer-ID timeout is checked on reception
        // always); the only downside is that memory reclamation time is bounded in the worst case by the longest
        // transfer-ID timeout among all subscriptions, but this is a reasonable tradeoff for the reduced complexity.
        rx_session_t* const ses = LIST_HEAD(self->rx.list_session_by_animation, rx_session_t, list_animation);
        if (ses != NULL) {
            const size_t in_progress_slots = rx_session_cleanup(ses, now);
            if ((in_progress_slots == 0) && (ses->last_admission_ts < (now - ses->owner->transfer_id_timeout))) {
                rx_session_destroy(ses);
            }
        }
        // Process the TX pipeline.
        tx_expire(self, now); // deadline maintenance first to keep queue pressure bounded
        FOREACH_IFACE (i) {   // submit queued frames through all currently writable interfaces
            if ((tx_ready_iface_bitmap & (1U << i)) != 0U) {
                tx_eject_pending(self, (byte_t)i);
            }
        }
    }
}

static void ingest_frame(canard_t* const     self,
                         const canard_us_t   timestamp,
                         const uint_least8_t iface_index,
                         const frame_t       frame)
{
    // Update the node-ID occupancy/collision monitoring states before routing the message.
    // We do this only on start frames because non-start frames have the version detection ambiguity,
    // which is not a problem for the routing logic (new states can only be created on start frames),
    // but it may cause phantom occupancy detection. Also, doing it only on start frames reduces the load.
    if (frame.start) {
        node_id_occupancy_update(self, frame.src, frame.dst);
    }
    // Route the frame to the appropriate destination internally.
    canard_subscription_t* const sub = rx_route(self, &frame);
    if (sub != NULL) {
        rx_session_update(sub, timestamp, &frame, iface_index);
    }
}

bool canard_ingest_frame(canard_t* const      self,
                         const canard_us_t    timestamp,
                         const uint_least8_t  iface_index,
                         const uint32_t       extended_can_id,
                         const canard_bytes_t can_data)
{
    const bool ok = (self != NULL) && (timestamp >= 0) && (iface_index < CANARD_IFACE_COUNT) &&
                    (extended_can_id < (UINT32_C(1) << 29U)) && ((can_data.size == 0) || (can_data.data != NULL));
    if (ok) {
        frame_t      frs[2] = { { 0 }, { 0 } };
        const byte_t parsed = rx_parse(extended_can_id, can_data, &frs[0], &frs[1]);
        if (parsed == 0) {
            self->err.rx_frame++;
        }
        if ((parsed & 1U) != 0) {
            CANARD_ASSERT(canard_kind_version(frs[0].kind) == 0);
            ingest_frame(self, timestamp, iface_index, frs[0]);
        }
        if ((parsed & 2U) != 0) {
            CANARD_ASSERT(canard_kind_version(frs[1].kind) == 1);
            ingest_frame(self, timestamp, iface_index, frs[1]);
        }
    }
    return ok;
}

uint16_t canard_0v1_crc_seed_from_data_type_signature(const uint64_t data_type_signature)
{
    uint16_t crc = CRC_INITIAL;
    uint64_t sig = data_type_signature;
    for (size_t i = 0; i < 8U; i++) {
        crc = crc_add_byte(crc, (byte_t)(sig & BYTE_MAX));
        sig >>= 8U;
    }
    return crc;
}
