# Migration Guide: Updating from Libcanard v3.x to v4.0

This guide is intended to help developers migrate their applications from Libcanard version 3.x to version 4.0. It outlines the key changes between the two versions and provides step-by-step instructions to update your code accordingly.

## Introduction

Libcanard is a compact implementation of the Cyphal/CAN protocol designed for high-integrity real-time embedded systems. Version 4 introduces several changes that may impact your existing codebase. This guide will help you understand these changes and update your application accordingly.

These changes do not affect wire compatibility.

## Version Changes

- **Libcanard Version**:
  - **Old**: `CANARD_VERSION_MAJOR 3`, `CANARD_VERSION_MINOR 3`
  - **New**: `CANARD_VERSION_MAJOR 4`, `CANARD_VERSION_MINOR 0`
- **Cyphal Specification Version**: Remains the same (`1.0`).

## API Changes

### New Functions

- **`canardTxFree`**:
  - **Description**: A helper function to free memory allocated for transmission queue items, including the frame payload buffer.
  - **Prototype**:
    ```c
    void canardTxFree(
        struct CanardTxQueue* const        que,
        const struct CanardInstance* const ins,
        struct CanardTxQueueItem* const    item);
    ```
  - **Usage**: After popping a transmission queue item using `canardTxPop`, use `canardTxFree` to deallocate its memory.

### Modified Functions

Several functions have updated prototypes and usage patterns:

1. **`canardInit`**:
   - **Old Prototype**:
     ```c
     CanardInstance canardInit(
         const CanardMemoryAllocate memory_allocate,
         const CanardMemoryFree     memory_free);
     ```
   - **New Prototype**:
     ```c
     struct CanardInstance canardInit(
         const struct CanardMemoryResource memory);
     ```
   - **Changes**:
     - Replaces `CanardMemoryAllocate` and `CanardMemoryFree` function pointers with a `CanardMemoryResource` struct.

2. **`canardTxInit`**:
   - **Old Prototype**:
     ```c
     CanardTxQueue canardTxInit(
         const size_t capacity,
         const size_t mtu_bytes);
     ```
   - **New Prototype**:
     ```c
     struct CanardTxQueue canardTxInit(
         const size_t                      capacity,
         const size_t                      mtu_bytes,
         const struct CanardMemoryResource memory);
     ```
   - **Changes**:
     - Adds a `CanardMemoryResource` parameter for memory allocation of payload data.

3. **`canardTxPush`**:
   - **Old Prototype**:
     ```c
     int32_t canardTxPush(
         CanardTxQueue* const                que,
         CanardInstance* const               ins,
         const CanardMicrosecond             tx_deadline_usec,
         const CanardTransferMetadata* const metadata,
         const size_t                        payload_size,
         const void* const                   payload);
     ```
   - **New Prototype**:
     ```c
     int32_t canardTxPush(
         struct CanardTxQueue* const                que,
         const struct CanardInstance* const         ins,
         const CanardMicrosecond                    tx_deadline_usec,
         const struct CanardTransferMetadata* const metadata,
         const struct CanardPayload                 payload);
     ```
   - **Changes**:
     - Replaces `payload_size` and `payload` with a single `CanardPayload` struct.

4. **`canardTxPeek`** and **`canardTxPop`**:
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

- **`CanardMemoryResource`** and **`CanardMemoryDeleter`**:
  - New structs to encapsulate memory allocation and deallocation functions along with user references.

## Memory Management Changes

- **Memory Resource Structs**:
  - Memory allocation and deallocation are now handled via `CanardMemoryResource` and `CanardMemoryDeleter` structs.
  - Functions now receive these structs instead of direct function pointers.

- **Allocation Functions**:
  - **Allocation**:
    ```c
    typedef void* (*CanardMemoryAllocate)(
        void* const user_reference,
        const size_t size);
    ```
  - **Deallocation**:
    ```c
    typedef void (*CanardMemoryDeallocate)(
        void* const user_reference,
        const size_t size,
        void* const pointer);
    ```

## Migration Steps

1. **Update Type Definitions**:
   - Replace all `typedef`-based enum and struct types with direct `struct` and `enum` declarations.
   - For example, change `CanardInstance` to `struct CanardInstance`.

2. **Adjust Memory Management Code**:
   - Replace separate memory allocation and deallocation function pointers with `CanardMemoryResource` and `CanardMemoryDeleter` structs.
   - Update function calls and definitions accordingly.

3. **Modify Function Calls**:
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
       canardTxPush(que, ins, tx_deadline_usec, metadata, payload_struct);
       ```

4. **Handle New Functions**:
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

5. **Update Struct Field Access**:
   - Adjust your code to access struct fields directly, considering the changes in struct definitions.
   - For example, access `payload.size` instead of `payload_size`.

6. **Adjust Memory Allocation Logic**:
   - Ensure that your memory allocation and deallocation functions conform to the new prototypes.
   - Pay attention to the additional `size` parameter in the deallocation function.

7. **Test Thoroughly**:
   - After making the changes, thoroughly test your application to ensure that it functions correctly with the new library version.
   - Pay special attention to memory management and potential leaks.
