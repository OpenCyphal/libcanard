// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.
//
// This is a trivial optional extension library that contains basic DSDL primitive serialization routines.
// It is intended for use in simple applications where auto-generated DSDL serialization logic is not available.
// The functions are fully stateless (pure); read their documentation comments for usage information.
// This is an optional part of libcanard that can be omitted if this functionality is not required by the application.
// High-integrity applications are not recommended to use this extension because it relies on unsafe memory operations.

#ifndef CANARD_DSDL_H_INCLUDED
#define CANARD_DSDL_H_INCLUDED

#include <float.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CANARD_DSDL_PLATFORM_TWOS_COMPLEMENT                                                     \
    ((INT8_MIN == -128) && (INT8_MAX == 127) && (INT16_MIN == -32768) && (INT16_MAX == 32767) && \
     (INT32_MIN == -0x80000000LL) && (INT32_MAX == 0x7FFFFFFFLL) && (INT64_MAX == 0x7FFFFFFFFFFFFFFFLL))

#define CANARD_DSDL_PLATFORM_IEEE754 \
    ((FLT_RADIX == 2) && (FLT_MANT_DIG == 24) && (FLT_MIN_EXP == -125) && (FLT_MAX_EXP == 128))

#if CANARD_DSDL_PLATFORM_TWOS_COMPLEMENT

/// This function may be used to serialize values for later transmission in a UAVCAN transfer.
/// It serializes a primitive value -- boolean, integer, character, or floating point -- following the DSDL
/// primitive serialization rules, and puts it at the specified bit offset in the destination buffer.
///
/// The function is only available if the platform uses two's complement signed integer representation.
///
/// If any of the input pointers are NULL or the value of length_bit is not specified in the table,
/// the function has no effect.
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
/// @param offset_bit    Offset, in bits, from the beginning of the destination buffer.
/// @param length_bit    Length of the value, in bits; see the table.
/// @param value         Pointer to the value; see the table.
void canardDSDLPrimitiveSerialize(void* const       destination,
                                  const size_t      offset_bit,
                                  const uint8_t     length_bit,
                                  const void* const value);

/// This function may be used to extract values from received UAVCAN transfers.
/// It deserializes a scalar value -- boolean, integer, character, or floating point -- from the specified
/// bit position in the source buffer.
///
/// The function is only available if the platform uses two's complement signed integer representation.
///
/// If any of the input pointers are NULL or the value of length_bit is not specified in the table,
/// the function has no effect.
///
/// The type of the value pointed to by 'out_value' is defined as follows:
///
///  | bit_length | is_signed   | out_value points to                      |
///  |------------|-------------|------------------------------------------|
///  | 1          | false       | bool (may be incompatible with uint8_t!) |
///  | 1          | true        | N/A                                      |
///  | [2, 8]     | false       | uint8_t, or char                         |
///  | [2, 8]     | true        | int8_t, or char                          |
///  | [9, 16]    | false       | uint16_t                                 |
///  | [9, 16]    | true        | int16_t                                  |
///  | [17, 32]   | false       | uint32_t                                 |
///  | [17, 32]   | true        | int32_t, or 32-bit float IEEE 754        |
///  | [33, 64]   | false       | uint64_t                                 |
///  | [33, 64]   | true        | int64_t, or 64-bit float IEEE 754        |
///
/// @param source       The source buffer where the data will be read from.
/// @param offset_bit   Offset, in bits, from the beginning of the source buffer.
/// @param length_bit   Length of the value, in bits; see the table.
/// @param is_signed    True if the value can be negative (i.e., sign bit extension is needed); see the table.
/// @param out_value    Pointer to the output storage; see the table.
void canardDSDLPrimitiveDeserialize(const void* const source,
                                    const size_t      offset_bit,
                                    const uint8_t     length_bit,
                                    const bool        is_signed,
                                    void* const       out_value);

#endif  // CANARD_DSDL_PLATFORM_TWOS_COMPLEMENT

#if CANARD_DSDL_PLATFORM_IEEE754

/// This alias for the native float is required to comply with MISRA.
typedef float CanardDSDLFloatNative;

/// Convert a native float into the standard IEEE 754 binary16 format. The byte order is native.
/// Overflow collapses into infinity with the same sign.
/// This function is only available if the native float format is IEEE 754 binary32.
uint16_t canardDSDLFloat16Pack(const CanardDSDLFloatNative value);

/// Convert a standard IEEE 754 binary16 value into the native float format. The byte order is native.
/// This function is only available if the native float format is IEEE 754 binary32.
CanardDSDLFloatNative canardDSDLFloat16Unpack(const uint16_t value);

#endif  // CANARD_DSDL_PLATFORM_IEEE754

#ifdef __cplusplus
}
#endif
#endif  // CANARD_DSDL_H_INCLUDED
