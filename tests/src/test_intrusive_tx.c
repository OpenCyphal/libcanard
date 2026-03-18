// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal Development Team.

#include "canard.c" // NOLINT(bugprone-suspicious-include)
#include "helpers.h"
#include <unity.h>

// Test context for vtable callbacks.
typedef struct
{
    canard_us_t           now;
    uint32_t              subject_id;
    size_t                feedback_calls;
    uint_least8_t         last_ack;
    canard_user_context_t last_context;
    canard_vtable_t       vtable;
} test_context_t;

static canard_us_t mock_now(canard_t* const self)
{
    const test_context_t* ctx = (const test_context_t*)self->user_context;
    return (ctx != NULL) ? ctx->now : 0;
}

// Dummy subject-ID resolver for publish tests.
static uint32_t mock_tx_subject_id(canard_t* const self, const canard_user_context_t context)
{
    (void)context;
    const test_context_t* ctx = (const test_context_t*)self->user_context;
    return (ctx != NULL) ? ctx->subject_id : 0;
}

// Dummy feedback callback for reliable transfers (matches vtable signature).
static void dummy_feedback(canard_t* const self, canard_user_context_t context, uint_least8_t acknowledgements)
{
    test_context_t* ctx = (test_context_t*)self->user_context;
    if (ctx != NULL) {
        ctx->feedback_calls++;
        ctx->last_ack     = acknowledgements;
        ctx->last_context = context;
    }
}

// Helper to initialize the test context and wire it into canard_t.
static void init_test_context(canard_t* const self, test_context_t* const ctx)
{
    ctx->now            = 0;
    ctx->subject_id     = 0;
    ctx->feedback_calls = 0;
    ctx->last_ack       = 0;
    ctx->last_context   = CANARD_USER_CONTEXT_NULL;
    ctx->vtable         = (canard_vtable_t){
                .now           = mock_now,
                .on_p2p        = NULL,
                .tx            = NULL,
                .tx_subject_id = mock_tx_subject_id,
                .filter        = NULL,
                .feedback      = dummy_feedback,
    };
    self->user_context = ctx;
    self->vtable       = &ctx->vtable;
}

// Helper macro to get frame size (dlc is a bitfield, size not stored directly).
#define FRAME_SIZE(f) canard_dlc_to_len[(f)->dlc]

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

// Helper to free a frame chain (requires canard_t* for refcount_dec).
static void free_frames(canard_t* const self, tx_frame_t* head)
{
    while (head != NULL) {
        tx_frame_t* const next = head->next;
        canard_refcount_dec(self, tx_frame_view(head));
        head = next;
    }
}

// Helper to set up a minimal canard instance for tx_spool tests.
static void setup_canard_for_spool(canard_t* self, instrumented_allocator_t* alloc, test_context_t* ctx)
{
    instrumented_allocator_new(alloc);
    memset(self, 0, sizeof(*self));
    init_test_context(self, ctx);
    self->mem.tx_frame = instrumented_allocator_make_resource(alloc);
}

// ==============================================  tx_spool (Cyphal v1)  ==============================================

static void test_tx_spool_single_frame_empty(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };

    // Empty payload, MTU=8 (Classic CAN). Expect 1 frame with just tail byte.
    tx_frame_t* head = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 5, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1, count_frames(head));
    TEST_ASSERT_EQUAL_size_t(1, FRAME_SIZE(head)); // Just tail byte, rounded to 1.
    // Tail: SOT=1, EOT=1, toggle=1, tid=5 => 0x80|0x40|0x20|0x05 = 0xE5.
    TEST_ASSERT_EQUAL_HEX8(0xE5, head->data[0]);

    free_frames(&self, head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_single_frame_small(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);

    const uint8_t        data[]  = { 0xDE, 0xAD, 0xBE, 0xEF };
    canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    // 4 bytes payload, MTU=8. Fits in single frame (4 < 7).
    tx_frame_t* head = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 17, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1, count_frames(head));
    // Frame size: ceil(4+1)=5 bytes.
    TEST_ASSERT_EQUAL_size_t(5, FRAME_SIZE(head));
    TEST_ASSERT_EQUAL_HEX8(0xDE, head->data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xAD, head->data[1]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, head->data[2]);
    TEST_ASSERT_EQUAL_HEX8(0xEF, head->data[3]);
    // Tail: SOT=1, EOT=1, toggle=1, tid=17 => 0x80|0x40|0x20|0x11 = 0xF1.
    TEST_ASSERT_EQUAL_HEX8(0xF1, head->data[4]);

    free_frames(&self, head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_single_frame_max_classic(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);

    const uint8_t        data[]  = { 1, 2, 3, 4, 5, 6, 7 }; // 7 bytes, max single-frame for Classic CAN.
    canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 0, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1, count_frames(head));
    // Frame size: 7 payload + 1 tail = 8 bytes.
    TEST_ASSERT_EQUAL_size_t(8, FRAME_SIZE(head));
    for (size_t i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[i]);
    }
    // Tail: SOT=1, EOT=1, toggle=1, tid=0 => 0xE0.
    TEST_ASSERT_EQUAL_HEX8(0xE0, head->data[7]);

    free_frames(&self, head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_single_multi_boundary(void)
{
    // Test the exact boundary between single-frame and multi-frame: 8 bytes payload.
    // 8 bytes >= MTU-1 (7), so it becomes multi-frame with CRC.
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);

    const uint8_t        data[]  = { 0, 1, 2, 3, 4, 5, 6, 7 }; // Exactly 8 bytes.
    canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 0, payload);
    TEST_ASSERT_NOT_NULL(head);
    // 8 bytes + 2 CRC = 10 bytes. At 7 bytes/frame: ceil(10/7) = 2 frames.
    TEST_ASSERT_EQUAL_size_t(2, count_frames(head));

    // Frame 1: 7 bytes payload + tail.
    TEST_ASSERT_EQUAL_size_t(8, FRAME_SIZE(head));
    for (size_t i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[i]);
    }
    // Tail: SOT=1, EOT=0, toggle=1, tid=0 => 0xA0.
    TEST_ASSERT_EQUAL_HEX8(0xA0, head->data[7]);

    // Frame 2: 1 byte payload + 2 CRC + tail = 4 bytes.
    tx_frame_t* frame2 = head->next;
    TEST_ASSERT_NOT_NULL(frame2);
    TEST_ASSERT_EQUAL_size_t(4, FRAME_SIZE(frame2));
    TEST_ASSERT_EQUAL_HEX8(data[7], frame2->data[0]);
    uint16_t expected_crc = crc_add(CRC_INITIAL, 8, data);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc >> 8U), frame2->data[1]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc & 0xFFU), frame2->data[2]);
    // Tail: SOT=0, EOT=1, toggle=0, tid=0 => 0x40.
    TEST_ASSERT_EQUAL_HEX8(0x40, frame2->data[3]);

    free_frames(&self, head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_single_frame_fd(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);

    const uint8_t        data[20] = { 0 }; // 20 bytes payload, MTU=64. Single frame.
    canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_FD, 31, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1, count_frames(head));
    // Frame size: ceil(20+1)=21 -> rounded to 24.
    TEST_ASSERT_EQUAL_size_t(24, FRAME_SIZE(head));
    // Tail at position 23: SOT=1, EOT=1, toggle=1, tid=31 => 0xFF.
    TEST_ASSERT_EQUAL_HEX8(0xFF, head->data[23]);
    // Check padding (positions 20, 21, 22).
    TEST_ASSERT_EQUAL_HEX8(0x00, head->data[20]);
    TEST_ASSERT_EQUAL_HEX8(0x00, head->data[21]);
    TEST_ASSERT_EQUAL_HEX8(0x00, head->data[22]);

    free_frames(&self, head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_fd_dlc_rounding(void)
{
    // Test various CAN FD DLC rounding cases: valid sizes are 8, 12, 16, 20, 24, 32, 48, 64.
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);

    // Test: 9 bytes payload + 1 tail = 10 -> rounds to 12.
    {
        const uint8_t        data[9] = { 0 };
        canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
        tx_frame_t*          head    = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_FD, 0, payload);
        TEST_ASSERT_NOT_NULL(head);
        TEST_ASSERT_EQUAL_size_t(12, FRAME_SIZE(head));
        // Tail at position 11.
        TEST_ASSERT_EQUAL_HEX8(0xE0, head->data[11]);
        free_frames(&self, head);
    }

    // Test: 13 bytes payload + 1 tail = 14 -> rounds to 16.
    {
        const uint8_t        data[13] = { 0 };
        canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
        tx_frame_t*          head     = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_FD, 0, payload);
        TEST_ASSERT_NOT_NULL(head);
        TEST_ASSERT_EQUAL_size_t(16, FRAME_SIZE(head));
        TEST_ASSERT_EQUAL_HEX8(0xE0, head->data[15]);
        free_frames(&self, head);
    }

    // Test: 25 bytes payload + 1 tail = 26 -> rounds to 32.
    {
        const uint8_t        data[25] = { 0 };
        canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
        tx_frame_t*          head     = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_FD, 0, payload);
        TEST_ASSERT_NOT_NULL(head);
        TEST_ASSERT_EQUAL_size_t(32, FRAME_SIZE(head));
        TEST_ASSERT_EQUAL_HEX8(0xE0, head->data[31]);
        free_frames(&self, head);
    }

    // Test: 33 bytes payload + 1 tail = 34 -> rounds to 48.
    {
        const uint8_t        data[33] = { 0 };
        canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
        tx_frame_t*          head     = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_FD, 0, payload);
        TEST_ASSERT_NOT_NULL(head);
        TEST_ASSERT_EQUAL_size_t(48, FRAME_SIZE(head));
        TEST_ASSERT_EQUAL_HEX8(0xE0, head->data[47]);
        free_frames(&self, head);
    }

    // Test: 49 bytes payload + 1 tail = 50 -> rounds to 64.
    {
        const uint8_t        data[49] = { 0 };
        canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
        tx_frame_t*          head     = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_FD, 0, payload);
        TEST_ASSERT_NOT_NULL(head);
        TEST_ASSERT_EQUAL_size_t(64, FRAME_SIZE(head));
        TEST_ASSERT_EQUAL_HEX8(0xE0, head->data[63]);
        free_frames(&self, head);
    }

    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_multi_frame_classic(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);

    // 10 bytes payload forces multi-frame with MTU=8 (7 bytes payload per frame max).
    const uint8_t        data[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 7, payload);
    TEST_ASSERT_NOT_NULL(head);
    // 10 bytes + 2 CRC = 12 bytes. At 7 bytes/frame: ceil(12/7) = 2 frames.
    TEST_ASSERT_EQUAL_size_t(2, count_frames(head));

    // Frame 1: 7 bytes payload + tail.
    TEST_ASSERT_EQUAL_size_t(8, FRAME_SIZE(head));
    for (size_t i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[i]);
    }
    // Tail: SOT=1, EOT=0, toggle=1, tid=7 => 0x80|0x20|0x07 = 0xA7.
    TEST_ASSERT_EQUAL_HEX8(0xA7, head->data[7]);

    // Frame 2: 3 bytes payload + 2 CRC + tail = 6 bytes.
    tx_frame_t* frame2 = head->next;
    TEST_ASSERT_NOT_NULL(frame2);
    TEST_ASSERT_EQUAL_size_t(6, FRAME_SIZE(frame2));
    TEST_ASSERT_EQUAL_HEX8(data[7], frame2->data[0]);
    TEST_ASSERT_EQUAL_HEX8(data[8], frame2->data[1]);
    TEST_ASSERT_EQUAL_HEX8(data[9], frame2->data[2]);
    // CRC at positions 3, 4 (big-endian). No padding needed.
    uint16_t expected_crc = crc_add(CRC_INITIAL, 10, data);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc >> 8U), frame2->data[3]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc & 0xFFU), frame2->data[4]);
    // Tail: SOT=0, EOT=1, toggle=0, tid=7 => 0x40|0x07 = 0x47.
    TEST_ASSERT_EQUAL_HEX8(0x47, frame2->data[5]);

    free_frames(&self, head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_multi_frame_three_frames(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);

    // 20 bytes + 2 CRC = 22. At 7 bytes/frame: ceil(22/7) = 4 frames.
    const uint8_t        data[20] = { 0 };
    canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 0, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(4, count_frames(head));

    // Verify tail bytes toggle pattern: 1, 0, 1, 0.
    tx_frame_t* f = head;
    // Frame 1: SOT=1, EOT=0, toggle=1 => 0xA0.
    TEST_ASSERT_EQUAL_HEX8(0xA0, f->data[FRAME_SIZE(f) - 1]);
    f = f->next;
    // Frame 2: SOT=0, EOT=0, toggle=0 => 0x00.
    TEST_ASSERT_EQUAL_HEX8(0x00, f->data[FRAME_SIZE(f) - 1]);
    f = f->next;
    // Frame 3: SOT=0, EOT=0, toggle=1 => 0x20.
    TEST_ASSERT_EQUAL_HEX8(0x20, f->data[FRAME_SIZE(f) - 1]);
    f = f->next;
    // Frame 4: SOT=0, EOT=1, toggle=0 => 0x40.
    TEST_ASSERT_EQUAL_HEX8(0x40, f->data[FRAME_SIZE(f) - 1]);

    free_frames(&self, head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_multi_frame_fd(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);

    // 100 bytes payload, MTU=64. 100 + 2 CRC = 102. At 63 bytes/frame: ceil(102/63) = 2 frames.
    uint8_t data[100];
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = (uint8_t)i;
    }
    canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_FD, 15, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(2, count_frames(head));

    // Frame 1: 63 bytes payload + tail = 64 bytes.
    TEST_ASSERT_EQUAL_size_t(64, FRAME_SIZE(head));
    for (size_t i = 0; i < 63; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[i]);
    }
    // Tail: SOT=1, EOT=0, toggle=1, tid=15 => 0xAF.
    TEST_ASSERT_EQUAL_HEX8(0xAF, head->data[63]);

    // Frame 2: 37 bytes payload + 2 CRC + tail. Size: 37+2+1=40 -> rounded to 48.
    tx_frame_t* frame2 = head->next;
    TEST_ASSERT_NOT_NULL(frame2);
    TEST_ASSERT_EQUAL_size_t(48, FRAME_SIZE(frame2));
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

    free_frames(&self, head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_fragmented_payload(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);

    const uint8_t        f1[] = { 0xAA, 0xBB };
    const uint8_t        f2[] = { 0xCC, 0xDD, 0xEE };
    canard_bytes_chain_t c2   = { .bytes = { .size = sizeof(f2), .data = f2 }, .next = NULL };
    canard_bytes_chain_t c1   = { .bytes = { .size = sizeof(f1), .data = f1 }, .next = &c2 };

    tx_frame_t* head = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 3, c1);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1, count_frames(head));
    // 5 bytes payload + 1 tail = 6 bytes.
    TEST_ASSERT_EQUAL_size_t(6, FRAME_SIZE(head));
    TEST_ASSERT_EQUAL_HEX8(0xAA, head->data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, head->data[1]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, head->data[2]);
    TEST_ASSERT_EQUAL_HEX8(0xDD, head->data[3]);
    TEST_ASSERT_EQUAL_HEX8(0xEE, head->data[4]);
    // Tail: SOT=1, EOT=1, toggle=1, tid=3 => 0xE3.
    TEST_ASSERT_EQUAL_HEX8(0xE3, head->data[5]);

    free_frames(&self, head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_crc_split(void)
{
    // Test CRC split across frames: first CRC byte in second-to-last frame, second in last frame.
    // With MTU=8 (7 payload bytes per frame), 13 bytes payload + 2 CRC = 15 bytes total.
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);

    const uint8_t        data[13] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
    canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 0, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(3, count_frames(head));

    // CRC over payload (no padding in this case).
    uint16_t expected_crc = crc_add(CRC_INITIAL, 13, data);

    // Frame 1: 7 bytes payload + tail.
    TEST_ASSERT_EQUAL_size_t(8, FRAME_SIZE(head));
    for (size_t i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[i]);
    }
    // Tail: SOT=1, EOT=0, toggle=1, tid=0 => 0xA0.
    TEST_ASSERT_EQUAL_HEX8(0xA0, head->data[7]);

    // Frame 2: 6 bytes payload + CRC high byte + tail.
    tx_frame_t* frame2 = head->next;
    TEST_ASSERT_NOT_NULL(frame2);
    TEST_ASSERT_EQUAL_size_t(8, FRAME_SIZE(frame2));
    for (size_t i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[7 + i], frame2->data[i]);
    }
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc >> 8U), frame2->data[6]); // CRC high byte.
    // Tail: SOT=0, EOT=0, toggle=0, tid=0 => 0x00.
    TEST_ASSERT_EQUAL_HEX8(0x00, frame2->data[7]);

    // Frame 3: CRC low byte + tail.
    tx_frame_t* frame3 = frame2->next;
    TEST_ASSERT_NOT_NULL(frame3);
    TEST_ASSERT_EQUAL_size_t(2, FRAME_SIZE(frame3));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc & 0xFFU), frame3->data[0]); // CRC low byte.
    // Tail: SOT=0, EOT=1, toggle=1, tid=0 => 0x60.
    TEST_ASSERT_EQUAL_HEX8(0x60, frame3->data[1]);

    free_frames(&self, head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_crc_only_last_frame(void)
{
    // Test case where the last frame contains ONLY CRC (no payload).
    // With MTU=8 (7 payload bytes per frame), 14 bytes payload + 2 CRC = 16 bytes total.
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);

    const uint8_t        data[14] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
    canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 0, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(3, count_frames(head));

    // CRC over payload (no padding).
    uint16_t expected_crc = crc_add(CRC_INITIAL, 14, data);

    // Frame 1: 7 bytes payload + tail.
    TEST_ASSERT_EQUAL_size_t(8, FRAME_SIZE(head));
    for (size_t i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[i]);
    }
    // Tail: SOT=1, EOT=0, toggle=1, tid=0 => 0xA0.
    TEST_ASSERT_EQUAL_HEX8(0xA0, head->data[7]);

    // Frame 2: 7 bytes payload + tail = 8 bytes.
    tx_frame_t* frame2 = head->next;
    TEST_ASSERT_NOT_NULL(frame2);
    TEST_ASSERT_EQUAL_size_t(8, FRAME_SIZE(frame2));
    for (size_t i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[7 + i], frame2->data[i]);
    }
    // Tail: SOT=0, EOT=0, toggle=0, tid=0 => 0x00.
    TEST_ASSERT_EQUAL_HEX8(0x00, frame2->data[7]);

    // Frame 3: CRC only (2 bytes) + tail = 3 bytes.
    tx_frame_t* frame3 = frame2->next;
    TEST_ASSERT_NOT_NULL(frame3);
    TEST_ASSERT_EQUAL_size_t(3, FRAME_SIZE(frame3));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc >> 8U), frame3->data[0]);   // CRC high byte.
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(expected_crc & 0xFFU), frame3->data[1]); // CRC low byte.
    // Tail: SOT=0, EOT=1, toggle=1, tid=0 => 0x60.
    TEST_ASSERT_EQUAL_HEX8(0x60, frame3->data[2]);

    free_frames(&self, head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_oom(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);
    alloc.limit_bytes = 0; // No memory available.

    const uint8_t        data[]  = { 1, 2, 3 };
    canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 0, payload);
    TEST_ASSERT_NULL(head);
}

static void test_tx_spool_oom_mid_chain(void)
{
    // 20 bytes payload requires 4 frames. Fail on the 3rd allocation.
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);
    alloc.limit_fragments = 2; // Allow only 2 allocations.

    const uint8_t        data[20] = { 0 };
    canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 0, payload);
    TEST_ASSERT_NULL(head);
    // Verify that all allocated frames were properly freed.
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_bytes);
    // Verify that 2 successful allocs + 1 failed alloc + 2 frees occurred.
    TEST_ASSERT_EQUAL_UINT64(3, alloc.count_alloc);
    TEST_ASSERT_EQUAL_UINT64(2, alloc.count_free);
}

// =============================================  tx_spool_v0 (UAVCAN v0)  =============================================

static void test_tx_spool_v0_single_frame_empty(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };

    tx_frame_t* head = tx_spool_v0(&self, 0xFFFF, 5, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1, count_frames(head));
    TEST_ASSERT_EQUAL_size_t(1, FRAME_SIZE(head));
    // Tail: SOT=1, EOT=1, toggle=0, tid=5 => 0x80|0x40|0x05 = 0xC5 (v0 starts toggle=0).
    TEST_ASSERT_EQUAL_HEX8(0xC5, head->data[0]);

    free_frames(&self, head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_v0_single_frame_max(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);

    const uint8_t        data[]  = { 1, 2, 3, 4, 5, 6, 7 }; // 7 bytes, single frame (7 < 8).
    canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool_v0(&self, 0xFFFF, 31, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1, count_frames(head));
    TEST_ASSERT_EQUAL_size_t(8, FRAME_SIZE(head)); // 7 + 1 tail = 8 bytes.
    for (size_t i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[i]);
    }
    // Tail: SOT=1, EOT=1, toggle=0, tid=31 => 0xDF.
    TEST_ASSERT_EQUAL_HEX8(0xDF, head->data[7]);

    free_frames(&self, head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_v0_single_multi_boundary(void)
{
    // Test the exact boundary: 8 bytes payload is the first multi-frame case for v0.
    // 8 bytes >= MTU (8), so it becomes multi-frame with CRC prepended.
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);

    const uint8_t        data[]   = { 0, 1, 2, 3, 4, 5, 6, 7 }; // Exactly 8 bytes.
    canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    const uint16_t       crc_seed = 0xFFFF;

    tx_frame_t* head = tx_spool_v0(&self, crc_seed, 0, payload);
    TEST_ASSERT_NOT_NULL(head);
    // 2 CRC + 8 bytes = 10 bytes. At 7 bytes/frame: ceil(10/7) = 2 frames.
    TEST_ASSERT_EQUAL_size_t(2, count_frames(head));

    uint16_t expected_crc = crc_add(crc_seed, sizeof(data), data);

    // Frame 1: CRC (2 bytes, little-endian) + 5 bytes payload + tail = 8 bytes.
    TEST_ASSERT_EQUAL_size_t(8, FRAME_SIZE(head));
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
    TEST_ASSERT_EQUAL_size_t(4, FRAME_SIZE(frame2));
    for (size_t i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[5 + i], frame2->data[i]);
    }
    // Tail: SOT=0, EOT=1, toggle=1, tid=0 => 0x60.
    TEST_ASSERT_EQUAL_HEX8(0x60, frame2->data[3]);

    free_frames(&self, head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_v0_multi_frame(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);

    // 10 bytes payload forces multi-frame (>= 8).
    // v0: CRC prepended. 2 CRC + 10 payload = 12 bytes. At 7 bytes/frame: ceil(12/7) = 2 frames.
    const uint8_t        data[]   = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    const uint16_t       crc_seed = 0x1234;

    tx_frame_t* head = tx_spool_v0(&self, crc_seed, 7, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(2, count_frames(head));

    // Compute expected CRC (seeded, then over payload).
    uint16_t expected_crc = crc_add(crc_seed, sizeof(data), data);

    // Frame 1: CRC (2 bytes, little-endian) + 5 bytes payload + tail = 8 bytes.
    TEST_ASSERT_EQUAL_size_t(8, FRAME_SIZE(head));
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
    TEST_ASSERT_EQUAL_size_t(6, FRAME_SIZE(frame2));
    for (size_t i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[5 + i], frame2->data[i]);
    }
    // Tail: SOT=0, EOT=1, toggle=1, tid=7 => 0x67.
    TEST_ASSERT_EQUAL_HEX8(0x67, frame2->data[5]);

    free_frames(&self, head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_v0_multi_frame_three_frames(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);

    // 20 bytes payload. v0: 2 CRC + 20 = 22 bytes. At 7 bytes/frame: ceil(22/7) = 4 frames.
    const uint8_t        data[20] = { 0 };
    canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool_v0(&self, 0xFFFF, 0, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(4, count_frames(head));

    // Verify tail bytes toggle pattern: 0, 1, 0, 1 (v0 starts at 0).
    tx_frame_t* f = head;
    // Frame 1: SOT=1, EOT=0, toggle=0 => 0x80.
    TEST_ASSERT_EQUAL_HEX8(0x80, f->data[FRAME_SIZE(f) - 1]);
    f = f->next;
    // Frame 2: SOT=0, EOT=0, toggle=1 => 0x20.
    TEST_ASSERT_EQUAL_HEX8(0x20, f->data[FRAME_SIZE(f) - 1]);
    f = f->next;
    // Frame 3: SOT=0, EOT=0, toggle=0 => 0x00.
    TEST_ASSERT_EQUAL_HEX8(0x00, f->data[FRAME_SIZE(f) - 1]);
    f = f->next;
    // Frame 4: SOT=0, EOT=1, toggle=1 => 0x60.
    TEST_ASSERT_EQUAL_HEX8(0x60, f->data[FRAME_SIZE(f) - 1]);

    free_frames(&self, head);
    TEST_ASSERT_EQUAL_size_t(0, alloc.allocated_fragments);
}

static void test_tx_spool_v0_oom(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);
    alloc.limit_bytes = 0;

    const uint8_t        data[]  = { 1, 2, 3 };
    canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool_v0(&self, 0xFFFF, 0, payload);
    TEST_ASSERT_NULL(head);
}

static void test_tx_spool_v0_oom_mid_chain(void)
{
    // 20 bytes payload requires 4 frames in v0. Fail on the 3rd allocation.
    canard_t                 self;
    instrumented_allocator_t alloc;
    test_context_t           ctx;
    setup_canard_for_spool(&self, &alloc, &ctx);
    alloc.limit_fragments = 2; // Allow only 2 allocations.

    const uint8_t        data[20] = { 0 };
    canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* head = tx_spool_v0(&self, 0xFFFF, 0, payload);
    TEST_ASSERT_NULL(head);
    // Verify that all allocated frames were properly freed.
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

// ==============================================  tx misc  ==============================================

static void test_transfer_kind_is_v0(void)
{
    TEST_ASSERT_TRUE(transfer_kind_is_v0(transfer_kind_v0_message));
    TEST_ASSERT_TRUE(transfer_kind_is_v0(transfer_kind_v0_request));
    TEST_ASSERT_TRUE(transfer_kind_is_v0(transfer_kind_v0_response));
    TEST_ASSERT_FALSE(transfer_kind_is_v0(transfer_kind_message));
    TEST_ASSERT_FALSE(transfer_kind_is_v0(transfer_kind_request));
    TEST_ASSERT_FALSE(transfer_kind_is_v0(transfer_kind_response));
}

static void test_tx_ack_timeout(void)
{
    TEST_ASSERT_EQUAL_INT64(10, tx_ack_timeout(10, canard_prio_exceptional, 0));
    TEST_ASSERT_EQUAL_INT64(40, tx_ack_timeout(10, canard_prio_immediate, 1));
}

// Forward declare helper used by txfer_prio test.
static canard_txfer_t* make_test_transfer(const canard_mem_t          mem,
                                          const transfer_kind_t       kind,
                                          const bool                  fd,
                                          const uint64_t              topic_hash,
                                          const uint32_t              can_id,
                                          const byte_t                transfer_id,
                                          const canard_user_context_t user_context);

static void test_txfer_prio(void)
{
    const uint32_t           can_id = ((uint32_t)canard_prio_high << PRIO_SHIFT);
    instrumented_allocator_t alloc;
    instrumented_allocator_new(&alloc);
    const canard_mem_t mem = instrumented_allocator_make_resource(&alloc);
    canard_txfer_t* tr = make_test_transfer(mem, transfer_kind_message, false, 0, can_id, 0, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_EQUAL_UINT8(canard_prio_high, txfer_prio(tr));
    mem_free(mem, sizeof(canard_txfer_t), tr);
}

// ==============================================  tx_push  ==============================================

// Helper to set up a basic canard instance for tx_push tests.
static void setup_canard_for_tx_push(canard_t*                 self,
                                     instrumented_allocator_t* alloc_tr,
                                     instrumented_allocator_t* alloc_fr,
                                     test_context_t*           ctx)
{
    instrumented_allocator_new(alloc_tr);
    instrumented_allocator_new(alloc_fr);
    memset(self, 0, sizeof(*self));
    init_test_context(self, ctx);
    self->mem.tx_transfer      = instrumented_allocator_make_resource(alloc_tr);
    self->mem.tx_frame         = instrumented_allocator_make_resource(alloc_fr);
    self->tx.queue_capacity    = 100; // Large enough for most tests.
    self->tx.fd                = true;
    self->ack_baseline_timeout = CANARD_TX_ACK_BASELINE_TIMEOUT_DEFAULT_us;
}

// Helper to drop a transfer from pending trees.
static void drop_pending(canard_t* const self, canard_txfer_t* const tr)
{
    FOREACH_IFACE (i) {
        if (cavl2_is_inserted(self->tx.pending[i], &tr->index_pending[i])) {
            cavl2_remove(&self->tx.pending[i], &tr->index_pending[i]);
        }
    }
}

// Helper to create a canard_txfer_t for testing using txfer_new.
static canard_txfer_t* make_test_transfer(const canard_mem_t          mem,
                                          const transfer_kind_t       kind,
                                          const bool                  fd,
                                          const uint64_t              topic_hash,
                                          const uint32_t              can_id,
                                          const byte_t                transfer_id,
                                          const canard_user_context_t user_context)
{
    return txfer_new(mem,
                     1000000, // deadline: 1 second
                     topic_hash,
                     transfer_id,
                     kind,
                     can_id,
                     fd,
                     user_context);
}

static void test_tx_push_basic_v1_classic(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);
    self.tx.fd = false;

    const uint8_t              data[]  = { 0xDE, 0xAD, 0xBE, 0xEF };
    const canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    canard_txfer_t*            tr      = make_test_transfer(
      self.mem.tx_transfer, transfer_kind_message, false, 0, 0x12345678, 5, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr);

    const bool ok = tx_push(&self, tr, false, false, 1, payload, CRC_INITIAL);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(1, self.tx.queue_size);
    TEST_ASSERT_TRUE(txfer_is_pending(&self, tr));
    TEST_ASSERT_NOT_NULL(self.tx.agewise.head);

    txfer_retire(&self, tr, true);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
    TEST_ASSERT_EQUAL_size_t(0, alloc_fr.allocated_fragments);
}

static void test_tx_push_basic_fd_multi_iface(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);

    const uint_least8_t        iface_bitmap = CANARD_IFACE_BITMAP_ALL;
    const uint8_t              data[]       = { 1, 2, 3, 4 };
    const canard_bytes_chain_t payload      = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    canard_txfer_t*            tr =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, 0, 0x12345678, 0, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr);

    const bool ok = tx_push(&self, tr, false, false, iface_bitmap, payload, CRC_INITIAL);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(1, self.tx.queue_size);
    TEST_ASSERT_EQUAL_UINT8(popcount(iface_bitmap), tr->head[0]->refcount);
    FOREACH_IFACE (i) {
        if ((iface_bitmap & (1U << i)) != 0U) {
            TEST_ASSERT_TRUE(cavl2_is_inserted(self.tx.pending[i], &tr->index_pending[i]));
        }
    }

    txfer_retire(&self, tr, true);
    TEST_ASSERT_EQUAL_size_t(0, alloc_fr.allocated_fragments);
}

static void test_tx_push_v0_uses_classic_mtu(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);

    const uint8_t              data[10] = { 0 };
    const canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    canard_txfer_t*            tr       = make_test_transfer(
      self.mem.tx_transfer, transfer_kind_v0_message, false, 0, 0x12345678, 0, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr);

    const bool ok = tx_push(&self, tr, false, true, 1, payload, 0x1234);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(2, self.tx.queue_size);

    txfer_retire(&self, tr, true);
    TEST_ASSERT_EQUAL_size_t(0, alloc_fr.allocated_fragments);
}

static void test_tx_push_oom_frame_alloc(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);
    alloc_fr.limit_bytes = 0;

    const uint8_t              data[]  = { 1, 2, 3 };
    const canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    canard_txfer_t*            tr      = make_test_transfer(
      self.mem.tx_transfer, transfer_kind_message, false, 0, 0x12345678, 0, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr);

    const bool ok = tx_push(&self, tr, false, false, 1, payload, CRC_INITIAL);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_size_t(0, self.tx.queue_size);
    TEST_ASSERT_EQUAL_UINT64(1, self.err.oom);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
}

static void test_tx_push_reliable_oom_removes_index(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);
    alloc_fr.limit_bytes = 0;

    const uint8_t              data[]  = { 1, 2, 3 };
    const canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    canard_txfer_t*            tr      = make_test_transfer(
      self.mem.tx_transfer, transfer_kind_message, false, 1, 0x12345678, 0, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr);

    const bool ok = tx_push(&self, tr, true, false, 1, payload, CRC_INITIAL);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NULL(self.tx.reliable);
    TEST_ASSERT_EQUAL_UINT64(1, self.err.oom);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
}

static void test_tx_push_queue_capacity_too_small(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);
    self.tx.queue_capacity = 1;

    const uint8_t              data[20] = { 0 };
    const canard_bytes_chain_t payload  = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    canard_txfer_t*            tr       = make_test_transfer(
      self.mem.tx_transfer, transfer_kind_message, false, 1, 0x12345678, 0, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr);

    const bool ok = tx_push(&self, tr, true, false, 1, payload, CRC_INITIAL);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NULL(self.tx.reliable);
    TEST_ASSERT_EQUAL_UINT64(1, self.err.tx_capacity);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
}

static void test_tx_ensure_queue_space_no_sacrifice(void)
{
    canard_t self          = { 0 };
    self.tx.queue_capacity = 1;
    self.tx.queue_size     = 1;

    TEST_ASSERT_FALSE(tx_ensure_queue_space(&self, 1));
}

static void test_tx_push_sacrifice_oldest(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);
    self.tx.queue_capacity = 1;

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    canard_txfer_t*            tr1 =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, 0, 0, 1, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr1);
    TEST_ASSERT_TRUE(tx_push(&self, tr1, false, false, 1, payload, CRC_INITIAL));

    canard_txfer_t* tr2 =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, 0, 0, 2, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr2);
    TEST_ASSERT_TRUE(tx_push(&self, tr2, false, false, 1, payload, CRC_INITIAL));
    TEST_ASSERT_EQUAL_UINT64(1, self.err.tx_sacrifice);
    TEST_ASSERT_EQUAL_size_t(1, self.tx.queue_size);
    TEST_ASSERT_EQUAL_PTR(tr2, LIST_HEAD(self.tx.agewise, canard_txfer_t, list_agewise));

    txfer_retire(&self, tr2, true);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
}

static void test_tx_push_duplicate_reliable_transfer(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);

    const uint64_t             topic_hash  = 0x1234567890ABCDEFULL;
    const byte_t               transfer_id = 5;
    const uint8_t              data[]      = { 1, 2, 3 };
    const canard_bytes_chain_t payload     = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    canard_txfer_t* tr1 = make_test_transfer(
      self.mem.tx_transfer, transfer_kind_message, true, topic_hash, 0x12345678, transfer_id, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr1);
    TEST_ASSERT_TRUE(tx_push(&self, tr1, true, false, 1, payload, CRC_INITIAL));

    canard_txfer_t* tr2 = make_test_transfer(
      self.mem.tx_transfer, transfer_kind_message, true, topic_hash, 0x12345678, transfer_id, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr2);
    TEST_ASSERT_FALSE(tx_push(&self, tr2, true, false, 1, payload, CRC_INITIAL));
    TEST_ASSERT_EQUAL_UINT64(1, self.err.tx_duplicate);

    txfer_retire(&self, tr1, true);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
    TEST_ASSERT_EQUAL_size_t(0, alloc_fr.allocated_fragments);
}

static void test_tx_push_fifo_same_priority(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    const uint32_t             can_id  = ((uint32_t)canard_prio_nominal << PRIO_SHIFT);
    canard_txfer_t*            tr1 =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, 0, can_id, 1, CANARD_USER_CONTEXT_NULL);
    canard_txfer_t* tr2 =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, 0, can_id, 2, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr1);
    TEST_ASSERT_NOT_NULL(tr2);

    TEST_ASSERT_TRUE(tx_push(&self, tr1, false, false, 1, payload, CRC_INITIAL));
    TEST_ASSERT_TRUE(tx_push(&self, tr2, false, false, 1, payload, CRC_INITIAL));
    TEST_ASSERT_EQUAL_PTR(tr1, LIST_HEAD(self.tx.agewise, canard_txfer_t, list_agewise));
    TEST_ASSERT_EQUAL_PTR(tr2, LIST_TAIL(self.tx.agewise, canard_txfer_t, list_agewise));

    txfer_retire(&self, tr2, true);
    txfer_retire(&self, tr1, true);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
}

// Ensure pending order wraps transfer-ID for same topic and priority.
static void test_tx_pending_same_topic_tid_wrap(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    const uint32_t             can_id  = ((uint32_t)canard_prio_nominal << PRIO_SHIFT);
    const uint64_t             topic   = 0xAABBCCDDU;
    canard_txfer_t*            tr1     = make_test_transfer(
      self.mem.tx_transfer, transfer_kind_message, true, topic, can_id, 30, CANARD_USER_CONTEXT_NULL);
    canard_txfer_t* tr2 =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, topic, can_id, 2, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr1);
    TEST_ASSERT_NOT_NULL(tr2);

    TEST_ASSERT_TRUE(tx_push(&self, tr1, false, false, 1, payload, CRC_INITIAL));
    TEST_ASSERT_TRUE(tx_push(&self, tr2, false, false, 1, payload, CRC_INITIAL));

    canard_txfer_t* const head = CAVL2_TO_OWNER(cavl2_min(self.tx.pending[0]), canard_txfer_t, index_pending[0]);
    TEST_ASSERT_EQUAL_PTR(tr1, head);

    txfer_retire(&self, tr2, true);
    txfer_retire(&self, tr1, true);
}

// Same transfer-ID uses deadline as a tiebreaker.
static void test_tx_pending_same_topic_tid_deadline(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    const uint32_t             can_id  = ((uint32_t)canard_prio_nominal << PRIO_SHIFT);
    const uint64_t             topic   = 0xDEADBEEFULL;
    canard_txfer_t*            tr1 =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, topic, can_id, 5, CANARD_USER_CONTEXT_NULL);
    canard_txfer_t* tr2 =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, topic, can_id, 5, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr1);
    TEST_ASSERT_NOT_NULL(tr2);
    tr1->deadline = 10;
    tr2->deadline = 20;

    TEST_ASSERT_TRUE(tx_push(&self, tr1, false, false, 1, payload, CRC_INITIAL));
    TEST_ASSERT_TRUE(tx_push(&self, tr2, false, false, 1, payload, CRC_INITIAL));

    canard_txfer_t* const head = CAVL2_TO_OWNER(cavl2_min(self.tx.pending[0]), canard_txfer_t, index_pending[0]);
    TEST_ASSERT_EQUAL_PTR(tr1, head);

    txfer_retire(&self, tr2, true);
    txfer_retire(&self, tr1, true);
}

// Different topic hash uses deadline ordering.
static void test_tx_pending_diff_topic_deadline(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    const uint32_t             can_id  = ((uint32_t)canard_prio_nominal << PRIO_SHIFT);
    canard_txfer_t*            tr1 =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, 1, can_id, 1, CANARD_USER_CONTEXT_NULL);
    canard_txfer_t* tr2 =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, 2, can_id, 2, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr1);
    TEST_ASSERT_NOT_NULL(tr2);
    tr1->deadline = 20;
    tr2->deadline = 10;

    TEST_ASSERT_TRUE(tx_push(&self, tr1, false, false, 1, payload, CRC_INITIAL));
    TEST_ASSERT_TRUE(tx_push(&self, tr2, false, false, 1, payload, CRC_INITIAL));

    canard_txfer_t* const head = CAVL2_TO_OWNER(cavl2_min(self.tx.pending[0]), canard_txfer_t, index_pending[0]);
    TEST_ASSERT_EQUAL_PTR(tr2, head);

    txfer_retire(&self, tr2, true);
    txfer_retire(&self, tr1, true);
}

// Cover comparator branches for staged and pending trees.
static void test_tx_comparator_branches(void)
{
    canard_txfer_t tr[2];
    memset(&tr, 0, sizeof(tr));

    // Staged compare: when < rhs.
    tr[1].deadline           = 100;
    tr[1].staged_until_delta = 10; // staged_until = 90
    tx_staged_key_t key      = { .when = 80, .tr = &tr[0] };
    TEST_ASSERT_EQUAL_INT32(-1, tx_cavl_compare_staged_until(&key, &tr[1].index_staged));

    // Staged compare: when > rhs.
    key.when = 100;
    TEST_ASSERT_EQUAL_INT32(+1, tx_cavl_compare_staged_until(&key, &tr[1].index_staged));

    // Staged compare: pointer tiebreak.
    key.when = 90;
    TEST_ASSERT_EQUAL_INT32(-1, tx_cavl_compare_staged_until(&key, &tr[1].index_staged));

    // Transfer-ID wraparound compare.
    TEST_ASSERT_TRUE(tx_compare_transfer_id(30, 2) < 0);

    // Pending compare: priority order.
    tr[0].topic_hash = 1;
    tr[1].topic_hash = 1;
    tr[0].can_id_msb = (uint32_t)(1U << (CAN_ID_MSb_BITS - 3U));
    tr[1].can_id_msb = (uint32_t)(2U << (CAN_ID_MSb_BITS - 3U));
    TEST_ASSERT_EQUAL_INT32(-1, tx_cavl_compare_pending_order(&tr[0], &tr[1].index_pending[0]));

    // Pending compare: same transfer-ID uses deadline.
    tr[0].can_id_msb  = tr[1].can_id_msb;
    tr[0].transfer_id = 5;
    tr[1].transfer_id = 5;
    tr[0].deadline    = 10;
    tr[1].deadline    = 20;
    TEST_ASSERT_EQUAL_INT32(-1, tx_cavl_compare_pending_order(&tr[0], &tr[1].index_pending[0]));

    // Pending compare: different topic uses deadline.
    tr[0].topic_hash = 1;
    tr[1].topic_hash = 2;
    tr[0].deadline   = 30;
    tr[1].deadline   = 20;
    TEST_ASSERT_EQUAL_INT32(+1, tx_cavl_compare_pending_order(&tr[0], &tr[1].index_pending[0]));

    // Pending compare: pointer tiebreakers.
    tr[0].topic_hash  = 3;
    tr[1].topic_hash  = 3;
    tr[0].transfer_id = 7;
    tr[1].transfer_id = 7;
    tr[0].deadline    = 40;
    tr[1].deadline    = 40;
    TEST_ASSERT_EQUAL_INT32(-1, tx_cavl_compare_pending_order(&tr[0], &tr[1].index_pending[0]));
    TEST_ASSERT_EQUAL_INT32(+1, tx_cavl_compare_pending_order(&tr[1], &tr[0].index_pending[0]));
}

static void test_tx_make_pending_orders(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);

    const canard_bytes_chain_t payload          = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    const uint32_t             can_id_high_prio = ((uint32_t)canard_prio_high << PRIO_SHIFT);
    const uint32_t             can_id_low_prio  = ((uint32_t)canard_prio_low << PRIO_SHIFT);
    canard_txfer_t*            tr1              = make_test_transfer(
      self.mem.tx_transfer, transfer_kind_message, true, 0, can_id_high_prio, 1, CANARD_USER_CONTEXT_NULL);
    canard_txfer_t* tr2 = make_test_transfer(
      self.mem.tx_transfer, transfer_kind_message, true, 0, can_id_low_prio, 2, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr1);
    TEST_ASSERT_NOT_NULL(tr2);

    TEST_ASSERT_TRUE(tx_push(&self, tr1, false, false, 1, payload, CRC_INITIAL));
    TEST_ASSERT_TRUE(tx_push(&self, tr2, false, false, 1, payload, CRC_INITIAL));

    canard_txfer_t* const head = CAVL2_TO_OWNER(cavl2_min(self.tx.pending[0]), canard_txfer_t, index_pending[0]);
    TEST_ASSERT_EQUAL_PTR(tr1, head);

    txfer_retire(&self, tr2, true);
    txfer_retire(&self, tr1, true);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
}

static void test_tx_receive_ack_retires_feedback(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);

    int                         tag          = 7;
    const canard_user_context_t user_context = make_user_context(&tag);
    const uint64_t              topic_hash   = 0x123456789ABCDEF0ULL;
    const uint64_t              lower_bound  = topic_hash & ACK_TOPIC_HASH_LOWER_BOUND_MASK;
    const canard_bytes_chain_t  payload      = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    canard_txfer_t*             tr =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, topic_hash, 0, 1, user_context);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_TRUE(tx_push(&self, tr, true, false, 1, payload, CRC_INITIAL));

    tx_receive_ack(&self, lower_bound, 1);
    TEST_ASSERT_EQUAL_UINT64(1, ctx.feedback_calls);
    TEST_ASSERT_EQUAL_UINT8(1, ctx.last_ack);
    TEST_ASSERT_EQUAL_PTR(&tag, ctx.last_context.ptr[0]);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
    TEST_ASSERT_EQUAL_size_t(0, alloc_fr.allocated_fragments);
}

static void test_tx_receive_ack_scan_miss(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);

    const uint64_t             topic_hash  = 0x123456789ABCDEF0ULL;
    const uint64_t             lower_bound = topic_hash & ACK_TOPIC_HASH_LOWER_BOUND_MASK;
    const canard_bytes_chain_t payload     = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    canard_txfer_t*            tr1 =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, topic_hash, 0, 1, CANARD_USER_CONTEXT_NULL);
    canard_txfer_t* tr3 =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, topic_hash, 0, 3, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr1);
    TEST_ASSERT_NOT_NULL(tr3);

    TEST_ASSERT_TRUE(tx_push(&self, tr1, true, false, 1, payload, CRC_INITIAL));
    TEST_ASSERT_TRUE(tx_push(&self, tr3, true, false, 1, payload, CRC_INITIAL));
    tx_receive_ack(&self, lower_bound, 2);
    TEST_ASSERT_EQUAL_size_t(2, self.tx.queue_size);

    txfer_retire(&self, tr1, true);
    txfer_retire(&self, tr3, true);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
}

static void test_tx_receive_ack_no_match(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);

    const uint64_t             topic_hash = 0x123456789ABCDEF0ULL;
    const canard_bytes_chain_t payload    = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    canard_txfer_t*            tr =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, topic_hash, 0, 1, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_TRUE(tx_push(&self, tr, true, false, 1, payload, CRC_INITIAL));

    tx_receive_ack(&self, topic_hash ^ 1ULL, 1);
    TEST_ASSERT_EQUAL_UINT64(0, ctx.feedback_calls);
    TEST_ASSERT_EQUAL_size_t(1, self.tx.queue_size);

    txfer_retire(&self, tr, true);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
}

// ===========================================  tx scheduling and retire  ============================================

static void test_txfer_is_pending_false(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);

    canard_txfer_t* tr = make_test_transfer(
      self.mem.tx_transfer, transfer_kind_message, false, 0, 0x12345678, 0, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_FALSE(txfer_is_pending(&self, tr));

    mem_free(self.mem.tx_transfer, sizeof(canard_txfer_t), tr);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
}

static void test_txfer_retire_updates_iter(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    canard_txfer_t*            tr1 =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, 0, 0, 1, CANARD_USER_CONTEXT_NULL);
    canard_txfer_t* tr2 =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, 0, 0, 2, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr1);
    TEST_ASSERT_NOT_NULL(tr2);
    TEST_ASSERT_TRUE(tx_push(&self, tr1, false, false, 1, payload, CRC_INITIAL));
    TEST_ASSERT_TRUE(tx_push(&self, tr2, false, false, 1, payload, CRC_INITIAL));
    self.tx.iter = tr1;

    txfer_retire(&self, tr1, true);
    TEST_ASSERT_EQUAL_PTR(tr2, self.tx.iter);

    txfer_retire(&self, tr2, true);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
}

static void test_tx_stage_reliable_if_inserts(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);
    ctx.now = 100;

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    canard_txfer_t*            tr =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, 1, 0, 1, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_TRUE(tx_push(&self, tr, true, false, 1, payload, CRC_INITIAL));

    tx_stage_reliable_if(&self, tr);
    TEST_ASSERT_TRUE(cavl2_is_inserted(self.tx.staged, &tr->index_staged));
    TEST_ASSERT_TRUE(tr->staged_until_delta > 0U);

    txfer_retire(&self, tr, true);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
}

// Equal staged timestamps must not collide in the staged tree.
static void test_tx_stage_reliable_if_tiebreak(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);
    self.ack_baseline_timeout = 1;
    ctx.now                   = 100;

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    canard_txfer_t*            tr1 =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, 1, 0, 1, CANARD_USER_CONTEXT_NULL);
    canard_txfer_t* tr2 =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, 1, 0, 2, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr1);
    TEST_ASSERT_NOT_NULL(tr2);
    tr1->deadline = 1000000;
    tr2->deadline = 1000000;

    TEST_ASSERT_TRUE(tx_push(&self, tr1, true, false, 1, payload, CRC_INITIAL));
    TEST_ASSERT_TRUE(tx_push(&self, tr2, true, false, 1, payload, CRC_INITIAL));

    tx_stage_reliable_if(&self, tr1);
    tx_stage_reliable_if(&self, tr2);
    TEST_ASSERT_TRUE(cavl2_is_inserted(self.tx.staged, &tr1->index_staged));
    TEST_ASSERT_TRUE(cavl2_is_inserted(self.tx.staged, &tr2->index_staged));

    txfer_retire(&self, tr2, true);
    txfer_retire(&self, tr1, true);
}

static void test_tx_stage_reliable_if_orders(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);
    ctx.now = 100;

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    canard_txfer_t*            tr1 =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, 1, 0, 1, CANARD_USER_CONTEXT_NULL);
    canard_txfer_t* tr2 =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, 1, 0, 2, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr1);
    TEST_ASSERT_NOT_NULL(tr2);
    TEST_ASSERT_TRUE(tx_push(&self, tr1, true, false, 1, payload, CRC_INITIAL));
    TEST_ASSERT_TRUE(tx_push(&self, tr2, true, false, 1, payload, CRC_INITIAL));

    tx_stage_reliable_if(&self, tr1);
    tx_stage_reliable_if(&self, tr2);
    TEST_ASSERT_TRUE(cavl2_is_inserted(self.tx.staged, &tr1->index_staged));
    TEST_ASSERT_TRUE(cavl2_is_inserted(self.tx.staged, &tr2->index_staged));

    canard_txfer_t* const first = CAVL2_TO_OWNER(cavl2_min(self.tx.staged), canard_txfer_t, index_staged);
    canard_txfer_t* const second =
      CAVL2_TO_OWNER(cavl2_next_greater(&first->index_staged), canard_txfer_t, index_staged);
    TEST_ASSERT_NOT_NULL(second);

    txfer_retire(&self, tr1, true);
    txfer_retire(&self, tr2, true);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
}

static void test_tx_stage_reliable_if_deadline_too_close(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);
    self.ack_baseline_timeout = 10;
    ctx.now                   = 0;

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    canard_txfer_t*            tr =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, 1, 0, 1, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr);
    tr->deadline = 5;
    TEST_ASSERT_TRUE(tx_push(&self, tr, true, false, 1, payload, CRC_INITIAL));

    tx_stage_reliable_if(&self, tr);
    TEST_ASSERT_NULL(self.tx.staged);

    txfer_retire(&self, tr, false);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
}

static void test_tx_promote_staged_requeues(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);
    ctx.now = 100;

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    canard_txfer_t*            tr =
      make_test_transfer(self.mem.tx_transfer, transfer_kind_message, true, 1, 0, 1, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_TRUE(tx_push(&self, tr, true, false, 1, payload, CRC_INITIAL));

    tx_stage_reliable_if(&self, tr);
    drop_pending(&self, tr);
    TEST_ASSERT_FALSE(txfer_is_pending(&self, tr));

    ctx.now = txfer_staged_until(tr);
    tx_promote_staged(&self, ctx.now);
    TEST_ASSERT_TRUE(txfer_is_pending(&self, tr));
    TEST_ASSERT_TRUE(cavl2_is_inserted(self.tx.staged, &tr->index_staged));

    txfer_retire(&self, tr, true);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
}

// ==============================================  canard_publish  ==============================================

static void test_canard_publish_pinned_best_effort_v1_0(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);
    self.tx.fd = false;

    // Publish a pinned best-effort transfer.
    const uint8_t              data[]  = { 0xAB };
    const canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    const bool                 ok =
      canard_publish(&self, 1000000, 1, canard_prio_nominal, 1, 0, payload, CANARD_USER_CONTEXT_NULL, false);
    TEST_ASSERT_TRUE(ok);
    canard_txfer_t* const tr = LIST_HEAD(self.tx.agewise, canard_txfer_t, list_agewise);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_EQUAL_size_t(2, FRAME_SIZE(tr->head[0]));
    TEST_ASSERT_EQUAL_HEX8(0xAB, tr->head[0]->data[0]);

    // Clean up.
    txfer_retire(&self, tr, true);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
    TEST_ASSERT_EQUAL_size_t(0, alloc_fr.allocated_fragments);
}

static void test_canard_publish_unpinned_best_effort_v1_1(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);
    self.tx.fd = false;

    // Publish an unpinned best-effort transfer.
    const uint8_t              data[]  = { 0xCD };
    const canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    const uint64_t             topic   = (uint64_t)CANARD_SUBJECT_ID_MAX_1v0 + 1U;
    const bool                 ok =
      canard_publish(&self, 1000000, 1, canard_prio_nominal, topic, 1, payload, CANARD_USER_CONTEXT_NULL, false);
    TEST_ASSERT_TRUE(ok);
    canard_txfer_t* const tr = LIST_HEAD(self.tx.agewise, canard_txfer_t, list_agewise);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_EQUAL_size_t(6, FRAME_SIZE(tr->head[0]));
    TEST_ASSERT_EQUAL_HEX8(0xCD, tr->head[0]->data[4]);

    // Clean up.
    txfer_retire(&self, tr, true);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
    TEST_ASSERT_EQUAL_size_t(0, alloc_fr.allocated_fragments);
}

static void test_canard_publish_pinned_reliable_v1_1(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);
    self.tx.fd = false;

    // Publish a pinned reliable transfer.
    const uint8_t              data[]  = { 0xEF };
    const canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    const uint64_t             topic   = 2;
    const bool                 ok =
      canard_publish(&self, 1000000, 1, canard_prio_nominal, topic, 2, payload, CANARD_USER_CONTEXT_NULL, true);
    TEST_ASSERT_TRUE(ok);
    canard_txfer_t* const tr = LIST_HEAD(self.tx.agewise, canard_txfer_t, list_agewise);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_EQUAL_size_t(6, FRAME_SIZE(tr->head[0]));
    TEST_ASSERT_EQUAL_HEX8(0xEF, tr->head[0]->data[4]);

    // Clean up.
    txfer_retire(&self, tr, true);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
    TEST_ASSERT_EQUAL_size_t(0, alloc_fr.allocated_fragments);
}

static void test_canard_0v1_publish_basic(void)
{
    canard_t                 self;
    instrumented_allocator_t alloc_tr;
    instrumented_allocator_t alloc_fr;
    test_context_t           ctx;
    setup_canard_for_tx_push(&self, &alloc_tr, &alloc_fr, &ctx);
    self.node_id = 1;

    // Publish a v0 transfer.
    const uint8_t              data[]  = { 1, 2 };
    const canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    const bool                 ok = canard_0v1_publish(&self, 1000000, 1, canard_prio_nominal, 10, 0xFFFF, 3, payload);
    TEST_ASSERT_TRUE(ok);
    canard_txfer_t* const tr = LIST_HEAD(self.tx.agewise, canard_txfer_t, list_agewise);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_FALSE(tr->fd);
    TEST_ASSERT_EQUAL_size_t(3, FRAME_SIZE(tr->head[0]));
    TEST_ASSERT_EQUAL_HEX8(1, tr->head[0]->data[0]);
    TEST_ASSERT_EQUAL_HEX8(2, tr->head[0]->data[1]);

    // Clean up.
    txfer_retire(&self, tr, true);
    TEST_ASSERT_EQUAL_size_t(0, alloc_tr.allocated_fragments);
    TEST_ASSERT_EQUAL_size_t(0, alloc_fr.allocated_fragments);
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
    // tx misc.
    RUN_TEST(test_transfer_kind_is_v0);
    RUN_TEST(test_tx_ack_timeout);
    RUN_TEST(test_txfer_prio);
    // tx_push.
    RUN_TEST(test_tx_push_basic_v1_classic);
    RUN_TEST(test_tx_push_basic_fd_multi_iface);
    RUN_TEST(test_tx_push_v0_uses_classic_mtu);
    RUN_TEST(test_tx_push_oom_frame_alloc);
    RUN_TEST(test_tx_push_reliable_oom_removes_index);
    RUN_TEST(test_tx_push_queue_capacity_too_small);
    RUN_TEST(test_tx_ensure_queue_space_no_sacrifice);
    RUN_TEST(test_tx_push_sacrifice_oldest);
    RUN_TEST(test_tx_push_duplicate_reliable_transfer);
    RUN_TEST(test_tx_push_fifo_same_priority);
    RUN_TEST(test_tx_pending_same_topic_tid_wrap);
    RUN_TEST(test_tx_pending_same_topic_tid_deadline);
    RUN_TEST(test_tx_pending_diff_topic_deadline);
    RUN_TEST(test_tx_comparator_branches);
    RUN_TEST(test_tx_make_pending_orders);
    RUN_TEST(test_tx_receive_ack_retires_feedback);
    RUN_TEST(test_tx_receive_ack_scan_miss);
    RUN_TEST(test_tx_receive_ack_no_match);
    // tx scheduling and retire.
    RUN_TEST(test_txfer_is_pending_false);
    RUN_TEST(test_txfer_retire_updates_iter);
    RUN_TEST(test_tx_stage_reliable_if_inserts);
    RUN_TEST(test_tx_stage_reliable_if_tiebreak);
    RUN_TEST(test_tx_stage_reliable_if_orders);
    RUN_TEST(test_tx_stage_reliable_if_deadline_too_close);
    RUN_TEST(test_tx_promote_staged_requeues);
    // canard_publish.
    RUN_TEST(test_canard_publish_pinned_best_effort_v1_0);
    RUN_TEST(test_canard_publish_unpinned_best_effort_v1_1);
    RUN_TEST(test_canard_publish_pinned_reliable_v1_1);
    RUN_TEST(test_canard_0v1_publish_basic);
    return UNITY_END();
}
