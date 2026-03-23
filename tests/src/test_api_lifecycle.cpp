// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal Development Team.

#include "helpers.h"
#include <unity.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// =====================================================================================================================
//                                            Allocator & Instance Setup
// =====================================================================================================================

static void* std_alloc_mem(const canard_mem_t, const size_t size) { return std::malloc(size); }
static void  std_free_mem(const canard_mem_t, const size_t, void* const pointer) { std::free(pointer); }

static const canard_mem_vtable_t std_mem_vtable = { .free = std_free_mem, .alloc = std_alloc_mem };

// =====================================================================================================================
//                                         TX / NOW / Filter Capture Helpers
// =====================================================================================================================

struct tx_record_t
{
    canard_us_t   deadline;
    uint_least8_t iface_index;
    bool          fd;  // cppcheck-suppress unusedStructMember
    uint32_t      can_id;
    size_t        can_data_size;
    uint_least8_t tail;
};

struct filter_record_t
{
    size_t invocation_count;
    size_t last_filter_count;
};

struct tx_capture_t
{
    canard_us_t     now;
    bool            accept_tx;
    size_t          count;
    tx_record_t     records[64];
    filter_record_t filter_rec;
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
    if (cap->count < (sizeof(cap->records) / sizeof(cap->records[0]))) {
        cap->records[cap->count] = tx_record_t{
            .deadline      = deadline,
            .iface_index   = iface_index,
            .fd            = fd,
            .can_id        = extended_can_id,
            .can_data_size = can_data.size,
            .tail          = 0U,
        };
        if ((can_data.size > 0U) && (can_data.data != nullptr)) {
            const auto* const bytes       = static_cast<const uint_least8_t*>(can_data.data);
            cap->records[cap->count].tail = bytes[can_data.size - 1U];
        }
    }
    cap->count++;
    return cap->accept_tx;
}

static bool capture_filter(canard_t* const self, const size_t filter_count, const canard_filter_t*)
{
    tx_capture_t* const cap = capture_from(self);
    cap->filter_rec.invocation_count++;
    cap->filter_rec.last_filter_count = filter_count;
    return true;
}

static const canard_vtable_t capture_vtable        = { .now = capture_now, .tx = capture_tx, .filter = nullptr };
static const canard_vtable_t capture_filter_vtable = { .now = capture_now, .tx = capture_tx, .filter = capture_filter };

// Minimal callbacks for canard_new() validity tests.
static canard_us_t mock_now(const canard_t* const) { return 0; }
static bool        mock_tx(canard_t* const,
                           void* const,
                           const canard_us_t,
                           const uint_least8_t,
                           const bool,
                           const uint32_t,
                           const canard_bytes_t)
{
    return false;
}
static const canard_vtable_t test_vtable = { .now = mock_now, .tx = mock_tx, .filter = nullptr };

static canard_mem_set_t make_std_memory()
{
    const canard_mem_t r = { .vtable = &std_mem_vtable, .context = nullptr };
    return canard_mem_set_t{ .tx_transfer = r, .tx_frame = r, .rx_session = r, .rx_payload = r, .rx_filters = r };
}

static void init_capture(canard_t* const        self,
                         tx_capture_t* const    cap,
                         const uint_least8_t    node_id,
                         const size_t           queue_capacity,
                         const size_t           filter_count,
                         const canard_vtable_t* vtable)
{
    std::memset(cap, 0, sizeof(*cap));
    cap->now       = 0;
    cap->accept_tx = true;
    cap->count     = 0;
    TEST_ASSERT_TRUE(canard_new(self, vtable, make_std_memory(), queue_capacity, 1234U, filter_count));
    TEST_ASSERT_TRUE(canard_set_node_id(self, node_id));
    self->user_context = cap;
}

static void init_with_capture(canard_t* const self, tx_capture_t* const cap)
{
    init_capture(self, cap, 42U, 16U, 0U, &capture_vtable);
}

// =====================================================================================================================
//                                           RX Capture Callback
// =====================================================================================================================

struct rx_capture_t
{
    size_t        count;
    canard_us_t   timestamp;
    canard_prio_t priority;
    uint_least8_t source_node_id;
    uint_least8_t transfer_id;
    size_t        payload_size;
    uint_least8_t payload_buf[256];
};

// For multi-frame transfers the origin must be freed by the application.
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
    if ((payload.origin.size > 0) && (payload.origin.data != nullptr)) {
        std::free(payload.origin.data);
    }
}

static const canard_subscription_vtable_t capture_sub_vtable = { .on_message = capture_on_message };

// =====================================================================================================================
//                                         CAN Frame Construction Helpers
// =====================================================================================================================

// v1.1 message: priority[28:26] | subject_id[25:8] | bit7=1(v1.1) | src[6:0]
static uint32_t make_v1v1_msg_can_id(const canard_prio_t prio, const uint16_t subject_id, const uint_least8_t src)
{
    return (static_cast<uint32_t>(prio) << 26U) | (static_cast<uint32_t>(subject_id) << 8U) | (UINT32_C(1) << 7U) |
           (static_cast<uint32_t>(src) & 0x7FU);
}

// Single-frame tail byte for v1: start=1, end=1, toggle=1 (v1 starts toggle=1).
static uint_least8_t make_v1_single_tail(const uint_least8_t tid) { return 0xE0U | (tid & 0x1FU); }

// Multiframe v1 tail byte helpers.
static uint_least8_t make_v1_start_tail(const bool toggle, const uint_least8_t tid)
{
    return static_cast<uint_least8_t>(0x80U | (toggle ? 0x20U : 0x00U) | (tid & 0x1FU));
}
static uint_least8_t make_v1_end_tail(const bool toggle, const uint_least8_t tid)
{
    return static_cast<uint_least8_t>(0x40U | (toggle ? 0x20U : 0x00U) | (tid & 0x1FU));
}

// =====================================================================================================================
//                                    CRC-16/CCITT-FALSE (same table as canard.c)
// =====================================================================================================================

static const uint16_t crc_table[256] = {
    0x0000U, 0x1021U, 0x2042U, 0x3063U, 0x4084U, 0x50A5U, 0x60C6U, 0x70E7U, 0x8108U, 0x9129U, 0xA14AU, 0xB16BU, 0xC18CU,
    0xD1ADU, 0xE1CEU, 0xF1EFU, 0x1231U, 0x0210U, 0x3273U, 0x2252U, 0x52B5U, 0x4294U, 0x72F7U, 0x62D6U, 0x9339U, 0x8318U,
    0xB37BU, 0xA35AU, 0xD3BDU, 0xC39CU, 0xF3FFU, 0xE3DEU, 0x2462U, 0x3443U, 0x0420U, 0x1401U, 0x64E6U, 0x74C7U, 0x44A4U,
    0x5485U, 0xA56AU, 0xB54BU, 0x8528U, 0x9509U, 0xE5EEU, 0xF5CFU, 0xC5ACU, 0xD58DU, 0x3653U, 0x2672U, 0x1611U, 0x0630U,
    0x76D7U, 0x66F6U, 0x5695U, 0x46B4U, 0xB75BU, 0xA77AU, 0x9719U, 0x8738U, 0xF7DFU, 0xE7FEU, 0xD79DU, 0xC7BCU, 0x48C4U,
    0x58E5U, 0x6886U, 0x78A7U, 0x0840U, 0x1861U, 0x2802U, 0x3823U, 0xC9CCU, 0xD9EDU, 0xE98EU, 0xF9AFU, 0x8948U, 0x9969U,
    0xA90AU, 0xB92BU, 0x5AF5U, 0x4AD4U, 0x7AB7U, 0x6A96U, 0x1A71U, 0x0A50U, 0x3A33U, 0x2A12U, 0xDBFDU, 0xCBDCU, 0xFBBFU,
    0xEB9EU, 0x9B79U, 0x8B58U, 0xBB3BU, 0xAB1AU, 0x6CA6U, 0x7C87U, 0x4CE4U, 0x5CC5U, 0x2C22U, 0x3C03U, 0x0C60U, 0x1C41U,
    0xEDAEU, 0xFD8FU, 0xCDECU, 0xDDCDU, 0xAD2AU, 0xBD0BU, 0x8D68U, 0x9D49U, 0x7E97U, 0x6EB6U, 0x5ED5U, 0x4EF4U, 0x3E13U,
    0x2E32U, 0x1E51U, 0x0E70U, 0xFF9FU, 0xEFBEU, 0xDFDDU, 0xCFFCU, 0xBF1BU, 0xAF3AU, 0x9F59U, 0x8F78U, 0x9188U, 0x81A9U,
    0xB1CAU, 0xA1EBU, 0xD10CU, 0xC12DU, 0xF14EU, 0xE16FU, 0x1080U, 0x00A1U, 0x30C2U, 0x20E3U, 0x5004U, 0x4025U, 0x7046U,
    0x6067U, 0x83B9U, 0x9398U, 0xA3FBU, 0xB3DAU, 0xC33DU, 0xD31CU, 0xE37FU, 0xF35EU, 0x02B1U, 0x1290U, 0x22F3U, 0x32D2U,
    0x4235U, 0x5214U, 0x6277U, 0x7256U, 0xB5EAU, 0xA5CBU, 0x95A8U, 0x8589U, 0xF56EU, 0xE54FU, 0xD52CU, 0xC50DU, 0x34E2U,
    0x24C3U, 0x14A0U, 0x0481U, 0x7466U, 0x6447U, 0x5424U, 0x4405U, 0xA7DBU, 0xB7FAU, 0x8799U, 0x97B8U, 0xE75FU, 0xF77EU,
    0xC71DU, 0xD73CU, 0x26D3U, 0x36F2U, 0x0691U, 0x16B0U, 0x6657U, 0x7676U, 0x4615U, 0x5634U, 0xD94CU, 0xC96DU, 0xF90EU,
    0xE92FU, 0x99C8U, 0x89E9U, 0xB98AU, 0xA9ABU, 0x5844U, 0x4865U, 0x7806U, 0x6827U, 0x18C0U, 0x08E1U, 0x3882U, 0x28A3U,
    0xCB7DU, 0xDB5CU, 0xEB3FU, 0xFB1EU, 0x8BF9U, 0x9BD8U, 0xABBBU, 0xBB9AU, 0x4A75U, 0x5A54U, 0x6A37U, 0x7A16U, 0x0AF1U,
    0x1AD0U, 0x2AB3U, 0x3A92U, 0xFD2EU, 0xED0FU, 0xDD6CU, 0xCD4DU, 0xBDAAU, 0xAD8BU, 0x9DE8U, 0x8DC9U, 0x7C26U, 0x6C07U,
    0x5C64U, 0x4C45U, 0x3CA2U, 0x2C83U, 0x1CE0U, 0x0CC1U, 0xEF1FU, 0xFF3EU, 0xCF5DU, 0xDF7CU, 0xAF9BU, 0xBFBAU, 0x8FD9U,
    0x9FF8U, 0x6E17U, 0x7E36U, 0x4E55U, 0x5E74U, 0x2E93U, 0x3EB2U, 0x0ED1U, 0x1EF0U,
};

static uint16_t crc16_add(uint16_t crc, const void* data, const size_t size)
{
    const auto* p = static_cast<const uint_least8_t*>(data);
    for (size_t i = 0; i < size; i++) {
        crc = static_cast<uint16_t>((static_cast<unsigned>(crc) << 8U) ^
                                    crc_table[(static_cast<unsigned>(crc) >> 8U) ^ p[i]]);
    }
    return crc;
}

// =====================================================================================================================
//                                         Node-ID Lifecycle Tests
// =====================================================================================================================

// 1. canard_new assigns a random node_id in [1,127]; different seeds yield different IDs.
static void test_canard_new_assigns_random_node_id()
{
    canard_t self1 = {};
    TEST_ASSERT_TRUE(canard_new(&self1, &test_vtable, make_std_memory(), 16U, 12345U, 0U));
    TEST_ASSERT_TRUE(self1.node_id >= 1U);
    TEST_ASSERT_TRUE(self1.node_id <= 127U);
    const uint_least8_t id1 = self1.node_id;
    canard_destroy(&self1);

    canard_t self2 = {};
    TEST_ASSERT_TRUE(canard_new(&self2, &test_vtable, make_std_memory(), 16U, 99999U, 0U));
    TEST_ASSERT_TRUE(self2.node_id >= 1U);
    TEST_ASSERT_TRUE(self2.node_id <= 127U);
    TEST_ASSERT_NOT_EQUAL(id1, self2.node_id);
    canard_destroy(&self2);
}

// 2. canard_new rejects invalid parameters.
static void test_canard_new_invalid_params()
{
    canard_t               self = {};
    const canard_mem_set_t mem  = make_std_memory();

    // NULL self.
    TEST_ASSERT_FALSE(canard_new(nullptr, &test_vtable, mem, 16U, 0U, 0U));
    // NULL vtable.
    TEST_ASSERT_FALSE(canard_new(&self, nullptr, mem, 16U, 0U, 0U));

    // Null vtable->now.
    canard_vtable_t bad_vtable = { .now = nullptr, .tx = mock_tx, .filter = nullptr };
    TEST_ASSERT_FALSE(canard_new(&self, &bad_vtable, mem, 16U, 0U, 0U));

    // Null vtable->tx.
    bad_vtable = { .now = mock_now, .tx = nullptr, .filter = nullptr };
    TEST_ASSERT_FALSE(canard_new(&self, &bad_vtable, mem, 16U, 0U, 0U));
}

// 3. set_node_id boundary values.
static void test_canard_set_node_id_boundary()
{
    canard_t self = {};
    TEST_ASSERT_TRUE(canard_new(&self, &test_vtable, make_std_memory(), 16U, 1234U, 0U));

    // 0 is valid per implementation (returns true).
    TEST_ASSERT_TRUE(canard_set_node_id(&self, 0U));

    // 1 is valid.
    TEST_ASSERT_TRUE(canard_set_node_id(&self, 1U));

    // 127 (max) is valid.
    TEST_ASSERT_TRUE(canard_set_node_id(&self, 127U));

    // 128 is invalid.
    TEST_ASSERT_FALSE(canard_set_node_id(&self, 128U));

    // 255 (CANARD_NODE_ID_ANONYMOUS) is invalid.
    TEST_ASSERT_FALSE(canard_set_node_id(&self, 255U));

    canard_destroy(&self);
}

// 4. set_node_id purges multiframe transfers whose first frame has departed.
static void test_canard_set_node_id_purges_multiframe()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_capture(&self, &cap, 42U, 16U, 0U, &capture_vtable);
    self.tx.fd = false; // Classic CAN: MTU=8, 7 payload bytes/frame -> 8+ byte payload triggers multiframe.

    // 20-byte payload on Classic CAN: multiframe (at least 3 frames).
    uint_least8_t payload_data[20];
    for (size_t i = 0; i < sizeof(payload_data); i++) {
        payload_data[i] = static_cast<uint_least8_t>(i);
    }
    const canard_bytes_chain_t payload = { .bytes = { .size = 20U, .data = payload_data }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_publish(&self, 100000, 1U, canard_prio_nominal, 100U, false, 0U, payload, nullptr));
    TEST_ASSERT_TRUE(self.tx.queue_size > 1U); // Multiframe -> multiple frames enqueued.

    // Poll to eject the first frame (marks first_frame_departed).
    canard_poll(&self, 1U);
    TEST_ASSERT_TRUE(cap.count >= 1U);

    // Set node_id to a different value -> multiframe with departed first frame is purged.
    TEST_ASSERT_TRUE(canard_set_node_id(&self, 99U));
    TEST_ASSERT_EQUAL_size_t(0U, self.tx.queue_size);

    canard_destroy(&self);
}

// 5. set_node_id with the same value is a no-op.
static void test_canard_set_node_id_same_noop()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_capture(&self, &cap, 42U, 16U, 0U, &capture_vtable);

    // Enqueue a transfer.
    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = nullptr }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_publish(&self, 100000, 1U, canard_prio_nominal, 100U, false, 0U, payload, nullptr));
    const size_t qs_before = self.tx.queue_size;

    // Setting the same node_id again should be a no-op.
    const bool dirty_before = self.rx.filters_dirty;
    TEST_ASSERT_TRUE(canard_set_node_id(&self, 42U));
    TEST_ASSERT_EQUAL_size_t(qs_before, self.tx.queue_size);
    TEST_ASSERT_EQUAL(dirty_before, self.rx.filters_dirty);

    canard_destroy(&self);
}

// =====================================================================================================================
//                                       Collision Detection Tests
// =====================================================================================================================

// 6. Ingesting a START frame from our own node_id triggers collision.
static void test_collision_on_ingest()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_capture(&self, &cap, 42U, 16U, 0U, &capture_vtable);

    // Must have a subscription so that the frame can be matched and processed.
    rx_capture_t          rx_cap = {};
    canard_subscription_t sub    = {};
    TEST_ASSERT_TRUE(canard_subscribe(&self, &sub, 1000U, false, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &rx_cap;

    // Ingest a single-frame (SOT=1, EOT=1) from source_node_id=42 (our own).
    const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 1000U, 42U);
    const uint_least8_t  frame[]  = { 0xAAU, make_v1_single_tail(0U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };

    cap.now = 100;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id, can_data));

    // Collision detected.
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.collision);
    // Node-ID changed to something different.
    TEST_ASSERT_NOT_EQUAL(42U, self.node_id);
    TEST_ASSERT_TRUE(self.node_id >= 1U);
    TEST_ASSERT_TRUE(self.node_id <= 127U);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// 7. Collision sets filters_dirty; next poll invokes the filter callback.
static void test_collision_filters_dirty()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_capture(&self, &cap, 42U, 16U, 4U, &capture_filter_vtable);

    rx_capture_t          rx_cap = {};
    canard_subscription_t sub    = {};
    TEST_ASSERT_TRUE(canard_subscribe(&self, &sub, 2000U, false, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &rx_cap;

    // Clear the dirty flag by polling once (subscription triggers dirty).
    canard_poll(&self, 0U);
    const size_t filter_calls_before = cap.filter_rec.invocation_count;

    // Ingest a frame from our own node_id -> collision -> filters_dirty set.
    const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 2000U, 42U);
    const uint_least8_t  frame[]  = { 0xBBU, make_v1_single_tail(0U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };
    cap.now                       = 200;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 200, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.collision);

    // Poll should invoke the filter callback because filters_dirty was set.
    canard_poll(&self, 0U);
    TEST_ASSERT_TRUE(cap.filter_rec.invocation_count > filter_calls_before);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// =====================================================================================================================
//                                              Poll Tests
// =====================================================================================================================

// 8. Poll triggers filter reconfiguration when filters are dirty.
static void test_poll_filter_reconfiguration()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_capture(&self, &cap, 42U, 16U, 4U, &capture_filter_vtable);

    // Subscribe sets filters_dirty.
    rx_capture_t          rx_cap = {};
    canard_subscription_t sub    = {};
    TEST_ASSERT_TRUE(canard_subscribe(&self, &sub, 3000U, false, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &rx_cap;

    // Poll invokes filter callback.
    canard_poll(&self, 0U);
    TEST_ASSERT_TRUE(cap.filter_rec.invocation_count >= 1U);
    TEST_ASSERT_TRUE(cap.filter_rec.last_filter_count >= 1U);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// 9. Unsubscribing sets filters_dirty; poll reconfigures filters again.
static void test_poll_filter_after_unsubscribe()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_capture(&self, &cap, 42U, 16U, 4U, &capture_filter_vtable);

    rx_capture_t          rx_cap = {};
    canard_subscription_t sub    = {};
    TEST_ASSERT_TRUE(canard_subscribe(&self, &sub, 4000U, false, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &rx_cap;

    // First poll configures filters.
    canard_poll(&self, 0U);
    const size_t calls_after_subscribe = cap.filter_rec.invocation_count;
    TEST_ASSERT_TRUE(calls_after_subscribe >= 1U);

    // Unsubscribe and poll again. Dirty should be set, filter called again.
    canard_unsubscribe(&self, &sub);
    canard_poll(&self, 0U);
    // cppcheck-suppress knownConditionTrueFalse  ; canard_poll invokes the callback, mutating invocation_count
    TEST_ASSERT_TRUE(cap.filter_rec.invocation_count > calls_after_subscribe);

    canard_destroy(&self);
}

// 10. Poll cleans up stale sessions; a repeat TID is accepted after session expiry.
static void test_poll_session_cleanup()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_capture(&self, &cap, 42U, 16U, 0U, &capture_vtable);

    const canard_us_t tid_timeout = 2000000; // 2 seconds

    rx_capture_t          rx_cap = {};
    canard_subscription_t sub    = {};
    TEST_ASSERT_TRUE(canard_subscribe(&self, &sub, 5000U, false, 256U, tid_timeout, &capture_sub_vtable));
    sub.user_context = &rx_cap;

    // Ingest a single-frame transfer from node 10, TID=5.
    const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 5000U, 10U);
    const uint_least8_t  frame[]  = { 0xCCU, make_v1_single_tail(5U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };
    cap.now                       = 1000;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 1000, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);

    // Advance time past max(30s, tid_timeout) = 30s from last activity.
    // last_admission_ts = 1000, so we need now > 1000 + 30000000.
    cap.now = 31000002;
    canard_poll(&self, 0U);

    // Ingest the same TID again. Should be accepted because the session was cleaned up.
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 31000002, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(2U, rx_cap.count);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// 11. Poll expires old transfers before ejecting new ones.
static void test_poll_deadline_then_tx()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_capture(&self, &cap, 42U, 16U, 0U, &capture_vtable);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = nullptr }, .next = nullptr };

    // Transfer A: deadline=100 (will expire).
    TEST_ASSERT_TRUE(canard_publish(&self, 100, 1U, canard_prio_nominal, 200U, false, 0U, payload, nullptr));
    // Transfer B: deadline=10000 (will not expire).
    TEST_ASSERT_TRUE(canard_publish(&self, 10000, 1U, canard_prio_nominal, 201U, false, 1U, payload, nullptr));

    // Advance time past deadline of transfer A.
    cap.now = 200;
    canard_poll(&self, CANARD_IFACE_BITMAP_ALL);

    // Transfer A expired; transfer B ejected.
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.tx_expiration);
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_UINT64(10000U, static_cast<uint64_t>(cap.records[0].deadline));

    canard_destroy(&self);
}

// =====================================================================================================================
//                                          Error Counter Tests
// =====================================================================================================================

// 12. OOM on publish: instrumented allocator with tx_frame limited to 0.
static void test_err_oom_on_publish()
{
    instrumented_allocator_t alloc_transfer = {};
    instrumented_allocator_t alloc_frame    = {};
    instrumented_allocator_new(&alloc_transfer);
    instrumented_allocator_new(&alloc_frame);
    alloc_frame.limit_fragments = 0U; // No frame allocations allowed.

    canard_mem_set_t mem = make_std_memory();
    mem.tx_transfer      = instrumented_allocator_make_resource(&alloc_transfer);
    mem.tx_frame         = instrumented_allocator_make_resource(&alloc_frame);

    canard_t self = {};
    TEST_ASSERT_TRUE(canard_new(&self, &test_vtable, mem, 16U, 1234U, 0U));
    TEST_ASSERT_TRUE(canard_set_node_id(&self, 42U));

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = nullptr }, .next = nullptr };
    // Publish should fail due to OOM when allocating frames (or transfer, depends on order).
    TEST_ASSERT_FALSE(canard_publish(&self, 1000, 1U, canard_prio_nominal, 100U, false, 0U, payload, nullptr));
    TEST_ASSERT_TRUE(self.err.oom > 0U);

    // No memory leaked.
    TEST_ASSERT_EQUAL_size_t(0U, alloc_transfer.allocated_fragments);
    TEST_ASSERT_EQUAL_size_t(0U, alloc_frame.allocated_fragments);

    canard_destroy(&self);
}

// 13. tx_capacity error: queue too small for the multiframe transfer.
static void test_err_tx_capacity()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_capture(&self, &cap, 42U, 1U, 0U, &capture_vtable); // queue_capacity=1
    self.tx.fd = false;                                      // Classic CAN

    // 20-byte payload on Classic CAN needs at least 4 frames. Queue capacity is 1 -> tx_capacity error.
    uint_least8_t payload_data[20];
    std::memset(payload_data, 0xAA, sizeof(payload_data));
    const canard_bytes_chain_t payload = { .bytes = { .size = 20U, .data = payload_data }, .next = nullptr };
    TEST_ASSERT_FALSE(canard_publish(&self, 10000, 1U, canard_prio_nominal, 300U, false, 0U, payload, nullptr));
    TEST_ASSERT_TRUE(self.err.tx_capacity > 0U);

    canard_destroy(&self);
}

// 14. tx_sacrifice: oldest transfer sacrificed to make room.
static void test_err_tx_sacrifice()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_capture(&self, &cap, 42U, 2U, 0U, &capture_vtable); // queue_capacity=2

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = nullptr }, .next = nullptr };

    // Enqueue 2 single-frame transfers (fills the queue).
    TEST_ASSERT_TRUE(canard_publish(&self, 10000, 1U, canard_prio_nominal, 400U, false, 0U, payload, nullptr));
    TEST_ASSERT_TRUE(canard_publish(&self, 10000, 1U, canard_prio_nominal, 401U, false, 1U, payload, nullptr));
    TEST_ASSERT_EQUAL_size_t(2U, self.tx.queue_size);

    // Enqueue a third transfer -> oldest must be sacrificed.
    TEST_ASSERT_TRUE(canard_publish(&self, 10000, 1U, canard_prio_nominal, 402U, false, 2U, payload, nullptr));
    TEST_ASSERT_TRUE(self.err.tx_sacrifice > 0U);

    canard_destroy(&self);
}

// 15. tx_expiration: transfer expired before ejection.
static void test_err_tx_expiration()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_with_capture(&self, &cap);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = nullptr }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_publish(&self, 50, 1U, canard_prio_nominal, 500U, false, 0U, payload, nullptr));

    cap.now = 100;
    canard_poll(&self, 1U);

    TEST_ASSERT_EQUAL_UINT64(1U, self.err.tx_expiration);
    TEST_ASSERT_EQUAL_size_t(0U, cap.count); // Nothing ejected, it was expired.

    canard_destroy(&self);
}

// 16. rx_frame: malformed frame (0 bytes, no tail byte).
static void test_err_rx_frame_malformed()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_with_capture(&self, &cap);

    const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 600U, 10U);
    const canard_bytes_t can_data = { .size = 0, .data = nullptr };

    cap.now = 100;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.rx_frame);

    canard_destroy(&self);
}

// 17. rx_transfer: multiframe with wrong CRC.
static void test_err_rx_transfer_bad_crc()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_capture(&self, &cap, 42U, 16U, 0U, &capture_vtable);

    rx_capture_t          rx_cap = {};
    canard_subscription_t sub    = {};
    TEST_ASSERT_TRUE(canard_subscribe(&self, &sub, 7000U, false, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &rx_cap;

    const uint32_t can_id = make_v1v1_msg_can_id(canard_prio_nominal, 7000U, 10U);

    // Construct a 2-frame Classic CAN multiframe with 8 bytes payload.
    // Frame 1 (SOT): 7 payload bytes + tail. Toggle starts at 1 for v1.
    // Frame 2 (EOT): 1 payload byte + CRC(2) + padding + tail.
    // We intentionally use a wrong CRC.
    const uint_least8_t payload[8] = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17 };

    // Frame 1: SOT=1, EOT=0, toggle=1, TID=3.
    uint_least8_t frame1[8];
    std::memcpy(frame1, payload, 7U);
    frame1[7] = make_v1_start_tail(true, 3U); // 0x80 | 0x20 | 0x03 = 0xA3

    // Frame 2: SOT=0, EOT=1, toggle=0, TID=3 -- with intentionally wrong CRC.
    uint_least8_t frame2[8];
    frame2[0] = payload[7]; // Last payload byte.
    // Wrong CRC bytes (just zeros).
    frame2[1] = 0x00U;
    frame2[2] = 0x00U;
    // Padding to fill to 8 bytes (data[3..6] are padding).
    frame2[3] = 0x00U;
    frame2[4] = 0x00U;
    frame2[5] = 0x00U;
    frame2[6] = 0x00U;
    frame2[7] = make_v1_end_tail(false, 3U); // 0x40 | 0x00 | 0x03 = 0x43

    const canard_bytes_t cd1 = { .size = sizeof(frame1), .data = frame1 };
    const canard_bytes_t cd2 = { .size = sizeof(frame2), .data = frame2 };

    cap.now = 100;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id, cd1));
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id, cd2));

    // Callback should NOT have fired (bad CRC).
    TEST_ASSERT_EQUAL_size_t(0U, rx_cap.count);
    TEST_ASSERT_TRUE(self.err.rx_transfer > 0U);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// 18. Collision counter (same scenario as test 6).
static void test_err_collision_counter()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_capture(&self, &cap, 42U, 16U, 0U, &capture_vtable);

    rx_capture_t          rx_cap = {};
    canard_subscription_t sub    = {};
    TEST_ASSERT_TRUE(canard_subscribe(&self, &sub, 8000U, false, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &rx_cap;

    TEST_ASSERT_EQUAL_UINT64(0U, self.err.collision);

    const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 8000U, 42U);
    const uint_least8_t  frame[]  = { 0xFFU, make_v1_single_tail(0U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };

    cap.now = 100;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.collision);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// 19. Error counters accumulate (do not reset).
static void test_err_counters_accumulate()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_with_capture(&self, &cap);

    // Trigger rx_frame error twice with empty frames.
    const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 9000U, 10U);
    const canard_bytes_t can_data = { .size = 0, .data = nullptr };

    cap.now = 100;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_UINT64(1U, self.err.rx_frame);

    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 101, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_UINT64(2U, self.err.rx_frame);

    canard_destroy(&self);
}

// =====================================================================================================================
//                                          Redundancy Tests
// =====================================================================================================================

// 20. Publish with iface_bitmap=3 ejects frames on both interfaces.
static void test_redundant_tx_both_interfaces()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_with_capture(&self, &cap);

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = nullptr }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_publish(&self, 10000, 3U, canard_prio_nominal, 10000U, false, 0U, payload, nullptr));

    // Poll iface 0.
    canard_poll(&self, 1U);
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(0U, cap.records[0].iface_index);
    const uint32_t expected_can_id = cap.records[0].can_id;

    // Poll iface 1.
    canard_poll(&self, 2U);
    TEST_ASSERT_EQUAL_size_t(2U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(1U, cap.records[1].iface_index);
    TEST_ASSERT_EQUAL_UINT32(expected_can_id, cap.records[1].can_id);

    canard_destroy(&self);
}

// 21. Redundant RX: same single-frame transfer on both ifaces is deduplicated.
static void test_redundant_rx_dedup()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_capture(&self, &cap, 42U, 16U, 0U, &capture_vtable);

    rx_capture_t          rx_cap = {};
    canard_subscription_t sub    = {};
    TEST_ASSERT_TRUE(canard_subscribe(&self, &sub, 11000U, false, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &rx_cap;

    const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 11000U, 10U);
    const uint_least8_t  frame[]  = { 0xDDU, make_v1_single_tail(7U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };

    cap.now = 500;
    // Ingest on iface 0.
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 500, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);

    // Ingest same frame on iface 1 -> deduplicated.
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 500, 1U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count); // Still 1, not 2.

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// 22. Redundant RX dedup for multiframe transfers.
static void test_redundant_rx_dedup_multiframe()
{
    canard_t     self = {};
    tx_capture_t cap  = {};
    init_capture(&self, &cap, 42U, 16U, 0U, &capture_vtable);

    rx_capture_t          rx_cap = {};
    canard_subscription_t sub    = {};
    TEST_ASSERT_TRUE(canard_subscribe(&self, &sub, 12000U, false, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = &rx_cap;

    const uint32_t can_id = make_v1v1_msg_can_id(canard_prio_nominal, 12000U, 10U);

    // Construct a valid 2-frame Classic CAN multiframe with 8-byte payload and correct CRC.
    const uint_least8_t payload[8] = { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7 };

    // Compute CRC over payload.
    const uint16_t crc = crc16_add(0xFFFFU, payload, sizeof(payload));

    // Frame 1: 7 payload bytes + tail (SOT=1, EOT=0, toggle=1, TID=2).
    uint_least8_t frame1[8];
    std::memcpy(frame1, payload, 7U);
    frame1[7] = make_v1_start_tail(true, 2U);

    // Frame 2: 1 payload byte + CRC hi + CRC lo + padding(4) + tail (SOT=0, EOT=1, toggle=0, TID=2).
    uint_least8_t frame2[8];
    frame2[0] = payload[7];
    frame2[1] = static_cast<uint_least8_t>((static_cast<unsigned>(crc) >> 8U) & 0xFFU);
    frame2[2] = static_cast<uint_least8_t>(crc & 0xFFU);
    frame2[3] = 0x00U; // padding
    frame2[4] = 0x00U;
    frame2[5] = 0x00U;
    frame2[6] = 0x00U;
    frame2[7] = make_v1_end_tail(false, 2U);

    const canard_bytes_t cd1 = { .size = sizeof(frame1), .data = frame1 };
    const canard_bytes_t cd2 = { .size = sizeof(frame2), .data = frame2 };

    cap.now = 600;

    // Ingest both frames on iface 0 -> transfer delivered.
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 600, 0U, can_id, cd1));
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 600, 0U, can_id, cd2));
    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count);

    // Ingest same two frames on iface 1 -> should be deduplicated.
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 600, 1U, can_id, cd1));
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 600, 1U, can_id, cd2));
    TEST_ASSERT_EQUAL_size_t(1U, rx_cap.count); // Still 1, not 2.

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// =====================================================================================================================
//                                              Test Runner
// =====================================================================================================================

extern "C" void setUp() {}
extern "C" void tearDown() {}

int main()
{
    UNITY_BEGIN();

    // Node-ID lifecycle.
    RUN_TEST(test_canard_new_assigns_random_node_id);
    RUN_TEST(test_canard_new_invalid_params);
    RUN_TEST(test_canard_set_node_id_boundary);
    RUN_TEST(test_canard_set_node_id_purges_multiframe);
    RUN_TEST(test_canard_set_node_id_same_noop);

    // Collision detection.
    RUN_TEST(test_collision_on_ingest);
    RUN_TEST(test_collision_filters_dirty);

    // Poll behavior.
    RUN_TEST(test_poll_filter_reconfiguration);
    RUN_TEST(test_poll_filter_after_unsubscribe);
    RUN_TEST(test_poll_session_cleanup);
    RUN_TEST(test_poll_deadline_then_tx);

    // Error counters.
    RUN_TEST(test_err_oom_on_publish);
    RUN_TEST(test_err_tx_capacity);
    RUN_TEST(test_err_tx_sacrifice);
    RUN_TEST(test_err_tx_expiration);
    RUN_TEST(test_err_rx_frame_malformed);
    RUN_TEST(test_err_rx_transfer_bad_crc);
    RUN_TEST(test_err_collision_counter);
    RUN_TEST(test_err_counters_accumulate);

    // Redundant interface semantics.
    RUN_TEST(test_redundant_tx_both_interfaces);
    RUN_TEST(test_redundant_rx_dedup);
    RUN_TEST(test_redundant_rx_dedup_multiframe);

    return UNITY_END();
}
