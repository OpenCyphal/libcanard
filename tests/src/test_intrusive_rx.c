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
        TEST_ASSERT_EQUAL_INT(canard_kind_message_16b, v1.kind);
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
        TEST_ASSERT_EQUAL_INT(canard_kind_message_16b, v1.kind);
        TEST_ASSERT_EQUAL_UINT32(0, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(0xFF, v1.dst);
        TEST_ASSERT_EQUAL_HEX8(0, v1.src);
        TEST_ASSERT_EQUAL_INT(canard_prio_exceptional, v1.priority);
        TEST_ASSERT_EQUAL_UINT8(0, v1.transfer_id);
    }
    // Max: prio=7, subject=65535 (0xFFFF), src=127. CAN ID = 0x1CFFFFFF.
    {
        const byte_t         d[] = { 0xFF }; // 0xE0|31 → tid=31
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(2, rx_parse(0x1CFFFFFFUL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(canard_kind_message_16b, v1.kind);
        TEST_ASSERT_EQUAL_UINT32(0xFFFFUL, v1.port_id);
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
        TEST_ASSERT_EQUAL_INT(canard_kind_message_13b, v1.kind);
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
        TEST_ASSERT_EQUAL_INT(canard_kind_message_13b, v1.kind);
        TEST_ASSERT_EQUAL_UINT32(8191, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(1, v1.src);
    }
    // Anonymous: prio=2, subject=100, bit24=1, src=0x55(pseudo), reserved=11b. CAN ID = 0x09606455.
    {
        const byte_t         d[] = { 0xE3 }; // v1 single, tid=3
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(2, rx_parse(0x09606455UL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(canard_kind_message_13b, v1.kind);
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
        TEST_ASSERT_EQUAL_INT(canard_kind_request, v1.kind);
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
        TEST_ASSERT_EQUAL_INT(canard_kind_response, v1.kind);
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
        TEST_ASSERT_EQUAL_INT(canard_kind_request, v1.kind);
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
        TEST_ASSERT_EQUAL_INT(canard_kind_v0_message, v0.kind);
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
        TEST_ASSERT_EQUAL_INT(canard_kind_v0_message, v0.kind);
        TEST_ASSERT_EQUAL_UINT32(0x040A, v0.port_id);
        TEST_ASSERT_EQUAL_HEX8(0xFF, v0.src); // anonymous
    }
    // Max: prio=7, type_id=0xFFFF, src=127. CAN ID = 0x1FFFFF7F.
    {
        const byte_t         d[] = { 0xDF }; // v0 single: 0xC0|31, tid=31
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(1, rx_parse(0x1FFFFF7FUL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(canard_kind_v0_message, v0.kind);
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
        TEST_ASSERT_EQUAL_INT(canard_kind_v0_request, v0.kind);
        TEST_ASSERT_EQUAL_UINT32(0x37, v0.port_id);
        TEST_ASSERT_EQUAL_HEX8(24, v0.dst);
        TEST_ASSERT_EQUAL_HEX8(11, v0.src);
        TEST_ASSERT_EQUAL_INT(canard_prio_nominal, v0.priority);
    }
    // Response: prio=0, type_id=1, dst=1, src=2. CAN ID = 0x03010181.
    {
        const byte_t         d[] = { 0xC0 }; // v0 single, tid=0
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(1, rx_parse(0x03010182UL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(canard_kind_v0_response, v0.kind);
        TEST_ASSERT_EQUAL_UINT32(1, v0.port_id);
        TEST_ASSERT_EQUAL_HEX8(1, v0.dst);
        TEST_ASSERT_EQUAL_HEX8(2, v0.src);
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
        TEST_ASSERT_EQUAL_INT(canard_kind_message_16b, v1.kind);
        TEST_ASSERT_EQUAL_UINT32(1234, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(42, v1.src);
        // v0: service request, type_id=4, dst=82, src=42
        TEST_ASSERT_EQUAL_INT(canard_kind_v0_request, v0.kind);
        TEST_ASSERT_EQUAL_UINT32(4, v0.port_id);
        TEST_ASSERT_EQUAL_HEX8(82, v0.dst);
        TEST_ASSERT_EQUAL_HEX8(42, v0.src);
    }
    // All-ones 0x1FFFFFFF: v1 rejected (bit23 in service path), v0 rejected (src==dst).
    {
        const byte_t         d[] = { 1, 2, 3, 4, 5, 6, 7, nf };
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x1FFFFFFFUL, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(0, ret);
    }
    // All-zeros 0x00000000: v1 parses as v1.0 message; v0 anonymous (src=0) rejected for non-first frame
    // because anonymous can only be single-frame (start && end required, but start=false here).
    {
        const byte_t         d[] = { 1, 2, 3, 4, 5, 6, 7, nf };
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x00000000UL, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(2, ret); // v1 only; v0 anonymous rejected
        // v1: v1.0 message (bit7=0), port_id=0, src=0
        TEST_ASSERT_EQUAL_INT(canard_kind_message_13b, v1.kind);
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
    // v1.0 service: max svc_id=511 with dst=0 and src=1
    // CAN ID: (0<<26)|(1<<25)|(0<<24)|(511<<14)|(0<<7)|0 = 0x027FC000
    {
        const byte_t         d[] = { 0xE0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(2, rx_parse(0x027FC001UL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(canard_kind_response, v1.kind);
        TEST_ASSERT_EQUAL_UINT32(511, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(0, v1.dst);
        TEST_ASSERT_EQUAL_HEX8(1, v1.src);
    }
    // v1.0 service: max dst=127 with svc_id=0 and src=0 → verify svc_id and src read 0.
    // CAN ID: (0<<26)|(1<<25)|(0<<24)|(0<<14)|(127<<7)|0 = 0x02003F80
    {
        const byte_t         d[] = { 0xE0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(2, rx_parse(0x02003F80UL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(canard_kind_response, v1.kind);
        TEST_ASSERT_EQUAL_UINT32(0, v1.port_id);
        TEST_ASSERT_EQUAL_HEX8(127, v1.dst);
        TEST_ASSERT_EQUAL_HEX8(0, v1.src);
    }
    // v0 service: max type_id=0xFF with dst=1, req=0 → verify req reads 0 and dst is correct.
    // CAN ID: ((0<<2)|3)<<24 | (0xFF<<16) | (0<<15) | (1<<8) | (1<<7) | 1 = 0x03FF0181
    {
        const byte_t         d[] = { 0xC0 }; // v0 single
        const canard_bytes_t pl  = { sizeof(d), d };
        TEST_ASSERT_EQUAL_UINT8(1, rx_parse(0x03FF0182UL, pl, &v0, &v1));
        TEST_ASSERT_EQUAL_INT(canard_kind_v0_response, v0.kind); // req=0
        TEST_ASSERT_EQUAL_UINT32(0xFF, v0.port_id);
        TEST_ASSERT_EQUAL_HEX8(1, v0.dst);
        TEST_ASSERT_EQUAL_HEX8(2, v0.src);
    }
}

// =====================================================================================================================
// Test 14: v1.1 does NOT reject bit 23 (unlike v1.0).
static void test_rx_parse_v1_1_accepts_bit23(void)
{
    frame_t v0;
    frame_t v1;
    // Subject 0x8000: bit 23 of CAN ID is set because (0x8000 << 8) sets bit 23.
    // CAN ID: (0<<26) | (0x8000<<8) | (1<<7) | 0 = 0x00800080
    {
        const byte_t         d[] = { 0xE5 }; // v1 single, tid=5
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x00800080UL, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(2, ret); // accepted despite bit 23
        TEST_ASSERT_EQUAL_INT(canard_kind_message_16b, v1.kind);
        TEST_ASSERT_EQUAL_UINT32(0x8000UL, v1.port_id);
    }
    // Max subject 0xFFFF also sets bit 23.
    // CAN ID: (0<<26) | (0xFFFF<<8) | (1<<7) | 0 = 0x00FFFF80
    {
        const byte_t         d[] = { 0xE0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x00FFFF80UL, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(2, ret);
        TEST_ASSERT_EQUAL_INT(canard_kind_message_16b, v1.kind);
        TEST_ASSERT_EQUAL_UINT32(0xFFFFUL, v1.port_id);
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
    TEST_ASSERT_EQUAL_INT(canard_kind_message_13b, v1.kind);
    TEST_ASSERT_EQUAL_UINT32(42, v1.port_id);
    // bits 22:21 = 11: CAN ID = (0<<26)|(3<<21)|(42<<8)|1 = 0x00602A01
    rx_parse(0x00602A01UL, pl, &v0, &v1);
    TEST_ASSERT_EQUAL_INT(canard_kind_message_13b, v1.kind);
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
    TEST_ASSERT_EQUAL_INT(canard_kind_message_16b, v1.kind);
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
    TEST_ASSERT_EQUAL_INT(canard_kind_v0_request, v0.kind);
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
// Test 19: One-byte frame (tail byte only). SOT+EOT single-frame tail 0xE0 → v1 parse, payload.size=0.
static void test_rx_parse_one_byte_tail_only(void)
{
    frame_t v0;
    frame_t v1;
    // v1.1 message CAN ID: prio=0, subject=0, src=0, bit7=1. CAN ID = 0x00000080.
    // Tail byte 0xE0 = SOT|EOT|toggle=1|TID=0 → v1 single-frame.
    {
        const byte_t         d[] = { 0xE0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x00000080UL, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(2, ret); // v1 accepted
        TEST_ASSERT_EQUAL_INT(canard_kind_message_16b, v1.kind);
        TEST_ASSERT_EQUAL_size_t(0, v1.payload.size);
        TEST_ASSERT_EQUAL_PTR(d, v1.payload.data);
        TEST_ASSERT_TRUE(v1.start);
        TEST_ASSERT_TRUE(v1.end);
        TEST_ASSERT_TRUE(v1.toggle);
        TEST_ASSERT_EQUAL_UINT8(0, v1.transfer_id);
    }
    // v0 single-frame: tail 0xC0 = SOT|EOT|toggle=0|TID=0 → v0 only.
    // CAN ID 0x00002A01: message, bit7=0. v0 message src=1 (non-anonymous), type_id=42.
    {
        const byte_t         d[] = { 0xC0 };
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x00002A01UL, pl, &v0, &v1);
        TEST_ASSERT_EQUAL_UINT8(1, ret); // v0 only
        TEST_ASSERT_EQUAL_INT(canard_kind_v0_message, v0.kind);
        TEST_ASSERT_EQUAL_size_t(0, v0.payload.size);
        TEST_ASSERT_EQUAL_PTR(d, v0.payload.data);
        TEST_ASSERT_TRUE(v0.start);
        TEST_ASSERT_TRUE(v0.end);
        TEST_ASSERT_FALSE(v0.toggle);
        TEST_ASSERT_EQUAL_UINT8(0, v0.transfer_id);
    }
}

// =====================================================================================================================
// Test 20: 64-byte CAN FD frame with v1.1 message CAN ID. Tail byte at position 63.
static void test_rx_parse_max_fd_frame(void)
{
    frame_t v0;
    frame_t v1;
    // v1.1 message CAN ID: prio=2, subject=5000, src=10, bit7=1. CAN ID = (2<<26)|(5000<<8)|(1<<7)|10 = 0x0813888A.
    byte_t d[64];
    memset(d, 0xBB, sizeof(d));
    d[63]                    = 0xEF; // v1 single-frame tail: SOT|EOT|toggle=1|TID=15
    const canard_bytes_t pl  = { sizeof(d), d };
    const byte_t         ret = rx_parse(0x0813888AUL, pl, &v0, &v1);
    TEST_ASSERT_EQUAL_UINT8(2, ret); // v1 only (toggle=1 excludes v0 on start)
    TEST_ASSERT_EQUAL_INT(canard_kind_message_16b, v1.kind);
    TEST_ASSERT_EQUAL_size_t(63, v1.payload.size);
    TEST_ASSERT_EQUAL_PTR(d, v1.payload.data);
    TEST_ASSERT_EQUAL_UINT8(15, v1.transfer_id);
    TEST_ASSERT_EQUAL_HEX8(10, v1.src);
    TEST_ASSERT_EQUAL_UINT32(5000, v1.port_id);
    TEST_ASSERT_EQUAL_INT(canard_prio_fast, v1.priority);
}

// =====================================================================================================================
// Test 21: v1.0 service with src==dst (self-addressing). Must be rejected.
static void test_rx_parse_v1_service_self_addressing(void)
{
    frame_t v0;
    frame_t v1;
    // v1.0 service request: prio=0, svc_id=1, dst=42, src=42. Self-addressing → rejected.
    // CAN ID: (0<<26)|(1<<25)|(1<<24)|(1<<14)|(42<<7)|42 = 0x0300552A
    {
        const byte_t         d[] = { 0xE0 }; // v1 single-frame tail: SOT|EOT|toggle=1|TID=0
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x0300552AUL, pl, &v0, &v1);
        // v1 rejected due to src==dst. v0: SOT=1 toggle=1 → v0 excluded. Return = 0.
        TEST_ASSERT_EQUAL_UINT8(0, ret);
    }
    // v1.0 service response: prio=4, svc_id=100, dst=10, src=10.
    // CAN ID: (4<<26)|(1<<25)|(0<<24)|(100<<14)|(10<<7)|10 = 0x1219050A
    {
        const byte_t         d[] = { 0xE3 }; // v1 single, tid=3, SOT|EOT|toggle=1
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x1219050AUL, pl, &v0, &v1);
        // v1 rejected (src==dst=10). v0: SOT=1 toggle=1 → excluded. Return=0.
        TEST_ASSERT_EQUAL_UINT8(0, ret);
    }
}

// =====================================================================================================================
// Test 22: v0 service with src==dst (self-addressing). Must be rejected.
static void test_rx_parse_v0_service_self_addressing(void)
{
    frame_t v0;
    frame_t v1;
    // v0 service request: prio=4, type_id=0x37, dst=11, src=11. Self-addressing.
    // CAN ID: (((4<<2)|3)<<24)|(0x37<<16)|(1<<15)|(11<<8)|(1<<7)|11 = 0x13378B8B
    {
        const byte_t         d[] = { 0xC0 }; // v0 single: SOT=1, EOT=1, toggle=0, TID=0
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x13378B8BUL, pl, &v0, &v1);
        // v0 rejected (src==dst). v1: SOT=1 toggle=0 → v1 excluded. Return=0.
        TEST_ASSERT_EQUAL_UINT8(0, ret);
    }
}

// =====================================================================================================================
// Test 23: v0 service with src=0. Must be rejected (node-ID 0 reserved for anonymous).
static void test_rx_parse_v0_service_zero_src(void)
{
    frame_t v0;
    frame_t v1;
    // v0 service request: prio=4, type_id=0x37, dst=24, src=0.
    // CAN ID: (((4<<2)|3)<<24)|(0x37<<16)|(1<<15)|(24<<8)|(1<<7)|0 = 0x13379880
    {
        const byte_t         d[] = { 0xC0 }; // v0 single
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x13379880UL, pl, &v0, &v1);
        // v0 rejected (src=0). v1: start && !toggle → excluded. Return=0.
        TEST_ASSERT_EQUAL_UINT8(0, ret);
    }
}

// =====================================================================================================================
// Test 24: v0 service with dst=0. Must be rejected (node-ID 0 reserved for anonymous).
static void test_rx_parse_v0_service_zero_dst(void)
{
    frame_t v0;
    frame_t v1;
    // v0 service request: prio=4, type_id=0x37, dst=0, src=11.
    // CAN ID: (((4<<2)|3)<<24)|(0x37<<16)|(1<<15)|(0<<8)|(1<<7)|11 = 0x1337808B
    {
        const byte_t         d[] = { 0xC0 }; // v0 single
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x1337808BUL, pl, &v0, &v1);
        // v0 rejected (dst=0). v1: start && !toggle → excluded. Return=0.
        TEST_ASSERT_EQUAL_UINT8(0, ret);
    }
}

// =====================================================================================================================
// Test 25: Middle frame (SOT=0, EOT=0) with only 1 byte (tail only). Payload=0. Rejected by payload_ok.
static void test_rx_parse_non_start_non_end_empty(void)
{
    frame_t v0;
    frame_t v1;
    // CAN ID valid for both versions: message, bit7=0. 0x00002A01.
    // Tail: SOT=0 EOT=0 toggle=0 TID=5 → 0x05. One byte total.
    {
        const byte_t         d[] = { 0x05 };
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x00002A01UL, pl, &v0, &v1);
        // payload_raw.size=1. payload.size=0.
        // payload_ok = (end || (1 >= 8)) && ((start && end) || (0 > 0))
        //            = (false || false) && (false || false)
        //            = false
        // Both rejected.
        TEST_ASSERT_EQUAL_UINT8(0, ret);
    }
}

// =====================================================================================================================
// Test 26: Continuation frame (non-last) with only 6 bytes. Under full MTU (8). Rejected.
static void test_rx_parse_non_last_short_frame(void)
{
    frame_t v0;
    frame_t v1;
    // CAN ID valid for both versions: message, bit7=0. 0x00002A01.
    // Tail: SOT=0 EOT=0 toggle=0 TID=3 → 0x03. 6 bytes total, 5 payload.
    {
        const byte_t         d[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x03 };
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x00002A01UL, pl, &v0, &v1);
        // payload_raw.size=6. payload.size=5.
        // payload_ok = (end || (6 >= 8)) && ((start && end) || (5 > 0))
        //            = (false || false) && (false || true)
        //            = false
        // Both rejected.
        TEST_ASSERT_EQUAL_UINT8(0, ret);
    }
    // Same but with 7 bytes: still under MTU.
    {
        const byte_t         d[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x03 };
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x00002A01UL, pl, &v0, &v1);
        // payload_raw.size=7 < 8 → first clause false. Rejected.
        TEST_ASSERT_EQUAL_UINT8(0, ret);
    }
    // With exactly 8 bytes: at MTU. Accepted.
    {
        const byte_t         d[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x03 };
        const canard_bytes_t pl  = { sizeof(d), d };
        const byte_t         ret = rx_parse(0x00002A01UL, pl, &v0, &v1);
        // payload_raw.size=8 >= 8 → OK. payload.size=7 > 0 → OK. Both versions attempted.
        TEST_ASSERT_EQUAL_UINT8(3, ret);
    }
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
    RUN_TEST(test_rx_parse_one_byte_tail_only);
    RUN_TEST(test_rx_parse_max_fd_frame);
    RUN_TEST(test_rx_parse_v1_service_self_addressing);
    RUN_TEST(test_rx_parse_v0_service_self_addressing);
    RUN_TEST(test_rx_parse_v0_service_zero_src);
    RUN_TEST(test_rx_parse_v0_service_zero_dst);
    RUN_TEST(test_rx_parse_non_start_non_end_empty);
    RUN_TEST(test_rx_parse_non_last_short_frame);
    return UNITY_END();
}
