// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal Development Team.

#include "canard.c" // NOLINT(bugprone-suspicious-include)
#include "helpers.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

static canard_filter_t make_filter(const canard_kind_t kind, const uint16_t port_id, const byte_t node_id)
{
    canard_t              self;
    canard_subscription_t sub;
    memset(&self, 0, sizeof(self));
    memset(&sub, 0, sizeof(sub));
    self.node_id = node_id;
    sub.kind     = kind;
    sub.port_id  = port_id;
    return rx_filter_for_subscription(&self, &sub);
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
        const canard_filter_t f = make_filter(canard_kind_1v1_message, 0xABCDU, 42U);
        TEST_ASSERT_EQUAL_HEX32(0x00ABCD80UL, f.extended_can_id);
        TEST_ASSERT_EQUAL_HEX32(0x03FFFF80UL, f.extended_mask);
    }

    // v1.0 message: subject=0x1ABC
    {
        const canard_filter_t f = make_filter(canard_kind_1v0_message, 0x1ABCU, 42U);
        TEST_ASSERT_EQUAL_HEX32(0x001ABC00UL, f.extended_can_id);
        TEST_ASSERT_EQUAL_HEX32(0x029FFF80UL, f.extended_mask);
    }

    // v1.0 request: service=0x1A5, dst node=42
    {
        const canard_filter_t f = make_filter(canard_kind_1v0_request, 0x1A5U, 42U);
        TEST_ASSERT_EQUAL_HEX32(0x03695500UL, f.extended_can_id);
        TEST_ASSERT_EQUAL_HEX32(0x03FFFF80UL, f.extended_mask);
    }

    // v1.0 response: service=0x1A5, dst node=42
    {
        const canard_filter_t f = make_filter(canard_kind_1v0_response, 0x1A5U, 42U);
        TEST_ASSERT_EQUAL_HEX32(0x02695500UL, f.extended_can_id);
        TEST_ASSERT_EQUAL_HEX32(0x03FFFF80UL, f.extended_mask);
    }

    // v0.1 message: data type ID=0xABCD
    {
        const canard_filter_t f = make_filter(canard_kind_0v1_message, 0xABCDU, 42U);
        TEST_ASSERT_EQUAL_HEX32(0x00ABCD00UL, f.extended_can_id);
        TEST_ASSERT_EQUAL_HEX32(0x00FFFF80UL, f.extended_mask);
    }

    // v0.1 request: data type ID=0x5A, dst node=42
    {
        const canard_filter_t f = make_filter(canard_kind_0v1_request, 0x5AU, 42U);
        TEST_ASSERT_EQUAL_HEX32(0x005AAA80UL, f.extended_can_id);
        TEST_ASSERT_EQUAL_HEX32(0x00FFFF80UL, f.extended_mask);
    }

    // v0.1 response: data type ID=0x5A, dst node=42
    {
        const canard_filter_t f = make_filter(canard_kind_0v1_response, 0x5AU, 42U);
        TEST_ASSERT_EQUAL_HEX32(0x005A2A80UL, f.extended_can_id);
        TEST_ASSERT_EQUAL_HEX32(0x00FFFF80UL, f.extended_mask);
    }
}

// =====================================================================================================================
// Group 2: rx_filter_for_subscription() acceptance behavior

static void test_rx_filter_for_subscription_v1_1_message_semantics(void)
{
    const canard_filter_t f = make_filter(canard_kind_1v1_message, 0x8001U, 55U);
    TEST_ASSERT_TRUE(filter_accepts(f, 0x008001AAUL));  // same subject, prio=0, src=42
    TEST_ASSERT_TRUE(filter_accepts(f, 0x1C8001FFUL));  // same subject, prio=7, src=127
    TEST_ASSERT_FALSE(filter_accepts(f, 0x028001AAUL)); // service bit (25) must be zero
    TEST_ASSERT_FALSE(filter_accepts(f, 0x018001AAUL)); // bit 24 must be zero for v1.1
    TEST_ASSERT_FALSE(filter_accepts(f, 0x0080012AUL)); // message selector bit (7) must be one
    TEST_ASSERT_FALSE(filter_accepts(f, 0x008002AAUL)); // subject mismatch
}

static void test_rx_filter_for_subscription_v1_0_message_semantics(void)
{
    const canard_filter_t f = make_filter(canard_kind_1v0_message, 42U, 55U);
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
    const canard_filter_t f = make_filter(canard_kind_1v0_request, 0x1A5U, 42U);
    TEST_ASSERT_TRUE(filter_accepts(f, 0x0369550BUL));  // base form
    TEST_ASSERT_TRUE(filter_accepts(f, 0x1B69557FUL));  // different prio and src
    TEST_ASSERT_FALSE(filter_accepts(f, 0x0269550BUL)); // response bit (24=0)
    TEST_ASSERT_FALSE(filter_accepts(f, 0x03E9550BUL)); // reserved bit 23 set
    TEST_ASSERT_FALSE(filter_accepts(f, 0x0369950BUL)); // service mismatch
    TEST_ASSERT_FALSE(filter_accepts(f, 0x0369558BUL)); // destination mismatch
}

static void test_rx_filter_for_subscription_v1_0_response_semantics(void)
{
    const canard_filter_t f = make_filter(canard_kind_1v0_response, 0x1A5U, 42U);
    TEST_ASSERT_TRUE(filter_accepts(f, 0x02695511UL));  // base form
    TEST_ASSERT_TRUE(filter_accepts(f, 0x1A69557FUL));  // different prio and src
    TEST_ASSERT_FALSE(filter_accepts(f, 0x03695511UL)); // request bit (24=1)
    TEST_ASSERT_FALSE(filter_accepts(f, 0x02E95511UL)); // reserved bit 23 set
    TEST_ASSERT_FALSE(filter_accepts(f, 0x02699511UL)); // service mismatch
    TEST_ASSERT_FALSE(filter_accepts(f, 0x02695591UL)); // destination mismatch
}

static void test_rx_filter_for_subscription_v0_message_semantics(void)
{
    const canard_filter_t f = make_filter(canard_kind_0v1_message, 0x1234U, 55U);
    TEST_ASSERT_TRUE(filter_accepts(f, 0x00123405UL));  // base form
    TEST_ASSERT_TRUE(filter_accepts(f, 0x01123455UL));  // bit 24 set
    TEST_ASSERT_TRUE(filter_accepts(f, 0x0312347FUL));  // bits 24:25 set
    TEST_ASSERT_FALSE(filter_accepts(f, 0x00123505UL)); // data type ID mismatch
    TEST_ASSERT_FALSE(filter_accepts(f, 0x00123485UL)); // service flag bit (7) set
}

static void test_rx_filter_for_subscription_v0_request_semantics(void)
{
    const canard_filter_t f = make_filter(canard_kind_0v1_request, 0x5AU, 42U);
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
    const canard_filter_t f = make_filter(canard_kind_0v1_response, 0x5AU, 42U);
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

    return UNITY_END();
}
