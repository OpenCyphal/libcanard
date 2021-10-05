# Compact UAVCAN/CAN v1 in C

[![Build Status](https://travis-ci.org/UAVCAN/libcanard.svg?branch=master)](https://travis-ci.org/UAVCAN/libcanard)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=libcanard&metric=alert_status)](https://sonarcloud.io/dashboard?id=libcanard)
[![Reliability Rating](https://sonarcloud.io/api/project_badges/measure?project=libcanard&metric=reliability_rating)](https://sonarcloud.io/dashboard?id=libcanard)
[![Coverage](https://sonarcloud.io/api/project_badges/measure?project=libcanard&metric=coverage)](https://sonarcloud.io/dashboard?id=libcanard)
[![Lines of Code](https://sonarcloud.io/api/project_badges/measure?project=libcanard&metric=ncloc)](https://sonarcloud.io/dashboard?id=libcanard)
[![Forum](https://img.shields.io/discourse/users.svg?server=https%3A%2F%2Fforum.uavcan.org&color=1700b3)](https://forum.uavcan.org)

Libcanard is a compact implementation of the UAVCAN/CAN protocol stack in C99/C11 for high-integrity real-time
embedded systems.

[UAVCAN](https://uavcan.org) is an open lightweight data bus standard designed for reliable intravehicular
communication in aerospace and robotic applications via CAN bus, Ethernet, and other robust transports.
The acronym UAVCAN stands for *Uncomplicated Application-level Vehicular Computing And Networking*.

**Read the docs in [`libcanard/canard.h`](/libcanard/canard.h).**

Find examples, starters, tutorials on the
[UAVCAN forum](https://forum.uavcan.org/t/libcanard-examples-starters-tutorials/935).

If you want to contribute, please read [`CONTRIBUTING.md`](/CONTRIBUTING.md).

## Features

- Full test coverage and extensive static analysis.
- Compliance with automatically enforceable MISRA C rules (reach out to <https://forum.uavcan.org> for details).
- Detailed time complexity and memory requirement models for the benefit of real-time high-integrity applications.
- Purely reactive API without the need for background servicing.
- Support for the Classic CAN and CAN FD.
- Support for redundant transports.
- Compatibility with 8/16/32/64-bit platforms.
- Compatibility with extremely resource-constrained baremetal environments starting from ca. 32K ROM, 4..8K RAM.
- Implemented in less than 1500 lines of code.

## Platforms

The library is designed to be usable without modification with any conventional 8/16/32/64-bit platform,
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

The UAVCAN Development Team maintains a collection of various platform-specific components in a separate repository
at <https://github.com/UAVCAN/platform_specific_components>.
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
CanardTxQueue queue = canardTxInit(100,                 // Limit the size of the queue at 100 frames max.
                                   CANARD_MTU_CAN_FD);  // Set MTU = 64 bytes (CAN FD).
```

Publish a message:

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
    // An error has occurred: either an argument is invalid, the TX queue is full, or we've ran out of memory.
    // It is possible to statically prove that an out-of-memory will never occur for a given application if the
    // heap is sized correctly; for background, refer to the Robson's Proof and the documentation for O1Heap.
}
```

Use [Nunavut](https://github.com/UAVCAN/nunavut) to automatically generate (de)serialization code from DSDL definitions.

The CAN frames generated from the message transfer are now stored in the `queue`.
We need to pick them out one by one and have them transmitted.
Normally, the following fragment should be invoked periodically to unload the CAN frames from the
prioritized transmission queue into the CAN driver (or several, if redundant interfaces are used):

```c
for (const CanardFrame* txf = NULL; (txf = canardTxPeek(&queue)) != NULL;)  // Look at the top of this TX queue.
{
    if ((0U == txf->timestamp_usec) || (txf->timestamp_usec > getCurrentMicroseconds()))  // Check the deadline.
    {
        if (!pleaseTransmit(txf))              // Send the frame over this redundant CAN iface.
        {
            break;                             // If the driver is busy, break and retry later.
        }
    }
    // After the frame is transmitted or if it has timed out while waiting, pop it from the queue and deallocate:
    canard.memory_free(&canard, canardTxPop(&queue));
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
                         16,        // The extent (the maximum possible payload size); pick a huge value if not sure.
                         CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
                         &heartbeat_subscription);

CanardRxSubscription my_service_subscription;
(void) canardRxSubscribe(&canard,   // Subscribe to an arbitrary service response.
                         CanardTransferKindResponse,  // Specify that we want service responses, not requests.
                         123,       // The Service-ID whose responses we will receive.
                         1024,      // The extent (the maximum payload size); pick a huge value if not sure.
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
It is always safe to pick a larger value if not sure.
You will find a more rigorous description in the UAVCAN Specification.

In Libcanard we use the term "subscription" not only for subjects (messages), but also for services, for simplicity.

We can subscribe and unsubscribe at runtime as many times as we want.
Normally, however, an embedded application would subscribe once and roll with it.
Okay, this is how we receive transfers:

```c
CanardTransfer transfer;
const int8_t result = canardRxAccept(&canard,
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

## Revisions

### v2.0

- Dedicated transmission queues per redundant CAN interface with depth limits.
- Fixed issues with const-correctness.
- Manual DSDL serialization helpers removed; use [Nunavut](https://github.com/UAVCAN/nunavut) instead.
- `canardRxAccept2()` replaced the deprecated `canardRxAccept()`.
- Support build configuration headers via `CANARD_CONFIG_HEADER`.

### v1.1

- Add new API function `canardRxAccept2()`, deprecate `canardRxAccept()`.
- Provide user references in `CanardRxSubscription`.
- Promote certain internal fields to the public API to allow introspection.

### v1.0

The initial release.
