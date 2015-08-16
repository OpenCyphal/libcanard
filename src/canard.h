#ifndef CANARD_H
#define CANARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <unistd.h>

/** The size of a memory block in bytes. */
#define CANARD_MEM_BLOCK_SIZE 32

/** A memory block used in the memory block allocator. */
typedef union CanardPoolAllocatorBlock_u {
    char bytes[CANARD_MEM_BLOCK_SIZE];
    union CanardPoolAllocatorBlock_u *next;
} CanardPoolAllocatorBlock;

typedef struct {
    CanardPoolAllocatorBlock *free_list;
} CanardPoolAllocator;

/** Inits a memory allocator.
 *
 * @param [in] allocator The memory allocator to initialize.
 * @param [in] buf The buffer used by the memory allocator.
 * @param [in] buf_len The number of blocks in buf.
 */
void canardInitPoolAllocator(CanardPoolAllocator *allocator, CanardPoolAllocatorBlock *buf, unsigned int buf_len);

/** Allocates a block from the given pool allocator. */
void *canardAllocateBlock(CanardPoolAllocator *allocator);

#ifdef __cplusplus
}
#endif

#endif
