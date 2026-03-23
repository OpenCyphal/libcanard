// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal Development Team.

// NOLINTBEGIN(*)

#include "canard.c" // NOLINT(bugprone-suspicious-include)
#include "helpers.h"
#include <unity.h>

// =====================================================================================================================
// Fixtures and helpers
// =====================================================================================================================

static canard_t make_canard(const uint64_t prng_seed, const byte_t node_id)
{
    canard_t self;
    memset(&self, 0, sizeof(self));
    self.prng_state = prng_seed;
    self.node_id    = node_id;
    node_id_occupancy_reset(&self);
    return self;
}

// Fill all bits EXCEPT the ones listed in `except` (array of `except_count` positions).
// Bit 0 is always set by node_id_occupancy_reset, so callers shouldn't include 0 in except.
static void fill_bitmap_except(canard_t* self, const size_t* except, size_t except_count)
{
    self->node_id_occupancy_bitmap[0] = UINT64_MAX;
    self->node_id_occupancy_bitmap[1] = UINT64_MAX;
    for (size_t i = 0; i < except_count; i++) {
        self->node_id_occupancy_bitmap[except[i] / 64U] &= ~(UINT64_C(1) << (except[i] % 64U));
    }
}

// Find a seed where, inside node_id_occupancy_update for the given bitmap state:
//   - chance(self, zc) returns want_purge
//   - the subsequent random(self, zc) returns want_z
// The function simulates the two PRNG calls that happen when pc > cap/2.
static uint64_t find_seed_dense(const uint64_t zc, const bool want_purge, const uint64_t want_z)
{
    for (uint64_t seed = 0; seed < 10000000ULL; seed++) {
        canard_t probe;
        memset(&probe, 0, sizeof(probe));
        probe.prng_state = seed;
        const bool ch    = chance(&probe, zc); // consumes one PRNG step
        if (ch != want_purge) {
            continue;
        }
        const uint64_t r = random(&probe, zc); // consumes another
        if (r == want_z) {
            return seed;
        }
    }
    return UINT64_MAX; // should never happen for small zc
}

// For sparse bitmaps (pc <= 64), chance is NOT called. Only random(self, zc) is consumed.
static uint64_t find_seed_sparse(const uint64_t zc, const uint64_t want_z)
{
    for (uint64_t seed = 0; seed < 10000000ULL; seed++) {
        canard_t probe;
        memset(&probe, 0, sizeof(probe));
        probe.prng_state = seed;
        const uint64_t r = random(&probe, zc);
        if (r == want_z) {
            return seed;
        }
    }
    return UINT64_MAX;
}

// Find a seed where chance(&self, p_reciprocal) returns the desired outcome (single call, no collision).
static uint64_t find_seed_chance(const uint64_t p_reciprocal, const bool want)
{
    for (uint64_t seed = 0; seed < 10000000ULL; seed++) {
        canard_t probe;
        memset(&probe, 0, sizeof(probe));
        probe.prng_state = seed;
        if (chance(&probe, p_reciprocal) == want) {
            return seed;
        }
    }
    return UINT64_MAX;
}

// =====================================================================================================================
// TX test infrastructure (needed for purge-on-collision tests)
// =====================================================================================================================

typedef struct
{
    canard_us_t now;
} tx_test_ctx_t;

static canard_us_t tx_test_now(const canard_t* const self)
{
    const tx_test_ctx_t* const ctx = (const tx_test_ctx_t*)self->user_context;
    return (ctx != NULL) ? ctx->now : 0;
}

static bool tx_test_tx(canard_t* const      self,
                       void* const          user_context,
                       const canard_us_t    deadline,
                       const uint_least8_t  iface_index,
                       const bool           fd,
                       const uint32_t       extended_can_id,
                       const canard_bytes_t can_data)
{
    (void)self;
    (void)user_context;
    (void)deadline;
    (void)iface_index;
    (void)fd;
    (void)extended_can_id;
    (void)can_data;
    return false; // Never eject -- we control frame departure manually
}

static const canard_vtable_t tx_test_vtable = {
    .now    = tx_test_now,
    .tx     = tx_test_tx,
    .filter = NULL,
};

// Build an instance suitable for TX tests: vtable + allocators set up.
static void init_tx_canard(canard_t*                 self,
                           tx_test_ctx_t*            ctx,
                           instrumented_allocator_t* alloc_transfer,
                           instrumented_allocator_t* alloc_frame,
                           const byte_t              node_id,
                           const uint64_t            prng_seed)
{
    instrumented_allocator_new(alloc_transfer);
    instrumented_allocator_new(alloc_frame);
    memset(self, 0, sizeof(*self));
    memset(ctx, 0, sizeof(*ctx));
    self->user_context      = ctx;
    self->vtable            = &tx_test_vtable;
    self->tx.queue_capacity = 64U;
    self->tx.fd             = true;
    self->mem.tx_transfer   = instrumented_allocator_make_resource(alloc_transfer);
    self->mem.tx_frame      = instrumented_allocator_make_resource(alloc_frame);
    self->prng_state        = prng_seed;
    self->node_id           = node_id;
    node_id_occupancy_reset(self);
}

// Enqueue a transfer with the given payload size. Returns the transfer pointer (NULL on failure).
static tx_transfer_t* enqueue_transfer(canard_t* self, const bool multi_frame_payload)
{
    const byte_t  data_small[] = { 0xAA };
    const byte_t  data_large[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 }; // >7 bytes = multi-frame classic
    const byte_t* d            = multi_frame_payload ? data_large : data_small;
    const size_t  sz           = multi_frame_payload ? sizeof(data_large) : sizeof(data_small);
    const canard_bytes_chain_t payload = { .bytes = { .size = sz, .data = d }, .next = NULL };
    const uint32_t             can_id  = ((uint32_t)canard_prio_nominal) << PRIO_SHIFT;
    tx_transfer_t*             tr      = tx_transfer_new(self, 1000000LL, can_id, false, NULL);
    if (tr != NULL) {
        if (!tx_push(self, tr, false, CANARD_IFACE_BITMAP_ALL, 5U, payload, CRC_INITIAL)) {
            tr = NULL; // tx_push already freed tr on failure
        }
    }
    return tr;
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

// =====================================================================================================================
// Group 1: node_id_occupancy_reset
// =====================================================================================================================

static void test_reset_clears_bitmap(void)
{
    canard_t self;
    memset(&self, 0, sizeof(self));
    self.node_id_occupancy_bitmap[0] = UINT64_MAX;
    self.node_id_occupancy_bitmap[1] = UINT64_MAX;
    node_id_occupancy_reset(&self);
    TEST_ASSERT_EQUAL_UINT64(1U, self.node_id_occupancy_bitmap[0]);
    TEST_ASSERT_EQUAL_UINT64(0U, self.node_id_occupancy_bitmap[1]);
}

static void test_reset_preserves_other_fields(void)
{
    canard_t self;
    memset(&self, 0, sizeof(self));
    self.node_id                     = 42;
    self.prng_state                  = 123;
    self.err.collision               = 7;
    self.node_id_occupancy_bitmap[0] = UINT64_MAX;
    self.node_id_occupancy_bitmap[1] = UINT64_MAX;
    node_id_occupancy_reset(&self);
    TEST_ASSERT_EQUAL_UINT8(42, self.node_id);
    TEST_ASSERT_EQUAL_UINT64(123, self.prng_state);
    TEST_ASSERT_EQUAL_UINT64(7, self.err.collision);
    TEST_ASSERT_EQUAL_UINT64(1U, self.node_id_occupancy_bitmap[0]);
    TEST_ASSERT_EQUAL_UINT64(0U, self.node_id_occupancy_bitmap[1]);
}

// =====================================================================================================================
// Group 2: Early Exit Paths
// =====================================================================================================================

static void test_update_anonymous_noop(void)
{
    canard_t       self = make_canard(0, 10);
    const uint64_t bm0  = self.node_id_occupancy_bitmap[0];
    const uint64_t bm1  = self.node_id_occupancy_bitmap[1];
    const uint64_t prng = self.prng_state;
    node_id_occupancy_update(&self, CANARD_NODE_ID_ANONYMOUS);
    TEST_ASSERT_EQUAL_UINT64(bm0, self.node_id_occupancy_bitmap[0]);
    TEST_ASSERT_EQUAL_UINT64(bm1, self.node_id_occupancy_bitmap[1]);
    TEST_ASSERT_EQUAL_UINT64(prng, self.prng_state);
    TEST_ASSERT_EQUAL_UINT64(0, self.err.collision);
}

static void test_update_known_noncolliding_noop(void)
{
    canard_t self = make_canard(0, 10);
    bitmap_set(self.node_id_occupancy_bitmap, 42);
    const uint64_t prng = self.prng_state;
    node_id_occupancy_update(&self, 42);
    // bitmap_test(42)==true AND node_id(10)!=42 -> early return
    TEST_ASSERT_EQUAL_UINT64(prng, self.prng_state);
    TEST_ASSERT_EQUAL_UINT64(0, self.err.collision);
    TEST_ASSERT_EQUAL_UINT8(10, self.node_id);
}

static void test_update_new_src_marks_bit(void)
{
    canard_t self = make_canard(0, 10);
    TEST_ASSERT_FALSE(bitmap_test(self.node_id_occupancy_bitmap, 42));
    node_id_occupancy_update(&self, 42);
    TEST_ASSERT_TRUE(bitmap_test(self.node_id_occupancy_bitmap, 42));
    TEST_ASSERT_EQUAL_UINT8(10, self.node_id);
    TEST_ASSERT_EQUAL_UINT64(0, self.err.collision);
}

// =====================================================================================================================
// Group 3: Collision Reroll
// =====================================================================================================================

static void test_collision_basic(void)
{
    canard_t self = make_canard(0, 42);
    node_id_occupancy_update(&self, 42);
    TEST_ASSERT_EQUAL_UINT64(1, self.err.collision);
    TEST_ASSERT_TRUE(self.rx.filters_dirty);
    TEST_ASSERT_TRUE(self.node_id != 42);
    TEST_ASSERT_TRUE(self.node_id > 0);
    TEST_ASSERT_TRUE(self.node_id <= CANARD_NODE_ID_MAX);
    TEST_ASSERT_FALSE(bitmap_test(self.node_id_occupancy_bitmap, self.node_id));
}

static void test_collision_known_src_still_triggers(void)
{
    canard_t self = make_canard(0, 42);
    // Pre-mark bit 42, so bitmap_test(42) is true, but node_id==42, so the early-exit
    // condition (bitmap_test(src) && node_id != src) is FALSE -- collision must still fire.
    bitmap_set(self.node_id_occupancy_bitmap, 42);
    node_id_occupancy_update(&self, 42);
    TEST_ASSERT_EQUAL_UINT64(1, self.err.collision);
    TEST_ASSERT_TRUE(self.rx.filters_dirty);
    TEST_ASSERT_TRUE(self.node_id != 42);
    TEST_ASSERT_TRUE(self.node_id > 0);
    TEST_ASSERT_TRUE(self.node_id <= CANARD_NODE_ID_MAX);
}

static void test_collision_zc_one_midrange(void)
{
    // All bits set except 50. node_id=10, src=10. zc=1.
    // For zc=1: random always returns 0 regardless of seed. Phase-1 skipped. Phase-2 scans to bit 50.
    // chance is called first (pc=127 > 64), and with zc=1 it always returns true.
    // After collision: node_id=50. Purge fires: bitmap reset to {0, 10}.
    canard_t self = make_canard(0, 10);
    fill_bitmap_except(&self, (size_t[]){ 50 }, 1);
    node_id_occupancy_update(&self, 10);
    TEST_ASSERT_EQUAL_UINT8(50, self.node_id);
    TEST_ASSERT_EQUAL_UINT64(1, self.err.collision);
    TEST_ASSERT_TRUE(self.rx.filters_dirty);
    // After purge: bitmap should be {0, 10}
    TEST_ASSERT_TRUE(bitmap_test(self.node_id_occupancy_bitmap, 0));
    TEST_ASSERT_TRUE(bitmap_test(self.node_id_occupancy_bitmap, 10));
    TEST_ASSERT_EQUAL_UINT8(2, popcount(self.node_id_occupancy_bitmap[0]) + popcount(self.node_id_occupancy_bitmap[1]));
}

static void test_collision_zc_one_bit_one(void)
{
    // Only bit 1 is free. Must reroll to 1.
    canard_t self = make_canard(0, 10);
    fill_bitmap_except(&self, (size_t[]){ 1 }, 1);
    node_id_occupancy_update(&self, 10);
    TEST_ASSERT_EQUAL_UINT8(1, self.node_id);
    TEST_ASSERT_EQUAL_UINT64(1, self.err.collision);
}

static void test_collision_zc_one_bit_127(void)
{
    // Only bit 127 is free. Must reroll to 127.
    canard_t self = make_canard(0, 10);
    fill_bitmap_except(&self, (size_t[]){ 127 }, 1);
    node_id_occupancy_update(&self, 10);
    TEST_ASSERT_EQUAL_UINT8(127, self.node_id);
    TEST_ASSERT_EQUAL_UINT64(1, self.err.collision);
}

static void test_collision_two_free_first(void)
{
    // Bits 5 and 100 are the only free bits. zc=2, pc=126 (>64 so chance IS called).
    // Want: chance returns false (no purge), random returns 0 -> pick bit 5.
    const uint64_t seed = find_seed_dense(2, false, 0);
    TEST_ASSERT_NOT_EQUAL_UINT64(UINT64_MAX, seed);
    canard_t     self     = make_canard(seed, 10);
    const size_t except[] = { 5, 100 };
    fill_bitmap_except(&self, except, 2);
    node_id_occupancy_update(&self, 10);
    TEST_ASSERT_EQUAL_UINT8(5, self.node_id);
    TEST_ASSERT_EQUAL_UINT64(1, self.err.collision);
    // No purge (chance returned false), so bitmap is unchanged (all set except 5 and 100, minus node_id=5 which is
    // clear)
    TEST_ASSERT_EQUAL_UINT8(126,
                            popcount(self.node_id_occupancy_bitmap[0]) + popcount(self.node_id_occupancy_bitmap[1]));
}

static void test_collision_two_free_second(void)
{
    // Same setup but random returns 1 -> pick bit 100.
    const uint64_t seed = find_seed_dense(2, false, 1);
    TEST_ASSERT_NOT_EQUAL_UINT64(UINT64_MAX, seed);
    canard_t     self     = make_canard(seed, 10);
    const size_t except[] = { 5, 100 };
    fill_bitmap_except(&self, except, 2);
    node_id_occupancy_update(&self, 10);
    TEST_ASSERT_EQUAL_UINT8(100, self.node_id);
    TEST_ASSERT_EQUAL_UINT64(1, self.err.collision);
    TEST_ASSERT_EQUAL_UINT8(126,
                            popcount(self.node_id_occupancy_bitmap[0]) + popcount(self.node_id_occupancy_bitmap[1]));
}

static void test_collision_repeated(void)
{
    canard_t self = make_canard(0, 42);
    for (int i = 0; i < 3; i++) {
        const byte_t old_id = self.node_id;
        node_id_occupancy_update(&self, self.node_id);
        TEST_ASSERT_EQUAL_UINT64((uint64_t)(i + 1), self.err.collision);
        TEST_ASSERT_TRUE(old_id != self.node_id);
        TEST_ASSERT_TRUE(self.node_id > 0);
        TEST_ASSERT_TRUE(self.node_id <= CANARD_NODE_ID_MAX);
        TEST_ASSERT_FALSE(bitmap_test(self.node_id_occupancy_bitmap, self.node_id));
    }
}

// =====================================================================================================================
// Group 4: Probabilistic Purge
// =====================================================================================================================

static void test_purge_below_threshold_no_call(void)
{
    // After reset, bit 0 is set (pc=1). Set bits 1..62 -> pc=63. Update with src=63 -> pc=64. 64>64 is false.
    canard_t self = make_canard(123, 120);
    for (byte_t i = 1; i < 63; i++) {
        bitmap_set(self.node_id_occupancy_bitmap, i);
    }
    // pc=63 now. Adding src=63 gives pc=64. 64 > 64 is false -> chance NOT called.
    const uint64_t prng = self.prng_state;
    node_id_occupancy_update(&self, 63);
    TEST_ASSERT_TRUE(bitmap_test(self.node_id_occupancy_bitmap, 63));
    // No collision (node_id=120, src=63), no chance call -> prng untouched
    TEST_ASSERT_EQUAL_UINT64(prng, self.prng_state);
    TEST_ASSERT_EQUAL_UINT64(0, self.err.collision);
    TEST_ASSERT_EQUAL_UINT8(120, self.node_id);
}

static void test_purge_above_threshold_fires(void)
{
    // Set 64 bits (0..63). pc=64. Update with src=64 -> pc=65. 65>64=true -> chance called.
    // Need seed where chance(self, 63) returns true. zc=128-65=63.
    const uint64_t seed = find_seed_chance(63, true);
    TEST_ASSERT_NOT_EQUAL_UINT64(UINT64_MAX, seed);
    canard_t self = make_canard(seed, 120);
    for (byte_t i = 1; i < 64; i++) {
        bitmap_set(self.node_id_occupancy_bitmap, i);
    }
    // pc=64. Add src=64 -> pc=65. zc=63. chance(self,63) returns true -> purge fires.
    node_id_occupancy_update(&self, 64);
    // No collision (node_id=120, src=64).
    TEST_ASSERT_EQUAL_UINT64(0, self.err.collision);
    TEST_ASSERT_EQUAL_UINT8(120, self.node_id);
    // After purge: bitmap = {0, 64}
    TEST_ASSERT_TRUE(bitmap_test(self.node_id_occupancy_bitmap, 0));
    TEST_ASSERT_TRUE(bitmap_test(self.node_id_occupancy_bitmap, 64));
    TEST_ASSERT_EQUAL_UINT8(2, popcount(self.node_id_occupancy_bitmap[0]) + popcount(self.node_id_occupancy_bitmap[1]));
}

static void test_purge_with_collision(void)
{
    // Dense bitmap with zc=2, collision on occupied bit, purge=true.
    // Free bits: 5 and 100. node_id=10, src=10. pc=126, zc=2.
    // Want: chance=true, random=1 -> pick bit 100.
    const uint64_t seed = find_seed_dense(2, true, 1);
    TEST_ASSERT_NOT_EQUAL_UINT64(UINT64_MAX, seed);
    canard_t     self     = make_canard(seed, 10);
    const size_t except[] = { 5, 100 };
    fill_bitmap_except(&self, except, 2);
    node_id_occupancy_update(&self, 10);
    // Collision reroll happens first (using pre-purge bitmap): new node_id=100.
    TEST_ASSERT_EQUAL_UINT8(100, self.node_id);
    TEST_ASSERT_EQUAL_UINT64(1, self.err.collision);
    TEST_ASSERT_TRUE(self.rx.filters_dirty);
    // Then purge fires: bitmap reset to {0, 10}. The new node_id (100) is NOT in the purged bitmap.
    TEST_ASSERT_TRUE(bitmap_test(self.node_id_occupancy_bitmap, 0));
    TEST_ASSERT_TRUE(bitmap_test(self.node_id_occupancy_bitmap, 10));
    TEST_ASSERT_EQUAL_UINT8(2, popcount(self.node_id_occupancy_bitmap[0]) + popcount(self.node_id_occupancy_bitmap[1]));
}

// =====================================================================================================================
// Group 5: TX Purge on Collision
// =====================================================================================================================

static void test_collision_empty_tx_queue(void)
{
    canard_t                 self;
    tx_test_ctx_t            ctx;
    instrumented_allocator_t alloc_transfer;
    instrumented_allocator_t alloc_frame;
    init_tx_canard(&self, &ctx, &alloc_transfer, &alloc_frame, 42, 0);
    // Empty TX queue. Trigger collision.
    node_id_occupancy_update(&self, 42);
    TEST_ASSERT_EQUAL_UINT64(1, self.err.collision);
    TEST_ASSERT_TRUE(self.node_id != 42);
    TEST_ASSERT_TRUE(self.node_id > 0);
    TEST_ASSERT_TRUE(self.node_id <= CANARD_NODE_ID_MAX);
    TEST_ASSERT_EQUAL_UINT64(0, alloc_transfer.allocated_fragments);
    TEST_ASSERT_EQUAL_UINT64(0, alloc_frame.allocated_fragments);
}

static void test_collision_purges_started_multiframe(void)
{
    canard_t                 self;
    tx_test_ctx_t            ctx;
    instrumented_allocator_t alloc_transfer;
    instrumented_allocator_t alloc_frame;
    init_tx_canard(&self, &ctx, &alloc_transfer, &alloc_frame, 42, 0);
    // Enqueue a multi-frame transfer.
    tx_transfer_t* const tr = enqueue_transfer(&self, true);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_TRUE(tr->multi_frame != 0U);
    // Simulate first frame departed.
    tr->first_frame_departed = 1U;
    // Trigger collision: tx_purge_continuations should free this transfer.
    node_id_occupancy_update(&self, 42);
    TEST_ASSERT_EQUAL_UINT64(1, self.err.collision);
    // Transfer was purged.
    TEST_ASSERT_EQUAL_UINT64(0, alloc_transfer.allocated_fragments);
    TEST_ASSERT_EQUAL_UINT64(0, alloc_frame.allocated_fragments);
}

static void test_collision_keeps_undeparted(void)
{
    canard_t                 self;
    tx_test_ctx_t            ctx;
    instrumented_allocator_t alloc_transfer;
    instrumented_allocator_t alloc_frame;
    init_tx_canard(&self, &ctx, &alloc_transfer, &alloc_frame, 42, 0);
    // Enqueue a multi-frame transfer but do NOT mark first_frame_departed.
    tx_transfer_t* const tr = enqueue_transfer(&self, true);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_TRUE(tr->multi_frame != 0U);
    TEST_ASSERT_TRUE(tr->first_frame_departed == 0U);
    // Trigger collision: tx_purge_continuations should NOT free this transfer.
    node_id_occupancy_update(&self, 42);
    TEST_ASSERT_EQUAL_UINT64(1, self.err.collision);
    // Transfer still alive.
    TEST_ASSERT_TRUE(alloc_transfer.allocated_fragments > 0);
    // Clean up.
    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_UINT64(0, alloc_transfer.allocated_fragments);
    TEST_ASSERT_EQUAL_UINT64(0, alloc_frame.allocated_fragments);
}

static void test_collision_keeps_singleframe(void)
{
    canard_t                 self;
    tx_test_ctx_t            ctx;
    instrumented_allocator_t alloc_transfer;
    instrumented_allocator_t alloc_frame;
    init_tx_canard(&self, &ctx, &alloc_transfer, &alloc_frame, 42, 0);
    // Enqueue a single-frame transfer and mark departed.
    tx_transfer_t* const tr = enqueue_transfer(&self, false);
    TEST_ASSERT_NOT_NULL(tr);
    TEST_ASSERT_TRUE(tr->multi_frame == 0U);
    tr->first_frame_departed = 1U;
    // Trigger collision: single-frame transfers are NOT purged (multi_frame==0).
    node_id_occupancy_update(&self, 42);
    TEST_ASSERT_EQUAL_UINT64(1, self.err.collision);
    // Transfer survives.
    TEST_ASSERT_TRUE(alloc_transfer.allocated_fragments > 0);
    // Clean up.
    free_all_transfers(&self);
    TEST_ASSERT_EQUAL_UINT64(0, alloc_transfer.allocated_fragments);
    TEST_ASSERT_EQUAL_UINT64(0, alloc_frame.allocated_fragments);
}

// =====================================================================================================================
// Group 6: Exhaustive / Property Tests
// =====================================================================================================================

static void test_collision_exhaustive_zc_one(void)
{
    // For every possible single free bit position p in [1,127], verify the collision picks exactly p.
    for (size_t p = 1; p <= CANARD_NODE_ID_MAX; p++) {
        const byte_t occupied_id = (byte_t)((p == 10) ? 11 : 10);
        canard_t     self        = make_canard(0, occupied_id);
        fill_bitmap_except(&self, &p, 1);
        const byte_t colliding = self.node_id;
        node_id_occupancy_update(&self, colliding);
        TEST_ASSERT_EQUAL_UINT8((byte_t)p, self.node_id);
        TEST_ASSERT_EQUAL_UINT64(1, self.err.collision);
    }
}

static void test_collision_never_picks_occupied(void)
{
    // 1000 random bitmaps with varying densities. Verify collisions always land on a free bit.
    uint64_t rng = 0xDEADBEEFCAFEBABEULL;
    for (int iter = 0; iter < 1000; iter++) {
        canard_t self;
        memset(&self, 0, sizeof(self));
        self.prng_state = splitmix64(&rng);
        // Generate a random bitmap with at least 2 free bits (one for the collision target, one for
        // the invariant that the new node_id is free). Bit 0 is always set.
        self.node_id_occupancy_bitmap[0] = 1; // bit 0
        self.node_id_occupancy_bitmap[1] = 0;
        // Randomly set bits [1,127] with about 50% probability
        for (size_t b = 1; b <= CANARD_NODE_ID_MAX; b++) {
            if ((splitmix64(&rng) % 2) == 0) {
                bitmap_set(self.node_id_occupancy_bitmap, b);
            }
        }
        // Ensure at least 2 free bits by clearing two if needed
        const byte_t pc = popcount(self.node_id_occupancy_bitmap[0]) + popcount(self.node_id_occupancy_bitmap[1]);
        if (pc >= 127) {
            // Clear bits 1 and 2 to guarantee free slots
            self.node_id_occupancy_bitmap[0] &= ~(UINT64_C(1) << 1U);
            self.node_id_occupancy_bitmap[0] &= ~(UINT64_C(1) << 2U);
        }
        // Pick an occupied node_id to collide on
        byte_t nid = 0;
        for (size_t b = 1; b <= CANARD_NODE_ID_MAX; b++) {
            if (bitmap_test(self.node_id_occupancy_bitmap, b)) {
                nid = (byte_t)b;
                break;
            }
        }
        if (nid == 0) {
            // No occupied bit found (very unlikely); set one
            bitmap_set(self.node_id_occupancy_bitmap, 1);
            nid = 1;
        }
        self.node_id = nid;
        node_id_occupancy_update(&self, nid);
        TEST_ASSERT_EQUAL_UINT64(1, self.err.collision);
        TEST_ASSERT_TRUE(self.node_id > 0);
        TEST_ASSERT_TRUE(self.node_id <= CANARD_NODE_ID_MAX);
        TEST_ASSERT_FALSE(bitmap_test(self.node_id_occupancy_bitmap, self.node_id));
        TEST_ASSERT_TRUE(self.node_id != nid);
    }
}

// =====================================================================================================================
// Group 7: Nearly-Full Bitmap and Purge Reset
// =====================================================================================================================

static void test_occupancy_bitmap_nearly_full(void)
{
    // Fill the bitmap so that only positions 50 and 100 are free (126 of 128 bits set).
    // On collision, the new node-ID must be one of the two free slots.
    const size_t except[] = { 50, 100 };
    // zc = 128 - 126 = 2. pc = 126 > 64, so chance IS called.
    // Try both outcomes: random=0 -> pick first free (50), random=1 -> pick second free (100).
    {
        const uint64_t seed = find_seed_dense(2, false, 0);
        TEST_ASSERT_NOT_EQUAL_UINT64(UINT64_MAX, seed);
        canard_t self = make_canard(seed, 10);
        fill_bitmap_except(&self, except, 2);
        node_id_occupancy_update(&self, 10);
        TEST_ASSERT_EQUAL_UINT64(1, self.err.collision);
        TEST_ASSERT_EQUAL_UINT8(50, self.node_id);
    }
    {
        const uint64_t seed = find_seed_dense(2, false, 1);
        TEST_ASSERT_NOT_EQUAL_UINT64(UINT64_MAX, seed);
        canard_t self = make_canard(seed, 10);
        fill_bitmap_except(&self, except, 2);
        node_id_occupancy_update(&self, 10);
        TEST_ASSERT_EQUAL_UINT64(1, self.err.collision);
        TEST_ASSERT_EQUAL_UINT8(100, self.node_id);
    }
}

static void test_collision_purge_resets_bitmap(void)
{
    // When pc > 64, a probabilistic purge may fire. After purge, the bitmap should contain only
    // bit 0 and the src node-ID bit.
    // Setup: 65 bits set (0..64), src=65 (new, not yet set). After adding src: pc=66, zc=62.
    // Need chance(self, 62) to return true so purge fires.
    const uint64_t seed = find_seed_chance(62, true);
    TEST_ASSERT_NOT_EQUAL_UINT64(UINT64_MAX, seed);
    canard_t self = make_canard(seed, 120);
    for (byte_t i = 1; i < 65; i++) {
        bitmap_set(self.node_id_occupancy_bitmap, i);
    }
    // pc=65. Add src=65 -> pc=66, zc=62. chance returns true -> purge fires.
    node_id_occupancy_update(&self, 65);
    // No collision (node_id=120, src=65).
    TEST_ASSERT_EQUAL_UINT64(0, self.err.collision);
    TEST_ASSERT_EQUAL_UINT8(120, self.node_id);
    // After purge: bitmap = {0, 65}
    TEST_ASSERT_TRUE(bitmap_test(self.node_id_occupancy_bitmap, 0));
    TEST_ASSERT_TRUE(bitmap_test(self.node_id_occupancy_bitmap, 65));
    const byte_t pc = popcount(self.node_id_occupancy_bitmap[0]) + popcount(self.node_id_occupancy_bitmap[1]);
    TEST_ASSERT_EQUAL_UINT8(2, pc);
    // Verify that the previously-set bits 1..64 are all cleared.
    for (byte_t i = 1; i < 65; i++) {
        TEST_ASSERT_FALSE(bitmap_test(self.node_id_occupancy_bitmap, i));
    }
}

// =====================================================================================================================
// Harness
// =====================================================================================================================

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    // Group 1: Reset
    RUN_TEST(test_reset_clears_bitmap);
    RUN_TEST(test_reset_preserves_other_fields);
    // Group 2: Early exit
    RUN_TEST(test_update_anonymous_noop);
    RUN_TEST(test_update_known_noncolliding_noop);
    RUN_TEST(test_update_new_src_marks_bit);
    // Group 3: Collision reroll
    RUN_TEST(test_collision_basic);
    RUN_TEST(test_collision_known_src_still_triggers);
    RUN_TEST(test_collision_zc_one_midrange);
    RUN_TEST(test_collision_zc_one_bit_one);
    RUN_TEST(test_collision_zc_one_bit_127);
    RUN_TEST(test_collision_two_free_first);
    RUN_TEST(test_collision_two_free_second);
    RUN_TEST(test_collision_repeated);
    // Group 4: Probabilistic purge
    RUN_TEST(test_purge_below_threshold_no_call);
    RUN_TEST(test_purge_above_threshold_fires);
    RUN_TEST(test_purge_with_collision);
    // Group 5: TX purge on collision
    RUN_TEST(test_collision_empty_tx_queue);
    RUN_TEST(test_collision_purges_started_multiframe);
    RUN_TEST(test_collision_keeps_undeparted);
    RUN_TEST(test_collision_keeps_singleframe);
    // Group 6: Exhaustive / property tests
    RUN_TEST(test_collision_exhaustive_zc_one);
    RUN_TEST(test_collision_never_picks_occupied);
    // Group 7: Nearly-full bitmap and purge reset
    RUN_TEST(test_occupancy_bitmap_nearly_full);
    RUN_TEST(test_collision_purge_resets_bitmap);
    return UNITY_END();
}

// NOLINTEND(*)
