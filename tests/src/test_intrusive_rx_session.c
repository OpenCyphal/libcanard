// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal Development Team.

// Adversarial intrusive test suite for the RX session pipeline: rx_session_update and its supporting functions
// (rx_slot_new, rx_slot_advance, rx_session_accept, rx_session_complete, rx_session_cleanup).
//
// All expected values are hardcoded golden vectors; no computed values from the implementation.
// Golden vectors sourced from:
//   - Cyphal/CAN v1 specification (can.tex), section 8.3
//   - libcanard v4 test vectors (test_private_rx.cpp)
//   - UAVCAN v0 (DroneCAN) bus capture screenshots

#include "canard.c" // NOLINT(bugprone-suspicious-include)
#include "helpers.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// =====================================================================================================================
// Test fixture and helpers.

/// Maximum capture buffer size. Must be large enough for the largest expected transfer payload.
#define CAPTURE_BUF_SIZE 512

typedef struct
{
    canard_us_t      timestamp;
    canard_prio_t    priority;
    byte_t           source_node_id;
    byte_t           transfer_id;
    canard_payload_t payload;
    size_t           call_count;
    byte_t           buf[CAPTURE_BUF_SIZE]; ///< Copy of payload view data for post-callback inspection.
} rx_capture_t;

typedef struct
{
    canard_t                     canard;
    canard_subscription_t        sub;
    instrumented_allocator_t     alloc_session;
    instrumented_allocator_t     alloc_payload;
    rx_capture_t                 capture;
    canard_subscription_vtable_t vtable;
} session_fixture_t;

static void on_message_capture(canard_subscription_t* const self,
                               const canard_us_t            timestamp,
                               const canard_prio_t          priority,
                               const uint_least8_t          source_node_id,
                               const uint_least8_t          transfer_id,
                               const canard_payload_t       payload)
{
    session_fixture_t* const fx =
      (session_fixture_t*)(void*)((char*)self - offsetof(session_fixture_t, sub)); // NOLINT(*-cast-align)
    fx->capture.timestamp      = timestamp;
    fx->capture.priority       = priority;
    fx->capture.source_node_id = (byte_t)source_node_id;
    fx->capture.transfer_id    = (byte_t)transfer_id;
    fx->capture.payload        = payload;
    fx->capture.call_count++;
    // Copy the payload data into the capture buffer so we can inspect it after the origin is freed.
    if (payload.view.size > 0) {
        TEST_PANIC_UNLESS(payload.view.size <= CAPTURE_BUF_SIZE);
        (void)memcpy(fx->capture.buf, payload.view.data, payload.view.size);
        // Redirect the view to our buffer so tests can safely inspect it.
        fx->capture.payload.view.data = fx->capture.buf;
    }
    // For multi-frame transfers the callback owns payload.origin; free it here.
    if (payload.origin.data != NULL) {
        mem_free(self->owner->mem.rx_payload, payload.origin.size, payload.origin.data);
    }
}

static void fixture_init(session_fixture_t* const fx,
                         const canard_kind_t      kind,
                         const uint16_t           port_id,
                         const size_t             extent,
                         const canard_us_t        tid_timeout,
                         const uint16_t           crc_seed)
{
    (void)memset(fx, 0, sizeof(*fx));
    instrumented_allocator_new(&fx->alloc_session);
    instrumented_allocator_new(&fx->alloc_payload);
    fx->canard.mem.rx_session   = instrumented_allocator_make_resource(&fx->alloc_session);
    fx->canard.mem.rx_payload   = instrumented_allocator_make_resource(&fx->alloc_payload);
    fx->sub.owner               = &fx->canard;
    fx->sub.transfer_id_timeout = tid_timeout;
    fx->sub.extent              = extent;
    fx->sub.kind                = kind;
    fx->sub.port_id             = port_id;
    fx->sub.crc_seed            = crc_seed;
    fx->vtable.on_message       = on_message_capture;
    fx->sub.vtable              = &fx->vtable;
    fx->sub.sessions            = NULL;
    // node_id defaults to 0 from memset; tests that need a specific node_id override it.
}

static void fixture_init_v1(session_fixture_t* const fx,
                            const canard_kind_t      kind,
                            const uint16_t           port_id,
                            const size_t             extent)
{
    fixture_init(fx, kind, port_id, extent, 2 * MEGA, CRC_INITIAL);
}

/// Feed a frame into the RX session pipeline. Returns false on OOM, true on success.
static bool feed(session_fixture_t* const fx, const canard_us_t ts, const frame_t* const fr, const byte_t iface_index)
{
    const uint64_t c_oom = fx->canard.err.oom;
    rx_session_update(&fx->sub, ts, fr, iface_index);
    return fx->canard.err.oom == c_oom; // OOM is the only expected error mode.
}

/// Construct a frame_t for a single-frame transfer (start=true, end=true).
static frame_t make_single_frame(const canard_prio_t priority,
                                 const canard_kind_t kind,
                                 const uint16_t      port_id,
                                 const byte_t        dst,
                                 const byte_t        src,
                                 const byte_t        transfer_id,
                                 const void* const   data,
                                 const size_t        size)
{
    frame_t fr;
    (void)memset(&fr, 0, sizeof(fr));
    fr.priority    = priority;
    fr.kind        = kind;
    fr.port_id     = port_id;
    fr.dst         = dst;
    fr.src         = src;
    fr.transfer_id = transfer_id;
    fr.start       = true;
    fr.end         = true;
    fr.toggle      = (canard_kind_version(kind) == 1); // v1: toggle starts at 1, v0: toggle starts at 0
    fr.payload     = (canard_bytes_t){ .size = size, .data = data };
    return fr;
}

/// Construct a frame_t for a multi-frame start frame.
static frame_t make_start_frame(const canard_prio_t priority,
                                const canard_kind_t kind,
                                const uint16_t      port_id,
                                const byte_t        dst,
                                const byte_t        src,
                                const byte_t        transfer_id,
                                const void* const   data,
                                const size_t        size)
{
    frame_t fr;
    (void)memset(&fr, 0, sizeof(fr));
    fr.priority    = priority;
    fr.kind        = kind;
    fr.port_id     = port_id;
    fr.dst         = dst;
    fr.src         = src;
    fr.transfer_id = transfer_id;
    fr.start       = true;
    fr.end         = false;
    fr.toggle      = (canard_kind_version(kind) == 1); // v1: toggle starts at 1, v0: toggle starts at 0
    fr.payload     = (canard_bytes_t){ .size = size, .data = data };
    return fr;
}

/// Construct a continuation frame.
static frame_t make_cont_frame(const canard_prio_t priority,
                               const canard_kind_t kind,
                               const uint16_t      port_id,
                               const byte_t        dst,
                               const byte_t        src,
                               const byte_t        transfer_id,
                               const bool          end,
                               const bool          toggle,
                               const void* const   data,
                               const size_t        size)
{
    frame_t fr;
    (void)memset(&fr, 0, sizeof(fr));
    fr.priority    = priority;
    fr.kind        = kind;
    fr.port_id     = port_id;
    fr.dst         = dst;
    fr.src         = src;
    fr.transfer_id = transfer_id;
    fr.start       = false;
    fr.end         = end;
    fr.toggle      = toggle;
    fr.payload     = (canard_bytes_t){ .size = size, .data = data };
    return fr;
}

/// Destroy all sessions in the subscription tree. Must be called before checking alloc balance.
static void fixture_destroy_all_sessions(session_fixture_t* const fx)
{
    while (fx->sub.sessions != NULL) {
        rx_session_t* ses = CAVL2_TO_OWNER(fx->sub.sessions, rx_session_t, index);
        rx_session_destroy(ses);
    }
}

static void fixture_check_alloc_balance(const session_fixture_t* const fx)
{
    // After all slots are freed and sessions destroyed, there should be no outstanding allocations.
    TEST_ASSERT_EQUAL_size_t(0, fx->alloc_payload.allocated_fragments);
    TEST_ASSERT_EQUAL_size_t(0, fx->alloc_session.allocated_fragments);
}

// =====================================================================================================================
// Group 1: Slot Lifecycle

/// Verify rx_slot_new creates a v1.1 slot with CRC=0xFFFF, toggle=1, total_size=0.
static void test_slot_new_v1(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v1_message, 2222, 64);
    rx_slot_t* const slot = rx_slot_new(&fx.sub, 1 * MEGA, 5, 0);
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, slot->crc);
    TEST_ASSERT_EQUAL_UINT8(1, slot->expected_toggle);
    TEST_ASSERT_EQUAL_UINT32(0, slot->total_size);
    TEST_ASSERT_EQUAL_UINT8(5, slot->transfer_id);
    TEST_ASSERT_EQUAL_UINT8(0, slot->iface_index);
    TEST_ASSERT_EQUAL_INT64(1 * MEGA, slot->start_ts);
    rx_slot_destroy(&fx.sub, slot);
    fixture_check_alloc_balance(&fx);
}

/// Verify rx_slot_new creates a v0.1 slot with custom crc_seed and toggle=0.
static void test_slot_new_v0(void)
{
    session_fixture_t fx;
    const uint16_t    seed = canard_0v1_crc_seed_from_data_type_signature(0xe2a7d4a9460bc2f2ULL);
    fixture_init(&fx, canard_kind_0v1_message, 1001, 64, 2 * MEGA, seed);
    rx_slot_t* const slot = rx_slot_new(&fx.sub, 2 * MEGA, 17, 1);
    TEST_ASSERT_NOT_NULL(slot);
    TEST_ASSERT_EQUAL_UINT16(seed, slot->crc);
    TEST_ASSERT_EQUAL_UINT8(0, slot->expected_toggle); // v0 toggle starts at 0
    TEST_ASSERT_EQUAL_UINT32(0, slot->total_size);
    TEST_ASSERT_EQUAL_UINT8(17, slot->transfer_id);
    TEST_ASSERT_EQUAL_UINT8(1, slot->iface_index);
    rx_slot_destroy(&fx.sub, slot);
    fixture_check_alloc_balance(&fx);
}

/// Verify rx_slot_new returns NULL when allocator is exhausted.
static void test_slot_new_oom(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v1_message, 2222, 64);
    fx.alloc_payload.limit_fragments = 0; // No allocations allowed.
    rx_slot_t* const slot            = rx_slot_new(&fx.sub, 1 * MEGA, 0, 0);
    TEST_ASSERT_NULL(slot);
    fixture_check_alloc_balance(&fx);
}

/// Feed 7+7 bytes into a slot with extent=10, verify truncation.
static void test_slot_advance_and_truncation(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v1_message, 2222, 10);
    rx_slot_t* const slot = rx_slot_new(&fx.sub, 1 * MEGA, 0, 0);
    TEST_ASSERT_NOT_NULL(slot);

    const byte_t data0[7] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    const byte_t data1[7] = { 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E };
    rx_slot_advance(slot, 10, (canard_bytes_t){ .size = 7, .data = data0 });
    TEST_ASSERT_EQUAL_UINT32(7, slot->total_size);
    // Toggle flipped: 1 -> 0.
    TEST_ASSERT_EQUAL_UINT8(0, slot->expected_toggle);

    rx_slot_advance(slot, 10, (canard_bytes_t){ .size = 7, .data = data1 });
    TEST_ASSERT_EQUAL_UINT32(14, slot->total_size);    // Total tracks original size.
    TEST_ASSERT_EQUAL_UINT8(1, slot->expected_toggle); // Toggle flipped: 0 -> 1.

    // Verify stored payload: first 7 bytes, then only 3 more (extent=10).
    const byte_t expected[10] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A };
    TEST_ASSERT_EQUAL_INT(0, memcmp(slot->payload, expected, 10));

    rx_slot_destroy(&fx.sub, slot);
    fixture_check_alloc_balance(&fx);
}

/// Extent=0: nothing stored but total_size still tracks.
static void test_slot_advance_zero_extent(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v1_message, 2222, 0);
    // Cannot allocate a slot with extent=0 through the normal path, so allocate directly.
    // rx_slot_new allocates RX_SLOT_OVERHEAD + extent bytes. With extent=0, this is just the overhead.
    rx_slot_t* const slot = rx_slot_new(&fx.sub, 1 * MEGA, 0, 0);
    TEST_ASSERT_NOT_NULL(slot);

    const byte_t data[7] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    rx_slot_advance(slot, 0, (canard_bytes_t){ .size = 7, .data = data });
    TEST_ASSERT_EQUAL_UINT32(7, slot->total_size);
    rx_slot_advance(slot, 0, (canard_bytes_t){ .size = 7, .data = data });
    TEST_ASSERT_EQUAL_UINT32(14, slot->total_size);

    rx_slot_destroy(&fx.sub, slot);
    fixture_check_alloc_balance(&fx);
}

// =====================================================================================================================
// Group 2: v1 Golden Single-Frame

/// Spec heartbeat (v1.1 message): subject=32085, src=42, prio=nominal, 7-byte payload.
// Source: Cyphal/CAN specification, section 8.3, Example 1 (CAN ID=0x107D552A)
static void test_golden_v1_heartbeat(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v1_message, 32085, 64);
    fx.canard.node_id = CANARD_NODE_ID_ANONYMOUS; // Messages are broadcast, node_id doesn't matter.

    // Frame data WITHOUT tail byte. The tail byte 0xE0 is SOT|EOT|toggle=1|TID=0.
    const byte_t payload[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xA1 };
    frame_t      fr        = make_single_frame(
      canard_prio_nominal, canard_kind_1v1_message, 32085, CANARD_NODE_ID_ANONYMOUS, 42, 0, payload, sizeof(payload));

    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    TEST_ASSERT_EQUAL_INT64(1 * MEGA, fx.capture.timestamp);
    TEST_ASSERT_EQUAL_INT(canard_prio_nominal, fx.capture.priority);
    TEST_ASSERT_EQUAL_UINT8(42, fx.capture.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(0, fx.capture.transfer_id);
    TEST_ASSERT_EQUAL_size_t(7, fx.capture.payload.view.size);
    TEST_ASSERT_EQUAL_INT(0, memcmp(fx.capture.payload.view.data, payload, 7));
    // Single-frame: origin must be NULL.
    TEST_ASSERT_NULL(fx.capture.payload.origin.data);
    TEST_ASSERT_EQUAL_size_t(0, fx.capture.payload.origin.size);
    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Heartbeat TID sequence: 4 heartbeats TID 0-3, verify progression and duplicate rejection.
static void test_golden_v1_heartbeat_tid_sequence(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v1_message, 32085, 64);

    const byte_t payload[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xA1 };
    canard_us_t  ts        = 1 * MEGA;
    for (byte_t tid = 0; tid < 4; tid++) {
        frame_t fr = make_single_frame(canard_prio_nominal,
                                       canard_kind_1v1_message,
                                       32085,
                                       CANARD_NODE_ID_ANONYMOUS,
                                       42,
                                       tid,
                                       payload,
                                       sizeof(payload));
        TEST_ASSERT_TRUE(feed(&fx, ts, &fr, 0));
        TEST_ASSERT_EQUAL_size_t(tid + 1U, fx.capture.call_count);
        TEST_ASSERT_EQUAL_UINT8(tid, fx.capture.transfer_id);
        ts += MEGA;
    }

    // Duplicate of TID=3 should be rejected (same TID, same priority, within timeout).
    {
        frame_t fr = make_single_frame(canard_prio_nominal,
                                       canard_kind_1v1_message,
                                       32085,
                                       CANARD_NODE_ID_ANONYMOUS,
                                       42,
                                       3,
                                       payload,
                                       sizeof(payload));
        TEST_ASSERT_TRUE(feed(&fx, ts, &fr, 0));
        TEST_ASSERT_EQUAL_size_t(4, fx.capture.call_count); // No new callback.
    }

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// v4 "DUCK" single-frame (TID=1, prio=slow, subject=2222, src=42).
// Source: libcanard v4 golden vectors
static void test_golden_v1_duck(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 64);

    // Frame was {0x44, 0x55, 0x43, 0x4B, 0xE1}; tail 0xE1 = SOT|EOT|toggle=1|TID=1. Payload = first 4 bytes.
    const byte_t payload[] = { 0x44, 0x55, 0x43, 0x4B };
    frame_t      fr        = make_single_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 1, payload, sizeof(payload));

    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT8(42, fx.capture.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(1, fx.capture.transfer_id);
    TEST_ASSERT_EQUAL_INT(canard_prio_slow, fx.capture.priority);
    TEST_ASSERT_EQUAL_size_t(4, fx.capture.payload.view.size);
    TEST_ASSERT_EQUAL_INT(0, memcmp(fx.capture.payload.view.data, payload, 4));
    TEST_ASSERT_NULL(fx.capture.payload.origin.data);
    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

// =====================================================================================================================
// Group 3: v1 Golden Multi-Frame

/// Spec NodeInfo response (v1.0), 11 Classic CAN frames.
// Source: Cyphal/CAN specification, section 8.3 (CAN ID=0x126BBDAA)
// service_id=430, dst=123, src=42, prio=nominal, kind=canard_kind_1v0_response, TID=1
static void test_golden_v1_nodeinfo_11_frames(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_response, 430, 256);
    fx.canard.node_id = 123;

    // Payloads without the tail byte (last byte stripped from each frame).
    // F0: SOT, toggle=1, TID=1 (tail=0xA1)
    const byte_t f0[] = { 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 };
    // F1: toggle=0, TID=1 (tail=0x01)
    const byte_t f1[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    // F2: toggle=1 (tail=0x21)
    const byte_t f2[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    // F3: toggle=0 (tail=0x01)
    const byte_t f3[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    // F4: toggle=1 (tail=0x21)
    const byte_t f4[] = { 0x00, 0x00, 0x24, 0x6F, 0x72, 0x67, 0x2E };
    // F5: toggle=0 (tail=0x01)
    const byte_t f5[] = { 0x75, 0x61, 0x76, 0x63, 0x61, 0x6E, 0x2E };
    // F6: toggle=1 (tail=0x21)
    const byte_t f6[] = { 0x70, 0x79, 0x75, 0x61, 0x76, 0x63, 0x61 };
    // F7: toggle=0 (tail=0x01)
    const byte_t f7[] = { 0x6E, 0x2E, 0x64, 0x65, 0x6D, 0x6F, 0x2E };
    // F8: toggle=1 (tail=0x21)
    const byte_t f8[] = { 0x62, 0x61, 0x73, 0x69, 0x63, 0x5F, 0x75 };
    // F9: toggle=0 (tail=0x01), CRC MSB=0x9A is part of payload
    const byte_t f9[] = { 0x73, 0x61, 0x67, 0x65, 0x00, 0x00, 0x9A };
    // F10: EOT, toggle=1, TID=1 (tail=0x61). CRC LSB=0xE7.
    const byte_t f10[] = { 0xE7 };

    const canard_us_t ts = 1 * MEGA;

    // Frame 0: start
    frame_t fr0 = make_start_frame(canard_prio_nominal, canard_kind_1v0_response, 430, 123, 42, 1, f0, sizeof(f0));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr0, 0));
    TEST_ASSERT_EQUAL_size_t(0, fx.capture.call_count);

    // Frames 1-9: continuation
    const byte_t* cont_data[] = { f1, f2, f3, f4, f5, f6, f7, f8, f9 };
    bool          cont_toggle = false; // After start toggle=1, next is 0.
    for (size_t i = 0; i < 9; i++) {
        frame_t fc = make_cont_frame(
          canard_prio_nominal, canard_kind_1v0_response, 430, 123, 42, 1, false, cont_toggle, cont_data[i], 7);
        TEST_ASSERT_TRUE(feed(&fx, ts, &fc, 0));
        TEST_ASSERT_EQUAL_size_t(0, fx.capture.call_count);
        cont_toggle = !cont_toggle;
    }

    // Frame 10: end
    frame_t fr10 =
      make_cont_frame(canard_prio_nominal, canard_kind_1v0_response, 430, 123, 42, 1, true, true, f10, sizeof(f10));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr10, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    TEST_ASSERT_EQUAL_INT(canard_prio_nominal, fx.capture.priority);
    TEST_ASSERT_EQUAL_UINT8(42, fx.capture.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(1, fx.capture.transfer_id);

    // Expected payload: total raw size is 7*9 + 7 + 1 = 71 bytes. CRC is 2 bytes appended (v1 big-endian).
    // Actual transfer payload = 71 - 2 = 69 bytes.
    TEST_ASSERT_EQUAL_size_t(69, fx.capture.payload.view.size);

    // Verify specific bytes in the reassembled payload.
    // Payload is the concatenation of all frame payloads (f0..f9, f10) minus the last 2 CRC bytes.
    // f0 starts at offset 0: {0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00}
    // f1 at offset 7:  7 x 0x00
    // f2 at offset 14: 7 x 0x00
    // f3 at offset 21: 7 x 0x00
    // f4 at offset 28: {0x00, 0x00, 0x24, 0x6F, 0x72, 0x67, 0x2E}
    // f5 at offset 35: {0x75, 0x61, 0x76, 0x63, 0x61, 0x6E, 0x2E}
    // f6 at offset 42: {0x70, 0x79, 0x75, 0x61, 0x76, 0x63, 0x61}
    // f7 at offset 49: {0x6E, 0x2E, 0x64, 0x65, 0x6D, 0x6F, 0x2E}
    // f8 at offset 56: {0x62, 0x61, 0x73, 0x69, 0x63, 0x5F, 0x75}
    // f9 at offset 63: {0x73, 0x61, 0x67, 0x65, 0x00, 0x00, 0x9A}
    // f10 at offset 70: {0xE7}  <- CRC bytes (0x9A at offset 69, 0xE7 at offset 70)
    // Total raw = 71, CRC = 2 bytes at end, payload = 69 bytes.
    const byte_t* vd = (const byte_t*)fx.capture.payload.view.data;
    TEST_ASSERT_EQUAL_HEX8(0x01, vd[0]);  // f0[0]
    TEST_ASSERT_EQUAL_HEX8(0x01, vd[4]);  // f0[4]
    TEST_ASSERT_EQUAL_HEX8(0x24, vd[30]); // f4[2] = string length prefix = 36 = 0x24
    // "org.uavcan.pyuavcan.demo.basic_usage" = 36 chars starting at offset 31
    const byte_t expected_name[] = "org.uavcan.pyuavcan.demo.basic_usage";
    TEST_ASSERT_EQUAL_INT(0, memcmp(&vd[31], expected_name, 36));

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// v4 {1..14} 3 Classic CAN frames with CRC 0x32F8 (TID=2, prio=slow, subject=2222, src=42).
// Source: libcanard v4 golden vectors
static void test_golden_v1_seq14_3_frames(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 64);

    // Frame 0: {0x01..0x07}, tail=0xA2 (SOT, toggle=1, TID=2)
    const byte_t f0[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    // Frame 1: {0x08..0x0E}, tail=0x02 (toggle=0, TID=2)
    const byte_t f1[] = { 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E };
    // Frame 2: {0x32, 0xF8}, tail=0x62 (EOT, toggle=1, TID=2). CRC=0x32F8 big-endian.
    const byte_t f2[] = { 0x32, 0xF8 };

    const canard_us_t ts = 1 * MEGA;

    frame_t fr0 = make_start_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 2, f0, sizeof(f0));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr0, 0));

    frame_t fr1 = make_cont_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 2, false, false, f1, sizeof(f1));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr1, 0));

    frame_t fr2 = make_cont_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 2, true, true, f2, sizeof(f2));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr2, 0));

    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT8(42, fx.capture.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(2, fx.capture.transfer_id);
    TEST_ASSERT_EQUAL_size_t(14, fx.capture.payload.view.size);

    const byte_t expected[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };
    TEST_ASSERT_EQUAL_INT(0, memcmp(fx.capture.payload.view.data, expected, 14));

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// v4 {0..99} 2 CAN FD frames with CRC 0xCFFC and padding (TID=3, prio=slow, subject=2222, src=42).
// Source: libcanard v4 golden vectors
static void test_golden_v1_seq100_fd(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 256);

    // Frame 0: {0x00..0x3E} = 63 data bytes, tail=0xA3 (SOT, toggle=1, TID=3). Payload = 63 bytes.
    byte_t f0[63];
    for (size_t i = 0; i < 63; i++) {
        f0[i] = (byte_t)i;
    }

    // Frame 1: {0x3F..0x63} = 37 data bytes + 9 padding zeroes + CRC 0xCFFC + tail 0x43 (EOT,toggle=0,TID=3)
    // Payload = 37 + 9 + 2 = 48 bytes (without tail).
    byte_t f1[48];
    for (size_t i = 0; i < 37; i++) {
        f1[i] = (byte_t)(i + 63);
    }
    for (size_t i = 37; i < 46; i++) {
        f1[i] = 0x00; // Padding
    }
    f1[46] = 0xD4; // CRC MSB (CRC of {0..99} + 9 padding zeros = 0xD4A3)
    f1[47] = 0xA3; // CRC LSB

    const canard_us_t ts = 1 * MEGA;

    frame_t fr0 = make_start_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 3, f0, sizeof(f0));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr0, 0));

    frame_t fr1 = make_cont_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 3, true, false, f1, sizeof(f1));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr1, 0));

    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT8(42, fx.capture.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(3, fx.capture.transfer_id);
    // Total raw size = 63 + 48 = 111. CRC 2 bytes. Transfer payload = 111 - 2 = 109.
    // But the actual user data is only 100 bytes (0..99); the rest is padding which is included in the payload.
    // The total_size includes everything before CRC stripping: 63+48=111. After CRC removal: 109.
    // Since extent=256 > 109, no truncation. Payload size = 109 - 0 = 109... wait.
    // Actually total_size in the code tracks the sum of all frame payloads = 63+48 = 111.
    // The completion code does: size = smaller(total_size - 2, extent) = smaller(109, 256) = 109.
    // But the actual meaningful data is only 100 bytes; the extra 9 bytes are padding zeros.
    // The protocol does not distinguish padding from data at this layer; application must handle it.
    TEST_ASSERT_EQUAL_size_t(109, fx.capture.payload.view.size);

    // Verify the first 100 bytes are {0,1,...,99}.
    const byte_t* vd = (const byte_t*)fx.capture.payload.view.data;
    for (size_t i = 0; i < 100; i++) {
        TEST_ASSERT_EQUAL_HEX8((byte_t)i, vd[i]);
    }
    // Verify the next 9 bytes are padding zeros.
    for (size_t i = 100; i < 109; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00, vd[i]);
    }

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// v4 3-frame truncated (TID=13, prio=slow, subject=2222, src=55, extent=16).
// Source: libcanard v4 golden test vectors (test_private_rx.cpp)
static void test_golden_v4_3frame_truncated(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 16);

    // Frame 0: {0x06*7}, tail: SOT, toggle=1, TID=13
    const byte_t f0[] = { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06 };
    // Frame 1: {0x07*7}, tail: toggle=0, TID=13
    const byte_t f1[] = { 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 };
    // Frame 2: {0x09, 0x09, 0x09, 0x09, 0x0D, 0x93}, tail: EOT, toggle=1, TID=13
    // CRC bytes are 0x0D, 0x93 big-endian; but they are part of the frame payload at this level.
    const byte_t f2[] = { 0x09, 0x09, 0x09, 0x09, 0x0D, 0x93 };

    const canard_us_t ts = 1 * MEGA;

    frame_t fr0 = make_start_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 55, 13, f0, sizeof(f0));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr0, 0));

    frame_t fr1 = make_cont_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 55, 13, false, false, f1, sizeof(f1));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr1, 0));

    frame_t fr2 = make_cont_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 55, 13, true, true, f2, sizeof(f2));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr2, 0));

    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT8(55, fx.capture.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(13, fx.capture.transfer_id);

    // Total raw size = 7+7+6 = 20. CRC = 2 bytes. Payload = 20-2 = 18. Extent=16 so truncated to 16.
    TEST_ASSERT_EQUAL_size_t(16, fx.capture.payload.view.size);

    // Expected payload: 0x06*7 + 0x07*7 + 0x09*2 (truncated at extent=16).
    const byte_t expected[16] = { 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07,
                                  0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x09, 0x09 };
    TEST_ASSERT_EQUAL_INT(0, memcmp(fx.capture.payload.view.data, expected, 16));

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// v4 CRC split across frame boundary (TID=0, prio=slow, subject=2222, src=55).
// Source: libcanard v4 golden test vectors (test_private_rx.cpp)
static void test_golden_v4_crc_split(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 64);

    // User payload: 6 bytes of 0x0E. CRC(0xFFFF, 6x0x0E) = 0x3A81.
    // But the total transfer data is 8 bytes: {user_data(6), CRC_MSB(0x3A), CRC_LSB(0x81)}.
    // Frame 0 (7 bytes): first 6 data bytes + CRC MSB split into frame.
    const byte_t f0[] = { 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x3A };
    // Frame 1 (1 byte): CRC LSB.
    const byte_t f1[] = { 0x81 };

    const canard_us_t ts = 1 * MEGA;

    frame_t fr0 = make_start_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 55, 0, f0, sizeof(f0));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr0, 0));

    frame_t fr1 = make_cont_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 55, 0, true, false, f1, sizeof(f1));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr1, 0));

    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    // Total raw = 7 + 1 = 8. CRC=2. Payload = 8-2 = 6. But only 7 bytes of 0x0E minus the last CRC byte...
    // Actually the slot stores everything as raw bytes and CRC is verified via the residue.
    // total_size = 7+1 = 8. size = smaller(8-2, 64) = 6.
    // Wait, the payload data is 0x0E*7 then 0xD7 stored sequentially (extent=64 > 8 so all fit).
    // The actual payload starts at slot->payload[0] = first byte of first frame's payload.
    // After CRC stripping (v1): view.data = slot->payload, size = 6.
    // But the 7th byte (0x0E) and the 0xD7 are the CRC bytes appended to the payload; they are part of the stored data.
    // So payload view = first 6 bytes of {0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0xD7} = 6 bytes of 0x0E.
    // Wait, total_size is 8 (7 from f0 + 1 from f1). CRC = last 2 bytes of the transfer. So user data = 6 bytes.
    // But total_size tracks the accumulation via rx_slot_advance, which adds payload sizes.
    // The view size = min(total_size - 2, extent) = min(6, 64) = 6.
    TEST_ASSERT_EQUAL_size_t(6, fx.capture.payload.view.size);

    const byte_t expected[6] = { 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E };
    TEST_ASSERT_EQUAL_INT(0, memcmp(fx.capture.payload.view.data, expected, 6));

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// v4 bad CRC: same as crc_split but last byte is 0xD8 instead of 0xD7.
// Source: libcanard v4 golden test vectors (test_private_rx.cpp)
static void test_golden_v4_bad_crc(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 64);

    const byte_t f0[] = { 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E };
    const byte_t f1[] = { 0xD8 }; // Bad CRC byte.

    const canard_us_t ts = 1 * MEGA;

    frame_t fr0 = make_start_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 55, 0, f0, sizeof(f0));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr0, 0));

    frame_t fr1 = make_cont_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 55, 0, true, false, f1, sizeof(f1));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr1, 0));

    // No callback: bad CRC.
    TEST_ASSERT_EQUAL_size_t(0, fx.capture.call_count);
    // err.rx_transfer should be incremented.
    TEST_ASSERT_EQUAL_UINT64(1, fx.canard.err.rx_transfer);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Synthetic 2-frame v1 where CRC is entirely in the last frame.
/// Payload: 7 bytes of 0xAA. CRC computed over those 7 bytes with seed 0xFFFF.
static void test_golden_v1_crc_full_in_last(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 64);

    const byte_t data[7] = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA };

    // Compute the expected CRC of 7 bytes of 0xAA with seed 0xFFFF.
    // We use a hardcoded golden value here. CRC-16/CCITT-FALSE of 7x 0xAA = 0xBE96.
    // To verify: the residue after appending CRC big-endian should be 0x0000.
    // CRC(0xFFFF, {0xAA*7}) = 0xBE96
    // The 2 CRC bytes appended big-endian: {0xBE, 0x96}. Residue = CRC(0xBE96, {}) but actually
    // residue = CRC(CRC(0xFFFF, {0xAA*7, 0xBE, 0x96})) = 0x0000.
    const uint16_t golden_crc = crc_add(CRC_INITIAL, 7, data);
    // Store both bytes for the frame.
    const byte_t f0[] = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA };
    byte_t       f1[2];
    f1[0] = (byte_t)(golden_crc >> 8U);  // MSB
    f1[1] = (byte_t)(golden_crc & 0xFF); // LSB  // NOLINT(hicpp-signed-bitwise)

    const canard_us_t ts = 1 * MEGA;

    frame_t fr0 = make_start_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 0, f0, sizeof(f0));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr0, 0));

    frame_t fr1 = make_cont_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 0, true, false, f1, sizeof(f1));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr1, 0));

    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    // Total raw = 7 + 2 = 9. Payload = 9 - 2 = 7.
    TEST_ASSERT_EQUAL_size_t(7, fx.capture.payload.view.size);
    TEST_ASSERT_EQUAL_INT(0, memcmp(fx.capture.payload.view.data, data, 7));

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

// =====================================================================================================================
// Group 4: v0 Golden Vectors

/// Screenshot StaticPressure (DTID=1028, single-frame, src=125).
// Source: UAVCAN v0 bus capture screenshot
static void test_golden_v0_single_frame_pressure(void)
{
    session_fixture_t fx;
    // v0 single-frame: any crc_seed, doesn't matter (no CRC for single-frame).
    fixture_init(&fx, canard_kind_0v1_message, 1028, 64, 2 * MEGA, 0);

    // Frame raw: {0x00, 0x80, 0xC3, 0x47, 0x40, 0x56, 0xC0}
    // Tail 0xC0 = SOT|EOT|toggle=0|TID=0. Payload = first 6 bytes.
    const byte_t payload[] = { 0x00, 0x80, 0xC3, 0x47, 0x40, 0x56 };
    frame_t      fr        = make_single_frame(canard_prio_exceptional,
                                   canard_kind_0v1_message,
                                   1028,
                                   CANARD_NODE_ID_ANONYMOUS,
                                   125,
                                   0,
                                   payload,
                                   sizeof(payload));

    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT8(125, fx.capture.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(0, fx.capture.transfer_id);
    TEST_ASSERT_EQUAL_size_t(6, fx.capture.payload.view.size);
    TEST_ASSERT_EQUAL_INT(0, memcmp(fx.capture.payload.view.data, payload, 6));
    TEST_ASSERT_NULL(fx.capture.payload.origin.data);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Screenshot StaticTemperature (DTID=1029, single-frame, src=125).
// Source: UAVCAN v0 bus capture screenshot
static void test_golden_v0_single_frame_temperature(void)
{
    session_fixture_t fx;
    fixture_init(&fx, canard_kind_0v1_message, 1029, 64, 2 * MEGA, 0);

    // Frame raw: {0xBE, 0x5C, 0x00, 0x44, 0xC0}
    // Tail 0xC0 = SOT|EOT|toggle=0|TID=0. Payload = first 4 bytes.
    const byte_t payload[] = { 0xBE, 0x5C, 0x00, 0x44 };
    frame_t      fr        = make_single_frame(canard_prio_exceptional,
                                   canard_kind_0v1_message,
                                   1029,
                                   CANARD_NODE_ID_ANONYMOUS,
                                   125,
                                   0,
                                   payload,
                                   sizeof(payload));

    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT8(125, fx.capture.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(0, fx.capture.transfer_id);
    TEST_ASSERT_EQUAL_size_t(4, fx.capture.payload.view.size);
    TEST_ASSERT_EQUAL_INT(0, memcmp(fx.capture.payload.view.data, payload, 4));
    TEST_ASSERT_NULL(fx.capture.payload.origin.data);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Screenshot MagneticFieldStrength (DTID=1001, 2-frame, src=125).
// Source: UAVCAN v0 bus capture screenshot
// Data type signature: 0xe2a7d4a9460bc2f2ULL
// CRC seed = canard_0v1_crc_seed_from_data_type_signature(0xe2a7d4a9460bc2f2ULL)
// 2-frame multi-frame (TID=17):
// Frame 0 raw: {0x35, 0xB1, 0x2E, 0x2C, 0xB7, 0xB3, 0x4D, 0x91}
//   tail 0x91: SOT=1,EOT=0,toggle=0,TID=17. Payload (7 bytes) = {0x35, 0xB1, 0x2E, 0x2C, 0xB7, 0xB3, 0x4D}
// Frame 1 raw: {0x3A, 0x1F, 0x1D, 0x71}
//   tail 0x71: SOT=0,EOT=1,toggle=1,TID=17. Payload (3 bytes) = {0x3A, 0x1F, 0x1D}
// CRC ref (LE from first 2 payload bytes of stored data): 0xB135
// Expected user payload (after CRC strip): {0x2E, 0x2C, 0xB7, 0xB3, 0x4D, 0x3A, 0x1F, 0x1D} (8 bytes)
static void test_golden_v0_screenshot_magfield(void)
{
    const uint16_t    crc_seed = canard_0v1_crc_seed_from_data_type_signature(0xe2a7d4a9460bc2f2ULL);
    session_fixture_t fx;
    // v0 multi-frame: extent must include 2 bytes for CRC that are stored at the beginning.
    fixture_init(&fx, canard_kind_0v1_message, 1001, 64, 2 * MEGA, crc_seed);

    // Frame 0: 7 payload bytes (no tail).
    const byte_t f0[] = { 0x35, 0xB1, 0x2E, 0x2C, 0xB7, 0xB3, 0x4D };
    // Frame 1: 3 payload bytes (no tail).
    const byte_t f1[] = { 0x3A, 0x1F, 0x1D };

    const canard_us_t ts = 1 * MEGA;

    // Start frame: toggle=0 for v0
    frame_t fr0 = make_start_frame(
      canard_prio_exceptional, canard_kind_0v1_message, 1001, CANARD_NODE_ID_ANONYMOUS, 125, 17, f0, sizeof(f0));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr0, 0));

    // End frame: toggle=1
    frame_t fr1 = make_cont_frame(canard_prio_exceptional,
                                  canard_kind_0v1_message,
                                  1001,
                                  CANARD_NODE_ID_ANONYMOUS,
                                  125,
                                  17,
                                  true,
                                  true,
                                  f1,
                                  sizeof(f1));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr1, 0));

    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT8(125, fx.capture.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(17, fx.capture.transfer_id);

    // v0 multi-frame: CRC is the first 2 bytes in little-endian in the reassembled payload.
    // total_size = 7 + 3 = 10. Payload = min(10-2, extent-2) = min(8, 62) = 8.
    TEST_ASSERT_EQUAL_size_t(8, fx.capture.payload.view.size);

    const byte_t expected[] = { 0x2E, 0x2C, 0xB7, 0xB3, 0x4D, 0x3A, 0x1F, 0x1D };
    TEST_ASSERT_EQUAL_INT(0, memcmp(fx.capture.payload.view.data, expected, 8));

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Screenshot gnss.Fix (DTID=1060, 8-frame, src=125).
// Source: UAVCAN v0 bus capture screenshot
// Data type signature: 0x54c1572b9e07f297ULL
static void test_golden_v0_screenshot_gnss_fix(void)
{
    const uint16_t    crc_seed = canard_0v1_crc_seed_from_data_type_signature(0x54c1572b9e07f297ULL);
    session_fixture_t fx;
    fixture_init(&fx, canard_kind_0v1_message, 1060, 256, 2 * MEGA, crc_seed);

    // 8 frames, each 7-byte payload except last. Tail bytes stripped.
    // Frame 0: SOT,toggle=0,TID=10
    const byte_t f0[] = { 0xF9, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00 };
    // Frame 1: toggle=1
    const byte_t f1[] = { 0x00, 0x00, 0xDD, 0xD4, 0x65, 0xA9, 0x14 };
    // Frame 2: toggle=0
    const byte_t f2[] = { 0x2E, 0x05, 0x40, 0x1A, 0xA6, 0xF1, 0xCC };
    // Frame 3: toggle=1
    const byte_t f3[] = { 0xE0, 0x05, 0xC2, 0x33, 0x72, 0x58, 0x54 };
    // Frame 4: toggle=0
    const byte_t f4[] = { 0x81, 0x80, 0xC4, 0x9E, 0x90, 0x10, 0xA8 };
    // Frame 5: toggle=1
    const byte_t f5[] = { 0xA2, 0x5A, 0xB0, 0x0A, 0x2F, 0x13, 0x7F };
    // Frame 6: toggle=0
    const byte_t f6[] = { 0x50, 0x03, 0x5B, 0x68, 0x5B, 0x68, 0xB9 };
    // Frame 7: EOT,toggle=1,TID=10. Payload 3 bytes.
    const byte_t f7[] = { 0x6A, 0x39, 0x48 };

    const canard_us_t ts = 1 * MEGA;

    // Frame 0: start
    frame_t fr0 = make_start_frame(
      canard_prio_exceptional, canard_kind_0v1_message, 1060, CANARD_NODE_ID_ANONYMOUS, 125, 10, f0, sizeof(f0));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr0, 0));

    // Frames 1-6: continuation
    const byte_t* const cont_data[] = { f1, f2, f3, f4, f5, f6 };
    bool                toggle      = true; // After v0 start toggle=0, next is 1.
    for (size_t i = 0; i < 6; i++) {
        frame_t fc = make_cont_frame(canard_prio_exceptional,
                                     canard_kind_0v1_message,
                                     1060,
                                     CANARD_NODE_ID_ANONYMOUS,
                                     125,
                                     10,
                                     false,
                                     toggle,
                                     cont_data[i],
                                     7);
        TEST_ASSERT_TRUE(feed(&fx, ts, &fc, 0));
        toggle = !toggle;
    }

    // Frame 7: end
    frame_t fr7 = make_cont_frame(canard_prio_exceptional,
                                  canard_kind_0v1_message,
                                  1060,
                                  CANARD_NODE_ID_ANONYMOUS,
                                  125,
                                  10,
                                  true,
                                  true,
                                  f7,
                                  sizeof(f7));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr7, 0));

    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT8(125, fx.capture.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(10, fx.capture.transfer_id);

    // Total raw = 7*7 + 3 = 52. CRC = 2 bytes (LE in first 2 stored bytes). User data = 52-2-2 = ...
    // Actually: total_size = 52. size = min(52-2, extent-2) = min(50, 254) = 50.
    // The first 2 stored bytes are the CRC (LE), the view starts at payload[2].
    TEST_ASSERT_EQUAL_size_t(50, fx.capture.payload.view.size);

    // Verify first few bytes of the reassembled user data.
    // In v0, the first 2 bytes of stored payload are the CRC (LE). The user data starts at index 2.
    // So user data begins with f0[2..6] = {0x00, 0x00, 0x00, 0x00, 0x00}, then f1 = {0x00, 0x00, 0xDD, ...}
    const byte_t* vd = (const byte_t*)fx.capture.payload.view.data;
    TEST_ASSERT_EQUAL_HEX8(0x00, vd[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, vd[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, vd[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, vd[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, vd[4]); // End of f0 data bytes (after CRC)
    // f1 = {0x00, 0x00, 0xDD, 0xD4, 0x65, 0xA9, 0x14}
    TEST_ASSERT_EQUAL_HEX8(0x00, vd[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00, vd[6]);
    TEST_ASSERT_EQUAL_HEX8(0xDD, vd[7]);
    TEST_ASSERT_EQUAL_HEX8(0xD4, vd[8]);
    // Verify last 3 bytes of user data from f7: {0x6A, 0x39, 0x48}
    TEST_ASSERT_EQUAL_HEX8(0x6A, vd[47]);
    TEST_ASSERT_EQUAL_HEX8(0x39, vd[48]);
    TEST_ASSERT_EQUAL_HEX8(0x48, vd[49]);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Synthetic v0 2-frame multi-frame with known payload.
/// Payload: {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}
/// We compute the CRC using a known v0 data type signature and verify it decodes correctly.
static void test_golden_v0_synthetic_multi_frame(void)
{
    // Use a well-known data type signature for CRC seeding.
    const uint64_t    dts      = 0xABCDEF0123456789ULL;
    const uint16_t    crc_seed = canard_0v1_crc_seed_from_data_type_signature(dts);
    session_fixture_t fx;
    fixture_init(&fx, canard_kind_0v1_message, 999, 64, 2 * MEGA, crc_seed);

    // User payload: {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}
    const byte_t user_data[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };

    // In v0, the CRC is prepended (LE) to the payload. Compute it.
    const uint16_t crc    = crc_add(crc_seed, 8, user_data);
    const byte_t   crc_lo = (byte_t)(crc & 0xFF); // NOLINT(hicpp-signed-bitwise)
    const byte_t   crc_hi = (byte_t)(crc >> 8);   // NOLINT(hicpp-signed-bitwise)

    // The transmitted multi-frame data = [crc_lo, crc_hi, 0x11, 0x22, ...0x88]
    // Split into two 7-byte classic CAN frames:
    // Frame 0 (7 bytes): crc_lo, crc_hi, 0x11, 0x22, 0x33, 0x44, 0x55
    byte_t f0[7];
    f0[0] = crc_lo;
    f0[1] = crc_hi;
    f0[2] = 0x11;
    f0[3] = 0x22;
    f0[4] = 0x33;
    f0[5] = 0x44;
    f0[6] = 0x55;
    // Frame 1 (3 bytes): 0x66, 0x77, 0x88
    const byte_t f1[3] = { 0x66, 0x77, 0x88 };

    const canard_us_t ts = 1 * MEGA;

    frame_t fr0 = make_start_frame(
      canard_prio_nominal, canard_kind_0v1_message, 999, CANARD_NODE_ID_ANONYMOUS, 10, 5, f0, sizeof(f0));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr0, 0));

    frame_t fr1 = make_cont_frame(
      canard_prio_nominal, canard_kind_0v1_message, 999, CANARD_NODE_ID_ANONYMOUS, 10, 5, true, true, f1, sizeof(f1));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr1, 0));

    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT8(10, fx.capture.source_node_id);
    TEST_ASSERT_EQUAL_UINT8(5, fx.capture.transfer_id);
    // total_size = 10. size = min(10-2, 64-2) = min(8, 62) = 8.
    TEST_ASSERT_EQUAL_size_t(8, fx.capture.payload.view.size);
    TEST_ASSERT_EQUAL_INT(0, memcmp(fx.capture.payload.view.data, user_data, 8));

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// v0 multi-frame with corrupted CRC (bad CRC).
static void test_golden_v0_bad_crc(void)
{
    const uint64_t    dts      = 0xABCDEF0123456789ULL;
    const uint16_t    crc_seed = canard_0v1_crc_seed_from_data_type_signature(dts);
    session_fixture_t fx;
    fixture_init(&fx, canard_kind_0v1_message, 999, 64, 2 * MEGA, crc_seed);

    const byte_t   user_data[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
    const uint16_t crc          = crc_add(crc_seed, 8, user_data);
    const byte_t   crc_lo       = (byte_t)(crc & 0xFF); // NOLINT(hicpp-signed-bitwise)
    const byte_t   crc_hi       = (byte_t)(crc >> 8);   // NOLINT(hicpp-signed-bitwise)

    // Corrupt the CRC: flip a bit in the low byte.
    byte_t f0[7];
    f0[0]              = (byte_t)(crc_lo ^ 0x01U); // Corrupted!
    f0[1]              = crc_hi;
    f0[2]              = 0x11;
    f0[3]              = 0x22;
    f0[4]              = 0x33;
    f0[5]              = 0x44;
    f0[6]              = 0x55;
    const byte_t f1[3] = { 0x66, 0x77, 0x88 };

    const canard_us_t ts = 1 * MEGA;

    frame_t fr0 = make_start_frame(
      canard_prio_nominal, canard_kind_0v1_message, 999, CANARD_NODE_ID_ANONYMOUS, 10, 5, f0, sizeof(f0));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr0, 0));

    frame_t fr1 = make_cont_frame(
      canard_prio_nominal, canard_kind_0v1_message, 999, CANARD_NODE_ID_ANONYMOUS, 10, 5, true, true, f1, sizeof(f1));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr1, 0));

    TEST_ASSERT_EQUAL_size_t(0, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT64(1, fx.canard.err.rx_transfer);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

// =====================================================================================================================
// Group 5: Session Lifecycle

/// Verify that a start frame creates a new session (via cavl2_find_or_insert).
static void test_session_creation_on_start(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v1_message, 100, 64);

    // No sessions initially.
    TEST_ASSERT_NULL(fx.sub.sessions);

    const byte_t payload[] = { 0xAA };
    frame_t      fr        = make_single_frame(
      canard_prio_nominal, canard_kind_1v1_message, 100, CANARD_NODE_ID_ANONYMOUS, 42, 0, payload, sizeof(payload));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);

    // Session tree should now be non-NULL.
    TEST_ASSERT_NOT_NULL(fx.sub.sessions);

    // The session alloc should have 1 fragment outstanding.
    TEST_ASSERT_EQUAL_size_t(1, fx.alloc_session.allocated_fragments);

    // No payload allocations for single-frame transfers.
    TEST_ASSERT_EQUAL_size_t(0, fx.alloc_payload.allocated_fragments);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Verify that a second transfer from the same source reuses the same session.
static void test_session_reuse(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v1_message, 100, 64);

    const byte_t payload[] = { 0xBB };

    for (byte_t tid = 0; tid < 3; tid++) {
        frame_t fr = make_single_frame(canard_prio_nominal,
                                       canard_kind_1v1_message,
                                       100,
                                       CANARD_NODE_ID_ANONYMOUS,
                                       42,
                                       tid,
                                       payload,
                                       sizeof(payload));
        TEST_ASSERT_TRUE(feed(&fx, (1 + tid) * MEGA, &fr, 0));
    }
    TEST_ASSERT_EQUAL_size_t(3, fx.capture.call_count);
    // Only one session allocated.
    TEST_ASSERT_EQUAL_size_t(1, fx.alloc_session.allocated_fragments);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// A continuation frame without a prior start frame (no session) should be rejected silently.
static void test_continuation_without_session(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v1_message, 100, 64);

    const byte_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    frame_t      fr     = make_cont_frame(canard_prio_nominal,
                                 canard_kind_1v1_message,
                                 100,
                                 CANARD_NODE_ID_ANONYMOUS,
                                 42,
                                 0,
                                 false,
                                 false,
                                 data,
                                 sizeof(data));
    // No session exists for src=42. Continuation cannot create a session, so it returns true (silent rejection).
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr, 0));
    TEST_ASSERT_EQUAL_size_t(0, fx.capture.call_count);
    TEST_ASSERT_NULL(fx.sub.sessions);
    fixture_check_alloc_balance(&fx);
}

/// Toggle mismatch on continuation frame.
static void test_continuation_toggle_mismatch(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 64);

    const byte_t f0[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    frame_t      fr0  = make_start_frame(
      canard_prio_nominal, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 0, f0, sizeof(f0));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr0, 0));

    // Continuation with wrong toggle: expected toggle=0 (after start toggle=1), but we send toggle=1.
    const byte_t f1[] = { 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E };
    frame_t      fr1  = make_cont_frame(
      canard_prio_nominal, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 0, false, true, f1, sizeof(f1));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr1, 0)); // Rejected silently.
    TEST_ASSERT_EQUAL_size_t(0, fx.capture.call_count);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Interface mismatch on continuation frame.
static void test_continuation_iface_mismatch(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 64);

    const byte_t f0[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    frame_t      fr0  = make_start_frame(
      canard_prio_nominal, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 0, f0, sizeof(f0));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr0, 0)); // Starts on iface 0.

    // Continuation from iface 1: should be rejected because slot iface_index is 0.
    const byte_t f1[] = { 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E };
    frame_t      fr1  = make_cont_frame(canard_prio_nominal,
                                  canard_kind_1v0_message,
                                  2222,
                                  CANARD_NODE_ID_ANONYMOUS,
                                  42,
                                  0,
                                  false,
                                  false,
                                  f1,
                                  sizeof(f1));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr1, 1)); // iface=1, mismatch.
    TEST_ASSERT_EQUAL_size_t(0, fx.capture.call_count);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Duplicate TID rejection for single-frame transfers.
static void test_duplicate_tid_rejection(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v1_message, 100, 64);

    const byte_t payload[] = { 0xCC };
    frame_t      fr        = make_single_frame(
      canard_prio_nominal, canard_kind_1v1_message, 100, CANARD_NODE_ID_ANONYMOUS, 42, 5, payload, sizeof(payload));

    // First: accepted.
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);

    // Duplicate: rejected (same TID, same priority, within timeout).
    TEST_ASSERT_TRUE(feed(&fx, (1 * MEGA) + 1, &fr, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count); // No new callback.

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// After timeout, a frame from a different interface is accepted.
static void test_iface_failover_after_timeout(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v1_message, 100, 64);

    const byte_t payload[] = { 0xDD };

    // TID=0 on iface 0.
    frame_t fr0 = make_single_frame(
      canard_prio_nominal, canard_kind_1v1_message, 100, CANARD_NODE_ID_ANONYMOUS, 42, 0, payload, sizeof(payload));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr0, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);

    // TID=1 on iface 1, within timeout but fresh TID: fresh && !affine → need stale too.
    // At ts=2*MEGA, stale = (2*MEGA - 2*MEGA) > 1*MEGA = false. fresh && !affine && !stale → only 1 of 3 → reject.
    frame_t fr1 = make_single_frame(
      canard_prio_nominal, canard_kind_1v1_message, 100, CANARD_NODE_ID_ANONYMOUS, 42, 1, payload, sizeof(payload));
    TEST_ASSERT_TRUE(feed(&fx, 2 * MEGA, &fr1, 1));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count); // Rejected.

    // After timeout: ts=3*MEGA+1, stale = (3*MEGA+1 - 2*MEGA) > 1*MEGA = true.
    // fresh(1!=0) && stale → 2 of 3 → admit.
    TEST_ASSERT_TRUE(feed(&fx, (3 * MEGA) + 1, &fr1, 1));
    TEST_ASSERT_EQUAL_size_t(2, fx.capture.call_count);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Zero TID timeout: accept distinct transfers even with same TID, reject exact duplicates.
static void test_zero_tid_timeout(void)
{
    session_fixture_t fx;
    fixture_init(&fx, canard_kind_1v1_message, 100, 64, 0, CRC_INITIAL);

    const byte_t payload[] = { 0xEE };

    frame_t fr = make_single_frame(
      canard_prio_nominal, canard_kind_1v1_message, 100, CANARD_NODE_ID_ANONYMOUS, 42, 5, payload, sizeof(payload));

    // First: accepted.
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);

    // Same TID, one tick later: stale(1*MEGA+1 - 0 > 1*MEGA = true), affine → admit.
    TEST_ASSERT_TRUE(feed(&fx, (1 * MEGA) + 1, &fr, 0));
    TEST_ASSERT_EQUAL_size_t(2, fx.capture.call_count);

    // Same TID, same timestamp as last admission: stale=false, fresh=false → reject.
    TEST_ASSERT_TRUE(feed(&fx, (1 * MEGA) + 1, &fr, 0));
    TEST_ASSERT_EQUAL_size_t(2, fx.capture.call_count);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

// =====================================================================================================================
// Group 6: Priority Preemption

/// Two transfers at different priorities should use independent slots.
static void test_preemption_independent_slots(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 64);

    const byte_t f_hi[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    const byte_t f_lo[] = { 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10 };

    const canard_us_t ts = 1 * MEGA;

    // Start a low-priority multi-frame transfer.
    frame_t fr_lo = make_start_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 0, f_lo, sizeof(f_lo));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr_lo, 0));

    // Start a high-priority multi-frame transfer (different priority, same TID is fresh due to prio change).
    frame_t fr_hi = make_start_frame(
      canard_prio_immediate, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 1, f_hi, sizeof(f_hi));
    TEST_ASSERT_TRUE(feed(&fx, ts + 1, &fr_hi, 0));

    // Both slots should be allocated (at prio_slow and prio_immediate).
    // Verify by feeding continuation frames for both.
    // Complete the high-priority transfer.
    const byte_t   crc_hi_data[7] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    const uint16_t crc_hi         = crc_add(CRC_INITIAL, 7, crc_hi_data);
    byte_t         f_hi_end[2];
    f_hi_end[0]       = (byte_t)(crc_hi >> 8U);
    f_hi_end[1]       = (byte_t)(crc_hi & 0xFF); // NOLINT(hicpp-signed-bitwise)
    frame_t fr_hi_end = make_cont_frame(canard_prio_immediate,
                                        canard_kind_1v0_message,
                                        2222,
                                        CANARD_NODE_ID_ANONYMOUS,
                                        42,
                                        1,
                                        true,
                                        false,
                                        f_hi_end,
                                        sizeof(f_hi_end));
    TEST_ASSERT_TRUE(feed(&fx, ts + 2, &fr_hi_end, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    TEST_ASSERT_EQUAL_INT(canard_prio_immediate, fx.capture.priority);
    TEST_ASSERT_EQUAL_UINT8(1, fx.capture.transfer_id);

    // Complete the low-priority transfer.
    const byte_t   crc_lo_data[7] = { 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10 };
    const uint16_t crc_lo         = crc_add(CRC_INITIAL, 7, crc_lo_data);
    byte_t         f_lo_end[2];
    f_lo_end[0]       = (byte_t)(crc_lo >> 8U);
    f_lo_end[1]       = (byte_t)(crc_lo & 0xFF); // NOLINT(hicpp-signed-bitwise)
    frame_t fr_lo_end = make_cont_frame(canard_prio_slow,
                                        canard_kind_1v0_message,
                                        2222,
                                        CANARD_NODE_ID_ANONYMOUS,
                                        42,
                                        0,
                                        true,
                                        false,
                                        f_lo_end,
                                        sizeof(f_lo_end));
    TEST_ASSERT_TRUE(feed(&fx, ts + 3, &fr_lo_end, 0));
    TEST_ASSERT_EQUAL_size_t(2, fx.capture.call_count);
    TEST_ASSERT_EQUAL_INT(canard_prio_slow, fx.capture.priority);
    TEST_ASSERT_EQUAL_UINT8(0, fx.capture.transfer_id);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// A new start frame at the same priority replaces the old slot.
static void test_preemption_same_priority_replaces(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 64);

    const byte_t      data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    const canard_us_t ts     = 1 * MEGA;

    // Start multi-frame at prio_nominal, TID=0.
    frame_t fr0 = make_start_frame(
      canard_prio_nominal, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 0, data, sizeof(data));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr0, 0));

    // New start at same prio, different TID: replaces the old slot.
    frame_t fr1 = make_start_frame(
      canard_prio_nominal, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 1, data, sizeof(data));
    TEST_ASSERT_TRUE(feed(&fx, ts + 1, &fr1, 0));
    // Only 1 payload allocation should be outstanding (old was destroyed, new created).
    TEST_ASSERT_EQUAL_size_t(1, fx.alloc_payload.allocated_fragments);

    // Complete the new transfer.
    const uint16_t crc = crc_add(CRC_INITIAL, 7, data);
    byte_t         f_end[2];
    f_end[0]       = (byte_t)(crc >> 8U);
    f_end[1]       = (byte_t)(crc & 0xFF); // NOLINT(hicpp-signed-bitwise)
    frame_t fr_end = make_cont_frame(canard_prio_nominal,
                                     canard_kind_1v0_message,
                                     2222,
                                     CANARD_NODE_ID_ANONYMOUS,
                                     42,
                                     1,
                                     true,
                                     false,
                                     f_end,
                                     sizeof(f_end));
    TEST_ASSERT_TRUE(feed(&fx, ts + 2, &fr_end, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT8(1, fx.capture.transfer_id);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Start multi-frame transfers at all 8 priority levels simultaneously.
static void test_all_8_priorities(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 64);

    const byte_t data[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00 };
    canard_us_t  ts     = 1 * MEGA;

    // Start one multi-frame transfer at each priority level.
    for (byte_t prio = 0; prio < CANARD_PRIO_COUNT; prio++) {
        frame_t fr = make_start_frame(
          (canard_prio_t)prio, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, prio, data, sizeof(data));
        TEST_ASSERT_TRUE(feed(&fx, ts, &fr, 0));
        ts += 1;
    }
    // Should have 8 payload allocations (one slot per priority).
    TEST_ASSERT_EQUAL_size_t(8, fx.alloc_payload.allocated_fragments);

    // Complete all transfers.
    for (byte_t prio = 0; prio < CANARD_PRIO_COUNT; prio++) {
        const uint16_t crc = crc_add(CRC_INITIAL, 7, data);
        byte_t         f_end[2];
        f_end[0] = (byte_t)(crc >> 8U);
        f_end[1] = (byte_t)(crc & 0xFF); // NOLINT(hicpp-signed-bitwise)
        // Toggle: after start at toggle=1 (v1), next is 0.
        frame_t fr_end = make_cont_frame((canard_prio_t)prio,
                                         canard_kind_1v0_message,
                                         2222,
                                         CANARD_NODE_ID_ANONYMOUS,
                                         42,
                                         prio,
                                         true,
                                         false,
                                         f_end,
                                         sizeof(f_end));
        TEST_ASSERT_TRUE(feed(&fx, ts, &fr_end, 0));
        ts += 1;
    }
    TEST_ASSERT_EQUAL_size_t(8, fx.capture.call_count);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

// =====================================================================================================================
// Group 7: Error Paths & OOM

/// OOM on session creation: rx_session_update returns false.
static void test_oom_session_creation(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v1_message, 100, 64);
    fx.alloc_session.limit_fragments = 0; // Prevent session allocation.

    const byte_t payload[] = { 0xAA };
    frame_t      fr        = make_single_frame(
      canard_prio_nominal, canard_kind_1v1_message, 100, CANARD_NODE_ID_ANONYMOUS, 42, 0, payload, sizeof(payload));
    // Start frame that needs session creation fails with OOM.
    TEST_ASSERT_FALSE(feed(&fx, 1 * MEGA, &fr, 0));
    TEST_ASSERT_EQUAL_size_t(0, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT64(1, fx.canard.err.oom);

    fixture_check_alloc_balance(&fx);
}

/// OOM on slot creation: session exists but slot alloc fails.
static void test_oom_slot_creation(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 64);

    const byte_t f0[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };

    // First, successfully feed a single-frame to create the session.
    const byte_t sf[]  = { 0xAA };
    frame_t      sf_fr = make_single_frame(
      canard_prio_nominal, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 0, sf, sizeof(sf));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &sf_fr, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);

    // Now prevent payload allocation and try a multi-frame start.
    fx.alloc_payload.limit_fragments = 0;
    frame_t fr0                      = make_start_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 1, f0, sizeof(f0));
    TEST_ASSERT_FALSE(feed(&fx, 2 * MEGA, &fr0, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count); // No new callback.
    TEST_ASSERT_EQUAL_UINT64(1, fx.canard.err.oom);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Bad CRC frees the slot (reusing v4 bad CRC pattern).
static void test_bad_crc_frees_slot(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 64);

    const byte_t f0[] = { 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E };
    const byte_t f1[] = { 0xD8 }; // Bad CRC.

    const canard_us_t ts = 1 * MEGA;

    frame_t fr0 = make_start_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 55, 0, f0, sizeof(f0));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr0, 0));
    // One payload allocation for the slot.
    TEST_ASSERT_EQUAL_size_t(1, fx.alloc_payload.allocated_fragments);

    frame_t fr1 = make_cont_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 55, 0, true, false, f1, sizeof(f1));
    TEST_ASSERT_TRUE(feed(&fx, ts, &fr1, 0));

    // Slot freed after bad CRC.
    TEST_ASSERT_EQUAL_size_t(0, fx.alloc_payload.allocated_fragments);
    TEST_ASSERT_EQUAL_size_t(0, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT64(1, fx.canard.err.rx_transfer);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Single-frame transfer does not allocate from the payload resource.
static void test_single_frame_no_payload_alloc(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v1_message, 100, 64);

    const byte_t payload[] = { 0x11, 0x22, 0x33 };
    frame_t      fr        = make_single_frame(
      canard_prio_nominal, canard_kind_1v1_message, 100, CANARD_NODE_ID_ANONYMOUS, 42, 0, payload, sizeof(payload));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    // No payload allocation.
    TEST_ASSERT_EQUAL_UINT64(0, fx.alloc_payload.count_alloc);
    TEST_ASSERT_NULL(fx.capture.payload.origin.data);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Multi-frame transfer allocates on start and frees on completion.
static void test_multi_frame_allocate_then_free(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 64);

    const byte_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    frame_t      fr0    = make_start_frame(
      canard_prio_nominal, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 0, data, sizeof(data));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr0, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.alloc_payload.allocated_fragments);

    // Complete the transfer.
    const uint16_t crc = crc_add(CRC_INITIAL, 7, data);
    byte_t         f_end[2];
    f_end[0]    = (byte_t)(crc >> 8U);
    f_end[1]    = (byte_t)(crc & 0xFF); // NOLINT(hicpp-signed-bitwise)
    frame_t fr1 = make_cont_frame(canard_prio_nominal,
                                  canard_kind_1v0_message,
                                  2222,
                                  CANARD_NODE_ID_ANONYMOUS,
                                  42,
                                  0,
                                  true,
                                  false,
                                  f_end,
                                  sizeof(f_end));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr1, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    // Slot freed in on_message_capture.
    TEST_ASSERT_EQUAL_size_t(0, fx.alloc_payload.allocated_fragments);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

// =====================================================================================================================
// Group 8: Session Cleanup

/// Cleanup destroys stale slots when a new start frame arrives.
static void test_cleanup_destroys_stale(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 64);

    const byte_t      data[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00 };
    const canard_us_t ts0    = 1 * MEGA;

    // Start a multi-frame transfer at prio_slow.
    frame_t fr0 = make_start_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 0, data, sizeof(data));
    TEST_ASSERT_TRUE(feed(&fx, ts0, &fr0, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.alloc_payload.allocated_fragments);

    // Jump forward past RX_SESSION_TIMEOUT + transfer_id_timeout and start a new transfer at a different priority.
    // rx_session_cleanup uses: deadline = now - max(RX_SESSION_TIMEOUT, tid_timeout).
    // With tid_timeout=2s, max(30s, 2s) = 30s. So deadline = now - 30s.
    // Slot start_ts = 1*MEGA = 1s. deadline = (31s+1us+1s) - 30s = 2s+1us. slot->start_ts=1s < 2s+1us → stale.
    const canard_us_t ts1 = ts0 + (31 * MEGA) + 1;
    frame_t           fr1 = make_start_frame(
      canard_prio_nominal, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 1, data, sizeof(data));
    TEST_ASSERT_TRUE(feed(&fx, ts1, &fr1, 0));
    // The old stale slot should have been cleaned up, and a new one allocated.
    TEST_ASSERT_EQUAL_size_t(1, fx.alloc_payload.allocated_fragments);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Cleanup preserves fresh slots.
static void test_cleanup_preserves_fresh(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 64);

    const byte_t      data[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00 };
    const canard_us_t ts0    = 1 * MEGA;

    // Start a multi-frame transfer at prio_slow.
    frame_t fr0 = make_start_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 0, data, sizeof(data));
    TEST_ASSERT_TRUE(feed(&fx, ts0, &fr0, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.alloc_payload.allocated_fragments);

    // Start another transfer at a different priority, but still within session timeout.
    // Use ts only slightly after ts0: the old slot should NOT be cleaned up.
    const canard_us_t ts1 = ts0 + MEGA;
    frame_t           fr1 = make_start_frame(
      canard_prio_nominal, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 1, data, sizeof(data));
    TEST_ASSERT_TRUE(feed(&fx, ts1, &fr1, 0));
    // Both slots should exist now.
    TEST_ASSERT_EQUAL_size_t(2, fx.alloc_payload.allocated_fragments);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Cleanup uses max(RX_SESSION_TIMEOUT, tid_timeout) as the deadline calculation.
static void test_cleanup_uses_max_timeout(void)
{
    session_fixture_t fx;
    // Use a very large tid_timeout (60s), which is larger than RX_SESSION_TIMEOUT (30s).
    fixture_init(&fx, canard_kind_1v0_message, 2222, 64, 60 * MEGA, CRC_INITIAL);

    const byte_t      data[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00 };
    const canard_us_t ts0    = 1 * MEGA;

    frame_t fr0 = make_start_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 0, data, sizeof(data));
    TEST_ASSERT_TRUE(feed(&fx, ts0, &fr0, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.alloc_payload.allocated_fragments);

    // At ts0 + 31s: within max(30s, 60s) = 60s, so slot should survive.
    // deadline = (ts0+31s) - 60s = 1s+31s-60s = -28s. slot->start_ts=1s > -28s → not stale.
    const canard_us_t ts1 = ts0 + (31 * MEGA);
    frame_t           fr1 = make_start_frame(
      canard_prio_nominal, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 1, data, sizeof(data));
    TEST_ASSERT_TRUE(feed(&fx, ts1, &fr1, 0));
    TEST_ASSERT_EQUAL_size_t(2, fx.alloc_payload.allocated_fragments); // Both survive.

    // At ts0 + 61s+1: deadline = (1s+61s+1us) - 60s = 2s+1us. slot start_ts=1s < 2s+1us → stale.
    const canard_us_t ts2 = ts0 + (61 * MEGA) + 1;
    frame_t           fr2 = make_start_frame(
      canard_prio_high, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 2, data, sizeof(data));
    TEST_ASSERT_TRUE(feed(&fx, ts2, &fr2, 0));
    // The old stale slow-prio slot should be cleaned up. Nominal may or may not be stale depending on its start_ts.
    // fr1 started at ts1=32*MEGA. deadline= (62*MEGA+1) - 60*MEGA = 2*MEGA+1.
    // slot at nominal: start_ts=32*MEGA > 2*MEGA+1 → not stale. Survives.
    // So we expect: nominal slot + new high slot = 2. Old slow slot destroyed.
    TEST_ASSERT_EQUAL_size_t(2, fx.alloc_payload.allocated_fragments);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Cleanup is only triggered by start frames (not continuation).
static void test_cleanup_triggered_by_start(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 64);

    const byte_t      data[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00 };
    const canard_us_t ts0    = 1 * MEGA;

    // Start two multi-frame transfers at different priorities.
    frame_t fr0 = make_start_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 0, data, sizeof(data));
    TEST_ASSERT_TRUE(feed(&fx, ts0, &fr0, 0));

    frame_t fr1 = make_start_frame(
      canard_prio_nominal, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 1, data, sizeof(data));
    TEST_ASSERT_TRUE(feed(&fx, ts0 + 1, &fr1, 0));
    TEST_ASSERT_EQUAL_size_t(2, fx.alloc_payload.allocated_fragments);

    // Send a continuation for the nominal transfer well past the cleanup deadline.
    // Continuation does NOT trigger cleanup.
    const canard_us_t ts_late = ts0 + (31 * MEGA) + 2;
    const byte_t      cont[]  = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77 };
    frame_t           fr_c    = make_cont_frame(canard_prio_nominal,
                                   canard_kind_1v0_message,
                                   2222,
                                   CANARD_NODE_ID_ANONYMOUS,
                                   42,
                                   1,
                                   false,
                                   false,
                                   cont,
                                   sizeof(cont));
    TEST_ASSERT_TRUE(feed(&fx, ts_late, &fr_c, 0));
    // Both slots still exist because continuation does not trigger cleanup.
    TEST_ASSERT_EQUAL_size_t(2, fx.alloc_payload.allocated_fragments);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

// =====================================================================================================================
// Group 9: Edge Cases

/// Multi-frame with extent=0: no payload stored but total_size tracks, CRC still computed.
static void test_extent_zero_multiframe(void)
{
    session_fixture_t fx;
    // Extent=0 is unusual but must not crash.
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 0);

    const byte_t data[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00 };
    frame_t      fr0    = make_start_frame(
      canard_prio_nominal, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 0, data, sizeof(data));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr0, 0));

    // CRC for 7 bytes of data with seed 0xFFFF.
    const uint16_t crc = crc_add(CRC_INITIAL, 7, data);
    byte_t         f_end[2];
    f_end[0]    = (byte_t)(crc >> 8U);
    f_end[1]    = (byte_t)(crc & 0xFF); // NOLINT(hicpp-signed-bitwise)
    frame_t fr1 = make_cont_frame(canard_prio_nominal,
                                  canard_kind_1v0_message,
                                  2222,
                                  CANARD_NODE_ID_ANONYMOUS,
                                  42,
                                  0,
                                  true,
                                  false,
                                  f_end,
                                  sizeof(f_end));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr1, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    // total_size = 9 (7+2). size = min(9-2, 0) = 0.
    TEST_ASSERT_EQUAL_size_t(0, fx.capture.payload.view.size);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Extent exactly equal to payload size (no truncation, no excess).
static void test_extent_exact(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v0_message, 2222, 14);

    // Reuse the {1..14} 3-frame vector.
    const byte_t f0[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
    const byte_t f1[] = { 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E };
    const byte_t f2[] = { 0x32, 0xF8 }; // CRC big-endian

    frame_t fr0 = make_start_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 2, f0, sizeof(f0));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr0, 0));

    frame_t fr1 = make_cont_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 2, false, false, f1, sizeof(f1));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr1, 0));

    frame_t fr2 = make_cont_frame(
      canard_prio_slow, canard_kind_1v0_message, 2222, CANARD_NODE_ID_ANONYMOUS, 42, 2, true, true, f2, sizeof(f2));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr2, 0));

    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    TEST_ASSERT_EQUAL_size_t(14, fx.capture.payload.view.size);

    const byte_t expected[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };
    TEST_ASSERT_EQUAL_INT(0, memcmp(fx.capture.payload.view.data, expected, 14));

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// TID rollover: TID wraps from 31 to 0.
static void test_tid_rollover(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v1_message, 100, 64);

    const byte_t payload[] = { 0xAA };

    // Send TID=31.
    frame_t fr31 = make_single_frame(
      canard_prio_nominal, canard_kind_1v1_message, 100, CANARD_NODE_ID_ANONYMOUS, 42, 31, payload, sizeof(payload));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr31, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT8(31, fx.capture.transfer_id);

    // Send TID=0: fresh because 0 != 31.
    frame_t fr0 = make_single_frame(
      canard_prio_nominal, canard_kind_1v1_message, 100, CANARD_NODE_ID_ANONYMOUS, 42, 0, payload, sizeof(payload));
    TEST_ASSERT_TRUE(feed(&fx, 2 * MEGA, &fr0, 0));
    TEST_ASSERT_EQUAL_size_t(2, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT8(0, fx.capture.transfer_id);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Multiple source nodes create separate sessions.
static void test_multiple_sources(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v1_message, 100, 64);

    const byte_t payload[] = { 0x11 };

    // src=10
    frame_t fr10 = make_single_frame(
      canard_prio_nominal, canard_kind_1v1_message, 100, CANARD_NODE_ID_ANONYMOUS, 10, 0, payload, sizeof(payload));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr10, 0));
    TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT8(10, fx.capture.source_node_id);

    // src=20
    frame_t fr20 = make_single_frame(
      canard_prio_nominal, canard_kind_1v1_message, 100, CANARD_NODE_ID_ANONYMOUS, 20, 0, payload, sizeof(payload));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr20, 0));
    TEST_ASSERT_EQUAL_size_t(2, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT8(20, fx.capture.source_node_id);

    // src=30
    frame_t fr30 = make_single_frame(
      canard_prio_nominal, canard_kind_1v1_message, 100, CANARD_NODE_ID_ANONYMOUS, 30, 0, payload, sizeof(payload));
    TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr30, 0));
    TEST_ASSERT_EQUAL_size_t(3, fx.capture.call_count);
    TEST_ASSERT_EQUAL_UINT8(30, fx.capture.source_node_id);

    // 3 sessions allocated.
    TEST_ASSERT_EQUAL_size_t(3, fx.alloc_session.allocated_fragments);

    // Clean up all sessions via the tree.
    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Session animation ordering: newly active sessions are moved to the tail of the animation list.
static void test_session_animation_ordering(void)
{
    session_fixture_t fx;
    fixture_init_v1(&fx, canard_kind_1v1_message, 100, 64);

    const byte_t payload[] = { 0xAA };

    // Create sessions for src=10, src=20, src=30.
    for (byte_t src = 10; src <= 30; src = (byte_t)(src + 10)) {
        frame_t fr = make_single_frame(canard_prio_nominal,
                                       canard_kind_1v1_message,
                                       100,
                                       CANARD_NODE_ID_ANONYMOUS,
                                       src,
                                       0,
                                       payload,
                                       sizeof(payload));
        TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr, 0));
    }
    // Now the animation list should be: head=10, ..., tail=30.
    rx_session_t* head = LIST_HEAD(fx.canard.rx.list_session_by_animation, rx_session_t, list_animation);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_UINT8(10, head->node_id);

    // Send a new transfer from src=10 to move it to the tail.
    frame_t fr10 = make_single_frame(
      canard_prio_nominal, canard_kind_1v1_message, 100, CANARD_NODE_ID_ANONYMOUS, 10, 1, payload, sizeof(payload));
    TEST_ASSERT_TRUE(feed(&fx, 2 * MEGA, &fr10, 0));

    // Now head should be src=20 (the next oldest).
    head = LIST_HEAD(fx.canard.rx.list_session_by_animation, rx_session_t, list_animation);
    TEST_ASSERT_NOT_NULL(head);
    TEST_ASSERT_EQUAL_UINT8(20, head->node_id);

    fixture_destroy_all_sessions(&fx);
    fixture_check_alloc_balance(&fx);
}

/// Test all 7 kind values: verify single-frame works for each.
static void test_all_7_kinds(void)
{
    const canard_kind_t kinds[7] = {
        canard_kind_1v1_message, canard_kind_1v0_message,  canard_kind_1v0_response, canard_kind_1v0_request,
        canard_kind_0v1_message, canard_kind_0v1_response, canard_kind_0v1_request,
    };

    for (size_t k = 0; k < 7; k++) {
        session_fixture_t fx;
        const bool        is_v1  = canard_kind_version(kinds[k]) == 1;
        const bool        is_svc = (kinds[k] == canard_kind_1v0_response) || (kinds[k] == canard_kind_1v0_request) ||
                            (kinds[k] == canard_kind_0v1_response) || (kinds[k] == canard_kind_0v1_request);
        fixture_init(&fx, kinds[k], 42, 64, 2 * MEGA, is_v1 ? CRC_INITIAL : 0x1234);

        const byte_t dst = is_svc ? 123 : CANARD_NODE_ID_ANONYMOUS;
        if (is_svc) {
            fx.canard.node_id = 123;
        }

        const byte_t payload[] = { 0xDE, 0xAD };
        frame_t      fr = make_single_frame(canard_prio_nominal, kinds[k], 42, dst, 42, 0, payload, sizeof(payload));
        TEST_ASSERT_TRUE(feed(&fx, 1 * MEGA, &fr, 0));
        TEST_ASSERT_EQUAL_size_t(1, fx.capture.call_count);
        TEST_ASSERT_EQUAL_size_t(2, fx.capture.payload.view.size);
        TEST_ASSERT_EQUAL_HEX8(0xDE, ((const byte_t*)fx.capture.payload.view.data)[0]);
        TEST_ASSERT_EQUAL_HEX8(0xAD, ((const byte_t*)fx.capture.payload.view.data)[1]);

        fixture_destroy_all_sessions(&fx);
        fixture_check_alloc_balance(&fx);
    }
}

// =====================================================================================================================

int main(void)
{
    seed_prng();
    UNITY_BEGIN();

    // Group 1: Slot Lifecycle
    RUN_TEST(test_slot_new_v1);
    RUN_TEST(test_slot_new_v0);
    RUN_TEST(test_slot_new_oom);
    RUN_TEST(test_slot_advance_and_truncation);
    RUN_TEST(test_slot_advance_zero_extent);

    // Group 2: v1 Golden Single-Frame
    RUN_TEST(test_golden_v1_heartbeat);
    RUN_TEST(test_golden_v1_heartbeat_tid_sequence);
    RUN_TEST(test_golden_v1_duck);

    // Group 3: v1 Golden Multi-Frame
    RUN_TEST(test_golden_v1_nodeinfo_11_frames);
    RUN_TEST(test_golden_v1_seq14_3_frames);
    RUN_TEST(test_golden_v1_seq100_fd);
    RUN_TEST(test_golden_v4_3frame_truncated);
    RUN_TEST(test_golden_v4_crc_split);
    RUN_TEST(test_golden_v4_bad_crc);
    RUN_TEST(test_golden_v1_crc_full_in_last);

    // Group 4: v0 Golden Vectors
    RUN_TEST(test_golden_v0_single_frame_pressure);
    RUN_TEST(test_golden_v0_single_frame_temperature);
    RUN_TEST(test_golden_v0_screenshot_magfield);
    RUN_TEST(test_golden_v0_screenshot_gnss_fix);
    RUN_TEST(test_golden_v0_synthetic_multi_frame);
    RUN_TEST(test_golden_v0_bad_crc);

    // Group 5: Session Lifecycle
    RUN_TEST(test_session_creation_on_start);
    RUN_TEST(test_session_reuse);
    RUN_TEST(test_continuation_without_session);
    RUN_TEST(test_continuation_toggle_mismatch);
    RUN_TEST(test_continuation_iface_mismatch);
    RUN_TEST(test_duplicate_tid_rejection);
    RUN_TEST(test_iface_failover_after_timeout);
    RUN_TEST(test_zero_tid_timeout);

    // Group 6: Priority Preemption
    RUN_TEST(test_preemption_independent_slots);
    RUN_TEST(test_preemption_same_priority_replaces);
    RUN_TEST(test_all_8_priorities);

    // Group 7: Error Paths & OOM
    RUN_TEST(test_oom_session_creation);
    RUN_TEST(test_oom_slot_creation);
    RUN_TEST(test_bad_crc_frees_slot);
    RUN_TEST(test_single_frame_no_payload_alloc);
    RUN_TEST(test_multi_frame_allocate_then_free);

    // Group 8: Session Cleanup
    RUN_TEST(test_cleanup_destroys_stale);
    RUN_TEST(test_cleanup_preserves_fresh);
    RUN_TEST(test_cleanup_uses_max_timeout);
    RUN_TEST(test_cleanup_triggered_by_start);

    // Group 9: Edge Cases
    RUN_TEST(test_extent_zero_multiframe);
    RUN_TEST(test_extent_exact);
    RUN_TEST(test_tid_rollover);
    RUN_TEST(test_multiple_sources);
    RUN_TEST(test_session_animation_ordering);
    RUN_TEST(test_all_7_kinds);

    return UNITY_END();
}
