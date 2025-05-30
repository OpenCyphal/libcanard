/// This software is distributed under the terms of the MIT License.
/// Copyright (c) OpenCyphal.
/// Author: Pavel Kirienko <pavel@opencyphal.org>

#include "canard.h"
#include <stddef.h>
#include <string.h>

// --------------------------------------------- BUILD CONFIGURATION ---------------------------------------------

/// Define this macro to include build configuration header.
/// Usage example with CMake: "-DCANARD_CONFIG_HEADER=\"${CMAKE_CURRENT_SOURCE_DIR}/my_canard_config.h\""
#ifdef CANARD_CONFIG_HEADER
#    include CANARD_CONFIG_HEADER
#endif

/// By default, this macro resolves to the standard assert(). The user can redefine this if necessary.
/// To disable assertion checks completely, make it expand into `(void)(0)`.
#ifndef CANARD_ASSERT
// Intentional violation of MISRA: inclusion not at the top of the file to eliminate unnecessary dependency on assert.h.
#    include <assert.h>  // NOSONAR
// Intentional violation of MISRA: assertion macro cannot be replaced with a function definition.
#    define CANARD_ASSERT(x) assert(x)  // NOSONAR
#endif

/// Define CANARD_CRC_TABLE=0 to use slow but ROM-efficient transfer-CRC computation algorithm.
/// Doing so is expected to save ca. 500 bytes of ROM and increase the cost of RX/TX transfer processing by ~half.
#ifndef CANARD_CRC_TABLE
#    define CANARD_CRC_TABLE 1
#endif

/// This macro is needed for testing and for library development.
#ifndef CANARD_PRIVATE
#    define CANARD_PRIVATE static inline
#endif

#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 199901L)
#    error "Unsupported language: ISO C99 or a newer version is required."
#endif

// --------------------------------------------- INTERNAL INCLUDES ----------------------------------------------
// The internal includes are placed here after the config header is included and CANARD_ASSERT is defined.

#include "_canard_cavl.h"

// --------------------------------------------- COMMON DEFINITIONS ---------------------------------------------

#define BITS_PER_BYTE 8U
#define BYTE_MAX 0xFFU

#define CAN_EXT_ID_MASK ((UINT32_C(1) << 29U) - 1U)

#define MFT_NON_LAST_FRAME_PAYLOAD_MIN 7U

#define PADDING_BYTE_VALUE 0U

#define OFFSET_PRIORITY 26U
#define OFFSET_SUBJECT_ID 8U
#define OFFSET_SERVICE_ID 14U
#define OFFSET_DST_NODE_ID 7U

#define FLAG_SERVICE_NOT_MESSAGE (UINT32_C(1) << 25U)
#define FLAG_ANONYMOUS_MESSAGE (UINT32_C(1) << 24U)
#define FLAG_REQUEST_NOT_RESPONSE (UINT32_C(1) << 24U)
#define FLAG_RESERVED_23 (UINT32_C(1) << 23U)
#define FLAG_RESERVED_07 (UINT32_C(1) << 7U)

#define TAIL_START_OF_TRANSFER 128U
#define TAIL_END_OF_TRANSFER 64U
#define TAIL_TOGGLE 32U

#define INITIAL_TOGGLE_STATE true

#define CONTAINER_OF(type, ptr, member) \
    ((const type*) (((ptr) == NULL) ? NULL : (const void*) (((const char*) (ptr)) - offsetof(type, member))))
#define MUTABLE_CONTAINER_OF(type, ptr, member) \
    ((type*) (((ptr) == NULL) ? NULL : (void*) (((char*) (ptr)) - offsetof(type, member))))

/// Used for inserting new items into AVL trees.
CANARD_PRIVATE struct CanardTreeNode* avlTrivialFactory(void* const user_reference)
{
    return (struct CanardTreeNode*) user_reference;
}

// --------------------------------------------- TRANSFER CRC ---------------------------------------------

typedef uint16_t TransferCRC;

#define CRC_INITIAL 0xFFFFU
#define CRC_RESIDUE 0x0000U
#define CRC_SIZE_BYTES 2U

#if (CANARD_CRC_TABLE != 0)
static const uint16_t CRCTable[256] = {
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
#endif

CANARD_PRIVATE TransferCRC crcAddByte(const TransferCRC crc, const uint8_t byte)
{
#if (CANARD_CRC_TABLE != 0)
    return (uint16_t) ((uint16_t) (crc << BITS_PER_BYTE) ^
                       CRCTable[(uint16_t) ((uint16_t) (crc >> BITS_PER_BYTE) ^ byte) & BYTE_MAX]);
#else
    static const TransferCRC Top  = 0x8000U;
    static const TransferCRC Poly = 0x1021U;
    TransferCRC              out  = crc ^ (uint16_t) ((uint16_t) (byte) << BITS_PER_BYTE);
    // Consider adding a compilation option that replaces this with a CRC table. Adds 512 bytes of ROM.
    // Do not fold this into a loop because a size-optimizing compiler won't unroll it degrading the performance.
    out = (uint16_t) ((uint16_t) (out << 1U) ^ (((out & Top) != 0U) ? Poly : 0U));
    out = (uint16_t) ((uint16_t) (out << 1U) ^ (((out & Top) != 0U) ? Poly : 0U));
    out = (uint16_t) ((uint16_t) (out << 1U) ^ (((out & Top) != 0U) ? Poly : 0U));
    out = (uint16_t) ((uint16_t) (out << 1U) ^ (((out & Top) != 0U) ? Poly : 0U));
    out = (uint16_t) ((uint16_t) (out << 1U) ^ (((out & Top) != 0U) ? Poly : 0U));
    out = (uint16_t) ((uint16_t) (out << 1U) ^ (((out & Top) != 0U) ? Poly : 0U));
    out = (uint16_t) ((uint16_t) (out << 1U) ^ (((out & Top) != 0U) ? Poly : 0U));
    out = (uint16_t) ((uint16_t) (out << 1U) ^ (((out & Top) != 0U) ? Poly : 0U));
    return out;
#endif
}

CANARD_PRIVATE TransferCRC crcAdd(const TransferCRC crc, const struct CanardPayload payload)
{
    CANARD_ASSERT((payload.data != NULL) || (payload.size == 0U));
    TransferCRC    out = crc;
    const uint8_t* p   = (const uint8_t*) payload.data;
    for (size_t i = 0; i < payload.size; i++)
    {
        out = crcAddByte(out, *p);
        ++p;
    }
    return out;
}

// --------------------------------------------- TRANSMISSION ---------------------------------------------

/// Chain of TX frames prepared for insertion into a TX queue.
typedef struct
{
    struct CanardTxQueueItem* head;
    struct CanardTxQueueItem* tail;
    size_t                    size;
} TxChain;

CANARD_PRIVATE uint32_t txMakeMessageSessionSpecifier(const CanardPortID subject_id, const CanardNodeID src_node_id)
{
    CANARD_ASSERT(src_node_id <= CANARD_NODE_ID_MAX);
    CANARD_ASSERT(subject_id <= CANARD_SUBJECT_ID_MAX);
    const uint32_t tmp = subject_id | (CANARD_SUBJECT_ID_MAX + 1) | ((CANARD_SUBJECT_ID_MAX + 1) * 2);
    return src_node_id | (tmp << OFFSET_SUBJECT_ID);
}

CANARD_PRIVATE uint32_t txMakeServiceSessionSpecifier(const CanardPortID service_id,
                                                      const bool         request_not_response,
                                                      const CanardNodeID src_node_id,
                                                      const CanardNodeID dst_node_id)
{
    CANARD_ASSERT(src_node_id <= CANARD_NODE_ID_MAX);
    CANARD_ASSERT(dst_node_id <= CANARD_NODE_ID_MAX);
    CANARD_ASSERT(service_id <= CANARD_SERVICE_ID_MAX);
    return src_node_id | (((uint32_t) dst_node_id) << OFFSET_DST_NODE_ID) |  //
           (((uint32_t) service_id) << OFFSET_SERVICE_ID) |                  //
           (request_not_response ? FLAG_REQUEST_NOT_RESPONSE : 0U) | FLAG_SERVICE_NOT_MESSAGE;
}

/// This is the transport MTU rounded up to next full DLC minus the tail byte.
CANARD_PRIVATE size_t adjustPresentationLayerMTU(const size_t mtu_bytes)
{
    const size_t max_index = (sizeof(CanardCANLengthToDLC) / sizeof(CanardCANLengthToDLC[0])) - 1U;
    size_t       mtu       = 0U;
    if (mtu_bytes < CANARD_MTU_CAN_CLASSIC)
    {
        mtu = CANARD_MTU_CAN_CLASSIC;
    }
    else if (mtu_bytes <= max_index)
    {
        mtu = CanardCANDLCToLength[CanardCANLengthToDLC[mtu_bytes]];  // Round up to nearest valid length.
    }
    else
    {
        mtu = CanardCANDLCToLength[CanardCANLengthToDLC[max_index]];
    }
    return mtu - 1U;
}

CANARD_PRIVATE int32_t txMakeCANID(const struct CanardTransferMetadata* const tr,
                                   const struct CanardPayload                 payload,
                                   const CanardNodeID                         local_node_id,
                                   const size_t                               presentation_layer_mtu)
{
    CANARD_ASSERT(tr != NULL);
    CANARD_ASSERT(presentation_layer_mtu > 0);
    int32_t out = -CANARD_ERROR_INVALID_ARGUMENT;
    if ((tr->transfer_kind == CanardTransferKindMessage) && (CANARD_NODE_ID_UNSET == tr->remote_node_id) &&
        (tr->port_id <= CANARD_SUBJECT_ID_MAX))
    {
        if (local_node_id <= CANARD_NODE_ID_MAX)
        {
            out = (int32_t) txMakeMessageSessionSpecifier(tr->port_id, local_node_id);
            CANARD_ASSERT(out >= 0);
        }
        else if (payload.size <= presentation_layer_mtu)
        {
            CANARD_ASSERT((payload.data != NULL) || (payload.size == 0U));
            const CanardNodeID c    = (CanardNodeID) (crcAdd(CRC_INITIAL, payload) & CANARD_NODE_ID_MAX);
            const uint32_t     spec = txMakeMessageSessionSpecifier(tr->port_id, c) | FLAG_ANONYMOUS_MESSAGE;
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
            out = (int32_t) txMakeServiceSessionSpecifier(tr->port_id,
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

CANARD_PRIVATE uint8_t txMakeTailByte(const bool             start_of_transfer,
                                      const bool             end_of_transfer,
                                      const bool             toggle,
                                      const CanardTransferID transfer_id)
{
    CANARD_ASSERT(start_of_transfer ? (toggle == INITIAL_TOGGLE_STATE) : true);
    return (uint8_t) ((start_of_transfer ? TAIL_START_OF_TRANSFER : 0U) |
                      (end_of_transfer ? TAIL_END_OF_TRANSFER : 0U) | (toggle ? TAIL_TOGGLE : 0U) |
                      (transfer_id & CANARD_TRANSFER_ID_MAX));
}

/// Takes a frame payload size, returns a new size that is >=x and is rounded up to the nearest valid DLC.
CANARD_PRIVATE size_t txRoundFramePayloadSizeUp(const size_t x)
{
    CANARD_ASSERT(x < (sizeof(CanardCANLengthToDLC) / sizeof(CanardCANLengthToDLC[0])));
    // Suppressing a false-positive out-of-bounds access error from Sonar. Its control flow analyser is misbehaving.
    const size_t y = CanardCANLengthToDLC[x];  // NOSONAR
    CANARD_ASSERT(y < (sizeof(CanardCANDLCToLength) / sizeof(CanardCANDLCToLength[0])));
    return CanardCANDLCToLength[y];
}

/// The item is only allocated and initialized, but NOT included into the queue! The caller needs to do that.
CANARD_PRIVATE struct CanardTxQueueItem* txAllocateQueueItem(struct CanardTxQueue* const        que,
                                                             const struct CanardInstance* const ins,
                                                             const uint32_t                     id,
                                                             const CanardMicrosecond            deadline_usec,
                                                             const size_t                       payload_size)
{
    CANARD_ASSERT(que != NULL);
    CANARD_ASSERT(payload_size > 0U);

    // TX queue items are allocated in the memory pool of the instance.
    // These items are just TX pipeline support structures, they are not directly transfer payload data.
    // In contrast, see below allocation of the payload buffer from a different memory pool.
    struct CanardTxQueueItem* out = ins->memory.allocate(ins->memory.user_reference, sizeof(struct CanardTxQueueItem));
    if (out != NULL)
    {
        out->priority_base.up    = NULL;
        out->priority_base.lr[0] = NULL;
        out->priority_base.lr[1] = NULL;
        out->priority_base.bf    = 0;

        out->deadline_base.up    = NULL;
        out->deadline_base.lr[0] = NULL;
        out->deadline_base.lr[1] = NULL;
        out->deadline_base.bf    = 0;

        out->next_in_transfer = NULL;  // Last by default.
        out->tx_deadline_usec = deadline_usec;

        // The payload is allocated in the memory pool of the TX queue.
        // The memory pool of the queue is supposed to be provided by the media.
        void* const payload_data = que->memory.allocate(que->memory.user_reference, payload_size);
        if (NULL != payload_data)
        {
            out->frame.payload.size           = payload_size;
            out->frame.payload.data           = payload_data;
            out->frame.payload.allocated_size = payload_size;
            out->frame.extended_can_id        = id;
        }
        else
        {
            ins->memory.deallocate(ins->memory.user_reference, sizeof(struct CanardTxQueueItem), out);
            out = NULL;
        }
    }
    return out;
}

/// Frames with identical CAN ID that are added later always compare greater than their counterparts with same CAN ID.
/// This ensures that CAN frames with the same CAN ID are transmitted in the FIFO order.
/// Frames that should be transmitted earlier compare smaller (i.e., put on the left side of the tree).
CANARD_PRIVATE int8_t txAVLPriorityPredicate(           //
    void* const                        user_reference,  // NOSONAR Cavl API requires pointer to non-const.
    const struct CanardTreeNode* const node)
{
    typedef struct CanardTxQueueItem TxItem;

    const TxItem* const target = CONTAINER_OF(TxItem, user_reference, priority_base);
    const TxItem* const other  = CONTAINER_OF(TxItem, node, priority_base);
    CANARD_ASSERT((target != NULL) && (other != NULL));
    return (target->frame.extended_can_id >= other->frame.extended_can_id) ? +1 : -1;
}

/// Frames with identical deadline
/// that are added later always compare greater than their counterparts with the same deadline.
/// This ensures that CAN frames with the same deadline are, when timed out, dropped in the FIFO order.
/// Frames that should be dropped earlier compare smaller (i.e., put on the left side of the tree).
CANARD_PRIVATE int8_t txAVLDeadlinePredicate(           //
    void* const                        user_reference,  // NOSONAR Cavl API requires pointer to non-const.
    const struct CanardTreeNode* const node)
{
    typedef struct CanardTxQueueItem TxItem;

    const TxItem* const target = CONTAINER_OF(TxItem, user_reference, deadline_base);
    const TxItem* const other  = CONTAINER_OF(TxItem, node, deadline_base);
    CANARD_ASSERT((target != NULL) && (other != NULL));
    return (target->tx_deadline_usec >= other->tx_deadline_usec) ? +1 : -1;
}

/// Returns the number of frames enqueued or error (i.e., =1 or <0).
CANARD_PRIVATE int32_t txPushSingleFrame(struct CanardTxQueue* const        que,
                                         const struct CanardInstance* const ins,
                                         const CanardMicrosecond            deadline_usec,
                                         const uint32_t                     can_id,
                                         const CanardTransferID             transfer_id,
                                         const struct CanardPayload         payload)
{
    CANARD_ASSERT((que != NULL) && (ins != NULL));
    CANARD_ASSERT((payload.data != NULL) || (payload.size == 0));
    const size_t frame_payload_size = txRoundFramePayloadSizeUp(payload.size + 1U);
    CANARD_ASSERT(frame_payload_size > payload.size);
    const size_t padding_size = frame_payload_size - payload.size - 1U;
    CANARD_ASSERT((padding_size + payload.size + 1U) == frame_payload_size);
    int32_t                         out = 0;
    struct CanardTxQueueItem* const tqi =
        (que->size < que->capacity) ? txAllocateQueueItem(que, ins, can_id, deadline_usec, frame_payload_size) : NULL;
    if (tqi != NULL)
    {
        if (payload.size > 0U)  // The check is needed to avoid calling memcpy() with a NULL pointer, it's an UB.
        {
            CANARD_ASSERT(payload.data != NULL);
            // Clang-Tidy raises an error recommending the use of memcpy_s() instead.
            // We ignore it because the safe functions are poorly supported; reliance on them may limit the portability.
            (void) memcpy(tqi->frame.payload.data, payload.data, payload.size);  // NOLINT
        }
        // Clang-Tidy raises an error recommending the use of memset_s() instead.
        // We ignore it because the safe functions are poorly supported; reliance on them may limit the portability.
        uint8_t* const frame_bytes = tqi->frame.payload.data;
        (void) memset(frame_bytes + payload.size, PADDING_BYTE_VALUE, padding_size);  // NOLINT
        *(frame_bytes + (frame_payload_size - 1U)) = txMakeTailByte(true, true, true, transfer_id);

        // Insert the newly created TX item into the priority queue.
        const struct CanardTreeNode* const priority_queue_res =
            cavlSearch(&que->priority_root, &tqi->priority_base, &txAVLPriorityPredicate, &avlTrivialFactory);
        (void) priority_queue_res;
        CANARD_ASSERT(priority_queue_res == &tqi->priority_base);

        // Insert the newly created TX item into the deadline queue.
        const struct CanardTreeNode* const deadline_queue_res =
            cavlSearch(&que->deadline_root, &tqi->deadline_base, &txAVLDeadlinePredicate, &avlTrivialFactory);
        (void) deadline_queue_res;
        CANARD_ASSERT(deadline_queue_res == &tqi->deadline_base);

        que->size++;
        CANARD_ASSERT(que->size <= que->capacity);
        out = 1;  // One frame enqueued.
    }
    else
    {
        out = -CANARD_ERROR_OUT_OF_MEMORY;
    }
    CANARD_ASSERT((out < 0) || (out == 1));
    return out;
}

/// Produces a chain of Tx queue items for later insertion into the Tx queue. The tail is NULL if OOM.
CANARD_PRIVATE TxChain txGenerateMultiFrameChain(struct CanardTxQueue* const        que,
                                                 const struct CanardInstance* const ins,
                                                 const size_t                       presentation_layer_mtu,
                                                 const CanardMicrosecond            deadline_usec,
                                                 const uint32_t                     can_id,
                                                 const CanardTransferID             transfer_id,
                                                 const struct CanardPayload         payload)
{
    CANARD_ASSERT(que != NULL);
    CANARD_ASSERT(presentation_layer_mtu > 0U);
    CANARD_ASSERT(payload.size > presentation_layer_mtu);  // Otherwise, a single-frame transfer should be used.
    CANARD_ASSERT(payload.data != NULL);

    TxChain        out                   = {.head = NULL, .tail = NULL, .size = 0U};
    const size_t   payload_size_with_crc = payload.size + CRC_SIZE_BYTES;
    size_t         offset                = 0U;
    TransferCRC    crc                   = crcAdd(CRC_INITIAL, payload);
    bool           toggle                = INITIAL_TOGGLE_STATE;
    const uint8_t* payload_ptr           = (const uint8_t*) payload.data;
    while (offset < payload_size_with_crc)
    {
        out.size++;
        const size_t frame_payload_size_with_tail =
            ((payload_size_with_crc - offset) < presentation_layer_mtu)
                ? txRoundFramePayloadSizeUp((payload_size_with_crc - offset) + 1U)  // Padding in the last frame only.
                : (presentation_layer_mtu + 1U);
        struct CanardTxQueueItem* const tqi =
            txAllocateQueueItem(que, ins, can_id, deadline_usec, frame_payload_size_with_tail);
        if (NULL == out.head)
        {
            out.head = tqi;
        }
        else
        {
            // C std, 6.7.2.1.15: A pointer to a structure object <...> points to its initial member, and vice versa.
            // Can't just read tqi->base because tqi may be NULL; https://github.com/OpenCyphal/libcanard/issues/203.
            out.tail->next_in_transfer = tqi;
        }
        out.tail = tqi;
        if (NULL == out.tail)
        {
            break;
        }

        // Copy the payload into the frame.
        uint8_t* const frame_bytes        = tqi->frame.payload.data;
        const size_t   frame_payload_size = frame_payload_size_with_tail - 1U;
        size_t         frame_offset       = 0U;
        if (offset < payload.size)
        {
            size_t move_size = payload.size - offset;
            if (move_size > frame_payload_size)
            {
                move_size = frame_payload_size;
            }
            // Clang-Tidy raises an error recommending the use of memcpy_s() instead.
            // We ignore it because the safe functions are poorly supported; reliance on them may limit the portability.
            // SonarQube incorrectly detects a buffer overflow here.
            (void) memcpy(frame_bytes, payload_ptr, move_size);  // NOLINT NOSONAR
            frame_offset = frame_offset + move_size;
            offset += move_size;
            payload_ptr += move_size;
        }

        // Handle the last frame of the transfer: it is special because it also contains padding and CRC.
        if (offset >= payload.size)
        {
            // Insert padding -- only in the last frame. Don't forget to include padding into the CRC.
            while ((frame_offset + CRC_SIZE_BYTES) < frame_payload_size)
            {
                frame_bytes[frame_offset] = PADDING_BYTE_VALUE;
                ++frame_offset;
                crc = crcAddByte(crc, PADDING_BYTE_VALUE);
            }

            // Insert the CRC.
            if ((frame_offset < frame_payload_size) && (offset == payload.size))
            {
                // SonarQube incorrectly detects a buffer overflow here.
                frame_bytes[frame_offset] = (uint8_t) (crc >> BITS_PER_BYTE);  // NOSONAR
                ++frame_offset;
                ++offset;
            }
            if ((frame_offset < frame_payload_size) && (offset > payload.size))
            {
                frame_bytes[frame_offset] = (uint8_t) (crc & BYTE_MAX);
                ++frame_offset;
                ++offset;
            }
        }

        // Finalize the frame.
        CANARD_ASSERT((frame_offset + 1U) == out.tail->frame.payload.size);
        // SonarQube incorrectly detects a buffer overflow here.
        frame_bytes[frame_offset] =  // NOSONAR
            txMakeTailByte(out.head == out.tail, offset >= payload_size_with_crc, toggle, transfer_id);
        toggle = !toggle;
    }
    return out;
}

/// Returns the number of frames enqueued or error.
CANARD_PRIVATE int32_t txPushMultiFrame(struct CanardTxQueue* const        que,
                                        const struct CanardInstance* const ins,
                                        const size_t                       presentation_layer_mtu,
                                        const CanardMicrosecond            deadline_usec,
                                        const uint32_t                     can_id,
                                        const CanardTransferID             transfer_id,
                                        const struct CanardPayload         payload)
{
    CANARD_ASSERT(que != NULL);
    CANARD_ASSERT(presentation_layer_mtu > 0U);
    CANARD_ASSERT(payload.size > presentation_layer_mtu);  // Otherwise, a single-frame transfer should be used.

    int32_t      out                   = 0;  // The number of frames enqueued or negated error.
    const size_t payload_size_with_crc = payload.size + CRC_SIZE_BYTES;
    const size_t num_frames = ((payload_size_with_crc + presentation_layer_mtu) - 1U) / presentation_layer_mtu;
    CANARD_ASSERT(num_frames >= 2);
    if ((que->size + num_frames) <= que->capacity)  // Bail early if we can see that we won't fit anyway.
    {
        const TxChain sq =
            txGenerateMultiFrameChain(que, ins, presentation_layer_mtu, deadline_usec, can_id, transfer_id, payload);
        if (sq.tail != NULL)
        {
            struct CanardTxQueueItem* next = sq.head;
            do
            {
                const struct CanardTreeNode* const priority_queue_res =
                    cavlSearch(&que->priority_root, &next->priority_base, &txAVLPriorityPredicate, &avlTrivialFactory);
                (void) priority_queue_res;
                CANARD_ASSERT(priority_queue_res == &next->priority_base);
                CANARD_ASSERT(que->priority_root != NULL);

                const struct CanardTreeNode* const deadline_queue_res =
                    cavlSearch(&que->deadline_root, &next->deadline_base, &txAVLDeadlinePredicate, &avlTrivialFactory);
                (void) deadline_queue_res;
                CANARD_ASSERT(deadline_queue_res == &next->deadline_base);
                CANARD_ASSERT(que->deadline_root != NULL);

                next = next->next_in_transfer;
            } while (next != NULL);
            CANARD_ASSERT(num_frames == sq.size);
            que->size += sq.size;
            CANARD_ASSERT(que->size <= que->capacity);
            CANARD_ASSERT((sq.size + 0ULL) <= INT32_MAX);  // +0 is to suppress warning.
            out = (int32_t) sq.size;
        }
        else
        {
            out                            = -CANARD_ERROR_OUT_OF_MEMORY;
            struct CanardTxQueueItem* head = sq.head;
            while (head != NULL)
            {
                struct CanardTxQueueItem* const next = head->next_in_transfer;
                canardTxFree(que, ins, head);
                head = next;
            }
        }
    }
    else  // We predict that we're going to run out of queue, don't bother serializing the transfer.
    {
        out = -CANARD_ERROR_OUT_OF_MEMORY;
    }
    CANARD_ASSERT((out < 0) || (out >= 2));
    return out;
}

/// Returns the number of frames freed. The number is at most 1 unless drop_whole_transfer is true.
CANARD_PRIVATE size_t txPopAndFreeTransfer(struct CanardTxQueue* const        que,
                                           const struct CanardInstance* const ins,
                                           struct CanardTxQueueItem* const    tx_item,
                                           const bool                         drop_whole_transfer)
{
    CANARD_ASSERT(que != NULL);
    CANARD_ASSERT(ins != NULL);
    CANARD_ASSERT(tx_item != NULL);

    size_t                    count           = 0;
    struct CanardTxQueueItem* next_tx_item    = tx_item;
    struct CanardTxQueueItem* tx_item_to_free = canardTxPop(que, next_tx_item);
    while (NULL != tx_item_to_free)
    {
        next_tx_item = tx_item_to_free->next_in_transfer;
        canardTxFree(que, ins, tx_item_to_free);
        if (!drop_whole_transfer)
        {
            break;
        }
        count++;
        tx_item_to_free = canardTxPop(que, next_tx_item);
    }
    return count;
}

/// Flushes expired transfers by comparing deadline timestamps of the pending transfers with the current time.
/// Returns the number of expired frames.
CANARD_PRIVATE size_t txFlushExpiredTransfers(struct CanardTxQueue* const        que,
                                              const struct CanardInstance* const ins,
                                              const CanardMicrosecond            now_usec)
{
    CANARD_ASSERT(que != NULL);
    CANARD_ASSERT(ins != NULL);
    CANARD_ASSERT(now_usec > 0);

    size_t                    count   = 0;
    struct CanardTreeNode*    tx_node = cavlFindExtremum(que->deadline_root, false);
    struct CanardTxQueueItem* tx_item = MUTABLE_CONTAINER_OF(struct CanardTxQueueItem, tx_node, deadline_base);
    while (NULL != tx_item)
    {
        if (now_usec <= tx_item->tx_deadline_usec)
        {
            break;  // The queue is sorted by deadline, so we can stop here.
        }
        count += txPopAndFreeTransfer(que, ins, tx_item, true);  // drop the whole transfer

        tx_node = cavlFindExtremum(que->deadline_root, false);
        tx_item = MUTABLE_CONTAINER_OF(struct CanardTxQueueItem, tx_node, deadline_base);
    }
    return count;
}

// --------------------------------------------- RECEPTION ---------------------------------------------

#define RX_SESSIONS_PER_SUBSCRIPTION (CANARD_NODE_ID_MAX + 1U)

/// The memory requirement model provided in the documentation assumes that the maximum size of this structure never
/// exceeds 48 bytes on any conventional platform.
/// A user that needs a detailed analysis of the worst-case memory consumption may compute the size of this structure
/// for the particular platform at hand manually or by evaluating its sizeof().
/// The fields are ordered to minimize the amount of padding on all conventional platforms.
typedef struct CanardInternalRxSession
{
    CanardMicrosecond transfer_timestamp_usec;  ///< Timestamp of the last received start-of-transfer.
    size_t            total_payload_size;       ///< The payload size before the implicit truncation, including the CRC.
    struct CanardMutablePayload payload;        ///< Dynamically allocated and handed off to the application when done.
    TransferCRC                 calculated_crc;  ///< Updated with the received payload in real time.
    CanardTransferID            transfer_id;
    uint8_t                     redundant_iface_index;  ///< Arbitrary value in [0, 255].
    bool                        toggle;
} CanardInternalRxSession;

/// High-level transport frame model.
typedef struct
{
    CanardMicrosecond       timestamp_usec;
    enum CanardPriority     priority;
    enum CanardTransferKind transfer_kind;
    CanardPortID            port_id;
    CanardNodeID            source_node_id;
    CanardNodeID            destination_node_id;
    CanardTransferID        transfer_id;
    bool                    start_of_transfer;
    bool                    end_of_transfer;
    bool                    toggle;
    struct CanardPayload    payload;
} RxFrameModel;

/// Returns truth if the frame is valid and parsed successfully. False if the frame is not a valid Cyphal/CAN frame.
CANARD_PRIVATE bool rxTryParseFrame(const CanardMicrosecond         timestamp_usec,
                                    const struct CanardFrame* const frame,
                                    RxFrameModel* const             out)
{
    CANARD_ASSERT(frame != NULL);
    CANARD_ASSERT(frame->extended_can_id <= CAN_EXT_ID_MASK);
    CANARD_ASSERT(out != NULL);
    bool valid = false;
    if (frame->payload.size > 0)
    {
        CANARD_ASSERT(frame->payload.data != NULL);
        out->timestamp_usec = timestamp_usec;

        // CAN ID parsing.
        const uint32_t can_id = frame->extended_can_id;
        out->priority         = (enum CanardPriority)((can_id >> OFFSET_PRIORITY) & CANARD_PRIORITY_MAX);
        out->source_node_id   = (CanardNodeID) (can_id & CANARD_NODE_ID_MAX);
        if (0 == (can_id & FLAG_SERVICE_NOT_MESSAGE))
        {
            out->transfer_kind = CanardTransferKindMessage;
            out->port_id       = (CanardPortID) ((can_id >> OFFSET_SUBJECT_ID) & CANARD_SUBJECT_ID_MAX);
            if ((can_id & FLAG_ANONYMOUS_MESSAGE) != 0)
            {
                out->source_node_id = CANARD_NODE_ID_UNSET;
            }
            out->destination_node_id = CANARD_NODE_ID_UNSET;
            // Reserved bits may be unreserved in the future.
            valid = (0 == (can_id & FLAG_RESERVED_23)) && (0 == (can_id & FLAG_RESERVED_07));
        }
        else
        {
            out->transfer_kind =
                ((can_id & FLAG_REQUEST_NOT_RESPONSE) != 0) ? CanardTransferKindRequest : CanardTransferKindResponse;
            out->port_id             = (CanardPortID) ((can_id >> OFFSET_SERVICE_ID) & CANARD_SERVICE_ID_MAX);
            out->destination_node_id = (CanardNodeID) ((can_id >> OFFSET_DST_NODE_ID) & CANARD_NODE_ID_MAX);
            // The reserved bit may be unreserved in the future. It may be used to extend the service-ID to 10 bits.
            // Per Specification, source cannot be the same as the destination.
            valid = (0 == (can_id & FLAG_RESERVED_23)) && (out->source_node_id != out->destination_node_id);
        }

        // Payload parsing.
        out->payload.size = frame->payload.size - 1U;  // Cut off the tail byte.
        out->payload.data = frame->payload.data;

        // Tail byte parsing.
        // Intentional violation of MISRA: pointer arithmetics is required to locate the tail byte. Unavoidable.
        const uint8_t tail     = *(((const uint8_t*) out->payload.data) + out->payload.size);  // NOSONAR
        out->transfer_id       = tail & CANARD_TRANSFER_ID_MAX;
        out->start_of_transfer = ((tail & TAIL_START_OF_TRANSFER) != 0);
        out->end_of_transfer   = ((tail & TAIL_END_OF_TRANSFER) != 0);
        out->toggle            = ((tail & TAIL_TOGGLE) != 0);

        // Final validation.
        // Protocol version check: if SOT is set, then the toggle shall also be set.
        valid = valid && ((!out->start_of_transfer) || (INITIAL_TOGGLE_STATE == out->toggle));
        // Anonymous transfers can be only single-frame transfers.
        valid = valid &&
                ((out->start_of_transfer && out->end_of_transfer) || (CANARD_NODE_ID_UNSET != out->source_node_id));
        // Non-last frames of a multi-frame transfer shall utilize the MTU fully.
        valid = valid && ((out->payload.size >= MFT_NON_LAST_FRAME_PAYLOAD_MIN) || out->end_of_transfer);
        // A frame that is a part of a multi-frame transfer cannot be empty (tail byte not included).
        valid = valid && ((out->payload.size > 0) || (out->start_of_transfer && out->end_of_transfer));
    }
    return valid;
}

CANARD_PRIVATE void rxInitTransferMetadataFromFrame(const RxFrameModel* const            frame,
                                                    struct CanardTransferMetadata* const out_transfer)
{
    CANARD_ASSERT(frame != NULL);
    CANARD_ASSERT(frame->payload.data != NULL);
    CANARD_ASSERT(out_transfer != NULL);
    out_transfer->priority       = frame->priority;
    out_transfer->transfer_kind  = frame->transfer_kind;
    out_transfer->port_id        = frame->port_id;
    out_transfer->remote_node_id = frame->source_node_id;
    out_transfer->transfer_id    = frame->transfer_id;
}

/// The implementation is borrowed from the Specification.
CANARD_PRIVATE uint8_t rxComputeTransferIDDifference(const uint8_t a, const uint8_t b)
{
    CANARD_ASSERT(a <= CANARD_TRANSFER_ID_MAX);
    CANARD_ASSERT(b <= CANARD_TRANSFER_ID_MAX);
    int16_t diff = (int16_t) (((int16_t) a) - ((int16_t) b));
    if (diff < 0)
    {
        const uint8_t modulo = 1U << CANARD_TRANSFER_ID_BIT_LENGTH;
        diff                 = (int16_t) (diff + (int16_t) modulo);
    }
    return (uint8_t) diff;
}

CANARD_PRIVATE int8_t rxSessionWritePayload(struct CanardInstance* const   ins,
                                            CanardInternalRxSession* const rxs,
                                            const size_t                   extent,
                                            const struct CanardPayload     payload)
{
    CANARD_ASSERT(ins != NULL);
    CANARD_ASSERT(rxs != NULL);
    CANARD_ASSERT((payload.data != NULL) || (payload.size == 0U));
    CANARD_ASSERT(rxs->payload.size <= extent);  // This invariant is enforced by the subscription logic.
    CANARD_ASSERT(rxs->payload.size <= rxs->total_payload_size);

    rxs->total_payload_size += payload.size;

    // Allocate the payload lazily, as late as possible.
    if ((NULL == rxs->payload.data) && (extent > 0U))
    {
        CANARD_ASSERT(rxs->payload.size == 0);
        rxs->payload.data           = ins->memory.allocate(ins->memory.user_reference, extent);
        rxs->payload.allocated_size = extent;
    }

    int8_t out = 0;
    if (rxs->payload.data != NULL)
    {
        // Copy the payload into the contiguous buffer. Apply the implicit truncation rule if necessary.
        size_t bytes_to_copy = payload.size;
        if ((rxs->payload.size + bytes_to_copy) > extent)
        {
            CANARD_ASSERT(rxs->payload.size <= extent);
            bytes_to_copy = extent - rxs->payload.size;
            CANARD_ASSERT((rxs->payload.size + bytes_to_copy) == extent);
            CANARD_ASSERT(bytes_to_copy < payload.size);
        }
        // This memcpy() call here is one of the two variable-complexity operations in the RX pipeline;
        // the other one is the search of the matching subscription state.
        // Excepting these two cases, the entire RX pipeline contains neither loops nor recursion.
        // Intentional violation of MISRA: indexing on a pointer. This is done to avoid pointer arithmetics.
        // Clang-Tidy raises an error recommending the use of memcpy_s() instead.
        // We ignore it because the safe functions are poorly supported; reliance on them may limit the portability.
        (void) memcpy(((uint8_t*) rxs->payload.data) + rxs->payload.size,  // NOLINT NOSONAR
                      payload.data,
                      bytes_to_copy);
        rxs->payload.size += bytes_to_copy;
        CANARD_ASSERT(rxs->payload.size <= extent);
        CANARD_ASSERT(rxs->payload.size <= rxs->payload.allocated_size);
    }
    else
    {
        CANARD_ASSERT(rxs->payload.size == 0);
        out = (extent > 0U) ? -CANARD_ERROR_OUT_OF_MEMORY : 0;
    }
    CANARD_ASSERT(out <= 0);
    return out;
}

CANARD_PRIVATE void rxSessionRestart(struct CanardInstance* const ins, CanardInternalRxSession* const rxs)
{
    CANARD_ASSERT(ins != NULL);
    CANARD_ASSERT(rxs != NULL);
    // `rxs->payload.data` may be NULL, which is OK.
    ins->memory.deallocate(ins->memory.user_reference, rxs->payload.allocated_size, rxs->payload.data);
    rxs->total_payload_size     = 0U;
    rxs->payload.size           = 0U;
    rxs->payload.data           = NULL;
    rxs->payload.allocated_size = 0U;
    rxs->calculated_crc         = CRC_INITIAL;
    rxs->transfer_id            = (CanardTransferID) ((rxs->transfer_id + 1U) & CANARD_TRANSFER_ID_MAX);
    // The transport index is retained.
    rxs->toggle = INITIAL_TOGGLE_STATE;
}

CANARD_PRIVATE int8_t rxSessionAcceptFrame(struct CanardInstance* const   ins,
                                           CanardInternalRxSession* const rxs,
                                           const RxFrameModel* const      frame,
                                           const size_t                   extent,
                                           struct CanardRxTransfer* const out_transfer)
{
    CANARD_ASSERT(ins != NULL);
    CANARD_ASSERT(rxs != NULL);
    CANARD_ASSERT(frame != NULL);
    CANARD_ASSERT(frame->payload.data != NULL);
    CANARD_ASSERT(frame->transfer_id <= CANARD_TRANSFER_ID_MAX);
    CANARD_ASSERT(out_transfer != NULL);

    if (frame->start_of_transfer)  // The transfer timestamp is the timestamp of its first frame.
    {
        rxs->transfer_timestamp_usec = frame->timestamp_usec;
    }

    const bool single_frame = frame->start_of_transfer && frame->end_of_transfer;
    if (!single_frame)
    {
        // Update the CRC. Observe that the implicit truncation rule may apply here: the payload may be
        // truncated, but its CRC is validated always anyway.
        rxs->calculated_crc = crcAdd(rxs->calculated_crc, frame->payload);
    }

    int8_t out = rxSessionWritePayload(ins, rxs, extent, frame->payload);
    if (out < 0)
    {
        CANARD_ASSERT(-CANARD_ERROR_OUT_OF_MEMORY == out);
        rxSessionRestart(ins, rxs);  // Out-of-memory.
    }
    else if (frame->end_of_transfer)
    {
        CANARD_ASSERT(0 == out);
        if (single_frame || (CRC_RESIDUE == rxs->calculated_crc))
        {
            out = 1;  // One transfer received, notify the application.
            rxInitTransferMetadataFromFrame(frame, &out_transfer->metadata);
            out_transfer->timestamp_usec = rxs->transfer_timestamp_usec;
            out_transfer->payload        = rxs->payload;

            // Cut off the CRC from the payload if it's there -- we don't want to expose it to the user.
            CANARD_ASSERT(rxs->total_payload_size >= rxs->payload.size);
            const size_t truncated_amount = rxs->total_payload_size - rxs->payload.size;
            if ((!single_frame) && (CRC_SIZE_BYTES > truncated_amount))  // Single-frame transfers don't have CRC.
            {
                CANARD_ASSERT(out_transfer->payload.size >= (CRC_SIZE_BYTES - truncated_amount));
                out_transfer->payload.size -= CRC_SIZE_BYTES - truncated_amount;
            }

            // Ownership passed over to the application, nullify to prevent freeing.
            rxs->payload.size           = 0U;
            rxs->payload.data           = NULL;
            rxs->payload.allocated_size = 0U;
        }
        rxSessionRestart(ins, rxs);  // Successful completion.
    }
    else
    {
        rxs->toggle = !rxs->toggle;
    }
    return out;
}

/// Evaluates the state of the RX session with respect to time and the new frame, and restarts it if necessary.
/// The next step is to accept the frame if the transfer-ID, toggle but, and transport index match; reject otherwise.
/// The logic of this function is simple: in the most cases (see below exception) it restarts the reassembler
/// if the start-of-transfer flag is set and any two of the three conditions are met:
///
///     - The frame has arrived over the same interface as the previous transfer.
///     - New transfer-ID is detected.
///     - The transfer-ID timeout has expired.
///
/// The only exception is when the transfer-ID timeout has expired and the new frame has the same transfer-ID as it
/// was expected BUT not on the same transport as before. In this case, the reassembler is "restarted" only
/// if the total payload size is zero (meaning that the reassembler has not been started yet), and so could be more
/// "relaxed" and not so sticky to the transport index. This case was discovered during libcyphal development when
/// multiple redundant transports were used in context of multiple concurrent RPC clients for the same service id.
/// For more information see https://github.com/OpenCyphal/libcanard/issues/228
///
/// Notice that mere TID-timeout is not enough to restart to prevent the interface index oscillation;
/// while this is not visible at the application layer, it may delay the transfer arrival.
CANARD_PRIVATE void rxSessionSynchronize(CanardInternalRxSession* const rxs,
                                         const RxFrameModel* const      frame,
                                         const uint8_t                  redundant_iface_index,
                                         const CanardMicrosecond        transfer_id_timeout_usec)
{
    CANARD_ASSERT(rxs != NULL);
    CANARD_ASSERT(frame != NULL);
    CANARD_ASSERT(rxs->transfer_id <= CANARD_TRANSFER_ID_MAX);
    CANARD_ASSERT(frame->transfer_id <= CANARD_TRANSFER_ID_MAX);

    const bool same_transport = rxs->redundant_iface_index == redundant_iface_index;
    // Examples: rxComputeTransferIDDifference(2, 3)==31
    //           rxComputeTransferIDDifference(2, 2)==0
    //           rxComputeTransferIDDifference(2, 1)==1
    const bool tid_match = rxs->transfer_id == frame->transfer_id;
    const bool tid_new   = rxComputeTransferIDDifference(rxs->transfer_id, frame->transfer_id) > 1;
    // The transfer ID timeout is measured relative to the timestamp of the last start-of-transfer frame.
    const bool tid_timeout = (frame->timestamp_usec > rxs->transfer_timestamp_usec) &&
                             ((frame->timestamp_usec - rxs->transfer_timestamp_usec) > transfer_id_timeout_usec);
    // The total payload size is zero when a new transfer reassembling has not been started yet, hence the idle.
    const bool idle = 0U == rxs->total_payload_size;

    const bool restartable = (same_transport && tid_new) ||      //
                             (same_transport && tid_timeout) ||  //
                             (tid_timeout && tid_new) ||         //
                             (tid_timeout && tid_match && idle);
    // Restarting the transfer reassembly only makes sense if the new frame is a start of transfer.
    // Otherwise, the new transfer would be impossible to reassemble anyway since the first frame is lost.
    if (frame->start_of_transfer && restartable)
    {
        CANARD_ASSERT(frame->start_of_transfer);
        rxs->total_payload_size    = 0U;
        rxs->payload.size          = 0U;  // The buffer is not released because we still need it.
        rxs->calculated_crc        = CRC_INITIAL;
        rxs->transfer_id           = frame->transfer_id;
        rxs->toggle                = INITIAL_TOGGLE_STATE;
        rxs->redundant_iface_index = redundant_iface_index;
    }
}

/// RX session state machine update is the most intricate part of any Cyphal transport implementation.
/// The state model used here is derived from the reference pseudocode given in the original UAVCAN v0 specification.
/// The Cyphal/CAN v1 specification, which this library is an implementation of, does not provide any reference
/// pseudocode. Instead, it takes a higher-level, more abstract approach, where only the high-level requirements
/// are given and the particular algorithms are left to be implementation-defined. Such abstract approach is much
/// advantageous because it allows implementers to choose whatever solution works best for the specific application at
/// hand, while the wire compatibility is still guaranteed by the high-level requirements given in the specification.
CANARD_PRIVATE int8_t rxSessionUpdate(struct CanardInstance* const   ins,
                                      CanardInternalRxSession* const rxs,
                                      const RxFrameModel* const      frame,
                                      const uint8_t                  redundant_iface_index,
                                      const CanardMicrosecond        transfer_id_timeout_usec,
                                      const size_t                   extent,
                                      struct CanardRxTransfer* const out_transfer)
{
    CANARD_ASSERT(ins != NULL);
    CANARD_ASSERT(rxs != NULL);
    CANARD_ASSERT(frame != NULL);
    CANARD_ASSERT(out_transfer != NULL);
    CANARD_ASSERT(rxs->transfer_id <= CANARD_TRANSFER_ID_MAX);
    CANARD_ASSERT(frame->transfer_id <= CANARD_TRANSFER_ID_MAX);
    rxSessionSynchronize(rxs, frame, redundant_iface_index, transfer_id_timeout_usec);
    int8_t out = 0;
    // The purpose of the correct_start check is to reduce the possibility of accepting a malformed multi-frame
    // transfer in the event of a CRC collision. The scenario where this failure mode would manifest is as follows:
    // 1. A valid transfer (whether single- or multi-frame) is accepted with TID=X.
    // 2. All frames of the subsequent multi-frame transfer with TID=X+1 are lost except for the last one.
    // 3. The CRC of said multi-frame transfer happens to yield the correct residue when applied to the fragment
    //    of the payload contained in the last frame of the transfer (a CRC collision is in effect).
    // 4. The last frame of the multi-frame transfer is erroneously accepted even though it is malformed.
    // The correct_start check eliminates this failure mode by ensuring that the first frame is observed.
    // See https://github.com/OpenCyphal/libcanard/issues/189.
    const bool correct_iface  = (rxs->redundant_iface_index == redundant_iface_index);
    const bool correct_toggle = (frame->toggle == rxs->toggle);
    const bool correct_tid    = (frame->transfer_id == rxs->transfer_id);
    const bool correct_start  = frame->start_of_transfer  //
                                    ? (0 == rxs->total_payload_size)
                                    : (rxs->total_payload_size > 0);
    if (correct_iface && correct_toggle && correct_tid && correct_start)
    {
        out = rxSessionAcceptFrame(ins, rxs, frame, extent, out_transfer);
    }
    return out;
}

CANARD_PRIVATE int8_t rxAcceptFrame(struct CanardInstance* const       ins,
                                    struct CanardRxSubscription* const subscription,
                                    const RxFrameModel* const          frame,
                                    const uint8_t                      redundant_iface_index,
                                    struct CanardRxTransfer* const     out_transfer)
{
    CANARD_ASSERT(ins != NULL);
    CANARD_ASSERT(subscription != NULL);
    CANARD_ASSERT(subscription->port_id == frame->port_id);
    CANARD_ASSERT(frame != NULL);
    CANARD_ASSERT(frame->payload.data != NULL);
    CANARD_ASSERT(frame->transfer_id <= CANARD_TRANSFER_ID_MAX);
    CANARD_ASSERT((CANARD_NODE_ID_UNSET == frame->destination_node_id) || (ins->node_id == frame->destination_node_id));
    CANARD_ASSERT(out_transfer != NULL);

    int8_t out = 0;
    if (frame->source_node_id <= CANARD_NODE_ID_MAX)
    {
        // If such session does not exist, create it. This only makes sense if this is the first frame of a
        // transfer, otherwise, we won't be able to receive the transfer anyway so we don't bother.
        if ((NULL == subscription->sessions[frame->source_node_id]) && frame->start_of_transfer)
        {
            CanardInternalRxSession* const rxs =
                (CanardInternalRxSession*) ins->memory.allocate(ins->memory.user_reference,
                                                                sizeof(CanardInternalRxSession));
            subscription->sessions[frame->source_node_id] = rxs;
            if (rxs != NULL)
            {
                rxs->transfer_timestamp_usec = frame->timestamp_usec;
                rxs->total_payload_size      = 0U;
                rxs->payload.size            = 0U;
                rxs->payload.data            = NULL;
                rxs->payload.allocated_size  = 0U;
                rxs->calculated_crc          = CRC_INITIAL;
                rxs->transfer_id             = frame->transfer_id;
                rxs->redundant_iface_index   = redundant_iface_index;
                rxs->toggle                  = INITIAL_TOGGLE_STATE;
            }
            else
            {
                out = -CANARD_ERROR_OUT_OF_MEMORY;
            }
        }
        // There are two possible reasons why the session may not exist: 1. OOM; 2. SOT-miss.
        if (subscription->sessions[frame->source_node_id] != NULL)
        {
            CANARD_ASSERT(out == 0);
            out = rxSessionUpdate(ins,
                                  subscription->sessions[frame->source_node_id],
                                  frame,
                                  redundant_iface_index,
                                  subscription->transfer_id_timeout_usec,
                                  subscription->extent,
                                  out_transfer);
        }
    }
    else
    {
        CANARD_ASSERT(frame->source_node_id == CANARD_NODE_ID_UNSET);
        // Anonymous transfers are stateless. No need to update the state machine, just blindly accept it.
        // We have to copy the data into an allocated storage because the API expects it: the lifetime shall be
        // independent of the input data and the memory shall be free-able.
        const size_t payload_size =
            (subscription->extent < frame->payload.size) ? subscription->extent : frame->payload.size;
        void* const payload_data = ins->memory.allocate(ins->memory.user_reference, payload_size);
        if (payload_data != NULL)
        {
            rxInitTransferMetadataFromFrame(frame, &out_transfer->metadata);
            out_transfer->timestamp_usec         = frame->timestamp_usec;
            out_transfer->payload.size           = payload_size;
            out_transfer->payload.data           = payload_data;
            out_transfer->payload.allocated_size = payload_size;
            // Clang-Tidy raises an error recommending the use of memcpy_s() instead.
            // We ignore it because the safe functions are poorly supported; reliance on them may limit the portability.
            (void) memcpy(payload_data, frame->payload.data, payload_size);  // NOLINT
            out = 1;
        }
        else
        {
            out = -CANARD_ERROR_OUT_OF_MEMORY;
        }
    }
    return out;
}

CANARD_PRIVATE int8_t
rxSubscriptionPredicateOnPortID(void* const user_reference,  // NOSONAR Cavl API requires pointer to non-const.
                                const struct CanardTreeNode* const node)
{
    CANARD_ASSERT((user_reference != NULL) && (node != NULL));
    const CanardPortID  sought    = *((const CanardPortID*) user_reference);
    const CanardPortID  other     = CONTAINER_OF(struct CanardRxSubscription, node, base)->port_id;
    static const int8_t NegPos[2] = {-1, +1};
    // Clang-Tidy mistakenly identifies a narrowing cast to int8_t here, which is incorrect.
    return (sought == other) ? 0 : NegPos[sought > other];  // NOLINT no narrowing conversion is taking place here
}

CANARD_PRIVATE int8_t
rxSubscriptionPredicateOnStruct(void* const user_reference,  // NOSONAR Cavl API requires pointer to non-const.
                                const struct CanardTreeNode* const node)
{
    return rxSubscriptionPredicateOnPortID(  //
        &MUTABLE_CONTAINER_OF(struct CanardRxSubscription, user_reference, base)->port_id,
        node);
}

// --------------------------------------------- PUBLIC API ---------------------------------------------

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

struct CanardInstance canardInit(const struct CanardMemoryResource memory)
{
    CANARD_ASSERT(memory.allocate != NULL);
    CANARD_ASSERT(memory.deallocate != NULL);
    const struct CanardInstance out = {
        .user_reference   = NULL,
        .node_id          = CANARD_NODE_ID_UNSET,
        .memory           = memory,
        .rx_subscriptions = {NULL, NULL, NULL},
    };
    return out;
}

struct CanardTxQueue canardTxInit(const size_t                      capacity,
                                  const size_t                      mtu_bytes,
                                  const struct CanardMemoryResource memory)
{
    CANARD_ASSERT(memory.allocate != NULL);
    CANARD_ASSERT(memory.deallocate != NULL);
    struct CanardTxQueue out = {
        .capacity       = capacity,
        .mtu_bytes      = mtu_bytes,
        .size           = 0,
        .priority_root  = NULL,
        .deadline_root  = NULL,
        .memory         = memory,
        .user_reference = NULL,
    };
    return out;
}

int32_t canardTxPush(struct CanardTxQueue* const                que,
                     const struct CanardInstance* const         ins,
                     const CanardMicrosecond                    tx_deadline_usec,
                     const struct CanardTransferMetadata* const metadata,
                     const struct CanardPayload                 payload,
                     const CanardMicrosecond                    now_usec,
                     uint64_t* const                            frames_expired)
{
    // Before pushing payload (potentially in multiple frames), we need to try to flush any expired transfers.
    // This is necessary to ensure that we don't exhaust the capacity of the queue by holding outdated frames.
    // The flushing is done by comparing deadline timestamps of the pending transfers with the current time,
    // which makes sense only if the current time is known (bigger than zero).
    if (now_usec > 0)
    {
        const size_t count = txFlushExpiredTransfers(que, ins, now_usec);
        if (frames_expired != NULL)
        {
            (*frames_expired) += count;
        }
    }

    int32_t out = -CANARD_ERROR_INVALID_ARGUMENT;
    if ((ins != NULL) && (que != NULL) && (metadata != NULL) && ((payload.data != NULL) || (0U == payload.size)))
    {
        const size_t  pl_mtu       = adjustPresentationLayerMTU(que->mtu_bytes);
        const int32_t maybe_can_id = txMakeCANID(metadata, payload, ins->node_id, pl_mtu);
        if (maybe_can_id >= 0)
        {
            if (payload.size <= pl_mtu)
            {
                out = txPushSingleFrame(que,
                                        ins,
                                        tx_deadline_usec,
                                        (uint32_t) maybe_can_id,
                                        metadata->transfer_id,
                                        payload);
                CANARD_ASSERT((out < 0) || (out == 1));
            }
            else
            {
                out = txPushMultiFrame(que,
                                       ins,
                                       pl_mtu,
                                       tx_deadline_usec,
                                       (uint32_t) maybe_can_id,
                                       metadata->transfer_id,
                                       payload);
                CANARD_ASSERT((out < 0) || (out >= 2));
            }
        }
        else
        {
            out = maybe_can_id;
        }
    }
    CANARD_ASSERT(out != 0);
    return out;
}

struct CanardTxQueueItem* canardTxPeek(const struct CanardTxQueue* const que)
{
    struct CanardTxQueueItem* out = NULL;
    if (que != NULL)
    {
        struct CanardTreeNode* const priority_node = cavlFindExtremum(que->priority_root, false);
        out = MUTABLE_CONTAINER_OF(struct CanardTxQueueItem, priority_node, priority_base);
    }
    return out;
}

struct CanardTxQueueItem* canardTxPop(struct CanardTxQueue* const que, struct CanardTxQueueItem* const item)
{
    if ((que != NULL) && (item != NULL))
    {
        // Paragraph 6.7.2.1.15 of the C standard says:
        //     A pointer to a structure object, suitably converted, points to its initial member, and vice versa.
        // Note that the highest-priority frame is always a leaf node in the AVL tree, which means that it is very
        // cheap to remove.
        cavlRemove(&que->priority_root, &item->priority_base);
        cavlRemove(&que->deadline_root, &item->deadline_base);
        que->size--;
    }
    return item;
}

void canardTxFree(struct CanardTxQueue* const        que,
                  const struct CanardInstance* const ins,
                  struct CanardTxQueueItem*          item)
{
    if ((que != NULL) && (ins != NULL) && (item != NULL))
    {
        if (item->frame.payload.data != NULL)
        {
            que->memory.deallocate(que->memory.user_reference,
                                   item->frame.payload.allocated_size,
                                   item->frame.payload.data);
        }

        ins->memory.deallocate(ins->memory.user_reference, sizeof(struct CanardTxQueueItem), item);
    }
}

int8_t canardTxPoll(struct CanardTxQueue* const        que,
                    const struct CanardInstance* const ins,
                    const CanardMicrosecond            now_usec,
                    void* const                        user_reference,
                    const CanardTxFrameHandler         frame_handler,
                    uint64_t* const                    frames_expired,
                    uint64_t* const                    frames_failed)
{
    int8_t out = -CANARD_ERROR_INVALID_ARGUMENT;
    if ((que != NULL) && (ins != NULL) && (frame_handler != NULL))
    {
        // Before peeking a frame to transmit, we need to try to flush any expired transfers.
        // This will not only ensure ASAP freeing of the queue capacity, but also makes sure that the following
        // `canardTxPeek` will return a not expired item (if any), so we don't need to check the deadline again.
        // The flushing is done by comparing deadline timestamps of the pending transfers with the current time,
        // which makes sense only if the current time is known (bigger than zero).
        if (now_usec > 0)
        {
            const size_t count = txFlushExpiredTransfers(que, ins, now_usec);
            if (frames_expired != NULL)
            {
                (*frames_expired) += count;
            }
        }

        struct CanardTxQueueItem* const tx_item = canardTxPeek(que);
        if (tx_item != NULL)
        {
            // No need to check the deadline again, as we have already flushed all expired transfers.
            out = frame_handler(user_reference, tx_item->tx_deadline_usec, &tx_item->frame);
            // We gonna release (pop and free) the frame if the handler returned:
            // - either a positive value - the frame has been successfully accepted by the handler;
            // - or a negative value - the frame has been rejected by the handler due to a failure.
            // Zero value means that the handler cannot accept the frame at the moment, so we keep it in the queue.
            if (out != 0)
            {
                const bool   failed = out < 0;
                const size_t count  = txPopAndFreeTransfer(que, ins, tx_item, failed);
                if (failed && (frames_failed != NULL))
                {
                    (*frames_failed) += count;
                }
            }
        }
        else
        {
            out = 0;  // No frames to transmit.
        }
    }

    CANARD_ASSERT(out <= 1);
    return out;
}

int8_t canardRxAccept(struct CanardInstance* const        ins,
                      const CanardMicrosecond             timestamp_usec,
                      const struct CanardFrame* const     frame,
                      const uint8_t                       redundant_iface_index,
                      struct CanardRxTransfer* const      out_transfer,
                      struct CanardRxSubscription** const out_subscription)
{
    int8_t out = -CANARD_ERROR_INVALID_ARGUMENT;
    if ((ins != NULL) && (out_transfer != NULL) && (frame != NULL) && (frame->extended_can_id <= CAN_EXT_ID_MASK) &&
        ((frame->payload.data != NULL) || (0 == frame->payload.size)))
    {
        RxFrameModel model = {0};
        if (rxTryParseFrame(timestamp_usec, frame, &model))
        {
            if ((CANARD_NODE_ID_UNSET == model.destination_node_id) || (ins->node_id == model.destination_node_id))
            {
                // This is the reason the function has a logarithmic time complexity of the number of subscriptions.
                // Note also that this one of the two variable-complexity operations in the RX pipeline; the other one
                // is memcpy(). Excepting these two cases, the entire RX pipeline contains neither loops nor recursion.
                struct CanardTreeNode* const sub_node = cavlSearch(&ins->rx_subscriptions[(size_t) model.transfer_kind],
                                                                   &model.port_id,
                                                                   &rxSubscriptionPredicateOnPortID,
                                                                   NULL);
                struct CanardRxSubscription* const sub =
                    MUTABLE_CONTAINER_OF(struct CanardRxSubscription, sub_node, base);
                if (out_subscription != NULL)
                {
                    *out_subscription = sub;  // Expose selected instance to the caller.
                }
                if (sub != NULL)
                {
                    CANARD_ASSERT(sub->port_id == model.port_id);
                    out = rxAcceptFrame(ins, sub, &model, redundant_iface_index, out_transfer);
                }
                else
                {
                    out = 0;  // No matching subscription.
                }
            }
            else
            {
                out = 0;  // Mis-addressed frame (normally it should be filtered out by the hardware).
            }
        }
        else
        {
            out = 0;  // A non-Cyphal/CAN input frame.
        }
    }
    CANARD_ASSERT(out <= 1);
    return out;
}

int8_t canardRxSubscribe(struct CanardInstance* const       ins,
                         const enum CanardTransferKind      transfer_kind,
                         const CanardPortID                 port_id,
                         const size_t                       extent,
                         const CanardMicrosecond            transfer_id_timeout_usec,
                         struct CanardRxSubscription* const out_subscription)
{
    int8_t       out = -CANARD_ERROR_INVALID_ARGUMENT;
    const size_t tk  = (size_t) transfer_kind;
    if ((ins != NULL) && (out_subscription != NULL) && (tk < CANARD_NUM_TRANSFER_KINDS))
    {
        // Reset to the initial state. This is absolutely critical because the new payload size limit may be larger
        // than the old value; if there are any payload buffers allocated, we may overrun them because they are shorter
        // than the new payload limit. So we clear the subscription and thus ensure that no overrun may occur.
        out = canardRxUnsubscribe(ins, transfer_kind, port_id);
        if (out >= 0)
        {
            out_subscription->transfer_id_timeout_usec = transfer_id_timeout_usec;
            out_subscription->extent                   = extent;
            out_subscription->port_id                  = port_id;
            for (size_t i = 0; i < RX_SESSIONS_PER_SUBSCRIPTION; i++)
            {
                // The sessions will be created ad-hoc. Normally, for a low-jitter deterministic system,
                // we could have pre-allocated sessions here, but that requires too much memory to be feasible.
                // We could accept an extra argument that would instruct us to pre-allocate sessions here?
                out_subscription->sessions[i] = NULL;
            }
            const struct CanardTreeNode* const res = cavlSearch(&ins->rx_subscriptions[tk],
                                                                &out_subscription->base,
                                                                &rxSubscriptionPredicateOnStruct,
                                                                &avlTrivialFactory);
            (void) res;
            CANARD_ASSERT(res == &out_subscription->base);
            out = (out > 0) ? 0 : 1;
        }
    }
    return out;
}

int8_t canardRxUnsubscribe(struct CanardInstance* const  ins,
                           const enum CanardTransferKind transfer_kind,
                           const CanardPortID            port_id)
{
    int8_t       out = -CANARD_ERROR_INVALID_ARGUMENT;
    const size_t tk  = (size_t) transfer_kind;
    if ((ins != NULL) && (tk < CANARD_NUM_TRANSFER_KINDS))
    {
        CanardPortID port_id_mutable = port_id;

        struct CanardTreeNode* const sub_node = cavlSearch(  //
            &ins->rx_subscriptions[tk],
            &port_id_mutable,
            &rxSubscriptionPredicateOnPortID,
            NULL);
        if (sub_node != NULL)
        {
            struct CanardRxSubscription* const sub = MUTABLE_CONTAINER_OF(struct CanardRxSubscription, sub_node, base);
            cavlRemove(&ins->rx_subscriptions[tk], sub_node);
            CANARD_ASSERT(sub->port_id == port_id);
            out = 1;
            for (size_t i = 0; i < RX_SESSIONS_PER_SUBSCRIPTION; i++)
            {
                CanardInternalRxSession* const session = sub->sessions[i];
                if (session != NULL)
                {
                    ins->memory.deallocate(ins->memory.user_reference,
                                           session->payload.allocated_size,
                                           session->payload.data);
                    ins->memory.deallocate(ins->memory.user_reference, sizeof(*session), session);
                    sub->sessions[i] = NULL;
                }
            }
        }
        else
        {
            out = 0;
        }
    }
    return out;
}

int8_t canardRxGetSubscription(struct CanardInstance* const        ins,
                               const enum CanardTransferKind       transfer_kind,
                               const CanardPortID                  port_id,
                               struct CanardRxSubscription** const out_subscription)
{
    int8_t       out = -CANARD_ERROR_INVALID_ARGUMENT;
    const size_t tk  = (size_t) transfer_kind;
    if ((ins != NULL) && (tk < CANARD_NUM_TRANSFER_KINDS))
    {
        CanardPortID port_id_mutable = port_id;

        struct CanardTreeNode* const sub_node = cavlSearch(  //
            &ins->rx_subscriptions[tk],
            &port_id_mutable,
            &rxSubscriptionPredicateOnPortID,
            NULL);
        if (sub_node != NULL)
        {
            struct CanardRxSubscription* const sub = MUTABLE_CONTAINER_OF(struct CanardRxSubscription, sub_node, base);
            CANARD_ASSERT(sub->port_id == port_id);
            if (out_subscription != NULL)
            {
                *out_subscription = sub;
            }
            out = 1;
        }
        else
        {
            out = 0;
        }
    }
    return out;
}

struct CanardFilter canardMakeFilterForSubject(const CanardPortID subject_id)
{
    struct CanardFilter out = {0};

    out.extended_can_id = ((uint32_t) subject_id) << OFFSET_SUBJECT_ID;
    out.extended_mask   = FLAG_SERVICE_NOT_MESSAGE | FLAG_RESERVED_07 | (CANARD_SUBJECT_ID_MAX << OFFSET_SUBJECT_ID);

    return out;
}

struct CanardFilter canardMakeFilterForService(const CanardPortID service_id, const CanardNodeID local_node_id)
{
    struct CanardFilter out = {0};

    out.extended_can_id = FLAG_SERVICE_NOT_MESSAGE | (((uint32_t) service_id) << OFFSET_SERVICE_ID) |
                          (((uint32_t) local_node_id) << OFFSET_DST_NODE_ID);
    out.extended_mask = FLAG_SERVICE_NOT_MESSAGE | FLAG_RESERVED_23 | (CANARD_SERVICE_ID_MAX << OFFSET_SERVICE_ID) |
                        (CANARD_NODE_ID_MAX << OFFSET_DST_NODE_ID);

    return out;
}

struct CanardFilter canardMakeFilterForServices(const CanardNodeID local_node_id)
{
    struct CanardFilter out = {0};

    out.extended_can_id = FLAG_SERVICE_NOT_MESSAGE | (((uint32_t) local_node_id) << OFFSET_DST_NODE_ID);
    out.extended_mask   = FLAG_SERVICE_NOT_MESSAGE | FLAG_RESERVED_23 | (CANARD_NODE_ID_MAX << OFFSET_DST_NODE_ID);

    return out;
}

struct CanardFilter canardConsolidateFilters(const struct CanardFilter* a, const struct CanardFilter* b)
{
    struct CanardFilter out = {0};

    out.extended_mask   = a->extended_mask & b->extended_mask & ~(a->extended_can_id ^ b->extended_can_id);
    out.extended_can_id = a->extended_can_id & out.extended_mask;

    return out;
}
