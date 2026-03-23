// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal Development Team.
//
// Adversarial TX-to-RX roundtrip tests: publish on a TX instance, poll to capture CAN frames via the TX callback,
// then feed every captured frame into a separate RX instance and verify the RX callback delivers the correct payload.

#include "helpers.h"
#include <unity.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// =====================================================================================================================
// Infrastructure
// =====================================================================================================================

// CRC-16/CCITT-FALSE used by Cyphal v1 multi-frame transfers.
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

static uint16_t crc16_ccitt(uint16_t crc, const void* data, const size_t len)
{
    const auto* p = static_cast<const uint_least8_t*>(data);
    for (size_t i = 0; i < len; i++) {
        crc = static_cast<uint16_t>((static_cast<unsigned>(crc) << 8U) ^
                                    crc_lut[(static_cast<unsigned>(crc) >> 8U) ^ p[i]]);
    }
    return crc;
}

// ------------------------------------------------  Allocator  --------------------------------------------------------

static void* std_alloc_mem(const canard_mem_t, const size_t size) { return std::malloc(size); }
static void  std_free_mem(const canard_mem_t, const size_t, void* const pointer) { std::free(pointer); }

static const canard_mem_vtable_t std_mem_vtable = { .free = std_free_mem, .alloc = std_alloc_mem };

static canard_mem_set_t make_std_memory()
{
    const canard_mem_t r = { .vtable = &std_mem_vtable, .context = nullptr };
    return canard_mem_set_t{ .tx_transfer = r, .tx_frame = r, .rx_session = r, .rx_payload = r, .rx_filters = r };
}

// ------------------------------------------------  TX Capture  -------------------------------------------------------

struct full_tx_record_t
{
    canard_us_t   deadline;
    uint_least8_t iface_index;
    bool          fd;
    uint32_t      can_id;
    size_t        data_size;
    uint_least8_t data[64]; // max CAN FD frame
};

struct full_tx_capture_t
{
    canard_us_t                       now;
    size_t                            count;
    std::array<full_tx_record_t, 128> records;
};

static full_tx_capture_t* tx_capture_from(const canard_t* const self)
{
    return static_cast<full_tx_capture_t*>(self->user_context);
}

static canard_us_t tx_capture_now(const canard_t* const self) { return tx_capture_from(self)->now; }

static bool tx_capture_tx(canard_t* const self,
                          void* const,
                          const canard_us_t    deadline,
                          const uint_least8_t  iface_index,
                          const bool           fd,
                          const uint32_t       extended_can_id,
                          const canard_bytes_t can_data)
{
    full_tx_capture_t* const cap = tx_capture_from(self);
    TEST_ASSERT_NOT_NULL(cap);
    if (cap->count < cap->records.size()) {
        full_tx_record_t& rec = cap->records[cap->count];
        rec.deadline          = deadline;
        rec.iface_index       = iface_index;
        rec.fd                = fd;
        rec.can_id            = extended_can_id;
        rec.data_size         = can_data.size;
        if ((can_data.size > 0U) && (can_data.data != nullptr)) {
            const size_t n = (can_data.size < sizeof(rec.data)) ? can_data.size : sizeof(rec.data);
            std::memcpy(rec.data, can_data.data, n);
        }
    }
    cap->count++;
    return true; // Always accept.
}

static const canard_vtable_t tx_vtable = { .now = tx_capture_now, .tx = tx_capture_tx, .filter = nullptr };

// ------------------------------------------------  RX Capture  -------------------------------------------------------

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

// For multi-frame transfers the origin must be freed by the application.
static void roundtrip_on_message(canard_subscription_t* const self,
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
    if ((payload.origin.size > 0) && (payload.origin.data != nullptr)) {
        std::free(payload.origin.data);
    }
}

static const canard_subscription_vtable_t roundtrip_sub_vtable = { .on_message = roundtrip_on_message };

// ------------------------------------------------  RX Helper (mock now)  ---------------------------------------------

struct rx_context_t
{
    canard_us_t   now_val;
    rx_capture_t* capture; // not used by now callback but handy to keep together
};

static canard_us_t rx_now(const canard_t* const self)
{
    return static_cast<const rx_context_t*>(self->user_context)->now_val;
}

static bool rx_tx(canard_t* const,
                  void* const,
                  const canard_us_t,
                  const uint_least8_t,
                  const bool,
                  const uint32_t,
                  const canard_bytes_t)
{
    return false; // RX instance never transmits.
}

static const canard_vtable_t rx_vtable = { .now = rx_now, .tx = rx_tx, .filter = nullptr };

// ------------------------------------------------  Roundtrip Harness  ------------------------------------------------

static constexpr uint_least8_t TX_NODE_ID = 42U;
static constexpr uint_least8_t RX_NODE_ID = 99U;
static constexpr canard_us_t   DEADLINE   = 1000000;
static constexpr canard_us_t   TIMESTAMP  = 5000;
static constexpr size_t        EXTENT     = 512U;

/// Sets up the TX instance with full-frame capture. Caller owns both objects.
static void init_tx(canard_t* const tx, full_tx_capture_t* const cap, const uint_least8_t node_id)
{
    *cap       = full_tx_capture_t{};
    cap->now   = 0;
    cap->count = 0;
    TEST_ASSERT_TRUE(canard_new(tx, &tx_vtable, make_std_memory(), 64U, 0xCAFEU, 0U));
    TEST_ASSERT_TRUE(canard_set_node_id(tx, node_id));
    tx->user_context = cap;
}

/// Sets up the RX instance. Caller owns both objects.
static void init_rx(canard_t* const rx, rx_context_t* const ctx, const uint_least8_t node_id)
{
    *ctx         = rx_context_t{};
    ctx->now_val = 0;
    ctx->capture = nullptr;
    TEST_ASSERT_TRUE(canard_new(rx, &rx_vtable, make_std_memory(), 16U, 0xBEEFU, 0U));
    TEST_ASSERT_TRUE(canard_set_node_id(rx, node_id));
    rx->user_context = ctx;
}

/// Feed all captured TX frames (for iface 0 only) into the RX instance.
static void feed_captured_frames(canard_t* const rx, const full_tx_capture_t& cap, const canard_us_t timestamp)
{
    for (size_t i = 0; i < cap.count; i++) {
        if (cap.records[i].iface_index == 0U) {
            const canard_bytes_t frame_data = { .size = cap.records[i].data_size, .data = cap.records[i].data };
            canard_ingest_frame(rx, timestamp, 0U, cap.records[i].can_id, frame_data);
        }
    }
}

// =====================================================================================================================
// Test 1: v1.1 single-frame Classic CAN, 4-byte payload
// =====================================================================================================================
static void test_roundtrip_v1v1_single_frame_classic()
{
    canard_t          tx_inst = {};
    full_tx_capture_t tx_cap  = {};
    init_tx(&tx_inst, &tx_cap, TX_NODE_ID);
    tx_inst.tx.fd = false; // Classic CAN

    canard_t     rx_inst = {};
    rx_context_t rx_ctx  = {};
    rx_capture_t rx_cap  = {};
    init_rx(&rx_inst, &rx_ctx, RX_NODE_ID);
    rx_ctx.now_val = TIMESTAMP;

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe(
      &rx_inst, &sub, 100U, false, EXTENT, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &roundtrip_sub_vtable));
    sub.user_context = &rx_cap;

    const uint_least8_t        payload_data[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    const canard_bytes_chain_t payload         = { .bytes = { .size = 4, .data = payload_data }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_publish(&tx_inst, DEADLINE, 1U, canard_prio_nominal, 100U, false, 0U, payload, nullptr));

    canard_poll(&tx_inst, 1U);
    TEST_ASSERT_EQUAL_size_t(1U, tx_cap.count); // Single frame.
    TEST_ASSERT_FALSE(tx_cap.records[0].fd);

    feed_captured_frames(&rx_inst, tx_cap, TIMESTAMP);

    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
    TEST_ASSERT_EQUAL_UINT8(canard_prio_nominal, rx_cap.priority);
    TEST_ASSERT_EQUAL_UINT8(TX_NODE_ID, rx_cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(0U, rx_cap.transfer_id);
    TEST_ASSERT_EQUAL_size_t(4U, rx_cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_data, rx_cap.payload_buf, 4U);

    canard_unsubscribe(&rx_inst, &sub);
    canard_destroy(&rx_inst);
    canard_destroy(&tx_inst);
}

// =====================================================================================================================
// Test 2: v1.1 single-frame CAN FD, 30-byte payload
// =====================================================================================================================
static void test_roundtrip_v1v1_single_frame_fd()
{
    canard_t          tx_inst = {};
    full_tx_capture_t tx_cap  = {};
    init_tx(&tx_inst, &tx_cap, TX_NODE_ID);
    // FD is default (tx.fd = true).

    canard_t     rx_inst = {};
    rx_context_t rx_ctx  = {};
    rx_capture_t rx_cap  = {};
    init_rx(&rx_inst, &rx_ctx, RX_NODE_ID);
    rx_ctx.now_val = TIMESTAMP;

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe(
      &rx_inst, &sub, 200U, false, EXTENT, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &roundtrip_sub_vtable));
    sub.user_context = &rx_cap;

    uint_least8_t payload_data[30];
    for (size_t i = 0; i < sizeof(payload_data); i++) {
        payload_data[i] = static_cast<uint_least8_t>(i + 1U);
    }
    const canard_bytes_chain_t payload = { .bytes = { .size = 30, .data = payload_data }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_publish(&tx_inst, DEADLINE, 1U, canard_prio_fast, 200U, false, 5U, payload, nullptr));

    canard_poll(&tx_inst, 1U);
    TEST_ASSERT_EQUAL_size_t(1U, tx_cap.count); // Single frame (30+1=31 < 64).
    TEST_ASSERT_TRUE(tx_cap.records[0].fd);

    feed_captured_frames(&rx_inst, tx_cap, TIMESTAMP);

    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
    TEST_ASSERT_EQUAL_UINT8(canard_prio_fast, rx_cap.priority);
    TEST_ASSERT_EQUAL_UINT8(TX_NODE_ID, rx_cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(5U, rx_cap.transfer_id);
    // 30 payload + 1 tail = 31 bytes. CAN FD DLC rounds 31 up to 32. RX sees 32-1(tail) = 31 bytes.
    // The first 30 bytes are the original payload; byte 31 is DLC padding (zero).
    TEST_ASSERT_EQUAL_size_t(31U, rx_cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_data, rx_cap.payload_buf, 30U);
    TEST_ASSERT_EQUAL_UINT8(0U, rx_cap.payload_buf[30]); // DLC padding byte.

    canard_unsubscribe(&rx_inst, &sub);
    canard_destroy(&rx_inst);
    canard_destroy(&tx_inst);
}

// =====================================================================================================================
// Test 3: v1.1 multiframe Classic CAN, 8-byte payload (exactly triggers multiframe: 8+1 > 8)
// =====================================================================================================================
static void test_roundtrip_v1v1_multiframe_classic_2frames()
{
    canard_t          tx_inst = {};
    full_tx_capture_t tx_cap  = {};
    init_tx(&tx_inst, &tx_cap, TX_NODE_ID);
    tx_inst.tx.fd = false;

    canard_t     rx_inst = {};
    rx_context_t rx_ctx  = {};
    rx_capture_t rx_cap  = {};
    init_rx(&rx_inst, &rx_ctx, RX_NODE_ID);
    rx_ctx.now_val = TIMESTAMP;

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe(
      &rx_inst, &sub, 300U, false, EXTENT, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &roundtrip_sub_vtable));
    sub.user_context = &rx_cap;

    const uint_least8_t        payload_data[8] = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17 };
    const canard_bytes_chain_t payload         = { .bytes = { .size = 8, .data = payload_data }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_publish(&tx_inst, DEADLINE, 1U, canard_prio_nominal, 300U, false, 3U, payload, nullptr));

    canard_poll(&tx_inst, 1U);
    TEST_ASSERT_TRUE(tx_cap.count >= 2U); // At least 2 frames for multiframe.

    feed_captured_frames(&rx_inst, tx_cap, TIMESTAMP);

    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
    TEST_ASSERT_EQUAL_UINT8(canard_prio_nominal, rx_cap.priority);
    TEST_ASSERT_EQUAL_UINT8(TX_NODE_ID, rx_cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(3U, rx_cap.transfer_id);
    // Multiframe delivers payload+padding (CRC stripped). The original 8 bytes must be intact.
    TEST_ASSERT_TRUE(rx_cap.payload_size >= 8U);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_data, rx_cap.payload_buf, 8U);

    canard_unsubscribe(&rx_inst, &sub);
    canard_destroy(&rx_inst);
    canard_destroy(&tx_inst);
}

// =====================================================================================================================
// Test 4: v1.1 multiframe Classic CAN, 20-byte payload (~4 frames)
// =====================================================================================================================
static void test_roundtrip_v1v1_multiframe_classic_many()
{
    canard_t          tx_inst = {};
    full_tx_capture_t tx_cap  = {};
    init_tx(&tx_inst, &tx_cap, TX_NODE_ID);
    tx_inst.tx.fd = false;

    canard_t     rx_inst = {};
    rx_context_t rx_ctx  = {};
    rx_capture_t rx_cap  = {};
    init_rx(&rx_inst, &rx_ctx, RX_NODE_ID);
    rx_ctx.now_val = TIMESTAMP;

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe(
      &rx_inst, &sub, 400U, false, EXTENT, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &roundtrip_sub_vtable));
    sub.user_context = &rx_cap;

    uint_least8_t payload_data[20];
    for (size_t i = 0; i < sizeof(payload_data); i++) {
        payload_data[i] = static_cast<uint_least8_t>(0xA0U + i);
    }
    const canard_bytes_chain_t payload = { .bytes = { .size = 20, .data = payload_data }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_publish(&tx_inst, DEADLINE, 1U, canard_prio_nominal, 400U, false, 7U, payload, nullptr));

    canard_poll(&tx_inst, 1U);
    TEST_ASSERT_TRUE(tx_cap.count >= 3U); // 20 bytes over classic CAN => at least 4 frames.

    feed_captured_frames(&rx_inst, tx_cap, TIMESTAMP);

    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
    TEST_ASSERT_EQUAL_UINT8(TX_NODE_ID, rx_cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(7U, rx_cap.transfer_id);
    TEST_ASSERT_TRUE(rx_cap.payload_size >= 20U);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_data, rx_cap.payload_buf, 20U);

    canard_unsubscribe(&rx_inst, &sub);
    canard_destroy(&rx_inst);
    canard_destroy(&tx_inst);
}

// =====================================================================================================================
// Test 5: v1.1 multiframe CAN FD, 70-byte payload (2 frames)
// =====================================================================================================================
static void test_roundtrip_v1v1_multiframe_fd()
{
    canard_t          tx_inst = {};
    full_tx_capture_t tx_cap  = {};
    init_tx(&tx_inst, &tx_cap, TX_NODE_ID);
    // FD is default.

    canard_t     rx_inst = {};
    rx_context_t rx_ctx  = {};
    rx_capture_t rx_cap  = {};
    init_rx(&rx_inst, &rx_ctx, RX_NODE_ID);
    rx_ctx.now_val = TIMESTAMP;

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe(
      &rx_inst, &sub, 500U, false, EXTENT, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &roundtrip_sub_vtable));
    sub.user_context = &rx_cap;

    uint_least8_t payload_data[70];
    for (size_t i = 0; i < sizeof(payload_data); i++) {
        payload_data[i] = static_cast<uint_least8_t>(i & 0xFFU);
    }
    const canard_bytes_chain_t payload = { .bytes = { .size = 70, .data = payload_data }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_publish(&tx_inst, DEADLINE, 1U, canard_prio_nominal, 500U, false, 1U, payload, nullptr));

    canard_poll(&tx_inst, 1U);
    TEST_ASSERT_TRUE(tx_cap.count >= 2U); // 70 bytes over FD => 2 frames.

    feed_captured_frames(&rx_inst, tx_cap, TIMESTAMP);

    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
    TEST_ASSERT_EQUAL_UINT8(TX_NODE_ID, rx_cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(1U, rx_cap.transfer_id);
    TEST_ASSERT_TRUE(rx_cap.payload_size >= 70U);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_data, rx_cap.payload_buf, 70U);

    canard_unsubscribe(&rx_inst, &sub);
    canard_destroy(&rx_inst);
    canard_destroy(&tx_inst);
}

// =====================================================================================================================
// Test 6: v1.0 single-frame message (rev_1v0=true), 5-byte payload, subject_id=4000
// =====================================================================================================================
static void test_roundtrip_v1v0_single_frame()
{
    canard_t          tx_inst = {};
    full_tx_capture_t tx_cap  = {};
    init_tx(&tx_inst, &tx_cap, TX_NODE_ID);
    tx_inst.tx.fd = false; // v1.0 typically Classic CAN.

    canard_t     rx_inst = {};
    rx_context_t rx_ctx  = {};
    rx_capture_t rx_cap  = {};
    init_rx(&rx_inst, &rx_ctx, RX_NODE_ID);
    rx_ctx.now_val = TIMESTAMP;

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe(
      &rx_inst, &sub, 4000U, true /*rev_1v0*/, EXTENT, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &roundtrip_sub_vtable));
    sub.user_context = &rx_cap;

    const uint_least8_t        payload_data[5] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
    const canard_bytes_chain_t payload         = { .bytes = { .size = 5, .data = payload_data }, .next = nullptr };
    TEST_ASSERT_TRUE(
      canard_publish(&tx_inst, DEADLINE, 1U, canard_prio_high, 4000U, true /*rev_1v0*/, 10U, payload, nullptr));

    canard_poll(&tx_inst, 1U);
    TEST_ASSERT_EQUAL_size_t(1U, tx_cap.count); // Single frame (5+1=6 < 8).

    // Verify v1.0 CAN ID: bit7 must be 0 (not v1.1).
    TEST_ASSERT_EQUAL_UINT8(0U, (tx_cap.records[0].can_id >> 7U) & 1U);

    feed_captured_frames(&rx_inst, tx_cap, TIMESTAMP);

    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
    TEST_ASSERT_EQUAL_UINT8(canard_prio_high, rx_cap.priority);
    TEST_ASSERT_EQUAL_UINT8(TX_NODE_ID, rx_cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(10U, rx_cap.transfer_id);
    TEST_ASSERT_EQUAL_size_t(5U, rx_cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_data, rx_cap.payload_buf, 5U);

    canard_unsubscribe(&rx_inst, &sub);
    canard_destroy(&rx_inst);
    canard_destroy(&tx_inst);
}

// =====================================================================================================================
// Test 7: v1.0 multiframe message, 15-byte payload Classic CAN
// =====================================================================================================================
static void test_roundtrip_v1v0_multiframe()
{
    canard_t          tx_inst = {};
    full_tx_capture_t tx_cap  = {};
    init_tx(&tx_inst, &tx_cap, TX_NODE_ID);
    tx_inst.tx.fd = false;

    canard_t     rx_inst = {};
    rx_context_t rx_ctx  = {};
    rx_capture_t rx_cap  = {};
    init_rx(&rx_inst, &rx_ctx, RX_NODE_ID);
    rx_ctx.now_val = TIMESTAMP;

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe(
      &rx_inst, &sub, 5000U, true /*rev_1v0*/, EXTENT, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &roundtrip_sub_vtable));
    sub.user_context = &rx_cap;

    uint_least8_t payload_data[15];
    for (size_t i = 0; i < sizeof(payload_data); i++) {
        payload_data[i] = static_cast<uint_least8_t>(0x50U + i);
    }
    const canard_bytes_chain_t payload = { .bytes = { .size = 15, .data = payload_data }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_publish(&tx_inst, DEADLINE, 1U, canard_prio_nominal, 5000U, true, 2U, payload, nullptr));

    canard_poll(&tx_inst, 1U);
    TEST_ASSERT_TRUE(tx_cap.count >= 3U); // 15 bytes classic CAN => 3+ frames.

    feed_captured_frames(&rx_inst, tx_cap, TIMESTAMP);

    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
    TEST_ASSERT_EQUAL_UINT8(TX_NODE_ID, rx_cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(2U, rx_cap.transfer_id);
    TEST_ASSERT_TRUE(rx_cap.payload_size >= 15U);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_data, rx_cap.payload_buf, 15U);

    canard_unsubscribe(&rx_inst, &sub);
    canard_destroy(&rx_inst);
    canard_destroy(&tx_inst);
}

// =====================================================================================================================
// Test 8: v1 service request, single-frame
// =====================================================================================================================
static void test_roundtrip_v1_service_request()
{
    canard_t          tx_inst = {};
    full_tx_capture_t tx_cap  = {};
    init_tx(&tx_inst, &tx_cap, TX_NODE_ID); // Client node.

    canard_t     rx_inst = {};
    rx_context_t rx_ctx  = {};
    rx_capture_t rx_cap  = {};
    init_rx(&rx_inst, &rx_ctx, RX_NODE_ID); // Server node.
    rx_ctx.now_val = TIMESTAMP;

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_request(
      &rx_inst, &sub, 50U, EXTENT, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &roundtrip_sub_vtable));
    sub.user_context = &rx_cap;

    const uint_least8_t        payload_data[3] = { 0xCA, 0xFE, 0x42 };
    const canard_bytes_chain_t payload         = { .bytes = { .size = 3, .data = payload_data }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_request(&tx_inst, DEADLINE, canard_prio_nominal, 50U, RX_NODE_ID, 0U, payload, nullptr));

    canard_poll(&tx_inst, 1U);
    TEST_ASSERT_EQUAL_size_t(1U, tx_cap.count);

    feed_captured_frames(&rx_inst, tx_cap, TIMESTAMP);

    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
    TEST_ASSERT_EQUAL_UINT8(TX_NODE_ID, rx_cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(0U, rx_cap.transfer_id);
    TEST_ASSERT_EQUAL_size_t(3U, rx_cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_data, rx_cap.payload_buf, 3U);

    canard_unsubscribe(&rx_inst, &sub);
    canard_destroy(&rx_inst);
    canard_destroy(&tx_inst);
}

// =====================================================================================================================
// Test 9: v1 service response, single-frame
// =====================================================================================================================
static void test_roundtrip_v1_service_response()
{
    // For a response: TX is the server (RX_NODE_ID), RX is the client (TX_NODE_ID).
    canard_t          tx_inst = {};
    full_tx_capture_t tx_cap  = {};
    init_tx(&tx_inst, &tx_cap, RX_NODE_ID); // Server node sends response.

    canard_t     rx_inst = {};
    rx_context_t rx_ctx  = {};
    rx_capture_t rx_cap  = {};
    init_rx(&rx_inst, &rx_ctx, TX_NODE_ID); // Client node receives response.
    rx_ctx.now_val = TIMESTAMP;

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_response(&rx_inst, &sub, 60U, EXTENT, &roundtrip_sub_vtable));
    sub.user_context = &rx_cap;

    const uint_least8_t        payload_data[4] = { 0xBE, 0xEF, 0x00, 0xFF };
    const canard_bytes_chain_t payload         = { .bytes = { .size = 4, .data = payload_data }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_respond(&tx_inst, DEADLINE, canard_prio_nominal, 60U, TX_NODE_ID, 15U, payload, nullptr));

    canard_poll(&tx_inst, 1U);
    TEST_ASSERT_EQUAL_size_t(1U, tx_cap.count);

    feed_captured_frames(&rx_inst, tx_cap, TIMESTAMP);

    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
    TEST_ASSERT_EQUAL_UINT8(RX_NODE_ID, rx_cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(15U, rx_cap.transfer_id);
    TEST_ASSERT_EQUAL_size_t(4U, rx_cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_data, rx_cap.payload_buf, 4U);

    canard_unsubscribe(&rx_inst, &sub);
    canard_destroy(&rx_inst);
    canard_destroy(&tx_inst);
}

// =====================================================================================================================
// Test 10: v1 service request multiframe, 20-byte payload
// =====================================================================================================================
static void test_roundtrip_v1_service_multiframe()
{
    canard_t          tx_inst = {};
    full_tx_capture_t tx_cap  = {};
    init_tx(&tx_inst, &tx_cap, TX_NODE_ID);
    tx_inst.tx.fd = false; // Force Classic CAN for multiframe.

    canard_t     rx_inst = {};
    rx_context_t rx_ctx  = {};
    rx_capture_t rx_cap  = {};
    init_rx(&rx_inst, &rx_ctx, RX_NODE_ID);
    rx_ctx.now_val = TIMESTAMP;

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_request(
      &rx_inst, &sub, 70U, EXTENT, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &roundtrip_sub_vtable));
    sub.user_context = &rx_cap;

    uint_least8_t payload_data[20];
    for (size_t i = 0; i < sizeof(payload_data); i++) {
        payload_data[i] = static_cast<uint_least8_t>(0xD0U + i);
    }
    const canard_bytes_chain_t payload = { .bytes = { .size = 20, .data = payload_data }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_request(&tx_inst, DEADLINE, canard_prio_high, 70U, RX_NODE_ID, 4U, payload, nullptr));

    canard_poll(&tx_inst, 1U);
    TEST_ASSERT_TRUE(tx_cap.count >= 3U); // 20 bytes classic CAN => multiframe.

    feed_captured_frames(&rx_inst, tx_cap, TIMESTAMP);

    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
    TEST_ASSERT_EQUAL_UINT8(TX_NODE_ID, rx_cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(4U, rx_cap.transfer_id);
    TEST_ASSERT_TRUE(rx_cap.payload_size >= 20U);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_data, rx_cap.payload_buf, 20U);

    canard_unsubscribe(&rx_inst, &sub);
    canard_destroy(&rx_inst);
    canard_destroy(&tx_inst);
}

// =====================================================================================================================
// Test 11: All 8 priority levels roundtrip
// =====================================================================================================================
static void test_roundtrip_all_priorities()
{
    for (uint_least8_t prio = 0; prio < CANARD_PRIO_COUNT; prio++) {
        canard_t          tx_inst = {};
        full_tx_capture_t tx_cap  = {};
        init_tx(&tx_inst, &tx_cap, TX_NODE_ID);

        canard_t     rx_inst = {};
        rx_context_t rx_ctx  = {};
        rx_capture_t rx_cap  = {};
        init_rx(&rx_inst, &rx_ctx, RX_NODE_ID);
        rx_ctx.now_val = TIMESTAMP;

        canard_subscription_t sub = {};
        TEST_ASSERT_TRUE(canard_subscribe(
          &rx_inst, &sub, 600U, false, EXTENT, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &roundtrip_sub_vtable));
        sub.user_context = &rx_cap;

        const uint_least8_t        payload_data[2] = { 0x11, 0x22 };
        const canard_bytes_chain_t payload         = { .bytes = { .size = 2, .data = payload_data }, .next = nullptr };
        TEST_ASSERT_TRUE(
          canard_publish(&tx_inst, DEADLINE, 1U, static_cast<canard_prio_t>(prio), 600U, false, 0U, payload, nullptr));

        canard_poll(&tx_inst, 1U);
        feed_captured_frames(&rx_inst, tx_cap, TIMESTAMP);

        TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
        TEST_ASSERT_EQUAL_UINT8(prio, rx_cap.priority);
        TEST_ASSERT_EQUAL_size_t(2U, rx_cap.payload_size);

        canard_unsubscribe(&rx_inst, &sub);
        canard_destroy(&rx_inst);
        canard_destroy(&tx_inst);
    }
}

// =====================================================================================================================
// Test 12: All 32 transfer IDs roundtrip
// =====================================================================================================================
static void test_roundtrip_all_transfer_ids()
{
    canard_t          tx_inst = {};
    full_tx_capture_t tx_cap  = {};
    init_tx(&tx_inst, &tx_cap, TX_NODE_ID);

    canard_t     rx_inst = {};
    rx_context_t rx_ctx  = {};
    rx_capture_t rx_cap  = {};
    init_rx(&rx_inst, &rx_ctx, RX_NODE_ID);

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe(
      &rx_inst, &sub, 700U, false, EXTENT, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &roundtrip_sub_vtable));
    sub.user_context = &rx_cap;

    for (unsigned tid = 0; tid <= CANARD_TRANSFER_ID_MAX; tid++) {
        tx_cap = full_tx_capture_t{};
        rx_cap = rx_capture_t{};
        // Advance the RX clock past the TID timeout so same TID is accepted again.
        rx_ctx.now_val = static_cast<canard_us_t>(tid) * (CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us + 1);

        const uint_least8_t        payload_data[1] = { static_cast<uint_least8_t>(tid & 0xFFU) };
        const canard_bytes_chain_t payload         = { .bytes = { .size = 1, .data = payload_data }, .next = nullptr };
        TEST_ASSERT_TRUE(canard_publish(&tx_inst,
                                        DEADLINE + rx_ctx.now_val,
                                        1U,
                                        canard_prio_nominal,
                                        700U,
                                        false,
                                        static_cast<uint_least8_t>(tid),
                                        payload,
                                        nullptr));

        canard_poll(&tx_inst, 1U);
        feed_captured_frames(&rx_inst, tx_cap, rx_ctx.now_val);

        TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
        TEST_ASSERT_EQUAL_UINT8(tid, rx_cap.transfer_id);
        TEST_ASSERT_EQUAL_size_t(1U, rx_cap.payload_size);
        TEST_ASSERT_EQUAL_UINT8(tid, rx_cap.payload_buf[0]);
    }

    canard_unsubscribe(&rx_inst, &sub);
    canard_destroy(&rx_inst);
    canard_destroy(&tx_inst);
}

// =====================================================================================================================
// Test 13: Zero-length (empty) payload
// =====================================================================================================================
static void test_roundtrip_empty_payload()
{
    canard_t          tx_inst = {};
    full_tx_capture_t tx_cap  = {};
    init_tx(&tx_inst, &tx_cap, TX_NODE_ID);

    canard_t     rx_inst = {};
    rx_context_t rx_ctx  = {};
    rx_capture_t rx_cap  = {};
    init_rx(&rx_inst, &rx_ctx, RX_NODE_ID);
    rx_ctx.now_val = TIMESTAMP;

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe(
      &rx_inst, &sub, 800U, false, EXTENT, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &roundtrip_sub_vtable));
    sub.user_context = &rx_cap;

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = nullptr }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_publish(&tx_inst, DEADLINE, 1U, canard_prio_nominal, 800U, false, 0U, payload, nullptr));

    canard_poll(&tx_inst, 1U);
    TEST_ASSERT_EQUAL_size_t(1U, tx_cap.count); // Single frame with just the tail byte.

    feed_captured_frames(&rx_inst, tx_cap, TIMESTAMP);

    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
    TEST_ASSERT_EQUAL_UINT8(TX_NODE_ID, rx_cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(0U, rx_cap.transfer_id);
    TEST_ASSERT_EQUAL_size_t(0U, rx_cap.payload_size);

    canard_unsubscribe(&rx_inst, &sub);
    canard_destroy(&rx_inst);
    canard_destroy(&tx_inst);
}

// =====================================================================================================================
// Test 14: Boundary - exactly 7 bytes (max single-frame Classic CAN)
// =====================================================================================================================
static void test_roundtrip_boundary_7_bytes_classic()
{
    canard_t          tx_inst = {};
    full_tx_capture_t tx_cap  = {};
    init_tx(&tx_inst, &tx_cap, TX_NODE_ID);
    tx_inst.tx.fd = false;

    canard_t     rx_inst = {};
    rx_context_t rx_ctx  = {};
    rx_capture_t rx_cap  = {};
    init_rx(&rx_inst, &rx_ctx, RX_NODE_ID);
    rx_ctx.now_val = TIMESTAMP;

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe(
      &rx_inst, &sub, 900U, false, EXTENT, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &roundtrip_sub_vtable));
    sub.user_context = &rx_cap;

    const uint_least8_t        payload_data[7] = { 1, 2, 3, 4, 5, 6, 7 };
    const canard_bytes_chain_t payload         = { .bytes = { .size = 7, .data = payload_data }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_publish(&tx_inst, DEADLINE, 1U, canard_prio_nominal, 900U, false, 0U, payload, nullptr));

    canard_poll(&tx_inst, 1U);
    TEST_ASSERT_EQUAL_size_t(1U, tx_cap.count); // 7+1 = 8 = MTU, still single frame.

    feed_captured_frames(&rx_inst, tx_cap, TIMESTAMP);

    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
    TEST_ASSERT_EQUAL_size_t(7U, rx_cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_data, rx_cap.payload_buf, 7U);

    canard_unsubscribe(&rx_inst, &sub);
    canard_destroy(&rx_inst);
    canard_destroy(&tx_inst);
}

// =====================================================================================================================
// Test 15: Boundary - exactly 8 bytes (minimum multiframe Classic CAN)
// =====================================================================================================================
static void test_roundtrip_boundary_8_bytes_classic()
{
    canard_t          tx_inst = {};
    full_tx_capture_t tx_cap  = {};
    init_tx(&tx_inst, &tx_cap, TX_NODE_ID);
    tx_inst.tx.fd = false;

    canard_t     rx_inst = {};
    rx_context_t rx_ctx  = {};
    rx_capture_t rx_cap  = {};
    init_rx(&rx_inst, &rx_ctx, RX_NODE_ID);
    rx_ctx.now_val = TIMESTAMP;

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe(
      &rx_inst, &sub, 1000U, false, EXTENT, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &roundtrip_sub_vtable));
    sub.user_context = &rx_cap;

    const uint_least8_t        payload_data[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };
    const canard_bytes_chain_t payload         = { .bytes = { .size = 8, .data = payload_data }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_publish(&tx_inst, DEADLINE, 1U, canard_prio_nominal, 1000U, false, 0U, payload, nullptr));

    canard_poll(&tx_inst, 1U);
    TEST_ASSERT_TRUE(tx_cap.count >= 2U); // 8+1 > 8 => multiframe.

    feed_captured_frames(&rx_inst, tx_cap, TIMESTAMP);

    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
    TEST_ASSERT_TRUE(rx_cap.payload_size >= 8U);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_data, rx_cap.payload_buf, 8U);

    canard_unsubscribe(&rx_inst, &sub);
    canard_destroy(&rx_inst);
    canard_destroy(&tx_inst);
}

// =====================================================================================================================
// Test 16: Boundary - exactly 63 bytes (max single-frame CAN FD)
// =====================================================================================================================
static void test_roundtrip_boundary_63_bytes_fd()
{
    canard_t          tx_inst = {};
    full_tx_capture_t tx_cap  = {};
    init_tx(&tx_inst, &tx_cap, TX_NODE_ID);
    // FD is default.

    canard_t     rx_inst = {};
    rx_context_t rx_ctx  = {};
    rx_capture_t rx_cap  = {};
    init_rx(&rx_inst, &rx_ctx, RX_NODE_ID);
    rx_ctx.now_val = TIMESTAMP;

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe(
      &rx_inst, &sub, 1100U, false, EXTENT, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &roundtrip_sub_vtable));
    sub.user_context = &rx_cap;

    uint_least8_t payload_data[63];
    for (size_t i = 0; i < sizeof(payload_data); i++) {
        payload_data[i] = static_cast<uint_least8_t>(i ^ 0x55U);
    }
    const canard_bytes_chain_t payload = { .bytes = { .size = 63, .data = payload_data }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_publish(&tx_inst, DEADLINE, 1U, canard_prio_nominal, 1100U, false, 0U, payload, nullptr));

    canard_poll(&tx_inst, 1U);
    TEST_ASSERT_EQUAL_size_t(1U, tx_cap.count); // 63+1 = 64 = MTU, single frame.

    feed_captured_frames(&rx_inst, tx_cap, TIMESTAMP);

    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
    TEST_ASSERT_EQUAL_size_t(63U, rx_cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_data, rx_cap.payload_buf, 63U);

    canard_unsubscribe(&rx_inst, &sub);
    canard_destroy(&rx_inst);
    canard_destroy(&tx_inst);
}

// =====================================================================================================================
// Test 17: Boundary - exactly 64 bytes (minimum multiframe CAN FD)
// =====================================================================================================================
static void test_roundtrip_boundary_64_bytes_fd()
{
    canard_t          tx_inst = {};
    full_tx_capture_t tx_cap  = {};
    init_tx(&tx_inst, &tx_cap, TX_NODE_ID);
    // FD is default.

    canard_t     rx_inst = {};
    rx_context_t rx_ctx  = {};
    rx_capture_t rx_cap  = {};
    init_rx(&rx_inst, &rx_ctx, RX_NODE_ID);
    rx_ctx.now_val = TIMESTAMP;

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe(
      &rx_inst, &sub, 1200U, false, EXTENT, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &roundtrip_sub_vtable));
    sub.user_context = &rx_cap;

    uint_least8_t payload_data[64];
    for (size_t i = 0; i < sizeof(payload_data); i++) {
        payload_data[i] = static_cast<uint_least8_t>(i);
    }
    const canard_bytes_chain_t payload = { .bytes = { .size = 64, .data = payload_data }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_publish(&tx_inst, DEADLINE, 1U, canard_prio_nominal, 1200U, false, 0U, payload, nullptr));

    canard_poll(&tx_inst, 1U);
    TEST_ASSERT_TRUE(tx_cap.count >= 2U); // 64+1 > 64 => multiframe.

    feed_captured_frames(&rx_inst, tx_cap, TIMESTAMP);

    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
    TEST_ASSERT_TRUE(rx_cap.payload_size >= 64U);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload_data, rx_cap.payload_buf, 64U);

    canard_unsubscribe(&rx_inst, &sub);
    canard_destroy(&rx_inst);
    canard_destroy(&tx_inst);
}

// =====================================================================================================================
// Test 18: Scattered payload (3-fragment canard_bytes_chain_t), 3+4+5=12 bytes
// =====================================================================================================================
static void test_roundtrip_scattered_payload()
{
    canard_t          tx_inst = {};
    full_tx_capture_t tx_cap  = {};
    init_tx(&tx_inst, &tx_cap, TX_NODE_ID);

    canard_t     rx_inst = {};
    rx_context_t rx_ctx  = {};
    rx_capture_t rx_cap  = {};
    init_rx(&rx_inst, &rx_ctx, RX_NODE_ID);
    rx_ctx.now_val = TIMESTAMP;

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe(
      &rx_inst, &sub, 1300U, false, EXTENT, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &roundtrip_sub_vtable));
    sub.user_context = &rx_cap;

    // Three payload fragments: 3, 4, 5 bytes = 12 total.
    const uint_least8_t frag0[3] = { 0xAA, 0xBB, 0xCC };
    const uint_least8_t frag1[4] = { 0xDD, 0xEE, 0xFF, 0x11 };
    const uint_least8_t frag2[5] = { 0x22, 0x33, 0x44, 0x55, 0x66 };

    // Build chain: frag0 -> frag1 -> frag2 -> null.
    const canard_bytes_chain_t chain2 = { .bytes = { .size = 5, .data = frag2 }, .next = nullptr };
    const canard_bytes_chain_t chain1 = { .bytes = { .size = 4, .data = frag1 }, .next = &chain2 };
    const canard_bytes_chain_t chain0 = { .bytes = { .size = 3, .data = frag0 }, .next = &chain1 };

    TEST_ASSERT_TRUE(canard_publish(&tx_inst, DEADLINE, 1U, canard_prio_nominal, 1300U, false, 0U, chain0, nullptr));

    canard_poll(&tx_inst, 1U);
    TEST_ASSERT_TRUE(tx_cap.count >= 1U);

    feed_captured_frames(&rx_inst, tx_cap, TIMESTAMP);

    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);
    TEST_ASSERT_EQUAL_UINT8(TX_NODE_ID, rx_cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(0U, rx_cap.transfer_id);

    // Verify reassembled payload matches the concatenation of the three fragments.
    const uint_least8_t expected[12] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
    TEST_ASSERT_TRUE(rx_cap.payload_size >= 12U);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, rx_cap.payload_buf, 12U);

    canard_unsubscribe(&rx_inst, &sub);
    canard_destroy(&rx_inst);
    canard_destroy(&tx_inst);
}

// =====================================================================================================================
// Unity boilerplate
// =====================================================================================================================

extern "C" void setUp() {}
extern "C" void tearDown() {}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_roundtrip_v1v1_single_frame_classic);
    RUN_TEST(test_roundtrip_v1v1_single_frame_fd);
    RUN_TEST(test_roundtrip_v1v1_multiframe_classic_2frames);
    RUN_TEST(test_roundtrip_v1v1_multiframe_classic_many);
    RUN_TEST(test_roundtrip_v1v1_multiframe_fd);
    RUN_TEST(test_roundtrip_v1v0_single_frame);
    RUN_TEST(test_roundtrip_v1v0_multiframe);
    RUN_TEST(test_roundtrip_v1_service_request);
    RUN_TEST(test_roundtrip_v1_service_response);
    RUN_TEST(test_roundtrip_v1_service_multiframe);
    RUN_TEST(test_roundtrip_all_priorities);
    RUN_TEST(test_roundtrip_all_transfer_ids);
    RUN_TEST(test_roundtrip_empty_payload);
    RUN_TEST(test_roundtrip_boundary_7_bytes_classic);
    RUN_TEST(test_roundtrip_boundary_8_bytes_classic);
    RUN_TEST(test_roundtrip_boundary_63_bytes_fd);
    RUN_TEST(test_roundtrip_boundary_64_bytes_fd);
    RUN_TEST(test_roundtrip_scattered_payload);

    return UNITY_END();
}
