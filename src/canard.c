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
