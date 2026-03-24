// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal Development Team.

#include "helpers.h"
#include <unity.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>

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
    uint_least8_t payload_buf[256];
};

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
}

static const canard_subscription_vtable_t capture_sub_vtable = { .on_message = capture_on_message };

// -------------------------------------------  CAN Frame Construction Helpers  ----------------------------------------

// v1.1 message: priority[28:26] | subject_id[25:8] | bit7=1(v1.1) | src[6:0]
static uint32_t make_v1v1_msg_can_id(const canard_prio_t prio, const uint16_t subject_id, const uint_least8_t src)
{
    return (static_cast<uint32_t>(prio) << 26U) | (static_cast<uint32_t>(subject_id) << 8U) | (UINT32_C(1) << 7U) |
           (static_cast<uint32_t>(src) & 0x7FU);
}

// v1.0 service: priority[28:26] | bit25=1(svc) | rnr[24] | service_id[23:14] | dst[13:7] | src[6:0]
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

// Single-frame tail byte for v1: start=1, end=1, toggle=1 (v1 starts toggle=1).
static uint_least8_t make_v1_single_tail(const uint_least8_t tid) { return 0xE0U | (tid & 0x1FU); }

// v0 message: priority[28:26] | data_type_id[23:8] | bit7=0 | src[6:0]
static uint32_t make_v0_msg_can_id(const canard_prio_t prio, const uint16_t data_type_id, const uint_least8_t src)
{
    return (static_cast<uint32_t>(prio) << 26U) | (static_cast<uint32_t>(data_type_id) << 8U) |
           (static_cast<uint32_t>(src) & 0x7FU);
}

// v0 service: priority[28:26] | data_type_id[23:16] | rnr[15] | dst[14:8] | bit7=1(svc) | src[6:0]
static uint32_t make_v0_svc_can_id(const canard_prio_t prio,
                                   const uint_least8_t data_type_id,
                                   const bool          request_not_response,
                                   const uint_least8_t dst,
                                   const uint_least8_t src)
{
    return (static_cast<uint32_t>(prio) << 26U) | (static_cast<uint32_t>(data_type_id) << 16U) |
           (request_not_response ? (UINT32_C(1) << 15U) : 0U) | (static_cast<uint32_t>(dst) << 8U) |
           (UINT32_C(1) << 7U) | (static_cast<uint32_t>(src) & 0x7FU);
}

// Single-frame tail byte for v0: start=1, end=1, toggle=0 (v0 starts toggle=0).
static uint_least8_t make_v0_single_tail(const uint_least8_t tid) { return 0xC0U | (tid & 0x1FU); }

// -------------------------------------------  Argument Validation  ---------------------------------------------------

static void test_subscribe_null_args()
{
    canard_t              self = {};
    canard_subscription_t sub  = {};
    TEST_ASSERT_FALSE(canard_subscribe_16b(nullptr, &sub, 100U, 64U, 2000000, &capture_sub_vtable));
    TEST_ASSERT_FALSE(canard_subscribe_16b(&self, nullptr, 100U, 64U, 2000000, &capture_sub_vtable));
    TEST_ASSERT_FALSE(canard_subscribe_16b(&self, &sub, 100U, 64U, 2000000, nullptr));

    const canard_subscription_vtable_t bad_vtable = { .on_message = nullptr };
    TEST_ASSERT_FALSE(canard_subscribe_16b(&self, &sub, 100U, 64U, 2000000, &bad_vtable));
}

static void test_subscribe_13b_null_args()
{
    canard_t              self = {};
    canard_subscription_t sub  = {};
    TEST_ASSERT_FALSE(canard_subscribe_13b(nullptr, &sub, 100U, 64U, 2000000, &capture_sub_vtable));
    TEST_ASSERT_FALSE(canard_subscribe_13b(&self, nullptr, 100U, 64U, 2000000, &capture_sub_vtable));
    TEST_ASSERT_FALSE(canard_subscribe_13b(&self, &sub, 100U, 64U, 2000000, nullptr));

    const canard_subscription_vtable_t bad_vtable = { .on_message = nullptr };
    TEST_ASSERT_FALSE(canard_subscribe_13b(&self, &sub, 100U, 64U, 2000000, &bad_vtable));
}

static void test_subscribe_request_null_args()
{
    canard_t              self = {};
    canard_subscription_t sub  = {};
    TEST_ASSERT_FALSE(canard_subscribe_request(nullptr, &sub, 10U, 64U, 2000000, &capture_sub_vtable));
    TEST_ASSERT_FALSE(canard_subscribe_request(&self, nullptr, 10U, 64U, 2000000, &capture_sub_vtable));
    TEST_ASSERT_FALSE(canard_subscribe_request(&self, &sub, 10U, 64U, 2000000, nullptr));
}

static void test_subscribe_response_null_args()
{
    canard_t              self = {};
    canard_subscription_t sub  = {};
    TEST_ASSERT_FALSE(canard_subscribe_response(nullptr, &sub, 10U, 64U, &capture_sub_vtable));
    TEST_ASSERT_FALSE(canard_subscribe_response(&self, nullptr, 10U, 64U, &capture_sub_vtable));
    TEST_ASSERT_FALSE(canard_subscribe_response(&self, &sub, 10U, 64U, nullptr));
}

static void test_subscribe_port_id_range()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);
    canard_subscription_t sub = {};

    // v1.0 subject must be <= 8191.
    TEST_ASSERT_FALSE(canard_subscribe_13b(&self, &sub, 8192U, 64U, 2000000, &capture_sub_vtable));
    TEST_ASSERT_TRUE(canard_subscribe_13b(&self, &sub, 8191U, 64U, 2000000, &capture_sub_vtable));
    canard_unsubscribe(&self, &sub);

    // v1.1 subject: full 16-bit range is valid.
    canard_subscription_t sub2 = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub2, 0xFFFFU, 64U, 2000000, &capture_sub_vtable));
    canard_unsubscribe(&self, &sub2);

    // Service ID must be <= 511.
    canard_subscription_t sub3 = {};
    TEST_ASSERT_FALSE(canard_subscribe_request(&self, &sub3, 512U, 64U, 2000000, &capture_sub_vtable));
    TEST_ASSERT_TRUE(canard_subscribe_request(&self, &sub3, 511U, 64U, 2000000, &capture_sub_vtable));
    canard_unsubscribe(&self, &sub3);

    canard_subscription_t sub4 = {};
    TEST_ASSERT_FALSE(canard_subscribe_response(&self, &sub4, 512U, 64U, &capture_sub_vtable));
    TEST_ASSERT_TRUE(canard_subscribe_response(&self, &sub4, 511U, 64U, &capture_sub_vtable));
    canard_unsubscribe(&self, &sub4);

    canard_destroy(&self);
}

// -------------------------------------------  Duplicate Rejection  ---------------------------------------------------

static void test_subscribe_duplicate_rejection()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    canard_subscription_t sub1 = {};
    canard_subscription_t sub2 = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub1, 100U, 64U, 2000000, &capture_sub_vtable));
    // Same subject-ID, same kind: must fail.
    TEST_ASSERT_FALSE(canard_subscribe_16b(&self, &sub2, 100U, 64U, 2000000, &capture_sub_vtable));
    canard_unsubscribe(&self, &sub1);

    // Same port-ID but different kind: must succeed (message vs request use separate trees).
    canard_subscription_t sub_msg = {};
    canard_subscription_t sub_req = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub_msg, 100U, 64U, 2000000, &capture_sub_vtable));
    TEST_ASSERT_TRUE(canard_subscribe_request(&self, &sub_req, 100U, 64U, 2000000, &capture_sub_vtable));
    canard_unsubscribe(&self, &sub_req);
    canard_unsubscribe(&self, &sub_msg);

    canard_destroy(&self);
}

// -------------------------------------------  Subscribe-Unsubscribe Lifecycle  ---------------------------------------

static void test_subscribe_unsubscribe_resubscribe()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 200U, 64U, 2000000, &capture_sub_vtable));
    canard_unsubscribe(&self, &sub);
    // Re-subscribe to the same subject must succeed.
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 200U, 64U, 2000000, &capture_sub_vtable));
    canard_unsubscribe(&self, &sub);

    canard_destroy(&self);
}

// -------------------------------------------  Single-Frame Message Reception  ----------------------------------------

static void test_v1v1_message_reception()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 1234U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = (&cap);

    // Construct a single-frame v1.1 message from node 10, priority nominal, transfer-ID 7.
    const uint32_t can_id = make_v1v1_msg_can_id(canard_prio_nominal, 1234U, 10U);
    // Payload: {0xDE, 0xAD} + tail byte.
    const uint_least8_t  frame[]  = { 0xDEU, 0xADU, make_v1_single_tail(7U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };

    now_val = 1000;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 1000, 0U, can_id, can_data));

    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_INT64(1000, cap.timestamp);
    TEST_ASSERT_EQUAL_UINT8(canard_prio_nominal, cap.priority);
    TEST_ASSERT_EQUAL_UINT8(10U, cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(7U, cap.transfer_id);
    TEST_ASSERT_EQUAL_size_t(2U, cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8(0xDEU, cap.payload_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xADU, cap.payload_buf[1]);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Service Request Reception  ---------------------------------------------

static void test_service_request_reception()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_request(&self, &sub, 100U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = (&cap);

    // Service request from node 10 to node 42 (us), service-ID 100, transfer-ID 3.
    const uint32_t       can_id   = make_v1_svc_can_id(canard_prio_nominal, 100U, true, 42U, 10U);
    const uint_least8_t  frame[]  = { 0xCAU, 0xFEU, make_v1_single_tail(3U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };

    now_val = 2000;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 2000, 0U, can_id, can_data));

    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(10U, cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(3U, cap.transfer_id);
    TEST_ASSERT_EQUAL_size_t(2U, cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8(0xCAU, cap.payload_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFEU, cap.payload_buf[1]);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Service Response Reception  --------------------------------------------

static void test_service_response_reception()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_response(&self, &sub, 200U, 256U, &capture_sub_vtable));
    sub.user_context = (&cap);

    // Service response from node 99 to node 42 (us), service-ID 200, transfer-ID 5.
    const uint32_t       can_id   = make_v1_svc_can_id(canard_prio_nominal, 200U, false, 42U, 99U);
    const uint_least8_t  frame[]  = { 0xBEU, 0xEFU, make_v1_single_tail(5U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };

    now_val = 3000;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 3000, 0U, can_id, can_data));

    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(99U, cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(5U, cap.transfer_id);
    TEST_ASSERT_EQUAL_size_t(2U, cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8(0xBEU, cap.payload_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xEFU, cap.payload_buf[1]);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  Unsubscribe Stops Delivery  --------------------------------------------

static void test_unsubscribe_stops_delivery()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 500U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = (&cap);

    // First frame: should be delivered.
    const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 500U, 10U);
    const uint_least8_t  frame[]  = { 0xAAU, make_v1_single_tail(0U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };
    now_val                       = 100;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);

    // Unsubscribe, then ingest again: callback must NOT fire.
    canard_unsubscribe(&self, &sub);
    cap = {};
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 200, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count);

    canard_destroy(&self);
}

// -------------------------------------------  Multiple Subscriptions Routing  ----------------------------------------

static void capture_on_message_simple(canard_subscription_t* const self,
                                      const canard_us_t,
                                      const canard_prio_t,
                                      const uint_least8_t source_node_id,
                                      const uint_least8_t transfer_id,
                                      // cppcheck-suppress passedByValueCallback
                                      const canard_payload_t payload)
{
    auto* const cap = static_cast<rx_capture_t*>(self->user_context);
    cap->count++;
    cap->source_node_id = source_node_id;
    cap->transfer_id    = transfer_id;
    cap->payload_size   = payload.view.size;
}

static const canard_subscription_vtable_t simple_sub_vtable = { .on_message = capture_on_message_simple };

static void test_multiple_subscriptions_routing()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t cap_a = {};
    rx_capture_t cap_b = {};

    canard_subscription_t sub_a = {};
    canard_subscription_t sub_b = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub_a, 300U, 256U, 2000000, &simple_sub_vtable));
    sub_a.user_context = (&cap_a);
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub_b, 400U, 256U, 2000000, &simple_sub_vtable));
    sub_b.user_context = (&cap_b);

    // Message for subject 300.
    {
        const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 300U, 10U);
        const uint_least8_t  frame[]  = { 0x11U, make_v1_single_tail(1U) };
        const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };
        now_val                       = 100;
        TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id, can_data));
    }
    // Message for subject 400.
    {
        const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 400U, 20U);
        const uint_least8_t  frame[]  = { 0x22U, make_v1_single_tail(2U) };
        const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };
        now_val                       = 200;
        TEST_ASSERT_TRUE(canard_ingest_frame(&self, 200, 0U, can_id, can_data));
    }

    TEST_ASSERT_EQUAL_size_t(1U, cap_a.count);
    TEST_ASSERT_EQUAL_UINT8(10U, cap_a.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(1U, cap_a.transfer_id);

    TEST_ASSERT_EQUAL_size_t(1U, cap_b.count);
    TEST_ASSERT_EQUAL_UINT8(20U, cap_b.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(2U, cap_b.transfer_id);

    canard_unsubscribe(&self, &sub_b);
    canard_unsubscribe(&self, &sub_a);
    canard_destroy(&self);
}

// -------------------------------------------  Unsubscribe Cleans Up Sessions  ----------------------------------------

static void test_unsubscribe_cleans_up_sessions()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub, 600U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = (&cap);

    // Receive messages from two different remote nodes to create two sessions.
    for (uint_least8_t src = 10U; src <= 11U; src++) {
        const uint32_t       can_id   = make_v1v1_msg_can_id(canard_prio_nominal, 600U, src);
        const uint_least8_t  frame[]  = { 0xFFU, make_v1_single_tail(0U) };
        const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };
        now_val                       = 100;
        TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id, can_data));
    }
    TEST_ASSERT_EQUAL_size_t(2U, cap.count);
    TEST_ASSERT_NOT_NULL(sub.sessions); // Sessions exist.

    // Unsubscribe must destroy all sessions without leaking.
    canard_unsubscribe(&self, &sub);

    canard_destroy(&self);
}

// -------------------------------------------  v0 Argument Validation  ------------------------------------------------

static void test_v0_subscribe_null_args()
{
    canard_t              self = {};
    canard_subscription_t sub  = {};
    TEST_ASSERT_FALSE(canard_v0_subscribe(nullptr, &sub, 100U, 0xBEEFU, 64U, 2000000, &capture_sub_vtable));
    TEST_ASSERT_FALSE(canard_v0_subscribe(&self, nullptr, 100U, 0xBEEFU, 64U, 2000000, &capture_sub_vtable));
    TEST_ASSERT_FALSE(canard_v0_subscribe(&self, &sub, 100U, 0xBEEFU, 64U, 2000000, nullptr));

    const canard_subscription_vtable_t bad_vtable = { .on_message = nullptr };
    TEST_ASSERT_FALSE(canard_v0_subscribe(&self, &sub, 100U, 0xBEEFU, 64U, 2000000, &bad_vtable));
}

static void test_v0_subscribe_request_null_args()
{
    canard_t              self = {};
    canard_subscription_t sub  = {};
    TEST_ASSERT_FALSE(canard_v0_subscribe_request(nullptr, &sub, 10U, 0xBEEFU, 64U, 2000000, &capture_sub_vtable));
    TEST_ASSERT_FALSE(canard_v0_subscribe_request(&self, nullptr, 10U, 0xBEEFU, 64U, 2000000, &capture_sub_vtable));
    TEST_ASSERT_FALSE(canard_v0_subscribe_request(&self, &sub, 10U, 0xBEEFU, 64U, 2000000, nullptr));
}

static void test_v0_subscribe_response_null_args()
{
    canard_t              self = {};
    canard_subscription_t sub  = {};
    TEST_ASSERT_FALSE(canard_v0_subscribe_response(nullptr, &sub, 10U, 0xBEEFU, 64U, &capture_sub_vtable));
    TEST_ASSERT_FALSE(canard_v0_subscribe_response(&self, nullptr, 10U, 0xBEEFU, 64U, &capture_sub_vtable));
    TEST_ASSERT_FALSE(canard_v0_subscribe_response(&self, &sub, 10U, 0xBEEFU, 64U, nullptr));
}

// -------------------------------------------  v0 Port-ID Range  ------------------------------------------------------

static void test_v0_subscribe_port_id_range()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    // v0 message data_type_id: full uint16 range is valid.
    canard_subscription_t sub1 = {};
    TEST_ASSERT_TRUE(canard_v0_subscribe(&self, &sub1, 0xFFFFU, 0x1234U, 64U, 2000000, &capture_sub_vtable));
    canard_unsubscribe(&self, &sub1);

    // v0 service data_type_id: 0xFF is valid.
    canard_subscription_t sub2 = {};
    TEST_ASSERT_TRUE(canard_v0_subscribe_request(&self, &sub2, 0xFFU, 0x1234U, 64U, 2000000, &capture_sub_vtable));
    canard_unsubscribe(&self, &sub2);

    canard_subscription_t sub3 = {};
    TEST_ASSERT_TRUE(canard_v0_subscribe_response(&self, &sub3, 0xFFU, 0x1234U, 64U, &capture_sub_vtable));
    canard_unsubscribe(&self, &sub3);

    canard_destroy(&self);
}

// -------------------------------------------  v0 Duplicate Rejection  ------------------------------------------------

static void test_v0_subscribe_duplicate_rejection()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    canard_subscription_t sub1 = {};
    canard_subscription_t sub2 = {};
    TEST_ASSERT_TRUE(canard_v0_subscribe(&self, &sub1, 100U, 0x1234U, 64U, 2000000, &capture_sub_vtable));
    // Same data_type_id, same kind: must fail.
    TEST_ASSERT_FALSE(canard_v0_subscribe(&self, &sub2, 100U, 0x5678U, 64U, 2000000, &capture_sub_vtable));
    canard_unsubscribe(&self, &sub1);

    // Different kinds (v0 message vs v0 request) with same numeric ID: must succeed.
    canard_subscription_t sub_msg = {};
    canard_subscription_t sub_req = {};
    TEST_ASSERT_TRUE(canard_v0_subscribe(&self, &sub_msg, 100U, 0x1234U, 64U, 2000000, &capture_sub_vtable));
    TEST_ASSERT_TRUE(canard_v0_subscribe_request(&self, &sub_req, 100U, 0x1234U, 64U, 2000000, &capture_sub_vtable));
    canard_unsubscribe(&self, &sub_req);
    canard_unsubscribe(&self, &sub_msg);

    // v0 and v1 subscriptions with the same numeric port_id coexist (separate kind trees).
    canard_subscription_t sub_v0 = {};
    canard_subscription_t sub_v1 = {};
    TEST_ASSERT_TRUE(canard_v0_subscribe(&self, &sub_v0, 200U, 0x1234U, 64U, 2000000, &capture_sub_vtable));
    TEST_ASSERT_TRUE(canard_subscribe_16b(&self, &sub_v1, 200U, 64U, 2000000, &capture_sub_vtable));
    canard_unsubscribe(&self, &sub_v1);
    canard_unsubscribe(&self, &sub_v0);

    canard_destroy(&self);
}

// -------------------------------------------  v0 Subscribe/Unsubscribe Lifecycle  ------------------------------------

static void test_v0_subscribe_unsubscribe_resubscribe()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_v0_subscribe(&self, &sub, 300U, 0xAAAAU, 64U, 2000000, &capture_sub_vtable));
    canard_unsubscribe(&self, &sub);
    // Re-subscribe to the same data_type_id must succeed.
    TEST_ASSERT_TRUE(canard_v0_subscribe(&self, &sub, 300U, 0xAAAAU, 64U, 2000000, &capture_sub_vtable));
    canard_unsubscribe(&self, &sub);

    canard_destroy(&self);
}

// -------------------------------------------  v0 Single-Frame Message Reception  -------------------------------------

static void test_v0_message_reception()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_v0_subscribe(&self, &sub, 1000U, 0x1234U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = (&cap);

    // Single-frame v0 message from node 10, priority nominal, transfer-ID 3.
    const uint32_t       can_id   = make_v0_msg_can_id(canard_prio_nominal, 1000U, 10U);
    const uint_least8_t  frame[]  = { 0xDEU, 0xADU, make_v0_single_tail(3U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };

    now_val = 1000;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 1000, 0U, can_id, can_data));

    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_INT64(1000, cap.timestamp);
    TEST_ASSERT_EQUAL_UINT8(canard_prio_nominal, cap.priority);
    TEST_ASSERT_EQUAL_UINT8(10U, cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(3U, cap.transfer_id);
    TEST_ASSERT_EQUAL_size_t(2U, cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8(0xDEU, cap.payload_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xADU, cap.payload_buf[1]);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  v0 Service Request Reception  ------------------------------------------

static void test_v0_service_request_reception()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_v0_subscribe_request(&self, &sub, 0x37U, 0xBEEFU, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = (&cap);

    // v0 service request from node 10 to node 42 (us), data_type_id 0x37, transfer-ID 5.
    const uint32_t       can_id   = make_v0_svc_can_id(canard_prio_nominal, 0x37U, true, 42U, 10U);
    const uint_least8_t  frame[]  = { 0xCAU, 0xFEU, make_v0_single_tail(5U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };

    now_val = 2000;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 2000, 0U, can_id, can_data));

    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(10U, cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(5U, cap.transfer_id);
    TEST_ASSERT_EQUAL_size_t(2U, cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8(0xCAU, cap.payload_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFEU, cap.payload_buf[1]);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  v0 Service Response Reception  -----------------------------------------

static void test_v0_service_response_reception()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_v0_subscribe_response(&self, &sub, 0x55U, 0xFACEU, 256U, &capture_sub_vtable));
    sub.user_context = (&cap);

    // v0 service response from node 99 to node 42 (us), data_type_id 0x55, transfer-ID 7.
    const uint32_t       can_id   = make_v0_svc_can_id(canard_prio_nominal, 0x55U, false, 42U, 99U);
    const uint_least8_t  frame[]  = { 0xBEU, 0xEFU, make_v0_single_tail(7U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };

    now_val = 3000;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 3000, 0U, can_id, can_data));

    TEST_ASSERT_EQUAL_size_t(1U, cap.count);
    TEST_ASSERT_EQUAL_UINT8(99U, cap.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(7U, cap.transfer_id);
    TEST_ASSERT_EQUAL_size_t(2U, cap.payload_size);
    TEST_ASSERT_EQUAL_UINT8(0xBEU, cap.payload_buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xEFU, cap.payload_buf[1]);

    canard_unsubscribe(&self, &sub);
    canard_destroy(&self);
}

// -------------------------------------------  v0 Unsubscribe Stops Delivery  -----------------------------------------

static void test_v0_unsubscribe_stops_delivery()
{
    canard_t    self    = {};
    canard_us_t now_val = 0;
    init_canard(&self, &now_val, 42U);

    rx_capture_t          cap = {};
    canard_subscription_t sub = {};
    TEST_ASSERT_TRUE(canard_v0_subscribe(&self, &sub, 500U, 0x1234U, 256U, 2000000, &capture_sub_vtable));
    sub.user_context = (&cap);

    // First frame: should be delivered.
    const uint32_t       can_id   = make_v0_msg_can_id(canard_prio_nominal, 500U, 10U);
    const uint_least8_t  frame[]  = { 0xAAU, make_v0_single_tail(0U) };
    const canard_bytes_t can_data = { .size = sizeof(frame), .data = frame };
    now_val                       = 100;
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 100, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(1U, cap.count);

    // Unsubscribe, then ingest again: callback must NOT fire.
    canard_unsubscribe(&self, &sub);
    cap = {};
    TEST_ASSERT_TRUE(canard_ingest_frame(&self, 200, 0U, can_id, can_data));
    TEST_ASSERT_EQUAL_size_t(0U, cap.count);

    canard_destroy(&self);
}

// -------------------------------------------  Harness  ---------------------------------------------------------------

extern "C" void setUp() {}
extern "C" void tearDown() {}

int main()
{
    UNITY_BEGIN();

    // Argument validation.
    RUN_TEST(test_subscribe_null_args);
    RUN_TEST(test_subscribe_13b_null_args);
    RUN_TEST(test_subscribe_request_null_args);
    RUN_TEST(test_subscribe_response_null_args);
    RUN_TEST(test_subscribe_port_id_range);

    // Subscription management.
    RUN_TEST(test_subscribe_duplicate_rejection);
    RUN_TEST(test_subscribe_unsubscribe_resubscribe);

    // End-to-end message reception.
    RUN_TEST(test_v1v1_message_reception);
    RUN_TEST(test_service_request_reception);
    RUN_TEST(test_service_response_reception);

    // Unsubscribe behavior.
    RUN_TEST(test_unsubscribe_stops_delivery);
    RUN_TEST(test_multiple_subscriptions_routing);
    RUN_TEST(test_unsubscribe_cleans_up_sessions);

    // v0 argument validation.
    RUN_TEST(test_v0_subscribe_null_args);
    RUN_TEST(test_v0_subscribe_request_null_args);
    RUN_TEST(test_v0_subscribe_response_null_args);
    RUN_TEST(test_v0_subscribe_port_id_range);

    // v0 subscription management.
    RUN_TEST(test_v0_subscribe_duplicate_rejection);
    RUN_TEST(test_v0_subscribe_unsubscribe_resubscribe);

    // v0 end-to-end reception.
    RUN_TEST(test_v0_message_reception);
    RUN_TEST(test_v0_service_request_reception);
    RUN_TEST(test_v0_service_response_reception);

    // v0 unsubscribe behavior.
    RUN_TEST(test_v0_unsubscribe_stops_delivery);

    return UNITY_END();
}
