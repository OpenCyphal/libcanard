// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal Development Team.

#include "canard.c" // NOLINT(bugprone-suspicious-include)
#include "helpers.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

static canard_filter_t make_filter(const canard_kind_t kind, const uint16_t port_id, const byte_t node_id)
{
    canard_t self;
    memset(&self, 0, sizeof(self));
    self.node_id = node_id;
    return rx_filter_for_subscription(&self, kind, port_id);
}

static bool filter_accepts(const canard_filter_t filter, const uint32_t can_id)
{
    return (can_id & filter.extended_mask) == filter.extended_can_id;
}

// =====================================================================================================================
// Group 1: rx_filter_for_subscription() golden vectors

static void test_rx_filter_for_subscription_golden_vectors(void)
{
    // v1.1 message: subject=0xABCD
    {
        const canard_filter_t f = make_filter(canard_kind_message_16b, 0xABCDU, 42U);
        TEST_ASSERT_EQUAL_HEX32(0x00ABCD80UL, f.extended_can_id);
        TEST_ASSERT_EQUAL_HEX32(0x03FFFF80UL, f.extended_mask);
    }

    // v1.0 message: subject=0x1ABC
    {
        const canard_filter_t f = make_filter(canard_kind_message_13b, 0x1ABCU, 42U);
        TEST_ASSERT_EQUAL_HEX32(0x001ABC00UL, f.extended_can_id);
        TEST_ASSERT_EQUAL_HEX32(0x029FFF80UL, f.extended_mask);
    }

    // v1.0 request: service=0x1A5, dst node=42
    {
        const canard_filter_t f = make_filter(canard_kind_request, 0x1A5U, 42U);
        TEST_ASSERT_EQUAL_HEX32(0x03695500UL, f.extended_can_id);
        TEST_ASSERT_EQUAL_HEX32(0x03FFFF80UL, f.extended_mask);
    }

    // v1.0 response: service=0x1A5, dst node=42
    {
        const canard_filter_t f = make_filter(canard_kind_response, 0x1A5U, 42U);
        TEST_ASSERT_EQUAL_HEX32(0x02695500UL, f.extended_can_id);
        TEST_ASSERT_EQUAL_HEX32(0x03FFFF80UL, f.extended_mask);
    }

    // v0.1 message: data type ID=0xABCD
    {
        const canard_filter_t f = make_filter(canard_kind_v0_message, 0xABCDU, 42U);
        TEST_ASSERT_EQUAL_HEX32(0x00ABCD00UL, f.extended_can_id);
        TEST_ASSERT_EQUAL_HEX32(0x00FFFF80UL, f.extended_mask);
    }

    // v0.1 request: data type ID=0x5A, dst node=42
    {
        const canard_filter_t f = make_filter(canard_kind_v0_request, 0x5AU, 42U);
        TEST_ASSERT_EQUAL_HEX32(0x005AAA80UL, f.extended_can_id);
        TEST_ASSERT_EQUAL_HEX32(0x00FFFF80UL, f.extended_mask);
    }

    // v0.1 response: data type ID=0x5A, dst node=42
    {
        const canard_filter_t f = make_filter(canard_kind_v0_response, 0x5AU, 42U);
        TEST_ASSERT_EQUAL_HEX32(0x005A2A80UL, f.extended_can_id);
        TEST_ASSERT_EQUAL_HEX32(0x00FFFF80UL, f.extended_mask);
    }
}

// =====================================================================================================================
// Group 2: rx_filter_for_subscription() acceptance behavior

static void test_rx_filter_for_subscription_v1_1_message_semantics(void)
{
    const canard_filter_t f = make_filter(canard_kind_message_16b, 0x8001U, 55U);
    TEST_ASSERT_TRUE(filter_accepts(f, 0x008001AAUL));  // same subject, prio=0, src=42
    TEST_ASSERT_TRUE(filter_accepts(f, 0x1C8001FFUL));  // same subject, prio=7, src=127
    TEST_ASSERT_FALSE(filter_accepts(f, 0x028001AAUL)); // service bit (25) must be zero
    TEST_ASSERT_FALSE(filter_accepts(f, 0x018001AAUL)); // bit 24 must be zero for v1.1
    TEST_ASSERT_FALSE(filter_accepts(f, 0x0080012AUL)); // message selector bit (7) must be one
    TEST_ASSERT_FALSE(filter_accepts(f, 0x008002AAUL)); // subject mismatch
}

static void test_rx_filter_for_subscription_v1_0_message_semantics(void)
{
    const canard_filter_t f = make_filter(canard_kind_message_13b, 42U, 55U);
    TEST_ASSERT_TRUE(filter_accepts(f, 0x00002A01UL));  // base form
    TEST_ASSERT_TRUE(filter_accepts(f, 0x00602A7FUL));  // reserved bits 22:21 set
    TEST_ASSERT_TRUE(filter_accepts(f, 0x01602A55UL));  // anonymous marker bit 24 set
    TEST_ASSERT_FALSE(filter_accepts(f, 0x00802A01UL)); // reserved bit 23 must be zero
    TEST_ASSERT_FALSE(filter_accepts(f, 0x02002A01UL)); // service bit (25) must be zero
    TEST_ASSERT_FALSE(filter_accepts(f, 0x00002B01UL)); // subject mismatch
    TEST_ASSERT_FALSE(filter_accepts(f, 0x00002A81UL)); // message selector bit (7) must be zero
}

static void test_rx_filter_for_subscription_v1_0_request_semantics(void)
{
    const canard_filter_t f = make_filter(canard_kind_request, 0x1A5U, 42U);
    TEST_ASSERT_TRUE(filter_accepts(f, 0x0369550BUL));  // base form
    TEST_ASSERT_TRUE(filter_accepts(f, 0x1B69557FUL));  // different prio and src
    TEST_ASSERT_FALSE(filter_accepts(f, 0x0269550BUL)); // response bit (24=0)
    TEST_ASSERT_FALSE(filter_accepts(f, 0x03E9550BUL)); // reserved bit 23 set
    TEST_ASSERT_FALSE(filter_accepts(f, 0x0369950BUL)); // service mismatch
    TEST_ASSERT_FALSE(filter_accepts(f, 0x0369558BUL)); // destination mismatch
}

static void test_rx_filter_for_subscription_v1_0_response_semantics(void)
{
    const canard_filter_t f = make_filter(canard_kind_response, 0x1A5U, 42U);
    TEST_ASSERT_TRUE(filter_accepts(f, 0x02695511UL));  // base form
    TEST_ASSERT_TRUE(filter_accepts(f, 0x1A69557FUL));  // different prio and src
    TEST_ASSERT_FALSE(filter_accepts(f, 0x03695511UL)); // request bit (24=1)
    TEST_ASSERT_FALSE(filter_accepts(f, 0x02E95511UL)); // reserved bit 23 set
    TEST_ASSERT_FALSE(filter_accepts(f, 0x02699511UL)); // service mismatch
    TEST_ASSERT_FALSE(filter_accepts(f, 0x02695591UL)); // destination mismatch
}

static void test_rx_filter_for_subscription_v0_message_semantics(void)
{
    const canard_filter_t f = make_filter(canard_kind_v0_message, 0x1234U, 55U);
    TEST_ASSERT_TRUE(filter_accepts(f, 0x00123405UL));  // base form
    TEST_ASSERT_TRUE(filter_accepts(f, 0x01123455UL));  // bit 24 set
    TEST_ASSERT_TRUE(filter_accepts(f, 0x0312347FUL));  // bits 24:25 set
    TEST_ASSERT_FALSE(filter_accepts(f, 0x00123505UL)); // data type ID mismatch
    TEST_ASSERT_FALSE(filter_accepts(f, 0x00123485UL)); // service flag bit (7) set
}

static void test_rx_filter_for_subscription_v0_request_semantics(void)
{
    const canard_filter_t f = make_filter(canard_kind_v0_request, 0x5AU, 42U);
    TEST_ASSERT_TRUE(filter_accepts(f, 0x005AAA87UL));  // base form
    TEST_ASSERT_TRUE(filter_accepts(f, 0x015AAA81UL));  // bit 24 set
    TEST_ASSERT_TRUE(filter_accepts(f, 0x035AAAFFUL));  // bits 24:25 set
    TEST_ASSERT_FALSE(filter_accepts(f, 0x005A2A87UL)); // response bit (15=0)
    TEST_ASSERT_FALSE(filter_accepts(f, 0x005AAB87UL)); // destination mismatch
    TEST_ASSERT_FALSE(filter_accepts(f, 0x005BAA87UL)); // data type ID mismatch
    TEST_ASSERT_FALSE(filter_accepts(f, 0x005AAA07UL)); // service flag bit (7) cleared
}

static void test_rx_filter_for_subscription_v0_response_semantics(void)
{
    const canard_filter_t f = make_filter(canard_kind_v0_response, 0x5AU, 42U);
    TEST_ASSERT_TRUE(filter_accepts(f, 0x005A2A81UL));  // base form
    TEST_ASSERT_TRUE(filter_accepts(f, 0x015A2A82UL));  // bit 24 set
    TEST_ASSERT_TRUE(filter_accepts(f, 0x035A2AFFUL));  // bits 24:25 set
    TEST_ASSERT_FALSE(filter_accepts(f, 0x005AAA81UL)); // request bit (15=1)
    TEST_ASSERT_FALSE(filter_accepts(f, 0x005A2B81UL)); // destination mismatch
    TEST_ASSERT_FALSE(filter_accepts(f, 0x005B2A81UL)); // data type ID mismatch
    TEST_ASSERT_FALSE(filter_accepts(f, 0x005A2A01UL)); // service flag bit (7) cleared
}

// =====================================================================================================================
// Group 3: rx_filter_fuse()

static void test_rx_filter_fuse_basic(void)
{
    const canard_filter_t req   = { .extended_can_id = 0x03695500UL, .extended_mask = 0x03FFFF80UL };
    const canard_filter_t resp  = { .extended_can_id = 0x02695500UL, .extended_mask = 0x03FFFF80UL };
    const canard_filter_t fused = rx_filter_fuse(req, resp);
    TEST_ASSERT_EQUAL_HEX32(0x02695500UL, fused.extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(0x02FFFF80UL, fused.extended_mask);
    TEST_ASSERT_TRUE(filter_accepts(fused, 0x0369552AUL));  // request
    TEST_ASSERT_TRUE(filter_accepts(fused, 0x02695555UL));  // response
    TEST_ASSERT_FALSE(filter_accepts(fused, 0x0269952AUL)); // service mismatch
}

static void test_rx_filter_fuse_is_commutative(void)
{
    const canard_filter_t a = { .extended_can_id = 0x005AAA80UL, .extended_mask = 0x00FFFF80UL };
    const canard_filter_t b = { .extended_can_id = 0x005A2A80UL, .extended_mask = 0x00FFFF80UL };
    const canard_filter_t x = rx_filter_fuse(a, b);
    const canard_filter_t y = rx_filter_fuse(b, a);
    TEST_ASSERT_EQUAL_HEX32(x.extended_can_id, y.extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(x.extended_mask, y.extended_mask);
}

// =====================================================================================================================
// Group 4: rx_filter_rank()

static void test_rx_filter_rank_vectors(void)
{
    TEST_ASSERT_EQUAL_UINT8(0U,
                            rx_filter_rank((canard_filter_t){ .extended_can_id = 0, .extended_mask = 0x00000000UL }));
    TEST_ASSERT_EQUAL_UINT8(1U,
                            rx_filter_rank((canard_filter_t){ .extended_can_id = 0, .extended_mask = 0x00000001UL }));
    TEST_ASSERT_EQUAL_UINT8(16U,
                            rx_filter_rank((canard_filter_t){ .extended_can_id = 0, .extended_mask = 0x029FFF80UL }));
    TEST_ASSERT_EQUAL_UINT8(19U,
                            rx_filter_rank((canard_filter_t){ .extended_can_id = 0, .extended_mask = 0x03FFFF80UL }));
    TEST_ASSERT_EQUAL_UINT8(29U,
                            rx_filter_rank((canard_filter_t){ .extended_can_id = 0, .extended_mask = 0x1FFFFFFFUL }));
}

// =====================================================================================================================
// Group 5: rx_filter_coalesce_into()

static void test_rx_filter_coalesce_into_selects_best_rank(void)
{
    canard_filter_t into[2] = {
        { .extended_can_id = 0x0UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x8UL, .extended_mask = 0xFUL },
    };
    const canard_filter_t new_filter   = { .extended_can_id = 0xCUL, .extended_mask = 0xFUL };
    const canard_filter_t fused_index1 = rx_filter_fuse(into[1], new_filter);
    rx_filter_coalesce_into(2U, into, new_filter);
    TEST_ASSERT_EQUAL_HEX32(0x0UL, into[0].extended_can_id); // unchanged
    TEST_ASSERT_EQUAL_HEX32(0xFUL, into[0].extended_mask);   // unchanged
    TEST_ASSERT_EQUAL_HEX32(fused_index1.extended_can_id, into[1].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(fused_index1.extended_mask, into[1].extended_mask);
}

static void test_rx_filter_coalesce_into_tie_prefers_later_index(void)
{
    canard_filter_t into[2] = {
        { .extended_can_id = 0x0UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x3UL, .extended_mask = 0xFUL },
    };
    const canard_filter_t new_filter   = { .extended_can_id = 0x5UL, .extended_mask = 0xFUL };
    const canard_filter_t fused_index1 = rx_filter_fuse(into[1], new_filter);
    rx_filter_coalesce_into(2U, into, new_filter);
    TEST_ASSERT_EQUAL_HEX32(0x0UL, into[0].extended_can_id); // tie, earlier entry must remain unchanged
    TEST_ASSERT_EQUAL_HEX32(0xFUL, into[0].extended_mask);
    TEST_ASSERT_EQUAL_HEX32(fused_index1.extended_can_id, into[1].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(fused_index1.extended_mask, into[1].extended_mask);
}

static void test_rx_filter_coalesce_into_merges_existing_pair_when_best(void)
{
    canard_filter_t into[2] = {
        { .extended_can_id = 0x0UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x1UL, .extended_mask = 0xFUL },
    };
    const canard_filter_t new_filter     = { .extended_can_id = 0xFUL, .extended_mask = 0xFUL };
    const canard_filter_t fused_existing = rx_filter_fuse(into[0], into[1]);
    rx_filter_coalesce_into(2U, into, new_filter);
    TEST_ASSERT_EQUAL_HEX32(fused_existing.extended_can_id, into[0].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(fused_existing.extended_mask, into[0].extended_mask);
    TEST_ASSERT_EQUAL_HEX32(new_filter.extended_can_id, into[1].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(new_filter.extended_mask, into[1].extended_mask);
}

static void test_rx_filter_coalesce_into_single_entry(void)
{
    canard_filter_t       into[1]       = { { .extended_can_id = 0x3UL, .extended_mask = 0xFUL } };
    const canard_filter_t new_filter    = { .extended_can_id = 0x5UL, .extended_mask = 0xFUL };
    const canard_filter_t expected_fuse = rx_filter_fuse(into[0], new_filter);
    rx_filter_coalesce_into(1U, into, new_filter);
    TEST_ASSERT_EQUAL_HEX32(expected_fuse.extended_can_id, into[0].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(expected_fuse.extended_mask, into[0].extended_mask);
}

// =====================================================================================================================
// Acceptance-invariant helpers for adversarial coalescence testing.
// The fundamental correctness property: after coalescence, every CAN ID accepted by any original filter
// or the new filter must still be accepted by at least one filter in the result array.

static void assert_coverage_invariant(const size_t           count,
                                      const canard_filter_t* original,
                                      const canard_filter_t  new_filter,
                                      const canard_filter_t* result,
                                      const unsigned         domain_bits)
{
    const uint32_t domain = UINT32_C(1) << domain_bits;
    for (uint32_t x = 0; x < domain; x++) {
        bool was_accepted = filter_accepts(new_filter, x);
        for (size_t k = 0; !was_accepted && (k < count); k++) {
            was_accepted = filter_accepts(original[k], x);
        }
        if (was_accepted) {
            bool is_accepted = false;
            for (size_t k = 0; !is_accepted && (k < count); k++) {
                is_accepted = filter_accepts(result[k], x);
            }
            TEST_ASSERT_TRUE(is_accepted);
        }
    }
}

// Copy-coalesce-check wrapper. Modifies `into` in place and verifies the invariant.
static void coalesce_and_check(const size_t          count,
                               canard_filter_t*      into,
                               const canard_filter_t new_filter,
                               const unsigned        domain_bits)
{
    canard_filter_t original[8];
    TEST_ASSERT_TRUE(count <= 8U);
    memcpy(original, into, count * sizeof(canard_filter_t));
    rx_filter_coalesce_into(count, into, new_filter);
    assert_coverage_invariant(count, original, new_filter, into, domain_bits);
}

// =====================================================================================================================
// Group 6: Exhaustive small-domain invariant tests

static void test_coalesce_coverage_count1_exhaustive(void)
{
    // Exhaustively test all 4-bit filter combinations for count=1 (65536 combos).
    for (uint32_t im = 0; im <= 0xFU; im++) {
        for (uint32_t ii = 0; ii <= 0xFU; ii++) {
            for (uint32_t nm = 0; nm <= 0xFU; nm++) {
                for (uint32_t ni = 0; ni <= 0xFU; ni++) {
                    canard_filter_t       into[1] = { { .extended_can_id = ii & im, .extended_mask = im } };
                    const canard_filter_t nf      = { .extended_can_id = ni & nm, .extended_mask = nm };
                    coalesce_and_check(1U, into, nf, 4U);
                }
            }
        }
    }
}

static void test_coalesce_coverage_count2_sampled(void)
{
    // Sample 4-bit combinations for count=2.
    static const uint32_t masks[] = { 0x0U, 0x3U, 0x5U, 0x7U, 0x9U, 0xCU, 0xFU };
    static const uint32_t ids[]   = { 0x0U, 0x3U, 0x5U, 0x7U, 0xAU, 0xCU, 0xFU };
    const size_t          nm      = sizeof(masks) / sizeof(masks[0]);
    const size_t          ni      = sizeof(ids) / sizeof(ids[0]);
    for (size_t a = 0; a < nm; a++) {
        for (size_t b = 0; b < ni; b++) {
            for (size_t c = 0; c < nm; c++) {
                for (size_t d = 0; d < ni; d++) {
                    canard_filter_t into[2] = {
                        { .extended_can_id = 0x5U & 0xFU, .extended_mask = 0xFU },
                        { .extended_can_id = ids[b] & masks[a], .extended_mask = masks[a] },
                    };
                    const canard_filter_t nf = { .extended_can_id = ids[d] & masks[c], .extended_mask = masks[c] };
                    coalesce_and_check(2U, into, nf, 4U);
                }
            }
        }
    }
}

static void test_coalesce_coverage_count3_targeted(void)
{
    // Targeted configurations for count=3 verifying the invariant.
    {
        canard_filter_t into[3] = {
            { .extended_can_id = 0x0UL, .extended_mask = 0xFUL },
            { .extended_can_id = 0x1UL, .extended_mask = 0xFUL },
            { .extended_can_id = 0x2UL, .extended_mask = 0xFUL },
        };
        coalesce_and_check(3U, into, (canard_filter_t){ .extended_can_id = 0x4UL, .extended_mask = 0xFUL }, 4U);
    }
    {
        canard_filter_t into[3] = {
            { .extended_can_id = 0x0UL, .extended_mask = 0xFUL },
            { .extended_can_id = 0x1UL, .extended_mask = 0xFUL },
            { .extended_can_id = 0x3UL, .extended_mask = 0xFUL },
        };
        coalesce_and_check(3U, into, (canard_filter_t){ .extended_can_id = 0x7UL, .extended_mask = 0xFUL }, 4U);
    }
    {
        canard_filter_t into[3] = {
            { .extended_can_id = 0x0UL, .extended_mask = 0xFUL },
            { .extended_can_id = 0x8UL, .extended_mask = 0xFUL },
            { .extended_can_id = 0xCUL, .extended_mask = 0xFUL },
        };
        coalesce_and_check(3U, into, (canard_filter_t){ .extended_can_id = 0xEUL, .extended_mask = 0xFUL }, 4U);
    }
    { // wildcard at position 0
        canard_filter_t into[3] = {
            { .extended_can_id = 0x0UL, .extended_mask = 0x0UL },
            { .extended_can_id = 0x5UL, .extended_mask = 0xFUL },
            { .extended_can_id = 0xAUL, .extended_mask = 0xFUL },
        };
        coalesce_and_check(3U, into, (canard_filter_t){ .extended_can_id = 0xFUL, .extended_mask = 0xFUL }, 4U);
    }
    { // mixed mask widths
        canard_filter_t into[3] = {
            { .extended_can_id = 0x4UL, .extended_mask = 0xCUL },
            { .extended_can_id = 0x1UL, .extended_mask = 0x3UL },
            { .extended_can_id = 0xFUL, .extended_mask = 0xFUL },
        };
        coalesce_and_check(3U, into, (canard_filter_t){ .extended_can_id = 0x8UL, .extended_mask = 0x8UL }, 4U);
    }
}

// =====================================================================================================================
// Group 7: Structural tests (count >= 3)

static void test_coalesce_count3_fuses_existing_pair(void)
{
    // into[0] and into[1] are most similar; new is dissimilar.
    // Expect: into[0]=fuse(into[0],into[1]), into[1]=new, into[2] unchanged.
    canard_filter_t into[3] = {
        { .extended_can_id = 0x0UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x1UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0xAUL, .extended_mask = 0xFUL },
    };
    const canard_filter_t new_filter     = { .extended_can_id = 0xFUL, .extended_mask = 0xFUL };
    const canard_filter_t fused_existing = rx_filter_fuse(into[0], into[1]);
    // fuse(0,1) rank=3, all other pairs rank<=2 (verified in planning).
    coalesce_and_check(3U, into, new_filter, 4U);
    TEST_ASSERT_EQUAL_HEX32(fused_existing.extended_can_id, into[0].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(fused_existing.extended_mask, into[0].extended_mask);
    TEST_ASSERT_EQUAL_HEX32(new_filter.extended_can_id, into[1].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(new_filter.extended_mask, into[1].extended_mask);
    TEST_ASSERT_EQUAL_HEX32(0xAUL, into[2].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(0xFUL, into[2].extended_mask);
}

static void test_coalesce_count3_fuses_with_new_at_last(void)
{
    // into[2] and new are most similar; best_j==count, only into[2] changes.
    canard_filter_t into[3] = {
        { .extended_can_id = 0x0UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x5UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0xCUL, .extended_mask = 0xFUL },
    };
    const canard_filter_t new_filter    = { .extended_can_id = 0xDUL, .extended_mask = 0xFUL };
    const canard_filter_t expected_fuse = rx_filter_fuse(into[2], new_filter); // rank=3, best
    coalesce_and_check(3U, into, new_filter, 4U);
    TEST_ASSERT_EQUAL_HEX32(0x0UL, into[0].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(0xFUL, into[0].extended_mask);
    TEST_ASSERT_EQUAL_HEX32(0x5UL, into[1].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(0xFUL, into[1].extended_mask);
    TEST_ASSERT_EQUAL_HEX32(expected_fuse.extended_can_id, into[2].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(expected_fuse.extended_mask, into[2].extended_mask);
}

static void test_coalesce_count3_fuses_with_new_at_first(void)
{
    // into[0] and new are strictly the best pair; best_i=0, best_j==count.
    canard_filter_t into[3] = {
        { .extended_can_id = 0xEUL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x5UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x0UL, .extended_mask = 0xFUL },
    };
    const canard_filter_t new_filter    = { .extended_can_id = 0xFUL, .extended_mask = 0xFUL };
    const canard_filter_t expected_fuse = rx_filter_fuse(into[0], new_filter); // rank=3
    // fuse(1,new)=fuse(0x5,0xF,0xF,0xF): mask=0xF & ~0xA = 0x5, r=2. So (0,count) wins.
    // But fuse(1,count) has r=2, while fuse(0,count) has r=3. Check (0,1): mask=0xF & ~(0xE^0x5)=0xF & ~0xB=0x4, r=1.
    // (0,2): mask=0xF & ~0xE=0x1, r=1. (1,2): mask=0xF & ~0x5=0xA, r=2. So best is (0,count) with r=3.
    coalesce_and_check(3U, into, new_filter, 4U);
    TEST_ASSERT_EQUAL_HEX32(expected_fuse.extended_can_id, into[0].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(expected_fuse.extended_mask, into[0].extended_mask);
    TEST_ASSERT_EQUAL_HEX32(0x5UL, into[1].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(0xFUL, into[1].extended_mask);
    TEST_ASSERT_EQUAL_HEX32(0x0UL, into[2].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(0xFUL, into[2].extended_mask);
}

static void test_coalesce_count4_middle_best_i(void)
{
    // best_i at middle position (index 2), best_j==count.
    canard_filter_t into[4] = {
        { .extended_can_id = 0x0UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x5UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x6UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0xAUL, .extended_mask = 0xFUL },
    };
    const canard_filter_t new_filter = { .extended_can_id = 0x7UL, .extended_mask = 0xFUL };
    // fuse(2,new): mask=0xF & ~0x1=0xE, r=3. fuse(1,new): mask=0xF & ~0x2=0xD, r=3.
    // Tie at r=3; (2,count) enumerated after (1,count), so (2,count) wins via >=.
    const canard_filter_t expected_fuse = rx_filter_fuse(into[2], new_filter);
    coalesce_and_check(4U, into, new_filter, 4U);
    TEST_ASSERT_EQUAL_HEX32(0x0UL, into[0].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(0x5UL, into[1].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(expected_fuse.extended_can_id, into[2].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(expected_fuse.extended_mask, into[2].extended_mask);
    TEST_ASSERT_EQUAL_HEX32(0xAUL, into[3].extended_can_id);
}

static void test_coalesce_count5(void)
{
    canard_filter_t into[5] = {
        { .extended_can_id = 0x0UL, .extended_mask = 0xFUL }, { .extended_can_id = 0x2UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x4UL, .extended_mask = 0xFUL }, { .extended_can_id = 0x8UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0xCUL, .extended_mask = 0xFUL },
    };
    const canard_filter_t new_filter = { .extended_can_id = 0xDUL, .extended_mask = 0xFUL };
    // Multiple rank-3 pairs exist; (4,count) is last enumerated tie → wins via >=.
    const canard_filter_t expected_fuse = rx_filter_fuse(into[4], new_filter);
    coalesce_and_check(5U, into, new_filter, 4U);
    TEST_ASSERT_EQUAL_HEX32(0x0UL, into[0].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(0x2UL, into[1].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(0x4UL, into[2].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(0x8UL, into[3].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(expected_fuse.extended_can_id, into[4].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(expected_fuse.extended_mask, into[4].extended_mask);
}

// =====================================================================================================================
// Group 8: Degenerate cases

static void test_coalesce_all_identical(void)
{
    canard_filter_t into[3] = {
        { .extended_can_id = 0x5UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x5UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x5UL, .extended_mask = 0xFUL },
    };
    coalesce_and_check(3U, into, (canard_filter_t){ .extended_can_id = 0x5UL, .extended_mask = 0xFUL }, 4U);
    // All identical → fuse is identity → array unchanged.
    for (size_t k = 0; k < 3; k++) {
        TEST_ASSERT_EQUAL_HEX32(0x5UL, into[k].extended_can_id);
        TEST_ASSERT_EQUAL_HEX32(0xFUL, into[k].extended_mask);
    }
}

static void test_coalesce_wildcard_existing(void)
{
    canard_filter_t into[2] = {
        { .extended_can_id = 0x0UL, .extended_mask = 0x0UL }, // wildcard
        { .extended_can_id = 0x5UL, .extended_mask = 0xFUL },
    };
    coalesce_and_check(2U, into, (canard_filter_t){ .extended_can_id = 0xAUL, .extended_mask = 0xFUL }, 4U);
}

static void test_coalesce_new_is_wildcard(void)
{
    canard_filter_t into[2] = {
        { .extended_can_id = 0x5UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0xAUL, .extended_mask = 0xFUL },
    };
    const canard_filter_t nf = { .extended_can_id = 0x0UL, .extended_mask = 0x0UL };
    coalesce_and_check(2U, into, nf, 4U);
}

static void test_coalesce_fully_specified_29bit(void)
{
    canard_filter_t into[2] = {
        { .extended_can_id = 0x1FFFFFFEUL, .extended_mask = 0x1FFFFFFFUL },
        { .extended_can_id = 0x10000000UL, .extended_mask = 0x1FFFFFFFUL },
    };
    const canard_filter_t new_filter    = { .extended_can_id = 0x1FFFFFFFUL, .extended_mask = 0x1FFFFFFFUL };
    const canard_filter_t expected_fuse = rx_filter_fuse(into[0], new_filter);
    // fuse(0,new): mask=0x1FFFFFFF & ~0x01 = 0x1FFFFFFE, r=28. Best by far.
    rx_filter_coalesce_into(2U, into, new_filter);
    TEST_ASSERT_EQUAL_HEX32(expected_fuse.extended_can_id, into[0].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(expected_fuse.extended_mask, into[0].extended_mask);
    TEST_ASSERT_EQUAL_HEX32(0x10000000UL, into[1].extended_can_id); // unchanged
    // Verify acceptance of specific CAN IDs.
    TEST_ASSERT_TRUE(filter_accepts(into[0], 0x1FFFFFFEUL));
    TEST_ASSERT_TRUE(filter_accepts(into[0], 0x1FFFFFFFUL));
    TEST_ASSERT_FALSE(filter_accepts(into[0], 0x1FFFFFFCUL));
}

static void test_coalesce_new_identical_to_existing(void)
{
    canard_filter_t into[2] = {
        { .extended_can_id = 0x3UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0xAUL, .extended_mask = 0xFUL },
    };
    const canard_filter_t new_filter = { .extended_can_id = 0xAUL, .extended_mask = 0xFUL };
    // fuse(into[1], new) is identity: rank=4 (best possible for 4-bit). Wins easily.
    coalesce_and_check(2U, into, new_filter, 4U);
    TEST_ASSERT_EQUAL_HEX32(0x3UL, into[0].extended_can_id); // unchanged
    TEST_ASSERT_EQUAL_HEX32(0xAUL, into[1].extended_can_id); // identity fuse
    TEST_ASSERT_EQUAL_HEX32(0xFUL, into[1].extended_mask);
}

static void test_coalesce_worst_case_expansion(void)
{
    // Maximally dissimilar: all ID bits differ → fuse produces wildcard.
    canard_filter_t       into[1]    = { { .extended_can_id = 0x0UL, .extended_mask = 0xFUL } };
    const canard_filter_t new_filter = { .extended_can_id = 0xFUL, .extended_mask = 0xFUL };
    coalesce_and_check(1U, into, new_filter, 4U);
    TEST_ASSERT_EQUAL_HEX32(0x0UL, into[0].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(0x0UL, into[0].extended_mask); // wildcard
    for (uint32_t x = 0; x < 16U; x++) {
        TEST_ASSERT_TRUE(filter_accepts(into[0], x));
    }
}

// =====================================================================================================================
// Group 9: Tie-breaking

static void test_coalesce_tie_prefers_later_pair_count3(void)
{
    // Four pairs tie at rank 3; the last enumerated (2,count) must win.
    canard_filter_t into[3] = {
        { .extended_can_id = 0x0UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x2UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x8UL, .extended_mask = 0xFUL },
    };
    const canard_filter_t new_filter    = { .extended_can_id = 0xAUL, .extended_mask = 0xFUL };
    const canard_filter_t expected_fuse = rx_filter_fuse(into[2], new_filter);
    // Pairs at rank 3: (0,1) mask=0xD, (0,2) mask=0x7, (1,count) mask=0x7, (2,count) mask=0xD.
    // Due to >=, (2,count) wins. best_j==count so only into[2] changes.
    coalesce_and_check(3U, into, new_filter, 4U);
    TEST_ASSERT_EQUAL_HEX32(0x0UL, into[0].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(0x2UL, into[1].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(expected_fuse.extended_can_id, into[2].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(expected_fuse.extended_mask, into[2].extended_mask);
}

// =====================================================================================================================
// Group 10: Multi-step coalescence

static void test_coalesce_sequential_invariant_4bit(void)
{
    // Simulate rx_filter_configure: fill 3 slots, then coalesce 5 more filters.
    // Track the cumulative set of required CAN IDs and verify after each step.
    canard_filter_t filters[3] = {
        { .extended_can_id = 0x1UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x3UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x5UL, .extended_mask = 0xFUL },
    };
    const canard_filter_t extras[] = {
        { .extended_can_id = 0x7UL, .extended_mask = 0xFUL }, { .extended_can_id = 0x9UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0xBUL, .extended_mask = 0xFUL }, { .extended_can_id = 0xDUL, .extended_mask = 0xFUL },
        { .extended_can_id = 0xFUL, .extended_mask = 0xFUL },
    };
    // Build the cumulative required-acceptance set.
    bool required[16] = { false };
    for (size_t k = 0; k < 3U; k++) {
        for (uint32_t x = 0; x < 16U; x++) {
            if (filter_accepts(filters[k], x)) {
                required[x] = true;
            }
        }
    }
    for (size_t step = 0; step < 5U; step++) {
        for (uint32_t x = 0; x < 16U; x++) {
            if (filter_accepts(extras[step], x)) {
                required[x] = true;
            }
        }
        rx_filter_coalesce_into(3U, filters, extras[step]);
        // Verify cumulative invariant.
        for (uint32_t x = 0; x < 16U; x++) {
            if (required[x]) {
                bool found = false;
                for (size_t k = 0; !found && (k < 3U); k++) {
                    found = filter_accepts(filters[k], x);
                }
                TEST_ASSERT_TRUE(found);
            }
        }
    }
}

static void test_coalesce_sequential_realistic_cyphal(void)
{
    // 3 filter slots, 6 subscriptions → 3 direct + 3 coalesced.
    canard_filter_t filters[3] = {
        make_filter(canard_kind_message_16b, 100U, 42U),
        make_filter(canard_kind_request, 200U, 42U),
        make_filter(canard_kind_v0_message, 300U, 42U),
    };
    const canard_filter_t extras[] = {
        make_filter(canard_kind_message_16b, 101U, 42U),
        make_filter(canard_kind_v0_request, 50U, 42U),
        make_filter(canard_kind_response, 200U, 42U),
    };
    // For each subscription, generate a representative CAN ID that must be accepted.
    // v1.1 msg subject=100: (100<<8)|0x80 | src=1 → 0x00006481
    // v1.0 req svc=200 dst=42: (1<<25)|(1<<24)|(200<<14)|(42<<7)|src=1 → 0x03321501
    // v0.1 msg dtype=300: (300<<8)|src=1 → 0x00012C01
    // v1.1 msg subject=101: (101<<8)|0x80|src=1 → 0x00006581
    // v0.1 req dtype=50 dst=42: (50<<16)|(1<<15)|(42<<8)|0x80|src=1 → 0x0032AA81
    // v1.0 resp svc=200 dst=42: (1<<25)|(200<<14)|(42<<7)|src=1 → 0x02321501
    const uint32_t must_accept[] = {
        0x00006481UL, 0x03321501UL, 0x00012C01UL, 0x00006581UL, 0x0032AA81UL, 0x02321501UL,
    };
    for (size_t step = 0; step < 3U; step++) {
        rx_filter_coalesce_into(3U, filters, extras[step]);
        // All CAN IDs introduced so far must still be accepted.
        for (size_t a = 0; a < 3U + step + 1U; a++) {
            bool found = false;
            for (size_t k = 0; !found && (k < 3U); k++) {
                found = filter_accepts(filters[k], must_accept[a]);
            }
            TEST_ASSERT_TRUE(found);
        }
    }
}

// =====================================================================================================================
// Group 11: Realistic Cyphal filters

static void test_coalesce_v1_messages_pair(void)
{
    // Two adjacent v1.1 message subjects: subjects 100 and 101.
    canard_filter_t       into[1]    = { make_filter(canard_kind_message_16b, 100U, 42U) };
    const canard_filter_t new_filter = make_filter(canard_kind_message_16b, 101U, 42U);
    rx_filter_coalesce_into(1U, into, new_filter);
    // The fused filter must accept both subjects (arbitrary src node).
    TEST_ASSERT_TRUE(filter_accepts(into[0], 0x00006481UL));  // subject 100
    TEST_ASSERT_TRUE(filter_accepts(into[0], 0x00006581UL));  // subject 101
    TEST_ASSERT_FALSE(filter_accepts(into[0], 0x00006681UL)); // subject 102 should not match
}

static void test_coalesce_v1_request_response_pair(void)
{
    // Request and response for service 0x1A5, node 42.
    canard_filter_t       into[1]    = { make_filter(canard_kind_request, 0x1A5U, 42U) };
    const canard_filter_t new_filter = make_filter(canard_kind_response, 0x1A5U, 42U);
    rx_filter_coalesce_into(1U, into, new_filter);
    TEST_ASSERT_TRUE(filter_accepts(into[0], 0x0369552AUL)); // request (src=42)
    TEST_ASSERT_TRUE(filter_accepts(into[0], 0x02695511UL)); // response (src=17)
}

static void test_coalesce_mixed_versions(void)
{
    // Cross-version coalescence: v1.1 + v1.0 + v0.
    canard_filter_t into[2] = {
        make_filter(canard_kind_message_16b, 100U, 42U),
        make_filter(canard_kind_message_13b, 42U, 55U),
    };
    const canard_filter_t new_filter = make_filter(canard_kind_v0_message, 300U, 42U);
    canard_filter_t       original[2];
    memcpy(original, into, sizeof(original));
    rx_filter_coalesce_into(2U, into, new_filter);
    // Verify specific CAN IDs from each subscription are still accepted.
    const uint32_t ids[] = { 0x00006481UL, 0x00002A01UL, 0x00012C01UL };
    for (size_t a = 0; a < 3U; a++) {
        bool found = false;
        for (size_t k = 0; !found && (k < 2U); k++) {
            found = filter_accepts(into[k], ids[a]);
        }
        TEST_ASSERT_TRUE(found);
    }
}

// =====================================================================================================================
// Group 12: Adversarial

static void test_coalesce_adversarial_bit_pattern(void)
{
    // Checkerboard patterns in 8-bit domain.
    canard_filter_t into[3] = {
        { .extended_can_id = 0x55UL, .extended_mask = 0xFFUL },
        { .extended_can_id = 0xAAUL, .extended_mask = 0xFFUL },
        { .extended_can_id = 0x33UL, .extended_mask = 0xFFUL },
    };
    const canard_filter_t new_filter = { .extended_can_id = 0xCCUL, .extended_mask = 0xFFUL };
    // fuse(0,1) rank=0, fuse(0,2) rank=4, fuse(1,new) rank=4; (1,count) is last tie → wins.
    const canard_filter_t expected_fuse = rx_filter_fuse(into[1], new_filter);
    coalesce_and_check(3U, into, new_filter, 8U);
    TEST_ASSERT_EQUAL_HEX32(0x55UL, into[0].extended_can_id); // unchanged
    TEST_ASSERT_EQUAL_HEX32(expected_fuse.extended_can_id, into[1].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(expected_fuse.extended_mask, into[1].extended_mask);
    TEST_ASSERT_EQUAL_HEX32(0x33UL, into[2].extended_can_id); // unchanged
}

static void test_coalesce_best_i_zero_best_j_one(void)
{
    // Force best pair to be (0,1) strictly: fuse(0,1) rank=3, all others <=2.
    canard_filter_t into[3] = {
        { .extended_can_id = 0x0UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x1UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x6UL, .extended_mask = 0x7UL },
    };
    const canard_filter_t new_filter = { .extended_can_id = 0x4UL, .extended_mask = 0x7UL };
    // fuse(0,1): mask=0xE, r=3. fuse(0,2): r=1. fuse(0,new): r=2. fuse(1,2): r=0.
    // fuse(1,new): r=1. fuse(2,new): r=2. Strictly best is (0,1).
    const canard_filter_t fused01 = rx_filter_fuse(into[0], into[1]);
    coalesce_and_check(3U, into, new_filter, 4U);
    TEST_ASSERT_EQUAL_HEX32(fused01.extended_can_id, into[0].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(fused01.extended_mask, into[0].extended_mask);
    TEST_ASSERT_EQUAL_HEX32(new_filter.extended_can_id, into[1].extended_can_id);
    TEST_ASSERT_EQUAL_HEX32(new_filter.extended_mask, into[1].extended_mask);
    TEST_ASSERT_EQUAL_HEX32(0x6UL, into[2].extended_can_id); // unchanged
}

static void test_coalesce_greedy_multistep_degradation(void)
{
    // Multi-step coalescence where greedy choices accumulate: verify the invariant still holds.
    canard_filter_t into[2] = {
        { .extended_can_id = 0x0UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x1UL, .extended_mask = 0xFUL },
    };
    const canard_filter_t extras[] = {
        { .extended_can_id = 0x2UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x4UL, .extended_mask = 0xFUL },
        { .extended_can_id = 0x8UL, .extended_mask = 0xFUL },
    };
    bool required[16] = { false };
    for (uint32_t x = 0; x < 16U; x++) {
        if (filter_accepts(into[0], x) || filter_accepts(into[1], x)) {
            required[x] = true;
        }
    }
    for (size_t step = 0; step < 3U; step++) {
        for (uint32_t x = 0; x < 16U; x++) {
            if (filter_accepts(extras[step], x)) {
                required[x] = true;
            }
        }
        rx_filter_coalesce_into(2U, into, extras[step]);
        for (uint32_t x = 0; x < 16U; x++) {
            if (required[x]) {
                TEST_ASSERT_TRUE(filter_accepts(into[0], x) || filter_accepts(into[1], x));
            }
        }
    }
}

// =====================================================================================================================
// rx_filter_configure() integration

static void* oom_alloc(const canard_mem_t mem, const size_t size)
{
    (void)mem;
    (void)size;
    return NULL;
}
static void oom_free(const canard_mem_t mem, const size_t size, void* const pointer)
{
    (void)mem;
    (void)size;
    (void)pointer;
}
static const canard_mem_vtable_t oom_mem_vtable = { .free = oom_free, .alloc = oom_alloc };

static void* real_alloc(const canard_mem_t mem, const size_t size)
{
    (void)mem;
    return malloc(size);
}
static void real_free(const canard_mem_t mem, const size_t size, void* const pointer)
{
    (void)mem;
    (void)size;
    free(pointer);
}
static const canard_mem_vtable_t real_mem_vtable = { .free = real_free, .alloc = real_alloc };

static bool test_filter_cb(canard_t* const self, const size_t filter_count, const canard_filter_t* filters)
{
    (void)self;
    (void)filter_count;
    (void)filters;
    return true;
}

static canard_us_t test_now_cb(const canard_t* const self)
{
    (void)self;
    return 0;
}

static bool test_tx_cb(canard_t* const      self,
                       void* const          origin,
                       const canard_us_t    deadline,
                       const uint_least8_t  iface_index,
                       const bool           fd,
                       const uint32_t       extended_can_id,
                       const canard_bytes_t can_data)
{
    (void)self;
    (void)origin;
    (void)deadline;
    (void)iface_index;
    (void)fd;
    (void)extended_can_id;
    (void)can_data;
    return true;
}

static const canard_vtable_t test_vtable_with_filter = { .now    = test_now_cb,
                                                         .tx     = test_tx_cb,
                                                         .filter = test_filter_cb };

static void dummy_on_message(canard_subscription_t* const self,
                             const canard_us_t            timestamp,
                             const canard_prio_t          priority,
                             const uint_least8_t          source_node_id,
                             const uint_least8_t          transfer_id,
                             const canard_payload_t       payload)
{
    (void)self;
    (void)timestamp;
    (void)priority;
    (void)source_node_id;
    (void)transfer_id;
    (void)payload;
}

static const canard_subscription_vtable_t dummy_sub_vtable = { .on_message = dummy_on_message };

static void test_rx_filter_configure_oom(void)
{
    // rx_filter_configure() should return false and increment err.oom when allocation fails.
    const canard_mem_t oom_mem  = { .vtable = &oom_mem_vtable, .context = NULL };
    const canard_mem_t real_mem = { .vtable = &real_mem_vtable, .context = NULL };
    canard_t           self;
    memset(&self, 0, sizeof(self));
    const canard_mem_set_t mem = { .tx_transfer = real_mem,
                                   .tx_frame    = real_mem,
                                   .rx_session  = real_mem,
                                   .rx_payload  = real_mem,
                                   .rx_filters  = oom_mem };
    TEST_ASSERT_TRUE(canard_new(&self, &test_vtable_with_filter, mem, 16U, 1234U, 4U));
    // Add a subscription so filtering has work to do.
    canard_subscription_t sub;
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 100U, 64U, 1000000, &dummy_sub_vtable));
    TEST_ASSERT_EQUAL_UINT64(0U, self.err.oom);
    // Call rx_filter_configure directly — allocation fails → returns false, err.oom incremented.
    TEST_ASSERT_FALSE(rx_filter_configure(&self));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.oom);
    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

static void test_rx_filter_configure_coalescence_overflow(void)
{
    // When subscriptions exceed filter capacity, the overflow path calls rx_filter_coalesce_into().
    const canard_mem_t real_mem = { .vtable = &real_mem_vtable, .context = NULL };
    canard_t           self;
    memset(&self, 0, sizeof(self));
    const canard_mem_set_t mem = { .tx_transfer = real_mem,
                                   .tx_frame    = real_mem,
                                   .rx_session  = real_mem,
                                   .rx_payload  = real_mem,
                                   .rx_filters  = real_mem };
    // Only 1 hardware filter slot but 2 subscriptions → overflow into coalescence.
    TEST_ASSERT_TRUE(canard_new(&self, &test_vtable_with_filter, mem, 16U, 1234U, 1U));
    canard_subscription_t sub1;
    canard_subscription_t sub2;
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub1, 100U, 64U, 1000000, &dummy_sub_vtable));
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub2, 200U, 64U, 1000000, &dummy_sub_vtable));
    // Should succeed — the filter callback returns true.
    TEST_ASSERT_TRUE(rx_filter_configure(&self));
    canard_unsubscribe(&self, &sub1);
    canard_unsubscribe(&self, &sub2);
    canard_destroy(&self);
}

// =====================================================================================================================
// rx_filter_match()

static void test_rx_filter_match_empty(void)
{
    TEST_ASSERT_FALSE(rx_filter_match(0, NULL, 0x12345678U));
    TEST_ASSERT_FALSE(rx_filter_match(0, NULL, 0U));
}

static void test_rx_filter_match_single_hit(void)
{
    const canard_filter_t f = make_filter(canard_kind_message_13b, 7509U, 42);
    TEST_ASSERT_TRUE(rx_filter_match(1, &f, f.extended_can_id));
    // Also matches with different source node-ID (bits 6:0 are not masked).
    TEST_ASSERT_TRUE(rx_filter_match(1, &f, f.extended_can_id | 1U));
    TEST_ASSERT_TRUE(rx_filter_match(1, &f, f.extended_can_id | 0x7FU));
}

static void test_rx_filter_match_single_miss(void)
{
    const canard_filter_t f = make_filter(canard_kind_message_13b, 7509U, 42);
    // A completely different subject-ID should not match.
    const canard_filter_t other = make_filter(canard_kind_message_13b, 100U, 42);
    TEST_ASSERT_FALSE(rx_filter_match(1, &f, other.extended_can_id));
    // v0 message with same numeric ID should also not match (different mask/bits).
    const canard_filter_t v0 = make_filter(canard_kind_v0_message, 341U, 42);
    TEST_ASSERT_FALSE(rx_filter_match(1, &f, v0.extended_can_id));
}

static void test_rx_filter_match_multiple(void)
{
    const canard_filter_t arr[] = {
        make_filter(canard_kind_message_16b, 100U, 42),
        make_filter(canard_kind_message_13b, 200U, 42),
        make_filter(canard_kind_v0_message, 300U, 42),
    };
    // Each filter's own CAN ID should be matched.
    TEST_ASSERT_TRUE(rx_filter_match(3, arr, arr[0].extended_can_id));
    TEST_ASSERT_TRUE(rx_filter_match(3, arr, arr[1].extended_can_id));
    TEST_ASSERT_TRUE(rx_filter_match(3, arr, arr[2].extended_can_id));
    // Unrelated CAN ID should not match any.
    const canard_filter_t unrelated = make_filter(canard_kind_message_13b, 999U, 42);
    TEST_ASSERT_FALSE(rx_filter_match(3, arr, unrelated.extended_can_id));
}

// =====================================================================================================================
// rx_filter_configure() forced Heartbeat/NodeStatus filters

#define HEARTBEAT_SUBJECT_ID 7509U
#define NODESTATUS_DTYPE_ID  341U

static size_t          g_cap_count;       // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static canard_filter_t g_cap_filters[32]; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

static bool capturing_filter_cb(canard_t* const self, const size_t filter_count, const canard_filter_t* const filters)
{
    (void)self;
    g_cap_count = filter_count;
    for (size_t i = 0; i < filter_count && i < 32U; i++) {
        g_cap_filters[i] = filters[i];
    }
    return true;
}

static const canard_vtable_t capturing_vtable = { .now = test_now_cb, .tx = test_tx_cb, .filter = capturing_filter_cb };

// Checks whether any of the captured filters accepts a given CAN ID.
static bool captured_accepts(const uint32_t can_id)
{
    for (size_t i = 0; i < g_cap_count; i++) {
        if (filter_accepts(g_cap_filters[i], can_id)) {
            return true;
        }
    }
    return false;
}

// Build a representative CAN ID for the forced message from an arbitrary source node.
static uint32_t heartbeat_can_id(const byte_t source)
{
    return (HEARTBEAT_SUBJECT_ID << 8U) | (source & CANARD_NODE_ID_MAX);
}
static uint32_t nodestatus_can_id(const byte_t source)
{
    return (NODESTATUS_DTYPE_ID << 8U) | (source & CANARD_NODE_ID_MAX);
}

static canard_t make_instance(const size_t filter_count)
{
    const canard_mem_t     real_mem = { .vtable = &real_mem_vtable, .context = NULL };
    const canard_mem_set_t mem      = { .tx_transfer = real_mem,
                                        .tx_frame    = real_mem,
                                        .rx_session  = real_mem,
                                        .rx_payload  = real_mem,
                                        .rx_filters  = real_mem };
    canard_t               self;
    memset(&self, 0, sizeof(self));
    (void)canard_new(&self, &capturing_vtable, mem, 16U, 1234U, filter_count);
    g_cap_count = 0;
    memset(g_cap_filters, 0, sizeof(g_cap_filters));
    return self;
}

static void test_rx_filter_configure_forced_no_subs(void)
{
    canard_t self = make_instance(4);
    TEST_ASSERT_TRUE(rx_filter_configure(&self));
    TEST_ASSERT_EQUAL_size_t(2U, g_cap_count); // Heartbeat + NodeStatus
    TEST_ASSERT_TRUE(captured_accepts(heartbeat_can_id(0)));
    TEST_ASSERT_TRUE(captured_accepts(heartbeat_can_id(42)));
    TEST_ASSERT_TRUE(captured_accepts(heartbeat_can_id(127)));
    TEST_ASSERT_TRUE(captured_accepts(nodestatus_can_id(0)));
    TEST_ASSERT_TRUE(captured_accepts(nodestatus_can_id(42)));
    TEST_ASSERT_TRUE(captured_accepts(nodestatus_can_id(127)));
    canard_destroy(&self);
}

static void test_rx_filter_configure_forced_heartbeat_subscribed(void)
{
    canard_t              self = make_instance(4);
    canard_subscription_t sub;
    TEST_ASSERT_TRUE(canard_subscribe_13b(&self, &sub, HEARTBEAT_SUBJECT_ID, 64U, 1000000, &dummy_sub_vtable));
    TEST_ASSERT_TRUE(rx_filter_configure(&self));
    // 1 subscription filter + 1 forced NodeStatus = 2
    TEST_ASSERT_EQUAL_size_t(2U, g_cap_count);
    TEST_ASSERT_TRUE(captured_accepts(heartbeat_can_id(1)));
    TEST_ASSERT_TRUE(captured_accepts(nodestatus_can_id(1)));
    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

static void test_rx_filter_configure_forced_nodestatus_subscribed(void)
{
    canard_t              self = make_instance(4);
    canard_subscription_t sub;
    TEST_ASSERT_TRUE(canard_v0_subscribe(&self, &sub, NODESTATUS_DTYPE_ID, 0, 64U, 1000000, &dummy_sub_vtable));
    TEST_ASSERT_TRUE(rx_filter_configure(&self));
    // 1 subscription filter + 1 forced Heartbeat = 2
    TEST_ASSERT_EQUAL_size_t(2U, g_cap_count);
    TEST_ASSERT_TRUE(captured_accepts(heartbeat_can_id(1)));
    TEST_ASSERT_TRUE(captured_accepts(nodestatus_can_id(1)));
    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

static void test_rx_filter_configure_forced_both_subscribed(void)
{
    canard_t              self = make_instance(4);
    canard_subscription_t sub_hb;
    canard_subscription_t sub_ns;
    TEST_ASSERT_TRUE(canard_subscribe_13b(&self, &sub_hb, HEARTBEAT_SUBJECT_ID, 64U, 1000000, &dummy_sub_vtable));
    TEST_ASSERT_TRUE(canard_v0_subscribe(&self, &sub_ns, NODESTATUS_DTYPE_ID, 0, 64U, 1000000, &dummy_sub_vtable));
    TEST_ASSERT_TRUE(rx_filter_configure(&self));
    // Both already covered by subscriptions, no extras.
    TEST_ASSERT_EQUAL_size_t(2U, g_cap_count);
    TEST_ASSERT_TRUE(captured_accepts(heartbeat_can_id(1)));
    TEST_ASSERT_TRUE(captured_accepts(nodestatus_can_id(1)));
    canard_unsubscribe(&self, &sub_hb);
    canard_unsubscribe(&self, &sub_ns);
    canard_destroy(&self);
}

static void test_rx_filter_configure_forced_capacity_1(void)
{
    canard_t self = make_instance(1);
    TEST_ASSERT_TRUE(rx_filter_configure(&self));
    // Heartbeat fills the slot, NodeStatus is coalesced into it.
    TEST_ASSERT_EQUAL_size_t(1U, g_cap_count);
    // The coalesced filter must still accept both.
    TEST_ASSERT_TRUE(captured_accepts(heartbeat_can_id(1)));
    TEST_ASSERT_TRUE(captured_accepts(nodestatus_can_id(1)));
    canard_destroy(&self);
}

static void test_rx_filter_configure_forced_capacity_2_no_subs(void)
{
    canard_t self = make_instance(2);
    TEST_ASSERT_TRUE(rx_filter_configure(&self));
    TEST_ASSERT_EQUAL_size_t(2U, g_cap_count);
    // Each forced filter gets its own slot.
    TEST_ASSERT_TRUE(captured_accepts(heartbeat_can_id(1)));
    TEST_ASSERT_TRUE(captured_accepts(nodestatus_can_id(1)));
    canard_destroy(&self);
}

static void test_rx_filter_configure_forced_with_unrelated_subs(void)
{
    canard_t              self = make_instance(10);
    canard_subscription_t sub1;
    canard_subscription_t sub2;
    canard_subscription_t sub3;
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub1, 100U, 64U, 1000000, &dummy_sub_vtable));
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub2, 200U, 64U, 1000000, &dummy_sub_vtable));
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub3, 300U, 64U, 1000000, &dummy_sub_vtable));
    TEST_ASSERT_TRUE(rx_filter_configure(&self));
    // 3 subs + 2 forced = 5
    TEST_ASSERT_EQUAL_size_t(5U, g_cap_count);
    TEST_ASSERT_TRUE(captured_accepts(heartbeat_can_id(1)));
    TEST_ASSERT_TRUE(captured_accepts(nodestatus_can_id(1)));
    // Subscriptions still work.
    const canard_filter_t f1 = make_filter(canard_kind_message_16b, 100U, 0);
    const canard_filter_t f2 = make_filter(canard_kind_message_16b, 200U, 0);
    const canard_filter_t f3 = make_filter(canard_kind_message_16b, 300U, 0);
    TEST_ASSERT_TRUE(captured_accepts(f1.extended_can_id));
    TEST_ASSERT_TRUE(captured_accepts(f2.extended_can_id));
    TEST_ASSERT_TRUE(captured_accepts(f3.extended_can_id));
    canard_unsubscribe(&self, &sub1);
    canard_unsubscribe(&self, &sub2);
    canard_unsubscribe(&self, &sub3);
    canard_destroy(&self);
}

static void test_rx_filter_configure_forced_overflow(void)
{
    // capacity=1 with 2 unrelated subs: subs fill+coalesce, then forced filters also coalesce in.
    canard_t              self = make_instance(1);
    canard_subscription_t sub1;
    canard_subscription_t sub2;
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub1, 100U, 64U, 1000000, &dummy_sub_vtable));
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub2, 200U, 64U, 1000000, &dummy_sub_vtable));
    TEST_ASSERT_TRUE(rx_filter_configure(&self));
    TEST_ASSERT_EQUAL_size_t(1U, g_cap_count);
    // After heavy coalescence the single filter should still accept all four CAN IDs.
    const canard_filter_t f1 = make_filter(canard_kind_message_16b, 100U, 0);
    const canard_filter_t f2 = make_filter(canard_kind_message_16b, 200U, 0);
    TEST_ASSERT_TRUE(captured_accepts(f1.extended_can_id));
    TEST_ASSERT_TRUE(captured_accepts(f2.extended_can_id));
    TEST_ASSERT_TRUE(captured_accepts(heartbeat_can_id(1)));
    TEST_ASSERT_TRUE(captured_accepts(nodestatus_can_id(1)));
    canard_unsubscribe(&self, &sub1);
    canard_unsubscribe(&self, &sub2);
    canard_destroy(&self);
}

// =====================================================================================================================

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_rx_filter_for_subscription_golden_vectors);
    RUN_TEST(test_rx_filter_for_subscription_v1_1_message_semantics);
    RUN_TEST(test_rx_filter_for_subscription_v1_0_message_semantics);
    RUN_TEST(test_rx_filter_for_subscription_v1_0_request_semantics);
    RUN_TEST(test_rx_filter_for_subscription_v1_0_response_semantics);
    RUN_TEST(test_rx_filter_for_subscription_v0_message_semantics);
    RUN_TEST(test_rx_filter_for_subscription_v0_request_semantics);
    RUN_TEST(test_rx_filter_for_subscription_v0_response_semantics);

    RUN_TEST(test_rx_filter_fuse_basic);
    RUN_TEST(test_rx_filter_fuse_is_commutative);

    RUN_TEST(test_rx_filter_rank_vectors);

    RUN_TEST(test_rx_filter_coalesce_into_selects_best_rank);
    RUN_TEST(test_rx_filter_coalesce_into_tie_prefers_later_index);
    RUN_TEST(test_rx_filter_coalesce_into_merges_existing_pair_when_best);
    RUN_TEST(test_rx_filter_coalesce_into_single_entry);

    RUN_TEST(test_coalesce_coverage_count1_exhaustive);
    RUN_TEST(test_coalesce_coverage_count2_sampled);
    RUN_TEST(test_coalesce_coverage_count3_targeted);

    RUN_TEST(test_coalesce_count3_fuses_existing_pair);
    RUN_TEST(test_coalesce_count3_fuses_with_new_at_last);
    RUN_TEST(test_coalesce_count3_fuses_with_new_at_first);
    RUN_TEST(test_coalesce_count4_middle_best_i);
    RUN_TEST(test_coalesce_count5);

    RUN_TEST(test_coalesce_all_identical);
    RUN_TEST(test_coalesce_wildcard_existing);
    RUN_TEST(test_coalesce_new_is_wildcard);
    RUN_TEST(test_coalesce_fully_specified_29bit);
    RUN_TEST(test_coalesce_new_identical_to_existing);
    RUN_TEST(test_coalesce_worst_case_expansion);

    RUN_TEST(test_coalesce_tie_prefers_later_pair_count3);

    RUN_TEST(test_coalesce_sequential_invariant_4bit);
    RUN_TEST(test_coalesce_sequential_realistic_cyphal);

    RUN_TEST(test_coalesce_v1_messages_pair);
    RUN_TEST(test_coalesce_v1_request_response_pair);
    RUN_TEST(test_coalesce_mixed_versions);

    RUN_TEST(test_coalesce_adversarial_bit_pattern);
    RUN_TEST(test_coalesce_best_i_zero_best_j_one);
    RUN_TEST(test_coalesce_greedy_multistep_degradation);

    RUN_TEST(test_rx_filter_configure_oom);
    RUN_TEST(test_rx_filter_configure_coalescence_overflow);

    RUN_TEST(test_rx_filter_match_empty);
    RUN_TEST(test_rx_filter_match_single_hit);
    RUN_TEST(test_rx_filter_match_single_miss);
    RUN_TEST(test_rx_filter_match_multiple);

    RUN_TEST(test_rx_filter_configure_forced_no_subs);
    RUN_TEST(test_rx_filter_configure_forced_heartbeat_subscribed);
    RUN_TEST(test_rx_filter_configure_forced_nodestatus_subscribed);
    RUN_TEST(test_rx_filter_configure_forced_both_subscribed);
    RUN_TEST(test_rx_filter_configure_forced_capacity_1);
    RUN_TEST(test_rx_filter_configure_forced_capacity_2_no_subs);
    RUN_TEST(test_rx_filter_configure_forced_with_unrelated_subs);
    RUN_TEST(test_rx_filter_configure_forced_overflow);

    return UNITY_END();
}
