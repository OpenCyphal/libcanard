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
    .on_p2p = NULL,
    .tx     = mock_tx,
    .filter = mock_filter,
};

static const canard_mem_vtable_t kStdMemVtable = {
    .free  = std_free_mem,
    .alloc = std_alloc_mem,
};

static canard_mem_set_t make_std_memory(void)
{
    const canard_mem_t r = { &kStdMemVtable, NULL };
    return canard_mem_set_t{ r, r, r, r };
}

// Basic constructor argument validation.
static void test_canard_new_validation(void)
{
    canard_t                  self    = {};
    canard_filter_t           filters = {};
    const canard_mem_set_t    mem     = make_std_memory();
    const canard_mem_vtable_t bad_mv  = { .free = std_free_mem, .alloc = NULL };
    const canard_mem_t        bad_mr  = { &bad_mv, NULL };
    const canard_mem_set_t    bad_mem = canard_mem_set_t{ bad_mr, bad_mr, bad_mr, bad_mr };

    TEST_ASSERT_FALSE(canard_new(NULL, &kTestVtable, mem, 16, 1, 1234, 0, NULL)); // Invalid self.
    TEST_ASSERT_FALSE(canard_new(&self, NULL, mem, 16, 1, 1234, 0, NULL));        // Invalid vtable.
    TEST_ASSERT_FALSE(
      canard_new(&self, &kTestVtable, mem, 16, CANARD_NODE_ID_MAX + 1U, 1234, 0, NULL)); // Invalid node-ID.
    TEST_ASSERT_FALSE(canard_new(&self, &kTestVtable, mem, 16, 1, 1234, 1, NULL));       // Missing filter storage.
    TEST_ASSERT_FALSE(canard_new(&self, &kTestVtable, bad_mem, 16, 1, 1234, 0, NULL));   // Invalid memory callbacks.
    TEST_ASSERT_TRUE(canard_new(&self, &kTestVtable, mem, 16, 1, 1234, 1, &filters));    // Valid constructor call.
    canard_destroy(&self);
}

// Constructor initializes state; destroy purges enqueued TX state.
static void test_canard_new_and_destroy(void)
{
    canard_t               self = {};
    const canard_mem_set_t mem  = make_std_memory();
    TEST_ASSERT_TRUE(canard_new(&self, &kTestVtable, mem, 16, 42, 0x0123456789ABCDEFULL, 0, NULL));
    TEST_ASSERT_EQUAL_UINT8(42U, (uint8_t)self.node_id);
    TEST_ASSERT_TRUE(self.tx.fd);
    TEST_ASSERT_EQUAL_size_t(16U, self.tx.queue_capacity);
    TEST_ASSERT_EQUAL_UINT64(0x0123456789ABCDEFULL, self.prng_state);

    // Queue one transfer and ensure destroy purges it.
    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    TEST_ASSERT_TRUE(canard_publish(&self, 1000, 1, canard_prio_nominal, 123, 0, payload, CANARD_USER_CONTEXT_NULL));
    TEST_ASSERT_NOT_NULL(self.tx.agewise.head);
    TEST_ASSERT_NOT_EQUAL_size_t(0U, self.tx.queue_size);

    canard_destroy(&self);
    TEST_ASSERT_NULL(self.tx.agewise.head);
    TEST_ASSERT_NULL(self.vtable);
    TEST_ASSERT_EQUAL_size_t(0U, self.tx.queue_size);
}

// Pending interface bitmap reports all interfaces that currently have pending TX.
static void test_canard_pending_ifaces(void)
{
    canard_t               self = {};
    const canard_mem_set_t mem  = make_std_memory();
    TEST_ASSERT_TRUE(canard_new(&self, &kTestVtable, mem, 16, 1, 1234, 0, NULL));

    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    TEST_ASSERT_EQUAL_UINT8(0U, canard_pending_ifaces(&self));
    TEST_ASSERT_TRUE(canard_publish(&self, 1000, 1U, canard_prio_nominal, 10U, 0U, payload, CANARD_USER_CONTEXT_NULL));
    TEST_ASSERT_TRUE(canard_publish(&self, 1000, 2U, canard_prio_nominal, 11U, 1U, payload, CANARD_USER_CONTEXT_NULL));
    TEST_ASSERT_EQUAL_UINT8(3U, canard_pending_ifaces(&self));

    canard_destroy(&self);
}

// Golden signatures generated from pydronecan (dronecan 1.0.24), standard uavcan.* types.
static void test_canard_0v1_crc_seed_from_data_type_signature_golden(void)
{
    struct test_vector_t
    {
        uint64_t signature;
        uint16_t expected_seed;
    };
    static const test_vector_t vectors[] = {
        { 0xD8A7486238EC3AF3ULL, 0x5E37U }, { 0x5E9BBA44FAF1EA04ULL, 0x9A63U }, { 0xE2A7D4A9460BC2F2ULL, 0x037CU },
        { 0xB6AC0C442430297EULL, 0xBA25U }, { 0x8280632C40E574B5ULL, 0x4E3EU }, { 0x72A63A3C6F41FA9BULL, 0x2055U },
        { 0xD5513C3F7AFAC74EULL, 0x94ADU }, { 0x0A1892D72AB8945FULL, 0x02CFU }, { 0xC77DF38BA122F5DAULL, 0x506EU },
        { 0x7B48E55FCFF42A57ULL, 0xC02BU }, { 0xCDC7C43412BDC89AULL, 0xEBF4U }, { 0x49272A6477D96271ULL, 0x17A4U },
        { 0x306F69E0A591AFAAULL, 0x472DU }, { 0x4AF6E57B2B2BE29CULL, 0xCD1FU }, { 0x9371428A92F01FD6ULL, 0x87DBU },
        { 0xB9F127865BE0D61EULL, 0xEFD0U }, { 0x70261C28A94144C6ULL, 0x5D7EU }, { 0xCE0F9F621CF7E70BULL, 0xF8B4U },
        { 0x217F5C87D7EC951DULL, 0xE4B8U }, { 0xA9AF28AEA2FBB254ULL, 0x1591U }, { 0x9BE8BDC4C3DBBFD2ULL, 0x24AEU },
        { 0x54C1572B9E07F297ULL, 0x9510U }, { 0xCA41E7000F37435FULL, 0xC798U }, { 0x1F56030ECB171501ULL, 0x897EU },
        { 0xA1A036268B0C3455ULL, 0x3004U }, { 0x624A519D42553D82ULL, 0xAEE4U }, { 0x286B4A387BA84BC4ULL, 0xBF2EU },
        { 0xD38AA3EE75537EC6ULL, 0x41D9U }, { 0xBE9EA9FEC2B15D52ULL, 0xDFBDU }, { 0x2031D93C8BDD1EC4ULL, 0x9E31U },
        { 0x249C26548A711966ULL, 0xF674U }, { 0x8313D33D0DDDA115ULL, 0x2CB7U }, { 0xBBA05074AD757480ULL, 0x591FU },
        { 0x68FFFE70FC771952ULL, 0x049CU }, { 0x8700F375556A8003ULL, 0x1890U }, { 0x463B10CCCBE51C3DULL, 0x1D70U },
    };
    for (size_t i = 0; i < (sizeof(vectors) / sizeof(vectors[0])); i++) {
        TEST_ASSERT_EQUAL_HEX16(vectors[i].expected_seed,
                                canard_0v1_crc_seed_from_data_type_signature(vectors[i].signature));
    }
}

static void test_canard_publish_validation(void)
{
    canard_t self = {};

    // Invalid interface bitmap.
    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    TEST_ASSERT_FALSE(canard_publish(&self, 0, 0, canard_prio_nominal, 0, 0, payload, CANARD_USER_CONTEXT_NULL));

    // Invalid payload.
    const canard_bytes_chain_t bad_payload = { .bytes = { .size = 1, .data = NULL }, .next = NULL };
    TEST_ASSERT_FALSE(canard_publish(&self, 0, 1, canard_prio_nominal, 0, 0, bad_payload, CANARD_USER_CONTEXT_NULL));
}

static void test_canard_publish_oom(void)
{
    canard_mem_vtable_t vtable = {};
    vtable.free                = dummy_free_mem;
    vtable.alloc               = dummy_alloc_mem;
    canard_t self              = {};
    self.mem.tx_transfer       = canard_mem_t{ &vtable, NULL };
    self.mem.tx_frame          = canard_mem_t{ &vtable, NULL };

    // Allocation failure in txfer_new should return false.
    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    TEST_ASSERT_FALSE(canard_publish(&self, 0, 1, canard_prio_nominal, 0, 0, payload, CANARD_USER_CONTEXT_NULL));
}

static void test_canard_0v1_publish_requires_node_id(void)
{
    canard_t self = {};

    // Node-ID zero should reject the request.
    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
    TEST_ASSERT_FALSE(
      canard_0v1_publish(&self, 0, 1, canard_prio_nominal, 1, 0xFFFF, 0, payload, CANARD_USER_CONTEXT_NULL));
}

static void test_canard_publish_subject_id_out_of_range(void)
{
    const canard_bytes_chain_t payload = { .bytes = { .size = 0, .data = NULL }, .next = NULL };
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
