# Migration Guide: Updating from Libcanard v3.x to v4.0

This guide is intended to help developers migrate their applications from Libcanard version 3.x to version 4.0. It outlines the key changes between the two versions and provides step-by-step instructions to update your code accordingly.

## Introduction

Libcanard is a compact implementation of the Cyphal/CAN protocol designed for high-integrity real-time embedded systems. Version 4 introduces several changes that may impact your existing codebase. This guide will help you understand these changes and update your application accordingly.

These changes do not affect wire compatibility.

## Version Changes

- **Libcanard Version**:
  - **Old**: `CANARD_VERSION_MAJOR 3`
  - **New**: `CANARD_VERSION_MAJOR 4`
- **Cyphal Specification Version**: Remains the same (`1.0`).

## API Changes

### New Functions

- **`canardTxFree`**:
  - **Description**: A helper function to free memory allocated for transmission queue items, including the frame payload buffer.
  - **Prototype**:
    ```c
    void canardTxFree(struct CanardTxQueue* const        que,
                      const struct CanardInstance* const ins,
                      struct CanardTxQueueItem* const    item);
    ```
  - **Usage**: After popping a transmission queue item using `canardTxPop`, use `canardTxFree` to deallocate its memory.

- **`canardTxPoll`**:
  - **Description**: A helper function simplifies the transmission process by combining frame retrieval, transmission, and cleanup into a single function.
  - **Prototype**:
    ```c
    int8_t canardTxPoll(struct CanardTxQueue* const        que,
                        const struct CanardInstance* const ins,
                        const CanardMicrosecond            now_usec,
                        void* const                        user_reference,
                        const CanardTxFrameHandler         frame_handler,
                        uint64_t* const                    frames_expired,
                        uint64_t* const                    frames_failed);
    ```
    - **Purpose**: Streamlines the process of handling frames from the TX queue.
    - **Functionality**:
      - Retrieves the next frame to be transmitted.
      - Invokes a user-provided `frame_handler` to transmit the frame.
      - Manages frame cleanup based on the handler's return value.
      - Automatically drops timed-out frames if `now_usec` is provided.
      - Increments the statistical counters `frames_expired` and `frames_failed` unless their pointers are NULL.

### Modified Functions

Several functions have updated prototypes and usage patterns:

1. **`canardInit`**:
   - **Old Prototype**:
     ```c
     CanardInstance canardInit(const CanardMemoryAllocate memory_allocate,
                               const CanardMemoryFree     memory_free);
     ```
   - **New Prototype**:
     ```c
     struct CanardInstance canardInit(const struct CanardMemoryResource memory);
     ```
   - **Changes**:
     - Replaces `CanardMemoryAllocate` and `CanardMemoryFree` function pointers with a `CanardMemoryResource` struct.

1. **`canardTxInit`**:
   - **Old Prototype**:
     ```c
     CanardTxQueue canardTxInit(const size_t capacity,
                                const size_t mtu_bytes);
     ```
   - **New Prototype**:
     ```c
     struct CanardTxQueue canardTxInit(const size_t                      capacity,
                                       const size_t                      mtu_bytes,
                                       const struct CanardMemoryResource memory);
     ```
   - **Changes**:
     - Adds a `CanardMemoryResource` parameter for memory allocation of payload data.

1. **`canardTxPush`**:
   - **Old Prototype**:
     ```c
     int32_t canardTxPush(CanardTxQueue* const                que,
                          CanardInstance* const               ins,
                          const CanardMicrosecond             tx_deadline_usec,
                          const CanardTransferMetadata* const metadata,
                          const size_t                        payload_size,
                          const void* const                   payload);
     ```
   - **New Prototype**:
     ```c
     int32_t canardTxPush(struct CanardTxQueue* const                que,
                          const struct CanardInstance* const         ins,
                          const CanardMicrosecond                    tx_deadline_usec,
                          const struct CanardTransferMetadata* const metadata,
                          const struct CanardPayload                 payload,
                          const CanardMicrosecond                    now_usec,
                          uint64_t* const                            frames_expired);
     ```
   - **Changes**:
     - Replaces `payload_size` and `payload` with a single `CanardPayload` struct.
     - Adds a `now_usec` parameter for handling timed-out frames.
       - **Purpose**: Allows the library to automatically drop frames that have exceeded their transmission deadlines (`tx_deadline_usec`).
       - **Behavior**: If `now_usec` is greater than `tx_deadline_usec`, the frames already in the TX queue will be dropped, and the `dropped_frames` counter in `CanardTxQueueStats` will be incremented.
       - **Optional Feature**: Passing `0` for `now_usec` disables automatic dropping, maintaining previous behavior.
     - Adds a new `frames_expired` parameter that is incremented for every expired frame, unless NULL.

1. **`canardTxPeek`** and **`canardTxPop`**:
   - The functions now return and accept pointers to mutable `struct CanardTxQueueItem` instead of const pointers.

### Removed Functions

- No functions have been explicitly removed, but some have modified prototypes.

## Data Structure Changes

### Type Definitions

- **Enumerations**:
  - Changed from `typedef enum` to `enum` without `typedef`.
  - **Old**:
    ```c
    typedef enum { ... } CanardPriority;
    ```
  - **New**:
    ```c
    enum CanardPriority { ... };
    ```

- **Structures**:
  - Changed from forward declarations and `typedef struct` to direct `struct` definitions.
  - **Old**:
    ```c
    typedef struct CanardInstance CanardInstance;
    ```
  - **New**:
    ```c
    struct CanardInstance { ... };
    ```

- **Function pointers**:
  - **Added** `CanardTxFrameHandler` function pointer type.
    ```c
    typedef int8_t (*CanardTxFrameHandler)(void* const                      user_reference,
                                           const CanardMicrosecond          deadline_usec,
                                           struct CanardMutableFrame* const frame);
    ``` 

### Struct Modifications

- **`CanardFrame`**:
  - **Old**:
    ```c
    typedef struct {
        uint32_t extended_can_id;
        size_t   payload_size;
        const void* payload;
    } CanardFrame;
    ```
  - **New**:
    ```c
    struct CanardFrame {
        uint32_t extended_can_id;
        struct CanardPayload payload;
    };
    ```
  - **Changes**:
    - Payload now uses the `CanardPayload` struct.

- **New Structs Introduced**:
  - **`CanardPayload`** and **`CanardMutablePayload`**: Encapsulate payload data and size.
  - **`CanardMutableFrame`**: Similar to `CanardFrame` but uses `CanardMutablePayload`.

- **`CanardInstance`**:
  - Now contains a `CanardMemoryResource` for memory management instead of separate function pointers.

- **`CanardTxQueue`**:
  - Includes a `CanardMemoryResource` for payload data allocation.
  - Includes a `CanardTxQueueStats` for tracking number of dropped frames.

- **`CanardMemoryResource`** and **`CanardMemoryDeleter`**:
  - New structs to encapsulate memory allocation and deallocation functions along with user references.

## Memory Management Changes

- **Memory Resource Structs**:
  - Memory allocation and deallocation are now handled via `CanardMemoryResource` and `CanardMemoryDeleter` structs.
  - Functions now receive these structs instead of direct function pointers.

- **Allocation Functions**:
  - **Allocation**:
    ```c
    typedef void* (*CanardMemoryAllocate)(void* const user_reference, const size_t size);
    ```
  - **Deallocation**:
    ```c
    typedef void (*CanardMemoryDeallocate)(void* const user_reference, const size_t size, void* const pointer);
    ```

## Automatic Dropping of Timed-Out Frames

#### Description

Frames in the TX queue that have exceeded their `tx_deadline_usec` can now be automatically dropped when `now_usec` is provided to `canardTxPush()` or `canardTxPoll()`.

- **Benefit**: Reduces the worst-case peak memory footprint.
- **Optional**: Feature can be disabled by passing `0` for `now_usec`.

#### Migration Steps

1. **Enable or Disable Automatic Dropping**:

    - **Enable**: Provide the current time to `now_usec` in both `canardTxPush()` and `canardTxPoll()`.
    - **Disable**: Pass `0` to `now_usec` to retain manual control.

2. **Adjust Application Logic**:

    - Monitor the `dropped_frames` counter in `CanardTxQueueStats` if tracking of dropped frames is required.

## Migration Steps

1. **Update Type Definitions**:
   - Replace all `typedef`-based enum and struct types with direct `struct` and `enum` declarations.
   - For example, change `CanardInstance` to `struct CanardInstance`.

1. **Adjust Memory Management Code**:
   - Replace separate memory allocation and deallocation function pointers with `CanardMemoryResource` and `CanardMemoryDeleter` structs.
   - Update function calls and definitions accordingly.

1. **Modify Function Calls**:
   - Update all function calls to match the new prototypes.
   - **`canardInit`**:
     - Before:
       ```c
       canardInit(memory_allocate, memory_free);
       ```
     - After:
       ```c
       struct CanardMemoryResource memory = {
           .user_reference = ...,
           .allocate = memory_allocate,
           .deallocate = memory_free
       };
       canardInit(memory);
       ```
   - **`canardTxInit`**:
     - Before:
       ```c
       canardTxInit(capacity, mtu_bytes);
       ```
     - After:
       ```c
       canardTxInit(capacity, mtu_bytes, memory);
       ```
   - **`canardTxPush`**:
     - Before:
       ```c
       canardTxPush(que, ins, tx_deadline_usec, metadata, payload_size, payload);
       ```
     - After:
       ```c
       struct CanardPayload payload_struct = {
           .size = payload_size,
           .data = payload
       };
       uint64_t frames_expired = 0;  // This is optional; pass NULL if not needed.
       canardTxPush(que, ins, tx_deadline_usec, metadata, payload_struct, now_usec, &frames_expired);
       ```

1. **Handle New Functions**:
   - Use `canardTxFree` to deallocate transmission queue items after popping them.
     - Example:
       ```c
       struct CanardTxQueueItem* item = canardTxPeek(&tx_queue);
       while (item != NULL) {
           // Transmit the frame...
           canardTxPop(&tx_queue, item);
           canardTxFree(&tx_queue, &canard_instance, item);
           item = canardTxPeek(&tx_queue);
       }
       ```

   - If currently using `canardTxPeek()`, `canardTxPop()`, and `canardTxFree()`, consider replacing that logic with `canardTxPoll()` for simplicity.
     - Define a function matching the `CanardTxFrameHandler` signature:
       ```c
       int8_t myFrameHandler(void* const user_reference,
                             const CanardMicrosecond deadline_usec,
                             struct CanardMutableFrame* frame)
       {
           // Implement transmission logic here
           // Return positive value on success - the frame will be released
           // Return zero to retry later - the frame will stay in the TX queue
           // Return negative value on failure - whole transfer (including this frame) will be dropped
       }
       ```     
     - Example:  
       ```c
       // Before
       struct CanardTxQueueItem* item = canardTxPeek(queue);
       if (item != NULL) {
           // Handle deadline
           // Transmit item->frame
           // Unless the media is busy:
           item = canardTxPop(queue, item);
           canardTxFree(queue, instance, item);
       }
       // After
       uint64_t frames_expired = 0;  // The counters are optional; pass NULL if not needed.
       uint64_t frames_failed  = 0;
       int8_t result = canardTxPoll(queue,
                                    instance,
                                    now_usec,
                                    user_reference,
                                    myFrameHandler,
                                    &frames_expired,
                                    &frames_failed);
       ```
1. **Update Struct Field Access**:
   - Adjust your code to access struct fields directly, considering the changes in struct definitions.
   - For example, access `payload.size` instead of `payload_size`.

1. **Adjust Memory Allocation Logic**:
   - Ensure that your memory allocation and deallocation functions conform to the new prototypes.
   - Pay attention to the additional `size` parameter in the deallocation function.

1. **Test Thoroughly**:
   - After making the changes, thoroughly test your application to ensure that it functions correctly with the new library version.
   - Pay special attention to memory management and potential leaks.
