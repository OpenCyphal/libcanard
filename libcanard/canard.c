// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#include "canard.h"
#include <assert.h>
#include <string.h>

// ---------------------------------------- BUILD CONFIGURATION ----------------------------------------

/// By default, this macro resolves to the standard assert(). The user can redefine this if necessary.
/// To disable assertion checks completely, make it expand into `(void)(0)`.
#ifndef CANARD_ASSERT
// Intentional violation of MISRA: assertion macro cannot be replaced with a function definition.
#    define CANARD_ASSERT(x) assert(x)  // NOSONAR
#endif

/// This macro is needed only for testing and for library development. Do not redefine this in production.
#if defined(CANARD_EXPOSE_INTERNALS) && CANARD_EXPOSE_INTERNALS
#    define CANARD_INTERNAL
#else
#    define CANARD_INTERNAL static inline
#endif

#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 199901L)
#    error "Unsupported language: ISO C99 or a newer version is required."
#endif

#if __STDC_VERSION__ < 201112L
// Intentional violation of MISRA: static assertion macro cannot be replaced with a function definition.
#    define static_assert(x, ...) typedef char _static_assert_gl(_static_assertion_, __LINE__)[(x) ? 1 : -1]  // NOSONAR
#    define _static_assert_gl(a, b) _static_assert_gl_impl(a, b)                                              // NOSONAR
// Intentional violation of MISRA: the paste operator ## cannot be avoided in this context.
#    define _static_assert_gl_impl(a, b) a##b  // NOSONAR
#endif

// ---------------------------------------- COMMON CONSTANTS ----------------------------------------

#define TAIL_START_OF_TRANSFER 128U
#define TAIL_END_OF_TRANSFER 64U
#define TAIL_TOGGLE 32U

#define CAN_EXT_ID_MASK ((UINT32_C(1) << 29U) - 1U)

#define BITS_PER_BYTE 8U
#define BYTE_MAX 0xFFU

#define PADDING_BYTE 0U

// ---------------------------------------- TRANSFER CRC ----------------------------------------

#define CRC_INITIAL 0xFFFFU
#define CRC_SIZE_BYTES 2U

CANARD_INTERNAL uint16_t crcAddByte(const uint16_t crc, const uint8_t byte);
CANARD_INTERNAL uint16_t crcAddByte(const uint16_t crc, const uint8_t byte)
{
    uint16_t out = crc ^ (uint16_t)((uint16_t)(byte) << BITS_PER_BYTE);
    for (uint8_t i = 0; i < BITS_PER_BYTE; i++)  // Should we use a table instead? Adds 512 bytes of ROM.
    {
        // The no-lint statements suppress the warnings about magic numbers. These numbers are not magic.
        out = ((out & 0x8000U) != 0U) ? ((uint16_t)(out << 1U) ^ 0x1021U) : (uint16_t)(out << 1U);  // NOLINT
    }
    return out;
}

CANARD_INTERNAL uint16_t crcAdd(const uint16_t crc, const size_t size, const void* const data);
CANARD_INTERNAL uint16_t crcAdd(const uint16_t crc, const size_t size, const void* const data)
{
    uint16_t       out = crc;
    const uint8_t* p   = (const uint8_t*) data;
    for (size_t i = 0; i < size; i++)
    {
        out = crcAddByte(out, *p);
        ++p;
    }
    return out;
}

// ---------------------------------------- SESSION SPECIFIER ----------------------------------------

#define OFFSET_PRIORITY 26U
#define OFFSET_SUBJECT_ID 8U
#define OFFSET_SERVICE_ID 14U
#define OFFSET_DST_NODE_ID 7U

#define SERVICE_NOT_MESSAGE (UINT32_C(1) << 25U)
#define ANONYMOUS_MESSAGE (UINT32_C(1) << 24U)
#define REQUEST_NOT_RESPONSE (UINT32_C(1) << 24U)

CANARD_INTERNAL uint32_t makeMessageSessionSpecifier(const uint16_t subject_id, const uint8_t src_node_id);
CANARD_INTERNAL uint32_t makeMessageSessionSpecifier(const uint16_t subject_id, const uint8_t src_node_id)
{
    return ((uint32_t)(src_node_id & CANARD_NODE_ID_MAX)) |
           ((uint32_t)(subject_id & CANARD_SUBJECT_ID_MAX) << OFFSET_SUBJECT_ID);
}

CANARD_INTERNAL uint32_t makeServiceSessionSpecifier(const uint16_t service_id,
                                                     const bool     request_not_response,
                                                     const uint8_t  src_node_id,
                                                     const uint8_t  dst_node_id);
CANARD_INTERNAL uint32_t makeServiceSessionSpecifier(const uint16_t service_id,
                                                     const bool     request_not_response,
                                                     const uint8_t  src_node_id,
                                                     const uint8_t  dst_node_id)
{
    return ((uint32_t)(src_node_id & CANARD_NODE_ID_MAX)) |
           ((uint32_t)(dst_node_id & CANARD_NODE_ID_MAX) << OFFSET_DST_NODE_ID) |
           ((uint32_t)(service_id & CANARD_SERVICE_ID_MAX) << OFFSET_SERVICE_ID) |
           (request_not_response ? REQUEST_NOT_RESPONSE : 0U) | SERVICE_NOT_MESSAGE;
}

// ---------------------------------------- TRANSMISSION ----------------------------------------

/// The fields are ordered to minimize padding on all platforms.
typedef struct CanardInternalTxQueueItem
{
    struct CanardInternalTxQueueItem* next;

    uint32_t id;
    uint64_t deadline_usec;
    uint8_t  payload_size;

    // Intentional violation of MISRA: this flex array is the lesser of three evils. The other two are:
    //  - Use pointer, make it point to the remainder of the allocated memory following this structure.
    //    The pointer is bad because it requires us to use pointer arithmetics and adds sizeof(void*) of waste per item.
    //  - Use a separate memory allocation for data. This is terribly wasteful.
    uint8_t payload[];  // NOSONAR
} CanardInternalTxQueueItem;

CANARD_INTERNAL uint8_t getPresentationLayerMTU(const CanardInstance* const ins);
CANARD_INTERNAL uint8_t getPresentationLayerMTU(const CanardInstance* const ins)
{
    uint8_t out = 0U;
    if (ins->mtu_bytes < CANARD_MTU_MIN)
    {
        out = CANARD_MTU_MIN - 1U;
    }
    else if (ins->mtu_bytes > CANARD_MTU_MAX)
    {
        out = CANARD_MTU_MAX - 1U;
    }
    else
    {
        out = ins->mtu_bytes - 1U;
    }
    return out;
}

/// Returns a value above CAN_EXT_ID_MASK to indicate failure.
CANARD_INTERNAL uint32_t makeCANID(const CanardTransfer* const transfer,
                                   const uint8_t               local_node_id,
                                   const uint8_t               presentation_layer_mtu);
CANARD_INTERNAL uint32_t makeCANID(const CanardTransfer* const transfer,
                                   const uint8_t               local_node_id,
                                   const uint8_t               presentation_layer_mtu)
{
    uint32_t out = UINT32_MAX;
    if (transfer->transfer_kind == CanardTransferKindMessage)
    {
        if (local_node_id <= CANARD_NODE_ID_MAX)
        {
            out = makeMessageSessionSpecifier(transfer->port_id, local_node_id);
        }
        else if (transfer->payload_size <= presentation_layer_mtu)
        {
            const uint8_t c = (uint8_t) crcAdd(CRC_INITIAL, transfer->payload_size, transfer->payload);
            out             = makeMessageSessionSpecifier(transfer->port_id, c) | ANONYMOUS_MESSAGE;
        }
        else
        {
            CANARD_ASSERT(false);  // Anonymous multi-frame message transfers are not allowed.
        }
    }
    else if ((transfer->transfer_kind == CanardTransferKindRequest) ||
             (transfer->transfer_kind == CanardTransferKindResponse))
    {
        if (local_node_id <= CANARD_NODE_ID_MAX)
        {
            out = makeServiceSessionSpecifier(transfer->port_id,
                                              transfer->transfer_kind == CanardTransferKindRequest,
                                              local_node_id,
                                              transfer->remote_node_id);
        }
        else
        {
            CANARD_ASSERT(false);  // Anonymous service transfers are not allowed.
        }
    }
    else
    {
        CANARD_ASSERT(false);  // Invalid transfer kind.
    }

    if (out < UINT32_MAX)
    {
        out |= ((uint32_t) transfer->priority & CANARD_PRIORITY_MAX) << OFFSET_PRIORITY;
        CANARD_ASSERT(out <= CAN_EXT_ID_MASK);
    }
    return out;
}

CANARD_INTERNAL uint8_t makeTailByte(const bool    start_of_transfer,
                                     const bool    end_of_transfer,
                                     const bool    toggle,
                                     const uint8_t transfer_id);
CANARD_INTERNAL uint8_t makeTailByte(const bool    start_of_transfer,
                                     const bool    end_of_transfer,
                                     const bool    toggle,
                                     const uint8_t transfer_id)
{
    return (uint8_t)((start_of_transfer ? TAIL_START_OF_TRANSFER : 0U) | (end_of_transfer ? TAIL_END_OF_TRANSFER : 0U) |
                     (toggle ? TAIL_TOGGLE : 0U) | (transfer_id & CANARD_TRANSFER_ID_MAX));
}

CANARD_INTERNAL CanardInternalTxQueueItem* allocateTxQueueItem(CanardInstance* const ins,
                                                               const uint32_t        id,
                                                               const uint64_t        deadline_usec,
                                                               const uint8_t         payload_size);
CANARD_INTERNAL CanardInternalTxQueueItem* allocateTxQueueItem(CanardInstance* const ins,
                                                               const uint32_t        id,
                                                               const uint64_t        deadline_usec,
                                                               const uint8_t         payload_size)
{
    CANARD_ASSERT(ins != NULL);
    CANARD_ASSERT(id <= CAN_EXT_ID_MASK);
    CANARD_ASSERT(payload_size > 0U);  // UAVCAN/CAN doesn't allow zero-payload frames.
    CanardInternalTxQueueItem* const out =
        (CanardInternalTxQueueItem*) ins->heap_allocate(ins, sizeof(CanardInternalTxQueueItem) + payload_size);
    if (out != NULL)
    {
        out->next          = NULL;
        out->id            = id;
        out->deadline_usec = deadline_usec;
        out->payload_size  = payload_size;
    }
    return out;
}

/// Returns the element after which new elements with the specified CAN ID should be inserted.
/// Returns NULL if the element shall be inserted in the beginning of the list (i.e., no prior elements).
CANARD_INTERNAL CanardInternalTxQueueItem* findTxQueueSupremum(CanardInstance* const ins, const uint32_t can_id);
CANARD_INTERNAL CanardInternalTxQueueItem* findTxQueueSupremum(CanardInstance* const ins, const uint32_t can_id)
{
    CANARD_ASSERT(ins != NULL);
    CANARD_ASSERT(can_id <= CAN_EXT_ID_MASK);
    CanardInternalTxQueueItem* out = ins->_tx_queue;  // The linear search should be replaced with O(log n) at least.
    // TODO: INCOMPLETE.
    if ((out != NULL) && (out->id <= can_id))
    {
        while ((out != NULL) && (out->next != NULL) && (out->next->id <= can_id))
        {
            out = out->next;
        }
    }
    CANARD_ASSERT((out == NULL) || (out->id <= can_id));
    return out;
}

CANARD_INTERNAL void pushSingleFrameTransfer(CanardInstance* const ins,
                                             const uint64_t        deadline_usec,
                                             const uint32_t        can_id,
                                             const uint8_t         transfer_id,
                                             const size_t          payload_size,
                                             const void* const     payload);
CANARD_INTERNAL void pushSingleFrameTransfer(CanardInstance* const ins,
                                             const uint64_t        deadline_usec,
                                             const uint32_t        can_id,
                                             const uint8_t         transfer_id,
                                             const size_t          payload_size,
                                             const void* const     payload)
{
    CANARD_ASSERT(ins != NULL);
    CANARD_ASSERT(can_id <= CAN_EXT_ID_MASK);
    CANARD_ASSERT((payload_size == 0U) || (payload != NULL));
    CanardInternalTxQueueItem* const tqi =
        allocateTxQueueItem(ins, can_id, deadline_usec, (uint8_t)(payload_size + 1U));
    if (tqi != NULL)
    {
        (void) memmove(&tqi->payload[0], payload, payload_size);
        tqi->payload[payload_size]     = makeTailByte(true, true, true, transfer_id);
        CanardInternalTxQueueItem* sup = findTxQueueSupremum(ins, can_id);
        if (sup != NULL)
        {
            tqi->next = sup->next;
            sup->next = tqi;
        }
        else
        {
            tqi->next      = ins->_tx_queue;
            ins->_tx_queue = tqi;
        }
    }
}

CANARD_INTERNAL void pushMultiFrameTransfer(CanardInstance* const ins,
                                            const uint8_t         presentation_layer_mtu,
                                            const uint64_t        deadline_usec,
                                            const uint32_t        can_id,
                                            const uint8_t         transfer_id,
                                            const size_t          payload_size,
                                            const void* const     payload);
CANARD_INTERNAL void pushMultiFrameTransfer(CanardInstance* const ins,
                                            const uint8_t         presentation_layer_mtu,
                                            const uint64_t        deadline_usec,
                                            const uint32_t        can_id,
                                            const uint8_t         transfer_id,
                                            const size_t          payload_size,
                                            const void* const     payload)
{
    CANARD_ASSERT(ins != NULL);
    CANARD_ASSERT(presentation_layer_mtu > 1U);
    CANARD_ASSERT(can_id <= CAN_EXT_ID_MASK);
    CANARD_ASSERT(payload_size > presentation_layer_mtu);
    CANARD_ASSERT(payload != NULL);

    // TODO: QUEUE PRE-ALLOCATION.

    const size_t   payload_size_with_crc = payload_size + CRC_SIZE_BYTES;
    size_t         offset                = 0U;
    uint16_t       crc                   = crcAdd(CRC_INITIAL, payload_size, payload);
    bool           start_of_transfer     = true;
    bool           toggle                = true;
    const uint8_t* payload_ptr           = (const uint8_t*) payload;

    while (offset < payload_size_with_crc)
    {
        // Compute the required size of the frame payload for this frame, including CRC and padding.
        uint8_t frame_payload_size_with_tail = (uint8_t)(presentation_layer_mtu + 1U);
        {
            const size_t remaining_payload_with_crc = payload_size_with_crc - offset;
            if (remaining_payload_with_crc < presentation_layer_mtu)
            {
                const size_t index = remaining_payload_with_crc + 1U;
                CANARD_ASSERT(index < sizeof(CanardCANLengthToDLC));
                // Round up to accommodate padding.
                frame_payload_size_with_tail = CanardCANDLCToLength[CanardCANLengthToDLC[index]];
            }
        }

        // Allocate the storage using the above computed size.
        CanardInternalTxQueueItem* const tqi =
            allocateTxQueueItem(ins, can_id, deadline_usec, frame_payload_size_with_tail);
        if (tqi == NULL)
        {
            break;
        }

        // Copy the payload into the frame.
        const uint8_t frame_payload_size = (uint8_t)(frame_payload_size_with_tail - 1U);
        uint8_t       frame_offset       = 0U;
        while ((offset < payload_size) && (frame_offset < frame_payload_size))
        {
            tqi->payload[frame_offset] = *payload_ptr;
            ++frame_offset;
            ++offset;
            ++payload_ptr;
        }

        // Handle the last frame of the transfer: it is special because it also contains padding and CRC.
        const bool end_of_transfer = offset >= payload_size;
        if (end_of_transfer)
        {
            // Insert padding -- only in the last frame. Don't forget to include padding into the CRC.
            while ((frame_offset + CRC_SIZE_BYTES) < frame_payload_size)
            {
                tqi->payload[frame_offset] = PADDING_BYTE;
                ++frame_offset;
                crc = crcAddByte(crc, PADDING_BYTE);
            }

            // Insert the CRC.
            if ((frame_offset < frame_payload_size) && (offset == payload_size))
            {
                tqi->payload[frame_offset] = (uint8_t)(crc & BYTE_MAX);
                ++frame_offset;
            }
            if ((frame_offset < frame_payload_size) && (offset > payload_size))
            {
                tqi->payload[frame_offset] = (uint8_t)(crc >> BITS_PER_BYTE);
                ++frame_offset;
            }
        }

        // Finalize the frame.
        tqi->payload[frame_offset] = makeTailByte(start_of_transfer, end_of_transfer, toggle, transfer_id);
        start_of_transfer          = false;
        toggle                     = !toggle;
    }

    // TODO: INSERTION.
}

// ---------------------------------------- RECEPTION ----------------------------------------

/// The fields are ordered to minimize padding on all platforms.
typedef struct CanardInternalRxSession
{
    struct CanardInternalRxSession* next;

    size_t   payload_capacity;  ///< Payload past this limit may be discarded by the library.
    size_t   payload_size;      ///< How many bytes received so far.
    uint8_t* payload;

    uint64_t timestamp_usec;            ///< Time of last update of this session. Used for removal on timeout.
    uint32_t transfer_id_timeout_usec;  ///< When (current time - update timestamp) exceeds this, it's dead.

    const uint32_t session_specifier;  ///< Differentiates this session from other sessions.

    uint16_t calculated_crc;  ///< Updated with the received payload in real time.
    uint8_t  transfer_id;
    bool     next_toggle;
} CanardInternalRxSession;

// ---------------------------------------- PUBLIC API ----------------------------------------

const uint8_t CanardCANDLCToLength[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
const uint8_t CanardCANLengthToDLC[65] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,                               // 0-8
    9,  9,  9,  9,                                                   // 9-12
    10, 10, 10, 10,                                                  // 13-16
    11, 11, 11, 11,                                                  // 17-20
    12, 12, 12, 12,                                                  // 21-24
    13, 13, 13, 13, 13, 13, 13, 13,                                  // 25-32
    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,  // 33-48
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,  // 49-64
};

CanardInstance canardInit(const CanardHeapAllocate heap_allocate,
                          const CanardHeapFree     heap_free,
                          const CanardRxFilter     rx_filter)
{
    CANARD_ASSERT(heap_allocate != NULL);
    CANARD_ASSERT(heap_free != NULL);
    CANARD_ASSERT(rx_filter != NULL);
    const CanardInstance out = {
        .user_reference = NULL,
        .node_id        = CANARD_NODE_ID_UNSET,
        .mtu_bytes      = CANARD_MTU_CAN_FD,
        .heap_allocate  = heap_allocate,
        .heap_free      = heap_free,
        .rx_filter      = rx_filter,
        ._rx_sessions   = NULL,
        ._tx_queue      = NULL,
    };
    return out;
}

void canardTxPush(CanardInstance* const ins, const CanardTransfer* const transfer)
{
    if ((ins != NULL) && (transfer != NULL))
    {
        const uint8_t  pl_mtu = getPresentationLayerMTU(ins);
        const uint32_t can_id = makeCANID(transfer, ins->node_id, pl_mtu);
        if (can_id <= CAN_EXT_ID_MASK)
        {
            if (transfer->payload_size <= pl_mtu)
            {
                pushSingleFrameTransfer(ins,
                                        transfer->timestamp_usec,
                                        can_id,
                                        transfer->transfer_id,
                                        transfer->payload_size,
                                        transfer->payload);
            }
            else
            {
                pushMultiFrameTransfer(ins,
                                       pl_mtu,
                                       transfer->timestamp_usec,
                                       can_id,
                                       transfer->transfer_id,
                                       transfer->payload_size,
                                       transfer->payload);
            }
        }
    }
    else
    {
        CANARD_ASSERT(false);
    }
}

// ---------------------------------------- FLOAT16 SERIALIZATION ----------------------------------------

#if CANARD_PLATFORM_IEEE754

// Intentional violation of MISRA: we need this union because the alternative is far more error prone.
// We have to rely on low-level data representation details to do the conversion; unions are helpful.
typedef union  // NOSONAR
{
    uint32_t              bits;
    CanardIEEE754Binary32 real;
} Float32Bits;
static_assert(4 == sizeof(CanardIEEE754Binary32), "Native float format shall match IEEE 754 binary32");
static_assert(4 == sizeof(Float32Bits), "Native float format shall match IEEE 754 binary32");

uint16_t canardDSDLFloat16Serialize(const CanardIEEE754Binary32 value)
{
    // The no-lint statements suppress the warnings about magic numbers. These numbers are not magic.
    const uint32_t    round_mask = ~(uint32_t) 0x0FFFU;                 // NOLINT
    const Float32Bits f32inf     = {.bits = ((uint32_t) 255U) << 23U};  // NOLINT
    const Float32Bits f16inf     = {.bits = ((uint32_t) 31U) << 23U};   // NOLINT
    const Float32Bits magic      = {.bits = ((uint32_t) 15U) << 23U};   // NOLINT
    Float32Bits       in         = {.real = value};
    const uint32_t    sign       = in.bits & (((uint32_t) 1U) << 31U);  // NOLINT
    in.bits ^= sign;
    uint16_t out = 0;
    if (in.bits >= f32inf.bits)
    {
        out = (in.bits > f32inf.bits) ? (uint16_t) 0x7FFFU : (uint16_t) 0x7C00U;  // NOLINT
    }
    else
    {
        in.bits &= round_mask;
        in.real *= magic.real;
        in.bits -= round_mask;
        if (in.bits > f16inf.bits)
        {
            in.bits = f16inf.bits;
        }
        out = (uint16_t)(in.bits >> 13U);  // NOLINT
    }
    out |= (uint16_t)(sign >> 16U);  // NOLINT
    return out;
}

CanardIEEE754Binary32 canardDSDLFloat16Deserialize(const uint16_t value)
{
    // The no-lint statements suppress the warnings about magic numbers. These numbers are not magic.
    const Float32Bits magic   = {.bits = ((uint32_t) 0xEFU) << 23U};             // NOLINT
    const Float32Bits inf_nan = {.bits = ((uint32_t) 0x8FU) << 23U};             // NOLINT
    Float32Bits       out     = {.bits = ((uint32_t)(value & 0x7FFFU)) << 13U};  // NOLINT
    out.real *= magic.real;
    if (out.real >= inf_nan.real)
    {
        out.bits |= ((uint32_t) 0xFFU) << 23U;  // NOLINT
    }
    out.bits |= ((uint32_t)(value & 0x8000U)) << 16U;  // NOLINT
    return out.real;
}

#endif  // CANARD_PLATFORM_IEEE754
