#include <gtest/gtest.h>

#include "canard.h"

#define AVAILABLE_BLOCKS 3

class MemoryAllocatorTestGroup: public ::testing::Test {
    protected:
    CanardPoolAllocator allocator;
    CanardPoolAllocatorBlock buffer[AVAILABLE_BLOCKS];

    virtual void SetUp()
    {
        canardInitPoolAllocator(&allocator, buffer, AVAILABLE_BLOCKS);
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

TEST_F(MemoryAllocatorTestGroup, CanAllocateBlock)
{
    void *block;

    block = canardAllocateBlock(&allocator);

    // Check that the first free memory block was used and that the next block
    // is ready.
    ASSERT_EQ(&buffer[0], block);
    ASSERT_EQ(&buffer[1], allocator.free_list);
}

TEST_F(MemoryAllocatorTestGroup, ReturnsNullIfThereIsNoBlockLeft)
{
    void *block;

    // First exhaust all availables block
    for (int i = 0; i < AVAILABLE_BLOCKS; ++i) {
        canardAllocateBlock(&allocator);
    }

    // Try to allocate one extra block
    block = canardAllocateBlock(&allocator);
    ASSERT_EQ(NULL, block);
}

