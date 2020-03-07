# Compact UAVCAN/CAN v1 in C

[![Build Status](https://travis-ci.org/UAVCAN/libcanard.svg?branch=master)](https://travis-ci.org/UAVCAN/libcanard)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=libcanard&metric=alert_status)](https://sonarcloud.io/dashboard?id=libcanard)
[![Reliability Rating](https://sonarcloud.io/api/project_badges/measure?project=libcanard&metric=reliability_rating)](https://sonarcloud.io/dashboard?id=libcanard)
[![Coverage](https://sonarcloud.io/api/project_badges/measure?project=libcanard&metric=coverage)](https://sonarcloud.io/dashboard?id=libcanard)
[![Lines of Code](https://sonarcloud.io/api/project_badges/measure?project=libcanard&metric=ncloc)](https://sonarcloud.io/dashboard?id=libcanard)
[![Forum](https://img.shields.io/discourse/users.svg?server=https%3A%2F%2Fforum.uavcan.org&color=1700b3)](https://forum.uavcan.org)

Libcanard is a compact implementation of the UAVCAN/CAN protocol stack in C11 for high-integrity real-time
embedded systems.

[UAVCAN](https://uavcan.org) is an open lightweight data bus standard designed for reliable intravehicular
communication in aerospace and robotic applications via CAN bus, Ethernet, and other robust transports.
The acronym UAVCAN stands for *Uncomplicated Application-level Vehicular Communication And Networking*.

**READ THE DOCS: [`libcanard/canard.h`](/libcanard/canard.h)**

Contribute: [`CONTRIBUTING.md`](/CONTRIBUTING.md)

Ask questions: [forum.uavcan.org](https://forum.uavcan.org)

## Example

The example augments the documentation but does not replace it.

The library requires a constant-complexity deterministic dynamic memory allocator.
We could use the standard C heap, but most implementations are not constant-complexity,
so let's suppose that we're using [O1Heap](https://github.com/pavel-kirienko/o1heap) instead.
We are going to need basic wrappers:

```c
static void* memAllocate(CanardInstance* const ins, const size_t amount)
{
    (void) ins;
    return o1heapAllocate(my_allocator, amount);
}

static void memFree(CanardInstance* const ins, void* const pointer)
{
    (void) ins;
    o1heapFree(my_allocator, pointer);
}
```

Init a library instance:

```c
CanardInstance ins = canardInit(&memAllocate, &memFree);
ins.mtu_bytes = CANARD_MTU_CAN_CLASSIC;  // Defaults to 64 (CAN FD); here we select Classic CAN.
ins.node_id   = 42;                      // Defaults to anonymous; can be set up later at any point.
```

Publish a message:

```c
static uint8_t my_message_transfer_id;  // Must be static or heap-allocated to retain state between calls.
const CanardTransfer transfer = {
    .timestamp_usec = transmission_deadline,      // Zero if transmission deadline is not limited.
    .priority       = CanardPriorityNominal,
    .transfer_kind  = CanardTransferKindMessage,
    .port_id        = 1234,                       // This is the subject-ID.
    .remote_node_id = CANARD_NODE_ID_UNSET,       // Messages cannot be unicast, so use UNSET.
    .transfer_id    = my_message_transfer_id,
    .payload_size   = 47,
    .payload        = "\x2D\x00" "Sancho, it strikes me thou art in great fear.",
};
++my_message_transfer_id;  // The transfer-ID shall be incremented after every transmission on this subject.
int32_t result = canardTxPush(&ins, &transfer);
if (result < 0)
{
    // An error has occurred: either an argument is invalid or we've ran out of memory.
    // It is possible to statically prove that an out-of-memory will never occur for a given application if the
    // heap is sized correctly; for background, refer to the Robson's Proof and the documentation for O1Heap.
    abort();
}
```

The CAN frames generated from the message transfer are now stored in the transmission queue.
We need to pick them out one by one and have them transmitted.
Normally, the following fragment should be invoked periodically to unload the CAN frames from the
prioritized transmission queue into the CAN driver (or several, if redundant interfaces are used):

```c
for (const CanardFrame* txf = NULL; (txf = canardTxPeek(&ins)) != NULL;)  // Look at the top of the TX queue.
{
    if (txf->timestamp_usec < getCurrentMicroseconds())  // Check if the frame has timed out.
    {
        if (!pleaseTransmit(&txf))             // Send the frame. Redundant interfaces may be used here.
        {
            break;                             // If the driver is busy, break and retry later.
        }
    }
    canardTxPop(&ins);                         // Remove the frame from the queue after it's transmitted.
    ins.memory_free(&ins, (CanardFrame*)txf);  // Deallocate the dynamic memory afterwards.
}
```

Transfer reception is done by feeding frames into the transfer reassembly state machine.
But first, we need to subscribe:

```c
CanardRxSubscription my_subscription;
(void) canardRxSubscribe(&ins,
                         CanardTransferKindResponse,  // Indicate that we want service responses.
                         123,                         // The Service-ID to subscribe to.
                         1024,                        // The maximum payload size (max DSDL object size).
                         &my_subscription);
```

We can subscribe and unsubscribe at runtime as many times as we want.
Normally, however, an embedded application would subscribe once and roll with it.
Okay, this is how we receive transfers:

```c
CanardTransfer transfer;
const int8_t result = canardRxAccept(&ins,
                                     &received_frame,            // The CAN frame received from the bus.
                                     redundant_interface_index,  // If the transport is not redundant, use 0.
                                     &transfer);
if (result < 0)
{
    // An error has occurred: either an argument is invalid or we've ran out of memory.
    // It is possible to statically prove that an out-of-memory will never occur for a given application if
    // the heap is sized correctly; for background, refer to the Robson's Proof and the documentation for O1Heap.
    // Reception of an invalid frame is NOT an error.
    abort();
}
else if (result == 1)
{
    processReceivedTransfer(redundant_interface_index, &transfer);  // A transfer has been received, process it.
    ins.memory_free(&ins, (void*)transfer.payload);  // Deallocate the dynamic memory afterwards.
}
else
{
    // Nothing to do.
    // The received frame is either invalid or it's a non-last frame of a multi-frame transfer.
    // Reception of an invalid frame is NOT reported as an error because it is not an error.
}
```

The DSDL serialization helper library can be used to (de-)serialize DSDL objects without auto-generated code.
Here's a simple deserialization example for a `uavcan.node.Heartbeat.1.0` message:

```c
uint8_t  mode   = canardDSDLGetU8(heartbeat_transfer->payload,  heartbeat_transfer->payload_size, 34,  3);
uint32_t uptime = canardDSDLGetU32(heartbeat_transfer->payload, heartbeat_transfer->payload_size,  0, 32);
uint32_t vssc   = canardDSDLGetU32(heartbeat_transfer->payload, heartbeat_transfer->payload_size, 37, 19);
uint8_t  health = canardDSDLGetU8(heartbeat_transfer->payload,  heartbeat_transfer->payload_size, 32,  2);
```

And the opposite:

```c
uint8_t buffer[7];
//              destination offset   value bit-length
canardDSDLSetUxx(&buffer[0], 34,          2,  3);   // mode
canardDSDLSetUxx(&buffer[0],  0, 0xDEADBEEF, 32);   // uptime
canardDSDLSetUxx(&buffer[0], 37,    0x7FFFF, 19);   // vssc
canardDSDLSetUxx(&buffer[0], 32,          2,  2);   // health
// Now it can be transmitted:
my_transfer->payload      = &buffer[0];
my_transfer->payload_size = sizeof(buffer);
result = canardTxPush(&ins, &my_transfer);
```

Full API specification is available in the documentation.
If you find the examples to be unclear or incorrect, please, open a ticket.
