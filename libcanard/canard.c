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
    CANARD_ASSERT((data != NULL) || (size == 0U));
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

#define FLAG_SERVICE_NOT_MESSAGE (UINT32_C(1) << 25U)
#define FLAG_ANONYMOUS_MESSAGE (UINT32_C(1) << 24U)
#define FLAG_REQUEST_NOT_RESPONSE (UINT32_C(1) << 24U)

CANARD_INTERNAL uint32_t makeMessageSessionSpecifier(const uint16_t subject_id, const uint8_t src_node_id);
CANARD_INTERNAL uint32_t makeMessageSessionSpecifier(const uint16_t subject_id, const uint8_t src_node_id)
{
    CANARD_ASSERT(src_node_id <= CANARD_NODE_ID_MAX);
    CANARD_ASSERT(subject_id <= CANARD_SUBJECT_ID_MAX);
    return src_node_id | ((uint32_t) subject_id << OFFSET_SUBJECT_ID);
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
    CANARD_ASSERT(src_node_id <= CANARD_NODE_ID_MAX);
    CANARD_ASSERT(dst_node_id <= CANARD_NODE_ID_MAX);
    CANARD_ASSERT(service_id <= CANARD_SERVICE_ID_MAX);
    return src_node_id | (((uint32_t) dst_node_id) << OFFSET_DST_NODE_ID) |  //
           (((uint32_t) service_id) << OFFSET_SERVICE_ID) |                  //
           (request_not_response ? FLAG_REQUEST_NOT_RESPONSE : 0U) | FLAG_SERVICE_NOT_MESSAGE;
}

// ---------------------------------------- TRANSMISSION ----------------------------------------

/// The fields are ordered to minimize padding on all platforms.
typedef struct CanardInternalTxQueueItem
{
    struct CanardInternalTxQueueItem* next;

    uint32_t id;
    uint64_t deadline_usec;
    size_t   payload_size;

    // Intentional violation of MISRA: this flex array is the lesser of three evils. The other two are:
    //  - Use pointer, make it point to the remainder of the allocated memory following this structure.
    //    The pointer is bad because it requires us to use pointer arithmetics and adds sizeof(void*) of waste per item.
    //  - Use a separate memory allocation for data. This is terribly wasteful.
    uint8_t payload[];  // NOSONAR
} CanardInternalTxQueueItem;

/// This is the transport MTU rounded up to next full DLC minus the tail byte.
CANARD_INTERNAL size_t getPresentationLayerMTU(const CanardInstance* const ins);
CANARD_INTERNAL size_t getPresentationLayerMTU(const CanardInstance* const ins)
{
    const size_t max_index = (sizeof(CanardCANLengthToDLC) / sizeof(CanardCANLengthToDLC[0])) - 1U;
    size_t       mtu       = 0U;
    if (ins->mtu_bytes < CANARD_MTU_CAN_CLASSIC)
    {
        mtu = CANARD_MTU_CAN_CLASSIC;
    }
    else if (ins->mtu_bytes <= max_index)
    {
        mtu = CanardCANDLCToLength[CanardCANLengthToDLC[ins->mtu_bytes]];  // Round up to nearest valid length.
    }
    else
    {
        mtu = CanardCANDLCToLength[CanardCANLengthToDLC[max_index]];
    }
    return mtu - 1U;
}

CANARD_INTERNAL int32_t makeCANID(const CanardTransfer* const tr,
                                  const uint8_t               local_node_id,
                                  const size_t                presentation_layer_mtu);
CANARD_INTERNAL int32_t makeCANID(const CanardTransfer* const tr,
                                  const uint8_t               local_node_id,
                                  const size_t                presentation_layer_mtu)
{
    CANARD_ASSERT(tr != NULL);
    CANARD_ASSERT(presentation_layer_mtu > 0);
    int32_t out = -CANARD_ERROR_INVALID_ARGUMENT;
    if ((tr->transfer_kind == CanardTransferKindMessage) && (CANARD_NODE_ID_UNSET == tr->remote_node_id) &&
        (tr->port_id <= CANARD_SUBJECT_ID_MAX))
    {
        if (local_node_id <= CANARD_NODE_ID_MAX)
        {
            out = (int32_t) makeMessageSessionSpecifier(tr->port_id, local_node_id);
            CANARD_ASSERT(out >= 0);
        }
        else if (tr->payload_size <= presentation_layer_mtu)
        {
            CANARD_ASSERT((tr->payload != NULL) || (tr->payload_size == 0U));
            const uint8_t  c    = (uint8_t)(crcAdd(CRC_INITIAL, tr->payload_size, tr->payload) & CANARD_NODE_ID_MAX);
            const uint32_t spec = makeMessageSessionSpecifier(tr->port_id, c) | FLAG_ANONYMOUS_MESSAGE;
            CANARD_ASSERT(spec <= CAN_EXT_ID_MASK);
            out = (int32_t) spec;
        }
        else
        {
            out = -CANARD_ERROR_INVALID_ARGUMENT;  // Anonymous multi-frame message trs are not allowed.
        }
    }
    else if (((tr->transfer_kind == CanardTransferKindRequest) || (tr->transfer_kind == CanardTransferKindResponse)) &&
             (tr->remote_node_id <= CANARD_NODE_ID_MAX) && (tr->port_id <= CANARD_SERVICE_ID_MAX))
    {
        if (local_node_id <= CANARD_NODE_ID_MAX)
        {
            out = (int32_t) makeServiceSessionSpecifier(tr->port_id,
                                                        tr->transfer_kind == CanardTransferKindRequest,
                                                        local_node_id,
                                                        tr->remote_node_id);
            CANARD_ASSERT(out >= 0);
        }
        else
        {
            out = -CANARD_ERROR_INVALID_ARGUMENT;  // Anonymous service transfers are not allowed.
        }
    }
    else
    {
        out = -CANARD_ERROR_INVALID_ARGUMENT;
    }

    if (out >= 0)
    {
        const uint32_t prio = (uint32_t) tr->priority;
        if (prio <= CANARD_PRIORITY_MAX)
        {
            const uint32_t id = ((uint32_t) out) | (prio << OFFSET_PRIORITY);
            out               = (int32_t) id;
        }
        else
        {
            out = -CANARD_ERROR_INVALID_ARGUMENT;
        }
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
    CANARD_ASSERT(start_of_transfer ? toggle : true);
    return (uint8_t)((start_of_transfer ? TAIL_START_OF_TRANSFER : 0U) | (end_of_transfer ? TAIL_END_OF_TRANSFER : 0U) |
                     (toggle ? TAIL_TOGGLE : 0U) | (transfer_id & CANARD_TRANSFER_ID_MAX));
}

/// Takes a frame payload size, returns a new size that is >=x and is rounded up to the nearest valid DLC.
CANARD_INTERNAL size_t roundFramePayloadSizeUp(const size_t x);
CANARD_INTERNAL size_t roundFramePayloadSizeUp(const size_t x)
{
    CANARD_ASSERT(x < (sizeof(CanardCANLengthToDLC) / sizeof(CanardCANLengthToDLC[0])));
    // Suppressing a false-positive out-of-bounds access error from Sonar. Its control flow analyser is misbehaving.
    const size_t y = CanardCANLengthToDLC[x];  // NOSONAR
    CANARD_ASSERT(y < (sizeof(CanardCANDLCToLength) / sizeof(CanardCANDLCToLength[0])));
    return CanardCANDLCToLength[y];
}

CANARD_INTERNAL CanardInternalTxQueueItem* allocateTxQueueItem(CanardInstance* const ins,
                                                               const uint32_t        id,
                                                               const uint64_t        deadline_usec,
                                                               const size_t          payload_size);
CANARD_INTERNAL CanardInternalTxQueueItem* allocateTxQueueItem(CanardInstance* const ins,
                                                               const uint32_t        id,
                                                               const uint64_t        deadline_usec,
                                                               const size_t          payload_size)
{
    CANARD_ASSERT(ins != NULL);
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
CANARD_INTERNAL CanardInternalTxQueueItem* findTxQueueSupremum(const CanardInstance* const ins, const uint32_t can_id);
CANARD_INTERNAL CanardInternalTxQueueItem* findTxQueueSupremum(const CanardInstance* const ins, const uint32_t can_id)
{
    CANARD_ASSERT(ins != NULL);
    CANARD_ASSERT(can_id <= CAN_EXT_ID_MASK);
    CanardInternalTxQueueItem* out = ins->_tx_queue;
    if ((NULL == out) || (out->id > can_id))
    {
        out = NULL;
    }
    else
    {
        // The linear search should be replaced with O(log n) at least. Please help us here.
        while ((out != NULL) && (out->next != NULL) && (out->next->id <= can_id))
        {
            out = out->next;
        }
    }
    CANARD_ASSERT((out == NULL) || (out->id <= can_id));
    return out;
}

/// Returns the number of frames enqueued or error (i.e., =1 or <0).
CANARD_INTERNAL int32_t pushSingleFrameTransfer(CanardInstance* const ins,
                                                const uint64_t        deadline_usec,
                                                const uint32_t        can_id,
                                                const uint8_t         transfer_id,
                                                const size_t          payload_size,
                                                const void* const     payload);
CANARD_INTERNAL int32_t pushSingleFrameTransfer(CanardInstance* const ins,
                                                const uint64_t        deadline_usec,
                                                const uint32_t        can_id,
                                                const uint8_t         transfer_id,
                                                const size_t          payload_size,
                                                const void* const     payload)
{
    CANARD_ASSERT(ins != NULL);
    CANARD_ASSERT((payload != NULL) || (payload_size == 0));

    const size_t frame_payload_size = roundFramePayloadSizeUp(payload_size + 1U);
    CANARD_ASSERT(frame_payload_size > payload_size);
    const size_t padding_size = frame_payload_size - payload_size - 1U;
    CANARD_ASSERT((padding_size + payload_size + 1U) == frame_payload_size);
    int32_t out = 0;

    CanardInternalTxQueueItem* const tqi = allocateTxQueueItem(ins, can_id, deadline_usec, frame_payload_size);
    if (tqi != NULL)
    {
        if (payload_size > 0U)  // The check is needed to avoid calling memcpy() with a NULL pointer, it's an UB.
        {
            CANARD_ASSERT(payload != NULL);
            // Clang-Tidy raises an error recommending the use of memcpy_s() instead.
            // We ignore this recommendation because it is not available in C99.
            (void) memcpy(&tqi->payload[0], payload, payload_size);  // NOLINT
        }

        // Clang-Tidy raises an error recommending the use of memset_s() instead.
        // We ignore this recommendation because it is not available in C99.
        (void) memset(&tqi->payload[payload_size], PADDING_BYTE, padding_size);  // NOLINT

        tqi->payload[frame_payload_size - 1U] = makeTailByte(true, true, true, transfer_id);
        CanardInternalTxQueueItem* const sup  = findTxQueueSupremum(ins, can_id);
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
        out = 1;  // One frame enqueued.
    }
    else
    {
        out = -CANARD_ERROR_OUT_OF_MEMORY;
    }
    CANARD_ASSERT((out < 0) || (out == 1));
    return out;
}

/// Returns the number of frames enqueued or error.
CANARD_INTERNAL int32_t pushMultiFrameTransfer(CanardInstance* const ins,
                                               const size_t          presentation_layer_mtu,
                                               const uint64_t        deadline_usec,
                                               const uint32_t        can_id,
                                               const uint8_t         transfer_id,
                                               const size_t          payload_size,
                                               const void* const     payload);
CANARD_INTERNAL int32_t pushMultiFrameTransfer(CanardInstance* const ins,
                                               const size_t          presentation_layer_mtu,
                                               const uint64_t        deadline_usec,
                                               const uint32_t        can_id,
                                               const uint8_t         transfer_id,
                                               const size_t          payload_size,
                                               const void* const     payload)
{
    CANARD_ASSERT(ins != NULL);
    CANARD_ASSERT(presentation_layer_mtu > 0U);
    CANARD_ASSERT(payload_size > presentation_layer_mtu);  // Otherwise, a single-frame transfer should be used.
    CANARD_ASSERT(payload != NULL);

    int32_t out = 0;  // The number of frames enqueued or negated error.

    CanardInternalTxQueueItem* head = NULL;  // Head and tail of the linked list of frames of this transfer.
    CanardInternalTxQueueItem* tail = NULL;

    const size_t   payload_size_with_crc = payload_size + CRC_SIZE_BYTES;
    size_t         offset                = 0U;
    uint16_t       crc                   = crcAdd(CRC_INITIAL, payload_size, payload);
    bool           start_of_transfer     = true;
    bool           toggle                = true;
    const uint8_t* payload_ptr           = (const uint8_t*) payload;

    while (offset < payload_size_with_crc)
    {
        ++out;
        const size_t frame_payload_size_with_tail =
            ((payload_size_with_crc - offset) < presentation_layer_mtu)
                ? roundFramePayloadSizeUp(payload_size_with_crc - offset + 1U)  // Add padding only in the last frame.
                : (presentation_layer_mtu + 1U);
        CanardInternalTxQueueItem* const tqi =
            allocateTxQueueItem(ins, can_id, deadline_usec, frame_payload_size_with_tail);
        if (NULL == head)
        {
            head = tqi;
        }
        else
        {
            tail->next = tqi;
        }
        tail = tqi;
        if (NULL == tail)
        {
            break;
        }

        // Copy the payload into the frame.
        const size_t frame_payload_size = frame_payload_size_with_tail - 1U;
        size_t       frame_offset       = 0U;
        if (offset < payload_size)
        {
            size_t move_size = payload_size - offset;
            if (move_size > frame_payload_size)
            {
                move_size = frame_payload_size;
            }
            // Clang-Tidy raises an error recommending the use of memcpy_s() instead.
            // We ignore this recommendation because it is not available in C99.
            (void) memcpy(&tail->payload[0], payload_ptr, move_size);  // NOLINT
            frame_offset = frame_offset + move_size;
            offset += move_size;
            payload_ptr += move_size;
        }

        // Handle the last frame of the transfer: it is special because it also contains padding and CRC.
        if (offset >= payload_size)
        {
            // Insert padding -- only in the last frame. Don't forget to include padding into the CRC.
            while ((frame_offset + CRC_SIZE_BYTES) < frame_payload_size)
            {
                tail->payload[frame_offset] = PADDING_BYTE;
                ++frame_offset;
                crc = crcAddByte(crc, PADDING_BYTE);
            }

            // Insert the CRC.
            if ((frame_offset < frame_payload_size) && (offset == payload_size))
            {
                tail->payload[frame_offset] = (uint8_t)(crc >> BITS_PER_BYTE);
                ++frame_offset;
                ++offset;
            }
            if ((frame_offset < frame_payload_size) && (offset > payload_size))
            {
                tail->payload[frame_offset] = (uint8_t)(crc & BYTE_MAX);
                ++frame_offset;
                ++offset;
            }
        }

        // Finalize the frame.
        CANARD_ASSERT((frame_offset + 1U) == tail->payload_size);
        tail->payload[frame_offset] =
            makeTailByte(start_of_transfer, offset >= payload_size_with_crc, toggle, transfer_id);
        start_of_transfer = false;
        toggle            = !toggle;
    }

    if (tail != NULL)
    {
        CANARD_ASSERT(head->next != NULL);  // This is not a single-frame transfer so at least two frames shall exist.
        CANARD_ASSERT(tail->next == NULL);  // The list shall be properly terminated.
        CanardInternalTxQueueItem* const sup = findTxQueueSupremum(ins, can_id);
        if (NULL == sup)  // Once the insertion point is located, we insert the entire frame sequence in constant time.
        {
            tail->next     = ins->_tx_queue;
            ins->_tx_queue = head;
        }
        else
        {
            tail->next = sup->next;
            sup->next  = head;
        }
    }
    else  // Failed to allocate at least one frame in the queue! Remove all frames and abort.
    {
        out = -CANARD_ERROR_OUT_OF_MEMORY;
        while (head != NULL)
        {
            CanardInternalTxQueueItem* const next = head->next;
            ins->heap_free(ins, head);
            head = next;
        }
    }

    CANARD_ASSERT((out < 0) || (out >= 2));
    return out;
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
    uint8_t  iface_index;
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
        .mtu_bytes      = CANARD_MTU_CAN_FD,
        .node_id        = CANARD_NODE_ID_UNSET,
        .heap_allocate  = heap_allocate,
        .heap_free      = heap_free,
        .rx_filter      = rx_filter,
        ._rx_sessions   = NULL,
        ._tx_queue      = NULL,
    };
    return out;
}

int32_t canardTxPush(CanardInstance* const ins, const CanardTransfer* const transfer)
{
    int32_t out = -CANARD_ERROR_INVALID_ARGUMENT;
    if ((ins != NULL) && (transfer != NULL) && ((transfer->payload != NULL) || (transfer->payload_size == 0U)))
    {
        const size_t  pl_mtu       = getPresentationLayerMTU(ins);
        const int32_t maybe_can_id = makeCANID(transfer, ins->node_id, pl_mtu);
        if (maybe_can_id >= 0)
        {
            if (transfer->payload_size <= pl_mtu)
            {
                out = pushSingleFrameTransfer(ins,
                                              transfer->timestamp_usec,
                                              (uint32_t) maybe_can_id,
                                              transfer->transfer_id,
                                              transfer->payload_size,
                                              transfer->payload);
            }
            else
            {
                out = pushMultiFrameTransfer(ins,
                                             pl_mtu,
                                             transfer->timestamp_usec,
                                             (uint32_t) maybe_can_id,
                                             transfer->transfer_id,
                                             transfer->payload_size,
                                             transfer->payload);
            }
        }
        else
        {
            out = maybe_can_id;
        }
    }
    return out;
}

int8_t canardTxPeek(const CanardInstance* const ins, CanardCANFrame* const out_frame)
{
    int8_t out = -CANARD_ERROR_INVALID_ARGUMENT;
    if ((ins != NULL) && (out_frame != NULL))
    {
        CanardInternalTxQueueItem* const tqi = ins->_tx_queue;
        if (tqi != NULL)
        {
            out_frame->timestamp_usec  = tqi->deadline_usec;
            out_frame->extended_can_id = tqi->id;
            out_frame->payload_size    = tqi->payload_size;
            out_frame->payload         = &tqi->payload[0];
            out                        = 1;
        }
        else
        {
            out = 0;
        }
    }
    return out;
}

void canardTxPop(CanardInstance* const ins)
{
    if ((ins != NULL) && (ins->_tx_queue != NULL))
    {
        CanardInternalTxQueueItem* const next = ins->_tx_queue->next;
        ins->heap_free(ins, ins->_tx_queue);
        ins->_tx_queue = next;
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
    // The no-lint statements suppress the warning about the use of union. This is required for low-level bit access.
    const uint32_t    round_mask = ~(uint32_t) 0x0FFFU;                 // NOLINT
    const Float32Bits f32inf     = {.bits = ((uint32_t) 255U) << 23U};  // NOLINT NOSONAR
    const Float32Bits f16inf     = {.bits = ((uint32_t) 31U) << 23U};   // NOLINT NOSONAR
    const Float32Bits magic      = {.bits = ((uint32_t) 15U) << 23U};   // NOLINT NOSONAR
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
    // The no-lint statements suppress the warning about the use of union. This is required for low-level bit access.
    const Float32Bits magic   = {.bits = ((uint32_t) 0xEFU) << 23U};             // NOLINT NOSONAR
    const Float32Bits inf_nan = {.bits = ((uint32_t) 0x8FU) << 23U};             // NOLINT NOSONAR
    Float32Bits       out     = {.bits = ((uint32_t)(value & 0x7FFFU)) << 13U};  // NOLINT NOSONAR
    out.real *= magic.real;
    if (out.real >= inf_nan.real)
    {
        out.bits |= ((uint32_t) 0xFFU) << 23U;  // NOLINT
    }
    out.bits |= ((uint32_t)(value & 0x8000U)) << 16U;  // NOLINT
    return out.real;
}

#endif  // CANARD_PLATFORM_IEEE754
