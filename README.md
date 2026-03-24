<div align="center">

# Cyphal/CAN transport in C

[![CI](https://github.com/OpenCyphal/libcanard/actions/workflows/main.yml/badge.svg)](https://github.com/OpenCyphal/libcanard/actions/workflows/main.yml)
[![Forum](https://img.shields.io/discourse/users.svg?server=https%3A%2F%2Fforum.opencyphal.org&color=1700b3)](https://forum.opencyphal.org)

</div>

-----

Libcanard is a robust implementation of the Cyphal/CAN transport layer in C for high-integrity real-time embedded systems.
Supports Cyphal v1.1, v1.0, and legacy UAVCAN v0 aka DroneCAN.

The library supports both Classic CAN and CAN FD, in redundant and non-redundant configurations.

The library is compatible with 8/16/32/64-bit platforms, including extremely resource-constrained
baremetal MCU platforms starting from 32K ROM and 32K RAM.

The interface with the underlying platform is very clean and minimal, enabling quick adaptation to any CAN-enabled MCU.
The OpenCyphal Development Team maintains a collection of various platform-specific components in a separate repository
at <https://github.com/OpenCyphal/platform_specific_components>.
Users are encouraged to search through that repository for drivers, examples, and other pieces that may be
reused in the target application to speed up the design of the media IO layer (driver) for the application.

[Cyphal](https://opencyphal.org) is an open lightweight data bus standard designed for reliable intravehicular
communication in aerospace and robotic applications via CAN bus, Ethernet, and other robust transports.

**Read the docs in [`libcanard/canard.h`](libcanard/canard.h).**

## Quick start

```c++
#include <assert.h>
#include <stdlib.h>
#include "canard.h"

// Return the current monotonic time in microseconds starting from some arbitrary instant in the past (e.g., boot).
static canard_us_t app_now(const canard_t* const self); 

// Embedded systems may prefer https://github.com/pavel-kirienko/o1heap.
static void* app_alloc(const canard_mem_t memory, const size_t size) { return malloc(size); }
static void app_free(const canard_mem_t memory, const size_t size, void* const pointer) { free(pointer); }

// Transmit a CAN frame non-blockingly; return true if submitted, false if would block (will try again later).
// Frame transmission may be aborted of it doesn't hit the bus before the deadline.
static bool app_tx(canard_t* const      self,
                   void* const          user_context,
                   const canard_us_t    deadline,
                   const uint_least8_t  iface_index,
                   const bool           fd,
                   const uint32_t       extended_can_id,
                   const canard_bytes_t can_data);

// Process a received message. The user may attach arbitrary context to the subscription via the user context pointer.
static void app_on_message(canard_subscription_t* const self,
                           const canard_us_t            timestamp,
                           const canard_prio_t          priority,
                           const uint_least8_t          source_node_id,
                           const uint_least8_t          transfer_id,
                           const canard_payload_t       payload)
{
    // payload.view contains the useful payload bytes.
    if (payload.origin.data != NULL) { // The application may keep the payload if necessary; eventually must release.
        free(payload.origin.data); 
    }
}

int main(void)
{
    static const canard_mem_vtable_t mem_vtable = { .free  = app_free, .alloc = app_alloc };
    const canard_mem_t mem = { .vtable  = &mem_vtable, .context = NULL };
    const canard_mem_set_t mem_set = { .tx_transfer = mem, .tx_frame = mem, .rx_session = mem, .rx_payload = mem };
    const canard_vtable_t vtable = { .now = app_now, .tx = app_tx };
    static const canard_subscription_vtable_t sub_vtable = { .on_message = app_on_message };
    
    // Set up the local node. The node-ID will be allocated automatically.
    canard_t              node;
    if (!canard_new(&node, &vtable, mem_set, 100, UID_OR_TRUE_RANDOM_NUMBER, 0U)) {
        return -1;
    }
    if (!canard_set_node_id(&node, 42U)) {
        canard_destroy(&node);
        return -1;
    }
    
    // Subscribe for messages.
    canard_subscription_t sub;
    if (!canard_subscribe_13b(&node, &sub, 7509U, 63U, CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_us, &sub_vtable)) {
        canard_destroy(&node);
        return -1;
    }

    // Publish a message.
    const canard_bytes_chain_t payload = { .bytes = {.size = 12, .data = "Hello world!"} };
    assert(canard_publish_13b(&node,
                              app_now(&node) + 1000000,
                              CANARD_IFACE_BITMAP_ALL,
                              canard_prio_nominal,
                              7509U,
                              0U,
                              payload,
                              NULL));

    canard_poll(&node, CANARD_IFACE_BITMAP_ALL); // Call again whenever one or more ifaces become writable.

    // Later, when a CAN frame is received:
    // static const uint8_t rx_frame_bytes[8] = { ... };
    // (void)canard_ingest_frame(&node,
    //                           app_now(&node),
    //                           0U,
    //                           rx_extended_can_id,
    //                           (canard_bytes_t){.size = sizeof(rx_frame_bytes), .data = rx_frame_bytes});

    canard_unsubscribe(&node, &sub);
    canard_destroy(&node);
    return 0;
}
```

## Revisions

To release a new version, simply publish a new release on GitHub.

### v5.0 [WORK IN PROGRESS]

v5 is a major rework based on the experience gained from extensive production use of the previous revisions.
The API has been redesigned from scratch and as such there is no migration guide available;
please read the new `canard.h` to see how to use the library -- the new API is simpler than the old one.

Main changes:

- Support for new protocols alongside Cyphal v1.0:
  - Cyphal/CAN v1.1, which adds support for 16-bit subject-IDs (like in UAVCAN v0) via a new CAN ID layout format.
  - UAVCAN v0 aka DroneCAN, a legacy predecessor to Cyphal v1.0 that is still widely used.

- Anonymous messages can no longer be transmitted, but they can still be received.

- A new passive node-ID autoconfiguration based on a simple occupancy observer.
  This method is decentralized and is compatible with old nodes.
  A node-ID can still be assigned manually if needed.

- Automatic CAN acceptance filter configuration based on the current subscription set.
  The configuration is refreshed whenever the subscription set is modified or the local node-ID is changed.

- New TX pipeline using per-transfer queue granularity with efficient CAN frame deduplication across redundant
  interfaces, which resulted in a major reduction of heap memory footprint (typ. x2+ reduction).

- New RX pipeline supporting priority level preemption without transfer loss and reduced memory consumption.
  The old revision was susceptible to transfer loss when the remote initiated a higher-priority multi-frame
  transfer while a lower-priority multi-frame transfer was in flight. The v5 revision maintains concurrent
  reassemblers per priority level, enabling arbitrary priority nesting.

### v4.0

Updating from Libcanard v3 to v4 involves several changes in memory management and TX frame expiration.
Please follow the `MIGRATION_v3.x_to_v4.0.md` guide available in the v4 codebase.

### v3.2

- Added new `canardRxGetSubscription`.

### v3.1

- Remove the Dockerfile; use [toolshed](https://github.com/OpenCyphal/docker_toolchains/pkgs/container/toolshed)
  instead if necessary.
- Simplify the transfer reassembly state machine to address [#212](https://github.com/OpenCyphal/libcanard/issues/212).
  See also <https://forum.opencyphal.org/t/amendment-to-the-transfer-reception-state-machine-implementations/1870>.

#### v3.1.1

- Refactor the transfer reassembly state machine to enhance its maintainability and robustness.

#### v3.1.2

- Allow redefinition of CANARD_ASSERT via the config header;
  see [#219](https://github.com/OpenCyphal/libcanard/pull/219).

### v3.0

- Update branding as [UAVCAN v1 is renamed to Cyphal](https://forum.opencyphal.org/t/uavcan-v1-is-now-cyphal/1622).
- Improve MISRA compliance by removing use of flex array: ([#192](https://github.com/OpenCyphal/libcanard/pull/192)).
- Fix dependency issues in docker toolchain.

  There are no API changes in this release aside from the rebranding/renaming:
  `CANARD_UAVCAN_SPECIFICATION_VERSION_MAJOR` -> `CANARD_CYPHAL_SPECIFICATION_VERSION_MAJOR`
  `CANARD_UAVCAN_SPECIFICATION_VERSION_MINOR` -> `CANARD_CYPHAL_SPECIFICATION_VERSION_MINOR`

#### v3.0.1

- Remove UB as described in [203](https://github.com/OpenCyphal/libcanard/issues/203).

#### v3.0.2

- Robustify the multi-frame transfer reassembler state machine
  ([#189](https://github.com/OpenCyphal/libcanard/issues/189)).
- Eliminate the risk of a header file name collision by renaming the vendored Cavl header to `_canard_cavl.h`
  ([#196](https://github.com/OpenCyphal/libcanard/issues/196)).

### v2.0

- Dedicated transmission queues per redundant CAN interface with depth limits.
  The application is now expected to instantiate `CanardTxQueue` (or several in case of redundant transport) manually.

- Replace O(n) linked lists with fast O(log n) AVL trees
  ([Cavl](https://github.com/pavel-kirienko/cavl) library is distributed with libcanard).
  Traversing the list of RX subscriptions now requires recursive traversal of the tree.

- Manual DSDL serialization helpers removed; use [Nunavut](https://github.com/OpenCyphal/nunavut) instead.

- Replace bitwise CRC computation with much faster static table by default
  ([#185](https://github.com/OpenCyphal/libcanard/issues/185)).
  This can be disabled by setting `CANARD_CRC_TABLE=0`, which is expected to save ca. 500 bytes of ROM.

- Fixed issues with const-correctness in the API ([#175](https://github.com/OpenCyphal/libcanard/issues/175)).

- `canardRxAccept2()` renamed to `canardRxAccept()`.

- Support build configuration headers via `CANARD_CONFIG_HEADER`.

- Add API for generating CAN hardware acceptance filter configurations
  ([#169](https://github.com/OpenCyphal/libcanard/issues/169)).

### v1.1

- Add new API function `canardRxAccept2()`, deprecate `canardRxAccept()`.
- Provide user references in `CanardRxSubscription`.
- Promote certain internal fields to the public API to allow introspection.

### v1.0

The initial release.
