// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal Development Team.

#include "canard.c" // NOLINT(bugprone-suspicious-include)
#include "helpers.h"
#include <unity.h>

// ============================================  POPCOUNT TESTS  ============================================

static void test_popcount(byte_t (*const popcnt)(uint64_t))
{
    // Zero.
    TEST_ASSERT_EQUAL_UINT8(0, popcnt(0));

    // Single bits at each nibble boundary.
    TEST_ASSERT_EQUAL_UINT8(1, popcnt(1ULL));
    TEST_ASSERT_EQUAL_UINT8(1, popcnt(1ULL << 4U));
    TEST_ASSERT_EQUAL_UINT8(1, popcnt(1ULL << 8U));
    TEST_ASSERT_EQUAL_UINT8(1, popcnt(1ULL << 16U));
    TEST_ASSERT_EQUAL_UINT8(1, popcnt(1ULL << 32U));
    TEST_ASSERT_EQUAL_UINT8(1, popcnt(1ULL << 48U));
    TEST_ASSERT_EQUAL_UINT8(1, popcnt(1ULL << 63U));

    // All bits set in progressively larger ranges.
    TEST_ASSERT_EQUAL_UINT8(4, popcnt(0xF));
    TEST_ASSERT_EQUAL_UINT8(8, popcnt(0xFF));
    TEST_ASSERT_EQUAL_UINT8(12, popcnt(0xFFF));
    TEST_ASSERT_EQUAL_UINT8(16, popcnt(0xFFFF));
    TEST_ASSERT_EQUAL_UINT8(24, popcnt(0xFFFFFF));
    TEST_ASSERT_EQUAL_UINT8(32, popcnt(0xFFFFFFFF));
    TEST_ASSERT_EQUAL_UINT8(48, popcnt(0xFFFFFFFFFFFFULL));
    TEST_ASSERT_EQUAL_UINT8(64, popcnt(0xFFFFFFFFFFFFFFFFULL));

    // Alternating bit patterns.
    TEST_ASSERT_EQUAL_UINT8(32, popcnt(0xAAAAAAAAAAAAAAAAULL)); // 10101010...
    TEST_ASSERT_EQUAL_UINT8(32, popcnt(0x5555555555555555ULL)); // 01010101...
    TEST_ASSERT_EQUAL_UINT8(32, popcnt(0xCCCCCCCCCCCCCCCCULL)); // 11001100...
    TEST_ASSERT_EQUAL_UINT8(32, popcnt(0x3333333333333333ULL)); // 00110011...
    TEST_ASSERT_EQUAL_UINT8(32, popcnt(0xF0F0F0F0F0F0F0F0ULL)); // 11110000...
    TEST_ASSERT_EQUAL_UINT8(32, popcnt(0x0F0F0F0F0F0F0F0FULL)); // 00001111...

    // Byte patterns repeated.
    TEST_ASSERT_EQUAL_UINT8(8, popcnt(0x0101010101010101ULL));  // One bit per byte.
    TEST_ASSERT_EQUAL_UINT8(16, popcnt(0x0303030303030303ULL)); // Two bits per byte.
    TEST_ASSERT_EQUAL_UINT8(24, popcnt(0x0707070707070707ULL)); // Three bits per byte.
    TEST_ASSERT_EQUAL_UINT8(32, popcnt(0x0F0F0F0F0F0F0F0FULL)); // Four bits per byte.
    TEST_ASSERT_EQUAL_UINT8(56, popcnt(0x7F7F7F7F7F7F7F7FULL)); // Seven bits per byte.

    // Sparse patterns (few bits set).
    TEST_ASSERT_EQUAL_UINT8(2, popcnt(0x8000000000000001ULL)); // Endpoints only.
    TEST_ASSERT_EQUAL_UINT8(2, popcnt(0x0000000180000000ULL)); // Middle bits.
    TEST_ASSERT_EQUAL_UINT8(4, popcnt(0x8000000180000001ULL)); // Corners.

    // Dense patterns (few bits clear).
    TEST_ASSERT_EQUAL_UINT8(63, popcnt(0xFFFFFFFFFFFFFFFEULL)); // All but LSB.
    TEST_ASSERT_EQUAL_UINT8(63, popcnt(0x7FFFFFFFFFFFFFFFULL)); // All but MSB.
    TEST_ASSERT_EQUAL_UINT8(62, popcnt(0x7FFFFFFFFFFFFFFEULL)); // All but both ends.

    // Powers of two minus one (all lower bits set).
    TEST_ASSERT_EQUAL_UINT8(1, popcnt((1ULL << 1U) - 1U));
    TEST_ASSERT_EQUAL_UINT8(7, popcnt((1ULL << 7U) - 1U));
    TEST_ASSERT_EQUAL_UINT8(15, popcnt((1ULL << 15U) - 1U));
    TEST_ASSERT_EQUAL_UINT8(31, popcnt((1ULL << 31U) - 1U));
    TEST_ASSERT_EQUAL_UINT8(63, popcnt((1ULL << 63U) - 1U));

    // Small values.
    TEST_ASSERT_EQUAL_UINT8(1, popcnt(1));
    TEST_ASSERT_EQUAL_UINT8(1, popcnt(2));
    TEST_ASSERT_EQUAL_UINT8(2, popcnt(3));
    TEST_ASSERT_EQUAL_UINT8(1, popcnt(4));
    TEST_ASSERT_EQUAL_UINT8(2, popcnt(5));
    TEST_ASSERT_EQUAL_UINT8(2, popcnt(6));
    TEST_ASSERT_EQUAL_UINT8(3, popcnt(7));
    TEST_ASSERT_EQUAL_UINT8(1, popcnt(8));

    // Specific known values.
    TEST_ASSERT_EQUAL_UINT8(6, popcnt(63));
    TEST_ASSERT_EQUAL_UINT8(1, popcnt(64));
    TEST_ASSERT_EQUAL_UINT8(7, popcnt(127));
    TEST_ASSERT_EQUAL_UINT8(1, popcnt(128));
    TEST_ASSERT_EQUAL_UINT8(8, popcnt(255));
    TEST_ASSERT_EQUAL_UINT8(1, popcnt(256));
}

static void test_popcount_emulated(void) { test_popcount(popcount_emulated); }
static void test_popcount_intrinsics(void) { test_popcount(popcount); }

// ==============================================  CRC TESTS  ==============================================

static void test_crc_add(void)
{
    // Empty input returns initial CRC unchanged.
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc_add(CRC_INITIAL, 0, NULL));

    // Single bytes.
    uint8_t data = 0x00;
    TEST_ASSERT_EQUAL_HEX16(0xE1F0, crc_add(CRC_INITIAL, 1, &data));
    data = 0xFF;
    TEST_ASSERT_EQUAL_HEX16(0xFF00, crc_add(CRC_INITIAL, 1, &data));
    data = 'A';
    TEST_ASSERT_EQUAL_HEX16(0xB915, crc_add(CRC_INITIAL, 1, &data));

    // Standard test vector: "123456789" yields 0x29B1.
    const uint8_t vec[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc_add(CRC_INITIAL, sizeof(vec), vec));

    // Multi-byte patterns.
    const uint8_t zeros[8] = { 0 };
    TEST_ASSERT_EQUAL_HEX16(0x313E, crc_add(CRC_INITIAL, sizeof(zeros), zeros));
    const uint8_t ones[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    TEST_ASSERT_EQUAL_HEX16(0x97DF, crc_add(CRC_INITIAL, sizeof(ones), ones));

    // Incremental computation must match full computation.
    uint16_t crc_inc = CRC_INITIAL;
    for (size_t i = 0; i < sizeof(vec); i++) {
        crc_inc = crc_add(crc_inc, 1, &vec[i]);
    }
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc_inc);

    // Two-chunk computation.
    uint16_t crc_chunks = crc_add(CRC_INITIAL, 5, vec);
    crc_chunks          = crc_add(crc_chunks, 4, &vec[5]);
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc_chunks);
}

static void test_crc_add_chain(void)
{
    // Single fragment.
    const uint8_t              data[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    const canard_bytes_chain_t single = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc_add_chain(CRC_INITIAL, single));

    // Multiple fragments: "123" + "45" + "6789".
    const uint8_t              f1[] = { '1', '2', '3' };
    const uint8_t              f2[] = { '4', '5' };
    const uint8_t              f3[] = { '6', '7', '8', '9' };
    const canard_bytes_chain_t c3   = { .bytes = { .size = sizeof(f3), .data = f3 }, .next = NULL };
    const canard_bytes_chain_t c2   = { .bytes = { .size = sizeof(f2), .data = f2 }, .next = &c3 };
    const canard_bytes_chain_t c1   = { .bytes = { .size = sizeof(f1), .data = f1 }, .next = &c2 };
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc_add_chain(CRC_INITIAL, c1));

    // Empty fragments interspersed.
    const uint8_t              fa[] = { '1', '2', '3', '4', '5' };
    const uint8_t              fb[] = { '6', '7', '8', '9' };
    const canard_bytes_chain_t e4   = { .bytes = { .size = sizeof(fb), .data = fb }, .next = NULL };
    const canard_bytes_chain_t e3   = { .bytes = { .size = 0, .data = NULL }, .next = &e4 };
    const canard_bytes_chain_t e2   = { .bytes = { .size = 0, .data = NULL }, .next = &e3 };
    const canard_bytes_chain_t e1   = { .bytes = { .size = sizeof(fa), .data = fa }, .next = &e2 };
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc_add_chain(CRC_INITIAL, e1));

    // Single-byte fragments.
    const uint8_t              b1 = '1';
    const uint8_t              b2 = '2';
    const uint8_t              b3 = '3';
    const uint8_t              b4 = '4';
    const uint8_t              b5 = '5';
    const uint8_t              b6 = '6';
    const uint8_t              b7 = '7';
    const uint8_t              b8 = '8';
    const uint8_t              b9 = '9';
    const canard_bytes_chain_t s9 = { .bytes = { .size = 1, .data = &b9 }, .next = NULL };
    const canard_bytes_chain_t s8 = { .bytes = { .size = 1, .data = &b8 }, .next = &s9 };
    const canard_bytes_chain_t s7 = { .bytes = { .size = 1, .data = &b7 }, .next = &s8 };
    const canard_bytes_chain_t s6 = { .bytes = { .size = 1, .data = &b6 }, .next = &s7 };
    const canard_bytes_chain_t s5 = { .bytes = { .size = 1, .data = &b5 }, .next = &s6 };
    const canard_bytes_chain_t s4 = { .bytes = { .size = 1, .data = &b4 }, .next = &s5 };
    const canard_bytes_chain_t s3 = { .bytes = { .size = 1, .data = &b3 }, .next = &s4 };
    const canard_bytes_chain_t s2 = { .bytes = { .size = 1, .data = &b2 }, .next = &s3 };
    const canard_bytes_chain_t s1 = { .bytes = { .size = 1, .data = &b1 }, .next = &s2 };
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc_add_chain(CRC_INITIAL, s1));
}

// ==========================================  BYTES CHAIN TESTS  ==========================================

static void test_bytes_chain(void)
{
    // Single fragment.
    const uint8_t              data[] = { 'H', 'e', 'l', 'l', 'o', ',', ' ', 'w', 'o', 'r', 'l', 'd', '!' };
    const canard_bytes_chain_t single = { .bytes = { .size = sizeof(data), .data = data }, .next = NULL };
    TEST_ASSERT_EQUAL_size_t(13, bytes_chain_size(single));

    // Read entire single fragment.
    uint8_t              buf[16] = { 0 };
    bytes_chain_reader_t reader  = { .cursor = &single, .position = 0 };
    bytes_chain_read(&reader, sizeof(data), buf);
    TEST_ASSERT_EQUAL_MEMORY(data, buf, sizeof(data));

    // Multiple fragments: "Hello" + ", " + "world!".
    const uint8_t              f1[] = { 'H', 'e', 'l', 'l', 'o' };
    const uint8_t              f2[] = { ',', ' ' };
    const uint8_t              f3[] = { 'w', 'o', 'r', 'l', 'd', '!' };
    const canard_bytes_chain_t c3   = { .bytes = { .size = sizeof(f3), .data = f3 }, .next = NULL };
    const canard_bytes_chain_t c2   = { .bytes = { .size = sizeof(f2), .data = f2 }, .next = &c3 };
    const canard_bytes_chain_t c1   = { .bytes = { .size = sizeof(f1), .data = f1 }, .next = &c2 };
    TEST_ASSERT_EQUAL_size_t(13, bytes_chain_size(c1));

    // Read all at once across fragments.
    memset(buf, 0, sizeof(buf));
    reader = (bytes_chain_reader_t){ .cursor = &c1, .position = 0 };
    bytes_chain_read(&reader, 13, buf);
    TEST_ASSERT_EQUAL_MEMORY(data, buf, 13);

    // Read in chunks crossing fragment boundaries.
    memset(buf, 0, sizeof(buf));
    reader = (bytes_chain_reader_t){ .cursor = &c1, .position = 0 };
    bytes_chain_read(&reader, 3, buf);     // "Hel"
    bytes_chain_read(&reader, 4, buf + 3); // "lo, "
    bytes_chain_read(&reader, 6, buf + 7); // "world!"
    TEST_ASSERT_EQUAL_MEMORY(data, buf, 13);

    // Empty fragments interspersed.
    const uint8_t              fa[] = { 'A', 'B', 'C' };
    const uint8_t              fb[] = { 'D', 'E' };
    const canard_bytes_chain_t e5   = { .bytes = { .size = sizeof(fb), .data = fb }, .next = NULL };
    const canard_bytes_chain_t e4   = { .bytes = { .size = 0, .data = NULL }, .next = &e5 };
    const canard_bytes_chain_t e3   = { .bytes = { .size = 0, .data = NULL }, .next = &e4 };
    const canard_bytes_chain_t e2   = { .bytes = { .size = sizeof(fa), .data = fa }, .next = &e3 };
    const canard_bytes_chain_t e1   = { .bytes = { .size = 0, .data = NULL }, .next = &e2 };
    TEST_ASSERT_EQUAL_size_t(5, bytes_chain_size(e1));

    // Read skipping empty fragments.
    memset(buf, 0, sizeof(buf));
    reader = (bytes_chain_reader_t){ .cursor = &e1, .position = 0 };
    bytes_chain_read(&reader, 5, buf);
    TEST_ASSERT_EQUAL_UINT8('A', buf[0]);
    TEST_ASSERT_EQUAL_UINT8('B', buf[1]);
    TEST_ASSERT_EQUAL_UINT8('C', buf[2]);
    TEST_ASSERT_EQUAL_UINT8('D', buf[3]);
    TEST_ASSERT_EQUAL_UINT8('E', buf[4]);

    // Single-byte reads.
    reader = (bytes_chain_reader_t){ .cursor = &c1, .position = 0 };
    for (size_t i = 0; i < 13; i++) {
        uint8_t b = 0;
        bytes_chain_read(&reader, 1, &b);
        TEST_ASSERT_EQUAL_UINT8(data[i], b);
    }

    // Empty chain (single empty fragment).
    const canard_bytes_chain_t empty = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    TEST_ASSERT_EQUAL_size_t(0, bytes_chain_size(empty));
}

// ==============================================  LIST TESTS  ==============================================

typedef struct test_node_t
{
    int                  value;
    canard_list_member_t member;
} test_node_t;

static void test_list(void)
{
    canard_list_t list  = { .head = NULL, .tail = NULL };
    test_node_t   node1 = { .value = 1, .member = { .next = NULL, .prev = NULL } };
    test_node_t   node2 = { .value = 2, .member = { .next = NULL, .prev = NULL } };
    test_node_t   node3 = { .value = 3, .member = { .next = NULL, .prev = NULL } };

    // Empty list.
    TEST_ASSERT_NULL(list.head);
    TEST_ASSERT_NULL(list.tail);
    TEST_ASSERT_FALSE(is_listed(&list, &node1.member));

    // Delist on empty list is a no-op.
    delist(&list, &node1.member);
    TEST_ASSERT_NULL(list.head);

    // is_listed returns true when member is the sole element (next=NULL, prev=NULL, but head==member).
    list.head = list.tail = &node1.member;
    TEST_ASSERT_TRUE(is_listed(&list, &node1.member));
    list.head = list.tail = NULL;

    // Add single element.
    enlist_head(&list, &node1.member);
    TEST_ASSERT_EQUAL_PTR(&node1.member, list.head);
    TEST_ASSERT_EQUAL_PTR(&node1.member, list.tail);
    TEST_ASSERT_TRUE(is_listed(&list, &node1.member));
    TEST_ASSERT_NULL(node1.member.next);
    TEST_ASSERT_NULL(node1.member.prev);

    // Add second element at head.
    enlist_head(&list, &node2.member);
    TEST_ASSERT_EQUAL_PTR(&node2.member, list.head);
    TEST_ASSERT_EQUAL_PTR(&node1.member, list.tail);
    TEST_ASSERT_EQUAL_PTR(&node1.member, node2.member.next);
    TEST_ASSERT_EQUAL_PTR(&node2.member, node1.member.prev);

    // Add third element at head. Order: node3 -> node2 -> node1.
    enlist_head(&list, &node3.member);
    TEST_ASSERT_EQUAL_PTR(&node3.member, list.head);
    TEST_ASSERT_EQUAL_PTR(&node1.member, list.tail);

    // Delist middle.
    delist(&list, &node2.member);
    TEST_ASSERT_EQUAL_PTR(&node3.member, list.head);
    TEST_ASSERT_EQUAL_PTR(&node1.member, list.tail);
    TEST_ASSERT_EQUAL_PTR(&node1.member, node3.member.next);
    TEST_ASSERT_EQUAL_PTR(&node3.member, node1.member.prev);
    TEST_ASSERT_FALSE(is_listed(&list, &node2.member));

    // Re-add node2, then delist head.
    enlist_head(&list, &node2.member); // Order: node2 -> node3 -> node1.
    delist(&list, &node2.member);
    TEST_ASSERT_EQUAL_PTR(&node3.member, list.head);
    TEST_ASSERT_NULL(node3.member.prev);

    // Delist tail.
    delist(&list, &node1.member);
    TEST_ASSERT_EQUAL_PTR(&node3.member, list.head);
    TEST_ASSERT_EQUAL_PTR(&node3.member, list.tail);

    // Delist last element.
    delist(&list, &node3.member);
    TEST_ASSERT_NULL(list.head);
    TEST_ASSERT_NULL(list.tail);

    // Re-enlist moves element to front.
    enlist_head(&list, &node1.member);
    enlist_head(&list, &node2.member);
    enlist_head(&list, &node3.member); // Order: node3 -> node2 -> node1.
    enlist_head(&list, &node1.member); // Move tail to head. Order: node1 -> node3 -> node2.
    TEST_ASSERT_EQUAL_PTR(&node1.member, list.head);
    TEST_ASSERT_EQUAL_PTR(&node2.member, list.tail);
    TEST_ASSERT_EQUAL_PTR(&node3.member, node1.member.next);
    TEST_ASSERT_EQUAL_PTR(&node2.member, node3.member.next);

    // Test LIST_MEMBER and LIST_TAIL macros.
    test_node_t* head = LIST_MEMBER(list.head, test_node_t, member);
    test_node_t* tail = LIST_TAIL(list, test_node_t, member);
    TEST_ASSERT_EQUAL_INT(1, head->value);
    TEST_ASSERT_EQUAL_INT(2, tail->value);
}

static void test_list_enlist_tail(void)
{
    canard_list_t list  = { .head = NULL, .tail = NULL };
    test_node_t   node1 = { .value = 1, .member = { .next = NULL, .prev = NULL } };
    test_node_t   node2 = { .value = 2, .member = { .next = NULL, .prev = NULL } };
    test_node_t   node3 = { .value = 3, .member = { .next = NULL, .prev = NULL } };

    // Add single element at tail.
    enlist_tail(&list, &node1.member);
    TEST_ASSERT_EQUAL_PTR(&node1.member, list.head);
    TEST_ASSERT_EQUAL_PTR(&node1.member, list.tail);
    TEST_ASSERT_NULL(node1.member.next);
    TEST_ASSERT_NULL(node1.member.prev);

    // Add second element at tail. Order: node1 -> node2.
    enlist_tail(&list, &node2.member);
    TEST_ASSERT_EQUAL_PTR(&node1.member, list.head);
    TEST_ASSERT_EQUAL_PTR(&node2.member, list.tail);
    TEST_ASSERT_EQUAL_PTR(&node2.member, node1.member.next);
    TEST_ASSERT_EQUAL_PTR(&node1.member, node2.member.prev);

    // Add third element at tail. Order: node1 -> node2 -> node3.
    enlist_tail(&list, &node3.member);
    TEST_ASSERT_EQUAL_PTR(&node1.member, list.head);
    TEST_ASSERT_EQUAL_PTR(&node3.member, list.tail);
    TEST_ASSERT_EQUAL_PTR(&node2.member, node1.member.next);
    TEST_ASSERT_EQUAL_PTR(&node3.member, node2.member.next);
    TEST_ASSERT_EQUAL_PTR(&node2.member, node3.member.prev);

    // Re-enlist moves element to back.
    enlist_tail(&list, &node1.member); // Move head to tail. Order: node2 -> node3 -> node1.
    TEST_ASSERT_EQUAL_PTR(&node2.member, list.head);
    TEST_ASSERT_EQUAL_PTR(&node1.member, list.tail);
    TEST_ASSERT_EQUAL_PTR(&node3.member, node2.member.next);
    TEST_ASSERT_EQUAL_PTR(&node1.member, node3.member.next);
    TEST_ASSERT_EQUAL_PTR(&node3.member, node1.member.prev);
    TEST_ASSERT_NULL(node2.member.prev);
    TEST_ASSERT_NULL(node1.member.next);

    // Re-enlist tail is a no-op on ordering.
    enlist_tail(&list, &node1.member); // Order unchanged: node2 -> node3 -> node1.
    TEST_ASSERT_EQUAL_PTR(&node2.member, list.head);
    TEST_ASSERT_EQUAL_PTR(&node1.member, list.tail);

    // Mix enlist_head and enlist_tail.
    delist(&list, &node1.member);
    delist(&list, &node2.member);
    delist(&list, &node3.member);
    enlist_head(&list, &node2.member);
    enlist_tail(&list, &node3.member); // Order: node2 -> node3.
    enlist_head(&list, &node1.member); // Order: node1 -> node2 -> node3.
    TEST_ASSERT_EQUAL_PTR(&node1.member, list.head);
    TEST_ASSERT_EQUAL_PTR(&node3.member, list.tail);
    TEST_ASSERT_EQUAL_PTR(&node2.member, node1.member.next);
    TEST_ASSERT_EQUAL_PTR(&node3.member, node2.member.next);
}

void setUp(void) {}

void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_popcount_emulated);
    RUN_TEST(test_popcount_intrinsics);
    RUN_TEST(test_crc_add);
    RUN_TEST(test_crc_add_chain);
    RUN_TEST(test_bytes_chain);
    RUN_TEST(test_list);
    RUN_TEST(test_list_enlist_tail);
    return UNITY_END();
}
