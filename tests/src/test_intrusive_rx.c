// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal Development Team.

#include "canard.c" // NOLINT(bugprone-suspicious-include)
#include "helpers.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// All CAN IDs and expected values in this file are computed by hand from the protocol bit-field specifications.
// The test never reuses expressions from the implementation (like PRIO_SHIFT, CANARD_SERVICE_ID_MAX as masks)
// to derive expected results; everything is a hardcoded literal.

// =====================================================================================================================
// Test 1: Empty payload returns 0 and zeroes both outputs.
static void test_rx_parse_empty_payload(void)
{
    frame_t v0;
    frame_t v1;
    memset(&v0, 0xA5, sizeof(v0));
    memset(&v1, 0xA5, sizeof(v1));
    const canard_bytes_t pl  = { 0, NULL };
    const byte_t         ret = rx_parse(0x00000000UL, pl, &v0, &v1);
    TEST_ASSERT_EQUAL_UINT8(0, ret);
    // Outputs shall be zeroed even on the early-return path.
    TEST_ASSERT_EQUAL_UINT32(0, v0.port_id);
    TEST_ASSERT_EQUAL_UINT32(0, v1.port_id);
    TEST_ASSERT_EQUAL_HEX8(0, v0.src);
    TEST_ASSERT_EQUAL_HEX8(0, v1.src);
}

// =====================================================================================================================
// Test 2: v1.1 message golden values.
// v1.1 message CAN ID: (prio << 26) | (subject << 8) | (1 << 7) | src
static void test_rx_parse_v1_1_message_golden(void)
{
    frame_t v0;
    frame_t v1;
    // Mid: prio=3, subject=1234, src=42. CAN ID = 0x0C04D2AA.
    {
        const byte_t         d[] = { 0x11, 0x22, 0xE7 }; // v1 single-frame tail: SOT=1 EOT=1 toggle=1 tid=7
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x0C04D2AAUL, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(2, ret);
        TEST_ASSERT_EQUAL_INT(format_1v1_message, v1.format);
        TEST_ASSERT_EQUAL_UINT32(1234, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(0xFF, v1.dst);
        TEST_ASSERT_EQUAL_HEX8(42, v1.src);
        TEST_ASSERT_EQUAL_INT(canard_prio_high, v1.priority);
        TEST_ASSERT_EQUAL_UINT8(7, v1.transfer_id);
        TEST_ASSERT_TRUE(v1.start);
        TEST_ASSERT_TRUE(v1.end);
        TEST_ASSERT_TRUE(v1.toggle);
        TEST_ASSERT_EQUAL_size_t(2, v1.payload.size);
        TEST_ASSERT_EQUAL_PTR(d, v1.payload.data);
    }
    // Min: prio=0, subject=0, src=0. CAN ID = 0x00000080.
    {
        const byte_t         d[] = { 0xE0 }; // tid=0
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(2, rx_parse(0x00000080UL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(format_1v1_message, v1.format);
        TEST_ASSERT_EQUAL_UINT32(0, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(0xFF, v1.dst);
        TEST_ASSERT_EQUAL_HEX8(0, v1.src);
        TEST_ASSERT_EQUAL_INT(canard_prio_exceptional, v1.priority);
        TEST_ASSERT_EQUAL_UINT8(0, v1.transfer_id);
    }
    // Max: prio=7, subject=131071 (0x1FFFF), src=127. CAN ID = 0x1DFFFFFF.
    {
        const byte_t         d[] = { 0xFF }; // 0xE0|31 → tid=31
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(2, rx_parse(0x1DFFFFFFUL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(format_1v1_message, v1.format);
        TEST_ASSERT_EQUAL_UINT32(131071UL, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(0xFF, v1.dst);
        TEST_ASSERT_EQUAL_HEX8(127, v1.src);
        TEST_ASSERT_EQUAL_INT(canard_prio_optional, v1.priority);
        TEST_ASSERT_EQUAL_UINT8(31, v1.transfer_id);
    }
}

// =====================================================================================================================
// Test 3: v1.0 message golden values.
// v1.0 message CAN ID: (prio << 26) | (reserved22:21 << 21) | (subject << 8) | src   [bit7=0, bit25=0]
static void test_rx_parse_v1_0_message_golden(void)
{
    frame_t v0;
    frame_t v1;
    // Normal: prio=4, subject=42, src=11, reserved=11b. CAN ID = 0x10602A0B.
    {
        const byte_t         d[] = { 0xAA, 0xE5 }; // v1 single, tid=5
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(2, rx_parse(0x10602A0BUL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(format_1v0_message, v1.format);
        TEST_ASSERT_EQUAL_UINT32(42, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(0xFF, v1.dst);
        TEST_ASSERT_EQUAL_HEX8(11, v1.src);
        TEST_ASSERT_EQUAL_INT(canard_prio_nominal, v1.priority);
    }
    // Max subject: prio=0, subject=8191, src=1, reserved=11b. CAN ID = 0x007FFF01.
    {
        const byte_t         d[] = { 0xE0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(2, rx_parse(0x007FFF01UL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(format_1v0_message, v1.format);
        TEST_ASSERT_EQUAL_UINT32(8191, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(1, v1.src);
    }
    // Anonymous: prio=2, subject=100, bit24=1, src=0x55(pseudo), reserved=11b. CAN ID = 0x09606455.
    {
        const byte_t         d[] = { 0xE3 }; // v1 single, tid=3
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(2, rx_parse(0x09606455UL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(format_1v0_message, v1.format);
        TEST_ASSERT_EQUAL_UINT32(100, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(0xFF, v1.src); // anonymous
        TEST_ASSERT_EQUAL_INT(canard_prio_fast, v1.priority);
    }
}

// =====================================================================================================================
// Test 4: v1.0 service golden values.
// v1.0 service CAN ID: (prio << 26) | (1 << 25) | (req?1:0 << 24) | (svc_id << 14) | (dst << 7) | src
static void test_rx_parse_v1_0_service_golden(void)
{
    frame_t v0;
    frame_t v1;
    // Request: prio=4, svc_id=430, dst=24, src=11. CAN ID = 0x136B8C0B.
    {
        const byte_t         d[] = { 0xBB, 0xE1 }; // v1 single, tid=1
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(2, rx_parse(0x136B8C0BUL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(format_1v0_request, v1.format);
        TEST_ASSERT_EQUAL_UINT32(430, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(24, v1.dst);
        TEST_ASSERT_EQUAL_HEX8(11, v1.src);
        TEST_ASSERT_EQUAL_INT(canard_prio_nominal, v1.priority);
    }
    // Response min: prio=0, svc_id=0, dst=1, src=127. CAN ID = 0x020000FF.
    {
        const byte_t         d[] = { 0xE0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(2, rx_parse(0x020000FFUL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(format_1v0_response, v1.format);
        TEST_ASSERT_EQUAL_UINT32(0, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(1, v1.dst);
        TEST_ASSERT_EQUAL_HEX8(127, v1.src);
        TEST_ASSERT_EQUAL_INT(canard_prio_exceptional, v1.priority);
    }
    // Request max: prio=7, svc_id=511, dst=127, src=126. CAN ID = 0x1F7FFFFE.
    {
        const byte_t         d[] = { 0xFF }; // v1 single, tid=31
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(2, rx_parse(0x1F7FFFFEUL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(format_1v0_request, v1.format);
        TEST_ASSERT_EQUAL_UINT32(511, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(127, v1.dst);
        TEST_ASSERT_EQUAL_HEX8(126, v1.src);
        TEST_ASSERT_EQUAL_INT(canard_prio_optional, v1.priority);
    }
}

// =====================================================================================================================
// Test 5: v0.1 message golden values.
// v0 message CAN ID: (prio << 26) | (3 << 24) | (type_id << 8) | src   [bit7=0]
static void test_rx_parse_v0_message_golden(void)
{
    frame_t v0;
    frame_t v1;
    // Normal: prio=4, type_id=0x040A, src=1. CAN ID = 0x13040A01.
    {
        const byte_t         d[] = { 0x55, 0xC2 }; // v0 single: SOT=1 EOT=1 toggle=0 tid=2
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(1, rx_parse(0x13040A01UL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(format_0v1_message, v0.format);
        TEST_ASSERT_EQUAL_UINT32(0x040A, v0.port_id);
        TEST_ASSERT_EQUAL_HEX8(0xFF, v0.dst);
        TEST_ASSERT_EQUAL_HEX8(1, v0.src);
        TEST_ASSERT_EQUAL_INT(canard_prio_nominal, v0.priority);
    }
    // Anonymous: same frame but src=0. CAN ID = 0x13040A00.
    {
        const byte_t         d[] = { 0xC0 }; // v0 single, tid=0
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(1, rx_parse(0x13040A00UL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(format_0v1_message, v0.format);
        TEST_ASSERT_EQUAL_UINT32(0x040A, v0.port_id);
        TEST_ASSERT_EQUAL_HEX8(0xFF, v0.src); // anonymous
    }
    // Max: prio=7, type_id=0xFFFF, src=127. CAN ID = 0x1FFFFF7F.
    {
        const byte_t         d[] = { 0xDF }; // v0 single: 0xC0|31, tid=31
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(1, rx_parse(0x1FFFFF7FUL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(format_0v1_message, v0.format);
        TEST_ASSERT_EQUAL_UINT32(0xFFFF, v0.port_id);
        TEST_ASSERT_EQUAL_HEX8(127, v0.src);
        TEST_ASSERT_EQUAL_INT(canard_prio_optional, v0.priority);
    }
}

// =====================================================================================================================
// Test 6: v0.1 service golden values.
// v0 service CAN ID: (((prio<<2)|3) << 24) | (type_id << 16) | (req?1<<15:0) | (dst << 8) | (1 << 7) | src
static void test_rx_parse_v0_service_golden(void)
{
    frame_t v0;
    frame_t v1;
    // Request: prio=4, type_id=0x37, dst=24, src=11. CAN ID = 0x1337988B.
    {
        const byte_t         d[] = { 0x42, 0xC4 }; // v0 single, tid=4
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(1, rx_parse(0x1337988BUL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(format_0v1_request, v0.format);
        TEST_ASSERT_EQUAL_UINT32(0x37, v0.port_id);
        TEST_ASSERT_EQUAL_HEX8(24, v0.dst);
        TEST_ASSERT_EQUAL_HEX8(11, v0.src);
        TEST_ASSERT_EQUAL_INT(canard_prio_nominal, v0.priority);
    }
    // Response: prio=0, type_id=1, dst=1, src=1. CAN ID = 0x03010181.
    {
        const byte_t         d[] = { 0xC0 }; // v0 single, tid=0
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(1, rx_parse(0x03010181UL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(format_0v1_response, v0.format);
        TEST_ASSERT_EQUAL_UINT32(1, v0.port_id);
        TEST_ASSERT_EQUAL_HEX8(1, v0.dst);
        TEST_ASSERT_EQUAL_HEX8(1, v0.src);
        TEST_ASSERT_EQUAL_INT(canard_prio_exceptional, v0.priority);
    }
}

// =====================================================================================================================
// Test 7: v1.0 frames with reserved bit 23 set are rejected.
static void test_rx_parse_v1_0_reserved_bit23_reject(void)
{
    frame_t v0;
    frame_t v1;
    // v1.0 service: 0x136B8C0B | 0x00800000 = 0x13EB8C0B. v1 tail → is_v0=false. bit23 → is_v1=false.
    {
        const byte_t         d[] = { 0xE0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(0, rx_parse(0x13EB8C0BUL, pl, &v0, &v1));
    }
    // v1.0 message: 0x10602A0B | 0x00800000 = 0x10E02A0B. v1 tail → is_v0=false. bit23 → is_v1=false.
    {
        const byte_t         d[] = { 0xE0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(0, rx_parse(0x10E02A0BUL, pl, &v0, &v1));
    }
}

// =====================================================================================================================
// Test 8: v0 service with src=0 or dst=0 is rejected (0 reserved for anonymous/broadcast in v0).
static void test_rx_parse_v0_service_zero_node_reject(void)
{
    frame_t v0;
    frame_t v1;
    // src=0: CAN ID 0x13379880 → v0 service with src=0 → rejected. v0 tail → is_v1=false. Return=0.
    {
        const byte_t         d[] = { 0xC0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(0, rx_parse(0x13379880UL, pl, &v0, &v1));
    }
    // dst=0: CAN ID 0x1337808B → v0 service with dst=0 → rejected. v0 tail → is_v1=false. Return=0.
    {
        const byte_t         d[] = { 0xC0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(0, rx_parse(0x1337808BUL, pl, &v0, &v1));
    }
}

// =====================================================================================================================
// Test 9: Version detection via SOT+toggle in the tail byte.
static void test_rx_parse_version_detection(void)
{
    frame_t v0;
    frame_t v1;
    // CAN ID valid for both versions: v1.0 message / v0 message (bit7=0, bit25=0, bit23=0).
    const uint32_t can_id = 0x00002A01UL; // prio=0, subject=42 (v1.0), type_id=42 (v0), src=1
    // SOT=1 toggle=1 → v1 only
    {
        const byte_t         d[] = { 0xE0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(2, rx_parse(can_id, pl, &v0, &v1));
    }
    // SOT=1 toggle=0 → v0 only
    {
        const byte_t         d[] = { 0xC0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(1, rx_parse(can_id, pl, &v0, &v1));
    }
    // SOT=0 toggle=0 → both (8 bytes needed: non-last frames require full MTU)
    {
        const byte_t         d[] = { 1, 2, 3, 4, 5, 6, 7, 0x00 };
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(3, rx_parse(can_id, pl, &v0, &v1));
    }
    // SOT=0 toggle=1 → both (8 bytes needed: non-last frames require full MTU)
    {
        const byte_t         d[] = { 1, 2, 3, 4, 5, 6, 7, 0x20 };
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(3, rx_parse(can_id, pl, &v0, &v1));
    }
}

// =====================================================================================================================
// Test 10: Payload pointer and size are forwarded correctly.
static void test_rx_parse_payload_handling(void)
{
    frame_t        v0;
    frame_t        v1;
    const uint32_t can_id = 0x00000080UL; // v1.1 message, prio=0, subject=0, src=0
    // Size 1 (tail byte only → effective payload is 0 bytes)
    {
        const byte_t         d[] = { 0xE0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        rx_parse(can_id, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_size_t(0, v1.payload.size);
        TEST_ASSERT_EQUAL_PTR(d, v1.payload.data);
    }
    // Size 2
    {
        const byte_t         d[] = { 0xAA, 0xE0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        rx_parse(can_id, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_size_t(1, v1.payload.size);
        TEST_ASSERT_EQUAL_PTR(d, v1.payload.data);
    }
    // Size 8 (classic CAN)
    {
        const byte_t         d[] = { 1, 2, 3, 4, 5, 6, 7, 0xE0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        rx_parse(can_id, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_size_t(7, v1.payload.size);
        TEST_ASSERT_EQUAL_PTR(d, v1.payload.data);
    }
    // Size 64 (CAN FD)
    {
        byte_t d[64];
        memset(d, 0xAA, sizeof(d));
        d[63]                   = 0xE0;
        const canard_bytes_t pl = { sizeof(d), d };
        rx_parse(can_id, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_size_t(63, v1.payload.size);
        TEST_ASSERT_EQUAL_PTR(d, v1.payload.data);
    }
}

// =====================================================================================================================
// Test 11: Exhaustive tail-byte field mapping for all SOT/EOT/toggle combinations and TID boundary values.
static void test_rx_parse_tail_byte_exhaustive(void)
{
    frame_t        v0;
    frame_t        v1;
    const uint32_t can_id = 0x00002A01UL; // valid for both versions (message, bit7=0)

    // All 8 SOT/EOT/toggle combinations. Each uses its own data array to avoid cppcheck false positives.
    { // SOT=0 EOT=0 toggle=0 → both versions, check v1 (8 bytes: non-last requires full MTU)
        const byte_t         d[] = { 1, 2, 3, 4, 5, 6, 7, 0x00 };
        const canard_bytes_t pl  = { sizeof(d), d };
        rx_parse(can_id, pl, &v0, &v1);
        TEST_ASSERT_FALSE(v1.start);
        TEST_ASSERT_FALSE(v1.end);
        TEST_ASSERT_FALSE(v1.toggle);
    }
    { // SOT=0 EOT=0 toggle=1 (8 bytes: non-last requires full MTU)
        const byte_t         d[] = { 1, 2, 3, 4, 5, 6, 7, 0x20 };
        const canard_bytes_t pl  = { sizeof(d), d };
        rx_parse(can_id, pl, &v0, &v1);
        TEST_ASSERT_FALSE(v1.start);
        TEST_ASSERT_FALSE(v1.end);
        TEST_ASSERT_TRUE(v1.toggle);
    }
    { // SOT=0 EOT=1 toggle=0
        const byte_t         d[] = { 0x42, 0x40 };
        const canard_bytes_t pl  = { sizeof(d), d };
        rx_parse(can_id, pl, &v0, &v1);
        TEST_ASSERT_FALSE(v1.start);
        TEST_ASSERT_TRUE(v1.end);
        TEST_ASSERT_FALSE(v1.toggle);
    }
    { // SOT=0 EOT=1 toggle=1
        const byte_t         d[] = { 0x42, 0x60 };
        const canard_bytes_t pl  = { sizeof(d), d };
        rx_parse(can_id, pl, &v0, &v1);
        TEST_ASSERT_FALSE(v1.start);
        TEST_ASSERT_TRUE(v1.end);
        TEST_ASSERT_TRUE(v1.toggle);
    }
    { // SOT=1 EOT=0 toggle=0 → v0 only (8 bytes: non-last requires full MTU)
        const byte_t         d[] = { 1, 2, 3, 4, 5, 6, 7, 0x80 };
        const canard_bytes_t pl  = { sizeof(d), d };
        rx_parse(can_id, pl, &v0, &v1);
        TEST_ASSERT_TRUE(v0.start);
        TEST_ASSERT_FALSE(v0.end);
        TEST_ASSERT_FALSE(v0.toggle);
    }
    { // SOT=1 EOT=0 toggle=1 → v1 only (8 bytes: non-last requires full MTU)
        const byte_t         d[] = { 1, 2, 3, 4, 5, 6, 7, 0xA0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        rx_parse(can_id, pl, &v0, &v1);
        TEST_ASSERT_TRUE(v1.start);
        TEST_ASSERT_FALSE(v1.end);
        TEST_ASSERT_TRUE(v1.toggle);
    }
    { // SOT=1 EOT=1 toggle=0 → v0 only
        const byte_t         d[] = { 0x42, 0xC0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        rx_parse(can_id, pl, &v0, &v1);
        TEST_ASSERT_TRUE(v0.start);
        TEST_ASSERT_TRUE(v0.end);
        TEST_ASSERT_FALSE(v0.toggle);
    }
    { // SOT=1 EOT=1 toggle=1 → v1 only
        const byte_t         d[] = { 0x42, 0xE0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        rx_parse(can_id, pl, &v0, &v1);
        TEST_ASSERT_TRUE(v1.start);
        TEST_ASSERT_TRUE(v1.end);
        TEST_ASSERT_TRUE(v1.toggle);
    }
    // Transfer-ID boundary values with v1 single-frame tail (0xE0 | tid).
    { // tid=0
        const byte_t         d[] = { 0x42, 0xE0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        rx_parse(can_id, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(0, v1.transfer_id);
    }
    { // tid=1
        const byte_t         d[] = { 0x42, 0xE1 };
        const canard_bytes_t pl  = { sizeof(d), d };
        rx_parse(can_id, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(1, v1.transfer_id);
    }
    { // tid=15
        const byte_t         d[] = { 0x42, 0xEF };
        const canard_bytes_t pl  = { sizeof(d), d };
        rx_parse(can_id, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(15, v1.transfer_id);
    }
    { // tid=16
        const byte_t         d[] = { 0x42, 0xF0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        rx_parse(can_id, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(16, v1.transfer_id);
    }
    { // tid=31
        const byte_t         d[] = { 0x42, 0xFF };
        const canard_bytes_t pl  = { sizeof(d), d };
        rx_parse(can_id, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(31, v1.transfer_id);
    }
}

// =====================================================================================================================
// Test 12: Non-first frames where the same CAN ID produces valid but different results for v0 and v1.
static void test_rx_parse_cross_version_ambiguity(void)
{
    frame_t v0;
    frame_t v1;
    // Non-first tail: SOT=0 → both versions attempted.
    const byte_t nf = 0x05; // SOT=0 EOT=0 toggle=0 tid=5

    // v1.1 message CAN ID 0x0C04D2AA simultaneously parses as v0 service (bit7=1).
    {
        const byte_t         d[] = { 1, 2, 3, 4, 5, 6, 7, nf };
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x0C04D2AAUL, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(3, ret);
        // v1: v1.1 message, subject=1234, src=42
        TEST_ASSERT_EQUAL_INT(format_1v1_message, v1.format);
        TEST_ASSERT_EQUAL_UINT32(1234, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(42, v1.src);
        // v0: service request, type_id=4, dst=82, src=42
        TEST_ASSERT_EQUAL_INT(format_0v1_request, v0.format);
        TEST_ASSERT_EQUAL_UINT32(4, v0.port_id);
        TEST_ASSERT_EQUAL_HEX8(82, v0.dst);
        TEST_ASSERT_EQUAL_HEX8(42, v0.src);
    }
    // All-ones 0x1FFFFFFF: v1 rejected (bit23 in service path), v0 parses as service.
    {
        const byte_t         d[] = { 1, 2, 3, 4, 5, 6, 7, nf };
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x1FFFFFFFUL, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(1, ret);
        TEST_ASSERT_EQUAL_INT(format_0v1_request, v0.format);
        TEST_ASSERT_EQUAL_UINT32(0xFF, v0.port_id);
        TEST_ASSERT_EQUAL_HEX8(127, v0.dst);
        TEST_ASSERT_EQUAL_HEX8(127, v0.src);
    }
    // All-zeros 0x00000000: v1 parses as v1.0 message; v0 anonymous (src=0) rejected for non-first frame
    // because anonymous can only be single-frame (start && end required, but start=false here).
    {
        const byte_t         d[] = { 1, 2, 3, 4, 5, 6, 7, nf };
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x00000000UL, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(2, ret); // v1 only; v0 anonymous rejected
        // v1: v1.0 message (bit7=0), port_id=0, src=0
        TEST_ASSERT_EQUAL_INT(format_1v0_message, v1.format);
        TEST_ASSERT_EQUAL_UINT32(0, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(0, v1.src);
    }
}

// =====================================================================================================================
// Test 13: Adjacent bit fields do not bleed into each other.
static void test_rx_parse_bit_field_boundaries(void)
{
    frame_t v0;
    frame_t v1;
    // v1.0 service: max svc_id=511 with dst=0 and src=0 → verify dst and src read 0.
    // CAN ID: (0<<26)|(1<<25)|(0<<24)|(511<<14)|(0<<7)|0 = 0x027FC000
    {
        const byte_t         d[] = { 0xE0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(2, rx_parse(0x027FC000UL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(format_1v0_response, v1.format);
        TEST_ASSERT_EQUAL_UINT32(511, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(0, v1.dst);
        TEST_ASSERT_EQUAL_HEX8(0, v1.src);
    }
    // v1.0 service: max dst=127 with svc_id=0 and src=0 → verify svc_id and src read 0.
    // CAN ID: (0<<26)|(1<<25)|(0<<24)|(0<<14)|(127<<7)|0 = 0x02003F80
    {
        const byte_t         d[] = { 0xE0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(2, rx_parse(0x02003F80UL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(format_1v0_response, v1.format);
        TEST_ASSERT_EQUAL_UINT32(0, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(127, v1.dst);
        TEST_ASSERT_EQUAL_HEX8(0, v1.src);
    }
    // v0 service: max type_id=0xFF with dst=1, req=0 → verify req reads 0 and dst is correct.
    // CAN ID: ((0<<2)|3)<<24 | (0xFF<<16) | (0<<15) | (1<<8) | (1<<7) | 1 = 0x03FF0181
    {
        const byte_t         d[] = { 0xC0 }; // v0 single
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(1, rx_parse(0x03FF0181UL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(format_0v1_response, v0.format); // req=0
        TEST_ASSERT_EQUAL_UINT32(0xFF, v0.port_id);
        TEST_ASSERT_EQUAL_HEX8(1, v0.dst);
        TEST_ASSERT_EQUAL_HEX8(1, v0.src);
    }
}

// =====================================================================================================================
// Test 14: v1.1 does NOT reject bit 23 (unlike v1.0).
static void test_rx_parse_v1_1_accepts_bit23(void)
{
    frame_t v0;
    frame_t v1;
    // Subject 0x18000: bit 23 of CAN ID is set because (0x18000 << 8) sets bit 23.
    // CAN ID: (0<<26) | (0x18000<<8) | (1<<7) | 0 = 0x01800080
    {
        const byte_t         d[] = { 0xE5 }; // v1 single, tid=5
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x01800080UL, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(2, ret); // accepted despite bit 23
        TEST_ASSERT_EQUAL_INT(format_1v1_message, v1.format);
        TEST_ASSERT_EQUAL_UINT32(0x18000UL, v1.port_id);
    }
    // Max subject 0x1FFFF also sets bit 23.
    // CAN ID: (0<<26) | (0x1FFFF<<8) | (1<<7) | 0 = 0x01FFFF80
    {
        const byte_t         d[] = { 0xE0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x01FFFF80UL, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(2, ret);
        TEST_ASSERT_EQUAL_INT(format_1v1_message, v1.format);
        TEST_ASSERT_EQUAL_UINT32(0x1FFFFUL, v1.port_id);
    }
}

// =====================================================================================================================
// Test 15: v1.0 message reserved bits 22:21 are masked out and do not affect the extracted subject-ID.
static void test_rx_parse_v1_0_message_ignores_reserved_bits_22_21(void)
{
    frame_t              v0;
    frame_t              v1;
    const byte_t         d[] = { 0xE0 };
    const canard_bytes_t pl  = { sizeof(d), d };
    // bits 22:21 = 00: CAN ID = (0<<26)|(0<<21)|(42<<8)|1 = 0x00002A01
    rx_parse(0x00002A01UL, pl, &v0, &v1);
    TEST_ASSERT_EQUAL_INT(format_1v0_message, v1.format);
    TEST_ASSERT_EQUAL_UINT32(42, v1.port_id);
    // bits 22:21 = 11: CAN ID = (0<<26)|(3<<21)|(42<<8)|1 = 0x00602A01
    rx_parse(0x00602A01UL, pl, &v0, &v1);
    TEST_ASSERT_EQUAL_INT(format_1v0_message, v1.format);
    TEST_ASSERT_EQUAL_UINT32(42, v1.port_id); // same despite different reserved bits
}

// =====================================================================================================================
// Test 16: Non-first frame produces valid distinct results for both v0 and v1 simultaneously.
static void test_rx_parse_non_first_dual_output(void)
{
    frame_t v0;
    frame_t v1;
    // CAN ID 0x0C04D2AA: v1.1 message (bit7=1) / v0 service (bit7=1).
    // Non-first tail: SOT=0 EOT=0 toggle=0 tid=5. 8 bytes needed for non-last MTU.
    const byte_t         d[] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x05 };
    const canard_bytes_t pl  = { sizeof(d), d };
    const byte_t         ret = rx_parse(0x0C04D2AAUL, pl, &v0, &v1);
    TEST_ASSERT_EQUAL_UINT8(3, ret);

    // v1 output fully populated: v1.1 message, subject=1234, src=42.
    TEST_ASSERT_EQUAL_INT(format_1v1_message, v1.format);
    TEST_ASSERT_EQUAL_UINT32(1234, v1.port_id);
    TEST_ASSERT_EQUAL_HEX8(0xFF, v1.dst);
    TEST_ASSERT_EQUAL_HEX8(42, v1.src);
    TEST_ASSERT_EQUAL_INT(canard_prio_high, v1.priority);
    TEST_ASSERT_EQUAL_UINT8(5, v1.transfer_id);
    TEST_ASSERT_FALSE(v1.start);
    TEST_ASSERT_FALSE(v1.end);
    TEST_ASSERT_FALSE(v1.toggle);
    TEST_ASSERT_EQUAL_size_t(7, v1.payload.size);
    TEST_ASSERT_EQUAL_PTR(d, v1.payload.data);

    // v0 output fully populated: v0 service request, type_id=4, dst=82, src=42.
    TEST_ASSERT_EQUAL_INT(format_0v1_request, v0.format);
    TEST_ASSERT_EQUAL_UINT32(4, v0.port_id);
    TEST_ASSERT_EQUAL_HEX8(82, v0.dst);
    TEST_ASSERT_EQUAL_HEX8(42, v0.src);
    TEST_ASSERT_EQUAL_INT(canard_prio_high, v0.priority);
    TEST_ASSERT_EQUAL_UINT8(5, v0.transfer_id);
    TEST_ASSERT_FALSE(v0.start);
    TEST_ASSERT_FALSE(v0.end);
    TEST_ASSERT_FALSE(v0.toggle);
    TEST_ASSERT_EQUAL_size_t(7, v0.payload.size);
    TEST_ASSERT_EQUAL_PTR(d, v0.payload.data);
}

// =====================================================================================================================
// Test 17: Payload validation — exercises the payload_ok computation.
static void test_rx_parse_payload_validation(void)
{
    frame_t v0;
    frame_t v1;
    // CAN ID valid for both versions (message, bit7=0, bit25=0, bit23=0).
    const uint32_t can_id = 0x00002A01UL;

    // Non-last frame under MTU → rejected (payload_raw.size < 8 when end=false).
    {
        const byte_t         d[] = { 1, 2, 3, 4, 5, 6, 0x05 }; // 7 bytes, tail: SOT=0 EOT=0 toggle=0 tid=5
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(0, rx_parse(can_id, pl, &v0, &v1));
    }
    // Non-last frame at exact MTU → accepted.
    {
        const byte_t         d[] = { 1, 2, 3, 4, 5, 6, 7, 0x05 }; // 8 bytes
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(3, rx_parse(can_id, pl, &v0, &v1));
    }
    // Last frame of multi-frame with empty payload → rejected (payload.size=0, not single-frame).
    {
        const byte_t         d[] = { 0x40 }; // SOT=0 EOT=1 toggle=0 tid=0
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(0, rx_parse(can_id, pl, &v0, &v1));
    }
    // Single-frame with empty payload → accepted (start && end, so second clause passes).
    {
        const byte_t         d[] = { 0xC0 }; // SOT=1 EOT=1 toggle=0 tid=0 → v0 only
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(1, rx_parse(can_id, pl, &v0, &v1));
    }
}

// =====================================================================================================================
// Test 18: Anonymous multi-frame transfers are rejected.
static void test_rx_parse_anonymous_multi_frame_reject(void)
{
    frame_t v0;
    frame_t v1;

    // v1.0 anonymous multi-frame → v1 rejected (anonymous requires start && end).
    // CAN ID 0x09606455: v1.0 message, bit24=1 (anonymous), prio=2, subject=100.
    {
        const byte_t         d[] = { 1, 2, 3, 4, 5, 6, 7, 0xA0 }; // SOT=1 EOT=0 toggle=1 → v1 first frame
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x09606455UL, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(0, ret); // v1 rejected (anonymous multi-frame); v0 excluded (toggle=1)
    }
    // v0 anonymous multi-frame → v0 rejected (anonymous requires start && end).
    // CAN ID 0x13040A00: v0 message, src=0 (anonymous), prio=4, type_id=0x040A.
    {
        const byte_t         d[] = { 1, 2, 3, 4, 5, 6, 7, 0x80 }; // SOT=1 EOT=0 toggle=0 → v0 first frame
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x13040A00UL, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(0, ret); // v0 rejected (anonymous multi-frame); v1 excluded (toggle=0)
    }
    // v1.0 anonymous single-frame → accepted (sanity check).
    {
        const byte_t         d[] = { 0xE0 }; // SOT=1 EOT=1 toggle=1 tid=0
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(2, rx_parse(0x09606455UL, pl, &v0, &v1));
    }
    // v0 anonymous single-frame → accepted (sanity check).
    {
        const byte_t         d[] = { 0xC0 }; // SOT=1 EOT=1 toggle=0 tid=0
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(1, rx_parse(0x13040A00UL, pl, &v0, &v1));
    }
}

// =====================================================================================================================
// Helpers for rx_session_scan and rx_slot_write_payload tests.

typedef struct
{
    canard_t                 canard;
    canard_subscription_t    sub;
    instrumented_allocator_t alloc_slot;
    instrumented_allocator_t alloc_payload;
} test_rx_context_t;

static void test_rx_context_init(test_rx_context_t* const ctx, const size_t extent, const canard_us_t tid_timeout)
{
    memset(ctx, 0, sizeof(*ctx));
    instrumented_allocator_new(&ctx->alloc_slot);
    instrumented_allocator_new(&ctx->alloc_payload);
    ctx->canard.mem.rx_slot      = instrumented_allocator_make_resource(&ctx->alloc_slot);
    ctx->canard.mem.rx_payload   = instrumented_allocator_make_resource(&ctx->alloc_payload);
    ctx->sub.owner               = &ctx->canard;
    ctx->sub.extent              = extent;
    ctx->sub.transfer_id_timeout = tid_timeout;
}

// Allocates an rx_slot_t and optionally its payload buffer.
// For single_frame slots, payload.size equals payload_alloc_size. For multi-frame, it starts at 0.
static rx_slot_t* make_test_slot(const test_rx_context_t* const ctx,
                                 const canard_us_t              timestamp,
                                 const size_t                   payload_alloc_size,
                                 const bool                     single_frame)
{
    rx_slot_t* s = (rx_slot_t*)mem_alloc(ctx->canard.mem.rx_slot, sizeof(rx_slot_t));
    TEST_PANIC_UNLESS(s != NULL);
    memset(s, 0, sizeof(*s));
    s->timestamp    = timestamp;
    s->single_frame = single_frame;
    if (payload_alloc_size > 0) {
        s->payload.data = mem_alloc(ctx->canard.mem.rx_payload, payload_alloc_size);
        TEST_PANIC_UNLESS(s->payload.data != NULL);
        s->payload.size = single_frame ? payload_alloc_size : 0;
    }
    return s;
}

// =====================================================================================================================
// Test 19: rx_session_scan — purges stale slots and returns count of in-progress slots remaining.
static void test_rx_session_scan(void)
{
    // tid_timeout=40*MEGA > RX_SESSION_TIMEOUT=30*MEGA, so deadline = now - tid_timeout.
    // With now = 40*MEGA + 4000: deadline = 4000. Stale if timestamp < 4000.
    test_rx_context_t ctx;
    rx_session_t      ses;

    // 1) All slots NULL → return 0, ses.timestamp unchanged.
    {
        test_rx_context_init(&ctx, 64, 40 * MEGA);
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        TEST_ASSERT_EQUAL_size_t(0, rx_session_scan(&ses, 40 * MEGA + 4000));
        TEST_ASSERT_EQUAL_INT64(0, ses.timestamp);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_slot.allocated_fragments);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_payload.allocated_fragments);
    }
    // 2) All fresh, total_payload_size=0 → return 0 (no in-progress), ses.timestamp updated.
    {
        test_rx_context_init(&ctx, 64, 40 * MEGA);
        memset(&ses, 0, sizeof(ses));
        ses.owner    = &ctx.sub;
        ses.slots[0] = make_test_slot(&ctx, 4500, 0, true);
        ses.slots[3] = make_test_slot(&ctx, 4800, 0, true);
        ses.slots[7] = make_test_slot(&ctx, 4001, 0, true);
        TEST_ASSERT_EQUAL_size_t(0, rx_session_scan(&ses, 40 * MEGA + 4000));
        TEST_ASSERT_EQUAL_INT64(4800, ses.timestamp);
        TEST_ASSERT_EQUAL_UINT64(0, ctx.alloc_slot.count_free);
        rx_slot_destroy(&ctx.sub, ses.slots[0]);
        rx_slot_destroy(&ctx.sub, ses.slots[3]);
        rx_slot_destroy(&ctx.sub, ses.slots[7]);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_slot.allocated_fragments);
    }
    // 3) All stale → return 0, both destroyed, ses.timestamp unchanged (no fresh slots).
    {
        test_rx_context_init(&ctx, 64, 40 * MEGA);
        memset(&ses, 0, sizeof(ses));
        ses.owner    = &ctx.sub;
        ses.slots[1] = make_test_slot(&ctx, 3999, 0, true);
        ses.slots[5] = make_test_slot(&ctx, 2000, 0, true);
        TEST_ASSERT_EQUAL_size_t(0, rx_session_scan(&ses, 40 * MEGA + 4000));
        TEST_ASSERT_NULL(ses.slots[1]);
        TEST_ASSERT_NULL(ses.slots[5]);
        TEST_ASSERT_EQUAL_INT64(0, ses.timestamp);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_slot.allocated_fragments);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_slot.allocated_bytes);
    }
    // 4) Mix of fresh (with in-progress), stale, NULL.
    {
        test_rx_context_init(&ctx, 64, 40 * MEGA);
        memset(&ses, 0, sizeof(ses));
        ses.owner    = &ctx.sub;
        ses.slots[0] = make_test_slot(&ctx, 3999, 0, true);  // stale
        ses.slots[2] = make_test_slot(&ctx, 1000, 0, true);  // stale
        ses.slots[1] = make_test_slot(&ctx, 4500, 0, true);  // fresh, in-progress
        ses.slots[1]->total_payload_size = 10;
        ses.slots[4] = make_test_slot(&ctx, 4200, 0, true);  // fresh, idle
        ses.slots[6] = make_test_slot(&ctx, 4000, 0, true);  // boundary, NOT stale
        TEST_ASSERT_EQUAL_size_t(1, rx_session_scan(&ses, 40 * MEGA + 4000));
        TEST_ASSERT_NULL(ses.slots[0]);
        TEST_ASSERT_NULL(ses.slots[2]);
        TEST_ASSERT_NOT_NULL(ses.slots[1]);
        TEST_ASSERT_NOT_NULL(ses.slots[4]);
        TEST_ASSERT_NOT_NULL(ses.slots[6]);
        TEST_ASSERT_EQUAL_INT64(4500, ses.timestamp);
        rx_slot_destroy(&ctx.sub, ses.slots[1]);
        rx_slot_destroy(&ctx.sub, ses.slots[4]);
        rx_slot_destroy(&ctx.sub, ses.slots[6]);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_slot.allocated_fragments);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_slot.allocated_bytes);
    }
    // 5) Boundary: timestamp == deadline (4000). Condition is strict '<', so NOT stale.
    {
        test_rx_context_init(&ctx, 64, 40 * MEGA);
        memset(&ses, 0, sizeof(ses));
        ses.owner    = &ctx.sub;
        ses.slots[0] = make_test_slot(&ctx, 4000, 0, true);
        TEST_ASSERT_EQUAL_size_t(0, rx_session_scan(&ses, 40 * MEGA + 4000));
        TEST_ASSERT_NOT_NULL(ses.slots[0]);
        TEST_ASSERT_EQUAL_INT64(4000, ses.timestamp);
        rx_slot_destroy(&ctx.sub, ses.slots[0]);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_slot.allocated_fragments);
    }
    // 6) Boundary: timestamp == deadline - 1 (3999). Stale and destroyed.
    {
        test_rx_context_init(&ctx, 64, 40 * MEGA);
        memset(&ses, 0, sizeof(ses));
        ses.owner    = &ctx.sub;
        ses.slots[0] = make_test_slot(&ctx, 3999, 0, true);
        TEST_ASSERT_EQUAL_size_t(0, rx_session_scan(&ses, 40 * MEGA + 4000));
        TEST_ASSERT_NULL(ses.slots[0]);
        TEST_ASSERT_EQUAL_INT64(0, ses.timestamp);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_slot.allocated_fragments);
    }
    // 7) single_frame affects free size — instrumented allocator validates correct sizes.
    {
        test_rx_context_init(&ctx, 64, 40 * MEGA);
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        // Stale single-frame: payload freed with payload.size=42.
        ses.slots[0]                     = make_test_slot(&ctx, 3999, 42, true);
        ses.slots[0]->total_payload_size = 42;
        // Stale multi-frame: payload freed with extent=64.
        ses.slots[1]                     = make_test_slot(&ctx, 2000, 64, false);
        ses.slots[1]->payload.size       = 30; // written so far; irrelevant to free size
        ses.slots[1]->total_payload_size = 30;
        TEST_ASSERT_EQUAL_size_t(0, rx_session_scan(&ses, 40 * MEGA + 4000));
        TEST_ASSERT_NULL(ses.slots[0]);
        TEST_ASSERT_NULL(ses.slots[1]);
        TEST_ASSERT_EQUAL_INT64(0, ses.timestamp);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_slot.allocated_fragments);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_slot.allocated_bytes);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_payload.allocated_fragments);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_payload.allocated_bytes);
    }
    // 8) All 8 slots populated — verifies FOREACH_SLOT covers all CANARD_PRIO_COUNT entries.
    {
        test_rx_context_init(&ctx, 64, 40 * MEGA);
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        // 4 stale:
        ses.slots[0] = make_test_slot(&ctx, 3000, 0, true);
        ses.slots[2] = make_test_slot(&ctx, 3500, 0, true);
        ses.slots[4] = make_test_slot(&ctx, 2000, 0, true);
        ses.slots[6] = make_test_slot(&ctx, 3999, 0, true);
        // 4 fresh (slots[1] and slots[5] are in-progress):
        ses.slots[1] = make_test_slot(&ctx, 4500, 0, true);
        ses.slots[1]->total_payload_size = 10;
        ses.slots[3] = make_test_slot(&ctx, 4001, 0, true);
        ses.slots[5] = make_test_slot(&ctx, 4999, 0, true);
        ses.slots[5]->total_payload_size = 20;
        ses.slots[7] = make_test_slot(&ctx, 4000, 0, true);
        TEST_ASSERT_EQUAL_size_t(2, rx_session_scan(&ses, 40 * MEGA + 4000));
        TEST_ASSERT_NULL(ses.slots[0]);
        TEST_ASSERT_NULL(ses.slots[2]);
        TEST_ASSERT_NULL(ses.slots[4]);
        TEST_ASSERT_NULL(ses.slots[6]);
        TEST_ASSERT_NOT_NULL(ses.slots[1]);
        TEST_ASSERT_NOT_NULL(ses.slots[3]);
        TEST_ASSERT_NOT_NULL(ses.slots[5]);
        TEST_ASSERT_NOT_NULL(ses.slots[7]);
        TEST_ASSERT_EQUAL_INT64(4999, ses.timestamp);
        rx_slot_destroy(&ctx.sub, ses.slots[1]);
        rx_slot_destroy(&ctx.sub, ses.slots[3]);
        rx_slot_destroy(&ctx.sub, ses.slots[5]);
        rx_slot_destroy(&ctx.sub, ses.slots[7]);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_slot.allocated_fragments);
    }
    // 9) In-progress counting: multiple fresh slots with total_payload_size > 0.
    {
        test_rx_context_init(&ctx, 64, 40 * MEGA);
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        ses.slots[0] = make_test_slot(&ctx, 5000, 0, true);
        ses.slots[0]->total_payload_size = 10;
        ses.slots[3] = make_test_slot(&ctx, 6000, 0, true);
        ses.slots[3]->total_payload_size = 20;
        ses.slots[5] = make_test_slot(&ctx, 7000, 0, true); // fresh, idle
        TEST_ASSERT_EQUAL_size_t(2, rx_session_scan(&ses, 40 * MEGA + 4000));
        TEST_ASSERT_EQUAL_INT64(7000, ses.timestamp);
        rx_slot_destroy(&ctx.sub, ses.slots[0]);
        rx_slot_destroy(&ctx.sub, ses.slots[3]);
        rx_slot_destroy(&ctx.sub, ses.slots[5]);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_slot.allocated_fragments);
    }
    // 10) Fresh slots WITH payload allocations survive intact.
    {
        test_rx_context_init(&ctx, 64, 40 * MEGA);
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        // Fresh single-frame with 42-byte payload.
        ses.slots[0] = make_test_slot(&ctx, 5000, 42, true);
        ses.slots[0]->total_payload_size = 42;
        // Fresh multi-frame with extent-sized payload, partially written.
        ses.slots[1] = make_test_slot(&ctx, 6000, 64, false);
        ses.slots[1]->payload.size       = 30;
        ses.slots[1]->total_payload_size = 30;
        TEST_ASSERT_EQUAL_size_t(2, rx_session_scan(&ses, 40 * MEGA + 4000));
        TEST_ASSERT_EQUAL_UINT64(0, ctx.alloc_payload.count_free);
        TEST_ASSERT_EQUAL_size_t(42, ses.slots[0]->payload.size);
        TEST_ASSERT_EQUAL_size_t(30, ses.slots[1]->payload.size);
        TEST_ASSERT_EQUAL_INT64(6000, ses.timestamp);
        rx_slot_destroy(&ctx.sub, ses.slots[0]);
        rx_slot_destroy(&ctx.sub, ses.slots[1]);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_slot.allocated_fragments);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_payload.allocated_fragments);
    }
    // 11) Zero-extent subscription: stale slot with NULL payload.data.
    //     rx_slot_destroy → mem_free(rx_payload, 0, NULL) → skipped by NULL check in mem_free.
    {
        test_rx_context_init(&ctx, 0, 40 * MEGA);
        memset(&ses, 0, sizeof(ses));
        ses.owner    = &ctx.sub;
        ses.slots[0] = make_test_slot(&ctx, 3999, 0, true);
        TEST_ASSERT_EQUAL_size_t(0, rx_session_scan(&ses, 40 * MEGA + 4000));
        TEST_ASSERT_NULL(ses.slots[0]);
        TEST_ASSERT_EQUAL_INT64(0, ses.timestamp);
        TEST_ASSERT_EQUAL_UINT64(0, ctx.alloc_payload.count_free);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_slot.allocated_fragments);
    }
}

// =====================================================================================================================
// Test 20: rx_slot_write_payload — exercises all branches of payload accumulation logic.
static void test_rx_slot_write_payload(void)
{
    test_rx_context_t ctx;
    rx_session_t      ses;
    rx_slot_t         slot;

    // 1) Zero extent, empty payload, single-frame. No-op early return.
    {
        test_rx_context_init(&ctx, 0, 1000);
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        memset(&slot, 0, sizeof(slot));
        const canard_bytes_t pl = { 0, NULL };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, pl, true));
        TEST_ASSERT_TRUE(slot.single_frame);
        TEST_ASSERT_EQUAL_size_t(0, slot.total_payload_size);
        TEST_ASSERT_NULL(slot.payload.data);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_payload.allocated_fragments);
    }
    // 2) Zero extent, non-empty payload, single-frame. Payload discarded, total_payload_size tracked.
    {
        test_rx_context_init(&ctx, 0, 1000);
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        memset(&slot, 0, sizeof(slot));
        const byte_t         data[] = { 1, 2, 3, 4, 5 };
        const canard_bytes_t pl     = { sizeof(data), data };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, pl, true));
        TEST_ASSERT_TRUE(slot.single_frame);
        TEST_ASSERT_EQUAL_size_t(5, slot.total_payload_size);
        TEST_ASSERT_NULL(slot.payload.data);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_payload.allocated_fragments);
    }
    // 3) Non-zero extent, empty payload on first frame.
    {
        test_rx_context_init(&ctx, 64, 1000);
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        memset(&slot, 0, sizeof(slot));
        const canard_bytes_t pl = { 0, NULL };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, pl, false));
        TEST_ASSERT_FALSE(slot.single_frame);
        TEST_ASSERT_EQUAL_size_t(0, slot.total_payload_size);
        TEST_ASSERT_NULL(slot.payload.data);
    }
    // 4) Single-frame, payload < extent.
    {
        test_rx_context_init(&ctx, 64, 1000);
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        memset(&slot, 0, sizeof(slot));
        const byte_t         data[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22, 0x33, 0x44 };
        const canard_bytes_t pl     = { 10, data };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, pl, true));
        TEST_ASSERT_TRUE(slot.single_frame);
        TEST_ASSERT_EQUAL_size_t(10, slot.payload.size);
        TEST_ASSERT_EQUAL_size_t(10, slot.total_payload_size);
        TEST_ASSERT_NOT_NULL(slot.payload.data);
        TEST_ASSERT_EQUAL_MEMORY(data, slot.payload.data, 10);
        mem_free(ctx.canard.mem.rx_payload, slot.payload.size, slot.payload.data);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_payload.allocated_fragments);
    }
    // 5) Single-frame, payload > extent — truncation.
    {
        test_rx_context_init(&ctx, 4, 1000);
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        memset(&slot, 0, sizeof(slot));
        const byte_t         data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A };
        const canard_bytes_t pl     = { 10, data };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, pl, true));
        TEST_ASSERT_TRUE(slot.single_frame);
        TEST_ASSERT_EQUAL_size_t(4, slot.payload.size);
        TEST_ASSERT_EQUAL_size_t(10, slot.total_payload_size);
        TEST_ASSERT_EQUAL_MEMORY(data, slot.payload.data, 4);
        mem_free(ctx.canard.mem.rx_payload, slot.payload.size, slot.payload.data);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_payload.allocated_fragments);
    }
    // 6) Single-frame, payload == extent. Exact fit.
    {
        test_rx_context_init(&ctx, 10, 1000);
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        memset(&slot, 0, sizeof(slot));
        const byte_t         data[] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0 };
        const canard_bytes_t pl     = { 10, data };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, pl, true));
        TEST_ASSERT_TRUE(slot.single_frame);
        TEST_ASSERT_EQUAL_size_t(10, slot.payload.size);
        TEST_ASSERT_EQUAL_size_t(10, slot.total_payload_size);
        TEST_ASSERT_EQUAL_MEMORY(data, slot.payload.data, 10);
        mem_free(ctx.canard.mem.rx_payload, slot.payload.size, slot.payload.data);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_payload.allocated_fragments);
    }
    // 7) Single-frame OOM.
    {
        test_rx_context_init(&ctx, 64, 1000);
        ctx.alloc_payload.limit_bytes = 0;
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        memset(&slot, 0, sizeof(slot));
        const byte_t         data[] = { 1, 2, 3 };
        const canard_bytes_t pl     = { 3, data };
        TEST_ASSERT_FALSE(rx_slot_write_payload(&ses, &slot, pl, true));
        TEST_ASSERT_NULL(slot.payload.data);
    }
    // 8, 10, 11) Multi-frame: start → continuation → last frame.
    {
        test_rx_context_init(&ctx, 64, 1000);
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        memset(&slot, 0, sizeof(slot));
        // First frame (start, not last). Allocates full extent.
        const byte_t         d1[] = { 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8 };
        const canard_bytes_t pl1  = { 8, d1 };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, pl1, false));
        TEST_ASSERT_FALSE(slot.single_frame);
        TEST_ASSERT_EQUAL_size_t(8, slot.payload.size);
        TEST_ASSERT_EQUAL_size_t(8, slot.total_payload_size);
        TEST_ASSERT_NOT_NULL(slot.payload.data);
        TEST_ASSERT_EQUAL_MEMORY(d1, slot.payload.data, 8);
        // Continuation (not start, not last). Appends.
        const byte_t         d2[] = { 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8 };
        const canard_bytes_t pl2  = { 8, d2 };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, pl2, false));
        TEST_ASSERT_FALSE(slot.single_frame);
        TEST_ASSERT_EQUAL_size_t(16, slot.payload.size);
        TEST_ASSERT_EQUAL_size_t(16, slot.total_payload_size);
        TEST_ASSERT_EQUAL_MEMORY(d2, (const byte_t*)slot.payload.data + 8, 8);
        // Last frame.
        const byte_t         d3[] = { 0xC1, 0xC2, 0xC3, 0xC4 };
        const canard_bytes_t pl3  = { 4, d3 };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, pl3, true));
        TEST_ASSERT_FALSE(slot.single_frame); // start was false
        TEST_ASSERT_EQUAL_size_t(20, slot.payload.size);
        TEST_ASSERT_EQUAL_size_t(20, slot.total_payload_size);
        TEST_ASSERT_EQUAL_MEMORY(d3, (const byte_t*)slot.payload.data + 16, 4);
        mem_free(ctx.canard.mem.rx_payload, ctx.sub.extent, slot.payload.data);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_payload.allocated_fragments);
    }
    // 9) Multi-frame start OOM.
    {
        test_rx_context_init(&ctx, 64, 1000);
        ctx.alloc_payload.limit_bytes = 0;
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        memset(&slot, 0, sizeof(slot));
        const byte_t         data[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        const canard_bytes_t pl     = { 8, data };
        TEST_ASSERT_FALSE(rx_slot_write_payload(&ses, &slot, pl, false));
        TEST_ASSERT_NULL(slot.payload.data);
    }
    // 12) Multi-frame truncation at extent boundary.
    {
        test_rx_context_init(&ctx, 20, 1000);
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        memset(&slot, 0, sizeof(slot));
        const byte_t         d1[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        const canard_bytes_t f1   = { 8, d1 };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, f1, false));
        const byte_t         d2[] = { 9, 10, 11, 12, 13, 14, 15, 16 };
        const canard_bytes_t f2   = { 8, d2 };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, f2, false));
        TEST_ASSERT_EQUAL_size_t(16, slot.payload.size);
        // Write 8 more → copy_size = min(8, 20-16) = 4.
        const byte_t         d3[] = { 17, 18, 19, 20, 21, 22, 23, 24 };
        const canard_bytes_t f3   = { 8, d3 };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, f3, true));
        TEST_ASSERT_EQUAL_size_t(20, slot.payload.size);
        TEST_ASSERT_EQUAL_size_t(24, slot.total_payload_size);
        // Verify only the first 4 bytes of d3 were copied.
        const byte_t* buf = (const byte_t*)slot.payload.data;
        TEST_ASSERT_EQUAL_HEX8(17, buf[16]);
        TEST_ASSERT_EQUAL_HEX8(18, buf[17]);
        TEST_ASSERT_EQUAL_HEX8(19, buf[18]);
        TEST_ASSERT_EQUAL_HEX8(20, buf[19]);
        mem_free(ctx.canard.mem.rx_payload, ctx.sub.extent, slot.payload.data);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_payload.allocated_fragments);
    }
    // 13) Multi-frame extent already full — copy_size = 0, no crash.
    {
        test_rx_context_init(&ctx, 16, 1000);
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        memset(&slot, 0, sizeof(slot));
        const byte_t         d1[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        const canard_bytes_t f1   = { 8, d1 };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, f1, false));
        const byte_t         d2[] = { 9, 10, 11, 12, 13, 14, 15, 16 };
        const canard_bytes_t f2   = { 8, d2 };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, f2, false));
        TEST_ASSERT_EQUAL_size_t(16, slot.payload.size);
        const byte_t         d3[] = { 17, 18, 19, 20, 21, 22, 23, 24 };
        const canard_bytes_t f3   = { 8, d3 };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, f3, true));
        TEST_ASSERT_EQUAL_size_t(16, slot.payload.size);       // unchanged
        TEST_ASSERT_EQUAL_size_t(24, slot.total_payload_size); // raw total tracks everything
        mem_free(ctx.canard.mem.rx_payload, ctx.sub.extent, slot.payload.data);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_payload.allocated_fragments);
    }
    // 14) Data integrity — verify buffer contents byte-by-byte.
    {
        test_rx_context_init(&ctx, 64, 1000);
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        memset(&slot, 0, sizeof(slot));
        const byte_t         pat1[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE };
        const byte_t         pat2[] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
        const byte_t         pat3[] = { 0xFE, 0xDC, 0xBA, 0x98 };
        const canard_bytes_t f1     = { 8, pat1 };
        const canard_bytes_t f2     = { 8, pat2 };
        const canard_bytes_t f3     = { 4, pat3 };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, f1, false));
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, f2, false));
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, f3, true));
        TEST_ASSERT_EQUAL_size_t(20, slot.payload.size);
        TEST_ASSERT_EQUAL_size_t(20, slot.total_payload_size);
        const byte_t* buf = (const byte_t*)slot.payload.data;
        for (size_t i = 0; i < 8; i++) {
            TEST_ASSERT_EQUAL_HEX8(pat1[i], buf[i]);
        }
        for (size_t i = 0; i < 8; i++) {
            TEST_ASSERT_EQUAL_HEX8(pat2[i], buf[8 + i]);
        }
        for (size_t i = 0; i < 4; i++) {
            TEST_ASSERT_EQUAL_HEX8(pat3[i], buf[16 + i]);
        }
        mem_free(ctx.canard.mem.rx_payload, ctx.sub.extent, slot.payload.data);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_payload.allocated_fragments);
    }
    // 15) Empty continuation frame mid-stream — buffer and payload.size unchanged.
    {
        test_rx_context_init(&ctx, 64, 1000);
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        memset(&slot, 0, sizeof(slot));
        // Start frame: 8 bytes.
        const byte_t         d1[] = { 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8 };
        const canard_bytes_t f1   = { 8, d1 };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, f1, false));
        TEST_ASSERT_EQUAL_size_t(8, slot.payload.size);
        TEST_ASSERT_EQUAL_size_t(8, slot.total_payload_size);
        // Empty continuation: 0 bytes. Early return, no state change.
        const canard_bytes_t f2 = { 0, NULL };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, f2, false));
        TEST_ASSERT_EQUAL_size_t(8, slot.payload.size);
        TEST_ASSERT_EQUAL_size_t(8, slot.total_payload_size);
        // Last frame: 4 bytes appended.
        const byte_t         d3[] = { 0xC1, 0xC2, 0xC3, 0xC4 };
        const canard_bytes_t f3   = { 4, d3 };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, f3, true));
        TEST_ASSERT_EQUAL_size_t(12, slot.payload.size);
        TEST_ASSERT_EQUAL_size_t(12, slot.total_payload_size);
        TEST_ASSERT_EQUAL_MEMORY(d1, slot.payload.data, 8);
        TEST_ASSERT_EQUAL_MEMORY(d3, (const byte_t*)slot.payload.data + 8, 4);
        mem_free(ctx.canard.mem.rx_payload, ctx.sub.extent, slot.payload.data);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_payload.allocated_fragments);
    }
    // 16) Non-empty frame after empty first frame — start re-detection.
    //     An empty first frame leaves total_payload_size=0, so the next non-empty frame re-detects start=true.
    {
        test_rx_context_init(&ctx, 64, 1000);
        memset(&ses, 0, sizeof(ses));
        ses.owner = &ctx.sub;
        memset(&slot, 0, sizeof(slot));
        // Empty first frame (not last). total_payload_size stays 0, no allocation.
        const canard_bytes_t f0 = { 0, NULL };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, f0, false));
        TEST_ASSERT_EQUAL_size_t(0, slot.total_payload_size);
        TEST_ASSERT_NULL(slot.payload.data);
        // Non-empty frame (not last). Re-detects start=true, allocates extent.
        const byte_t         d1[] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
        const canard_bytes_t f1   = { 6, d1 };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, f1, false));
        TEST_ASSERT_EQUAL_size_t(6, slot.payload.size);
        TEST_ASSERT_EQUAL_size_t(6, slot.total_payload_size);
        TEST_ASSERT_NOT_NULL(slot.payload.data);
        // Last frame: 4 bytes appended.
        const byte_t         d2[] = { 0xAA, 0xBB, 0xCC, 0xDD };
        const canard_bytes_t f2   = { 4, d2 };
        TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, f2, true));
        TEST_ASSERT_EQUAL_size_t(10, slot.payload.size);
        TEST_ASSERT_EQUAL_size_t(10, slot.total_payload_size);
        TEST_ASSERT_EQUAL_MEMORY(d1, slot.payload.data, 6);
        TEST_ASSERT_EQUAL_MEMORY(d2, (const byte_t*)slot.payload.data + 6, 4);
        mem_free(ctx.canard.mem.rx_payload, ctx.sub.extent, slot.payload.data);
        TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_payload.allocated_fragments);
    }
}

// =====================================================================================================================
// Test 21: Dedicated bug-verification test — zero extent with non-empty multi-frame payload.
// Before fix: assertion crash. After fix: silently discards payload while tracking total_payload_size.
static void test_rx_slot_write_payload_zero_extent_nonempty(void)
{
    test_rx_context_t ctx;
    test_rx_context_init(&ctx, 0, 1000);
    rx_session_t ses;
    memset(&ses, 0, sizeof(ses));
    ses.owner = &ctx.sub;
    rx_slot_t slot;
    memset(&slot, 0, sizeof(slot));

    // First frame: 8-byte payload, not last.
    const byte_t         d1[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    const canard_bytes_t f1   = { 8, d1 };
    TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, f1, false));
    TEST_ASSERT_FALSE(slot.single_frame);
    TEST_ASSERT_EQUAL_size_t(8, slot.total_payload_size);
    TEST_ASSERT_NULL(slot.payload.data);
    TEST_ASSERT_EQUAL_size_t(0, slot.payload.size);

    // Second frame: 8-byte payload, last.
    const byte_t         d2[] = { 9, 10, 11, 12, 13, 14, 15, 16 };
    const canard_bytes_t f2   = { 8, d2 };
    TEST_ASSERT_TRUE(rx_slot_write_payload(&ses, &slot, f2, true));
    TEST_ASSERT_FALSE(slot.single_frame);
    TEST_ASSERT_EQUAL_size_t(16, slot.total_payload_size);
    TEST_ASSERT_NULL(slot.payload.data);
    TEST_ASSERT_EQUAL_size_t(0, ctx.alloc_payload.allocated_fragments);
}

// =====================================================================================================================

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_rx_parse_empty_payload);
    RUN_TEST(test_rx_parse_v1_1_message_golden);
    RUN_TEST(test_rx_parse_v1_0_message_golden);
    RUN_TEST(test_rx_parse_v1_0_service_golden);
    RUN_TEST(test_rx_parse_v0_message_golden);
    RUN_TEST(test_rx_parse_v0_service_golden);
    RUN_TEST(test_rx_parse_v1_0_reserved_bit23_reject);
    RUN_TEST(test_rx_parse_v0_service_zero_node_reject);
    RUN_TEST(test_rx_parse_version_detection);
    RUN_TEST(test_rx_parse_payload_handling);
    RUN_TEST(test_rx_parse_tail_byte_exhaustive);
    RUN_TEST(test_rx_parse_cross_version_ambiguity);
    RUN_TEST(test_rx_parse_bit_field_boundaries);
    RUN_TEST(test_rx_parse_v1_1_accepts_bit23);
    RUN_TEST(test_rx_parse_v1_0_message_ignores_reserved_bits_22_21);
    RUN_TEST(test_rx_parse_non_first_dual_output);
    RUN_TEST(test_rx_parse_payload_validation);
    RUN_TEST(test_rx_parse_anonymous_multi_frame_reject);

    RUN_TEST(test_rx_session_scan);
    RUN_TEST(test_rx_slot_write_payload);
    RUN_TEST(test_rx_slot_write_payload_zero_extent_nonempty);

    return UNITY_END();
}
