// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.
// Author: Pavel Kirienko <pavel.kirienko@zubax.com>
//
// This is a trivial optional extension library that contains basic DSDL  serialization routines.
// It is intended for use in simple applications where auto-generated DSDL serialization logic is not available.
// The functions are fully stateless (pure); read their documentation comments for usage information.
// This is an optional part of libcanard that can be omitted if this functionality is not required by the application.
// High-integrity applications are not recommended to use this extension because it relies on unsafe memory operations.

#ifndef CANARD_DSDL_H_INCLUDED
#define CANARD_DSDL_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef float  CanardDSDLFloat32;
typedef double CanardDSDLFloat64;

/// Serialize a DSDL field value at the specified bit offset from the beginning of the destination buffer.
/// The behavior is undefined if the input pointer is NULL. The time complexity is linear of the bit length.
/// Arguments:
///     buf     Destination buffer where the result will be stored.
///     off_bit Offset, in bits, from the beginning of the buffer. May exceed one byte.
///     value   The value itself (promoted to 64-bit for unification).
///     len_bit Length of the serialized representation, in bits. Zero has no effect.
void canardDSDLSetBit(uint8_t* const buf, const size_t off_bit, const bool value);
void canardDSDLSetUxx(uint8_t* const buf, const size_t off_bit, const uint64_t value, const uint8_t len_bit);
void canardDSDLSetIxx(uint8_t* const buf, const size_t off_bit, const int64_t value, const uint8_t len_bit);
void canardDSDLSetF16(uint8_t* const buf, const size_t off_bit, const CanardDSDLFloat32 value);
void canardDSDLSetF32(uint8_t* const buf, const size_t off_bit, const CanardDSDLFloat32 value);
void canardDSDLSetF64(uint8_t* const buf, const size_t off_bit, const CanardDSDLFloat64 value);

/// Deserialize a DSDL field value located at the specified bit offset from the beginning of the source buffer.
/// If the deserialized value extends beyond the end of the buffer, the missing bits are taken as zero, as required
/// by the DSDL specification (see Implicit Zero Extension Rule, IZER).
/// If len_bit is greater than the return type, extra bits will be truncated per regular narrowing conversion rules.
/// The behavior is undefined if the input pointer is NULL. The time complexity is linear of the bit length.
/// Returns the deserialized value. If the value spills over the buffer boundary, the spilled bits are taken as zero.
/// Arguments:
///     buf      Source buffer where the serialized representation will be read from.
///     buf_size The size of the source buffer, in bytes. Reads past this limit will be assumed to return zero bits.
///     off_bit  Offset, in bits, from the beginning of the buffer. May exceed one byte.
///     len_bit  Length of the serialized representation, in bits. Zero returns zero.
bool     canardDSDLGetBit(const uint8_t* const buf, const size_t buf_size, const size_t off_bit);
uint8_t  canardDSDLGetU08(const uint8_t* const buf, const size_t buf_size, const size_t off_bit, const uint8_t len_bit);
uint16_t canardDSDLGetU16(const uint8_t* const buf, const size_t buf_size, const size_t off_bit, const uint8_t len_bit);
uint32_t canardDSDLGetU32(const uint8_t* const buf, const size_t buf_size, const size_t off_bit, const uint8_t len_bit);
uint64_t canardDSDLGetU64(const uint8_t* const buf, const size_t buf_size, const size_t off_bit, const uint8_t len_bit);
int8_t   canardDSDLGetI08(const uint8_t* const buf, const size_t buf_size, const size_t off_bit, const uint8_t len_bit);
int16_t  canardDSDLGetI16(const uint8_t* const buf, const size_t buf_size, const size_t off_bit, const uint8_t len_bit);
int32_t  canardDSDLGetI32(const uint8_t* const buf, const size_t buf_size, const size_t off_bit, const uint8_t len_bit);
int64_t  canardDSDLGetI64(const uint8_t* const buf, const size_t buf_size, const size_t off_bit, const uint8_t len_bit);
CanardDSDLFloat32 canardDSDLGetF16(const uint8_t* const buf, const size_t buf_size, const size_t off_bit);
CanardDSDLFloat32 canardDSDLGetF32(const uint8_t* const buf, const size_t buf_size, const size_t off_bit);
CanardDSDLFloat64 canardDSDLGetF64(const uint8_t* const buf, const size_t buf_size, const size_t off_bit);

#ifdef __cplusplus
}
#endif
#endif  // CANARD_DSDL_H_INCLUDED
