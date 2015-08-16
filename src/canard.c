#include "canard.h"

void canardInitPoolAllocator(CanardPoolAllocator *allocator, CanardPoolAllocatorBlock *buf, unsigned int buf_len)
{
    unsigned int current_index = 0;
    CanardPoolAllocatorBlock **current_block = &(allocator->free_list);
    while (current_index < buf_len) {
        *current_block = &buf[current_index];
        current_block = &((*current_block)->next);
        current_index ++;
    }
    *current_block = NULL;
}

void *canardAllocateBlock(CanardPoolAllocator *allocator)
{
    void *result;

    /* Check if there are any blocks available in the free list. */
    if (allocator->free_list == NULL) {
        return NULL;
    }

    /* Take first available block and prepares next block for use. */
    result = allocator->free_list;
    allocator->free_list = allocator->free_list->next;

    return result;
}
