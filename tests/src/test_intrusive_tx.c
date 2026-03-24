// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal Development Team.

#include "canard.c" // NOLINT(bugprone-suspicious-include)
#include "helpers.h"
#include <unity.h>

// Test context for callbacks.
typedef struct
{
    canard_us_t now;
    byte_t      tx_budget[CANARD_IFACE_COUNT];
    size_t      tx_count;
} test_context_t;

// Monotonic time callback.
static canard_us_t mock_now(const canard_t* const self)
{
    const test_context_t* const ctx = (const test_context_t*)self->user_context;
    return (ctx != NULL) ? ctx->now : 0;
}

// Test TX callback with per-interface frame budgets.
static bool mock_tx(canard_t* const      self,
                    void* const          user_context,
                    const canard_us_t    deadline,
                    const uint_least8_t  iface_index,
                    const bool           fd,
                    const uint32_t       extended_can_id,
                    const canard_bytes_t can_data)
{
    (void)user_context;
    (void)deadline;
    (void)fd;
    (void)extended_can_id;
    (void)can_data;
    test_context_t* const ctx = (test_context_t*)self->user_context;
    if ((ctx == NULL) || (iface_index >= CANARD_IFACE_COUNT)) {
        return false;
    }
    ctx->tx_count++;
    if (ctx->tx_budget[iface_index] == 0U) {
        return false;
    }
    ctx->tx_budget[iface_index]--;
    return true;
}

// Minimal vtable used by tests.
static const canard_vtable_t test_vtable = { .now = mock_now, .tx = mock_tx, .filter = NULL };

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
    tx_transfer_t* tr = LIST_HEAD(self->tx.agewise, tx_transfer_t, list_agewise);
    while (tr != NULL) {
        tx_transfer_t* const next = LIST_NEXT(tr, tx_transfer_t, list_agewise);
        tx_retire(self, tr);
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

// Reconstructs the CAN-ID template from an enqueued transfer.
static uint32_t can_id_from_transfer(const tx_transfer_t* const tr)
{
    // Clangd/Clang-Tidy bug: bitfield integer promotion rules are modeled incorrectly -- the cast is not redundant.
    return ((uint32_t)tr->can_id_msb << 7U); // NOLINT(*-readability-casting)
}

// Returns the transfer-ID from the tail byte of the current frame on the given interface.
static byte_t transfer_id_from_cursor(const tx_transfer_t* const tr, const uint_least8_t iface_index)
{
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_TRUE(iface_index < CANARD_IFACE_COUNT);
    TEST_ASSERT_NOT_NULL(tr->cursor[iface_index]);
    const tx_frame_t* const frame = tr->cursor[iface_index];
    const size_t            size  = canard_dlc_to_len[frame->dlc];
    TEST_ASSERT_TRUE(size > 0U);
    return frame->data[size - 1U] & CANARD_TRANSFER_ID_MAX;
}

// Counts enqueued transfers.
static size_t count_enqueued_transfers(const canard_t* const self)
{
    size_t               count = 0;
    const tx_transfer_t* tr    = LIST_HEAD(self->tx.agewise, tx_transfer_t, list_agewise);
    while (tr != NULL) {
        count++;
        tr = LIST_NEXT(tr, tx_transfer_t, list_agewise);
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

    tx_frame_t* const head = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 7U, sizeof(data), payload);
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

    tx_frame_t* const head = tx_spool(&self, CRC_INITIAL, CANARD_MTU_CAN_CLASSIC, 3U, sizeof(data), payload);
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
    tx_transfer_t* const tr = tx_transfer_new(&self, 1000, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, NULL);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_TRUE(tx_push(&self, tr, false, 1U, 5U, payload, CRC_INITIAL));
    TEST_ASSERT_EQUAL_size_t(1U, self.tx.queue_size);
    TEST_ASSERT_NOT_NULL(LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise));
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
    tx_transfer_t* const tr = tx_transfer_new(&self, 1000, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, NULL);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_FALSE(tx_push(&self, tr, false, 1U, 1U, payload, CRC_INITIAL));
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
    tx_transfer_t* const tr = tx_transfer_new(&self, 1000, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, NULL);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_FALSE(tx_push(&self, tr, false, 1U, 3U, payload, CRC_INITIAL));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.oom);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// first_frame_departed flips only after a successful first-frame ejection.
static void test_tx_first_frame_departure_flag(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 16U);

    const byte_t               data[]  = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };
    const canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    const uint32_t             can_id  = (((uint32_t)canard_prio_nominal) << PRIO_SHIFT) | (123U << 8U) | (1UL << 7U);
    tx_transfer_t* const       tr      = tx_transfer_new(&self, 1000, can_id, false, NULL);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_TRUE(tx_push(&self, tr, false, 1U, 21U, payload, CRC_INITIAL));
    TEST_ASSERT_EQUAL_UINT8(1U, (uint8_t)tr->multi_frame);
    TEST_ASSERT_EQUAL_UINT8(0U, (uint8_t)tr->first_frame_departed);

    tx_eject_pending(&self, 0U); // Backpressure: budget is zero by default.
    TEST_ASSERT_EQUAL_UINT8(0U, (uint8_t)tr->first_frame_departed);
    TEST_ASSERT_EQUAL_size_t(1U, ctx.tx_count);

    ctx.tx_budget[0] = 1U;
    tx_eject_pending(&self, 0U);
    TEST_ASSERT_EQUAL_UINT8(1U, (uint8_t)tr->first_frame_departed);
    TEST_ASSERT_EQUAL_size_t(3U, ctx.tx_count); // one accepted frame + one rejected retry

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Purge keeps unstarted multi-frame transfers.
static void test_tx_purge_continuations_keeps_unstarted_multi_frame(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 16U);

    const byte_t               data[]  = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };
    const canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    tx_transfer_t* const       tr      = tx_transfer_new(
      &self, 1000, (((uint32_t)canard_prio_nominal) << PRIO_SHIFT) | (1U << 8U) | (1UL << 7U), false, NULL);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_TRUE(tx_push(&self, tr, false, 1U, 1U, payload, CRC_INITIAL));
    TEST_ASSERT_EQUAL_UINT8(1U, (uint8_t)tr->multi_frame);
    TEST_ASSERT_EQUAL_UINT8(0U, (uint8_t)tr->first_frame_departed);

    tx_purge_continuations(&self);
    TEST_ASSERT_EQUAL_size_t(1U, count_enqueued_transfers(&self));
    TEST_ASSERT_TRUE(is_listed(&self.tx.agewise, &tr->list_agewise));

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Purge removes started multi-frame transfers.
static void test_tx_purge_continuations_removes_started_multi_frame(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 16U);

    const byte_t               data[]  = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };
    const canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    tx_transfer_t* const       tr      = tx_transfer_new(
      &self, 1000, (((uint32_t)canard_prio_nominal) << PRIO_SHIFT) | (2U << 8U) | (1UL << 7U), false, NULL);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_TRUE(tx_push(&self, tr, false, 1U, 2U, payload, CRC_INITIAL));

    ctx.tx_budget[0] = 1U;
    tx_eject_pending(&self, 0U);
    TEST_ASSERT_EQUAL_UINT8(1U, (uint8_t)tr->first_frame_departed);
    TEST_ASSERT_EQUAL_size_t(1U, count_enqueued_transfers(&self));

    tx_purge_continuations(&self);
    TEST_ASSERT_EQUAL_size_t(0U, count_enqueued_transfers(&self));
    TEST_ASSERT_EQUAL_size_t(0U, self.tx.queue_size);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Purge does not affect started single-frame transfers.
static void test_tx_purge_continuations_keeps_started_single_frame(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 16U);

    const byte_t               data[]  = { 0xAAU };
    const canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    tx_transfer_t* const       tr      = tx_transfer_new(
      &self, 1000, (((uint32_t)canard_prio_nominal) << PRIO_SHIFT) | (3U << 8U) | (1UL << 7U), false, NULL);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_TRUE(tx_push(&self, tr, false, 3U, 3U, payload, CRC_INITIAL));
    TEST_ASSERT_EQUAL_UINT8(0U, (uint8_t)tr->multi_frame);

    ctx.tx_budget[0] = 1U;
    tx_eject_pending(&self, 0U);
    TEST_ASSERT_EQUAL_UINT8(1U, (uint8_t)tr->first_frame_departed);
    TEST_ASSERT_TRUE(cavl2_is_inserted(self.tx.pending[1], &tr->index_pending[1]));

    tx_purge_continuations(&self);
    TEST_ASSERT_EQUAL_size_t(1U, count_enqueued_transfers(&self));
    TEST_ASSERT_TRUE(is_listed(&self.tx.agewise, &tr->list_agewise));

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Purge removes only started multi-frame transfers from a mixed queue.
static void test_tx_purge_continuations_mixed_queue(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 32U);

    const byte_t               mf_a[]        = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };
    const byte_t               mf_b[]        = { 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U };
    const byte_t               sf[]          = { 0x55U };
    const canard_bytes_chain_t payload_a     = { .bytes = { .size = sizeof(mf_a), .data = mf_a }, .next = NULL };
    const canard_bytes_chain_t payload_b     = { .bytes = { .size = sizeof(mf_b), .data = mf_b }, .next = NULL };
    const canard_bytes_chain_t payload_c     = { .bytes = { .size = sizeof(sf), .data = sf }, .next = NULL };
    tx_transfer_t* const       started_multi = tx_transfer_new(
      &self, 1000, (((uint32_t)canard_prio_optional) << PRIO_SHIFT) | (500U << 8U) | (1UL << 7U), false, NULL);
    tx_transfer_t* const unstarted_multi = tx_transfer_new(
      &self, 1000, (((uint32_t)canard_prio_nominal) << PRIO_SHIFT) | (200U << 8U) | (1UL << 7U), false, NULL);
    tx_transfer_t* const started_single = tx_transfer_new(
      &self, 1000, (((uint32_t)canard_prio_exceptional) << PRIO_SHIFT) | (1U << 8U) | (1UL << 7U), false, NULL);
    TEST_ASSERT_NOT_NULL(started_multi);
    TEST_ASSERT_NOT_NULL(unstarted_multi);
    TEST_ASSERT_NOT_NULL(started_single);

    TEST_ASSERT_TRUE(tx_push(&self, started_multi, false, 1U, 10U, payload_a, CRC_INITIAL));
    ctx.tx_budget[0] = 1U;
    tx_eject_pending(&self, 0U); // Start the first multi-frame transfer.
    TEST_ASSERT_EQUAL_UINT8(1U, (uint8_t)started_multi->first_frame_departed);

    TEST_ASSERT_TRUE(tx_push(&self, unstarted_multi, false, 1U, 11U, payload_b, CRC_INITIAL));
    TEST_ASSERT_TRUE(tx_push(&self, started_single, false, 3U, 12U, payload_c, CRC_INITIAL));

    ctx.tx_budget[0] = 1U;
    tx_eject_pending(&self, 0U); // Start the single-frame transfer on iface 0.
    TEST_ASSERT_EQUAL_UINT8(1U, (uint8_t)started_single->first_frame_departed);
    TEST_ASSERT_EQUAL_size_t(3U, count_enqueued_transfers(&self));

    tx_purge_continuations(&self);
    TEST_ASSERT_EQUAL_size_t(2U, count_enqueued_transfers(&self));
    TEST_ASSERT_TRUE(is_listed(&self.tx.agewise, &unstarted_multi->list_agewise));
    TEST_ASSERT_TRUE(is_listed(&self.tx.agewise, &started_single->list_agewise));

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Node-ID change purges started multi-frame continuations but keeps unstarted ones.
static void test_canard_set_node_id_purges_started_multiframe_only(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 16U);
    self.node_id = 10U;

    const byte_t               started_data[]  = { 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U };
    const byte_t               fresh_data[]    = { 8U, 7U, 6U, 5U, 4U, 3U, 2U, 1U };
    const canard_bytes_chain_t payload_started = { .bytes = { .size = sizeof(started_data), .data = started_data },
                                                   .next  = NULL };
    const canard_bytes_chain_t payload_fresh   = { .bytes = { .size = sizeof(fresh_data), .data = fresh_data },
                                                   .next  = NULL };
    tx_transfer_t* const       started_multi   = tx_transfer_new(
      &self, 1000, (((uint32_t)canard_prio_nominal) << PRIO_SHIFT) | (10U << 8U) | (1UL << 7U), false, NULL);
    tx_transfer_t* const fresh_multi = tx_transfer_new(
      &self, 1000, (((uint32_t)canard_prio_nominal) << PRIO_SHIFT) | (11U << 8U) | (1UL << 7U), false, NULL);
    TEST_ASSERT_NOT_NULL(started_multi);
    TEST_ASSERT_NOT_NULL(fresh_multi);

    TEST_ASSERT_TRUE(tx_push(&self, started_multi, false, 1U, 20U, payload_started, CRC_INITIAL));
    ctx.tx_budget[0] = 1U;
    tx_eject_pending(&self, 0U);
    TEST_ASSERT_EQUAL_UINT8(1U, (uint8_t)started_multi->first_frame_departed);

    TEST_ASSERT_TRUE(tx_push(&self, fresh_multi, false, 1U, 21U, payload_fresh, CRC_INITIAL));
    TEST_ASSERT_EQUAL_size_t(2U, count_enqueued_transfers(&self));

    TEST_ASSERT_TRUE(canard_set_node_id(&self, 11U));
    TEST_ASSERT_EQUAL_size_t(1U, count_enqueued_transfers(&self));
    TEST_ASSERT_TRUE(is_listed(&self.tx.agewise, &fresh_multi->list_agewise));

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Node-ID assignment to the same value does not purge.
static void test_canard_set_node_id_same_value_keeps_queue(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 16U);
    self.node_id = 10U;

    const byte_t               data[]  = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };
    const canard_bytes_chain_t payload = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    tx_transfer_t* const       tr      = tx_transfer_new(
      &self, 1000, (((uint32_t)canard_prio_nominal) << PRIO_SHIFT) | (12U << 8U) | (1UL << 7U), false, NULL);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_TRUE(tx_push(&self, tr, false, 1U, 22U, payload, CRC_INITIAL));
    ctx.tx_budget[0] = 1U;
    tx_eject_pending(&self, 0U);
    TEST_ASSERT_EQUAL_UINT8(1U, (uint8_t)tr->first_frame_departed);

    TEST_ASSERT_TRUE(canard_set_node_id(&self, 10U));
    TEST_ASSERT_EQUAL_size_t(1U, count_enqueued_transfers(&self));
    TEST_ASSERT_TRUE(is_listed(&self.tx.agewise, &tr->list_agewise));

    free_all_transfers(&self);
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
    TEST_ASSERT_FALSE(canard_publish_16b(&self, 1000, 0U, canard_prio_nominal, 10U, 0U, empty_payload, NULL));
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

    TEST_ASSERT_TRUE(canard_publish_16b(&self, 1000, 1U, canard_prio_high, 1234U, 17U, payload, NULL));

    const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
    TEST_ASSERT_NOT_NULL(tr);
    const uint32_t can_id = (uint32_t)(tr->can_id_msb << 7U);
    TEST_ASSERT_EQUAL_UINT8(17U, transfer_id_from_cursor(tr, 0U));
    TEST_ASSERT_NOT_EQUAL(0U, can_id & (1UL << 7U));
    TEST_ASSERT_EQUAL_UINT32(1234U, (can_id >> 8U) & CANARD_SUBJECT_ID_MAX);

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate publish accepts the maximal v1.1 subject-ID and composes CAN ID as specified.
static void test_canard_publish_max_subject_encoding(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    TEST_ASSERT_TRUE(
      canard_publish_16b(&self, 1000, 1U, canard_prio_nominal, CANARD_SUBJECT_ID_MAX, 3U, payload, NULL));

    const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
    TEST_ASSERT_NOT_NULL(tr);
    const uint32_t can_id = can_id_from_transfer(tr);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)canard_prio_nominal, (can_id >> 26U) & 7U);
    TEST_ASSERT_EQUAL_UINT32(0U, (can_id >> 25U) & 1U); // service=0
    TEST_ASSERT_EQUAL_UINT32(0U, (can_id >> 24U) & 1U); // reserved=0
    TEST_ASSERT_EQUAL_UINT32((uint32_t)CANARD_SUBJECT_ID_MAX, (can_id >> 8U) & 0xFFFFU);
    TEST_ASSERT_EQUAL_UINT32(1U, (can_id >> 7U) & 1U); // v1.1 message marker

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate Cyphal v1.0 publish path through the unified publish API.
static void test_canard_1v0_publish_basic(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    TEST_ASSERT_TRUE(canard_publish_13b(&self, 1000, 1U, canard_prio_nominal, 42U, 7U, payload, NULL));

    const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
    TEST_ASSERT_NOT_NULL(tr);
    const uint32_t can_id = (uint32_t)(tr->can_id_msb << 7U);
    TEST_ASSERT_EQUAL_UINT32(3UL, (can_id >> 21U) & 3UL);

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate legacy v0 publish node-ID rule and nominal path.
static void test_canard_v0_publish_basic(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };

    // Node-ID zero is rejected.
    self.node_id = 0U;
    TEST_ASSERT_FALSE(canard_v0_publish(&self, 1000, 1U, canard_prio_nominal, 11U, 0xFFFFU, 3U, payload, NULL));

    // Non-zero node-ID is accepted.
    self.node_id = 1U;
    TEST_ASSERT_TRUE(canard_v0_publish(&self, 1000, 1U, canard_prio_nominal, 11U, 0xFFFFU, 3U, payload, NULL));
    const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_EQUAL_UINT8(0U, (uint8_t)tr->fd);

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate Cyphal v1.0 service request/response CAN-ID composition.
static void test_canard_1v0_service_basic(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);
    self.tx.fd   = true;
    self.node_id = 11U;

    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    TEST_ASSERT_TRUE(canard_request(&self, 1000, canard_prio_nominal, 430U, 24U, 5U, payload, NULL));
    TEST_ASSERT_TRUE(canard_respond(&self, 1000, canard_prio_nominal, 430U, 24U, 6U, payload, NULL));

    const tx_transfer_t* const req = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
    TEST_ASSERT_NOT_NULL(req);
    const tx_transfer_t* const res = LIST_NEXT(req, tx_transfer_t, list_agewise);
    TEST_ASSERT_NOT_NULL(res);

    const uint32_t expected_req_id = (((uint32_t)canard_prio_nominal) << PRIO_SHIFT) | //
                                     (1UL << 25U) | (1UL << 24U) | ((uint32_t)430U << 14U) | ((uint32_t)24U << 7U);
    const uint32_t expected_res_id = (((uint32_t)canard_prio_nominal) << PRIO_SHIFT) | //
                                     (1UL << 25U) | ((uint32_t)430U << 14U) | ((uint32_t)24U << 7U);
    TEST_ASSERT_EQUAL_UINT8(1U, (uint8_t)req->fd);
    TEST_ASSERT_EQUAL_UINT8(1U, (uint8_t)res->fd);
    TEST_ASSERT_EQUAL_UINT8(5U, transfer_id_from_cursor(req, 0U));
    TEST_ASSERT_EQUAL_UINT8(6U, transfer_id_from_cursor(res, 0U));
    TEST_ASSERT_EQUAL_UINT32(expected_req_id, can_id_from_transfer(req));
    TEST_ASSERT_EQUAL_UINT32(expected_res_id, can_id_from_transfer(res));

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate Cyphal v1.0 service validation branches.
static void test_canard_1v0_service_validation(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);
    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    const canard_bytes_chain_t bad     = { .bytes = { .size = 1U, .data = NULL }, .next = NULL };

    TEST_ASSERT_FALSE(
      canard_request(&self, 1000, canard_prio_nominal, CANARD_SERVICE_ID_MAX + 1U, 24U, 0U, payload, NULL));
    TEST_ASSERT_FALSE(
      canard_respond(&self, 1000, canard_prio_nominal, 430U, CANARD_NODE_ID_MAX + 1U, 0U, payload, NULL));
    TEST_ASSERT_FALSE(canard_request(&self, 1000, canard_prio_nominal, 430U, 24U, 0U, bad, NULL));

    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate Cyphal v1.0 service transfer allocation failure.
static void test_canard_1v0_service_oom(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);
    alloc.limit_fragments = 0U;

    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    TEST_ASSERT_FALSE(canard_request(&self, 1000, canard_prio_nominal, 430U, 24U, 1U, payload, NULL));
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate Cyphal v1.0 service queue-capacity failure.
static void test_canard_1v0_service_capacity(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 0U);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    TEST_ASSERT_FALSE(canard_respond(&self, 1000, canard_prio_nominal, 430U, 24U, 1U, payload, NULL));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.tx_capacity);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate UAVCAN v0 service request/response CAN-ID composition.
static void test_canard_v0_service_basic(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);
    self.tx.fd   = true; // Legacy service TX must still force Classic CAN.
    self.node_id = 11U;

    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    TEST_ASSERT_TRUE(canard_v0_request(&self, 1000, canard_prio_nominal, 0x37U, 0xBEEFU, 24U, 5U, payload, NULL));
    TEST_ASSERT_TRUE(canard_v0_respond(&self, 1000, canard_prio_nominal, 0x37U, 0xBEEFU, 24U, 6U, payload, NULL));

    const tx_transfer_t* const req = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
    TEST_ASSERT_NOT_NULL(req);
    const tx_transfer_t* const res = LIST_NEXT(req, tx_transfer_t, list_agewise);
    TEST_ASSERT_NOT_NULL(res);

    const uint32_t expected_req_id = (((uint32_t)canard_prio_nominal) << 26U) | //
                                     ((uint32_t)0x37U << 16U) | (1UL << 15U) | ((uint32_t)24U << 8U) | (1UL << 7U);
    const uint32_t expected_res_id = (((uint32_t)canard_prio_nominal) << 26U) | //
                                     ((uint32_t)0x37U << 16U) | ((uint32_t)24U << 8U) | (1UL << 7U);
    TEST_ASSERT_EQUAL_UINT8(0U, (uint8_t)req->fd);
    TEST_ASSERT_EQUAL_UINT8(0U, (uint8_t)res->fd);
    TEST_ASSERT_EQUAL_UINT8(5U, transfer_id_from_cursor(req, 0U));
    TEST_ASSERT_EQUAL_UINT8(6U, transfer_id_from_cursor(res, 0U));
    TEST_ASSERT_EQUAL_UINT32(expected_req_id, can_id_from_transfer(req));
    TEST_ASSERT_EQUAL_UINT32(expected_res_id, can_id_from_transfer(res));

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate UAVCAN v0 service validation branches.
static void test_canard_v0_service_validation(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);
    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    const canard_bytes_chain_t bad     = { .bytes = { .size = 1U, .data = NULL }, .next = NULL };

    self.node_id = 0U;
    TEST_ASSERT_FALSE(canard_v0_request(&self, 1000, canard_prio_nominal, 1U, 0xFFFFU, 24U, 0U, payload, NULL));
    self.node_id = 1U;
    TEST_ASSERT_FALSE(canard_v0_respond(&self, 1000, canard_prio_nominal, 1U, 0xFFFFU, 0U, 0U, payload, NULL));
    TEST_ASSERT_FALSE(
      canard_v0_request(&self, 1000, canard_prio_nominal, 1U, 0xFFFFU, CANARD_NODE_ID_MAX + 1U, 0U, payload, NULL));
    TEST_ASSERT_FALSE(canard_v0_request(&self, 1000, canard_prio_nominal, 1U, 0xFFFFU, 24U, 0U, bad, NULL));

    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate UAVCAN v0 service transfer allocation failure.
static void test_canard_v0_service_oom(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);
    alloc.limit_fragments = 0U;
    self.node_id          = 1U;

    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    TEST_ASSERT_FALSE(canard_v0_request(&self, 1000, canard_prio_nominal, 1U, 0xBEEFU, 24U, 1U, payload, NULL));
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate UAVCAN v0 service queue-capacity failure.
static void test_canard_v0_service_capacity(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 0U);
    self.node_id = 1U;

    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    TEST_ASSERT_FALSE(canard_v0_respond(&self, 1000, canard_prio_nominal, 1U, 0xBEEFU, 24U, 1U, payload, NULL));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.tx_capacity);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// =============================================
// CAN ID specification compliance tests.
// These tests verify CAN ID composition against hardcoded literals independently derived from the specifications,
// ensuring that a systematic bit-field error cannot hide behind recomputation using the same expressions.
// =============================================

// Cyphal v1.0 message broadcast CAN ID compliance.
// Spec layout: [28:26]=prio [25]=0 [24]=0 [23]=0 [22:21]=11 [20:8]=subject_id [7:0]=0 (template, source filled later)
static void test_1v0_publish_can_id_compliance(void)
{
    canard_t                   self;
    test_context_t             ctx;
    instrumented_allocator_t   alloc;
    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };

    // Case A: prio=exceptional(0), subject_id=0
    init_canard(&self, &ctx, &alloc, 8U);
    TEST_ASSERT_TRUE(canard_publish_13b(&self, 1000, 1U, canard_prio_exceptional, 0U, 0U, payload, NULL));
    {
        const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
        TEST_ASSERT_NOT_NULL(tr);
        const uint32_t cid = can_id_from_transfer(tr);
        TEST_ASSERT_EQUAL_HEX32(0x00600000UL, cid);
        TEST_ASSERT_EQUAL_UINT32(0U, (cid >> 26U) & 7U);     // priority
        TEST_ASSERT_EQUAL_UINT32(0U, (cid >> 25U) & 1U);     // service=0
        TEST_ASSERT_EQUAL_UINT32(0U, (cid >> 24U) & 1U);     // anonymous=0
        TEST_ASSERT_EQUAL_UINT32(0U, (cid >> 23U) & 1U);     // reserved
        TEST_ASSERT_EQUAL_UINT32(3U, (cid >> 21U) & 3U);     // reserved=11
        TEST_ASSERT_EQUAL_UINT32(0U, (cid >> 8U) & 0x1FFFU); // subject_id
        TEST_ASSERT_EQUAL_UINT32(0U, cid & 0xFFU);           // bits[7:0]=0
    }
    free_all_transfers(&self);

    // Case B: prio=optional(7), subject_id=8191
    init_canard(&self, &ctx, &alloc, 8U);
    TEST_ASSERT_TRUE(canard_publish_13b(&self, 1000, 1U, canard_prio_optional, 8191U, 0U, payload, NULL));
    {
        const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
        TEST_ASSERT_NOT_NULL(tr);
        const uint32_t cid = can_id_from_transfer(tr);
        TEST_ASSERT_EQUAL_HEX32(0x1C7FFF00UL, cid);
        TEST_ASSERT_EQUAL_UINT32(7U, (cid >> 26U) & 7U);
        TEST_ASSERT_EQUAL_UINT32(8191U, (cid >> 8U) & 0x1FFFU);
    }
    free_all_transfers(&self);

    // Case C: prio=high(3), subject_id=42
    init_canard(&self, &ctx, &alloc, 8U);
    TEST_ASSERT_TRUE(canard_publish_13b(&self, 1000, 1U, canard_prio_high, 42U, 0U, payload, NULL));
    {
        const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
        TEST_ASSERT_NOT_NULL(tr);
        const uint32_t cid = can_id_from_transfer(tr);
        TEST_ASSERT_EQUAL_HEX32(0x0C602A00UL, cid);
        TEST_ASSERT_EQUAL_UINT32(3U, (cid >> 26U) & 7U);
        TEST_ASSERT_EQUAL_UINT32(42U, (cid >> 8U) & 0x1FFFU);
    }
    free_all_transfers(&self);
}

// Cyphal v1.0 service request CAN ID compliance.
// Spec layout: [28:26]=prio [25]=1 [24]=1 [23]=0 [22:14]=service_id [13:7]=dest [6:0]=0 (template)
static void test_1v0_request_can_id_compliance(void)
{
    canard_t                   self;
    test_context_t             ctx;
    instrumented_allocator_t   alloc;
    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };

    // Case A: prio=exceptional(0), service_id=0, dest=1
    init_canard(&self, &ctx, &alloc, 8U);
    self.node_id = 10U;
    TEST_ASSERT_TRUE(canard_request(&self, 1000, canard_prio_exceptional, 0U, 1U, 0U, payload, NULL));
    {
        const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
        TEST_ASSERT_NOT_NULL(tr);
        const uint32_t cid = can_id_from_transfer(tr);
        TEST_ASSERT_EQUAL_HEX32(0x03000080UL, cid);
        TEST_ASSERT_EQUAL_UINT32(0U, (cid >> 26U) & 7U);     // priority
        TEST_ASSERT_EQUAL_UINT32(1U, (cid >> 25U) & 1U);     // service=1
        TEST_ASSERT_EQUAL_UINT32(1U, (cid >> 24U) & 1U);     // request=1
        TEST_ASSERT_EQUAL_UINT32(0U, (cid >> 23U) & 1U);     // reserved
        TEST_ASSERT_EQUAL_UINT32(0U, (cid >> 14U) & 0x1FFU); // service_id
        TEST_ASSERT_EQUAL_UINT32(1U, (cid >> 7U) & 0x7FU);   // dest
    }
    free_all_transfers(&self);

    // Case B: prio=optional(7), service_id=511, dest=127
    init_canard(&self, &ctx, &alloc, 8U);
    self.node_id = 10U;
    TEST_ASSERT_TRUE(canard_request(&self, 1000, canard_prio_optional, 511U, 127U, 0U, payload, NULL));
    {
        const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
        TEST_ASSERT_NOT_NULL(tr);
        const uint32_t cid = can_id_from_transfer(tr);
        TEST_ASSERT_EQUAL_HEX32(0x1F7FFF80UL, cid);
        TEST_ASSERT_EQUAL_UINT32(7U, (cid >> 26U) & 7U);
        TEST_ASSERT_EQUAL_UINT32(511U, (cid >> 14U) & 0x1FFU);
        TEST_ASSERT_EQUAL_UINT32(127U, (cid >> 7U) & 0x7FU);
    }
    free_all_transfers(&self);
}

// Cyphal v1.0 service response CAN ID compliance.
// Same as request but bit[24]=0 (response, not request).
static void test_1v0_respond_can_id_compliance(void)
{
    canard_t                   self;
    test_context_t             ctx;
    instrumented_allocator_t   alloc;
    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };

    // Case A: prio=fast(2), service_id=430, dest=24
    init_canard(&self, &ctx, &alloc, 8U);
    self.node_id = 11U;
    TEST_ASSERT_TRUE(canard_respond(&self, 1000, canard_prio_fast, 430U, 24U, 0U, payload, NULL));
    {
        const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
        TEST_ASSERT_NOT_NULL(tr);
        const uint32_t cid = can_id_from_transfer(tr);
        TEST_ASSERT_EQUAL_HEX32(0x0A6B8C00UL, cid);
        TEST_ASSERT_EQUAL_UINT32(2U, (cid >> 26U) & 7U);       // priority
        TEST_ASSERT_EQUAL_UINT32(1U, (cid >> 25U) & 1U);       // service=1
        TEST_ASSERT_EQUAL_UINT32(0U, (cid >> 24U) & 1U);       // request=0 (response)
        TEST_ASSERT_EQUAL_UINT32(430U, (cid >> 14U) & 0x1FFU); // service_id
        TEST_ASSERT_EQUAL_UINT32(24U, (cid >> 7U) & 0x7FU);    // dest
    }
    free_all_transfers(&self);

    // Case B: prio=nominal(4), service_id=1, dest=1
    init_canard(&self, &ctx, &alloc, 8U);
    self.node_id = 11U;
    TEST_ASSERT_TRUE(canard_respond(&self, 1000, canard_prio_nominal, 1U, 1U, 0U, payload, NULL));
    {
        const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
        TEST_ASSERT_NOT_NULL(tr);
        const uint32_t cid = can_id_from_transfer(tr);
        TEST_ASSERT_EQUAL_HEX32(0x12004080UL, cid);
        TEST_ASSERT_EQUAL_UINT32(4U, (cid >> 26U) & 7U);
        TEST_ASSERT_EQUAL_UINT32(1U, (cid >> 14U) & 0x1FFU);
        TEST_ASSERT_EQUAL_UINT32(1U, (cid >> 7U) & 0x7FU);
    }
    free_all_transfers(&self);
}

// UAVCAN v0 message broadcast CAN ID compliance.
// Spec layout: [28:24]=v0_prio [23:8]=dtid [7:0]=0 (template), where v0_prio=(cyphal_prio<<2)|3
static void test_v0_publish_can_id_compliance(void)
{
    canard_t                   self;
    test_context_t             ctx;
    instrumented_allocator_t   alloc;
    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };

    // Case A: prio=exceptional(0), dtid=0
    init_canard(&self, &ctx, &alloc, 8U);
    self.node_id = 1U;
    TEST_ASSERT_TRUE(canard_v0_publish(&self, 1000, 1U, canard_prio_exceptional, 0U, 0xFFFFU, 0U, payload, NULL));
    {
        const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
        TEST_ASSERT_NOT_NULL(tr);
        const uint32_t cid = can_id_from_transfer(tr);
        TEST_ASSERT_EQUAL_HEX32(0x00000000UL, cid);
        TEST_ASSERT_EQUAL_UINT32(0U, (cid >> 8U) & 0xFFFFU); // dtid
        TEST_ASSERT_EQUAL_UINT32(0U, cid & 0xFFU);           // bits[7:0]=0
    }
    free_all_transfers(&self);

    // Case B: prio=optional(7), dtid=0xFFFF
    init_canard(&self, &ctx, &alloc, 8U);
    self.node_id = 1U;
    TEST_ASSERT_TRUE(canard_v0_publish(&self, 1000, 1U, canard_prio_optional, 0xFFFFU, 0xFFFFU, 0U, payload, NULL));
    {
        const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
        TEST_ASSERT_NOT_NULL(tr);
        const uint32_t cid = can_id_from_transfer(tr);
        TEST_ASSERT_EQUAL_HEX32(0x1CFFFF00UL, cid);
        TEST_ASSERT_EQUAL_UINT32(28, (cid >> 24U) & 0x1FU); // v0_prio = (7<<2) = 28
        TEST_ASSERT_EQUAL_UINT32(0xFFFFU, (cid >> 8U) & 0xFFFFU);
    }
    free_all_transfers(&self);

    // Case C: prio=nominal(4), dtid=0x040A
    init_canard(&self, &ctx, &alloc, 8U);
    self.node_id = 1U;
    TEST_ASSERT_TRUE(canard_v0_publish(&self, 1000, 1U, canard_prio_nominal, 0x040AU, 0xFFFFU, 0U, payload, NULL));
    {
        const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
        TEST_ASSERT_NOT_NULL(tr);
        const uint32_t cid = can_id_from_transfer(tr);
        TEST_ASSERT_EQUAL_HEX32(0x10040A00UL, cid);
        TEST_ASSERT_EQUAL_UINT32(16, (cid >> 24U) & 0x1FU); // v0_prio = (4<<2) = 16
        TEST_ASSERT_EQUAL_UINT32(0x040AU, (cid >> 8U) & 0xFFFFU);
    }
    free_all_transfers(&self);
}

// UAVCAN v0 service request CAN ID compliance.
// Spec layout: [28:24]=v0_prio [23:16]=dtid [15]=1(request) [14:8]=dest [7]=1 [6:0]=0 (template)
static void test_v0_request_can_id_compliance(void)
{
    canard_t                   self;
    test_context_t             ctx;
    instrumented_allocator_t   alloc;
    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };

    // Case A: prio=exceptional(0), dti=1, dest=1
    init_canard(&self, &ctx, &alloc, 8U);
    self.node_id = 10U;
    TEST_ASSERT_TRUE(canard_v0_request(&self, 1000, canard_prio_exceptional, 1U, 0xFFFFU, 1U, 0U, payload, NULL));
    {
        const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
        TEST_ASSERT_NOT_NULL(tr);
        const uint32_t cid = can_id_from_transfer(tr);
        TEST_ASSERT_EQUAL_HEX32(0x00018180UL, cid);
        TEST_ASSERT_EQUAL_UINT32(0U, (cid >> 24U) & 0x1FU); // v0_prio = (0<<2)
        TEST_ASSERT_EQUAL_UINT32(1U, (cid >> 16U) & 0xFFU); // dtid
        TEST_ASSERT_EQUAL_UINT32(1U, (cid >> 15U) & 1U);    // request=1
        TEST_ASSERT_EQUAL_UINT32(1U, (cid >> 8U) & 0x7FU);  // dest
        TEST_ASSERT_EQUAL_UINT32(1U, (cid >> 7U) & 1U);     // service=1
    }
    free_all_transfers(&self);

    // Case B: prio=optional(7), dti=255, dest=127
    init_canard(&self, &ctx, &alloc, 8U);
    self.node_id = 10U;
    TEST_ASSERT_TRUE(canard_v0_request(&self, 1000, canard_prio_optional, 255U, 0xFFFFU, 127U, 0U, payload, NULL));
    {
        const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
        TEST_ASSERT_NOT_NULL(tr);
        const uint32_t cid = can_id_from_transfer(tr);
        TEST_ASSERT_EQUAL_HEX32(0x1CFFFF80UL, cid);
        TEST_ASSERT_EQUAL_UINT32(0x1CU, (cid >> 24U) & 0x1FU);
        TEST_ASSERT_EQUAL_UINT32(255U, (cid >> 16U) & 0xFFU);
        TEST_ASSERT_EQUAL_UINT32(1U, (cid >> 15U) & 1U);
        TEST_ASSERT_EQUAL_UINT32(127U, (cid >> 8U) & 0x7FU);
        TEST_ASSERT_EQUAL_UINT32(1U, (cid >> 7U) & 1U);
    }
    free_all_transfers(&self);
}

// UAVCAN v0 service response CAN ID compliance.
// Same as request but bit[15]=0 (response, not request).
static void test_v0_respond_can_id_compliance(void)
{
    canard_t                   self;
    test_context_t             ctx;
    instrumented_allocator_t   alloc;
    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };

    // Case A: prio=nominal(4), dti=0x37, dest=24
    init_canard(&self, &ctx, &alloc, 8U);
    self.node_id = 11U;
    TEST_ASSERT_TRUE(canard_v0_respond(&self, 1000, canard_prio_nominal, 0x37U, 0xFFFFU, 24U, 0U, payload, NULL));
    {
        const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
        TEST_ASSERT_NOT_NULL(tr);
        const uint32_t cid = can_id_from_transfer(tr);
        TEST_ASSERT_EQUAL_HEX32(0x10371880UL, cid);
        TEST_ASSERT_EQUAL_UINT32(0x10U, (cid >> 24U) & 0x1FU); // v0_prio = (4<<2) = 16
        TEST_ASSERT_EQUAL_UINT32(0x37U, (cid >> 16U) & 0xFFU); // dtid
        TEST_ASSERT_EQUAL_UINT32(0U, (cid >> 15U) & 1U);       // request=0 (response)
        TEST_ASSERT_EQUAL_UINT32(24U, (cid >> 8U) & 0x7FU);    // dest
        TEST_ASSERT_EQUAL_UINT32(1U, (cid >> 7U) & 1U);        // service=1
    }
    free_all_transfers(&self);

    // Case B: prio=immediate(1), dti=200, dest=42
    init_canard(&self, &ctx, &alloc, 8U);
    self.node_id = 11U;
    TEST_ASSERT_TRUE(canard_v0_respond(&self, 1000, canard_prio_immediate, 200U, 0xFFFFU, 42U, 0U, payload, NULL));
    {
        const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
        TEST_ASSERT_NOT_NULL(tr);
        const uint32_t cid = can_id_from_transfer(tr);
        TEST_ASSERT_EQUAL_HEX32(0x04C82A80UL, cid);
        TEST_ASSERT_EQUAL_UINT32(4U, (cid >> 24U) & 0x1FU); // v0_prio = (1<<2) = 4
        TEST_ASSERT_EQUAL_UINT32(200U, (cid >> 16U) & 0xFFU);
        TEST_ASSERT_EQUAL_UINT32(0U, (cid >> 15U) & 1U);
        TEST_ASSERT_EQUAL_UINT32(42U, (cid >> 8U) & 0x7FU);
        TEST_ASSERT_EQUAL_UINT32(1U, (cid >> 7U) & 1U);
    }
    free_all_transfers(&self);
}

// =============================================
// Group A: tx_spool boundary/CRC tests
// =============================================

// Verify single/multi frame boundary at Classic CAN MTU=8.
// 7 bytes payload: 7 < 8, single-frame. 8 bytes payload: 8 is NOT < 8, multiframe.
static void test_tx_spool_boundary_single_multi_classic(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;

    // 7 bytes: single-frame.
    {
        init_canard(&self, &ctx, &alloc, 16U);
        const byte_t               data[7] = { 1U, 2U, 3U, 4U, 5U, 6U, 7U };
        const canard_bytes_chain_t payload = { .bytes = { .size = 7U, .data = data }, .next = NULL };
        tx_frame_t* const          head    = tx_spool(&self, CRC_INITIAL, 8U, 0U, 7U, payload);
        TEST_ASSERT_NOT_NULL(head);
        TEST_ASSERT_EQUAL_size_t(1U, count_frames(head));
        // frame_size = tx_ceil(7+1) = 8. Tail byte at data[7].
        TEST_ASSERT_EQUAL_size_t(8U, canard_dlc_to_len[head->dlc]);
        const byte_t tail = head->data[7];
        TEST_ASSERT_EQUAL_HEX8(TAIL_SOT | TAIL_EOT | TAIL_TOGGLE | 0U, tail); // SOT+EOT+toggle, tid=0
        canard_refcount_dec(&self, tx_frame_view(head));
        TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
    }

    // 8 bytes: multiframe (8 is NOT < 8).
    {
        init_canard(&self, &ctx, &alloc, 16U);
        byte_t data[8];
        for (size_t i = 0; i < 8U; i++) {
            data[i] = (byte_t)(0x10U + i);
        }
        const canard_bytes_chain_t payload = { .bytes = { .size = 8U, .data = data }, .next = NULL };
        tx_frame_t* const          head    = tx_spool(&self, CRC_INITIAL, 8U, 0U, 8U, payload);
        TEST_ASSERT_NOT_NULL(head);
        TEST_ASSERT_TRUE(count_frames(head) >= 2U);
        // First frame: SOT set, EOT not set, toggle=1 (Cyphal v1).
        const byte_t tail0 = head->data[canard_dlc_to_len[head->dlc] - 1U];
        TEST_ASSERT_EQUAL_HEX8(TAIL_SOT | TAIL_TOGGLE, tail0 & (TAIL_SOT | TAIL_EOT | TAIL_TOGGLE));
        // Last frame: EOT set, SOT not set.
        tx_frame_t* last = head;
        while (last->next != NULL) {
            last = last->next;
        }
        const byte_t tail_last = last->data[canard_dlc_to_len[last->dlc] - 1U];
        TEST_ASSERT_NOT_EQUAL(0U, tail_last & TAIL_EOT);
        TEST_ASSERT_EQUAL_HEX8(0U, tail_last & TAIL_SOT);
        // Cleanup.
        tx_frame_t* f = head;
        while (f != NULL) {
            tx_frame_t* const next = f->next;
            canard_refcount_dec(&self, tx_frame_view(f));
            f = next;
        }
        TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
    }
}

// Verify single/multi frame boundary at CAN FD MTU=64.
// 63 bytes = single-frame. 64 bytes = multiframe.
static void test_tx_spool_boundary_single_multi_fd(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;

    // 63 bytes: single-frame (63 < 64).
    {
        init_canard(&self, &ctx, &alloc, 16U);
        byte_t data[63];
        for (size_t i = 0; i < 63U; i++) {
            data[i] = (byte_t)i;
        }
        const canard_bytes_chain_t payload = { .bytes = { .size = 63U, .data = data }, .next = NULL };
        tx_frame_t* const          head    = tx_spool(&self, CRC_INITIAL, 64U, 5U, 63U, payload);
        TEST_ASSERT_NOT_NULL(head);
        TEST_ASSERT_EQUAL_size_t(1U, count_frames(head));
        // frame_size = tx_ceil(63+1)=64. Tail at data[63].
        TEST_ASSERT_EQUAL_size_t(64U, canard_dlc_to_len[head->dlc]);
        const byte_t tail = head->data[63];
        TEST_ASSERT_EQUAL_HEX8(TAIL_SOT | TAIL_EOT | TAIL_TOGGLE | 5U, tail);
        canard_refcount_dec(&self, tx_frame_view(head));
        TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
    }

    // 64 bytes: multiframe (64 is NOT < 64).
    {
        init_canard(&self, &ctx, &alloc, 16U);
        byte_t data[64];
        for (size_t i = 0; i < 64U; i++) {
            data[i] = (byte_t)(0x80U + (i & 0x7FU));
        }
        const canard_bytes_chain_t payload = { .bytes = { .size = 64U, .data = data }, .next = NULL };
        tx_frame_t* const          head    = tx_spool(&self, CRC_INITIAL, 64U, 5U, 64U, payload);
        TEST_ASSERT_NOT_NULL(head);
        TEST_ASSERT_TRUE(count_frames(head) >= 2U);
        // First frame: SOT set, EOT not set.
        const byte_t tail0 = head->data[canard_dlc_to_len[head->dlc] - 1U];
        TEST_ASSERT_NOT_EQUAL(0U, tail0 & TAIL_SOT);
        TEST_ASSERT_EQUAL_HEX8(0U, tail0 & TAIL_EOT);
        // Cleanup.
        tx_frame_t* f = head;
        while (f != NULL) {
            tx_frame_t* const next = f->next;
            canard_refcount_dec(&self, tx_frame_view(f));
            f = next;
        }
        TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
    }
}

// Verify CRC split across frames with Classic CAN.
// 13 bytes payload, mtu=8, payload-per-frame=7.
// size_with_crc=15. Frame layout:
//   Frame 1: 7 payload bytes + tail  (offset=7)
//   Frame 2: 6 payload bytes + CRC high byte + tail  (offset=14)
//   Frame 3: CRC low byte + padding + tail (offset=15)
static void test_tx_spool_crc_split_across_frames(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 16U);

    byte_t data[13];
    for (size_t i = 0; i < 13U; i++) {
        data[i] = (byte_t)(0xA0U + i);
    }
    const canard_bytes_chain_t payload = { .bytes = { .size = 13U, .data = data }, .next = NULL };
    tx_frame_t* const          head    = tx_spool(&self, CRC_INITIAL, 8U, 2U, 13U, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(3U, count_frames(head));

    // Compute expected CRC over all 13 payload bytes (no padding in this case).
    const uint16_t crc = crc_add(CRC_INITIAL, 13U, data);

    // Frame 1: 7 payload bytes, tail with SOT.
    TEST_ASSERT_EQUAL_size_t(8U, canard_dlc_to_len[head->dlc]);
    for (size_t i = 0; i < 7U; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(TAIL_SOT | TAIL_TOGGLE | 2U, head->data[7]); // SOT, toggle=1

    // Frame 2: 6 payload bytes + CRC high byte, then tail.
    const tx_frame_t* const f2 = head->next;
    TEST_ASSERT_NOT_NULL(f2);
    TEST_ASSERT_EQUAL_size_t(8U, canard_dlc_to_len[f2->dlc]);
    for (size_t i = 0; i < 6U; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[7U + i], f2->data[i]);
    }
    TEST_ASSERT_EQUAL_HEX8((byte_t)(((unsigned)crc >> 8U) & 0xFFU), f2->data[6]); // CRC high byte
    TEST_ASSERT_EQUAL_HEX8(2U, f2->data[7] & (TAIL_SOT | TAIL_EOT | TAIL_TOGGLE | CANARD_TRANSFER_ID_MAX));
    // toggle=0 (second frame)

    // Frame 3: CRC low byte + tail. Frame size = tx_ceil(1+1) = 2.
    const tx_frame_t* const f3 = f2->next;
    TEST_ASSERT_NOT_NULL(f3);
    TEST_ASSERT_EQUAL_size_t(2U, canard_dlc_to_len[f3->dlc]);
    TEST_ASSERT_EQUAL_HEX8((byte_t)((unsigned)crc & 0xFFU), f3->data[0]);     // CRC low byte
    TEST_ASSERT_EQUAL_HEX8(TAIL_EOT | TAIL_TOGGLE | 2U, f3->data[1] & 0xFFU); // EOT, toggle=1 (third frame)

    // Cleanup.
    tx_frame_t* f = head;
    while (f != NULL) {
        tx_frame_t* const next = f->next;
        canard_refcount_dec(&self, tx_frame_view(f));
        f = next;
    }
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Empty payload: single frame with just the tail byte.
static void test_tx_spool_empty_payload(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 16U);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    tx_frame_t* const          head    = tx_spool(&self, CRC_INITIAL, 8U, 9U, 0U, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1U, count_frames(head));
    // frame_size = tx_ceil(0+1) = 1. The entire frame is just the tail byte.
    TEST_ASSERT_EQUAL_size_t(1U, canard_dlc_to_len[head->dlc]);
    TEST_ASSERT_EQUAL_HEX8(TAIL_SOT | TAIL_EOT | TAIL_TOGGLE | 9U, head->data[0]);

    canard_refcount_dec(&self, tx_frame_view(head));
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Scattered payload with 5 fragments must produce identical frames as a contiguous equivalent.
static void test_tx_spool_scattered_many_fragments(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;

    const byte_t frag0[] = { 0x01U, 0x02U };
    const byte_t frag1[] = { 0x03U, 0x04U };
    const byte_t frag2[] = { 0x05U, 0x06U };
    const byte_t frag3[] = { 0x07U, 0x08U };
    const byte_t frag4[] = { 0x09U, 0x0AU };
    // Total = 10 bytes, >= 8 => multiframe.

    canard_bytes_chain_t chain4 = { .bytes = { .size = 2U, .data = frag4 }, .next = NULL };
    canard_bytes_chain_t chain3 = { .bytes = { .size = 2U, .data = frag3 }, .next = &chain4 };
    canard_bytes_chain_t chain2 = { .bytes = { .size = 2U, .data = frag2 }, .next = &chain3 };
    canard_bytes_chain_t chain1 = { .bytes = { .size = 2U, .data = frag1 }, .next = &chain2 };
    canard_bytes_chain_t chain0 = { .bytes = { .size = 2U, .data = frag0 }, .next = &chain1 };

    // Spool scattered payload.
    init_canard(&self, &ctx, &alloc, 16U);
    tx_frame_t* const scattered = tx_spool(&self, CRC_INITIAL, 8U, 3U, 10U, chain0);
    TEST_ASSERT_NOT_NULL(scattered);

    // Spool contiguous equivalent.
    const byte_t               contig_data[] = { 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U, 0x09U, 0x0AU };
    const canard_bytes_chain_t contig        = { .bytes = { .size = 10U, .data = contig_data }, .next = NULL };
    canard_t                   self2;
    test_context_t             ctx2;
    instrumented_allocator_t   alloc2;
    init_canard(&self2, &ctx2, &alloc2, 16U);
    tx_frame_t* const contiguous = tx_spool(&self2, CRC_INITIAL, 8U, 3U, 10U, contig);
    TEST_ASSERT_NOT_NULL(contiguous);

    // Compare frame-by-frame.
    TEST_ASSERT_EQUAL_size_t(count_frames(scattered), count_frames(contiguous));
    const tx_frame_t* fs = scattered;
    const tx_frame_t* fc = contiguous;
    while ((fs != NULL) && (fc != NULL)) {
        const size_t sz_s = canard_dlc_to_len[fs->dlc];
        const size_t sz_c = canard_dlc_to_len[fc->dlc];
        TEST_ASSERT_EQUAL_size_t(sz_s, sz_c);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(fc->data, fs->data, sz_s);
        fs = fs->next;
        fc = fc->next;
    }

    // Cleanup.
    tx_frame_t* f = scattered;
    while (f != NULL) {
        tx_frame_t* const next = f->next;
        canard_refcount_dec(&self, tx_frame_view(f));
        f = next;
    }
    f = contiguous;
    while (f != NULL) {
        tx_frame_t* const next = f->next;
        canard_refcount_dec(&self2, tx_frame_view(f));
        f = next;
    }
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
    TEST_ASSERT_EQUAL_size_t(0U, alloc2.allocated_fragments);
}

// Scattered chain with empty intermediate fragments.
static void test_tx_spool_scattered_with_empty_fragments(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 16U);

    const byte_t         frag_a[] = { 0x11U, 0x22U, 0x33U };
    const byte_t         frag_b[] = { 0x44U, 0x55U, 0x66U, 0x77U };
    canard_bytes_chain_t c3       = { .bytes = { .size = 4U, .data = frag_b }, .next = NULL };
    canard_bytes_chain_t c2       = { .bytes = { .size = 0U, .data = NULL }, .next = &c3 };
    canard_bytes_chain_t c1       = { .bytes = { .size = 0U, .data = NULL }, .next = &c2 };
    canard_bytes_chain_t c0       = { .bytes = { .size = 3U, .data = frag_a }, .next = &c1 };
    // Total = 7 bytes. 7 < 8 => single-frame.

    tx_frame_t* const head = tx_spool(&self, CRC_INITIAL, 8U, 1U, 7U, c0);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1U, count_frames(head));
    TEST_ASSERT_EQUAL_size_t(8U, canard_dlc_to_len[head->dlc]); // tx_ceil(7+1)=8
    TEST_ASSERT_EQUAL_HEX8(0x11U, head->data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x22U, head->data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x33U, head->data[2]);
    TEST_ASSERT_EQUAL_HEX8(0x44U, head->data[3]);
    TEST_ASSERT_EQUAL_HEX8(0x55U, head->data[4]);
    TEST_ASSERT_EQUAL_HEX8(0x66U, head->data[5]);
    TEST_ASSERT_EQUAL_HEX8(0x77U, head->data[6]);
    TEST_ASSERT_EQUAL_HEX8(TAIL_SOT | TAIL_EOT | TAIL_TOGGLE | 1U, head->data[7]);

    canard_refcount_dec(&self, tx_frame_view(head));
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// OOM midway through multiframe spooling: all frames cleaned up, no leaks.
static void test_tx_spool_oom_midway(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 16U);

    byte_t data[20];
    for (size_t i = 0; i < 20U; i++) {
        data[i] = (byte_t)i;
    }
    const canard_bytes_chain_t payload = { .bytes = { .size = 20U, .data = data }, .next = NULL };
    // 20 bytes with mtu=8 needs ceil((20+2)/7)=ceil(22/7)=4 frames.
    // Allow only 2 frame allocations to cause OOM midway.
    alloc.limit_fragments = 2U;

    tx_frame_t* const head = tx_spool(&self, CRC_INITIAL, 8U, 0U, 20U, payload);
    TEST_ASSERT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Large payload with CAN FD: verify frame count and tail byte progression.
static void test_tx_spool_large_payload_fd(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 64U);

    byte_t data[300];
    for (size_t i = 0; i < 300U; i++) {
        data[i] = (byte_t)(i & 0xFFU);
    }
    const canard_bytes_chain_t payload = { .bytes = { .size = 300U, .data = data }, .next = NULL };
    // 300 >= 64 => multiframe. ceil((300+2)/63)=ceil(302/63)=5 frames.
    tx_frame_t* const head = tx_spool(&self, CRC_INITIAL, 64U, 7U, 300U, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(5U, count_frames(head));

    // First frame: SOT set, EOT not set, toggle=1.
    const byte_t tail0 = head->data[canard_dlc_to_len[head->dlc] - 1U];
    TEST_ASSERT_NOT_EQUAL(0U, tail0 & TAIL_SOT);
    TEST_ASSERT_EQUAL_HEX8(0U, tail0 & TAIL_EOT);
    TEST_ASSERT_NOT_EQUAL(0U, tail0 & TAIL_TOGGLE);

    // Walk frames: verify toggle alternation and SOT/EOT correctness.
    tx_frame_t* f   = head;
    bool        tog = true;
    size_t      idx = 0;
    while (f != NULL) {
        const size_t sz   = canard_dlc_to_len[f->dlc];
        const byte_t tail = f->data[sz - 1U];
        if (tog) {
            TEST_ASSERT_NOT_EQUAL(0U, tail & TAIL_TOGGLE);
        } else {
            TEST_ASSERT_EQUAL_HEX8(0U, tail & TAIL_TOGGLE);
        }
        if (idx == 0U) {
            TEST_ASSERT_NOT_EQUAL(0U, tail & TAIL_SOT);
        } else {
            TEST_ASSERT_EQUAL_HEX8(0U, tail & TAIL_SOT);
        }
        if (f->next == NULL) {
            TEST_ASSERT_NOT_EQUAL(0U, tail & TAIL_EOT);
        } else {
            TEST_ASSERT_EQUAL_HEX8(0U, tail & TAIL_EOT);
        }
        tog = !tog;
        idx++;
        f = f->next;
    }
    TEST_ASSERT_EQUAL_size_t(5U, idx);

    // Cleanup.
    f = head;
    while (f != NULL) {
        tx_frame_t* const next = f->next;
        canard_refcount_dec(&self, tx_frame_view(f));
        f = next;
    }
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// =============================================
// Group B: tx_spool_v0 tests
// =============================================

// v0 single-frame: 6 bytes payload. 6 < 8 => single frame.
static void test_tx_spool_v0_single_frame(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 16U);

    const byte_t               data[]  = { 0x10U, 0x20U, 0x30U, 0x40U, 0x50U, 0x60U };
    const canard_bytes_chain_t payload = { .bytes = { .size = 6U, .data = data }, .next = NULL };
    tx_frame_t* const          head    = tx_spool_v0(&self, CRC_INITIAL, 4U, 6U, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(1U, count_frames(head));
    // v0 single-frame: size = 6+1 = 7. No rounding in v0 single-frame path.
    TEST_ASSERT_EQUAL_size_t(7U, canard_dlc_to_len[head->dlc]);
    for (size_t i = 0; i < 6U; i++) {
        TEST_ASSERT_EQUAL_HEX8(data[i], head->data[i]);
    }
    // v0 toggle starts at 0. Tail: SOT+EOT, toggle=0.
    TEST_ASSERT_EQUAL_HEX8(TAIL_SOT | TAIL_EOT | 4U, head->data[6]);

    canard_refcount_dec(&self, tx_frame_view(head));
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// v0 boundary: 7 bytes single-frame, 8 bytes multiframe.
static void test_tx_spool_v0_boundary(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;

    // 7 bytes: 7 < 8 => single-frame.
    {
        init_canard(&self, &ctx, &alloc, 16U);
        const byte_t               data[]  = { 1U, 2U, 3U, 4U, 5U, 6U, 7U };
        const canard_bytes_chain_t payload = { .bytes = { .size = 7U, .data = data }, .next = NULL };
        tx_frame_t* const          head    = tx_spool_v0(&self, CRC_INITIAL, 0U, 7U, payload);
        TEST_ASSERT_NOT_NULL(head);
        TEST_ASSERT_EQUAL_size_t(1U, count_frames(head));
        TEST_ASSERT_EQUAL_size_t(8U, canard_dlc_to_len[head->dlc]);      // 7+1=8
        TEST_ASSERT_EQUAL_HEX8(TAIL_SOT | TAIL_EOT | 0U, head->data[7]); // toggle=0 for v0
        canard_refcount_dec(&self, tx_frame_view(head));
        TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
    }

    // 8 bytes: 8 is NOT < 8 => multiframe.
    {
        init_canard(&self, &ctx, &alloc, 16U);
        const byte_t               data[]  = { 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U };
        const canard_bytes_chain_t payload = { .bytes = { .size = 8U, .data = data }, .next = NULL };
        tx_frame_t* const          head    = tx_spool_v0(&self, CRC_INITIAL, 0U, 8U, payload);
        TEST_ASSERT_NOT_NULL(head);
        TEST_ASSERT_TRUE(count_frames(head) >= 2U);
        // First frame has SOT set, toggle=0 for v0.
        const byte_t tail0 = head->data[canard_dlc_to_len[head->dlc] - 1U];
        TEST_ASSERT_NOT_EQUAL(0U, tail0 & TAIL_SOT);
        TEST_ASSERT_EQUAL_HEX8(0U, tail0 & TAIL_TOGGLE); // v0 toggle starts at 0
        // Cleanup.
        tx_frame_t* f = head;
        while (f != NULL) {
            tx_frame_t* const next = f->next;
            canard_refcount_dec(&self, tx_frame_view(f));
            f = next;
        }
        TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
    }
}

// v0 multiframe: verify CRC byte order is little-endian (low byte first).
static void test_tx_spool_v0_crc_byte_order(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 16U);

    const byte_t               data[]  = { 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U };
    const canard_bytes_chain_t payload = { .bytes = { .size = 8U, .data = data }, .next = NULL };
    // 8 >= 8 => multiframe. CRC is computed over payload and prepended in v0.
    const uint16_t crc = crc_add(CRC_INITIAL, 8U, data);

    tx_frame_t* const head = tx_spool_v0(&self, CRC_INITIAL, 0U, 8U, payload);
    TEST_ASSERT_NOT_NULL(head);
    // v0 prepends CRC in LE: the first 2 bytes of the stream are [crc_low, crc_high].
    // Frame 1 data[0..6] are the first 7 stream bytes. Stream = [crc_lo, crc_hi, payload...].
    TEST_ASSERT_EQUAL_HEX8((byte_t)((unsigned)crc & 0xFFU), head->data[0]);
    TEST_ASSERT_EQUAL_HEX8((byte_t)(((unsigned)crc >> 8U) & 0xFFU), head->data[1]);

    // Cleanup.
    tx_frame_t* f = head;
    while (f != NULL) {
        tx_frame_t* const next = f->next;
        canard_refcount_dec(&self, tx_frame_view(f));
        f = next;
    }
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// v0 toggle alternation: starts at 0, alternates per frame.
static void test_tx_spool_v0_toggle_alternation(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 16U);

    // Need 3 frames in v0. size_total = size + 2 (CRC). mtu=8, bytes_per_frame=7.
    // 3 frames => size_total in (14,21]. With CRC prepended, size_total = size+2.
    // Choose size=19 => size_total=21 => ceil(21/7)=3 frames.
    byte_t data[19];
    for (size_t i = 0; i < 19U; i++) {
        data[i] = (byte_t)(i + 1U);
    }
    const canard_bytes_chain_t payload = { .bytes = { .size = 19U, .data = data }, .next = NULL };
    tx_frame_t* const          head    = tx_spool_v0(&self, CRC_INITIAL, 5U, 19U, payload);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_size_t(3U, count_frames(head));

    // Frame 1: toggle=0 (v0 starts at 0).
    const byte_t tail1 = head->data[canard_dlc_to_len[head->dlc] - 1U];
    TEST_ASSERT_EQUAL_HEX8(0U, tail1 & TAIL_TOGGLE);
    TEST_ASSERT_NOT_EQUAL(0U, tail1 & TAIL_SOT);
    TEST_ASSERT_EQUAL_HEX8(0U, tail1 & TAIL_EOT);

    // Frame 2: toggle=1.
    const tx_frame_t* const f2    = head->next;
    const byte_t            tail2 = f2->data[canard_dlc_to_len[f2->dlc] - 1U];
    TEST_ASSERT_NOT_EQUAL(0U, tail2 & TAIL_TOGGLE);
    TEST_ASSERT_EQUAL_HEX8(0U, tail2 & TAIL_SOT);
    TEST_ASSERT_EQUAL_HEX8(0U, tail2 & TAIL_EOT);

    // Frame 3: toggle=0.
    const tx_frame_t* const f3    = f2->next;
    const byte_t            tail3 = f3->data[canard_dlc_to_len[f3->dlc] - 1U];
    TEST_ASSERT_EQUAL_HEX8(0U, tail3 & TAIL_TOGGLE);
    TEST_ASSERT_EQUAL_HEX8(0U, tail3 & TAIL_SOT);
    TEST_ASSERT_NOT_EQUAL(0U, tail3 & TAIL_EOT);

    // Cleanup.
    tx_frame_t* f = head;
    while (f != NULL) {
        tx_frame_t* const next = f->next;
        canard_refcount_dec(&self, tx_frame_view(f));
        f = next;
    }
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// =============================================
// Group C: tx_push/sacrifice/expire tests
// =============================================

// The oldest transfer (head of agewise list) is sacrificed when the queue overflows.
static void test_tx_sacrifice_oldest_first(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 2U); // capacity=2

    const byte_t               d1[] = { 0xAAU };
    const byte_t               d2[] = { 0xBBU };
    const byte_t               d3[] = { 0xCCU };
    const canard_bytes_chain_t pay1 = { .bytes = { .size = 1U, .data = d1 }, .next = NULL };
    const canard_bytes_chain_t pay2 = { .bytes = { .size = 1U, .data = d2 }, .next = NULL };
    const canard_bytes_chain_t pay3 = { .bytes = { .size = 1U, .data = d3 }, .next = NULL };

    // Push 2 single-frame transfers. Queue is now full.
    tx_transfer_t* const tr1 = tx_transfer_new(&self, 1000, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, NULL);
    TEST_ASSERT_NOT_NULL(tr1);
    TEST_ASSERT_TRUE(tx_push(&self, tr1, false, 1U, 0U, pay1, CRC_INITIAL));

    tx_transfer_t* const tr2 = tx_transfer_new(&self, 2000, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, NULL);
    TEST_ASSERT_NOT_NULL(tr2);
    TEST_ASSERT_TRUE(tx_push(&self, tr2, false, 1U, 1U, pay2, CRC_INITIAL));
    TEST_ASSERT_EQUAL_size_t(2U, self.tx.queue_size);

    // Push a third: the oldest (tr1) must be sacrificed.
    tx_transfer_t* const tr3 = tx_transfer_new(&self, 3000, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, NULL);
    TEST_ASSERT_NOT_NULL(tr3);
    TEST_ASSERT_TRUE(tx_push(&self, tr3, false, 1U, 2U, pay3, CRC_INITIAL));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.tx_sacrifice);
    TEST_ASSERT_EQUAL_size_t(2U, self.tx.queue_size);

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Sacrificing a multiframe transfer frees all of its frames.
static void test_tx_sacrifice_multiframe_all_frames(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 4U); // capacity=4

    // Push a multiframe transfer: 8 bytes with mtu=8 => 2 frames. (size>=mtu => multiframe; ceil((8+2)/7)=2 frames)
    byte_t mf_data[8];
    for (size_t i = 0; i < 8U; i++) {
        mf_data[i] = (byte_t)i;
    }
    const canard_bytes_chain_t mf_pay = { .bytes = { .size = 8U, .data = mf_data }, .next = NULL };
    tx_transfer_t* const       mf_tr =
      tx_transfer_new(&self, 1000, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, NULL);
    TEST_ASSERT_NOT_NULL(mf_tr);
    TEST_ASSERT_TRUE(tx_push(&self, mf_tr, false, 1U, 0U, mf_pay, CRC_INITIAL));
    TEST_ASSERT_EQUAL_size_t(2U, self.tx.queue_size); // 2 frames for multiframe

    // Push single-frame transfers to fill remaining capacity.
    const byte_t               sf1_d[] = { 0xAAU };
    const byte_t               sf2_d[] = { 0xBBU };
    const canard_bytes_chain_t sf1_pay = { .bytes = { .size = 1U, .data = sf1_d }, .next = NULL };
    const canard_bytes_chain_t sf2_pay = { .bytes = { .size = 1U, .data = sf2_d }, .next = NULL };
    tx_transfer_t* const       sf1_tr =
      tx_transfer_new(&self, 2000, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, NULL);
    TEST_ASSERT_NOT_NULL(sf1_tr);
    TEST_ASSERT_TRUE(tx_push(&self, sf1_tr, false, 1U, 1U, sf1_pay, CRC_INITIAL));
    TEST_ASSERT_EQUAL_size_t(3U, self.tx.queue_size);

    tx_transfer_t* const sf2_tr =
      tx_transfer_new(&self, 3000, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, NULL);
    TEST_ASSERT_NOT_NULL(sf2_tr);
    TEST_ASSERT_TRUE(tx_push(&self, sf2_tr, false, 1U, 2U, sf2_pay, CRC_INITIAL));
    TEST_ASSERT_EQUAL_size_t(4U, self.tx.queue_size); // Full

    // Push another single-frame. The multiframe (oldest, 2 frames) is sacrificed.
    const byte_t               sf3_d[] = { 0xCCU };
    const canard_bytes_chain_t sf3_pay = { .bytes = { .size = 1U, .data = sf3_d }, .next = NULL };
    tx_transfer_t* const       sf3_tr =
      tx_transfer_new(&self, 4000, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, NULL);
    TEST_ASSERT_NOT_NULL(sf3_tr);
    TEST_ASSERT_TRUE(tx_push(&self, sf3_tr, false, 1U, 3U, sf3_pay, CRC_INITIAL));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.tx_sacrifice);
    // Queue was 4, sacrificed 2 frames (multiframe), added 1 = 3.
    TEST_ASSERT_EQUAL_size_t(3U, self.tx.queue_size);

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Expiration boundary: deadline=1000, now=1000 survives, now=1001 expires.
static void test_tx_expire_boundary(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);

    const byte_t               data[]  = { 0x55U };
    const canard_bytes_chain_t payload = { .bytes = { .size = 1U, .data = data }, .next = NULL };

    // Push with deadline=1000. tx_push calls tx_expire internally but ctx.now=0 at first.
    ctx.now                  = 0U;
    tx_transfer_t* const tr1 = tx_transfer_new(&self, 1000, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, NULL);
    TEST_ASSERT_NOT_NULL(tr1);
    TEST_ASSERT_TRUE(tx_push(&self, tr1, false, 1U, 0U, payload, CRC_INITIAL));
    TEST_ASSERT_EQUAL_size_t(1U, count_enqueued_transfers(&self));

    // Set now=1000 and push another to trigger tx_expire. 1000 > 1000 is false: survives.
    ctx.now                             = 1000U;
    const canard_bytes_chain_t payload2 = { .bytes = { .size = 1U, .data = data }, .next = NULL };
    tx_transfer_t* const tr2 = tx_transfer_new(&self, 5000, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, NULL);
    TEST_ASSERT_NOT_NULL(tr2);
    TEST_ASSERT_TRUE(tx_push(&self, tr2, false, 1U, 1U, payload2, CRC_INITIAL));
    TEST_ASSERT_EQUAL_size_t(2U, count_enqueued_transfers(&self));
    TEST_ASSERT_EQUAL_UINT64(0U, self.err.tx_expiration);

    // Set now=1001 and push again. 1001 > 1000 is true: tr1 expires.
    ctx.now                             = 1001U;
    const canard_bytes_chain_t payload3 = { .bytes = { .size = 1U, .data = data }, .next = NULL };
    tx_transfer_t* const tr3 = tx_transfer_new(&self, 5000, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, NULL);
    TEST_ASSERT_NOT_NULL(tr3);
    TEST_ASSERT_TRUE(tx_push(&self, tr3, false, 1U, 2U, payload3, CRC_INITIAL));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.tx_expiration);
    // tr1 expired, tr2 and tr3 remain.
    TEST_ASSERT_EQUAL_size_t(2U, count_enqueued_transfers(&self));

    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Exhaustive test of tx_predict_frame_count against a reference formula.
static void test_tx_predict_frame_count_exhaustive(void)
{
    // Reference: if size < mtu then 1, else ceil((size+2)/(mtu-1)).
    // Note: tx_predict_frame_count uses transfer_size <= (mtu-1) which is equivalent to size < mtu.
    const size_t sizes[] = { 0U, 1U, 6U, 7U, 8U, 12U, 13U, 62U, 63U, 64U, 100U, 300U };
    const size_t mtus[]  = { 8U, 64U };

    for (size_t mi = 0; mi < sizeof(mtus) / sizeof(mtus[0]); mi++) {
        const size_t mtu = mtus[mi];
        for (size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
            const size_t sz       = sizes[si];
            size_t       expected = 0U;
            if (sz < mtu) {
                expected = 1U;
            } else {
                // ceil((sz + 2) / (mtu - 1))
                expected = (sz + CRC_BYTES + (mtu - 1U) - 1U) / (mtu - 1U);
            }
            const size_t actual = tx_predict_frame_count(sz, mtu);
            TEST_ASSERT_EQUAL_size_t(expected, actual);
        }
    }
}

// Pushing with iface_bitmap=0x03 (both interfaces) increments refcount.
static void test_tx_push_refcount_multi_iface(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);

    const byte_t               data[]  = { 0x42U };
    const canard_bytes_chain_t payload = { .bytes = { .size = 1U, .data = data }, .next = NULL };
    tx_transfer_t* const tr = tx_transfer_new(&self, 5000, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, NULL);
    TEST_ASSERT_NOT_NULL(tr);
    // iface_bitmap=0x03 => both interfaces.
    TEST_ASSERT_TRUE(tx_push(&self, tr, false, 3U, 0U, payload, CRC_INITIAL));
    TEST_ASSERT_EQUAL_size_t(1U, self.tx.queue_size);

    // The single frame's refcount should be 2 (1 base + 1 for second iface).
    const tx_frame_t* const frame = tr->cursor[0];
    TEST_ASSERT_NOT_NULL(frame);
    TEST_ASSERT_EQUAL_size_t(2U, frame->refcount);
    TEST_ASSERT_EQUAL_PTR(frame, tr->cursor[1]); // Both cursors point to the same frame.

    // Eject from iface 0. Refcount drops to 1, queue_size stays 1 (refcount > 0).
    ctx.tx_budget[0] = 1U;
    tx_eject_pending(&self, 0U);
    TEST_ASSERT_EQUAL_size_t(1U, frame->refcount);
    TEST_ASSERT_EQUAL_size_t(1U, self.tx.queue_size); // Frame still alive (iface 1 holds it).

    // Eject from iface 1. Refcount drops to 0, frame is freed.
    ctx.tx_budget[1] = 1U;
    tx_eject_pending(&self, 1U);
    TEST_ASSERT_EQUAL_size_t(0U, self.tx.queue_size);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// =============================================
// Group D: additional branch coverage tests
// =============================================

// OOM cleanup loop in tx_spool_v0 when failure occurs after the first frame was already allocated (lines 662-668).
static void test_canard_v0_spool_oom_mid_chain(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);
    self.node_id = 1U;
    // Payload of 10 bytes => multiframe in v0 (10 >= 8). size_total = 10+2(CRC) = 12, ceil(12/7) = 2 frames.
    // tx_transfer_new uses 1 fragment, then tx_spool_v0 allocates frames.
    // Allow transfer + 1 frame = 2 fragments total, so the 2nd frame alloc fails.
    alloc.limit_fragments               = 2U;
    const byte_t               data[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    const canard_bytes_chain_t payload  = { .bytes = { .size = 10U, .data = data }, .next = NULL };
    TEST_ASSERT_FALSE(canard_v0_publish(&self, 1000, 1U, canard_prio_nominal, 11U, 0xFFFFU, 3U, payload, NULL));
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// tx_ensure_queue_space sacrifice returns NULL when agewise list is empty but queue_size >= capacity.
static void test_tx_ensure_queue_sacrifice_null(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 1U);
    // Artificially make queue appear full with no transfers in agewise list.
    self.tx.queue_size                 = 1U;
    const byte_t               data[]  = { 0x55U };
    const canard_bytes_chain_t payload = { .bytes = { .size = 1U, .data = data }, .next = NULL };
    TEST_ASSERT_FALSE(canard_publish_16b(&self, 1000, 1U, canard_prio_nominal, 10U, 0U, payload, NULL));
    TEST_ASSERT_TRUE(self.err.tx_capacity > 0U);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// canard_pending_ifaces(NULL) returns 0.
static void test_canard_pending_ifaces_null(void) { TEST_ASSERT_EQUAL_UINT8(0U, canard_pending_ifaces(NULL)); }

// Validation branches for canard_publish_16b.
static void test_publish_16b_validation_branches(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);
    const canard_bytes_chain_t ok_pay  = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    const canard_bytes_chain_t bad_pay = { .bytes = { .size = 1U, .data = NULL }, .next = NULL };
    // NULL self.
    TEST_ASSERT_FALSE(canard_publish_16b(NULL, 1000, 1U, canard_prio_nominal, 10U, 0U, ok_pay, NULL));
    // Priority out of range.
    TEST_ASSERT_FALSE(canard_publish_16b(&self, 1000, 1U, (canard_prio_t)CANARD_PRIO_COUNT, 10U, 0U, ok_pay, NULL));
    // Invalid bytes_chain (size>0, data=NULL).
    TEST_ASSERT_FALSE(canard_publish_16b(&self, 1000, 1U, canard_prio_nominal, 10U, 0U, bad_pay, NULL));
    // iface_bitmap = 0.
    TEST_ASSERT_FALSE(canard_publish_16b(&self, 1000, 0U, canard_prio_nominal, 10U, 0U, ok_pay, NULL));
    // iface_bitmap with invalid bits.
    TEST_ASSERT_FALSE(canard_publish_16b(&self, 1000, 0x80U, canard_prio_nominal, 10U, 0U, ok_pay, NULL));
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validation branches for canard_publish_13b.
static void test_publish_13b_validation_branches(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);
    const canard_bytes_chain_t ok_pay  = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    const canard_bytes_chain_t bad_pay = { .bytes = { .size = 1U, .data = NULL }, .next = NULL };
    // NULL self.
    TEST_ASSERT_FALSE(canard_publish_13b(NULL, 1000, 1U, canard_prio_nominal, 10U, 0U, ok_pay, NULL));
    // Priority out of range.
    TEST_ASSERT_FALSE(canard_publish_13b(&self, 1000, 1U, (canard_prio_t)CANARD_PRIO_COUNT, 10U, 0U, ok_pay, NULL));
    // Invalid bytes_chain.
    TEST_ASSERT_FALSE(canard_publish_13b(&self, 1000, 1U, canard_prio_nominal, 10U, 0U, bad_pay, NULL));
    // iface_bitmap = 0.
    TEST_ASSERT_FALSE(canard_publish_13b(&self, 1000, 0U, canard_prio_nominal, 10U, 0U, ok_pay, NULL));
    // iface_bitmap with invalid bits.
    TEST_ASSERT_FALSE(canard_publish_13b(&self, 1000, 0x80U, canard_prio_nominal, 10U, 0U, ok_pay, NULL));
    // subject_id > CANARD_SUBJECT_ID_MAX_13b.
    TEST_ASSERT_FALSE(
      canard_publish_13b(&self, 1000, 1U, canard_prio_nominal, CANARD_SUBJECT_ID_MAX_13b + 1U, 0U, ok_pay, NULL));
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validation branches for canard_v0_publish.
static void test_v0_publish_validation_branches(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);
    self.node_id                      = 1U;
    const canard_bytes_chain_t ok_pay = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    // iface_bitmap = 0.
    TEST_ASSERT_FALSE(canard_v0_publish(&self, 1000, 0U, canard_prio_nominal, 11U, 0xFFFFU, 0U, ok_pay, NULL));
    // iface_bitmap with bits outside CANARD_IFACE_BITMAP_ALL.
    TEST_ASSERT_FALSE(canard_v0_publish(&self, 1000, 0x80U, canard_prio_nominal, 11U, 0xFFFFU, 0U, ok_pay, NULL));
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validation branches for v0 service functions.
static void test_v0_service_validation_branches(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);
    self.node_id                      = 1U;
    const canard_bytes_chain_t ok_pay = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    // destination_node_id = 0.
    TEST_ASSERT_FALSE(canard_v0_request(&self, 1000, canard_prio_nominal, 1U, 0xFFFFU, 0U, 0U, ok_pay, NULL));
    // destination_node_id > CANARD_NODE_ID_MAX.
    TEST_ASSERT_FALSE(
      canard_v0_request(&self, 1000, canard_prio_nominal, 1U, 0xFFFFU, CANARD_NODE_ID_MAX + 1U, 0U, ok_pay, NULL));
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// canard_refcount_inc and canard_refcount_dec with NULL data are no-ops.
static void test_refcount_null_data(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);
    const canard_bytes_t null_obj = { .data = NULL, .size = 0U };
    canard_refcount_inc(null_obj);
    canard_refcount_dec(&self, null_obj);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// TX comparator equality fallthrough: two transfers with the same CAN ID but different seqno.
static void test_tx_comparator_equal_can_id(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 16U);
    const byte_t               d1[] = { 0xAAU };
    const byte_t               d2[] = { 0xBBU };
    const canard_bytes_chain_t pay1 = { .bytes = { .size = 1U, .data = d1 }, .next = NULL };
    const canard_bytes_chain_t pay2 = { .bytes = { .size = 1U, .data = d2 }, .next = NULL };
    // Same priority and subject => same can_id_msb. Different transfer_id/seqno.
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 1000, 1U, canard_prio_nominal, 100U, 0U, pay1, NULL));
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 1000, 1U, canard_prio_nominal, 100U, 1U, pay2, NULL));
    TEST_ASSERT_EQUAL_size_t(2U, count_enqueued_transfers(&self));
    // Both should be in the pending tree for iface 0; the equal can_id_msb path was exercised.
    const tx_transfer_t* const tr1 = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
    const tx_transfer_t* const tr2 = LIST_NEXT(tr1, tx_transfer_t, list_agewise);
    TEST_ASSERT_NOT_NULL(tr1);
    TEST_ASSERT_NOT_NULL(tr2);
    TEST_ASSERT_EQUAL_UINT32(tr1->can_id_msb, tr2->can_id_msb);
    TEST_ASSERT_TRUE(tr1->seqno != tr2->seqno);
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
    RUN_TEST(test_tx_first_frame_departure_flag);
    RUN_TEST(test_tx_purge_continuations_keeps_unstarted_multi_frame);
    RUN_TEST(test_tx_purge_continuations_removes_started_multi_frame);
    RUN_TEST(test_tx_purge_continuations_keeps_started_single_frame);
    RUN_TEST(test_tx_purge_continuations_mixed_queue);
    RUN_TEST(test_canard_set_node_id_purges_started_multiframe_only);
    RUN_TEST(test_canard_set_node_id_same_value_keeps_queue);

    // API-level TX paths.
    RUN_TEST(test_canard_publish_validation);
    RUN_TEST(test_canard_publish_basic);
    RUN_TEST(test_canard_publish_max_subject_encoding);
    RUN_TEST(test_canard_1v0_publish_basic);
    RUN_TEST(test_canard_v0_publish_basic);
    RUN_TEST(test_canard_1v0_service_basic);
    RUN_TEST(test_canard_1v0_service_validation);
    RUN_TEST(test_canard_1v0_service_oom);
    RUN_TEST(test_canard_1v0_service_capacity);
    RUN_TEST(test_canard_v0_service_basic);
    RUN_TEST(test_canard_v0_service_validation);
    RUN_TEST(test_canard_v0_service_oom);
    RUN_TEST(test_canard_v0_service_capacity);

    // CAN ID specification compliance (hardcoded literals from specs).
    RUN_TEST(test_1v0_publish_can_id_compliance);
    RUN_TEST(test_1v0_request_can_id_compliance);
    RUN_TEST(test_1v0_respond_can_id_compliance);
    RUN_TEST(test_v0_publish_can_id_compliance);
    RUN_TEST(test_v0_request_can_id_compliance);
    RUN_TEST(test_v0_respond_can_id_compliance);

    // Group A: tx_spool boundary/CRC tests.
    RUN_TEST(test_tx_spool_boundary_single_multi_classic);
    RUN_TEST(test_tx_spool_boundary_single_multi_fd);
    RUN_TEST(test_tx_spool_crc_split_across_frames);
    RUN_TEST(test_tx_spool_empty_payload);
    RUN_TEST(test_tx_spool_scattered_many_fragments);
    RUN_TEST(test_tx_spool_scattered_with_empty_fragments);
    RUN_TEST(test_tx_spool_oom_midway);
    RUN_TEST(test_tx_spool_large_payload_fd);

    // Group B: tx_spool_v0 tests.
    RUN_TEST(test_tx_spool_v0_single_frame);
    RUN_TEST(test_tx_spool_v0_boundary);
    RUN_TEST(test_tx_spool_v0_crc_byte_order);
    RUN_TEST(test_tx_spool_v0_toggle_alternation);

    // Group C: tx_push/sacrifice/expire tests.
    RUN_TEST(test_tx_sacrifice_oldest_first);
    RUN_TEST(test_tx_sacrifice_multiframe_all_frames);
    RUN_TEST(test_tx_expire_boundary);
    RUN_TEST(test_tx_predict_frame_count_exhaustive);
    RUN_TEST(test_tx_push_refcount_multi_iface);

    // Group D: additional branch coverage.
    RUN_TEST(test_canard_v0_spool_oom_mid_chain);
    RUN_TEST(test_tx_ensure_queue_sacrifice_null);
    RUN_TEST(test_canard_pending_ifaces_null);
    RUN_TEST(test_publish_16b_validation_branches);
    RUN_TEST(test_publish_13b_validation_branches);
    RUN_TEST(test_v0_publish_validation_branches);
    RUN_TEST(test_v0_service_validation_branches);
    RUN_TEST(test_refcount_null_data);
    RUN_TEST(test_tx_comparator_equal_can_id);

    return UNITY_END();
}
