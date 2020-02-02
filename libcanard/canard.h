// Copyright (c) 2016-2020 UAVCAN Development Team
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Contributors: https://github.com/UAVCAN/libcanard/contributors

#ifndef CANARD_H_INCLUDED
#define CANARD_H_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------- CONSTANT DEFINITIONS ----------------------------------------

/// Semantic version numbers of this library (not the UAVCAN specification).
/// API will be backward compatible within the same major version.
#define CANARD_VERSION_MAJOR 1
#define CANARD_VERSION_MINOR 0

/// The version number of the UAVCAN specification implemented by this library.
#define CANARD_UAVCAN_SPECIFICATION_VERSION_MAJOR 1

/// Error code definitions; inverse of these values may be returned from API calls.
#define CANARD_OK 0
// Value 1 is omitted intentionally since -1 is often used in 3rd party code
#define CANARD_ERROR_INVALID_ARGUMENT 2
#define CANARD_ERROR_OUT_OF_MEMORY 3
#define CANARD_ERROR_NODE_ID_NOT_SET 4

/// MTU values for supported protocols.
/// Per the recommendations given in the UAVCAN specification, other MTU values should not be used.
#define CANARD_MTU_CAN_CLASSIC 8U
#define CANARD_MTU_CAN_FD 64U

/// Parameter ranges are inclusive; the lower bound is zero for all. Refer to the specification for more info.
#define CANARD_SUBJECT_ID_MAX 32767U
#define CANARD_SERVICE_ID_MAX 511U
#define CANARD_NODE_ID_MAX 127U
#define CANARD_TRANSFER_ID_BIT_LENGTH 5U
#define CANARD_TRANSFER_ID_MAX ((1U << CANARD_TRANSFER_ID_BIT_LENGTH) - 1U)

/// This value represents an undefined node-ID: broadcast destination or anonymous source.
/// Library functions treat all values above @ref CANARD_NODE_ID_MAX as anonymous.
#define CANARD_NODE_ID_UNSET 255U

/// If not specified, the transfer-ID timeout will take this value for all new input sessions.
#define CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC 2000000UL

// ---------------------------------------- TYPE DEFINITIONS ----------------------------------------

// Forward declarations.
typedef struct CanardInstance         CanardInstance;
typedef struct CanardReceivedTransfer CanardReceivedTransfer;

/// Transfer priority level mnemonics per the recommendations given in the UAVCAN specification.
typedef enum
{
    CanardPriorityExceptional = 0,
    CanardPriorityImmediate   = 1,
    CanardPriorityFast        = 2,
    CanardPriorityHigh        = 3,
    CanardPriorityNominal     = 4,
    CanardPriorityLow         = 5,
    CanardPrioritySlow        = 6,
    CanardPriorityOptional    = 7,
} CanardPriority;

/// CAN data frame with an extended 29-bit ID. RTR/Error frames are not used and therefore not modeled here.
typedef struct
{
    /// For RX frames: reception timestamp.
    /// For TX frames: transmission deadline.
    /// The time system may be arbitrary as long as the clock is monotonic (steady).
    uint64_t time_usec;

    /// 29-bit extended ID. The bits above 29-th are ignored.
    uint32_t id;

    /// The useful data in the frame. The length value is not to be confused with DLC!
    uint8_t  data_length;
    uint8_t* data;
} CanardCANFrame;

/// Transfer kinds are defined by the UAVCAN specification.
typedef enum
{
    CanardTransferKindMessagePublication = 0,  ///< Broadcast, from publisher to all subscribers.
    CanardTransferKindServiceResponse    = 1,  ///< Point-to-point, from server to client.
    CanardTransferKindServiceRequest     = 2   ///< Point-to-point, from client to server.
} CanardTransferKind;

/// The application supplies the library with this information when a new transfer should be received.
typedef struct
{
    bool     should_accept;
    uint32_t transfer_id_timeout_usec;
    size_t   payload_capacity;
} CanardTransferReceptionParameters;

/// The application shall implement this function and supply a pointer to it to the library during initialization.
/// The library calls this function to determine whether a transfer should be received.
/// @param ins            Library instance.
/// @param port_id        Subject-ID or service-ID of the transfer.
/// @param transfer_kind  Message or service transfer.
/// @param source_node_id Node-ID of the origin; broadcast if anonymous.
/// @returns @ref CanardTransferReceptionParameters.
typedef CanardTransferReceptionParameters (*CanardShouldAcceptTransfer)(const CanardInstance* ins,
                                                                        uint16_t              port_id,
                                                                        CanardTransferKind    transfer_kind,
                                                                        uint8_t               source_node_id);

/// This is the core structure that keeps all of the states and allocated resources of the library instance.
/// The application should never access any of the fields directly! Instead, the API functions should be used.
struct CanardInstance
{
    CanardShouldAcceptTransfer should_accept_transfer;

    struct CanardInternalInputSession* input_sessions;
    struct CanardInternalTxQueueItem*  tx_queue;

    void* user_reference;  ///< User pointer that can link this instance with other objects

    uint8_t node_id;     ///< Local node-ID or @ref CANARD_NODE_ID_UNSET.
    uint8_t mtu_bytes;  ///< Maximum number of data bytes per CAN frame. Range: [8, 64].
};

/// This structure represents a received transfer for the application.
/// An instance of it is passed to the application via the callback when a new transfer is received.
/// Pointers to the structure and all its fields are invalidated after the callback returns.
struct CanardReceivedTransfer
{
    /// Timestamp of the first frame of this transfer.
    uint64_t timestamp_usec;

    size_t   payload_length;
    uint8_t* payload;

    CanardPriority     priority;
    CanardTransferKind transfer_kind;
    uint16_t           port_id;         ///< Subject-ID or service-ID.
    uint8_t            source_node_id;  ///< For anonymous transfers it's @ref CANARD_NODE_ID_UNSET.
    uint8_t            transfer_id;     ///< Bits above @ref CANARD_TRANSFER_ID_BIT_LENGTH are always zero.
};

// ---------------------------------------- CONFIGURATION FUNCTIONS ----------------------------------------

/// Initialize a library instance.
/// The local node-ID will be initialized as @ref CANARD_NODE_ID_UNSET, i.e. anonymous by default.
///
/// @param should_accept_transfer    Callback, see @ref CanardShouldAcceptTransfer.
/// @param user_reference            Optional application-defined pointer; NULL if not needed.
CanardInstance canardInit(const CanardShouldAcceptTransfer should_accept_transfer,
                          void* const                      user_reference);

/// Assign a new node-ID value to the current node. Node-ID can be assigned only once.
/// If the supplied value is invalid or the node-ID is already configured, nothing will be done.
void canardSetLocalNodeID(CanardInstance* const ins, const uint8_t self_node_id);

/// @returns Node-ID of the local node; 255 (broadcast) if the node-ID is not set, i.e. if the local node is anonymous.
uint8_t canardGetLocalNodeID(const CanardInstance* const ins);

/// Configure the maximum transmission unit. This can be done as many times as needed.
/// This setting defines the maximum number of bytes per CAN data frame in all outgoing transfers.
/// Regardless of this setting, CAN frames with any MTU up to CANARD_CONFIG_MTU_MAX bytes can always be accepted.
///
/// Only the standard values should be used as recommended by the specification (8, 64 bytes);
/// otherwise, interoperability issues may arise. See "CANARD_MTU_*".
///
/// Range: [8, 64]. The default is the maximum.
/// Invalid values are rounded to the nearest valid value.
void canardSetMTU(const CanardInstance* const ins, const uint8_t mtu_bytes);

/// Read the value of the user pointer.
/// The user pointer is configured once during initialization.
/// It can be used to store references to any user-specific data, or to link the instance object with C++ objects.
/// @returns The application-defined pointer.
void* canardGetUserReference(const CanardInstance* const ins);

// ---------------------------------------- OUTGOING TRANSFER FUNCTIONS ----------------------------------------

int32_t canardPublish(CanardInstance* const ins,
                      const uint16_t        subject_id,
                      uint8_t* const        inout_transfer_id,
                      const uint8_t         priority,
                      const size_t          payload_length,
                      const void* const     payload,
                      const uint64_t        deadline_usec);

int32_t canardRequest(CanardInstance* const ins,
                      const uint8_t         server_node_id,
                      const uint16_t        service_id,
                      uint8_t* const        inout_transfer_id,
                      const CanardPriority  priority,
                      const size_t          payload_length,
                      const void* const     payload,
                      const uint64_t        deadline_usec);

int32_t canardRespond(CanardInstance* const ins,
                      const uint8_t         client_node_id,
                      const uint16_t        service_id,
                      const uint8_t         transfer_id,
                      const CanardPriority  priority,
                      const size_t          payload_length,
                      const void* const     payload,
                      const uint64_t        deadline_usec);

/// TODO: REVIEW NEEDED
CanardCANFrame canardPeekTxQueue(const CanardInstance* const ins);
void canardPopTxQueue(const CanardInstance* const ins);

// ---------------------------------------- INCOMING TRANSFER FUNCTIONS ----------------------------------------

CanardReceivedTransfer canardReceive(CanardInstance* const ins, const CanardCANFrame frame);

void canardDestroyTransfer(CanardInstance* const ins, CanardReceivedTransfer* const transfer);

// ---------------------------------------- DATA SERIALIZATION FUNCTIONS ----------------------------------------

/// This function may be used to encode values for later transmission in a UAVCAN transfer. It encodes a scalar value
/// -- boolean, integer, character, or floating point -- and puts it at the specified bit offset in the specified
/// contiguous buffer. Simple payloads can also be encoded manually instead of using this function.
///
/// Caveat: This function works correctly only on platforms that use two's complement signed integer representation.
/// I am not aware of any modern microarchitecture that uses anything else than two's complement, so it should not
/// limit the portability.
///
/// The type of the value pointed to by 'value' is defined as follows:
///
///  | bit_length | value points to                          |
///  |------------|------------------------------------------|
///  | 1          | bool (may be incompatible with uint8_t!) |
///  | [2, 8]     | uint8_t, int8_t, or char                 |
///  | [9, 16]    | uint16_t, int16_t                        |
///  | [17, 32]   | uint32_t, int32_t, or 32-bit float       |
///  | [33, 64]   | uint64_t, int64_t, or 64-bit float       |
///
/// @param destination   Destination buffer where the result will be stored.
/// @param bit_offset    Offset, in bits, from the beginning of the destination buffer.
/// @param bit_length    Length of the value, in bits; see the table.
/// @param value         Pointer to the value; see the table.
void canardSerializePrimitive(void* const       destination,
                              const size_t      bit_offset,
                              const uint8_t     bit_length,
                              const void* const value);

/// This function may be used to extract values from received UAVCAN transfers. It decodes a scalar value --
/// boolean, integer, character, or floating point -- from the specified bit position in the source buffer.
///
/// Caveat: This function works correctly only on platforms that use two's complement signed integer representation.
/// I am not aware of any modern microarchitecture that uses anything else than two's complement, so it should not
/// limit the portability.
///
/// The type of the value pointed to by 'out_value' is defined as follows:
///
///  | bit_length | value_is_signed | out_value points to                      |
///  |------------|-----------------|------------------------------------------|
///  | 1          | false           | bool (may be incompatible with uint8_t!) |
///  | 1          | true            | N/A                                      |
///  | [2, 8]     | false           | uint8_t, or char                         |
///  | [2, 8]     | true            | int8_t, or char                          |
///  | [9, 16]    | false           | uint16_t                                 |
///  | [9, 16]    | true            | int16_t                                  |
///  | [17, 32]   | false           | uint32_t                                 |
///  | [17, 32]   | true            | int32_t, or 32-bit float IEEE 754        |
///  | [33, 64]   | false           | uint64_t                                 |
///  | [33, 64]   | true            | int64_t, or 64-bit float IEEE 754        |
///
/// @param source            The source buffer where the data will be read from.
/// @param bit_offset        Offset, in bits, from the beginning of the source buffer.
/// @param bit_length        Length of the value, in bits; see the table.
/// @param value_is_signed   True if the value can be negative (i.e., sign bit extension is needed); see the table.
/// @param out_value         Pointer to the output storage; see the table.
void canardDeserializePrimitive(const void* const source,
                                const size_t      bit_offset,
                                const uint8_t     bit_length,
                                const bool        value_is_signed,
                                void* const       out_value);

/// IEEE 754 binary16 marshaling helpers.
/// These functions convert between the native float and the standard IEEE 754 binary16 float (a.k.a. half precision).
/// It is assumed that the native float is IEEE 754 binary32, otherwise, the results may be unpredictable.
/// Majority of modern computers and microcontrollers use IEEE 754, so this limitation should not limit the portability.
uint16_t canardComposeFloat16(float value);
float    canardParseFloat16(uint16_t value);

#ifdef __cplusplus
}
#endif
#endif
