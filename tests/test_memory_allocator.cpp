#include <gtest/gtest.h>

#include "canard.h"

TEST(MemoryAllocatorTestGroup, CanInitAllocator)
{
    CanardPoolAllocator allocator;
    CanardPoolAllocatorBlock buffer[3];
    canardInitPoolAllocator(&allocator, buffer, 3);

    /* Check that the memory list is constructed correctly. */
    ASSERT_EQ(&buffer[0], allocator.free_list);
    ASSERT_EQ(&buffer[1], allocator.free_list->next);
    ASSERT_EQ(&buffer[2], allocator.free_list->next->next);
}
