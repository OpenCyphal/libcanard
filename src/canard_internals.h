#ifndef CANARD_INTERNALS_H
#define CANARD_INTERNALS_H

#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CANARD_INTERNAL
# define CANARD_INTERNAL static
#endif

/** A memory block used in the memory block allocator. */
typedef union CanardPoolAllocatorBlock_u
{
    char bytes[CANARD_MEM_BLOCK_SIZE];
    union CanardPoolAllocatorBlock_u* next;
} CanardPoolAllocatorBlock;

typedef struct
{
    CanardPoolAllocatorBlock* free_list;
} CanardPoolAllocator;

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
