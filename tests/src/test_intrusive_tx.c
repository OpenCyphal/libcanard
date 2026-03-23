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
static bool mock_tx(canard_t* const             self,
                    const canard_user_context_t user_context,
                    const canard_us_t           deadline,
                    const uint_least8_t         iface_index,
                    const bool                  fd,
                    const uint32_t              extended_can_id,
                    const canard_bytes_t        can_data)
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
static const canard_vtable_t test_vtable = { .now = mock_now, .on_unicast = NULL, .tx = mock_tx, .filter = NULL };

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
    tx_transfer_t* const       tr =
      tx_transfer_new(&self, 1000, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, CANARD_USER_CONTEXT_NULL);
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
    tx_transfer_t* const       tr =
      tx_transfer_new(&self, 1000, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, CANARD_USER_CONTEXT_NULL);
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
    tx_transfer_t* const       tr =
      tx_transfer_new(&self, 1000, ((uint32_t)canard_prio_nominal) << PRIO_SHIFT, false, CANARD_USER_CONTEXT_NULL);
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
    tx_transfer_t* const       tr      = tx_transfer_new(&self, 1000, can_id, false, CANARD_USER_CONTEXT_NULL);
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
    tx_transfer_t* const       tr =
      tx_transfer_new(&self,
                      1000,
                      (((uint32_t)canard_prio_nominal) << PRIO_SHIFT) | (1U << 8U) | (1UL << 7U),
                      false,
                      CANARD_USER_CONTEXT_NULL);
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
    tx_transfer_t* const       tr =
      tx_transfer_new(&self,
                      1000,
                      (((uint32_t)canard_prio_nominal) << PRIO_SHIFT) | (2U << 8U) | (1UL << 7U),
                      false,
                      CANARD_USER_CONTEXT_NULL);
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
    tx_transfer_t* const       tr =
      tx_transfer_new(&self,
                      1000,
                      (((uint32_t)canard_prio_nominal) << PRIO_SHIFT) | (3U << 8U) | (1UL << 7U),
                      false,
                      CANARD_USER_CONTEXT_NULL);
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

    const byte_t               mf_a[]    = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };
    const byte_t               mf_b[]    = { 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U };
    const byte_t               sf[]      = { 0x55U };
    const canard_bytes_chain_t payload_a = { .bytes = { .size = sizeof(mf_a), .data = mf_a }, .next = NULL };
    const canard_bytes_chain_t payload_b = { .bytes = { .size = sizeof(mf_b), .data = mf_b }, .next = NULL };
    const canard_bytes_chain_t payload_c = { .bytes = { .size = sizeof(sf), .data = sf }, .next = NULL };
    tx_transfer_t* const       started_multi =
      tx_transfer_new(&self,
                      1000,
                      (((uint32_t)canard_prio_optional) << PRIO_SHIFT) | (500U << 8U) | (1UL << 7U),
                      false,
                      CANARD_USER_CONTEXT_NULL);
    tx_transfer_t* const unstarted_multi =
      tx_transfer_new(&self,
                      1000,
                      (((uint32_t)canard_prio_nominal) << PRIO_SHIFT) | (200U << 8U) | (1UL << 7U),
                      false,
                      CANARD_USER_CONTEXT_NULL);
    tx_transfer_t* const started_single =
      tx_transfer_new(&self,
                      1000,
                      (((uint32_t)canard_prio_exceptional) << PRIO_SHIFT) | (1U << 8U) | (1UL << 7U),
                      false,
                      CANARD_USER_CONTEXT_NULL);
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
    tx_transfer_t* const       started_multi =
      tx_transfer_new(&self,
                      1000,
                      (((uint32_t)canard_prio_nominal) << PRIO_SHIFT) | (10U << 8U) | (1UL << 7U),
                      false,
                      CANARD_USER_CONTEXT_NULL);
    tx_transfer_t* const fresh_multi =
      tx_transfer_new(&self,
                      1000,
                      (((uint32_t)canard_prio_nominal) << PRIO_SHIFT) | (11U << 8U) | (1UL << 7U),
                      false,
                      CANARD_USER_CONTEXT_NULL);
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
    tx_transfer_t* const       tr =
      tx_transfer_new(&self,
                      1000,
                      (((uint32_t)canard_prio_nominal) << PRIO_SHIFT) | (12U << 8U) | (1UL << 7U),
                      false,
                      CANARD_USER_CONTEXT_NULL);
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
    TEST_ASSERT_FALSE(
      canard_publish(&self, 1000, 0U, canard_prio_nominal, 10U, 0U, empty_payload, CANARD_USER_CONTEXT_NULL));
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
    TEST_ASSERT_TRUE(canard_publish(
      &self, 1000, 1U, canard_prio_nominal, CANARD_SUBJECT_ID_MAX, 3U, payload, CANARD_USER_CONTEXT_NULL));

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

// Validate standalone Cyphal v1.0 publish path.
static void test_canard_1v0_publish_basic(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    TEST_ASSERT_TRUE(
      canard_1v0_publish(&self, 1000, 1U, canard_prio_nominal, 42U, 7U, payload, CANARD_USER_CONTEXT_NULL));

    const tx_transfer_t* const tr = LIST_HEAD(self.tx.agewise, tx_transfer_t, list_agewise);
    TEST_ASSERT_NOT_NULL(tr);
    const uint32_t can_id = (uint32_t)(tr->can_id_msb << 7U);
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
    TEST_ASSERT_FALSE(
      canard_0v1_publish(&self, 1000, 1U, canard_prio_nominal, 11U, 0xFFFFU, 3U, payload, CANARD_USER_CONTEXT_NULL));

    // Non-zero node-ID is accepted.
    self.node_id = 1U;
    TEST_ASSERT_TRUE(
      canard_0v1_publish(&self, 1000, 1U, canard_prio_nominal, 11U, 0xFFFFU, 3U, payload, CANARD_USER_CONTEXT_NULL));
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
    TEST_ASSERT_TRUE(
      canard_1v0_request(&self, 1000, canard_prio_nominal, 430U, 24U, 5U, payload, CANARD_USER_CONTEXT_NULL));
    TEST_ASSERT_TRUE(
      canard_1v0_respond(&self, 1000, canard_prio_nominal, 430U, 24U, 6U, payload, CANARD_USER_CONTEXT_NULL));

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

    TEST_ASSERT_FALSE(canard_1v0_request(
      &self, 1000, canard_prio_nominal, CANARD_SERVICE_ID_MAX + 1U, 24U, 0U, payload, CANARD_USER_CONTEXT_NULL));
    TEST_ASSERT_FALSE(canard_1v0_respond(
      &self, 1000, canard_prio_nominal, 430U, CANARD_NODE_ID_MAX + 1U, 0U, payload, CANARD_USER_CONTEXT_NULL));
    TEST_ASSERT_FALSE(
      canard_1v0_request(&self, 1000, canard_prio_nominal, 430U, 24U, 0U, bad, CANARD_USER_CONTEXT_NULL));

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
    TEST_ASSERT_FALSE(
      canard_1v0_request(&self, 1000, canard_prio_nominal, 430U, 24U, 1U, payload, CANARD_USER_CONTEXT_NULL));
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
    TEST_ASSERT_FALSE(
      canard_1v0_respond(&self, 1000, canard_prio_nominal, 430U, 24U, 1U, payload, CANARD_USER_CONTEXT_NULL));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.tx_capacity);
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate UAVCAN v0 service request/response CAN-ID composition.
static void test_canard_0v1_service_basic(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);
    self.tx.fd   = true; // Legacy service TX must still force Classic CAN.
    self.node_id = 11U;

    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    TEST_ASSERT_TRUE(
      canard_0v1_request(&self, 1000, canard_prio_nominal, 0x37U, 0xBEEFU, 24U, 5U, payload, CANARD_USER_CONTEXT_NULL));
    TEST_ASSERT_TRUE(
      canard_0v1_respond(&self, 1000, canard_prio_nominal, 0x37U, 0xBEEFU, 24U, 6U, payload, CANARD_USER_CONTEXT_NULL));

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
static void test_canard_0v1_service_validation(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);
    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    const canard_bytes_chain_t bad     = { .bytes = { .size = 1U, .data = NULL }, .next = NULL };

    self.node_id = 0U;
    TEST_ASSERT_FALSE(
      canard_0v1_request(&self, 1000, canard_prio_nominal, 1U, 0xFFFFU, 24U, 0U, payload, CANARD_USER_CONTEXT_NULL));
    self.node_id = 1U;
    TEST_ASSERT_FALSE(
      canard_0v1_respond(&self, 1000, canard_prio_nominal, 1U, 0xFFFFU, 0U, 0U, payload, CANARD_USER_CONTEXT_NULL));
    TEST_ASSERT_FALSE(canard_0v1_request(
      &self, 1000, canard_prio_nominal, 1U, 0xFFFFU, CANARD_NODE_ID_MAX + 1U, 0U, payload, CANARD_USER_CONTEXT_NULL));
    TEST_ASSERT_FALSE(
      canard_0v1_request(&self, 1000, canard_prio_nominal, 1U, 0xFFFFU, 24U, 0U, bad, CANARD_USER_CONTEXT_NULL));

    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate UAVCAN v0 service transfer allocation failure.
static void test_canard_0v1_service_oom(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 8U);
    alloc.limit_fragments = 0U;
    self.node_id          = 1U;

    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    TEST_ASSERT_FALSE(
      canard_0v1_request(&self, 1000, canard_prio_nominal, 1U, 0xBEEFU, 24U, 1U, payload, CANARD_USER_CONTEXT_NULL));
    TEST_ASSERT_EQUAL_size_t(0U, alloc.allocated_fragments);
}

// Validate UAVCAN v0 service queue-capacity failure.
static void test_canard_0v1_service_capacity(void)
{
    canard_t                 self;
    test_context_t           ctx;
    instrumented_allocator_t alloc;
    init_canard(&self, &ctx, &alloc, 0U);
    self.node_id = 1U;

    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };
    TEST_ASSERT_FALSE(
      canard_0v1_respond(&self, 1000, canard_prio_nominal, 1U, 0xBEEFU, 24U, 1U, payload, CANARD_USER_CONTEXT_NULL));
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
    TEST_ASSERT_TRUE(
      canard_1v0_publish(&self, 1000, 1U, canard_prio_exceptional, 0U, 0U, payload, CANARD_USER_CONTEXT_NULL));
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
    TEST_ASSERT_TRUE(
      canard_1v0_publish(&self, 1000, 1U, canard_prio_optional, 8191U, 0U, payload, CANARD_USER_CONTEXT_NULL));
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
    TEST_ASSERT_TRUE(canard_1v0_publish(&self, 1000, 1U, canard_prio_high, 42U, 0U, payload, CANARD_USER_CONTEXT_NULL));
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
    TEST_ASSERT_TRUE(
      canard_1v0_request(&self, 1000, canard_prio_exceptional, 0U, 1U, 0U, payload, CANARD_USER_CONTEXT_NULL));
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
    TEST_ASSERT_TRUE(
      canard_1v0_request(&self, 1000, canard_prio_optional, 511U, 127U, 0U, payload, CANARD_USER_CONTEXT_NULL));
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
    TEST_ASSERT_TRUE(
      canard_1v0_respond(&self, 1000, canard_prio_fast, 430U, 24U, 0U, payload, CANARD_USER_CONTEXT_NULL));
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
    TEST_ASSERT_TRUE(
      canard_1v0_respond(&self, 1000, canard_prio_nominal, 1U, 1U, 0U, payload, CANARD_USER_CONTEXT_NULL));
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
static void test_0v1_publish_can_id_compliance(void)
{
    canard_t                   self;
    test_context_t             ctx;
    instrumented_allocator_t   alloc;
    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };

    // Case A: prio=exceptional(0), dtid=0
    init_canard(&self, &ctx, &alloc, 8U);
    self.node_id = 1U;
    TEST_ASSERT_TRUE(
      canard_0v1_publish(&self, 1000, 1U, canard_prio_exceptional, 0U, 0xFFFFU, 0U, payload, CANARD_USER_CONTEXT_NULL));
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
    TEST_ASSERT_TRUE(canard_0v1_publish(
      &self, 1000, 1U, canard_prio_optional, 0xFFFFU, 0xFFFFU, 0U, payload, CANARD_USER_CONTEXT_NULL));
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
    TEST_ASSERT_TRUE(canard_0v1_publish(
      &self, 1000, 1U, canard_prio_nominal, 0x040AU, 0xFFFFU, 0U, payload, CANARD_USER_CONTEXT_NULL));
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
static void test_0v1_request_can_id_compliance(void)
{
    canard_t                   self;
    test_context_t             ctx;
    instrumented_allocator_t   alloc;
    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };

    // Case A: prio=exceptional(0), dti=1, dest=1
    init_canard(&self, &ctx, &alloc, 8U);
    self.node_id = 10U;
    TEST_ASSERT_TRUE(
      canard_0v1_request(&self, 1000, canard_prio_exceptional, 1U, 0xFFFFU, 1U, 0U, payload, CANARD_USER_CONTEXT_NULL));
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
    TEST_ASSERT_TRUE(canard_0v1_request(
      &self, 1000, canard_prio_optional, 255U, 0xFFFFU, 127U, 0U, payload, CANARD_USER_CONTEXT_NULL));
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
static void test_0v1_respond_can_id_compliance(void)
{
    canard_t                   self;
    test_context_t             ctx;
    instrumented_allocator_t   alloc;
    const canard_bytes_chain_t payload = { .bytes = { .size = 0U, .data = NULL }, .next = NULL };

    // Case A: prio=nominal(4), dti=0x37, dest=24
    init_canard(&self, &ctx, &alloc, 8U);
    self.node_id = 11U;
    TEST_ASSERT_TRUE(
      canard_0v1_respond(&self, 1000, canard_prio_nominal, 0x37U, 0xFFFFU, 24U, 0U, payload, CANARD_USER_CONTEXT_NULL));
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
    TEST_ASSERT_TRUE(canard_0v1_respond(
      &self, 1000, canard_prio_immediate, 200U, 0xFFFFU, 42U, 0U, payload, CANARD_USER_CONTEXT_NULL));
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
    RUN_TEST(test_canard_0v1_publish_basic);
    RUN_TEST(test_canard_1v0_service_basic);
    RUN_TEST(test_canard_1v0_service_validation);
    RUN_TEST(test_canard_1v0_service_oom);
    RUN_TEST(test_canard_1v0_service_capacity);
    RUN_TEST(test_canard_0v1_service_basic);
    RUN_TEST(test_canard_0v1_service_validation);
    RUN_TEST(test_canard_0v1_service_oom);
    RUN_TEST(test_canard_0v1_service_capacity);

    // CAN ID specification compliance (hardcoded literals from specs).
    RUN_TEST(test_1v0_publish_can_id_compliance);
    RUN_TEST(test_1v0_request_can_id_compliance);
    RUN_TEST(test_1v0_respond_can_id_compliance);
    RUN_TEST(test_0v1_publish_can_id_compliance);
    RUN_TEST(test_0v1_request_can_id_compliance);
    RUN_TEST(test_0v1_respond_can_id_compliance);

    return UNITY_END();
}
