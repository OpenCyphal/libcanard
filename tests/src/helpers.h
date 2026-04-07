// This software is distributed under the terms of the MIT License.
// Copyright (c) Cyphal Development Team.

// ReSharper disable CppRedundantInlineSpecifier
// NOLINTBEGIN(*-unchecked-string-to-number-conversion,*-deprecated-headers,*-designated-initializers,*-loop-convert)
// NOLINTBEGIN(*DeprecatedOrUnsafeBufferHandling,*err34-c,*-vararg,*-use-auto,*-use-nullptr,*-redundant-void-arg)
// NOLINTBEGIN(*-cstyle-cast)
#pragma once

#include <canard.h> // Shall always be included first.
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#if !(defined(CANARD_VERSION_MAJOR) && defined(CANARD_VERSION_MINOR))
#error "Library version not defined"
#endif

#if !(defined(CANARD_CYPHAL_VERSION_MAJOR) && defined(CANARD_CYPHAL_VERSION_MINOR))
#error "Cyphal specification version not defined"
#endif

// This is only needed to tell static analyzers that the code that follows is not C++.
#ifdef __cplusplus
extern "C"
{
#endif

#ifdef __cplusplus
#define TEST_CAST(type, value) static_cast<type>(value)
#else
#define TEST_CAST(type, value) ((type)(value))
#endif

#define TEST_PANIC(message)                                                                 \
    do {                                                                                    \
        (void)fprintf(stderr, "%s:%u: PANIC: %s\n", __FILE__, (unsigned)__LINE__, message); \
        (void)fflush(stderr);                                                               \
        abort();                                                                            \
    } while (0)
#define TEST_PANIC_UNLESS(condition) \
    do {                             \
        if (!(condition)) {          \
            TEST_PANIC(#condition);  \
        }                            \
    } while (0)

static inline void* dummy_alloc(void* const user, const size_t size)
{
    (void)user;
    (void)size;
    return NULL;
}

static inline void dummy_free(void* const user, const size_t size, const void* const pointer)
{
    (void)user;
    (void)size;
    TEST_PANIC_UNLESS(pointer == NULL);
}

/// The instrumented allocator tracks memory consumption, checks for heap corruption, and can be configured to fail
/// allocations above a certain threshold.
#define INSTRUMENTED_ALLOCATOR_CANARY_SIZE 1024U
typedef struct
{
    /// Each allocator has its own canary, to catch an attempt to free memory allocated by a different allocator.
    uint_least8_t canary[INSTRUMENTED_ALLOCATOR_CANARY_SIZE];
    /// The limit can be changed at any moment to control the maximum amount of memory that can be allocated.
    /// It may be set to a value less than the currently allocated amount.
    size_t limit_fragments;
    size_t limit_bytes;
    /// The current state of the allocator.
    size_t allocated_fragments;
    size_t allocated_bytes;
    /// Event counters.
    uint64_t count_alloc;
    uint64_t count_free;
} instrumented_allocator_t;

static inline void* instrumented_allocator_alloc(const canard_mem_t mem, const size_t size)
{
    instrumented_allocator_t* const self   = TEST_CAST(instrumented_allocator_t*, mem.context);
    void*                           result = NULL; // NOLINT(*-const-correctness)
    self->count_alloc++;
    if ((size > 0U) &&                                           //
        ((self->allocated_bytes + size) <= self->limit_bytes) && //
        ((self->allocated_fragments + 1U) <= self->limit_fragments)) {
        const size_t size_with_canaries = size + ((size_t)INSTRUMENTED_ALLOCATOR_CANARY_SIZE * 2U);
        void*        origin             = malloc(size_with_canaries);
        TEST_PANIC_UNLESS(origin != NULL);
        *TEST_CAST(size_t*, origin) = size;
        uint_least8_t* p            = TEST_CAST(uint_least8_t*, origin) + sizeof(size_t); // NOLINT(*-const-correctness)
        result                      = TEST_CAST(uint_least8_t*, origin) + INSTRUMENTED_ALLOCATOR_CANARY_SIZE;
        for (size_t i = sizeof(size_t); i < INSTRUMENTED_ALLOCATOR_CANARY_SIZE; i++) // Fill the front canary.
        {
            *p++ = self->canary[i];
        }
        for (size_t i = 0; i < size; i++) // Randomize the allocated fragment.
        {
            *p++ = (uint_least8_t)(rand() % (UINT_LEAST8_MAX + 1));
        }
        for (size_t i = 0; i < INSTRUMENTED_ALLOCATOR_CANARY_SIZE; i++) // Fill the back canary.
        {
            *p++ = self->canary[i];
        }
        self->allocated_fragments++;
        self->allocated_bytes += size;
    }
    return result;
}

static inline void instrumented_allocator_free(const canard_mem_t mem, const size_t size, void* const pointer)
{
    instrumented_allocator_t* const self = TEST_CAST(instrumented_allocator_t*, mem.context);
    self->count_free++;
    if (pointer != NULL) { // NOLINTNEXTLINE(*-const-correctness)
        uint_least8_t* p         = TEST_CAST(uint_least8_t*, pointer) - INSTRUMENTED_ALLOCATOR_CANARY_SIZE;
        void* const    origin    = p;
        const size_t   true_size = *TEST_CAST(const size_t*, origin);
        TEST_PANIC_UNLESS(size == true_size);
        p += sizeof(size_t);
        for (size_t i = sizeof(size_t); i < INSTRUMENTED_ALLOCATOR_CANARY_SIZE; i++) // Check the front canary.
        {
            TEST_PANIC_UNLESS(*p++ == self->canary[i]);
        }
        for (size_t i = 0; i < size; i++) // Destroy the returned memory to prevent use-after-free.
        {
            *p++ = (uint_least8_t)(rand() % (UINT_LEAST8_MAX + 1));
        }
        for (size_t i = 0; i < INSTRUMENTED_ALLOCATOR_CANARY_SIZE; i++) // Check the back canary.
        {
            TEST_PANIC_UNLESS(*p++ == self->canary[i]);
        }
        free(origin);
        TEST_PANIC_UNLESS(self->allocated_fragments > 0U);
        self->allocated_fragments--;
        TEST_PANIC_UNLESS(self->allocated_bytes >= size);
        self->allocated_bytes -= size;
    }
}

static const canard_mem_vtable_t instrumented_allocator_vtable = { .free  = instrumented_allocator_free,
                                                                   .alloc = instrumented_allocator_alloc };

/// By default, the limit is unrestricted (set to the maximum possible value).
static inline void instrumented_allocator_new(instrumented_allocator_t* const self)
{
    for (size_t i = 0; i < INSTRUMENTED_ALLOCATOR_CANARY_SIZE; i++) {
        self->canary[i] = (uint_least8_t)(rand() % (UINT_LEAST8_MAX + 1));
    }
    self->limit_fragments     = SIZE_MAX;
    self->limit_bytes         = SIZE_MAX;
    self->allocated_fragments = 0U;
    self->allocated_bytes     = 0U;
    self->count_alloc         = 0U;
    self->count_free          = 0U;
}

/// Resets the counters and generates a new canary.
/// Will crash if there are outstanding allocations.
static inline void instrumented_allocator_reset(instrumented_allocator_t* const self)
{
    TEST_PANIC_UNLESS(self->allocated_fragments == 0U);
    TEST_PANIC_UNLESS(self->allocated_bytes == 0U);
    instrumented_allocator_new(self);
}

static inline canard_mem_t instrumented_allocator_make_resource(instrumented_allocator_t* const self)
{
    const canard_mem_t result = { &instrumented_allocator_vtable, self };
    return result;
}

static inline void seed_prng(void)
{
    unsigned          seed    = (unsigned)time(NULL);
    const char* const env_var = getenv("RANDOM_SEED");
    if (env_var != NULL) {
        seed = (unsigned)atoll(env_var); // Conversion errors are possible but ignored.
    }
    srand(seed);
    (void)fprintf(stderr, "export RANDOM_SEED=%u\n", seed);
}

#ifdef __cplusplus
}
#endif

#undef TEST_CAST

// NOLINTEND(*-cstyle-cast)
// NOLINTEND(*DeprecatedOrUnsafeBufferHandling,*err34-c,*-vararg,*-use-auto,*-use-nullptr,*-redundant-void-arg)
// NOLINTEND(*-unchecked-string-to-number-conversion,*-deprecated-headers,*-designated-initializers,*-loop-convert)
