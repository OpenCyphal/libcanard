# Compact Cyphal/CAN in C

[![Main Workflow](https://github.com/OpenCyphal/libcanard/actions/workflows/main.yml/badge.svg)](https://github.com/OpenCyphal/libcanard/actions/workflows/main.yml)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=libcanard&metric=alert_status)](https://sonarcloud.io/dashboard?id=libcanard)
[![Reliability Rating](https://sonarcloud.io/api/project_badges/measure?project=libcanard&metric=reliability_rating)](https://sonarcloud.io/dashboard?id=libcanard)
[![Coverage](https://sonarcloud.io/api/project_badges/measure?project=libcanard&metric=coverage)](https://sonarcloud.io/dashboard?id=libcanard)
[![Lines of Code](https://sonarcloud.io/api/project_badges/measure?project=libcanard&metric=ncloc)](https://sonarcloud.io/dashboard?id=libcanard)
[![Forum](https://img.shields.io/discourse/users.svg?server=https%3A%2F%2Fforum.opencyphal.org&color=1700b3)](https://forum.opencyphal.org)

Libcanard is a compact implementation of the Cyphal/CAN protocol stack in C99/C11 for high-integrity real-time
embedded systems.

[Cyphal](https://opencyphal.org) is an open lightweight data bus standard designed for reliable intravehicular
communication in aerospace and robotic applications via CAN bus, Ethernet, and other robust transports.

**Read the docs in [`libcanard/canard.h`](/libcanard/canard.h).**

Find examples, starters, tutorials on the
[Cyphal forum](https://forum.opencyphal.org/t/libcanard-examples-starters-tutorials/935).

If you want to contribute, please read [`CONTRIBUTING.md`](/CONTRIBUTING.md).

## Features

- Full test coverage and extensive static analysis.
- Compliance with automatically enforceable MISRA C rules (reach out to <https://forum.opencyphal.org> for details).
- Detailed time complexity and memory requirement models for the benefit of real-time high-integrity applications.
- Purely reactive API without the need for background servicing.
- Support for the Classic CAN and CAN FD.
- Support for redundant transports.
- Compatibility with 8/16/32/64-bit platforms.
- Compatibility with extremely resource-constrained baremetal environments starting from 32K ROM and 8K RAM.
- Implemented in ≈1000 lines of code.

## Platforms

The library is designed to be usable out of the box with any conventional 8/16/32/64-bit platform,
including deeply embedded baremetal platforms, as long as there is a standard-compliant compiler available.
The platform-specific media IO layer (driver) is supposed to be provided by the application:

    +---------------------------------+
    |           Application           |
    +-------+-----------------+-------+
            |                 |
    +-------+-------+ +-------+-------+
    |   Libcanard   | |  Media layer  |
    +---------------+ +-------+-------+
                              |
                      +-------+-------+
                      |    Hardware   |
                      +---------------+

The OpenCyphal Development Team maintains a collection of various platform-specific components in a separate repository
at <https://github.com/OpenCyphal/platform_specific_components>.
Users are encouraged to search through that repository for drivers, examples, and other pieces that may be
reused in the target application to speed up the design of the media IO layer (driver) for the application.

## Example

The example augments the documentation but does not replace it.

The library requires a constant-complexity deterministic dynamic memory allocator.
We could use the standard C heap, but most implementations are not constant-complexity,
so let's suppose that we're using [O1Heap](https://github.com/pavel-kirienko/o1heap) instead.
We are going to need basic wrappers:

```c
static void* memAllocate(CanardInstance* const canard, const size_t amount)
{
    (void) canard;
    return o1heapAllocate(my_allocator, amount);
}

static void memFree(CanardInstance* const canard, void* const pointer)
{
    (void) canard;
    o1heapFree(my_allocator, pointer);
}
```

Init a library instance:

```c
CanardInstance canard = canardInit(&memAllocate, &memFree);
canard.node_id = 42;                        // Defaults to anonymous; can be set up later at any point.
```

In order to be able to send transfers over the network, we will need one transmission queue per redundant CAN interface:

```c
CanardTxQueue queue = canardTxInit(100,                 // Limit the size of the queue at 100 frames.
                                   CANARD_MTU_CAN_FD);  // Set MTU = 64 bytes. There is also CANARD_MTU_CAN_CLASSIC.
```

Publish a message (message serialization not shown):

```c
static uint8_t my_message_transfer_id;  // Must be static or heap-allocated to retain state between calls.
const CanardTransferMetadata transfer_metadata = {
    .priority       = CanardPriorityNominal,
    .transfer_kind  = CanardTransferKindMessage,
    .port_id        = 1234,                       // This is the subject-ID.
    .remote_node_id = CANARD_NODE_ID_UNSET,       // Messages cannot be unicast, so use UNSET.
    .transfer_id    = my_message_transfer_id,
};
++my_message_transfer_id;  // The transfer-ID shall be incremented after every transmission on this subject.
int32_t result = canardTxPush(&queue,               // Call this once per redundant CAN interface (queue).
                              &canard,
                              tx_deadline_usec,     // Zero if transmission deadline is not limited.
                              &transfer_metadata,
                              47,                   // Size of the message payload (see Nunavut transpiler).
                              "\x2D\x00" "Sancho, it strikes me thou art in great fear.");
if (result < 0)
{
    // An error has occurred: either an argument is invalid, the TX queue is full, or we've run out of memory.
    // It is possible to statically prove that an out-of-memory will never occur for a given application if the
    // heap is sized correctly; for background, refer to the Robson's Proof and the documentation for O1Heap.
}
```

Use [Nunavut](https://github.com/OpenCyphal/nunavut) to automatically generate
(de)serialization code from DSDL definitions.

The CAN frames generated from the message transfer are now stored in the `queue`.
We need to pick them out one by one and have them transmitted.
Normally, the following fragment should be invoked periodically to unload the CAN frames from the
prioritized transmission queue (or several, if redundant interfaces are used) into the CAN driver:

```c
for (const CanardTxQueueItem* ti = NULL; (ti = canardTxPeek(&queue)) != NULL;)  // Peek at the top of the queue.
{
    if ((0U == ti->tx_deadline_usec) || (ti->tx_deadline_usec > getCurrentMicroseconds()))  // Check the deadline.
    {
        if (!pleaseTransmit(ti))               // Send the frame over this redundant CAN iface.
        {
            break;                             // If the driver is busy, break and retry later.
        }
    }
    // After the frame is transmitted or if it has timed out while waiting, pop it from the queue and deallocate:
    canard.memory_free(&canard, canardTxPop(&queue, ti));
}
```

Transfer reception is done by feeding frames into the transfer reassembly state machine
from any of the redundant interfaces.
But first, we need to subscribe:

```c
CanardRxSubscription heartbeat_subscription;
(void) canardRxSubscribe(&canard,   // Subscribe to messages uavcan.node.Heartbeat.
                         CanardTransferKindMessage,
                         7509,      // The fixed Subject-ID of the Heartbeat message type (see DSDL definition).
                         16,        // The extent (the maximum possible payload size) provided by Nunavut.
                         CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                         &heartbeat_subscription);

CanardRxSubscription my_service_subscription;
(void) canardRxSubscribe(&canard,   // Subscribe to an arbitrary service response.
                         CanardTransferKindResponse,  // Specify that we want service responses, not requests.
                         123,       // The Service-ID whose responses we will receive.
                         1024,      // The extent (see above).
                         CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                         &my_service_subscription);
```

The "extent" refers to the minimum amount of memory required to hold any serialized representation of any compatible
version of the data type; or, in other words, it is the maximum possible size of received objects.
This parameter is determined by the data type author at the data type definition time.
It is typically larger than the maximum object size in order to allow the data type author to introduce more
fields in the future versions of the type;
for example, `MyMessage.1.0` may have the maximum size of 100 bytes and the extent 200 bytes;
a revised version `MyMessage.1.1` may have the maximum size anywhere between 0 and 200 bytes.
Extent values are provided per data type by DSDL transcompilers such as Nunavut.

In Libcanard we use the term "subscription" not only for subjects (messages), but also for services, for simplicity.

We can subscribe and unsubscribe at runtime as many times as we want.
Normally, however, an embedded application would subscribe once and roll with it.
Okay, this is how we receive transfers:

```c
CanardRxTransfer transfer;
const int8_t result = canardRxAccept(&canard,
                                     rx_timestamp_usec,          // When the frame was received, in microseconds.
                                     &received_frame,            // The CAN frame received from the bus.
                                     redundant_interface_index,  // If the transport is not redundant, use 0.
                                     &transfer,
                                     NULL);
if (result < 0)
{
    // An error has occurred: either an argument is invalid or we've ran out of memory.
    // It is possible to statically prove that an out-of-memory will never occur for a given application if
    // the heap is sized correctly; for background, refer to the Robson's Proof and the documentation for O1Heap.
    // Reception of an invalid frame is NOT an error.
}
else if (result == 1)
{
    processReceivedTransfer(redundant_interface_index, &transfer);  // A transfer has been received, process it.
    canard.memory_free(&canard, transfer.payload);                  // Deallocate the dynamic memory afterwards.
}
else
{
    // Nothing to do.
    // The received frame is either invalid or it's a non-last frame of a multi-frame transfer.
    // Reception of an invalid frame is NOT reported as an error because it is not an error.
}
```

A simple API for generating CAN hardware acceptance filter configurations is also provided.
Acceptance filters are generated in an extended 29-bit ID + mask scheme and can be used to minimize
the number of irrelevant transfers processed in software.

```c
// Generate an acceptance filter to receive only uavcan.node.Heartbeat.1.0 messages (fixed port-ID 7509):
CanardFilter heartbeat_config = canardMakeFilterForSubject(7509);
// And to receive only uavcan.register.Access.1.0 service transfers (fixed port-ID 384):
CanardFilter register_access_config = canardMakeFilterForService(384, ins.node_id);

// You can also combine the two filter configurations into one (may also accept irrelevant messages).
// This allows consolidating a large set of configurations to fit the number of hardware filters.
// For more information on the optimal subset of configurations to consolidate to minimize wasted CPU,
// see the Cyphal specification.
CanardFilter combined_config =
        canardConsolidateFilters(&heartbeat_config, &register_access_config);
configureHardwareFilters(combined_config.extended_can_id, combined_config.extended_mask);
```

Full API specification is available in the documentation.
If you find the examples to be unclear or incorrect, please, open a ticket.

## Revisions

### v3.0

- Update branding as [UAVCAN v1 is renamed to Cyphal](https://forum.opencyphal.org/t/uavcan-v1-is-now-cyphal/1622).
- Improve MISRA compliance by removing use of flex array: ([#192](https://github.com/OpenCyphal/libcanard/pull/192)).
- Fix dependency issues in docker toolchain.

  There are no API changes in this release aside from the rebranding/renaming:
  `CANARD_UAVCAN_SPECIFICATION_VERSION_MAJOR` -> `CANARD_CYPHAL_SPECIFICATION_VERSION_MAJOR`
  `CANARD_UAVCAN_SPECIFICATION_VERSION_MINOR` -> `CANARD_CYPHAL_SPECIFICATION_VERSION_MINOR`

#### v3.0.1

- Remove UB as described in [203](https://github.com/OpenCyphal/libcanard/issues/203).

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
