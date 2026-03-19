// This software is distributed under the terms of the MIT License.
// Copyright (c) OpenCyphal Development Team.

#include "helpers.h"
#include <unity.h>
#include <cstdint>
#include <cstdlib>
#include <memory>

// Simple allocator that always fails.
static void* dummy_alloc_mem(const canard_mem_t mem, const size_t size) { return dummy_alloc(mem.context, size); }
static void  dummy_free_mem(const canard_mem_t mem, const size_t size, void* const pointer)
{
    dummy_free(mem.context, size, pointer);
}

// Heap-backed allocator for constructor/API tests.
static void* std_alloc_mem(const canard_mem_t, const size_t size) { return std::malloc(size); }
static void  std_free_mem(const canard_mem_t, const size_t, void* const pointer) { std::free(pointer); }

// Minimal callbacks for canard_new().
static canard_us_t mock_now(canard_t* const) { return 0; }
static bool        mock_tx(canard_t* const,
                           const canard_user_context_t,
                           const canard_us_t,
                           const uint_least8_t,
                           const bool,
                           const uint32_t,
                           const canard_bytes_t)
{
    return false;
}
static bool mock_filter(canard_t* const, const size_t, const canard_filter_t*) { return true; }

// Shared vtable and memory resources used by canard_new() tests.
static const canard_vtable_t kTestVtable = {
    .now    = mock_now,
    .on_p2p = nullptr,
    .tx     = mock_tx,
    .filter = mock_filter,
};

static const canard_mem_vtable_t kStdMemVtable = {
    .free  = std_free_mem,
    .alloc = std_alloc_mem,
};

static canard_mem_set_t make_std_memory()
{
    const canard_mem_t r = { .vtable = &kStdMemVtable, .context = nullptr };
    return canard_mem_set_t{
        .tx_transfer = r,
        .tx_frame    = r,
        .rx_session  = r,
        .rx_payload  = r,
    };
}

// Basic constructor argument validation.
static void test_canard_new_validation()
{
    canard_t                  self    = {};
    canard_filter_t           filters = {};
    const canard_mem_set_t    mem     = make_std_memory();
    const canard_mem_vtable_t bad_mv  = { .free = std_free_mem, .alloc = nullptr };
    const canard_mem_t        bad_mr  = { .vtable = &bad_mv, .context = nullptr };
    const canard_mem_set_t    bad_mem = canard_mem_set_t{
           .tx_transfer = bad_mr,
           .tx_frame    = bad_mr,
           .rx_session  = bad_mr,
           .rx_payload  = bad_mr,
    };

    TEST_ASSERT_FALSE(canard_new(nullptr, &kTestVtable, mem, 16, 1, 1234, 0, nullptr)); // Invalid self.
    TEST_ASSERT_FALSE(canard_new(&self, nullptr, mem, 16, 1, 1234, 0, nullptr));        // Invalid vtable.
    TEST_ASSERT_FALSE(
      canard_new(&self, &kTestVtable, mem, 16, CANARD_NODE_ID_MAX + 1U, 1234, 0, nullptr)); // Invalid node-ID.
    TEST_ASSERT_FALSE(canard_new(&self, &kTestVtable, mem, 16, 1, 1234, 1, nullptr));       // Missing filter storage.
    TEST_ASSERT_FALSE(canard_new(&self, &kTestVtable, bad_mem, 16, 1, 1234, 0, nullptr));   // Invalid memory callbacks.
    TEST_ASSERT_TRUE(canard_new(&self, &kTestVtable, mem, 16, 1, 1234, 1, &filters));       // Valid constructor call.
    canard_destroy(&self);
}

// Constructor initializes state; destroy purges enqueued TX state.
static void test_canard_new_and_destroy()
{
    canard_t               self = {};
    const canard_mem_set_t mem  = make_std_memory();
    TEST_ASSERT_TRUE(canard_new(&self, &kTestVtable, mem, 16, 42, 0x0123456789ABCDEFULL, 0, nullptr));
    TEST_ASSERT_EQUAL_UINT8(42U, (uint8_t)self.node_id);
    TEST_ASSERT_TRUE(self.tx.fd);
    TEST_ASSERT_EQUAL_size_t(16U, self.tx.queue_capacity);
    TEST_ASSERT_EQUAL_UINT64(0x0123456789ABCDEFULL, self.prng_state);

    // Queue one transfer and ensure destroy purges it.
    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = nullptr }, .next = nullptr };
    TEST_ASSERT_TRUE(canard_publish(&self, 1000, 1, canard_prio_nominal, 123, 0, payload, CANARD_USER_CONTEXT_NULL));
    TEST_ASSERT_NOT_NULL(self.tx.agewise.head);
    TEST_ASSERT_NOT_EQUAL_size_t(0U, self.tx.queue_size);

    canard_destroy(&self);
    TEST_ASSERT_NULL(self.tx.agewise.head);
    TEST_ASSERT_NULL(self.vtable);
    TEST_ASSERT_EQUAL_size_t(0U, self.tx.queue_size);
}

// Pending interface bitmap reports all interfaces that currently have pending TX.
static void test_canard_pending_ifaces()
{
    canard_t               self = {};
    const canard_mem_set_t mem  = make_std_memory();
    TEST_ASSERT_TRUE(canard_new(&self, &kTestVtable, mem, 16, 1, 1234, 0, nullptr));

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = nullptr }, .next = nullptr };
    TEST_ASSERT_EQUAL_UINT8(0U, canard_pending_ifaces(&self));
    TEST_ASSERT_TRUE(canard_publish(&self, 1000, 1U, canard_prio_nominal, 10U, 0U, payload, CANARD_USER_CONTEXT_NULL));
    TEST_ASSERT_TRUE(canard_publish(&self, 1000, 2U, canard_prio_nominal, 11U, 1U, payload, CANARD_USER_CONTEXT_NULL));
    TEST_ASSERT_EQUAL_UINT8(3U, canard_pending_ifaces(&self));

    canard_destroy(&self);
}

// Golden signatures generated from pydronecan (dronecan 1.0.24), standard uavcan.* types.
static void test_canard_0v1_crc_seed_from_data_type_signature_golden()
{
    struct test_vector_t
    {
        uint64_t signature;
        uint16_t expected_seed;
    };
    static const test_vector_t vectors[] = {
        { .signature = 0xD8A7486238EC3AF3ULL, .expected_seed = 0x5E37U },
        { .signature = 0x5E9BBA44FAF1EA04ULL, .expected_seed = 0x9A63U },
        { .signature = 0xE2A7D4A9460BC2F2ULL, .expected_seed = 0x037CU },
        { .signature = 0xB6AC0C442430297EULL, .expected_seed = 0xBA25U },
        { .signature = 0x8280632C40E574B5ULL, .expected_seed = 0x4E3EU },
        { .signature = 0x72A63A3C6F41FA9BULL, .expected_seed = 0x2055U },
        { .signature = 0xD5513C3F7AFAC74EULL, .expected_seed = 0x94ADU },
        { .signature = 0x0A1892D72AB8945FULL, .expected_seed = 0x02CFU },
        { .signature = 0xC77DF38BA122F5DAULL, .expected_seed = 0x506EU },
        { .signature = 0x7B48E55FCFF42A57ULL, .expected_seed = 0xC02BU },
        { .signature = 0xCDC7C43412BDC89AULL, .expected_seed = 0xEBF4U },
        { .signature = 0x49272A6477D96271ULL, .expected_seed = 0x17A4U },
        { .signature = 0x306F69E0A591AFAAULL, .expected_seed = 0x472DU },
        { .signature = 0x4AF6E57B2B2BE29CULL, .expected_seed = 0xCD1FU },
        { .signature = 0x9371428A92F01FD6ULL, .expected_seed = 0x87DBU },
        { .signature = 0xB9F127865BE0D61EULL, .expected_seed = 0xEFD0U },
        { .signature = 0x70261C28A94144C6ULL, .expected_seed = 0x5D7EU },
        { .signature = 0xCE0F9F621CF7E70BULL, .expected_seed = 0xF8B4U },
        { .signature = 0x217F5C87D7EC951DULL, .expected_seed = 0xE4B8U },
        { .signature = 0xA9AF28AEA2FBB254ULL, .expected_seed = 0x1591U },
        { .signature = 0x9BE8BDC4C3DBBFD2ULL, .expected_seed = 0x24AEU },
        { .signature = 0x54C1572B9E07F297ULL, .expected_seed = 0x9510U },
        { .signature = 0xCA41E7000F37435FULL, .expected_seed = 0xC798U },
        { .signature = 0x1F56030ECB171501ULL, .expected_seed = 0x897EU },
        { .signature = 0xA1A036268B0C3455ULL, .expected_seed = 0x3004U },
        { .signature = 0x624A519D42553D82ULL, .expected_seed = 0xAEE4U },
        { .signature = 0x286B4A387BA84BC4ULL, .expected_seed = 0xBF2EU },
        { .signature = 0xD38AA3EE75537EC6ULL, .expected_seed = 0x41D9U },
        { .signature = 0xBE9EA9FEC2B15D52ULL, .expected_seed = 0xDFBDU },
        { .signature = 0x2031D93C8BDD1EC4ULL, .expected_seed = 0x9E31U },
        { .signature = 0x249C26548A711966ULL, .expected_seed = 0xF674U },
        { .signature = 0x8313D33D0DDDA115ULL, .expected_seed = 0x2CB7U },
        { .signature = 0xBBA05074AD757480ULL, .expected_seed = 0x591FU },
        { .signature = 0x68FFFE70FC771952ULL, .expected_seed = 0x049CU },
        { .signature = 0x8700F375556A8003ULL, .expected_seed = 0x1890U },
        { .signature = 0x463B10CCCBE51C3DULL, .expected_seed = 0x1D70U },
    };
    for (const test_vector_t& vector : vectors) {
        TEST_ASSERT_EQUAL_HEX16(vector.expected_seed, canard_0v1_crc_seed_from_data_type_signature(vector.signature));
    }
}

static void test_canard_publish_validation()
{
    canard_t self = {};

    // Invalid interface bitmap.
    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = nullptr }, .next = nullptr };
    TEST_ASSERT_FALSE(canard_publish(&self, 0, 0, canard_prio_nominal, 0, 0, payload, CANARD_USER_CONTEXT_NULL));

    // Invalid payload.
    const canard_bytes_chain_t bad_payload = { .bytes = { .size = 1, .data = nullptr }, .next = nullptr };
    TEST_ASSERT_FALSE(canard_publish(&self, 0, 1, canard_prio_nominal, 0, 0, bad_payload, CANARD_USER_CONTEXT_NULL));
}

static void test_canard_publish_oom()
{
    canard_mem_vtable_t vtable = {};
    vtable.free                = dummy_free_mem;
    vtable.alloc               = dummy_alloc_mem;
    canard_t self              = {};
    self.mem.tx_transfer       = canard_mem_t{ .vtable = &vtable, .context = nullptr };
    self.mem.tx_frame          = canard_mem_t{ .vtable = &vtable, .context = nullptr };

    // Allocation failure in txfer_new should return false.
    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = nullptr }, .next = nullptr };
    TEST_ASSERT_FALSE(canard_publish(&self, 0, 1, canard_prio_nominal, 0, 0, payload, CANARD_USER_CONTEXT_NULL));
}

static void test_canard_0v1_publish_requires_node_id()
{
    canard_t self = {};

    // Node-ID zero should reject the request.
    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = nullptr }, .next = nullptr };
    TEST_ASSERT_FALSE(
      canard_0v1_publish(&self, 0, 1, canard_prio_nominal, 1, 0xFFFF, 0, payload, CANARD_USER_CONTEXT_NULL));
}

static void test_canard_publish_subject_id_out_of_range()
{
    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = nullptr }, .next = nullptr };
    canard_t                   self    = {};
    TEST_ASSERT_FALSE(canard_publish(
      &self, 0, 1, canard_prio_nominal, CANARD_SUBJECT_ID_MAX + 1U, 0, payload, CANARD_USER_CONTEXT_NULL));
}

extern "C" void setUp() {}
extern "C" void tearDown() {}

int main()
{
    UNITY_BEGIN();

    // Constructor and utility API coverage.
    RUN_TEST(test_canard_new_validation);
    RUN_TEST(test_canard_new_and_destroy);
    RUN_TEST(test_canard_pending_ifaces);
    RUN_TEST(test_canard_0v1_crc_seed_from_data_type_signature_golden);

    // TX API validation checks.
    RUN_TEST(test_canard_publish_validation);
    RUN_TEST(test_canard_publish_oom);
    RUN_TEST(test_canard_0v1_publish_requires_node_id);
    RUN_TEST(test_canard_publish_subject_id_out_of_range);

    return UNITY_END();
}
