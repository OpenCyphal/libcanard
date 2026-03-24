// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal Development Team.

// Adversarial edge-case tests for the RX pipeline.
// Each test constructs raw CAN frames and injects them via canard_ingest_frame().

#include "helpers.h"
#include <unity.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// -------------------------------------------  CRC-16/CCITT-FALSE  ----------------------------------------------------

// clang-format off
static const uint16_t crc_lut[256] = {
    0x0000U, 0x1021U, 0x2042U, 0x3063U, 0x4084U, 0x50A5U, 0x60C6U, 0x70E7U,
    0x8108U, 0x9129U, 0xA14AU, 0xB16BU, 0xC18CU, 0xD1ADU, 0xE1CEU, 0xF1EFU,
    0x1231U, 0x0210U, 0x3273U, 0x2252U, 0x52B5U, 0x4294U, 0x72F7U, 0x62D6U,
    0x9339U, 0x8318U, 0xB37BU, 0xA35AU, 0xD3BDU, 0xC39CU, 0xF3FFU, 0xE3DEU,
    0x2462U, 0x3443U, 0x0420U, 0x1401U, 0x64E6U, 0x74C7U, 0x44A4U, 0x5485U,
    0xA56AU, 0xB54BU, 0x8528U, 0x9509U, 0xE5EEU, 0xF5CFU, 0xC5ACU, 0xD58DU,
    0x3653U, 0x2672U, 0x1611U, 0x0630U, 0x76D7U, 0x66F6U, 0x5695U, 0x46B4U,
    0xB75BU, 0xA77AU, 0x9719U, 0x8738U, 0xF7DFU, 0xE7FEU, 0xD79DU, 0xC7BCU,
    0x48C4U, 0x58E5U, 0x6886U, 0x78A7U, 0x0840U, 0x1861U, 0x2802U, 0x3823U,
    0xC9CCU, 0xD9EDU, 0xE98EU, 0xF9AFU, 0x8948U, 0x9969U, 0xA90AU, 0xB92BU,
    0x5AF5U, 0x4AD4U, 0x7AB7U, 0x6A96U, 0x1A71U, 0x0A50U, 0x3A33U, 0x2A12U,
    0xDBFDU, 0xCBDCU, 0xFBBFU, 0xEB9EU, 0x9B79U, 0x8B58U, 0xBB3BU, 0xAB1AU,
    0x6CA6U, 0x7C87U, 0x4CE4U, 0x5CC5U, 0x2C22U, 0x3C03U, 0x0C60U, 0x1C41U,
    0xEDAEU, 0xFD8FU, 0xCDECU, 0xDDCDU, 0xAD2AU, 0xBD0BU, 0x8D68U, 0x9D49U,
    0x7E97U, 0x6EB6U, 0x5ED5U, 0x4EF4U, 0x3E13U, 0x2E32U, 0x1E51U, 0x0E70U,
    0xFF9FU, 0xEFBEU, 0xDFDDU, 0xCFFCU, 0xBF1BU, 0xAF3AU, 0x9F59U, 0x8F78U,
    0x9188U, 0x81A9U, 0xB1CAU, 0xA1EBU, 0xD10CU, 0xC12DU, 0xF14EU, 0xE16FU,
    0x1080U, 0x00A1U, 0x30C2U, 0x20E3U, 0x5004U, 0x4025U, 0x7046U, 0x6067U,
    0x83B9U, 0x9398U, 0xA3FBU, 0xB3DAU, 0xC33DU, 0xD31CU, 0xE37FU, 0xF35EU,
    0x02B1U, 0x1290U, 0x22F3U, 0x32D2U, 0x4235U, 0x5214U, 0x6277U, 0x7256U,
    0xB5EAU, 0xA5CBU, 0x95A8U, 0x8589U, 0xF56EU, 0xE54FU, 0xD52CU, 0xC50DU,
    0x34E2U, 0x24C3U, 0x14A0U, 0x0481U, 0x7466U, 0x6447U, 0x5424U, 0x4405U,
    0xA7DBU, 0xB7FAU, 0x8799U, 0x97B8U, 0xE75FU, 0xF77EU, 0xC71DU, 0xD73CU,
    0x26D3U, 0x36F2U, 0x0691U, 0x16B0U, 0x6657U, 0x7676U, 0x4615U, 0x5634U,
    0xD94CU, 0xC96DU, 0xF90EU, 0xE92FU, 0x99C8U, 0x89E9U, 0xB98AU, 0xA9ABU,
    0x5844U, 0x4865U, 0x7806U, 0x6827U, 0x18C0U, 0x08E1U, 0x3882U, 0x28A3U,
    0xCB7DU, 0xDB5CU, 0xEB3FU, 0xFB1EU, 0x8BF9U, 0x9BD8U, 0xABBBU, 0xBB9AU,
    0x4A75U, 0x5A54U, 0x6A37U, 0x7A16U, 0x0AF1U, 0x1AD0U, 0x2AB3U, 0x3A92U,
    0xFD2EU, 0xED0FU, 0xDD6CU, 0xCD4DU, 0xBDAAU, 0xAD8BU, 0x9DE8U, 0x8DC9U,
    0x7C26U, 0x6C07U, 0x5C64U, 0x4C45U, 0x3CA2U, 0x2C83U, 0x1CE0U, 0x0CC1U,
    0xEF1FU, 0xFF3EU, 0xCF5DU, 0xDF7CU, 0xAF9BU, 0xBFBAU, 0x8FD9U, 0x9FF8U,
    0x6E17U, 0x7E36U, 0x4E55U, 0x5E74U, 0x2E93U, 0x3EB2U, 0x0ED1U, 0x1EF0U,
};
// clang-format on

static uint16_t crc16_ccitt(uint16_t crc, const uint_least8_t* data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc = static_cast<uint16_t>((static_cast<unsigned>(crc) << 8U) ^
                                    crc_lut[(static_cast<unsigned>(crc) >> 8U) ^ data[i]]);
    }
    return crc;
}

// -------------------------------------------  Allocator & Instance Setup  --------------------------------------------

static void* std_alloc_mem(const canard_mem_t, const size_t size) { return std::malloc(size); }
static void  std_free_mem(const canard_mem_t, const size_t, void* const pointer) { std::free(pointer); }

static canard_us_t mock_now(const canard_t* const self)
{
    return (self->user_context != nullptr) ? *static_cast<const canard_us_t*>(self->user_context) : 0;
}
static bool mock_tx(canard_t* const,
                    void* const,
                    const canard_us_t,
                    const uint_least8_t,
                    const bool,
                    const uint32_t,
                    const canard_bytes_t)
{
    return false;
}

static const canard_vtable_t     test_vtable    = { .now = mock_now, .tx = mock_tx, .filter = nullptr };
static const canard_mem_vtable_t std_mem_vtable = { .free = std_free_mem, .alloc = std_alloc_mem };

static canard_mem_set_t make_std_memory()
{
    const canard_mem_t r = { .vtable = &std_mem_vtable, .context = nullptr };
    return canard_mem_set_t{ .tx_transfer = r, .tx_frame = r, .rx_session = r, .rx_payload = r, .rx_filters = r };
}

static void init_canard(canard_t* const self, canard_us_t* const now_val, const uint_least8_t node_id)
{
    *now_val = 0;
    TEST_ASSERT_TRUE(canard_new(self, &test_vtable, make_std_memory(), 16U, 1234U, 0U));
    TEST_ASSERT_TRUE(canard_set_node_id(self, node_id));
    self->user_context = now_val;
}

// -------------------------------------------  RX Capture Callback  ---------------------------------------------------

struct rx_capture_t
{
    size_t        count;
    canard_us_t   timestamp;
    canard_prio_t priority;
    uint_least8_t source_node_id;
    uint_least8_t transfer_id;
    size_t        payload_size;
    uint_least8_t payload_buf[512];
};

// Captures the received transfer and frees the origin if it was dynamically allocated (multiframe case).
static void capture_on_message(canard_subscription_t* const self,
                               const canard_us_t            timestamp,
                               const canard_prio_t          priority,
                               const uint_least8_t          source_node_id,
                               const uint_least8_t          transfer_id,
                               // cppcheck-suppress passedByValueCallback
                               const canard_payload_t payload)
{
    auto* const cap = static_cast<rx_capture_t*>(self->user_context);
    cap->count++;
    cap->timestamp      = timestamp;
    cap->priority       = priority;
    cap->source_node_id = source_node_id;
    cap->transfer_id    = transfer_id;
    cap->payload_size   = payload.view.size;
    if ((payload.view.size > 0) && (payload.view.data != nullptr)) {
        const size_t n = (payload.view.size < sizeof(cap->payload_buf)) ? payload.view.size : sizeof(cap->payload_buf);
        std::memcpy(cap->payload_buf, payload.view.data, n);
    }
    // For multi-frame transfers the origin is dynamically allocated and must be freed by the application.
    if ((payload.origin.size > 0) && (payload.origin.data != nullptr)) {
        std::free(payload.origin.data);
    }
}

static const canard_subscription_vtable_t capture_sub_vtable = { .on_message = capture_on_message };

// -------------------------------------------  CAN Frame Construction Helpers  ----------------------------------------

// v1.1 message: priority[28:26] | subject_id[25:8] | bit7=1(v1.1) | src[6:0]
static uint32_t make_v1v1_msg_can_id(const canard_prio_t prio, const uint16_t subject_id, const uint_least8_t src)
{
    return (static_cast<uint32_t>(prio) << 26U) | (static_cast<uint32_t>(subject_id) << 8U) | (UINT32_C(1) << 7U) |
           (static_cast<uint32_t>(src) & 0x7FU);
}

// v1.0 message: priority[28:26] | 00 | subject_id[20:8] | bit7=0 | src[6:0]
static uint32_t make_v1v0_msg_can_id(const canard_prio_t prio, const uint16_t subject_id, const uint_least8_t src)
{
    return (static_cast<uint32_t>(prio) << 26U) | (static_cast<uint32_t>(subject_id) << 8U) |
           (static_cast<uint32_t>(src) & 0x7FU);
}

// v1 service: priority[28:26] | bit25=1(svc) | rnr[24] | service_id[23:14] | dst[13:7] | src[6:0]
static uint32_t make_v1_svc_can_id(const canard_prio_t prio,
                                   const uint16_t      service_id,
                                   const bool          request_not_response,
                                   const uint_least8_t dst,
                                   const uint_least8_t src)
{
    return (static_cast<uint32_t>(prio) << 26U) | (UINT32_C(1) << 25U) |
           (request_not_response ? (UINT32_C(1) << 24U) : 0U) | (static_cast<uint32_t>(service_id) << 14U) |
           (static_cast<uint32_t>(dst) << 7U) | (static_cast<uint32_t>(src) & 0x7FU);
}

// v0 message: priority[28:26] | data_type_id[23:8] | bit7=0 | src[6:0]
static uint32_t make_v0_msg_can_id(const canard_prio_t prio, const uint16_t dtid, const uint_least8_t src)
{
    return (static_cast<uint32_t>(prio) << 26U) | (static_cast<uint32_t>(dtid) << 8U) |
           (static_cast<uint32_t>(src) & 0x7FU);
}

// v1 tail bytes: SOT=bit7, EOT=bit6, toggle=bit5, tid=bits[4:0]
static uint_least8_t make_v1_single_tail(const uint_least8_t tid)
{
    return static_cast<uint_least8_t>(0xE0U | (tid & 0x1FU));
}
static uint_least8_t make_v1_tail(const bool sot, const bool eot, const bool toggle, const uint_least8_t tid)
{
    return static_cast<uint_least8_t>((sot ? 0x80U : 0U) | (eot ? 0x40U : 0U) | (toggle ? 0x20U : 0U) | (tid & 0x1FU));
}

// v0 tail bytes
static uint_least8_t make_v0_single_tail(const uint_least8_t tid)
{
    return static_cast<uint_least8_t>(0xC0U | (tid & 0x1FU));
}
static uint_least8_t make_v0_tail(const bool sot, const bool eot, const bool toggle, const uint_least8_t tid)
{
    return static_cast<uint_least8_t>((sot ? 0x80U : 0U) | (eot ? 0x40U : 0U) | (toggle ? 0x20U : 0U) | (tid & 0x1FU));
}

// -------------------------------------------  Test: v1 2-frame Classic CAN multiframe --------------------------------

static void test_rx_v1_multiframe_2frame_classic()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 1000U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    // Construct 2-frame v1.1 multiframe. Payload: 8 bytes {0x10..0x17}.
    // Classic CAN: MTU=8, payload_per_frame=7 (one byte for tail).
    // Frame 1 (SOT): 7 payload bytes + tail(SOT=1,EOT=0,toggle=1,tid=3)
    // Frame 2 (EOT): 1 payload byte + CRC(2 bytes, big-endian) + tail(SOT=0,EOT=1,toggle=0,tid=3)
    // Padding: frame 2 has 4 unused bytes that need to be padded with zeros before the CRC.
    const uint_least8_t payload[8] = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17 };
    const uint32_t      can_id     = make_v1v1_msg_can_id(canard_prio_nominal, 1000U, 10U);

    // For v1 multiframe CRC, compute over all payload bytes, then the CRC residue is checked.
    // The library accumulates crc_add(seed, payload_of_each_frame) where the last frame includes padding+crc.
    // For the residue to be 0, we compute CRC over the payload, then append the CRC big-endian.
    // Frame 1: 7 payload bytes + tail.
    uint_least8_t frame1[8];
    std::memcpy(frame1, payload, 7U);
    frame1[7] = make_v1_tail(true, false, true, 3U);

    // Frame 2: 1 remaining payload byte + 4 padding zeros + CRC(2 big-endian) + tail.
    // But we only have 7 data slots in frame2 (8 total - 1 tail).
    // Content: [payload[7], pad, pad, pad, pad, crc_hi, crc_lo] + tail
    // Wait -- the CRC is computed over payload AND padding. Let me recompute.
    // Actual data in the frame stream (before tail): payload(7) | payload(1) + padding + CRC(2)
    // Frame 2 data (7 bytes): [0x17, 0x00, 0x00, 0x00, 0x00, crc_hi, crc_lo]
    // The CRC covers: all payload(8) + padding(4) bytes, then the CRC result is appended.
    // Recompute: crc_over_all = crc16(0xFFFF, payload(8) + padding(4))
    const uint_least8_t padding[4] = { 0, 0, 0, 0 };
    uint16_t            crc        = crc16_ccitt(0xFFFFU, payload, 8U);
    crc                            = crc16_ccitt(crc, padding, 4U);
    const auto crc_hi              = static_cast<uint_least8_t>((static_cast<unsigned>(crc) >> 8U) & 0xFFU);
    const auto crc_lo              = static_cast<uint_least8_t>(crc & 0xFFU);

    uint_least8_t frame2[8];
    frame2[0] = payload[7]; // last payload byte
    frame2[1] = 0x00;       // padding
    frame2[2] = 0x00;
    frame2[3] = 0x00;
    frame2[4] = 0x00;
    frame2[5] = crc_hi;
    frame2[6] = crc_lo;
    frame2[7] = make_v1_tail(false, true, false, 3U); // toggle alternates: 1->0

    now_val = 100;
    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 100, 0U, can_id, canard_bytes_t{ .size = sizeof(frame1), .data = frame1 }));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count); // Not complete yet.

    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 200, 0U, can_id, canard_bytes_t{ .size = sizeof(frame2), .data = frame2 }));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_INT64(100, cap.timestamp); // Timestamp of the first frame.
    TEST_ASSERT_EQUAL_UINT8(canard_prio_nominal, cap.priority);
    TEST_ASSERT_EQUAL_UINT8(10U, cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(3U, cap.transfer_id);
    // The delivered payload size is total_size - 2 (CRC stripped), where total_size includes the padding.
    // Frame 1 contributes 7 bytes, frame 2 contributes 7 bytes (1 payload + 4 padding + 2 CRC) => total_size=14.
    // Delivered size = min(14 - 2, extent) = 12.  The first 8 bytes are the original payload; bytes 8..11 are padding.
    TEST_ASSERT_EQUAL_size_t(12U, cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, cap.payload_buf, 8U);
    // Padding bytes should be zero.
    for (size_t i = 8; i < 12; i++) {
        TEST_ASSERT_EQUAL_UINT8(0x00U, cap.payload_buf[i]);
    }

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: v1 3-frame multiframe  -------------------------------------------

static void test_rx_v1_multiframe_3frame()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 2000U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    // 15 bytes of payload. Classic CAN, MTU=8, 7 data bytes per frame.
    // Frame 1 (SOT): 7 payload bytes, tail(SOT=1,EOT=0,toggle=1,tid=5)
    // Frame 2 (mid): 7 payload bytes, tail(SOT=0,EOT=0,toggle=0,tid=5)
    // Frame 3 (EOT): 1 payload byte + 4 pad + CRC(2) = 7, tail(SOT=0,EOT=1,toggle=1,tid=5)
    const uint_least8_t payload[15] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                        0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F };
    const uint32_t      can_id      = make_v1v1_msg_can_id(canard_prio_high, 2000U, 20U);

    // CRC over payload(15) + padding(4).
    const uint_least8_t pad[4] = { 0, 0, 0, 0 };
    uint16_t            crc    = crc16_ccitt(0xFFFFU, payload, 15U);
    crc                        = crc16_ccitt(crc, pad, 4U);
    const auto crc_hi          = static_cast<uint_least8_t>((static_cast<unsigned>(crc) >> 8U) & 0xFFU);
    const auto crc_lo          = static_cast<uint_least8_t>(crc & 0xFFU);

    uint_least8_t frame1[8];
    std::memcpy(frame1, payload, 7U);
    frame1[7] = make_v1_tail(true, false, true, 5U); // SOT, toggle=1

    uint_least8_t frame2[8];
    std::memcpy(frame2, &payload[7], 7U);
    frame2[7] = make_v1_tail(false, false, false, 5U); // mid, toggle=0

    uint_least8_t frame3[8];
    frame3[0] = payload[14]; // last payload byte
    frame3[1] = 0x00;        // padding
    frame3[2] = 0x00;
    frame3[3] = 0x00;
    frame3[4] = 0x00;
    frame3[5] = crc_hi;
    frame3[6] = crc_lo;
    frame3[7] = make_v1_tail(false, true, true, 5U); // EOT, toggle=1

    now_val = 100;
    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 100, 0U, can_id, canard_bytes_t{ .size = sizeof(frame1), .data = frame1 }));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count);

    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 200, 0U, can_id, canard_bytes_t{ .size = sizeof(frame2), .data = frame2 }));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count);

    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 300, 0U, can_id, canard_bytes_t{ .size = sizeof(frame3), .data = frame3 }));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_INT64(100, cap.timestamp);
    TEST_ASSERT_EQUAL_UINT8(canard_prio_high, cap.priority);
    TEST_ASSERT_EQUAL_UINT8(20U, cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(5U, cap.transfer_id);
    // Delivered size = total_size - 2 = (7+7+7) - 2 = 19.  First 15 bytes are payload, then 4 padding zeros.
    TEST_ASSERT_EQUAL_size_t(19U, cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, cap.payload_buf, 15U);
    for (size_t i = 15; i < 19; i++) {
        TEST_ASSERT_EQUAL_UINT8(0x00U, cap.payload_buf[i]);
    }

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: v1 2-frame CAN FD multiframe  ------------------------------------

static void test_rx_v1_multiframe_fd()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 3000U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    // CAN FD: MTU=64, payload per frame=63. A 70-byte payload needs 2 frames.
    // Frame 1 (SOT): 63 payload bytes + tail(SOT=1,EOT=0,toggle=1,tid=1)
    // Frame 2 (EOT): 7 remaining payload bytes + padding(52) + CRC(2) + tail(SOT=0,EOT=1,toggle=0,tid=1)
    uint_least8_t payload[70];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = static_cast<uint_least8_t>(i & 0xFFU);
    }
    const uint32_t can_id = make_v1v1_msg_can_id(canard_prio_nominal, 3000U, 30U);

    // CRC covers payload(70) + padding(52).
    const uint_least8_t pad[52] = {};
    uint16_t            crc     = crc16_ccitt(0xFFFFU, payload, 70U);
    crc                         = crc16_ccitt(crc, pad, 52U);
    const auto crc_hi           = static_cast<uint_least8_t>((static_cast<unsigned>(crc) >> 8U) & 0xFFU);
    const auto crc_lo           = static_cast<uint_least8_t>(crc & 0xFFU);

    // Frame 1 (64 bytes).
    uint_least8_t frame1[64];
    std::memcpy(frame1, payload, 63U);
    frame1[63] = make_v1_tail(true, false, true, 1U);

    // Frame 2 (64 bytes): 7 payload + 52 padding + 2 CRC + 1 tail = 62; total data = 63; frame size = 64.
    uint_least8_t frame2[64];
    std::memcpy(frame2, &payload[63], 7U);
    std::memset(&frame2[7], 0, 52U); // padding
    frame2[59] = crc_hi;             // CRC at byte offset 59
    frame2[60] = crc_lo;             // CRC at byte offset 60
    frame2[61] = 0;                  // unused (but we need full 64-byte frame for non-last MTU check)
    frame2[62] = 0;                  // unused
    frame2[63] = make_v1_tail(false, true, false, 1U);

    // Wait, the last frame does not need to be full MTU. Let me reconsider.
    // rx_parse: non-last must use full MTU (payload_raw.size >= CANARD_MTU_CAN_CLASSIC=8). But for FD the check
    // is `end || (payload_raw.size >= CANARD_MTU_CAN_CLASSIC)`. The check uses CANARD_MTU_CAN_CLASSIC (8), not 64.
    // So the last frame can be shorter. But the CRC position depends on how many bytes are in the last frame.
    //
    // Actually, looking more carefully at the library code:
    // `(end || (payload_raw.size >= CANARD_MTU_CAN_CLASSIC))` -- this means non-last frames just need >= 8 bytes.
    // The library does NOT enforce MTU=64 for FD frames on the RX side. It just requires >= 8 for non-last.
    //
    // For the last frame (EOT), any size >= 1 is acceptable (just needs the tail byte).
    // The last frame carries: remaining_payload + CRC(2 bytes big-endian) + tail.
    // We have 7 remaining payload bytes. No padding needed if we send just 7+2+1=10 bytes.
    // But wait -- the CRC is computed over ALL payload data seen in the stream. The library does:
    //   crc = crc_add(crc, frame_payload_without_tail)
    // So we should NOT have padding unless needed for DLC rounding. Since this is raw frame injection,
    // we can choose the exact frame size. Let me use a compact last frame (no padding).

    // Recompute CRC without padding.
    crc                = crc16_ccitt(0xFFFFU, payload, 70U);
    const auto crc_hi2 = static_cast<uint_least8_t>((static_cast<unsigned>(crc) >> 8U) & 0xFFU);
    const auto crc_lo2 = static_cast<uint_least8_t>(crc & 0xFFU);

    // Frame 2 (10 bytes): 7 payload + CRC(2) + tail.
    uint_least8_t frame2b[10];
    std::memcpy(frame2b, &payload[63], 7U);
    frame2b[7] = crc_hi2;
    frame2b[8] = crc_lo2;
    frame2b[9] = make_v1_tail(false, true, false, 1U);

    now_val = 500;
    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 500, 0U, can_id, canard_bytes_t{ .size = sizeof(frame1), .data = frame1 }));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count);

    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 600, 0U, can_id, canard_bytes_t{ .size = sizeof(frame2b), .data = frame2b }));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(30U, cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(1U, cap.transfer_id);
    TEST_ASSERT_EQUAL_size_t(70U, cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, cap.payload_buf, 70U);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: multiframe CRC error  --------------------------------------------

static void test_rx_multiframe_crc_error()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 1100U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    const uint_least8_t payload[8] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22 };
    const uint32_t      can_id     = make_v1v1_msg_can_id(canard_prio_nominal, 1100U, 10U);

    // Correct CRC (for sanity reference).
    const uint_least8_t pad[4] = {};
    uint16_t            crc    = crc16_ccitt(0xFFFFU, payload, 8U);
    crc                        = crc16_ccitt(crc, pad, 4U);

    // Corrupt the CRC by flipping the low byte.
    const auto bad_crc_hi = static_cast<uint_least8_t>((static_cast<unsigned>(crc) >> 8U) & 0xFFU);
    const auto bad_crc_lo = static_cast<uint_least8_t>((crc & 0xFFU) ^ 0x01U); // flipped bit

    uint_least8_t frame1[8];
    std::memcpy(frame1, payload, 7U);
    frame1[7] = make_v1_tail(true, false, true, 0U);

    uint_least8_t frame2[8];
    frame2[0] = payload[7];
    frame2[1] = 0x00;
    frame2[2] = 0x00;
    frame2[3] = 0x00;
    frame2[4] = 0x00;
    frame2[5] = bad_crc_hi;
    frame2[6] = bad_crc_lo;
    frame2[7] = make_v1_tail(false, true, false, 0U);

    const uint64_t err_before = self.err.rx_transfer;
    now_val                   = 100;
    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 100, 0U, can_id, canard_bytes_t{ .size = sizeof(frame1), .data = frame1 }));
    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 200, 0U, can_id, canard_bytes_t{ .size = sizeof(frame2), .data = frame2 }));

    TEST_ASSERT_EQUAL_size_t(0U, cap.count);                         // No delivery.
    TEST_ASSERT_EQUAL_UINT64(err_before + 1U, self.err.rx_transfer); // Error counter incremented.

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: single bit flip in payload  --------------------------------------

static void test_rx_multiframe_single_bit_flip()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 1200U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    const uint_least8_t payload[8] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 };
    const uint32_t      can_id     = make_v1v1_msg_can_id(canard_prio_nominal, 1200U, 10U);

    // Compute correct CRC.
    const uint_least8_t pad[4] = {};
    uint16_t            crc    = crc16_ccitt(0xFFFFU, payload, 8U);
    crc                        = crc16_ccitt(crc, pad, 4U);
    const auto crc_hi          = static_cast<uint_least8_t>((static_cast<unsigned>(crc) >> 8U) & 0xFFU);
    const auto crc_lo          = static_cast<uint_least8_t>(crc & 0xFFU);

    // Frame 1 with a single bit flip in byte 3.
    uint_least8_t frame1[8];
    std::memcpy(frame1, payload, 7U);
    frame1[3] ^= 0x04U; // flip one bit in the 4th payload byte
    frame1[7] = make_v1_tail(true, false, true, 9U);

    uint_least8_t frame2[8];
    frame2[0] = payload[7];
    frame2[1] = 0x00;
    frame2[2] = 0x00;
    frame2[3] = 0x00;
    frame2[4] = 0x00;
    frame2[5] = crc_hi;
    frame2[6] = crc_lo;
    frame2[7] = make_v1_tail(false, true, false, 9U);

    const uint64_t err_before = self.err.rx_transfer;
    now_val                   = 100;
    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 100, 0U, can_id, canard_bytes_t{ .size = sizeof(frame1), .data = frame1 }));
    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 200, 0U, can_id, canard_bytes_t{ .size = sizeof(frame2), .data = frame2 }));

    TEST_ASSERT_EQUAL_size_t(0U, cap.count);
    TEST_ASSERT_EQUAL_UINT64(err_before + 1U, self.err.rx_transfer);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: extent truncation single-frame  ----------------------------------

static void test_rx_extent_truncation_single_frame()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    // Subscribe with extent=4; payload will be 6 bytes.
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 1300U, 4U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 1300U, 10U);
    const uint_least8_t  frame[]  = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, make_v1_single_tail(0U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };

    now_val = 100;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    // For single-frame, the view points into the raw CAN buffer. The library does not truncate single frames;
    // that is an application-level concern. Let's check what actually gets delivered.
    // Looking at the code: rx_session_complete_single_frame passes fr->payload directly, which is the
    // full payload minus the tail byte. The extent is not consulted for single-frame transfers.
    // So we expect the full 6 bytes.
    TEST_ASSERT_EQUAL_size_t(6U, cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8(0x01, cap.payload_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x06, cap.payload_buf[5]);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: extent truncation multiframe  ------------------------------------

static void test_rx_extent_truncation_multiframe()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    // Subscribe with extent=5. Payload will be 8 bytes. Expect truncation to 5.
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 1400U, 5U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    const uint_least8_t payload[8] = { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7 };
    const uint32_t      can_id     = make_v1v1_msg_can_id(canard_prio_nominal, 1400U, 10U);

    // CRC is computed over the full 8 payload bytes + 4 padding. The padding contributes to the
    // full CRC but the data in the slot is truncated to extent(5).
    const uint_least8_t pad[4] = {};
    uint16_t            crc    = crc16_ccitt(0xFFFFU, payload, 8U);
    crc                        = crc16_ccitt(crc, pad, 4U);
    const auto crc_hi          = static_cast<uint_least8_t>((static_cast<unsigned>(crc) >> 8U) & 0xFFU);
    const auto crc_lo          = static_cast<uint_least8_t>(crc & 0xFFU);

    uint_least8_t frame1[8];
    std::memcpy(frame1, payload, 7U);
    frame1[7] = make_v1_tail(true, false, true, 2U);

    uint_least8_t frame2[8];
    frame2[0] = payload[7];
    frame2[1] = 0x00;
    frame2[2] = 0x00;
    frame2[3] = 0x00;
    frame2[4] = 0x00;
    frame2[5] = crc_hi;
    frame2[6] = crc_lo;
    frame2[7] = make_v1_tail(false, true, false, 2U);

    now_val = 100;
    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 100, 0U, can_id, canard_bytes_t{ .size = sizeof(frame1), .data = frame1 }));
    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 200, 0U, can_id, canard_bytes_t{ .size = sizeof(frame2), .data = frame2 }));

    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    // Delivered payload should be truncated to 5 bytes.
    // The total_size is 12 (8 payload + 4 pad), so size = min(total_size - 2, extent) = min(10, 5) = 5.
    TEST_ASSERT_EQUAL_size_t(5U, cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8(0xA0, cap.payload_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xA4, cap.payload_buf[4]);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: transfer-ID duplicate rejected  ----------------------------------

static void test_rx_transfer_id_duplicate_rejected()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 1500U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 1500U, 10U);
    const uint_least8_t  frame[]  = { 0xDEU, 0xADU, make_v1_single_tail(7U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };

    // First ingestion at t=0.
    now_val = 0;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 0, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);

    // Second ingestion with same TID at t=1000000 (within 2s timeout). Should be rejected as duplicate.
    now_val = 1000000;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 1000000, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count); // Still 1, not 2.

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: transfer-ID timeout boundary (accepted)  -------------------------

static void test_rx_transfer_id_timeout_boundary()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap         = {};
    canard_subscription_t sub         = {};
    const canard_us_t     tid_timeout = 2000000; // 2 seconds
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 1600U, 256U, tid_timeout, &capture_sub_vtable));
    sub.user_context = &cap;

    const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 1600U, 10U);
    const uint_least8_t  frame[]  = { 0xAAU, make_v1_single_tail(5U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };

    // First at t=0.
    now_val = 0;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 0, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);

    // Second at t=tid_timeout+1 (just past the timeout). Should be accepted.
    now_val = tid_timeout + 1;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, tid_timeout + 1, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(2U, cap.count);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: transfer-ID timeout within (rejected)  ---------------------------

static void test_rx_transfer_id_timeout_within()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap         = {};
    canard_subscription_t sub         = {};
    const canard_us_t     tid_timeout = 2000000;
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 1700U, 256U, tid_timeout, &capture_sub_vtable));
    sub.user_context = &cap;

    const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 1700U, 10U);
    const uint_least8_t  frame[]  = { 0xBBU, make_v1_single_tail(5U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };

    // First at t=0.
    now_val = 0;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 0, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);

    // Second at t=tid_timeout-1 (just before timeout). Should be rejected.
    // The admission check: stale = (ts - timeout) > last_admission_ts, i.e., (1999999 - 2000000) > 0.
    // Since we use signed arithmetic, 1999999 - 2000000 = -1, which is NOT > 0. So stale is false.
    // fresh = (tid != last_tid) || (prio != last_prio). Same tid and same prio => fresh = false.
    // Result: rejected. Good.
    now_val = tid_timeout - 1;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, tid_timeout - 1, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count); // Still 1.

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: cross-interface duplicate ----------------------------------------

static void test_rx_duplicate_cross_interface()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 1800U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 1800U, 10U);
    const uint_least8_t  frame[]  = { 0xCCU, make_v1_single_tail(0U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };

    // First on interface 0.
    now_val = 100;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);

    // Same transfer on interface 1. Should be rejected (different interface, same TID, within timeout).
    now_val = 101;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 101, 1U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count); // Still 1.

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: priority preemption  ---------------------------------------------

static void test_rx_priority_preemption()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 1900U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    const uint_least8_t payload_lo[8] = { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7 };
    const uint_least8_t payload_hi[8] = { 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7 };

    // Low-priority (prio=4) transfer with TID=0, from node 10.
    const uint32_t      can_id_lo   = make_v1v1_msg_can_id(canard_prio_nominal, 1900U, 10U);
    const uint_least8_t pad[4]      = {};
    uint16_t            crc_lo_full = crc16_ccitt(0xFFFFU, payload_lo, 8U);
    crc_lo_full                     = crc16_ccitt(crc_lo_full, pad, 4U);

    uint_least8_t frame1_lo[8];
    std::memcpy(frame1_lo, payload_lo, 7U);
    frame1_lo[7] = make_v1_tail(true, false, true, 0U);

    uint_least8_t frame2_lo[8];
    frame2_lo[0] = payload_lo[7];
    std::memset(&frame2_lo[1], 0, 4);
    frame2_lo[5] = static_cast<uint_least8_t>((static_cast<unsigned>(crc_lo_full) >> 8U) & 0xFFU);
    frame2_lo[6] = static_cast<uint_least8_t>(crc_lo_full & 0xFFU);
    frame2_lo[7] = make_v1_tail(false, true, false, 0U);

    // High-priority (prio=2) transfer with TID=1, from same node 10.
    const uint32_t can_id_hi   = make_v1v1_msg_can_id(canard_prio_fast, 1900U, 10U);
    uint16_t       crc_hi_full = crc16_ccitt(0xFFFFU, payload_hi, 8U);
    crc_hi_full                = crc16_ccitt(crc_hi_full, pad, 4U);

    uint_least8_t frame1_hi[8];
    std::memcpy(frame1_hi, payload_hi, 7U);
    frame1_hi[7] = make_v1_tail(true, false, true, 1U);

    uint_least8_t frame2_hi[8];
    frame2_hi[0] = payload_hi[7];
    std::memset(&frame2_hi[1], 0, 4);
    frame2_hi[5] = static_cast<uint_least8_t>((static_cast<unsigned>(crc_hi_full) >> 8U) & 0xFFU);
    frame2_hi[6] = static_cast<uint_least8_t>(crc_hi_full & 0xFFU);
    frame2_hi[7] = make_v1_tail(false, true, false, 1U);

    now_val = 100;
    // Start low-priority.
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id_lo, canard_bytes_t{ .size = 8, .data = frame1_lo }));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count);
    // Start high-priority (preemption -- different priority slot).
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 110, 0U, can_id_hi, canard_bytes_t{ .size = 8, .data = frame1_hi }));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count);
    // Complete low-priority.
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 120, 0U, can_id_lo, canard_bytes_t{ .size = 8, .data = frame2_lo }));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(canard_prio_nominal, cap.priority);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_lo, cap.payload_buf, 8U);
    // Complete high-priority.
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 130, 0U, can_id_hi, canard_bytes_t{ .size = 8, .data = frame2_hi }));
    TEST_ASSERT_EQUAL_size_t(2U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(canard_prio_fast, cap.priority);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_hi, cap.payload_buf, 8U);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: priority preemption interleaved  ---------------------------------

static void test_rx_priority_preemption_interleaved()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 2100U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    const uint_least8_t payload_lo[8] = { 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7 };
    const uint_least8_t payload_hi[8] = { 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7 };
    const uint_least8_t pad[4]        = {};

    const uint32_t can_id_lo = make_v1v1_msg_can_id(canard_prio_nominal, 2100U, 10U);
    const uint32_t can_id_hi = make_v1v1_msg_can_id(canard_prio_fast, 2100U, 10U);

    // Build frames for both transfers.
    const uint16_t crc_lo = crc16_ccitt(crc16_ccitt(0xFFFFU, payload_lo, 8U), pad, 4U);
    const uint16_t crc_hi = crc16_ccitt(crc16_ccitt(0xFFFFU, payload_hi, 8U), pad, 4U);

    uint_least8_t f1_lo[8];
    uint_least8_t f2_lo[8];
    uint_least8_t f1_hi[8];
    uint_least8_t f2_hi[8];

    std::memcpy(f1_lo, payload_lo, 7U);
    f1_lo[7] = make_v1_tail(true, false, true, 0U);
    f2_lo[0] = payload_lo[7];
    std::memset(&f2_lo[1], 0, 4);
    f2_lo[5] = static_cast<uint_least8_t>((static_cast<unsigned>(crc_lo) >> 8U) & 0xFFU);
    f2_lo[6] = static_cast<uint_least8_t>(crc_lo & 0xFFU);
    f2_lo[7] = make_v1_tail(false, true, false, 0U);

    std::memcpy(f1_hi, payload_hi, 7U);
    f1_hi[7] = make_v1_tail(true, false, true, 1U);
    f2_hi[0] = payload_hi[7];
    std::memset(&f2_hi[1], 0, 4);
    f2_hi[5] = static_cast<uint_least8_t>((static_cast<unsigned>(crc_hi) >> 8U) & 0xFFU);
    f2_hi[6] = static_cast<uint_least8_t>(crc_hi & 0xFFU);
    f2_hi[7] = make_v1_tail(false, true, false, 1U);

    now_val = 100;
    // Interleave: lo_frame1, hi_frame1, lo_frame2, hi_frame2.
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id_lo, canard_bytes_t{ .size = 8, .data = f1_lo }));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count);

    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 110, 0U, can_id_hi, canard_bytes_t{ .size = 8, .data = f1_hi }));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count);

    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 120, 0U, can_id_lo, canard_bytes_t{ .size = 8, .data = f2_lo }));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(canard_prio_nominal, cap.priority);

    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 130, 0U, can_id_hi, canard_bytes_t{ .size = 8, .data = f2_hi }));
    TEST_ASSERT_EQUAL_size_t(2U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(canard_prio_fast, cap.priority);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: anonymous single-frame accepted  ---------------------------------

static void test_rx_anonymous_single_frame_accepted()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    // Subscribe to v1.0 subject 100 (anonymous nodes use v1.0 format with bit24=1 and src field as discriminator).
    TEST_ASSERT_TRUE(canard_subscribe_13b(&self, &sub, 100U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    // v1.0 anonymous message: prio[28:26] | 0 | 1(bit24=anon) | subject_id[20:8] | bit7=0 | discriminator[6:0]
    // The source field in the CAN ID is a pseudo-CRC of the payload (discriminator), not a real node-ID.
    const uint32_t can_id = (static_cast<uint32_t>(canard_prio_nominal) << 26U) | (UINT32_C(1) << 24U) |
                            (static_cast<uint32_t>(100U) << 8U) | 0x55U; // discriminator = 0x55

    // v1 anonymous is single-frame only, toggle must start at 1 (v1 start toggle).
    const uint_least8_t  frame[]  = { 0xCAU, 0xFEU, make_v1_single_tail(0U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };

    now_val = 100;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(canard_prio_nominal, cap.priority);
    TEST_ASSERT_EQUAL_size_t(2U, cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8(0xCAU, cap.payload_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFEU, cap.payload_buf[1]);
    // Source should be CANARD_NODE_ID_ANONYMOUS (0xFF).
    TEST_ASSERT_EQUAL_UINT8(CANARD_NODE_ID_ANONYMOUS, cap.source_node_id);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: anonymous multiframe rejected  -----------------------------------

static void test_rx_anonymous_multiframe_rejected()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_13b(&self, &sub, 200U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    // Construct a v1.0 anonymous frame that looks like SOT but not EOT (attempting multiframe).
    // For v1.0: prio[28:26] | 0 | bit24=1(anon) | subject_id[20:8] | bit7=0 | discriminator[6:0]
    const uint32_t can_id = (static_cast<uint32_t>(canard_prio_nominal) << 26U) | (UINT32_C(1) << 24U) |
                            (static_cast<uint32_t>(200U) << 8U) | 0x33U;

    // SOT=1, EOT=0, toggle=1 (v1 start). This should be rejected because anonymous cannot be multiframe.
    // The rx_parse function enforces: `is_v1 = is_v1 && start && end` for anonymous frames.
    const uint_least8_t frame[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, make_v1_tail(true, false, true, 0U) };

    const uint64_t err_before = self.err.rx_frame;
    now_val                   = 100;
    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 100, 0U, can_id, canard_bytes_t{ .size = sizeof(frame), .data = frame }));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count);
    // The frame should be unparseable as v1, and potentially parseable as v0 but with no matching subscription.
    // If neither parses, rx_frame error is incremented. Let's check:
    // For v1: is_v1 check requires start && end for anonymous, but we have start && !end => is_v1 = false.
    // For v0: src = 0x33 != 0, so it's not anonymous in v0. port_id = (can_id>>8)&0xFFFF = some value.
    //         The v0 parse may succeed, but there's no v0 subscription for it, so it's just silently dropped.
    // So the rx_frame error may not be incremented (v0 parse succeeds). Let's just verify no delivery.
    (void)err_before; // The v0 parse may succeed, so we don't assert on error count.

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: empty frame (malformed)  -----------------------------------------

static void test_rx_malformed_empty_frame()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 2200U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 2200U, 10U);
    const canard_bytes_t can_data = { .size = 0, .data = nullptr };

    const uint64_t err_before = self.err.rx_frame;
    now_val                   = 100;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count);
    TEST_ASSERT_EQUAL_UINT64(err_before + 1U, self.err.rx_frame);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: wrong toggle rejected  -------------------------------------------

static void test_rx_wrong_toggle_rejected()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 2300U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    const uint_least8_t payload[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    const uint32_t      can_id     = make_v1v1_msg_can_id(canard_prio_nominal, 2300U, 10U);

    // Correct CRC.
    const uint_least8_t pad[4] = {};
    uint16_t            crc    = crc16_ccitt(0xFFFFU, payload, 8U);
    crc                        = crc16_ccitt(crc, pad, 4U);
    const auto crc_hi          = static_cast<uint_least8_t>((static_cast<unsigned>(crc) >> 8U) & 0xFFU);
    const auto crc_lo          = static_cast<uint_least8_t>(crc & 0xFFU);

    // Frame 1 (SOT): toggle=1 (correct for v1).
    uint_least8_t frame1[8];
    std::memcpy(frame1, payload, 7U);
    frame1[7] = make_v1_tail(true, false, true, 4U);

    // Frame 2 (EOT): toggle=1 (WRONG! should be 0).
    uint_least8_t frame2[8];
    frame2[0] = payload[7];
    std::memset(&frame2[1], 0, 4);
    frame2[5] = crc_hi;
    frame2[6] = crc_lo;
    frame2[7] = make_v1_tail(false, true, true, 4U); // toggle=1, should be 0

    now_val = 100;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id, canard_bytes_t{ .size = 8, .data = frame1 }));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count);

    // The second frame has the wrong toggle. The admission solver checks:
    // slot->expected_toggle == fr->toggle. Expected is 0 (toggled from initial 1), but we sent 1. Rejected.
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 200, 0U, can_id, canard_bytes_t{ .size = 8, .data = frame2 }));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count); // Never completes.

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: interface affinity  ----------------------------------------------

static void test_rx_interface_affinity()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 2400U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    const uint_least8_t payload[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    const uint32_t      can_id     = make_v1v1_msg_can_id(canard_prio_nominal, 2400U, 10U);

    const uint_least8_t pad[4] = {};
    uint16_t            crc    = crc16_ccitt(0xFFFFU, payload, 8U);
    crc                        = crc16_ccitt(crc, pad, 4U);
    const auto crc_hi          = static_cast<uint_least8_t>((static_cast<unsigned>(crc) >> 8U) & 0xFFU);
    const auto crc_lo          = static_cast<uint_least8_t>(crc & 0xFFU);

    uint_least8_t frame1[8];
    std::memcpy(frame1, payload, 7U);
    frame1[7] = make_v1_tail(true, false, true, 6U);

    uint_least8_t frame2[8];
    frame2[0] = payload[7];
    std::memset(&frame2[1], 0, 4);
    frame2[5] = crc_hi;
    frame2[6] = crc_lo;
    frame2[7] = make_v1_tail(false, true, false, 6U);

    now_val = 100;
    // Start on interface 0.
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id, canard_bytes_t{ .size = 8, .data = frame1 }));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count);

    // Continuation on interface 1. Should be dropped (interface affinity mismatch).
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 200, 1U, can_id, canard_bytes_t{ .size = 8, .data = frame2 }));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count); // Transfer never completes.

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: stale session cleanup  -------------------------------------------

static void test_rx_stale_session_cleanup()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap         = {};
    canard_subscription_t sub         = {};
    const canard_us_t     tid_timeout = 2000000;
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 2500U, 256U, tid_timeout, &capture_sub_vtable));
    sub.user_context = &cap;

    const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 2500U, 10U);
    const uint_least8_t  frame1[] = { 0xDDU, make_v1_single_tail(5U) };
    const canard_bytes_t can_data = { .size = sizeof(frame1), .data = frame1 };

    // Receive first transfer at t=0.
    now_val = 0;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 0, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);

    // Advance time well past session timeout (30s).
    // Call poll() enough times to trigger session cleanup.
    // RX_SESSION_TIMEOUT is 30 * MEGA = 30_000_000 us.
    // The session is destroyed when: last_admission_ts < (now - transfer_id_timeout)
    // and there are no in-progress slots.
    // We need to poll enough times because poll only cleans the oldest session per call.
    now_val = 35000000; // 35 seconds.
    canard_poll(&self, 0U);

    // Now send the same transfer-ID again. It should be accepted because the session was destroyed.
    const uint_least8_t  frame2[]  = { 0xEEU, make_v1_single_tail(5U) };
    const canard_bytes_t can_data2 = { .size = sizeof(frame2), .data = frame2 };
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 35000000, 0U, can_id, can_data2));
    TEST_ASSERT_EQUAL_size_t(2U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(0xEEU, cap.payload_buf[0]);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Test: v0 multiframe roundtrip  -----------------------------------------

static void test_rx_v0_multiframe_roundtrip()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};

    // v0 data type signature for UAVCAN.protocol.GetNodeInfo (a well-known type).
    const uint64_t data_type_signature = 0xEE468A8121C46A9EULL;
    const uint16_t crc_seed            = canard_v0_crc_seed_from_data_type_signature(data_type_signature);

    // The extent represents user payload size; the library adds CRC overhead internally.
    TEST_ASSERT_TRUE(canard_v0_subscribe(&self, &sub, 1000U, crc_seed, 8U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    const uint_least8_t payload[8] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 };
    const uint32_t      can_id     = make_v0_msg_can_id(canard_prio_nominal, 1000U, 10U);

    // v0 multiframe CRC: computed over the user payload only, then prepended little-endian.
    const uint16_t v0crc = crc16_ccitt(crc_seed, payload, 8U);

    // Total stream data: [crc_lo, crc_hi, payload[0..7]]  = 10 bytes.
    // Classic CAN MTU=8, 7 data bytes per frame + tail byte.
    // Frame 1 (SOT): [crc_lo, crc_hi, payload[0..4]] + tail(SOT=1,EOT=0,toggle=0,tid=2) -- 7 data + 1 tail
    // Frame 2 (EOT): [payload[5..7]] + tail(SOT=0,EOT=1,toggle=1,tid=2) -- 3 data + 1 tail (last frame, short ok)
    //
    // Wait -- is the last frame allowed to be short? Yes, rx_parse: `end || (payload_raw.size >= 8)`.
    // So the last frame (end=true) can be < 8 bytes.
    //
    // But we need to be careful: the CRC in rx_session_accept for v0:
    //   For the first frame (start): crc_input = payload[2:] (skipping the 2 CRC bytes).
    //   For subsequent frames: crc_input = payload (full frame payload minus tail).
    // Then at completion: slot->crc is compared against slot->payload[0]|(slot->payload[1]<<8).
    //
    // The rx_slot_advance stores the data into the slot, and the CRC is accumulated separately.
    // So frame 1 stores: [crc_lo, crc_hi, payload[0..4]] = 7 bytes into the slot.
    // CRC accumulation on frame 1: crc_add(crc_seed, payload[0..4]) -- first 5 payload bytes.
    //   (because for v0 start frame, crc_input = frame_payload[2:])
    // Frame 2 stores: [payload[5..7]] = 3 bytes into the slot.
    // CRC accumulation on frame 2: crc_add(crc, payload[5..7]) -- remaining 3 payload bytes.
    // Total CRC: crc16(crc_seed, payload[0..7]) which equals v0crc.
    // Verification: slot->crc == slot->payload[0] | (slot->payload[1] << 8)
    //   = crc_lo | (crc_hi << 8) = v0crc. Correct!

    uint_least8_t frame1[8];
    frame1[0] = static_cast<uint_least8_t>(v0crc & 0xFFU);                                // crc_lo
    frame1[1] = static_cast<uint_least8_t>((static_cast<unsigned>(v0crc) >> 8U) & 0xFFU); // crc_hi
    std::memcpy(&frame1[2], payload, 5U);                                                 // first 5 payload bytes
    frame1[7] = make_v0_tail(true, false, false, 2U); // SOT=1, EOT=0, toggle=0 (v0 starts toggle=0)

    uint_least8_t frame2[4];
    std::memcpy(frame2, &payload[5], 3U);            // remaining 3 payload bytes
    frame2[3] = make_v0_tail(false, true, true, 2U); // SOT=0, EOT=1, toggle=1

    now_val = 100;
    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 100, 0U, can_id, canard_bytes_t{ .size = sizeof(frame1), .data = frame1 }));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count);

    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 200, 0U, can_id, canard_bytes_t{ .size = sizeof(frame2), .data = frame2 }));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_INT64(100, cap.timestamp);
    TEST_ASSERT_EQUAL_UINT8(canard_prio_nominal, cap.priority);
    TEST_ASSERT_EQUAL_UINT8(10U, cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(2U, cap.transfer_id);
    // The delivered payload excludes the 2-byte CRC prefix: 8 bytes of user data.
    TEST_ASSERT_EQUAL_size_t(8U, cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, cap.payload_buf, 8U);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// Regression test: v0 extent represents user payload size; CRC overhead is added by the library internally.
// Before the fix, the user had to pass extent+2 manually. Now extent=N means N bytes of user data are accepted.
// This test uses a tight extent equal to the payload size. With the old (buggy) behavior, extent=5 would mean
// only 3 bytes of user payload after subtracting CRC, causing truncation. With the fix, all 5 bytes arrive.
static void test_rx_v0_extent_excludes_crc()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};

    const uint64_t data_type_signature = 0xEE468A8121C46A9EULL;
    const uint16_t crc_seed            = canard_v0_crc_seed_from_data_type_signature(data_type_signature);

    // extent=10 means we want up to 10 bytes of user payload (CRC handled internally).
    TEST_ASSERT_TRUE(canard_v0_subscribe(&self, &sub, 2000U, crc_seed, 10U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    // 10-byte user payload, which requires 3 classic CAN frames with v0 framing:
    // Total stream: [crc_lo, crc_hi, payload[0..9]] = 12 bytes, 7 data bytes per frame + tail.
    // Frame 1 (SOT): [crc_lo, crc_hi, payload[0..4]] + tail  = 8 bytes
    // Frame 2 (MID): [payload[5..9]]                 + padding*2 + tail = 8 bytes
    //   Wait -- actually padding only in the last frame. Let me recompute.
    //   Frame 2 is not the last frame if we need more data. 12 bytes total, 7 per frame = ceil(12/7) = 2 frames.
    //   Frame 1: 7 bytes of data + tail = [crc_lo, crc_hi, p0, p1, p2, p3, p4, tail]
    //   Frame 2: 5 bytes of data + padding + CRC? No -- v0 prepends CRC, no CRC at end.
    //   Frame 2 (EOT): [p5, p6, p7, p8, p9, tail] = 6 bytes (last frame can be short).
    const uint_least8_t payload[10] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A };
    const uint32_t      can_id      = make_v0_msg_can_id(canard_prio_nominal, 2000U, 10U);
    const uint16_t      v0crc       = crc16_ccitt(crc_seed, payload, 10U);

    uint_least8_t frame1[8];
    frame1[0] = static_cast<uint_least8_t>(v0crc & 0xFFU);
    frame1[1] = static_cast<uint_least8_t>((static_cast<unsigned>(v0crc) >> 8U) & 0xFFU);
    std::memcpy(&frame1[2], payload, 5U);
    frame1[7] = make_v0_tail(true, false, false, 5U);

    uint_least8_t frame2[6];
    std::memcpy(frame2, &payload[5], 5U);
    frame2[5] = make_v0_tail(false, true, true, 5U);

    now_val = 100;
    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 100, 0U, can_id, canard_bytes_t{ .size = sizeof(frame1), .data = frame1 }));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count);

    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 200, 0U, can_id, canard_bytes_t{ .size = sizeof(frame2), .data = frame2 }));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    // All 10 bytes must be delivered -- extent=10 means 10 bytes of user payload.
    TEST_ASSERT_EQUAL_size_t(10U, cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, cap.payload_buf, 10U);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// Regression: v0 extent truncation works correctly at the user-payload level.
// Subscribe with extent=4, send 10 bytes -- only 4 user bytes should be delivered.
static void test_rx_v0_extent_truncation()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};

    const uint64_t data_type_signature = 0xEE468A8121C46A9EULL;
    const uint16_t crc_seed            = canard_v0_crc_seed_from_data_type_signature(data_type_signature);

    // extent=4 means at most 4 bytes of user payload. CRC overhead handled internally.
    TEST_ASSERT_TRUE(canard_v0_subscribe(&self, &sub, 3000U, crc_seed, 4U, 2000000, &capture_sub_vtable));
    sub.user_context = &cap;

    // 8-byte user payload → after truncation only 4 bytes should be delivered.
    const uint_least8_t payload[8] = { 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8 };
    const uint32_t      can_id     = make_v0_msg_can_id(canard_prio_nominal, 3000U, 10U);
    // CRC is over the full payload (not truncated).
    const uint16_t v0crc = crc16_ccitt(crc_seed, payload, 8U);

    // Stream: [crc_lo, crc_hi, payload[0..7]] = 10 bytes → 2 frames.
    uint_least8_t frame1[8];
    frame1[0] = static_cast<uint_least8_t>(v0crc & 0xFFU);
    frame1[1] = static_cast<uint_least8_t>((static_cast<unsigned>(v0crc) >> 8U) & 0xFFU);
    std::memcpy(&frame1[2], payload, 5U);
    frame1[7] = make_v0_tail(true, false, false, 7U);

    uint_least8_t frame2[4];
    std::memcpy(frame2, &payload[5], 3U);
    frame2[3] = make_v0_tail(false, true, true, 7U);

    now_val = 100;
    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 100, 0U, can_id, canard_bytes_t{ .size = sizeof(frame1), .data = frame1 }));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count);

    TEST_ASSERT_TRUE(
      canard_ingest_frame(&self, 200, 0U, can_id, canard_bytes_t{ .size = sizeof(frame2), .data = frame2 }));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    // Only 4 bytes of user data delivered due to extent truncation.
    TEST_ASSERT_EQUAL_size_t(4U, cap.payload_size);
    const uint_least8_t expected[4] = { 0xA1, 0xA2, 0xA3, 0xA4 };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, cap.payload_buf, 4U);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Harness  ---------------------------------------------------------------

extern "C" void setUp() {}
extern "C" void tearDown() {}

int main()
{
    UNITY_BEGIN();

    // v1 multiframe reassembly.
    RUN_TEST(test_rx_v1_multiframe_2frame_classic);
    RUN_TEST(test_rx_v1_multiframe_3frame);
    RUN_TEST(test_rx_v1_multiframe_fd);

    // CRC integrity.
    RUN_TEST(test_rx_multiframe_crc_error);
    RUN_TEST(test_rx_multiframe_single_bit_flip);

    // Extent truncation.
    RUN_TEST(test_rx_extent_truncation_single_frame);
    RUN_TEST(test_rx_extent_truncation_multiframe);

    // Transfer-ID deduplication.
    RUN_TEST(test_rx_transfer_id_duplicate_rejected);
    RUN_TEST(test_rx_transfer_id_timeout_boundary);
    RUN_TEST(test_rx_transfer_id_timeout_within);

    // Redundant interface handling.
    RUN_TEST(test_rx_duplicate_cross_interface);

    // Priority preemption.
    RUN_TEST(test_rx_priority_preemption);
    RUN_TEST(test_rx_priority_preemption_interleaved);

    // Anonymous transfers.
    RUN_TEST(test_rx_anonymous_single_frame_accepted);
    RUN_TEST(test_rx_anonymous_multiframe_rejected);

    // Malformed frames.
    RUN_TEST(test_rx_malformed_empty_frame);
    RUN_TEST(test_rx_wrong_toggle_rejected);

    // Interface affinity.
    RUN_TEST(test_rx_interface_affinity);

    // Session lifecycle.
    RUN_TEST(test_rx_stale_session_cleanup);

    // Legacy v0.
    RUN_TEST(test_rx_v0_multiframe_roundtrip);
    RUN_TEST(test_rx_v0_extent_excludes_crc);
    RUN_TEST(test_rx_v0_extent_truncation);

    return UNITY_END();
}
