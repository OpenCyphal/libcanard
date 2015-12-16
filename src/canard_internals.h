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


CANARD_INTERNAL CanardRxState *canardRxStateTraversal(CanardInstance* ins, const CanardCANFrame* frame, uint32_t transfer_descriptor);
CANARD_INTERNAL CanardRxState *canardCreateRxState(CanardPoolAllocator* allocator, uint32_t transfer_descriptor);
CANARD_INTERNAL CanardRxState *canardAppendRxState(CanardInstance* ins, uint32_t transfer_descriptor);
CANARD_INTERNAL CanardRxState *canardFindRxState(CanardRxState* state, uint32_t transfer_descriptor);
CANARD_INTERNAL void canardPrintStates(CanardInstance* ins);
CANARD_INTERNAL void canardBufferBlockPushBytes(CanardPoolAllocator* allocator, CanardRxState* state, const uint8_t* data, uint8_t data_len);
CANARD_INTERNAL CanardBufferBlock *canardCreateBufferBlock(CanardPoolAllocator* allocator);
CANARD_INTERNAL uint8_t canardTransferType(uint32_t id);
CANARD_INTERNAL uint16_t canardDataType(uint32_t id);
CANARD_INTERNAL void canardPushTxQueue(CanardInstance* ins, CanardTxQueueItem* item);
CANARD_INTERNAL bool priorityHigherThan(uint32_t id, uint32_t rhs);
CANARD_INTERNAL void canardPrintQueue(CanardInstance* ins);
CANARD_INTERNAL CanardTxQueueItem *canardCreateTxItem(CanardPoolAllocator* allocator);
CANARD_INTERNAL void cleanCanardRxState(CanardRxState* state);

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