/*
 * Copyright (c) 2016 UAVCAN Team
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Contributors: https://github.com/UAVCAN/libcanard/contributors
 */

#include <gtest/gtest.h>
#include "canard_internals.h"


#define AVAILABLE_BLOCKS 3


class MemoryAllocatorTestGroup: public ::testing::Test
{
protected:
    CanardPoolAllocator allocator;
    CanardPoolAllocatorBlock buffer[AVAILABLE_BLOCKS];

    virtual void SetUp()
    {
        initPoolAllocator(&allocator, buffer, AVAILABLE_BLOCKS);
    }
};

TEST_F(MemoryAllocatorTestGroup, FreeListIsConstructedCorrectly)
{
    // Check that the memory list is constructed correctly.
    ASSERT_EQ(&buffer[0], allocator.free_list);
    ASSERT_EQ(&buffer[1], allocator.free_list->next);
    ASSERT_EQ(&buffer[2], allocator.free_list->next->next);
    ASSERT_EQ(NULL, allocator.free_list->next->next->next);

    // Check statistics
    EXPECT_EQ(AVAILABLE_BLOCKS, allocator.statistics.capacity_blocks);
    EXPECT_EQ(0,                allocator.statistics.current_usage_blocks);
    EXPECT_EQ(0,                allocator.statistics.peak_usage_blocks);
}

TEST_F(MemoryAllocatorTestGroup, CanAllocateBlock)
{
    void* block = allocateBlock(&allocator);

    // Check that the first free memory block was used and that the next block is ready.
    ASSERT_EQ(&buffer[0], block);
    ASSERT_EQ(&buffer[1], allocator.free_list);

    // Check statistics
    EXPECT_EQ(AVAILABLE_BLOCKS, allocator.statistics.capacity_blocks);
    EXPECT_EQ(1,                allocator.statistics.current_usage_blocks);
    EXPECT_EQ(1,                allocator.statistics.peak_usage_blocks);
}

TEST_F(MemoryAllocatorTestGroup, ReturnsNullIfThereIsNoBlockLeft)
{
    // First exhaust all availables block
    for (int i = 0; i < AVAILABLE_BLOCKS; ++i)
    {
        allocateBlock(&allocator);
    }

    // Try to allocate one extra block
    void* block = allocateBlock(&allocator);
    ASSERT_EQ(NULL, block);

    // Check statistics
    EXPECT_EQ(AVAILABLE_BLOCKS, allocator.statistics.capacity_blocks);
    EXPECT_EQ(AVAILABLE_BLOCKS, allocator.statistics.current_usage_blocks);
    EXPECT_EQ(AVAILABLE_BLOCKS, allocator.statistics.peak_usage_blocks);
}

TEST_F(MemoryAllocatorTestGroup, CanFreeBlock)
{
    void* block = allocateBlock(&allocator);

    freeBlock(&allocator, block);

    // Check that the block was added back to the beginning
    ASSERT_EQ(&buffer[0], allocator.free_list);
    ASSERT_EQ(&buffer[1], allocator.free_list->next);

    // Check statistics
    EXPECT_EQ(AVAILABLE_BLOCKS, allocator.statistics.capacity_blocks);
    EXPECT_EQ(0,                allocator.statistics.current_usage_blocks);
    EXPECT_EQ(1,                allocator.statistics.peak_usage_blocks);
}
