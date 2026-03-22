// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal Development Team.

// Adversarial test suite for the RX admission state machine:
//   rx_session_solve_admission()  — decides whether to admit or reject a frame
//   rx_session_record_admission() — records admission state after an admitted start-of-transfer
//
// The admission formula for start-of-transfer frames is a majority vote:
//   (fresh && affine) || (affine && stale) || (stale && fresh)
// where:
//   fresh  = (transfer_id != last_admitted_transfer_id) || (priority != last_admitted_priority)
//   affine = (ses->iface_index == iface_index)
//   stale  = (ts - transfer_id_timeout) > last_admission_ts
//
// Continuation frames require an exact slot match: priority, transfer_id, iface_index, expected_toggle.

#include "canard.c" // NOLINT(bugprone-suspicious-include)
#include "helpers.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// =====================================================================================================================
// Test fixture: minimal scaffolding to call the admission functions in isolation.

typedef struct
{
    canard_t                 canard;
    canard_subscription_t    sub;
    rx_session_t             ses;
    instrumented_allocator_t alloc;
} fixture_t;

static void fixture_init(fixture_t* const f, const canard_us_t tid_timeout)
{
    memset(f, 0, sizeof(*f));
    instrumented_allocator_new(&f->alloc);
    f->canard.mem.rx_payload   = instrumented_allocator_make_resource(&f->alloc);
    f->sub.owner               = &f->canard;
    f->sub.transfer_id_timeout = tid_timeout;
    f->sub.extent              = 64;
    f->sub.kind                = canard_kind_1v1_message;
    f->sub.crc_seed            = CRC_INITIAL;
    f->ses.owner               = &f->sub;
    f->ses.last_admission_ts   = BIG_BANG;
    // All slots are NULL from memset; last_admitted_transfer_id=0, last_admitted_priority=0, iface_index=0.
}

static void fixture_free_slots(fixture_t* const f)
{
    FOREACH_PRIO (i) {
        if (f->ses.slots[i] != NULL) {
            rx_slot_destroy(&f->sub, f->ses.slots[i]);
            f->ses.slots[i] = NULL;
        }
    }
    TEST_ASSERT_EQUAL_size_t(0, f->alloc.allocated_fragments);
}

// Shorthand: call the solver for a start-of-transfer frame.
static bool solve_start(const fixture_t* const f,
                        const canard_us_t      ts,
                        const canard_prio_t    priority,
                        const byte_t           transfer_id,
                        const byte_t           iface_index)
{
    return rx_session_solve_admission(&f->ses, ts, priority, true, true, transfer_id, iface_index);
}

// Shorthand: call the solver for a continuation frame.
static bool solve_cont(const fixture_t* const f,
                       const canard_us_t      ts,
                       const canard_prio_t    priority,
                       const bool             toggle,
                       const byte_t           transfer_id,
                       const byte_t           iface_index)
{
    return rx_session_solve_admission(&f->ses, ts, priority, false, toggle, transfer_id, iface_index);
}

// Shorthand: record admission into the session.
static void record(fixture_t* const    f,
                   const canard_prio_t priority,
                   const byte_t        transfer_id,
                   const canard_us_t   ts,
                   const byte_t        iface_index)
{
    rx_session_record_admission(&f->ses, priority, transfer_id, ts, iface_index);
}

// =====================================================================================================================
// Group 1: Start-frame truth table — exhaustive {fresh, affine, stale} combinations.
// The majority-vote formula admits when at least 2 of 3 conditions are true.
//
// Baseline session state: last_tid=5, last_prio=nominal(4), iface=0, last_ts=1000000, timeout=2s.
//   fresh  = (tid != 5) || (prio != 4)
//   affine = (iface == 0)
//   stale  = (ts - 2000000) > 1000000   i.e.  ts > 3000000
static void test_start_truth_table(void)
{
    fixture_t f;
    fixture_init(&f, 2 * MEGA);
    f.ses.last_admitted_transfer_id = 5;
    f.ses.last_admitted_priority    = (byte_t)canard_prio_nominal;
    f.ses.iface_index               = 0;
    f.ses.last_admission_ts         = 1 * MEGA;

    // Row 0: F,F,F → reject (same tid+prio, wrong iface, not timed out)
    TEST_ASSERT_FALSE(solve_start(&f, 2 * MEGA, canard_prio_nominal, 5, 1));
    // Row 1: F,F,T → reject (same tid+prio, wrong iface, timed out)
    TEST_ASSERT_FALSE(solve_start(&f, 4 * MEGA, canard_prio_nominal, 5, 1));
    // Row 2: F,T,F → reject (same tid+prio, same iface, not timed out — duplicate)
    TEST_ASSERT_FALSE(solve_start(&f, 2 * MEGA, canard_prio_nominal, 5, 0));
    // Row 3: F,T,T → admit (same tid+prio, same iface, timed out)
    TEST_ASSERT_TRUE(solve_start(&f, 4 * MEGA, canard_prio_nominal, 5, 0));
    // Row 4: T,F,F → reject (new tid, wrong iface, not timed out)
    TEST_ASSERT_FALSE(solve_start(&f, 2 * MEGA, canard_prio_nominal, 6, 1));
    // Row 5: T,F,T → admit (new tid, wrong iface, timed out)
    TEST_ASSERT_TRUE(solve_start(&f, 4 * MEGA, canard_prio_nominal, 6, 1));
    // Row 6: T,T,F → admit (new tid, same iface, not timed out)
    TEST_ASSERT_TRUE(solve_start(&f, 2 * MEGA, canard_prio_nominal, 6, 0));
    // Row 7: T,T,T → admit (new tid, same iface, timed out)
    TEST_ASSERT_TRUE(solve_start(&f, 4 * MEGA, canard_prio_nominal, 6, 0));

    fixture_free_slots(&f);
}

// =====================================================================================================================
// Group 2: Fresh condition variants — what makes a frame "fresh".
static void test_fresh_variants(void)
{
    fixture_t f;
    fixture_init(&f, 2 * MEGA);
    f.ses.last_admitted_transfer_id = 10;
    f.ses.last_admitted_priority    = (byte_t)canard_prio_high;
    f.ses.iface_index               = 0;
    f.ses.last_admission_ts         = 1 * MEGA;

    // Fresh via TID only: same priority, different TID.
    TEST_ASSERT_TRUE(solve_start(&f, 2 * MEGA, canard_prio_high, 11, 0)); // fresh && affine
    // Fresh via priority only: same TID, different priority.
    TEST_ASSERT_TRUE(solve_start(&f, 2 * MEGA, canard_prio_low, 10, 0)); // fresh && affine
    // Fresh via both: different TID AND different priority.
    TEST_ASSERT_TRUE(solve_start(&f, 2 * MEGA, canard_prio_low, 11, 0)); // fresh && affine
    // Not fresh: both same.
    TEST_ASSERT_FALSE(solve_start(&f, 2 * MEGA, canard_prio_high, 10, 0)); // !fresh, affine, !stale → reject

    // TID boundary: last=31, incoming=0 → fresh (31 != 0, simple inequality, no modular arithmetic).
    f.ses.last_admitted_transfer_id = 31;
    TEST_ASSERT_TRUE(solve_start(&f, 2 * MEGA, canard_prio_high, 0, 0));
    // TID boundary: last=0, incoming=31 → fresh.
    f.ses.last_admitted_transfer_id = 0;
    TEST_ASSERT_TRUE(solve_start(&f, 2 * MEGA, canard_prio_high, 31, 0));

    fixture_free_slots(&f);
}

// =====================================================================================================================
// Group 3: Stale boundary — strict inequality and zero timeout edge cases.
static void test_stale_boundary(void)
{
    fixture_t f;
    fixture_init(&f, 2 * MEGA);
    f.ses.last_admitted_transfer_id = 5;
    f.ses.last_admitted_priority    = (byte_t)canard_prio_nominal;
    f.ses.iface_index               = 0;
    f.ses.last_admission_ts         = 1 * MEGA;

    // Exact boundary: ts - timeout == last_ts → NOT stale (strict >).
    // ts = 1000000 + 2000000 = 3000000. stale = (3000000 - 2000000) > 1000000 = 1000000 > 1000000 = false.
    TEST_ASSERT_FALSE(solve_start(&f, 3 * MEGA, canard_prio_nominal, 5, 0)); // !fresh, affine, !stale → reject

    // One tick past: ts = 3000001. stale = (3000001 - 2000000) > 1000000 = 1000001 > 1000000 = true.
    TEST_ASSERT_TRUE(solve_start(&f, (3 * MEGA) + 1, canard_prio_nominal, 5, 0)); // !fresh, affine, stale → admit

    // Zero timeout: ts == last_ts → not stale.
    f.sub.transfer_id_timeout = 0;
    // stale = (1000000 - 0) > 1000000 = false.
    TEST_ASSERT_FALSE(solve_start(&f, 1 * MEGA, canard_prio_nominal, 5, 0)); // !fresh, affine, !stale → reject

    // Zero timeout: ts == last_ts + 1 → stale.
    // stale = (1000001 - 0) > 1000000 = true.
    TEST_ASSERT_TRUE(solve_start(&f, (1 * MEGA) + 1, canard_prio_nominal, 5, 0)); // !fresh, affine, stale → admit

    fixture_free_slots(&f);
}

// =====================================================================================================================
// Group 4: Continuation frames — exact slot match required, timeout irrelevant.
static void test_continuation_frames(void)
{
    fixture_t f;
    fixture_init(&f, 2 * MEGA);
    f.ses.iface_index       = 0;
    f.ses.last_admission_ts = 1 * MEGA;

    // No slot at requested priority → reject.
    TEST_ASSERT_FALSE(solve_cont(&f, 2 * MEGA, canard_prio_nominal, true, 5, 0));

    // Create a slot at prio=nominal with tid=5, iface=0, toggle starts at 1 (v1 protocol).
    f.ses.slots[canard_prio_nominal] = rx_slot_new(&f.sub, 1 * MEGA, 5, 0);
    TEST_ASSERT_NOT_NULL(f.ses.slots[canard_prio_nominal]);
    // rx_slot_new sets expected_toggle = kind_is_v1(sub.kind) ? 1 : 0 = 1 for v1.1.

    // All fields match → admit.
    TEST_ASSERT_TRUE(solve_cont(&f, 2 * MEGA, canard_prio_nominal, true, 5, 0));

    // TID mismatch → reject.
    TEST_ASSERT_FALSE(solve_cont(&f, 2 * MEGA, canard_prio_nominal, true, 6, 0));

    // Iface mismatch → reject.
    TEST_ASSERT_FALSE(solve_cont(&f, 2 * MEGA, canard_prio_nominal, true, 5, 1));

    // Toggle mismatch → reject.
    TEST_ASSERT_FALSE(solve_cont(&f, 2 * MEGA, canard_prio_nominal, false, 5, 0));

    // Slot at different priority: slot exists at prio=nominal, query at prio=high (no slot there) → reject.
    TEST_ASSERT_FALSE(solve_cont(&f, 2 * MEGA, canard_prio_high, true, 5, 0));

    // Continuation ignores timeout: even after session timeout, an exact slot match is admitted.
    // This is critical for preempted transfers that take a long time to complete.
    TEST_ASSERT_TRUE(solve_cont(&f, 100 * MEGA, canard_prio_nominal, true, 5, 0));

    fixture_free_slots(&f);
}

// =====================================================================================================================
// Group 5: First frame to new session — BIG_BANG initial state.
static void test_first_frame_big_bang(void)
{
    fixture_t f;
    fixture_init(&f, 2 * MEGA);
    // Session is freshly initialized: last_ts=BIG_BANG, last_tid=0, last_prio=0, iface=0.

    // TID=5, prio=nominal, iface=0: fresh=true (5!=0), affine=true → admit.
    TEST_ASSERT_TRUE(solve_start(&f, 1 * MEGA, canard_prio_nominal, 5, 0));

    // TID=0, prio=exceptional(0), iface=0: matches zeroed state → fresh=false.
    // But affine=true and stale=true (BIG_BANG is ancient) → affine && stale → admit.
    TEST_ASSERT_TRUE(solve_start(&f, 1 * MEGA, canard_prio_exceptional, 0, 0));

    // TID=0, prio=exceptional(0), iface=1: fresh=false, affine=false, stale=true → only 1 of 3 → reject.
    TEST_ASSERT_FALSE(solve_start(&f, 1 * MEGA, canard_prio_exceptional, 0, 1));

    // Different iface but different TID: fresh=true, affine=false, stale=true → 2 of 3 → admit.
    TEST_ASSERT_TRUE(solve_start(&f, 1 * MEGA, canard_prio_nominal, 1, 1));

    fixture_free_slots(&f);
}

// =====================================================================================================================
// Group 6a: Preemption — TID rollover under priority preemption (the documented edge case).
// This verifies that priority tracking prevents false duplicate rejection when a high-priority
// transfer's TID wraps around to the same value as a low-priority transfer's TID.
static void test_preemption_tid_rollover(void)
{
    fixture_t f;
    fixture_init(&f, 2 * MEGA);
    f.ses.iface_index = 0;

    canard_us_t ts = 1 * MEGA;

    // Step 1: Admit low-prio TID=0.
    TEST_ASSERT_TRUE(solve_start(&f, ts, canard_prio_slow, 0, 0)); // fresh (0!=0? no, but prio=slow!=0 → fresh)
    record(&f, canard_prio_slow, 0, ts, 0);
    TEST_ASSERT_EQUAL_UINT8(0, f.ses.last_admitted_transfer_id);
    TEST_ASSERT_EQUAL_UINT8((byte_t)canard_prio_slow, f.ses.last_admitted_priority);

    // Step 2: High-prio TID=1 preempts. fresh because TID differs.
    ts += MEGA / 10;
    TEST_ASSERT_TRUE(solve_start(&f, ts, canard_prio_immediate, 1, 0));
    record(&f, canard_prio_immediate, 1, ts, 0);

    // Step 3: High-prio TID=2.
    ts += MEGA / 10;
    TEST_ASSERT_TRUE(solve_start(&f, ts, canard_prio_immediate, 2, 0));
    record(&f, canard_prio_immediate, 2, ts, 0);

    // Step 4: High-prio TID=0 (wraps around to same value as the original low-prio transfer).
    ts += MEGA / 10;
    TEST_ASSERT_TRUE(solve_start(&f, ts, canard_prio_immediate, 0, 0)); // fresh because 0!=2
    record(&f, canard_prio_immediate, 0, ts, 0);
    // Now: last_tid=0, last_prio=immediate.

    // Step 5: Late low-prio TID=0 arrives. TID matches (0==0) but priority differs (slow!=immediate) → fresh!
    ts += MEGA / 10;
    TEST_ASSERT_TRUE(solve_start(&f, ts, canard_prio_slow, 0, 0)); // fresh(prio differs) && affine → admit

    fixture_free_slots(&f);
}

// =====================================================================================================================
// Group 6b: Deep nesting — all 8 priority levels with the same TID.
// Each admission is fresh because the priority changes.
static void test_preemption_deep_nesting(void)
{
    fixture_t f;
    fixture_init(&f, 2 * MEGA);
    f.ses.iface_index = 0;

    canard_us_t ts = 1 * MEGA;

    // Admit at each priority level 0..7 with TID=10. Each is fresh because priority changes.
    for (byte_t prio = 0; prio < CANARD_PRIO_COUNT; prio++) {
        TEST_ASSERT_TRUE(solve_start(&f, ts, (canard_prio_t)prio, 10, 0));
        record(&f, (canard_prio_t)prio, 10, ts, 0);
        ts += MEGA / 10;
    }
    // Verify final state.
    TEST_ASSERT_EQUAL_UINT8(10, f.ses.last_admitted_transfer_id);
    TEST_ASSERT_EQUAL_UINT8(7, f.ses.last_admitted_priority); // canard_prio_optional

    // Now try prio=0 again with same TID=10: fresh because prio=0 != 7.
    TEST_ASSERT_TRUE(solve_start(&f, ts, canard_prio_exceptional, 10, 0));

    fixture_free_slots(&f);
}

// =====================================================================================================================
// Group 6c: Same TID returns on original priority after preemption.
static void test_preemption_return_to_original_priority(void)
{
    fixture_t f;
    fixture_init(&f, 2 * MEGA);
    f.ses.iface_index = 0;

    canard_us_t ts = 1 * MEGA;

    // Admit prio=nominal TID=5.
    TEST_ASSERT_TRUE(solve_start(&f, ts, canard_prio_nominal, 5, 0));
    record(&f, canard_prio_nominal, 5, ts, 0);

    // Preempt: admit prio=immediate TID=6.
    ts += MEGA / 10;
    TEST_ASSERT_TRUE(solve_start(&f, ts, canard_prio_immediate, 6, 0));
    record(&f, canard_prio_immediate, 6, ts, 0);
    // State: last_tid=6, last_prio=immediate.

    // Return: prio=nominal TID=6 arrives. TID matches (6==6) but prio differs (nominal!=immediate) → fresh.
    ts += MEGA / 10;
    TEST_ASSERT_TRUE(solve_start(&f, ts, canard_prio_nominal, 6, 0));

    fixture_free_slots(&f);
}

// =====================================================================================================================
// Group 7: record_admission — masking and overwrite behavior.
static void test_record_admission(void)
{
    fixture_t f;
    fixture_init(&f, 2 * MEGA);

    // Basic recording: verify all four fields.
    record(&f, canard_prio_high, 17, 5 * MEGA, 0);
    TEST_ASSERT_EQUAL_INT64(5 * MEGA, f.ses.last_admission_ts);
    TEST_ASSERT_EQUAL_UINT8(17, f.ses.last_admitted_transfer_id);
    TEST_ASSERT_EQUAL_UINT8((byte_t)canard_prio_high, f.ses.last_admitted_priority);
    TEST_ASSERT_EQUAL_UINT8(0, f.ses.iface_index);

    // Overwrite: second call replaces all fields.
    record(&f, canard_prio_optional, 31, 10 * MEGA, 1);
    TEST_ASSERT_EQUAL_INT64(10 * MEGA, f.ses.last_admission_ts);
    TEST_ASSERT_EQUAL_UINT8(31, f.ses.last_admitted_transfer_id);
    TEST_ASSERT_EQUAL_UINT8((byte_t)canard_prio_optional, f.ses.last_admitted_priority);
    TEST_ASSERT_EQUAL_UINT8(1, f.ses.iface_index);

    // Masking: values exceeding field widths are truncated.
    // Call the underlying function directly to pass out-of-range values without enum cast warnings.
    rx_session_record_admission(&f.ses, (canard_prio_t)7, 0xFF, 99 * MEGA, 0xFF);
    TEST_ASSERT_EQUAL_INT64(99 * MEGA, f.ses.last_admission_ts);
    TEST_ASSERT_EQUAL_UINT8(31, f.ses.last_admitted_transfer_id); // 0xFF & 0x1F = 31
    TEST_ASSERT_EQUAL_UINT8(7, f.ses.last_admitted_priority);     // max valid priority
    TEST_ASSERT_EQUAL_UINT8(1, f.ses.iface_index);                // 0xFF & 0x01 = 1

    fixture_free_slots(&f);
}

// =====================================================================================================================
// Group 8a: Normal TID progression on a single interface (0→1→2→...→31→0 rollover).
static void test_integration_tid_progression(void)
{
    fixture_t f;
    fixture_init(&f, 2 * MEGA);
    f.ses.iface_index = 0;

    canard_us_t ts = 1 * MEGA;

    // First frame: TID=0 on fresh session. fresh=false (0==0, prio_exceptional==0), but stale(BIG_BANG) → admit.
    TEST_ASSERT_TRUE(solve_start(&f, ts, canard_prio_nominal, 0, 0));
    record(&f, canard_prio_nominal, 0, ts, 0);

    // Progress through TID 1..31 and back to 0. Each is fresh because TID changes.
    for (byte_t tid = 1; tid <= 31; tid++) {
        ts += MEGA / 10;
        TEST_ASSERT_TRUE(solve_start(&f, ts, canard_prio_nominal, tid, 0));
        record(&f, canard_prio_nominal, tid, ts, 0);
    }

    // Rollover: TID 31→0.
    ts += MEGA / 10;
    TEST_ASSERT_TRUE(solve_start(&f, ts, canard_prio_nominal, 0, 0)); // fresh: 0 != 31
    record(&f, canard_prio_nominal, 0, ts, 0);

    fixture_free_slots(&f);
}

// =====================================================================================================================
// Group 8b: Duplicate rejection and timeout recovery.
static void test_integration_duplicate_rejection(void)
{
    fixture_t f;
    fixture_init(&f, 2 * MEGA);
    f.ses.iface_index = 0;

    const canard_us_t ts0 = 1 * MEGA;

    // Admit TID=5.
    TEST_ASSERT_TRUE(solve_start(&f, ts0, canard_prio_nominal, 5, 0));
    record(&f, canard_prio_nominal, 5, ts0, 0);

    // Immediate duplicate: same timestamp, same everything → reject.
    TEST_ASSERT_FALSE(solve_start(&f, ts0, canard_prio_nominal, 5, 0));

    // Still within timeout: reject.
    TEST_ASSERT_FALSE(solve_start(&f, ts0 + MEGA, canard_prio_nominal, 5, 0));

    // At exact timeout boundary: ts - timeout == last_ts → not stale → reject.
    TEST_ASSERT_FALSE(solve_start(&f, ts0 + (2 * MEGA), canard_prio_nominal, 5, 0));

    // One tick past timeout: stale → affine && stale → admit.
    TEST_ASSERT_TRUE(solve_start(&f, ts0 + (2 * MEGA) + 1, canard_prio_nominal, 5, 0));

    fixture_free_slots(&f);
}

// =====================================================================================================================
// Group 8c: Interface failover with TID collision — the stale&&!fresh&&!affine corner case.
static void test_integration_iface_failover_tid_collision(void)
{
    fixture_t f;
    fixture_init(&f, 2 * MEGA);
    f.ses.iface_index = 0;

    const canard_us_t ts0 = 1 * MEGA;

    // Admit TID=5 prio=high on iface=0.
    TEST_ASSERT_TRUE(solve_start(&f, ts0, canard_prio_high, 5, 0));
    record(&f, canard_prio_high, 5, ts0, 0);

    // After timeout, iface=1 sends TID=5 prio=high.
    // stale=true, fresh=false (5==5, high==high), affine=false (1!=0). Only 1 of 3 → reject.
    TEST_ASSERT_FALSE(solve_start(&f, ts0 + (3 * MEGA), canard_prio_high, 5, 1));

    // Recovery: iface=1 sends TID=6 prio=high.
    // stale=true, fresh=true (6!=5), affine=false. 2 of 3 → admit.
    TEST_ASSERT_TRUE(solve_start(&f, ts0 + (3 * MEGA), canard_prio_high, 6, 1));

    // Alternative recovery: iface=1 sends TID=5 but different priority.
    // Reset state first.
    record(&f, canard_prio_high, 5, ts0, 0);
    f.ses.iface_index = 0;
    // stale=true, fresh=true (prio_low!=prio_high), affine=false. 2 of 3 → admit.
    TEST_ASSERT_TRUE(solve_start(&f, ts0 + (3 * MEGA), canard_prio_low, 5, 1));

    fixture_free_slots(&f);
}

// =====================================================================================================================
// Group 8d: Zero timeout mode — accept all distinct transfers, reject same-timestamp duplicates.
static void test_integration_zero_timeout(void)
{
    fixture_t f;
    fixture_init(&f, 0); // Zero timeout.
    f.ses.iface_index = 0;

    const canard_us_t ts0 = 1 * MEGA;

    // Admit TID=5.
    TEST_ASSERT_TRUE(solve_start(&f, ts0, canard_prio_nominal, 5, 0));
    record(&f, canard_prio_nominal, 5, ts0, 0);

    // Same TID one tick later: stale(ts0+1 - 0 > ts0 = true), affine → admit (duplicates tolerated).
    TEST_ASSERT_TRUE(solve_start(&f, ts0 + 1, canard_prio_nominal, 5, 0));
    record(&f, canard_prio_nominal, 5, ts0 + 1, 0);

    // Same TID same timestamp: stale(ts0+1 - 0 > ts0+1 = false), fresh=false → reject (exact duplicate).
    TEST_ASSERT_FALSE(solve_start(&f, ts0 + 1, canard_prio_nominal, 5, 0));

    // Different iface, same TID, same timestamp: stale=false, fresh=false, affine=false → reject.
    TEST_ASSERT_FALSE(solve_start(&f, ts0 + 1, canard_prio_nominal, 5, 1));

    // Different iface, different TID, later timestamp:
    // stale=true, fresh=true, affine=false → 2 of 3 → admit.
    TEST_ASSERT_TRUE(solve_start(&f, ts0 + 2, canard_prio_nominal, 6, 1));

    fixture_free_slots(&f);
}

// =====================================================================================================================
// Group 8e: Documented limitation — duplicate-after-preemption.
// If a low-priority SOT frame is duplicated (CAN ACK glitch) and a higher-priority SOT is admitted
// between the original and its duplicate, the duplicate is falsely accepted as a new transfer
// because the priority change makes it "fresh". This is acknowledged as undecidable.
static void test_limitation_duplicate_after_preemption(void)
{
    fixture_t f;
    fixture_init(&f, 2 * MEGA);
    f.ses.iface_index = 0;

    canard_us_t ts = 1 * MEGA;

    // Original low-prio TID=1 admitted.
    TEST_ASSERT_TRUE(solve_start(&f, ts, canard_prio_slow, 1, 0));
    record(&f, canard_prio_slow, 1, ts, 0);

    // High-prio TID=2 preempts.
    ts += 1;
    TEST_ASSERT_TRUE(solve_start(&f, ts, canard_prio_immediate, 2, 0));
    record(&f, canard_prio_immediate, 2, ts, 0);
    // State: last_tid=2, last_prio=immediate.

    // Duplicate of the original low-prio TID=1 arrives (CAN ACK glitch).
    // fresh = (1!=2 → true). This is a false positive: it's actually a duplicate.
    // The design accepts this risk. Verify the behavior is as documented.
    ts += 1;
    TEST_ASSERT_TRUE(solve_start(&f, ts, canard_prio_slow, 1, 0)); // Known false admission.

    fixture_free_slots(&f);
}

// =====================================================================================================================

int main(void)
{
    seed_prng();
    UNITY_BEGIN();

    RUN_TEST(test_start_truth_table);
    RUN_TEST(test_fresh_variants);
    RUN_TEST(test_stale_boundary);
    RUN_TEST(test_continuation_frames);
    RUN_TEST(test_first_frame_big_bang);
    RUN_TEST(test_preemption_tid_rollover);
    RUN_TEST(test_preemption_deep_nesting);
    RUN_TEST(test_preemption_return_to_original_priority);
    RUN_TEST(test_record_admission);
    RUN_TEST(test_integration_tid_progression);
    RUN_TEST(test_integration_duplicate_rejection);
    RUN_TEST(test_integration_iface_failover_tid_collision);
    RUN_TEST(test_integration_zero_timeout);
    RUN_TEST(test_limitation_duplicate_after_preemption);

    return UNITY_END();
}
