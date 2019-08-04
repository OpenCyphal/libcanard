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

#include <catch.hpp>
#include "canard_internals.h"


#define BLOCK_SIZE 32
#define AVAILABLE_BLOCKS 3
#define BUFFER_SIZE (BLOCK_SIZE*AVAILABLE_BLOCKS)

struct MockBlock {
    uint8_t data[BLOCK_SIZE];
};

TEST_CASE("MemoryAllocatorTestGroup, FreeListIsConstructedCorrectly")
{
    CanardPoolAllocator allocator;
    struct MockBlock buffer[AVAILABLE_BLOCKS];
    initPoolAllocator(&allocator, buffer, sizeof(buffer), BLOCK_SIZE);

    // Check that the memory list is constructed correctly.
    REQUIRE(reinterpret_cast<CanardPoolAllocatorFreeBlock*>(&buffer[0]) == allocator.free_list);
    REQUIRE(reinterpret_cast<CanardPoolAllocatorFreeBlock*>(&buffer[1]) == allocator.free_list->next);
    REQUIRE(reinterpret_cast<CanardPoolAllocatorFreeBlock*>(&buffer[2]) == allocator.free_list->next->next);
    REQUIRE(NULL == allocator.free_list->next->next->next);

    // Check statistics
    REQUIRE(AVAILABLE_BLOCKS == allocator.statistics.capacity_blocks);
    REQUIRE(0 ==                allocator.statistics.current_usage_blocks);
    REQUIRE(0 ==                allocator.statistics.peak_usage_blocks);
}

TEST_CASE("MemoryAllocatorTestGroup, CanAllocateBlock")
{
    CanardPoolAllocator allocator;
    struct MockBlock buffer[AVAILABLE_BLOCKS];
    initPoolAllocator(&allocator, buffer, sizeof(buffer), BLOCK_SIZE);

    void* block = allocateBlock(&allocator);

    // Check that the first free memory block was used and that the next block is ready.
    REQUIRE(reinterpret_cast<CanardPoolAllocatorFreeBlock*>(&buffer[0]) == block);
    REQUIRE(reinterpret_cast<CanardPoolAllocatorFreeBlock*>(&buffer[1]) == allocator.free_list);

    // Check statistics
    REQUIRE(AVAILABLE_BLOCKS == allocator.statistics.capacity_blocks);
    REQUIRE(1 ==                allocator.statistics.current_usage_blocks);
    REQUIRE(1 ==                allocator.statistics.peak_usage_blocks);
}

TEST_CASE("MemoryAllocatorTestGroup, ReturnsNullIfThereIsNoBlockLeft")
{
    CanardPoolAllocator allocator;
    struct MockBlock buffer[AVAILABLE_BLOCKS];
    initPoolAllocator(&allocator, buffer, sizeof(buffer), BLOCK_SIZE);

    // First exhaust all availables block
    for (int i = 0; i < AVAILABLE_BLOCKS; ++i)
    {
        allocateBlock(&allocator);
    }

    // Try to allocate one extra block
    void* block = allocateBlock(&allocator);
    REQUIRE(NULL == block);

    // Check statistics
    REQUIRE(AVAILABLE_BLOCKS == allocator.statistics.capacity_blocks);
    REQUIRE(AVAILABLE_BLOCKS == allocator.statistics.current_usage_blocks);
    REQUIRE(AVAILABLE_BLOCKS == allocator.statistics.peak_usage_blocks);
}

TEST_CASE("MemoryAllocatorTestGroup, CanFreeBlock")
{
    CanardPoolAllocator allocator;
    struct MockBlock buffer[AVAILABLE_BLOCKS];
    initPoolAllocator(&allocator, buffer, sizeof(buffer), BLOCK_SIZE);

    void* block = allocateBlock(&allocator);

    freeBlock(&allocator, block);

    // Check that the block was added back to the beginning
    REQUIRE(reinterpret_cast<CanardPoolAllocatorFreeBlock*>(&buffer[0]) == allocator.free_list);
    REQUIRE(reinterpret_cast<CanardPoolAllocatorFreeBlock*>(&buffer[1]) == allocator.free_list->next);

    // Check statistics
    REQUIRE(AVAILABLE_BLOCKS == allocator.statistics.capacity_blocks);
    REQUIRE(0 ==                allocator.statistics.current_usage_blocks);
    REQUIRE(1 ==                allocator.statistics.peak_usage_blocks);
}
