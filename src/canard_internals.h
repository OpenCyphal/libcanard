#ifndef CANARD_INTERNALS_H
#define CANARD_INTERNALS_H

#include <unistd.h>
//#include "canard.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CANARD_INTERNAL
# define CANARD_INTERNAL static
#endif

#define TRANSFER_TIMEOUT_USEC 2000000

#define TRANSFER_ID_BIT_LEN 5
#define CANARD_EXT_ID_MASK       0x1FFFFFFFU
#define CANARD_CAN_FRAME_EFF    (1U << 31)  //extended frame format
#define CANARD_CAN_FRAME_RTR    (1U << 30)  //remote transmission
#define CANARD_CAN_FRAME_ERR    (1U << 29)  //error frame

#define CANARD_SOURCE_ID_FROM_ID(x) (x) & (0X7F)
#define CANARD_SERVICE_NOT_MSG_FROM_ID(x)   (((x) >> 7)  & 0X1)
#define CANARD_REQUEST_NOT_RESPONSE_FROM_ID(x) (((x) >> 15) & 0X1)
#define CANARD_DEST_ID_FROM_ID(x)   (((x) >> 8)  & 0X7F)
#define CANARD_PRIORITY_FROM_ID(x)  (((x) >> 24) & 0X1F)
#define CANARD_MSG_TYPE_FROM_ID(x)  (((x) >> 8)  & 0XFFFF)
#define CANARD_SRV_TYPE_FROM_ID(x)  (((x) >> 16)  & 0XFF)

#define TRANSFER_ID_FROM_TAIL_BYTE(x) ((x) & 0X1F)

#define IS_START_OF_TRANSFER(x) (((x) >> 7) & 0X1)
#define IS_END_OF_TRANSFER(x) (((x) >> 6) & 0X1)
#define TOGGLE_BIT(x) (((x) >> 5) & 0X1)


CANARD_INTERNAL CanardRxState *canardRxStateTraversal(CanardInstance* ins, uint32_t transfer_descriptor);
CANARD_INTERNAL CanardRxState *canardCreateRxState(CanardPoolAllocator* allocator, uint32_t transfer_descriptor);
// CANARD_INTERNAL CanardRxState *canardAppendRxState(CanardInstance* ins, uint32_t transfer_descriptor);
CANARD_INTERNAL CanardRxState *canardPrependRxState(CanardInstance* ins, uint32_t transfer_descriptor);
CANARD_INTERNAL CanardRxState *canardFindRxState(CanardRxState* state, uint32_t transfer_descriptor);
CANARD_INTERNAL void canardBufferBlockPushBytes(CanardPoolAllocator* allocator, CanardRxState* state, const uint8_t* data, uint8_t data_len);
CANARD_INTERNAL CanardBufferBlock *canardCreateBufferBlock(CanardPoolAllocator* allocator);
CANARD_INTERNAL uint8_t canardTransferType(uint32_t id);
CANARD_INTERNAL uint16_t canardDataType(uint32_t id);
CANARD_INTERNAL void canardPushTxQueue(CanardInstance* ins, CanardTxQueueItem* item);
CANARD_INTERNAL bool priorityHigherThan(uint32_t id, uint32_t rhs);
CANARD_INTERNAL CanardTxQueueItem *canardCreateTxItem(CanardPoolAllocator* allocator);
CANARD_INTERNAL void canardPrepareForNextTransfer(CanardRxState* state);
CANARD_INTERNAL int computeForwardDistance(uint8_t a, uint8_t b);
CANARD_INTERNAL void tidIncrement(uint8_t* transfer_id);
CANARD_INTERNAL uint64_t canardReleaseStatePayload(CanardInstance* ins, CanardRxState* rxstate);
CANARD_INTERNAL int canardEnqueueData(CanardInstance* ins, uint32_t can_id, uint8_t* transfer_id, const uint8_t* payload, uint16_t payload_len);

/** Inits a memory allocator.
 *
 * @param [in] allocator The memory allocator to initialize.
 * @param [in] buf The buffer used by the memory allocator.
 * @param [in] buf_len The number of blocks in buf.
 */
CANARD_INTERNAL void canardInitPoolAllocator(CanardPoolAllocator* allocator, CanardPoolAllocatorBlock* buf,
                                             unsigned int buf_len);

/** Allocates a block from the given pool allocator. */
CANARD_INTERNAL void* canardAllocateBlock(CanardPoolAllocator* allocator);

/** Frees a memory block previously returned by canardAllocateBlock. */
CANARD_INTERNAL void canardFreeBlock(CanardPoolAllocator* allocator, void* p);

#ifdef __cplusplus
}
#endif
#endif
