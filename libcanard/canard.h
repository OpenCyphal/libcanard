///                            ____                   ______            __          __
///                           / __ `____  ___  ____  / ____/_  ______  / /_  ____  / /
///                          / / / / __ `/ _ `/ __ `/ /   / / / / __ `/ __ `/ __ `/ /
///                         / /_/ / /_/ /  __/ / / / /___/ /_/ / /_/ / / / / /_/ / /
///                         `____/ .___/`___/_/ /_/`____/`__, / .___/_/ /_/`__,_/_/
///                             /_/                     /____/_/
///
/// Libcanard is a compact implementation of the Cyphal/CAN transport for high-integrity real-time embedded systems.
/// It is designed for use in robust deterministic embedded systems equipped with at least 32K ROM and RAM.
/// The library is designed to be compatible with any target platform and instruction set architecture, from 8 to 64
/// bit, little- and big-endian, RTOS-based or baremetal, etc., as long as there is a standards-compliant C compiler.
///
/// The library is intended to be integrated into the end application by simply copying its source files into the
/// source tree of the project; it does not require any special compilation options and should work out of the box.
/// There are build-time configuration parameters defined near the top of canard.c, but they are safe to ignore.
///
/// The library is not thread-safe: if used in a concurrent environment, it is the responsibility of the application
/// to provide adequate synchronization.
///
/// --------------------------------------------------------------------------------------------------------------------
/// This software is distributed under the terms of the MIT License.
/// Copyright (c) OpenCyphal.
/// Author: Pavel Kirienko <pavel@opencyphal.org>
/// Contributors: https://github.com/OpenCyphal/libcanard/contributors.

#ifndef CANARD_H_INCLUDED
#define CANARD_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define CANARD_VERSION_MAJOR 5
#define CANARD_VERSION_MINOR 0

#define CANARD_CYPHAL_SPECIFICATION_VERSION_MAJOR 1
#define CANARD_CYPHAL_SPECIFICATION_VERSION_MINOR 1

typedef struct canard_tree_t
{
    struct canard_tree_t* up;
    struct canard_tree_t* lr[2];
    int_fast8_t           bf;
} canard_tree_t;

typedef struct canard_bytes_t
{
    size_t      size;
    const void* data;
} canard_bytes_t;

typedef struct canard_bytes_scattered_t
{
    canard_bytes_t                         bytes;
    const struct canard_bytes_scattered_t* next; ///< NULL in the last fragment.
} canard_bytes_scattered_t;

/// The semantics are similar to malloc/free.
/// Consider using O1Heap: https://github.com/pavel-kirienko/o1heap.
/// The API documentation is written on the assumption that the memory management functions are O(1).
/// The user pointer is taken from the corresponding field of the memory resource structure.
typedef void* (*canard_mem_alloc_t)(void* const user, const size_t size);
typedef void (*canard_mem_free_t)(void* const user, const size_t size, void* const pointer);

/// A kind of memory resource that can only be used to free memory previously allocated by the user.
typedef struct canard_mem_deleter_t
{
    void*             user;
    canard_mem_free_t free;
} canard_mem_deleter_t;

typedef struct canard_mem_resource_t
{
    void*              user;
    canard_mem_free_t  free;
    canard_mem_alloc_t alloc;
} canard_mem_resource_t;

/// The size can be changed arbitrarily. This value is compromise between copy size and footprint and utility.
#define CANARD_USER_CONTEXT_PTR_COUNT 6

/// The library carries the user-provided context from inputs to outputs without interpreting it,
/// allowing the application to associate its own data with various entities inside the library.
typedef union canard_user_context_t
{
    void*         ptr[CANARD_USER_CONTEXT_PTR_COUNT];
    unsigned char bytes[sizeof(void*) * CANARD_USER_CONTEXT_PTR_COUNT];
} canard_user_context_t;
#ifdef __cplusplus
#define CANARD_USER_CONTEXT_NULL \
    canard_user_context_t {}
#else
#define CANARD_USER_CONTEXT_NULL ((canard_user_context_t){ .ptr = { NULL } })
#endif

/// MTU values for the supported protocols.
/// Per the recommendations given in the Cyphal/CAN Specification, other MTU values should not be used.
#define CANARD_MTU_CAN_CLASSIC 8U
#define CANARD_MTU_CAN_FD      64U

/// Parameter ranges are inclusive; the lower bound is zero for all.
#define CANARD_SUBJECT_ID_MAX         8191U
#define CANARD_SERVICE_ID_MAX         511U
#define CANARD_NODE_ID_MAX            127U
#define CANARD_PRIORITY_MAX           7U
#define CANARD_TRANSFER_ID_BIT_LENGTH 5U
#define CANARD_TRANSFER_ID_MAX        ((1U << CANARD_TRANSFER_ID_BIT_LENGTH) - 1U)

/// This value represents an undefined node-ID: broadcast destination or anonymous source.
/// Library functions treat all values above CANARD_NODE_ID_MAX as anonymous.
#define CANARD_NODE_ID_UNSET 255U

/// This is the recommended transfer-ID timeout value given in the Cyphal Specification. The application may choose
/// different values per subscription (i.e., per data specifier) depending on its timing requirements.
#define CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC 2000000UL

/// Conversion look-up tables between CAN DLC and byte count. The reverse conversion rounds up.
extern const uint8_t canard_dlc_to_len[16];
extern const uint8_t canard_len_to_dlc[65];

#ifdef __cplusplus
}
#endif
#endif
