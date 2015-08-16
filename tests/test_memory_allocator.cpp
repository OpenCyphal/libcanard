#include <gtest/gtest.h>

#include "canard.h"

class MemoryAllocatorTestGroup: public ::testing::Test {
    protected:
    CanardPoolAllocator allocator;
    CanardPoolAllocatorBlock buffer[3];

    virtual void SetUp()
    {
        canardInitPoolAllocator(&allocator, buffer, 3);
    }

};

TEST_F(MemoryAllocatorTestGroup, FreeListIsConstructedCorrectly)
{
    /* Check that the memory list is constructed correctly. */
    ASSERT_EQ(&buffer[0], allocator.free_list);
    ASSERT_EQ(&buffer[1], allocator.free_list->next);
    ASSERT_EQ(&buffer[2], allocator.free_list->next->next);
    ASSERT_EQ(NULL, allocator.free_list->next->next->next);

}
