// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal Development Team.

#include "canard.c" // NOLINT(bugprone-suspicious-include)
#include "helpers.h"
#include <unity.h>

// Helper to count frames in a chain.
static size_t count_frames(const tx_frame_t* head)
{
    size_t count = 0;
    while (head != NULL) {
        count++;
        head = head->next;
    }
    return count;
}

// Helper to free a frame chain.
static void free_frames(tx_frame_t* head)
{
    while (head != NULL) {
        tx_frame_t* const next = head->next;
        canard_refcount_dec(tx_frame_view(head));
        head = next;
    }
}

// ==============================================  tx_spool (Cyphal v1)  ==============================================

static void test_tx_spool_single_frame_empty(void)
{
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t                     queue_size = 0;
    const canard_bytes_chain_t payload    = { .bytes = { .size = 0, .data = NULL }, .next = NULL };

    // Empty payload, MTU=8 (Classic CAN). Expect 1 frame with just tail byte.
    tx_frame_t* head = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 5, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1, count_frames(head));
    TEST_ASSERT_EQUAL_size_t(1, queue_size);
    TEST_ASSERT_EQUAL_size_t(1, head->size); // Just tail byte, rounded to 1.
    // Tail: SOT=1, EOT=1, toggle=1, tid=5 => 0x80|0x40|0x20|0x05 = 0xE5.
    TEST_ASSERT_EQUAL_HEX8(0xE5, head->data[0]);

    free_frames(head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_single_frame_small(void)
{
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t               queue_size = 0;
    const uint8_t        data[]     = { 0xDE, 0xAD, 0xBE, 0xEF };
    canard_bytes_chain_t payload    = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    // 4 bytes payload, MTU=8. Fits in single frame (4 < 7).
    tx_frame_t* head = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 17, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1, count_frames(head));
    // Frame size: ceil(4+1)=5 bytes.
    TEST_ASSERT_EQUAL_size_t(5, head->size);
    TEST_ASSERT_EQUAL_HEX8(0xDE, head->data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xAD, head->data[1]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, head->data[2]);
    TEST_ASSERT_EQUAL_HEX8(0xEF, head->data[3]);
    // Tail: SOT=1, EOT=1, toggle=1, tid=17 => 0x80|0x40|0x20|0x11 = 0xF1.
    TEST_ASSERT_EQUAL_HEX8(0xF1, head->data[4]);

    free_frames(head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_single_frame_max_classic(void)
{
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t               queue_size = 0;
    const uint8_t        data[]     = { 1, 2, 3, 4, 5, 6, 7 }; // 7 bytes, max single-frame for Classic CAN.
    canard_bytes_chain_t payload    = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 0, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1, count_frames(head));
    // Frame size: 7 payload + 1 tail = 8 bytes.
    TEST_ASSERT_EQUAL_size_t(8, head->size);
    for (size_t i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[i]);
    }
    // Tail: SOT=1, EOT=1, toggle=1, tid=0 => 0xE0.
    TEST_ASSERT_EQUAL_HEX8(0xE0, head->data[7]);

    free_frames(head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_single_multi_boundary(void)
{
    // Test the exact boundary between single-frame and multi-frame: 8 bytes payload.
    // 8 bytes >= MTU-1 (7), so it becomes multi-frame with CRC.
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t               queue_size = 0;
    const uint8_t        data[]     = { 0, 1, 2, 3, 4, 5, 6, 7 }; // Exactly 8 bytes.
    canard_bytes_chain_t payload    = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 0, payload);
    TEST_ASSERT_NOT_NULL(head);
    // 8 bytes + 2 CRC = 10 bytes. At 7 bytes/frame: ceil(10/7) = 2 frames.
    TEST_ASSERT_EQUAL_size_t(2, count_frames(head));

    // Frame 1: 7 bytes payload + tail.
    TEST_ASSERT_EQUAL_size_t(8, head->size);
    for (size_t i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[i]);
    }
    // Tail: SOT=1, EOT=0, toggle=1, tid=0 => 0xA0.
    TEST_ASSERT_EQUAL_HEX8(0xA0, head->data[7]);

    // Frame 2: 1 byte payload + 2 CRC + tail = 4 bytes.
    tx_frame_t* frame2 = head->next;
    TEST_ASSERT_NOT_NULL(frame2);
    TEST_ASSERT_EQUAL_size_t(4, frame2->size);
    TEST_ASSERT_EQUAL_HEX8(data[7], frame2->data[0]);
    uint16_t expected_crc = crc_add(CRC_INITIAL, 8, data);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc >> 8U), frame2->data[1]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc & 0xFFU), frame2->data[2]);
    // Tail: SOT=0, EOT=1, toggle=0, tid=0 => 0x40.
    TEST_ASSERT_EQUAL_HEX8(0x40, frame2->data[3]);

    free_frames(head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_single_frame_fd(void)
{
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t               queue_size = 0;
    const uint8_t        data[20]   = { 0 }; // 20 bytes payload, MTU=64. Single frame.
    canard_bytes_chain_t payload    = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_FD, 31, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1, count_frames(head));
    // Frame size: ceil(20+1)=21 -> rounded to 24.
    TEST_ASSERT_EQUAL_size_t(24, head->size);
    // Tail at position 23: SOT=1, EOT=1, toggle=1, tid=31 => 0xFF.
    TEST_ASSERT_EQUAL_HEX8(0xFF, head->data[23]);
    // Check padding (positions 20, 21, 22).
    TEST_ASSERT_EQUAL_HEX8(0x00, head->data[20]);
    TEST_ASSERT_EQUAL_HEX8(0x00, head->data[21]);
    TEST_ASSERT_EQUAL_HEX8(0x00, head->data[22]);

    free_frames(head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_fd_dlc_rounding(void)
{
    // Test various CAN FD DLC rounding cases: valid sizes are 8, 12, 16, 20, 24, 32, 48, 64.
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    // Test: 9 bytes payload + 1 tail = 10 -> rounds to 12.
    {
        size_t               queue_size = 0;
        const uint8_t        data[9]    = { 0 };
        canard_bytes_chain_t payload    = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
        tx_frame_t*          head       = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_FD, 0, payload);
        TEST_ASSERT_NOT_NULL(head);
        TEST_ASSERT_EQUAL_size_t(12, head->size);
        // Tail at position 11.
        TEST_ASSERT_EQUAL_HEX8(0xE0, head->data[11]);
        free_frames(head);
    }

    // Test: 13 bytes payload + 1 tail = 14 -> rounds to 16.
    {
        size_t               queue_size = 0;
        const uint8_t        data[13]   = { 0 };
        canard_bytes_chain_t payload    = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
        tx_frame_t*          head       = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_FD, 0, payload);
        TEST_ASSERT_NOT_NULL(head);
        TEST_ASSERT_EQUAL_size_t(16, head->size);
        TEST_ASSERT_EQUAL_HEX8(0xE0, head->data[15]);
        free_frames(head);
    }

    // Test: 25 bytes payload + 1 tail = 26 -> rounds to 32.
    {
        size_t               queue_size = 0;
        const uint8_t        data[25]   = { 0 };
        canard_bytes_chain_t payload    = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
        tx_frame_t*          head       = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_FD, 0, payload);
        TEST_ASSERT_NOT_NULL(head);
        TEST_ASSERT_EQUAL_size_t(32, head->size);
        TEST_ASSERT_EQUAL_HEX8(0xE0, head->data[31]);
        free_frames(head);
    }

    // Test: 33 bytes payload + 1 tail = 34 -> rounds to 48.
    {
        size_t               queue_size = 0;
        const uint8_t        data[33]   = { 0 };
        canard_bytes_chain_t payload    = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
        tx_frame_t*          head       = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_FD, 0, payload);
        TEST_ASSERT_NOT_NULL(head);
        TEST_ASSERT_EQUAL_size_t(48, head->size);
        TEST_ASSERT_EQUAL_HEX8(0xE0, head->data[47]);
        free_frames(head);
    }

    // Test: 49 bytes payload + 1 tail = 50 -> rounds to 64.
    {
        size_t               queue_size = 0;
        const uint8_t        data[49]   = { 0 };
        canard_bytes_chain_t payload    = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
        tx_frame_t*          head       = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_FD, 0, payload);
        TEST_ASSERT_NOT_NULL(head);
        TEST_ASSERT_EQUAL_size_t(64, head->size);
        TEST_ASSERT_EQUAL_HEX8(0xE0, head->data[63]);
        free_frames(head);
    }

    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_multi_frame_classic(void)
{
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t queue_size = 0;
    // 10 bytes payload forces multi-frame with MTU=8 (7 bytes payload per frame max).
    const uint8_t        data[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 7, payload);
    TEST_ASSERT_NOT_NULL(head);
    // 10 bytes + 2 CRC = 12 bytes. At 7 bytes/frame: ceil(12/7) = 2 frames.
    TEST_ASSERT_EQUAL_size_t(2, count_frames(head));

    // Frame 1: 7 bytes payload + tail.
    TEST_ASSERT_EQUAL_size_t(8, head->size);
    for (size_t i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[i]);
    }
    // Tail: SOT=1, EOT=0, toggle=1, tid=7 => 0x80|0x20|0x07 = 0xA7.
    TEST_ASSERT_EQUAL_HEX8(0xA7, head->data[7]);

    // Frame 2: 3 bytes payload + 2 CRC + tail = 6 bytes.
    tx_frame_t* frame2 = head->next;
    TEST_ASSERT_NOT_NULL(frame2);
    TEST_ASSERT_EQUAL_size_t(6, frame2->size);
    TEST_ASSERT_EQUAL_HEX8(data[7], frame2->data[0]);
    TEST_ASSERT_EQUAL_HEX8(data[8], frame2->data[1]);
    TEST_ASSERT_EQUAL_HEX8(data[9], frame2->data[2]);
    // CRC at positions 3, 4 (big-endian). No padding needed.
    uint16_t expected_crc = crc_add(CRC_INITIAL, 10, data);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc >> 8U), frame2->data[3]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc & 0xFFU), frame2->data[4]);
    // Tail: SOT=0, EOT=1, toggle=0, tid=7 => 0x40|0x07 = 0x47.
    TEST_ASSERT_EQUAL_HEX8(0x47, frame2->data[5]);

    free_frames(head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_multi_frame_three_frames(void)
{
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t queue_size = 0;
    // 20 bytes + 2 CRC = 22. At 7 bytes/frame: ceil(22/7) = 4 frames.
    const uint8_t        data[20] = { 0 };
    canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 0, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(4, count_frames(head));

    // Verify tail bytes toggle pattern: 1, 0, 1, 0.
    tx_frame_t* f = head;
    // Frame 1: SOT=1, EOT=0, toggle=1 => 0xA0.
    TEST_ASSERT_EQUAL_HEX8(0xA0, f->data[f->size - 1]);
    f = f->next;
    // Frame 2: SOT=0, EOT=0, toggle=0 => 0x00.
    TEST_ASSERT_EQUAL_HEX8(0x00, f->data[f->size - 1]);
    f = f->next;
    // Frame 3: SOT=0, EOT=0, toggle=1 => 0x20.
    TEST_ASSERT_EQUAL_HEX8(0x20, f->data[f->size - 1]);
    f = f->next;
    // Frame 4: SOT=0, EOT=1, toggle=0 => 0x40.
    TEST_ASSERT_EQUAL_HEX8(0x40, f->data[f->size - 1]);

    free_frames(head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_multi_frame_fd(void)
{
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t queue_size = 0;
    // 100 bytes payload, MTU=64. 100 + 2 CRC = 102. At 63 bytes/frame: ceil(102/63) = 2 frames.
    uint8_t data[100];
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = (uint8_t)i;
    }
    canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_FD, 15, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(2, count_frames(head));

    // Frame 1: 63 bytes payload + tail = 64 bytes.
    TEST_ASSERT_EQUAL_size_t(64, head->size);
    for (size_t i = 0; i < 63; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[i]);
    }
    // Tail: SOT=1, EOT=0, toggle=1, tid=15 => 0xAF.
    TEST_ASSERT_EQUAL_HEX8(0xAF, head->data[63]);

    // Frame 2: 37 bytes payload + 2 CRC + tail. Size: 37+2+1=40 -> rounded to 48.
    tx_frame_t* frame2 = head->next;
    TEST_ASSERT_NOT_NULL(frame2);
    TEST_ASSERT_EQUAL_size_t(48, frame2->size);
    for (size_t i = 0; i < 37; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[63 + i], frame2->data[i]);
    }
    // Padding at positions 37..44.
    for (size_t i = 37; i < 45; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00, frame2->data[i]);
    }
    // CRC at positions 45, 46.
    uint16_t expected_crc = crc_add(CRC_INITIAL, 100, data);
    for (size_t i = 0; i < 8; i++) {
        expected_crc = crc_add_byte(expected_crc, 0x00);
    }
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc >> 8U), frame2->data[45]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc & 0xFFU), frame2->data[46]);
    // Tail: SOT=0, EOT=1, toggle=0, tid=15 => 0x4F.
    TEST_ASSERT_EQUAL_HEX8(0x4F, frame2->data[47]);

    free_frames(head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_fragmented_payload(void)
{
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t               queue_size = 0;
    const uint8_t        f1[]       = { 0xAA, 0xBB };
    const uint8_t        f2[]       = { 0xCC, 0xDD, 0xEE };
    canard_bytes_chain_t c2         = { .bytes = { .size = sizeof(f2), .data = f2 }, .next = NULL };
    canard_bytes_chain_t c1         = { .bytes = { .size = sizeof(f1), .data = f1 }, .next = &c2 };

    tx_frame_t* head = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 3, c1);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1, count_frames(head));
    // 5 bytes payload + 1 tail = 6 bytes.
    TEST_ASSERT_EQUAL_size_t(6, head->size);
    TEST_ASSERT_EQUAL_HEX8(0xAA, head->data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, head->data[1]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, head->data[2]);
    TEST_ASSERT_EQUAL_HEX8(0xDD, head->data[3]);
    TEST_ASSERT_EQUAL_HEX8(0xEE, head->data[4]);
    // Tail: SOT=1, EOT=1, toggle=1, tid=3 => 0xE3.
    TEST_ASSERT_EQUAL_HEX8(0xE3, head->data[5]);

    free_frames(head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_crc_split(void)
{
    // Test CRC split across frames: first CRC byte in second-to-last frame, second in last frame.
    // With MTU=8 (7 payload bytes per frame), 13 bytes payload + 2 CRC = 15 bytes total.
    // Frame 1: 7 bytes payload (0-6), tail
    // Frame 2: 6 bytes payload (7-12) + 1 CRC high byte, tail
    // Frame 3: 1 CRC low byte, tail
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t               queue_size = 0;
    const uint8_t        data[13]   = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
    canard_bytes_chain_t payload    = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 0, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(3, count_frames(head));

    // CRC over payload (no padding in this case).
    uint16_t expected_crc = crc_add(CRC_INITIAL, 13, data);

    // Frame 1: 7 bytes payload + tail.
    TEST_ASSERT_EQUAL_size_t(8, head->size);
    for (size_t i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[i]);
    }
    // Tail: SOT=1, EOT=0, toggle=1, tid=0 => 0xA0.
    TEST_ASSERT_EQUAL_HEX8(0xA0, head->data[7]);

    // Frame 2: 6 bytes payload + CRC high byte + tail.
    tx_frame_t* frame2 = head->next;
    TEST_ASSERT_NOT_NULL(frame2);
    TEST_ASSERT_EQUAL_size_t(8, frame2->size);
    for (size_t i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[7 + i], frame2->data[i]);
    }
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc >> 8U), frame2->data[6]); // CRC high byte.
    // Tail: SOT=0, EOT=0, toggle=0, tid=0 => 0x00.
    TEST_ASSERT_EQUAL_HEX8(0x00, frame2->data[7]);

    // Frame 3: CRC low byte + tail.
    tx_frame_t* frame3 = frame2->next;
    TEST_ASSERT_NOT_NULL(frame3);
    TEST_ASSERT_EQUAL_size_t(2, frame3->size);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc & 0xFFU), frame3->data[0]); // CRC low byte.
    // Tail: SOT=0, EOT=1, toggle=1, tid=0 => 0x60.
    TEST_ASSERT_EQUAL_HEX8(0x60, frame3->data[1]);

    free_frames(head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_crc_only_last_frame(void)
{
    // Test case where the last frame contains ONLY CRC (no payload).
    // With MTU=8 (7 payload bytes per frame), 14 bytes payload + 2 CRC = 16 bytes total.
    // Frame 1: 7 bytes payload (0-6), tail
    // Frame 2: 7 bytes payload (7-13), tail
    // Frame 3: 2 bytes CRC only, tail
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t               queue_size = 0;
    const uint8_t        data[14]   = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
    canard_bytes_chain_t payload    = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 0, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(3, count_frames(head));

    // CRC over payload (no padding).
    uint16_t expected_crc = crc_add(CRC_INITIAL, 14, data);

    // Frame 1: 7 bytes payload + tail.
    TEST_ASSERT_EQUAL_size_t(8, head->size);
    for (size_t i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[i]);
    }
    // Tail: SOT=1, EOT=0, toggle=1, tid=0 => 0xA0.
    TEST_ASSERT_EQUAL_HEX8(0xA0, head->data[7]);

    // Frame 2: 7 bytes payload + tail = 8 bytes.
    tx_frame_t* frame2 = head->next;
    TEST_ASSERT_NOT_NULL(frame2);
    TEST_ASSERT_EQUAL_size_t(8, frame2->size);
    for (size_t i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[7 + i], frame2->data[i]);
    }
    // Tail: SOT=0, EOT=0, toggle=0, tid=0 => 0x00.
    TEST_ASSERT_EQUAL_HEX8(0x00, frame2->data[7]);

    // Frame 3: CRC only (2 bytes) + tail = 3 bytes.
    tx_frame_t* frame3 = frame2->next;
    TEST_ASSERT_NOT_NULL(frame3);
    TEST_ASSERT_EQUAL_size_t(3, frame3->size);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc >> 8U), frame3->data[0]);   // CRC high byte.
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc & 0xFFU), frame3->data[1]); // CRC low byte.
    // Tail: SOT=0, EOT=1, toggle=1, tid=0 => 0x60.
    TEST_ASSERT_EQUAL_HEX8(0x60, frame3->data[2]);

    free_frames(head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_oom(void)
{
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    alloc.limit_bytes      = 0; // No memory available.
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t               queue_size = 0;
    const uint8_t        data[]     = { 1, 2, 3 };
    canard_bytes_chain_t payload    = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 0, payload);
    TEST_ASSERT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(0, queue_size);
}

static void test_tx_spool_oom_mid_chain(void)
{
    // 20 bytes payload requires 4 frames. Fail on the 3rd allocation.
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    alloc.limit_fragments  = 2; // Allow only 2 allocations.
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t               queue_size = 0;
    const uint8_t        data[20]   = { 0 };
    canard_bytes_chain_t payload    = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(mem, &queue_size, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 0, payload);
    TEST_ASSERT_NULL(head);
    // Verify that all allocated frames were properly freed.
    TEST_ASSERT_EQUAL_size_t(0, queue_size);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_bytes);
    // Verify that 2 successful allocs + 1 failed alloc + 2 frees occurred.
    TEST_ASSERT_EQUAL_UINT64(3, alloc.count_alloc);
    TEST_ASSERT_EQUAL_UINT64(2, alloc.count_free);
}

// =============================================  tx_spool_v0 (UAVCAN v0)  =============================================

static void test_tx_spool_v0_single_frame_empty(void)
{
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t                     queue_size = 0;
    const canard_bytes_chain_t payload    = { .bytes = { .size = 0, .data = NULL }, .next = NULL };

    tx_frame_t* head = tx_spool_v0(mem, &queue_size, 0xFFFF, 5, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1, count_frames(head));
    TEST_ASSERT_EQUAL_size_t(1, head->size);
    // Tail: SOT=1, EOT=1, toggle=0, tid=5 => 0x80|0x40|0x05 = 0xC5 (v0 starts toggle=0).
    TEST_ASSERT_EQUAL_HEX8(0xC5, head->data[0]);

    free_frames(head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_v0_single_frame_max(void)
{
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t               queue_size = 0;
    const uint8_t        data[]     = { 1, 2, 3, 4, 5, 6, 7 }; // 7 bytes, single frame (7 < 8).
    canard_bytes_chain_t payload    = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool_v0(mem, &queue_size, 0xFFFF, 31, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1, count_frames(head));
    TEST_ASSERT_EQUAL_size_t(8, head->size); // 7 + 1 tail = 8 bytes.
    for (size_t i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[i]);
    }
    // Tail: SOT=1, EOT=1, toggle=0, tid=31 => 0xDF.
    TEST_ASSERT_EQUAL_HEX8(0xDF, head->data[7]);

    free_frames(head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_v0_single_multi_boundary(void)
{
    // Test the exact boundary: 8 bytes payload is the first multi-frame case for v0.
    // 8 bytes >= MTU (8), so it becomes multi-frame with CRC prepended.
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t               queue_size = 0;
    const uint8_t        data[]     = { 0, 1, 2, 3, 4, 5, 6, 7 }; // Exactly 8 bytes.
    canard_bytes_chain_t payload    = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    const uint16_t       crc_seed   = 0xFFFF;

    tx_frame_t* head = tx_spool_v0(mem, &queue_size, crc_seed, 0, payload);
    TEST_ASSERT_NOT_NULL(head);
    // 2 CRC + 8 bytes = 10 bytes. At 7 bytes/frame: ceil(10/7) = 2 frames.
    TEST_ASSERT_EQUAL_size_t(2, count_frames(head));

    uint16_t expected_crc = crc_add(crc_seed, sizeof(data), data);

    // Frame 1: CRC (2 bytes, little-endian) + 5 bytes payload + tail = 8 bytes.
    TEST_ASSERT_EQUAL_size_t(8, head->size);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc & 0xFFU), head->data[0]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc >> 8U), head->data[1]);
    for (size_t i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[2 + i]);
    }
    // Tail: SOT=1, EOT=0, toggle=0, tid=0 => 0x80.
    TEST_ASSERT_EQUAL_HEX8(0x80, head->data[7]);

    // Frame 2: 3 bytes payload + tail = 4 bytes.
    tx_frame_t* frame2 = head->next;
    TEST_ASSERT_NOT_NULL(frame2);
    TEST_ASSERT_EQUAL_size_t(4, frame2->size);
    for (size_t i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[5 + i], frame2->data[i]);
    }
    // Tail: SOT=0, EOT=1, toggle=1, tid=0 => 0x60.
    TEST_ASSERT_EQUAL_HEX8(0x60, frame2->data[3]);

    free_frames(head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_v0_multi_frame(void)
{
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t queue_size = 0;
    // 10 bytes payload forces multi-frame (>= 8).
    // v0: CRC prepended. 2 CRC + 10 payload = 12 bytes. At 7 bytes/frame: ceil(12/7) = 2 frames.
    const uint8_t        data[]   = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    const uint16_t       crc_seed = 0x1234;

    tx_frame_t* head = tx_spool_v0(mem, &queue_size, crc_seed, 7, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(2, count_frames(head));

    // Compute expected CRC (seeded, then over payload).
    uint16_t expected_crc = crc_add(crc_seed, sizeof(data), data);

    // Frame 1: CRC (2 bytes, little-endian) + 5 bytes payload + tail = 8 bytes.
    TEST_ASSERT_EQUAL_size_t(8, head->size);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc & 0xFFU), head->data[0]); // CRC low byte.
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc >> 8U), head->data[1]);   // CRC high byte.
    for (size_t i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[2 + i]);
    }
    // Tail: SOT=1, EOT=0, toggle=0, tid=7 => 0x87.
    TEST_ASSERT_EQUAL_HEX8(0x87, head->data[7]);

    // Frame 2: 5 bytes payload + tail = 6 bytes.
    tx_frame_t* frame2 = head->next;
    TEST_ASSERT_NOT_NULL(frame2);
    TEST_ASSERT_EQUAL_size_t(6, frame2->size);
    for (size_t i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[5 + i], frame2->data[i]);
    }
    // Tail: SOT=0, EOT=1, toggle=1, tid=7 => 0x67.
    TEST_ASSERT_EQUAL_HEX8(0x67, frame2->data[5]);

    free_frames(head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_v0_multi_frame_three_frames(void)
{
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t queue_size = 0;
    // 20 bytes payload. v0: 2 CRC + 20 = 22 bytes. At 7 bytes/frame: ceil(22/7) = 4 frames.
    const uint8_t        data[20] = { 0 };
    canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool_v0(mem, &queue_size, 0xFFFF, 0, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(4, count_frames(head));

    // Verify tail bytes toggle pattern: 0, 1, 0, 1 (v0 starts at 0).
    tx_frame_t* f = head;
    // Frame 1: SOT=1, EOT=0, toggle=0 => 0x80.
    TEST_ASSERT_EQUAL_HEX8(0x80, f->data[f->size - 1]);
    f = f->next;
    // Frame 2: SOT=0, EOT=0, toggle=1 => 0x20.
    TEST_ASSERT_EQUAL_HEX8(0x20, f->data[f->size - 1]);
    f = f->next;
    // Frame 3: SOT=0, EOT=0, toggle=0 => 0x00.
    TEST_ASSERT_EQUAL_HEX8(0x00, f->data[f->size - 1]);
    f = f->next;
    // Frame 4: SOT=0, EOT=1, toggle=1 => 0x60.
    TEST_ASSERT_EQUAL_HEX8(0x60, f->data[f->size - 1]);

    free_frames(head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_v0_oom(void)
{
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    alloc.limit_bytes      = 0;
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t               queue_size = 0;
    const uint8_t        data[]     = { 1, 2, 3 };
    canard_bytes_chain_t payload    = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool_v0(mem, &queue_size, 0xFFFF, 0, payload);
    TEST_ASSERT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(0, queue_size);
}

static void test_tx_spool_v0_oom_mid_chain(void)
{
    // 20 bytes payload requires 4 frames in v0. Fail on the 3rd allocation.
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    alloc.limit_fragments  = 2; // Allow only 2 allocations.
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);

    size_t               queue_size = 0;
    const uint8_t        data[20]   = { 0 };
    canard_bytes_chain_t payload    = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool_v0(mem, &queue_size, 0xFFFF, 0, payload);
    TEST_ASSERT_NULL(head);
    // Verify that all allocated frames were properly freed.
    TEST_ASSERT_EQUAL_size_t(0, queue_size);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_bytes);
    // Verify that 2 successful allocs + 1 failed alloc + 2 frees occurred.
    TEST_ASSERT_EQUAL_UINT64(3, alloc.count_alloc);
    TEST_ASSERT_EQUAL_UINT64(2, alloc.count_free);
}

// =============================================  tx_predict_frame_count  =============================================

static void test_tx_predict_frame_count(void)
{
    // Single-frame cases (transfer_size <= mtu - 1).
    TEST_ASSERT_EQUAL_size_t(1, tx_predict_frame_count(0, CANARD_MTU_CAN_CLASSIC)); // Empty.
    TEST_ASSERT_EQUAL_size_t(1, tx_predict_frame_count(1, CANARD_MTU_CAN_CLASSIC)); // 1 byte.
    TEST_ASSERT_EQUAL_size_t(1, tx_predict_frame_count(7, CANARD_MTU_CAN_CLASSIC)); // Max single-frame Classic.
    TEST_ASSERT_EQUAL_size_t(1, tx_predict_frame_count(63, CANARD_MTU_CAN_FD));     // Max single-frame FD.

    // Multi-frame Classic CAN (MTU=8, 7 bytes/frame).
    // 8 bytes payload + 2 CRC = 10 bytes. ceil(10/7) = 2.
    TEST_ASSERT_EQUAL_size_t(2, tx_predict_frame_count(8, CANARD_MTU_CAN_CLASSIC));
    // 10 bytes payload + 2 CRC = 12 bytes. ceil(12/7) = 2.
    TEST_ASSERT_EQUAL_size_t(2, tx_predict_frame_count(10, CANARD_MTU_CAN_CLASSIC));
    // 13 bytes payload + 2 CRC = 15 bytes. ceil(15/7) = 3.
    TEST_ASSERT_EQUAL_size_t(3, tx_predict_frame_count(13, CANARD_MTU_CAN_CLASSIC));
    // 20 bytes payload + 2 CRC = 22 bytes. ceil(22/7) = 4.
    TEST_ASSERT_EQUAL_size_t(4, tx_predict_frame_count(20, CANARD_MTU_CAN_CLASSIC));

    // Multi-frame CAN FD (MTU=64, 63 bytes/frame).
    // 64 bytes payload + 2 CRC = 66 bytes. ceil(66/63) = 2.
    TEST_ASSERT_EQUAL_size_t(2, tx_predict_frame_count(64, CANARD_MTU_CAN_FD));
    // 100 bytes payload + 2 CRC = 102 bytes. ceil(102/63) = 2.
    TEST_ASSERT_EQUAL_size_t(2, tx_predict_frame_count(100, CANARD_MTU_CAN_FD));
    // 200 bytes payload + 2 CRC = 202 bytes. ceil(202/63) = 4.
    TEST_ASSERT_EQUAL_size_t(4, tx_predict_frame_count(200, CANARD_MTU_CAN_FD));

    // Boundary cases: verify exactly where frame count increases.
    // Classic CAN: 2->3 frames boundary. 12 bytes + 2 CRC = 14. ceil(14/7) = 2.
    TEST_ASSERT_EQUAL_size_t(2, tx_predict_frame_count(12, CANARD_MTU_CAN_CLASSIC));
    // 13 bytes + 2 CRC = 15. ceil(15/7) = 3.
    TEST_ASSERT_EQUAL_size_t(3, tx_predict_frame_count(13, CANARD_MTU_CAN_CLASSIC));

    // CAN FD: 2->3 frames boundary. 124 bytes + 2 CRC = 126. ceil(126/63) = 2.
    TEST_ASSERT_EQUAL_size_t(2, tx_predict_frame_count(124, CANARD_MTU_CAN_FD));
    // 125 bytes + 2 CRC = 127. ceil(127/63) = 3.
    TEST_ASSERT_EQUAL_size_t(3, tx_predict_frame_count(125, CANARD_MTU_CAN_FD));
}

void setUp(void) {}

void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    // tx_spool (Cyphal v1).
    RUN_TEST(test_tx_spool_single_frame_empty);
    RUN_TEST(test_tx_spool_single_frame_small);
    RUN_TEST(test_tx_spool_single_frame_max_classic);
    RUN_TEST(test_tx_spool_single_multi_boundary);
    RUN_TEST(test_tx_spool_single_frame_fd);
    RUN_TEST(test_tx_spool_fd_dlc_rounding);
    RUN_TEST(test_tx_spool_multi_frame_classic);
    RUN_TEST(test_tx_spool_multi_frame_three_frames);
    RUN_TEST(test_tx_spool_multi_frame_fd);
    RUN_TEST(test_tx_spool_fragmented_payload);
    RUN_TEST(test_tx_spool_crc_split);
    RUN_TEST(test_tx_spool_crc_only_last_frame);
    RUN_TEST(test_tx_spool_oom);
    RUN_TEST(test_tx_spool_oom_mid_chain);
    // tx_spool_v0 (UAVCAN v0).
    RUN_TEST(test_tx_spool_v0_single_frame_empty);
    RUN_TEST(test_tx_spool_v0_single_frame_max);
    RUN_TEST(test_tx_spool_v0_single_multi_boundary);
    RUN_TEST(test_tx_spool_v0_multi_frame);
    RUN_TEST(test_tx_spool_v0_multi_frame_three_frames);
    RUN_TEST(test_tx_spool_v0_oom);
    RUN_TEST(test_tx_spool_v0_oom_mid_chain);
    // tx_predict_frame_count.
    RUN_TEST(test_tx_predict_frame_count);
    return UNITY_END();
}
