// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal Development Team.

// NOLINTBEGIN(*-avoid-c-arrays,*-magic-numbers,*-vararg,*-use-auto,*-designated-initializers)

#include "helpers.h"
#include <unity.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// =====================================================================================================================
// TX capture infrastructure: records outgoing frames for post-hoc verification.
// Extended to also capture payload data (needed for scatter-gather verification).
// =====================================================================================================================

struct tx_record_t
{
    canard_us_t   deadline;
    uint_least8_t iface_index;
    bool          fd;
    uint32_t      can_id;
    uint_least8_t tail;
    size_t        data_size;
    uint_least8_t data[64]; // Enough for the largest CAN FD frame.
};

struct tx_capture_t
{
    canard_us_t                  now;
    bool                         accept_tx;
    size_t                       count;
    std::array<tx_record_t, 128> records;
};

static tx_capture_t* capture_from(const canard_t* const self) { return static_cast<tx_capture_t*>(self->user_context); }

static canard_us_t capture_now(const canard_t* const self) { return capture_from(self)->now; }

static bool capture_tx(canard_t* const self,
                       void* const,
                       const canard_us_t    deadline,
                       const uint_least8_t  iface_index,
                       const bool           fd,
                       const uint32_t       extended_can_id,
                       const canard_bytes_t can_data)
{
    tx_capture_t* const cap = capture_from(self);
    TEST_ASSERT_NOT_NULL(cap);
    if (cap->count < cap->records.size()) {
        tx_record_t& rec = cap->records[cap->count];
        rec.deadline     = deadline;
        rec.iface_index  = iface_index;
        rec.fd           = fd;
        rec.can_id       = extended_can_id;
        rec.tail         = 0U;
        rec.data_size    = can_data.size;
        if ((can_data.size > 0U) && (can_data.data != nullptr)) {
            const auto* const bytes = static_cast<const uint_least8_t*>(can_data.data);
            rec.tail                = bytes[can_data.size - 1U];
            std::memcpy(rec.data, bytes, can_data.size);
        }
    }
    cap->count++;
    return cap->accept_tx;
}

static const canard_vtable_t capture_vtable = { .now = capture_now, .tx = capture_tx, .filter = nullptr };

// =====================================================================================================================
// Instrumented-allocator-backed memory set for leak/OOM tracking.
// We create independent allocator instances so individual pools can be constrained.
// =====================================================================================================================

struct mem_pool_t
{
    instrumented_allocator_t tx_transfer;
    instrumented_allocator_t tx_frame;
    instrumented_allocator_t rx_session;
    instrumented_allocator_t rx_payload;
    instrumented_allocator_t rx_filters;
};

static void mem_pool_new(mem_pool_t* const pool)
{
    instrumented_allocator_new(&pool->tx_transfer);
    instrumented_allocator_new(&pool->tx_frame);
    instrumented_allocator_new(&pool->rx_session);
    instrumented_allocator_new(&pool->rx_payload);
    instrumented_allocator_new(&pool->rx_filters);
}

static canard_mem_set_t mem_pool_make(mem_pool_t* const pool)
{
    return canard_mem_set_t{
        .tx_transfer = instrumented_allocator_make_resource(&pool->tx_transfer),
        .tx_frame    = instrumented_allocator_make_resource(&pool->tx_frame),
        .rx_session  = instrumented_allocator_make_resource(&pool->rx_session),
        .rx_payload  = instrumented_allocator_make_resource(&pool->rx_payload),
        .rx_filters  = instrumented_allocator_make_resource(&pool->rx_filters),
    };
}

static void mem_pool_verify_no_leaks(const mem_pool_t* const pool)
{
    TEST_ASSERT_EQUAL_UINT64(0U, pool->tx_transfer.allocated_fragments);
    TEST_ASSERT_EQUAL_UINT64(0U, pool->tx_frame.allocated_fragments);
    TEST_ASSERT_EQUAL_UINT64(0U, pool->rx_session.allocated_fragments);
    TEST_ASSERT_EQUAL_UINT64(0U, pool->rx_payload.allocated_fragments);
    TEST_ASSERT_EQUAL_UINT64(0U, pool->rx_filters.allocated_fragments);
}

// =====================================================================================================================
// Initialization helpers.
// =====================================================================================================================

static void init_node(canard_t* const     self,
                      tx_capture_t* const cap,
                      mem_pool_t* const   pool,
                      const size_t        queue_capacity,
                      const uint_least8_t node_id)
{
    *cap           = tx_capture_t{};
    cap->now       = 0;
    cap->accept_tx = true;
    cap->count     = 0;
    mem_pool_new(pool);
    TEST_ASSERT_TRUE(canard_new(self, &capture_vtable, mem_pool_make(pool), queue_capacity, 42U, 0U));
    TEST_ASSERT_TRUE(canard_set_node_id(self, node_id));
    self->user_context = cap;
}

static canard_bytes_chain_t make_payload(const void* const data, const size_t size)
{
    return canard_bytes_chain_t{
        .bytes = { .size = size, .data = data },
        .next  = nullptr,
    };
}

static canard_bytes_chain_t make_empty_payload()
{
    return canard_bytes_chain_t{
        .bytes = { .size = 0U, .data = nullptr },
        .next  = nullptr,
    };
}

// Extract transfer-ID from the tail byte of a Cyphal v1 frame: lower 5 bits.
static uint_least8_t tid_from_tail(const uint_least8_t tail)
{
    return static_cast<uint_least8_t>(tail & CANARD_TRANSFER_ID_MAX);
}

// =====================================================================================================================
// Test 1: test_tx_queue_sacrifice_oldest
//   queue_capacity=3. Publish 3 single-frame transfers (TID 0,1,2). Queue full.
//   Publish TID 3 -> oldest (TID 0) is sacrificed. err.tx_sacrifice=1.
//   Poll and verify only TIDs 1,2,3 are ejected.
// =====================================================================================================================
static void test_tx_queue_sacrifice_oldest()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    mem_pool_t   pool = {};
    init_node(&self, &cap, &pool, 3U, 42U);

    const canard_bytes_chain_t payload = make_empty_payload();
    // Enqueue 3 single-frame transfers on iface 0 only.
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 100U, 0U, payload, nullptr));
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 100U, 1U, payload, nullptr));
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 100U, 2U, payload, nullptr));
    TEST_ASSERT_EQUAL_size_t(3U, self.tx.queue_size);
    TEST_ASSERT_EQUAL_UINT64(0U, self.err.tx_sacrifice);

    // Fourth publish triggers sacrifice of oldest (TID 0).
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 100U, 3U, payload, nullptr));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.tx_sacrifice);
    TEST_ASSERT_EQUAL_size_t(3U, self.tx.queue_size);

    // Poll all frames out.
    canard_poll(&self, 1U);
    TEST_ASSERT_EQUAL_size_t(3U, cap.count);
    // The remaining transfers are TID 1, 2, 3 (TID 0 was sacrificed).
    TEST_ASSERT_EQUAL_UINT8(1U, tid_from_tail(cap.records[0].tail));
    TEST_ASSERT_EQUAL_UINT8(2U, tid_from_tail(cap.records[1].tail));
    TEST_ASSERT_EQUAL_UINT8(3U, tid_from_tail(cap.records[2].tail));

    canard_destroy(&self);
    mem_pool_verify_no_leaks(&pool);
}

// =====================================================================================================================
// Test 2: test_tx_queue_sacrifice_multiframe_reclaims_all
//   queue_capacity=4. Publish one 3-frame multiframe (Classic CAN, ~20 bytes payload) and one single-frame.
//   Queue has 4 frames. Publish another single-frame -> multiframe (3 frames) sacrificed. err.tx_sacrifice=1.
// =====================================================================================================================
static void test_tx_queue_sacrifice_multiframe_reclaims_all()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    mem_pool_t   pool = {};
    init_node(&self, &cap, &pool, 4U, 42U);
    self.tx.fd = false; // Classic CAN: MTU=8, 7 payload bytes/frame.

    // 20-byte payload -> single-frame threshold for classic CAN is 7 bytes.
    // tx_predict_frame_count(20, 8) = ceil((20 + 2 + 6) / 7) = ceil(28/7) = 4... let's be precise.
    // Actually: bytes_per_frame = 8 - 1 = 7. size=20 >= 7, so multiframe.
    // n_frames = ceil((20 + 2 + 7 - 1) / 7) = ceil(28/7) = 4. That's 4 frames, which fills the queue alone.
    // We need exactly 3 frames, so payload size should yield 3 frames:
    // ceil((N + 2 + 6) / 7) = 3 -> N+8 <= 21 -> N <= 13. At N=13: ceil((13+2+6)/7) = ceil(21/7) = 3.
    const uint_least8_t        multi_data[13] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
    const canard_bytes_chain_t multi_payload  = make_payload(multi_data, sizeof(multi_data));
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 200U, 0U, multi_payload, nullptr));
    TEST_ASSERT_EQUAL_size_t(3U, self.tx.queue_size);

    // Single-frame transfer (empty payload = 1 frame).
    const canard_bytes_chain_t single_payload = make_empty_payload();
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 201U, 1U, single_payload, nullptr));
    TEST_ASSERT_EQUAL_size_t(4U, self.tx.queue_size);
    TEST_ASSERT_EQUAL_UINT64(0U, self.err.tx_sacrifice);

    // Publish another single-frame -> oldest (the 3-frame multiframe) sacrificed.
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 202U, 2U, single_payload, nullptr));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.tx_sacrifice);
    // 4 - 3 (sacrificed) + 1 (new) = 2.
    TEST_ASSERT_EQUAL_size_t(2U, self.tx.queue_size);

    canard_destroy(&self);
    mem_pool_verify_no_leaks(&pool);
}

// =====================================================================================================================
// Test 3: test_tx_queue_sacrifice_multiple_rounds
//   queue_capacity=4. Fill with 4 single-frame. Publish multiframe needing 3 frames ->
//   must sacrifice 3 old transfers to make room. err.tx_sacrifice >= 3.
// =====================================================================================================================
static void test_tx_queue_sacrifice_multiple_rounds()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    mem_pool_t   pool = {};
    init_node(&self, &cap, &pool, 4U, 42U);
    self.tx.fd = false; // Classic CAN

    const canard_bytes_chain_t single_payload = make_empty_payload();
    for (uint_least8_t tid = 0; tid < 4U; tid++) {
        TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 300U, tid, single_payload, nullptr));
    }
    TEST_ASSERT_EQUAL_size_t(4U, self.tx.queue_size);

    // Multiframe needing 3 frames (13-byte payload on Classic CAN: ceil((13+2+6)/7)=3).
    const uint_least8_t        multi_data[13] = {};
    const canard_bytes_chain_t multi_payload  = make_payload(multi_data, sizeof(multi_data));
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 301U, 10U, multi_payload, nullptr));
    // Must sacrifice 3 transfers to fit the 3-frame multiframe transfer.
    TEST_ASSERT_TRUE(self.err.tx_sacrifice >= 3U);
    // 4 - 3 sacrificed + 3 new multiframe frames = 4. But wait, the remaining 1 single-frame + 3 multiframe = 4.
    TEST_ASSERT_EQUAL_size_t(4U, self.tx.queue_size);

    canard_destroy(&self);
    mem_pool_verify_no_leaks(&pool);
}

// =====================================================================================================================
// Test 4: test_tx_queue_capacity_exceeded
//   queue_capacity=2. Publish multiframe needing 4 frames.
//   Even after sacrificing everything, can't fit. Returns false. err.tx_capacity=1.
// =====================================================================================================================
static void test_tx_queue_capacity_exceeded()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    mem_pool_t   pool = {};
    init_node(&self, &cap, &pool, 2U, 42U);
    self.tx.fd = false; // Classic CAN

    // 25-byte payload on classic CAN: ceil((25+2+6)/7) = ceil(33/7) = 5 frames. Way over capacity=2.
    const uint_least8_t        big_data[25] = {};
    const canard_bytes_chain_t big_payload  = make_payload(big_data, sizeof(big_data));
    TEST_ASSERT_FALSE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 400U, 0U, big_payload, nullptr));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.tx_capacity);
    TEST_ASSERT_EQUAL_size_t(0U, self.tx.queue_size);

    canard_destroy(&self);
    mem_pool_verify_no_leaks(&pool);
}

// =====================================================================================================================
// Test 5: test_tx_queue_capacity_boundary
//   queue_capacity=N. Publish a transfer requiring exactly N frames. Succeeds. queue_size = N.
// =====================================================================================================================
static void test_tx_queue_capacity_boundary()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    mem_pool_t   pool = {};
    // Classic CAN: 13-byte payload = 3 frames. Set capacity=3.
    init_node(&self, &cap, &pool, 3U, 42U);
    self.tx.fd = false;

    const uint_least8_t        data[13] = {};
    const canard_bytes_chain_t payload  = make_payload(data, sizeof(data));
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 500U, 0U, payload, nullptr));
    TEST_ASSERT_EQUAL_size_t(3U, self.tx.queue_size);
    TEST_ASSERT_EQUAL_UINT64(0U, self.err.tx_capacity);
    TEST_ASSERT_EQUAL_UINT64(0U, self.err.tx_sacrifice);

    canard_destroy(&self);
    mem_pool_verify_no_leaks(&pool);
}

// =====================================================================================================================
// Test 6: test_tx_queue_deadline_expiration
//   Publish 2 transfers: first deadline=100, second deadline=10000. Set now=200.
//   Publish a third -> expired first transfer purged. err.tx_expiration=1.
// =====================================================================================================================
static void test_tx_queue_deadline_expiration()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    mem_pool_t   pool = {};
    init_node(&self, &cap, &pool, 16U, 42U);

    const canard_bytes_chain_t payload = make_empty_payload();
    // Transfer 1: short deadline.
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 100, 1U, canard_prio_nominal, 600U, 0U, payload, nullptr));
    // Transfer 2: long deadline.
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 601U, 1U, payload, nullptr));
    TEST_ASSERT_EQUAL_size_t(2U, self.tx.queue_size);

    // Advance time past the first transfer's deadline.
    cap.now = 200;

    // Publish a third transfer; the expired one is purged during tx_push -> tx_expire.
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 602U, 2U, payload, nullptr));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.tx_expiration);
    // 2 original - 1 expired + 1 new = 2.
    TEST_ASSERT_EQUAL_size_t(2U, self.tx.queue_size);

    // Poll and verify only transfers 2 (TID 1) and 3 (TID 2) remain.
    canard_poll(&self, 1U);
    TEST_ASSERT_EQUAL_size_t(2U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(1U, tid_from_tail(cap.records[0].tail));
    TEST_ASSERT_EQUAL_UINT8(2U, tid_from_tail(cap.records[1].tail));

    canard_destroy(&self);
    mem_pool_verify_no_leaks(&pool);
}

// =====================================================================================================================
// Test 7: test_tx_queue_ordering_priority
//   Publish prio_low then prio_fast. Poll. Fast (lower CAN ID) ejected first.
// =====================================================================================================================
static void test_tx_queue_ordering_priority()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    mem_pool_t   pool = {};
    init_node(&self, &cap, &pool, 16U, 42U);

    const canard_bytes_chain_t payload = make_empty_payload();
    // Low priority (5) first.
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_low, 700U, 0U, payload, nullptr));
    // Fast priority (2) second.
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_fast, 700U, 1U, payload, nullptr));

    canard_poll(&self, 1U);
    TEST_ASSERT_EQUAL_size_t(2U, cap.count);
    // Fast (priority 2) has a lower CAN ID than Low (priority 5) and is ejected first.
    TEST_ASSERT_TRUE(cap.records[0].can_id < cap.records[1].can_id);
    // Verify priorities via CAN ID bits [28:26].
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint_least8_t>(canard_prio_fast),
                            static_cast<uint_least8_t>((cap.records[0].can_id >> 26U) & 7U));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint_least8_t>(canard_prio_low),
                            static_cast<uint_least8_t>((cap.records[1].can_id >> 26U) & 7U));

    canard_destroy(&self);
    mem_pool_verify_no_leaks(&pool);
}

// =====================================================================================================================
// Test 8: test_tx_queue_ordering_fifo_same_priority
//   Publish two same-priority transfers with different subject IDs. FIFO order preserved.
// =====================================================================================================================
static void test_tx_queue_ordering_fifo_same_priority()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    mem_pool_t   pool = {};
    init_node(&self, &cap, &pool, 16U, 42U);

    const canard_bytes_chain_t payload = make_empty_payload();
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 800U, 0U, payload, nullptr));
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 801U, 1U, payload, nullptr));

    canard_poll(&self, 1U);
    TEST_ASSERT_EQUAL_size_t(2U, cap.count);
    // Both have same priority; first-published (subject 800) ejected first due to smaller seqno tiebreaker.
    // Subject ID occupies bits [23:8] in v1.1.
    const uint32_t sid_0 = (cap.records[0].can_id >> 8U) & 0xFFFFU;
    const uint32_t sid_1 = (cap.records[1].can_id >> 8U) & 0xFFFFU;
    TEST_ASSERT_EQUAL_UINT32(800U, sid_0);
    TEST_ASSERT_EQUAL_UINT32(801U, sid_1);

    canard_destroy(&self);
    mem_pool_verify_no_leaks(&pool);
}

// =====================================================================================================================
// Test 9: test_tx_iface_bitmap_single
//   Publish with iface_bitmap=1 (iface 0 only). pending_ifaces()=1. Poll(1) ejects. Poll(2) ejects nothing.
// =====================================================================================================================
static void test_tx_iface_bitmap_single()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    mem_pool_t   pool = {};
    init_node(&self, &cap, &pool, 16U, 42U);

    const canard_bytes_chain_t payload = make_empty_payload();
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 900U, 0U, payload, nullptr));
    TEST_ASSERT_EQUAL_UINT8(1U, canard_pending_ifaces(&self));

    // Poll iface 0 -> ejected.
    canard_poll(&self, 1U);
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(0U, cap.records[0].iface_index);

    // Poll iface 1 -> nothing.
    canard_poll(&self, 2U);
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(0U, canard_pending_ifaces(&self));
    TEST_ASSERT_EQUAL_size_t(0U, self.tx.queue_size);

    canard_destroy(&self);
    mem_pool_verify_no_leaks(&pool);
}

// =====================================================================================================================
// Test 10: test_tx_iface_bitmap_both
//   Publish with iface_bitmap=3. Both interfaces have pending frames. Poll(1) -> iface 0. Poll(2) -> iface 1.
// =====================================================================================================================
static void test_tx_iface_bitmap_both()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    mem_pool_t   pool = {};
    init_node(&self, &cap, &pool, 16U, 42U);

    const canard_bytes_chain_t payload = make_empty_payload();
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 3U, canard_prio_nominal, 1000U, 5U, payload, nullptr));
    TEST_ASSERT_EQUAL_UINT8(3U, canard_pending_ifaces(&self));

    // Poll iface 0.
    canard_poll(&self, 1U);
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(0U, cap.records[0].iface_index);

    // Iface 1 still pending.
    TEST_ASSERT_EQUAL_UINT8(2U, canard_pending_ifaces(&self));

    // Poll iface 1.
    canard_poll(&self, 2U);
    TEST_ASSERT_EQUAL_size_t(2U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(1U, cap.records[1].iface_index);

    // Same CAN ID and tail byte on both interfaces.
    TEST_ASSERT_EQUAL_UINT32(cap.records[0].can_id, cap.records[1].can_id);
    TEST_ASSERT_EQUAL_UINT8(cap.records[0].tail, cap.records[1].tail);
    TEST_ASSERT_EQUAL_UINT8(0U, canard_pending_ifaces(&self));

    canard_destroy(&self);
    mem_pool_verify_no_leaks(&pool);
}

// =====================================================================================================================
// Test 11: test_tx_refcount_lifecycle
//   Publish single-frame on iface_bitmap=3. queue_size=1 (shared via refcount).
//   Poll(1) -> iface 0 ejected, frame still alive for iface 1. Poll(2) -> freed. queue_size=0.
// =====================================================================================================================
static void test_tx_refcount_lifecycle()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    mem_pool_t   pool = {};
    init_node(&self, &cap, &pool, 16U, 42U);

    const canard_bytes_chain_t payload = make_empty_payload();
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 3U, canard_prio_nominal, 1100U, 0U, payload, nullptr));
    // Only 1 frame allocated even though 2 interfaces will use it.
    TEST_ASSERT_EQUAL_size_t(1U, self.tx.queue_size);

    // Eject from iface 0. Frame still referenced by iface 1.
    canard_poll(&self, 1U);
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(0U, cap.records[0].iface_index);
    // Frame still counted because iface 1 holds a reference.
    TEST_ASSERT_EQUAL_size_t(1U, self.tx.queue_size);

    // Eject from iface 1. Last reference -> frame freed.
    canard_poll(&self, 2U);
    TEST_ASSERT_EQUAL_size_t(2U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(1U, cap.records[1].iface_index);
    TEST_ASSERT_EQUAL_size_t(0U, self.tx.queue_size);

    canard_destroy(&self);
    mem_pool_verify_no_leaks(&pool);
}

// =====================================================================================================================
// Test 12: test_tx_scattered_gather_3_fragments
//   Publish with 3-fragment canard_bytes_chain_t. Verify frame contains assembled data.
// =====================================================================================================================
static void test_tx_scattered_gather_3_fragments()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    mem_pool_t   pool = {};
    init_node(&self, &cap, &pool, 16U, 42U);
    // CAN FD (default) for single-frame with 5 bytes total -> 1 frame with 8-byte DLC (rounded up).

    // Fragment 1: {0x11, 0x22}
    const uint_least8_t frag1[] = { 0x11U, 0x22U };
    // Fragment 2: {0x33}
    const uint_least8_t frag2[] = { 0x33U };
    // Fragment 3: {0x44, 0x55}
    const uint_least8_t frag3[] = { 0x44U, 0x55U };

    const canard_bytes_chain_t chain3 = { .bytes = { .size = sizeof(frag3), .data = frag3 }, .next = nullptr };
    const canard_bytes_chain_t chain2 = { .bytes = { .size = sizeof(frag2), .data = frag2 }, .next = &chain3 };
    const canard_bytes_chain_t chain1 = { .bytes = { .size = sizeof(frag1), .data = frag1 }, .next = &chain2 };

    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 1200U, 7U, chain1, nullptr));
    TEST_ASSERT_EQUAL_size_t(1U, self.tx.queue_size);

    canard_poll(&self, 1U);
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);

    const tx_record_t& rec = cap.records[0];
    // 5 bytes payload + 1 tail byte = 6 bytes. CAN FD DLC supports 6-byte frames natively (no padding needed).
    TEST_ASSERT_EQUAL_size_t(6U, rec.data_size);
    // Verify assembled payload bytes.
    TEST_ASSERT_EQUAL_UINT8(0x11U, rec.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x22U, rec.data[1]);
    TEST_ASSERT_EQUAL_UINT8(0x33U, rec.data[2]);
    TEST_ASSERT_EQUAL_UINT8(0x44U, rec.data[3]);
    TEST_ASSERT_EQUAL_UINT8(0x55U, rec.data[4]);
    // Tail byte: single-frame (SOT|EOT=0xC0), toggle=1 (0x20), TID=7 -> 0xE7.
    TEST_ASSERT_EQUAL_UINT8(0xE7U, rec.data[5]);

    canard_destroy(&self);
    mem_pool_verify_no_leaks(&pool);
}

// =====================================================================================================================
// Test 13: test_tx_oom_frame_allocation
//   Allow tx_transfer allocation but restrict tx_frame to 0 fragments. Publish fails. err.oom++. No leaks.
// =====================================================================================================================
static void test_tx_oom_frame_allocation()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    mem_pool_t   pool = {};
    init_node(&self, &cap, &pool, 16U, 42U);

    // Constrain the tx_frame allocator to 0 so frame allocation fails.
    pool.tx_frame.limit_fragments = 0U;

    const canard_bytes_chain_t payload = make_empty_payload();
    // The transfer object is allocated (from tx_transfer), but frame allocation fails inside tx_spool.
    TEST_ASSERT_FALSE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 1300U, 0U, payload, nullptr));
    TEST_ASSERT_TRUE(self.err.oom > 0U);
    TEST_ASSERT_EQUAL_size_t(0U, self.tx.queue_size);

    canard_destroy(&self);
    mem_pool_verify_no_leaks(&pool);
}

// =====================================================================================================================
// Test 14: test_tx_oom_transfer_allocation
//   Restrict tx_transfer to 0 fragments. Publish fails. err.oom++. No leaks.
// =====================================================================================================================
static void test_tx_oom_transfer_allocation()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    mem_pool_t   pool = {};
    init_node(&self, &cap, &pool, 16U, 42U);

    // Constrain the tx_transfer allocator to 0 so transfer allocation fails.
    pool.tx_transfer.limit_fragments = 0U;

    const canard_bytes_chain_t payload = make_empty_payload();
    TEST_ASSERT_FALSE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 1400U, 0U, payload, nullptr));
    TEST_ASSERT_TRUE(self.err.oom > 0U);
    TEST_ASSERT_EQUAL_size_t(0U, self.tx.queue_size);

    canard_destroy(&self);
    mem_pool_verify_no_leaks(&pool);
}

// =====================================================================================================================
// Test 15: test_tx_v0_always_classic_can
//   Set self.tx.fd = true. Publish via canard_v0_publish(). Poll. Frame must have fd=false.
// =====================================================================================================================
static void test_tx_v0_always_classic_can()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    mem_pool_t   pool = {};
    init_node(&self, &cap, &pool, 16U, 42U);
    self.tx.fd = true; // FD mode enabled globally.

    const canard_bytes_chain_t payload = make_empty_payload();
    // canard_v0_publish signature:
    //   (self, deadline, iface_bitmap, priority, data_type_id, crc_seed, transfer_id, payload, user_context)
    TEST_ASSERT_TRUE(canard_v0_publish(&self, 10000, 1U, canard_prio_nominal, 100U, 0U, 0U, payload, nullptr));

    canard_poll(&self, 1U);
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    // v0 transfers are always Classic CAN regardless of the fd flag.
    TEST_ASSERT_FALSE(cap.records[0].fd);

    canard_destroy(&self);
    mem_pool_verify_no_leaks(&pool);
}

// =====================================================================================================================
// Test 16: test_tx_backpressure_resumes
//   Publish single-frame. Set accept_tx=false. Poll -> TX callback returns false. Frame stays pending.
//   Set accept_tx=true. Poll again -> frame ejected. queue_size=0.
// =====================================================================================================================
static void test_tx_backpressure_resumes()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    mem_pool_t   pool = {};
    init_node(&self, &cap, &pool, 16U, 42U);

    const canard_bytes_chain_t payload = make_empty_payload();
    TEST_ASSERT_TRUE(canard_publish_16b(&self, 10000, 1U, canard_prio_nominal, 1600U, 0U, payload, nullptr));
    TEST_ASSERT_EQUAL_size_t(1U, self.tx.queue_size);

    // Simulate backpressure: TX callback rejects the frame.
    cap.accept_tx = false;
    canard_poll(&self, 1U);
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);                   // Callback was invoked.
    TEST_ASSERT_EQUAL_UINT8(1U, canard_pending_ifaces(&self)); // Still pending.
    TEST_ASSERT_EQUAL_size_t(1U, self.tx.queue_size);          // Frame not freed.

    // Release backpressure and retry.
    cap.accept_tx = true;
    canard_poll(&self, 1U);
    TEST_ASSERT_EQUAL_size_t(2U, cap.count); // Called again.
    TEST_ASSERT_EQUAL_UINT8(0U, canard_pending_ifaces(&self));
    TEST_ASSERT_EQUAL_size_t(0U, self.tx.queue_size);

    canard_destroy(&self);
    mem_pool_verify_no_leaks(&pool);
}

// =====================================================================================================================
// Test runner
// =====================================================================================================================

extern "C" void setUp() {}
extern "C" void tearDown() {}

int main()
{
    seed_prng();
    UNITY_BEGIN();

    RUN_TEST(test_tx_queue_sacrifice_oldest);
    RUN_TEST(test_tx_queue_sacrifice_multiframe_reclaims_all);
    RUN_TEST(test_tx_queue_sacrifice_multiple_rounds);
    RUN_TEST(test_tx_queue_capacity_exceeded);
    RUN_TEST(test_tx_queue_capacity_boundary);
    RUN_TEST(test_tx_queue_deadline_expiration);
    RUN_TEST(test_tx_queue_ordering_priority);
    RUN_TEST(test_tx_queue_ordering_fifo_same_priority);
    RUN_TEST(test_tx_iface_bitmap_single);
    RUN_TEST(test_tx_iface_bitmap_both);
    RUN_TEST(test_tx_refcount_lifecycle);
    RUN_TEST(test_tx_scattered_gather_3_fragments);
    RUN_TEST(test_tx_oom_frame_allocation);
    RUN_TEST(test_tx_oom_transfer_allocation);
    RUN_TEST(test_tx_v0_always_classic_can);
    RUN_TEST(test_tx_backpressure_resumes);

    return UNITY_END();
}

// NOLINTEND(*-avoid-c-arrays,*-magic-numbers,*-vararg,*-use-auto,*-designated-initializers)
