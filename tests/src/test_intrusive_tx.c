// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal Development Team.

#include "canard.c" // NOLINT(bugprone-suspicious-include)
#include "helpers.h"
#include <unity.h>

// Test context for callbacks.
typedef struct
{
    canard_us_t now;
} test_context_t;

// Monotonic time callback.
static canard_us_t mock_now(canard_t* const self)
{
    const test_context_t* const ctx = (const test_context_t*)self->user_context;
    return (ctx != NULL) ? ctx->now : 0;
}

// Minimal vtable used by tests.
static const canard_vtable_t test_vtable = {
    .now    = mock_now,
    .on_p2p = NULL,
    .tx     = NULL,
    .filter = NULL,
};

// Build a minimal instance with instrumented allocators.
static void init_canard(canard_t* const                 self,
                        test_context_t* const           ctx,
                        instrumented_allocator_t* const alloc,
                        const size_t                    queue_capacity)
{
    instrumented_allocator_new(alloc);
    memset(self, 0, sizeof(*self));
    memset(ctx, 0, sizeof(*ctx));
    self->user_context      = ctx;
    self->vtable            = &test_vtable;
    self->tx.queue_capacity = queue_capacity;
    self->mem.tx_transfer   = instrumented_allocator_make_resource(alloc);
    self->mem.tx_frame      = instrumented_allocator_make_resource(alloc);
}

// Release all queued transfers.
static void free_all_transfers(canard_t* const self)
{
    canard_txfer_t* tr = LIST_HEAD(self->tx.agewise, canard_txfer_t, list_agewise);
    while (tr != NULL) {
        canard_txfer_t* const next = LIST_NEXT(tr, canard_txfer_t, list_agewise);
        txfer_retire(self, tr);
        tr = next;
    }
}

// Count frames in a frame chain.
static size_t count_frames(const tx_frame_t* head)
{
    size_t count = 0;
    while (head != NULL) {
        count++;
        head = head->next;
    }
    return count;
}

// Validate single-frame spooling.
static void test_tx_spool_single_frame(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 16U);

    const byte_t               data[]  = { 1U, 2U, 3U, 4U };
    const canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* const head = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 7U, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1U, count_frames(head));
    TEST_ASSERT_EQUAL_size_t(5U, canard_dlc_to_len[head->dlc]);
    TEST_ASSERT_EQUAL_HEX8(0xE7, head->data[4]);

    canard_refcount_dec(&self, tx_frame_view(head));
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate multi-frame spooling.
static void test_tx_spool_multi_frame(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 16U);

    byte_t data[10U];
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = (byte_t)i;
    }
    const canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    tx_frame_t* const head = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 3U, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(2U, count_frames(head));
    TEST_ASSERT_EQUAL_HEX8(0xA3, head->data[7]);
    TEST_ASSERT_NOT_NULL(head->next);
    TEST_ASSERT_EQUAL_HEX8(0x43, head->next->data[5]);

    // Drop the whole frame chain.
    tx_frame_t* f = head;
    while (f != NULL) {
        tx_frame_t* const next = f->next;
        canard_refcount_dec(&self, tx_frame_view(f));
        f = next;
    }
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate successful TX enqueue.
static void test_tx_push_basic(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);

    const byte_t               data[]  = { 0xAAU };
    const canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    canard_txfer_t* const      tr      = txfer_new(
      self.mem.tx_transfer, 1000, 5U, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_TRUE(tx_push(&self, tr, false, 1U, payload, CRC_INITIAL));
    TEST_ASSERT_EQUAL_size_t(1U, self.tx.queue_size);
    TEST_ASSERT_NOT_NULL(LIST_HEAD(self.tx.agewise, canard_txfer_t, list_agewise));
    TEST_ASSERT_TRUE(cavl2_is_inserted(self.tx.pending[0], &tr->index_pending[0]));

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate queue-capacity rejection.
static void test_tx_push_capacity_reject(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 0U);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    canard_txfer_t* const      tr      = txfer_new(
      self.mem.tx_transfer, 1000, 1U, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_FALSE(tx_push(&self, tr, false, 1U, payload, CRC_INITIAL));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.tx_capacity);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate OOM path while spooling.
static void test_tx_push_oom(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);

    // One fragment for transfer object only; frame allocation will fail.
    alloc.limit_fragments = 1U;

    const byte_t               data[]  = { 1U, 2U, 3U, 4U };
    const canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    canard_txfer_t* const      tr      = txfer_new(
      self.mem.tx_transfer, 1000, 3U, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, CANARD_USER_CONTEXT_NULL);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_FALSE(tx_push(&self, tr, false, 1U, payload, CRC_INITIAL));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.oom);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate publish argument checking.
static void test_canard_publish_validation(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);

    const canard_bytes_chain_t empty_payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    TEST_ASSERT_FALSE(
      canard_publish(&self, 1000, 0U, canard_prio_nominal, 10U, 0U, empty_payload, CANARD_USER_CONTEXT_NULL));
    TEST_ASSERT_FALSE(canard_publish(
      &self, 1000, 1U, canard_prio_nominal, CANARD_SUBJECT_ID_MAX + 1U, 0U, empty_payload, CANARD_USER_CONTEXT_NULL));
}

// Validate publish CAN-ID composition.
static void test_canard_publish_basic(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);

    const byte_t               data[]  = { 0x55U };
    const canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };

    TEST_ASSERT_TRUE(canard_publish(&self, 1000, 1U, canard_prio_high, 1234U, 17U, payload, CANARD_USER_CONTEXT_NULL));

    const canard_txfer_t* const tr = LIST_HEAD(self.tx.agewise, canard_txfer_t, list_agewise);
    TEST_ASSERT_NOT_NULL(tr);
    const uint32_t can_id = ((uint32_t)tr->can_id_msb) << 7U;
    TEST_ASSERT_EQUAL_UINT8(17U, (uint8_t)tr->transfer_id);
    TEST_ASSERT_NOT_EQUAL(0U, can_id & (1UL << 7U));
    TEST_ASSERT_EQUAL_UINT32(1234U, (can_id >> 8U) & CANARD_SUBJECT_ID_MAX);

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate standalone Cyphal v1.0 publish path.
static void test_canard_1v0_publish_basic(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    TEST_ASSERT_TRUE(canard_1v0_publish(&self, 1000, 1U, canard_prio_nominal, 42U, 7U, payload));

    const canard_txfer_t* const tr = LIST_HEAD(self.tx.agewise, canard_txfer_t, list_agewise);
    TEST_ASSERT_NOT_NULL(tr);
    const uint32_t can_id = ((uint32_t)tr->can_id_msb) << 7U;
    TEST_ASSERT_EQUAL_UINT32(3UL, (can_id >> 21U) & 3UL);

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate legacy v0 publish node-ID rule and nominal path.
static void test_canard_0v1_publish_basic(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };

    // Node-ID zero is rejected.
    self.node_id = 0U;
    TEST_ASSERT_FALSE(canard_0v1_publish(&self, 1000, 1U, canard_prio_nominal, 11U, 0xFFFFU, 3U, payload));

    // Non-zero node-ID is accepted.
    self.node_id = 1U;
    TEST_ASSERT_TRUE(canard_0v1_publish(&self, 1000, 1U, canard_prio_nominal, 11U, 0xFFFFU, 3U, payload));
    const canard_txfer_t* const tr = LIST_HEAD(self.tx.agewise, canard_txfer_t, list_agewise);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_EQUAL_UINT8(0U, (uint8_t)tr->fd);

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

void setUp(void) {}

void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();

    // TX frame builder.
    RUN_TEST(test_tx_spool_single_frame);
    RUN_TEST(test_tx_spool_multi_frame);

    // TX queue internals.
    RUN_TEST(test_tx_push_basic);
    RUN_TEST(test_tx_push_capacity_reject);
    RUN_TEST(test_tx_push_oom);

    // API-level TX paths.
    RUN_TEST(test_canard_publish_validation);
    RUN_TEST(test_canard_publish_basic);
    RUN_TEST(test_canard_1v0_publish_basic);
    RUN_TEST(test_canard_0v1_publish_basic);

    return UNITY_END();
}
