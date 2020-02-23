// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#include "canard_dsdl.h"
#include <assert.h>

// --------------------------------------------- BUILD CONFIGURATION ---------------------------------------------

/// By default, this macro resolves to the standard assert(). The user can redefine this if necessary.
/// To disable assertion checks completely, make it expand into `(void)(0)`.
#ifndef CANARD_ASSERT
// Intentional violation of MISRA: assertion macro cannot be replaced with a function definition.
#    define CANARD_ASSERT(x) assert(x)  // NOSONAR
#endif

/// This macro is needed only for testing and for library development. Do not redefine this in production.
#if defined(CANARD_EXPOSE_PRIVATE) && CANARD_EXPOSE_PRIVATE
#    define CANARD_PRIVATE
#else
#    define CANARD_PRIVATE static inline
#endif

#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 201112L)
#    error "Unsupported language: ISO C11 or a newer version is required."
#endif

// --------------------------------------------- COMMON CONSTANTS ---------------------------------------------

#define BYTE_SIZE 8U
#define BYTE_MAX 0xFFU

// --------------------------------------------- PRIMITIVE SERIALIZATION ---------------------------------------------

#if CANARD_DSDL_PLATFORM_TWOS_COMPLEMENT

/// Per the DSDL specification, it is assumed that 1 byte = 8 bits.
CANARD_PRIVATE void copyBitArray(const size_t         length_bit,
                                 const size_t         src_offset_bit,
                                 const size_t         dst_offset_bit,
                                 const uint8_t* const src,
                                 uint8_t* const       dst)
{
    CANARD_ASSERT((src != NULL) || (length_bit == 0U));
    CANARD_ASSERT((dst != NULL) || (length_bit == 0U));
    size_t       src_off  = src_offset_bit;
    size_t       dst_off  = dst_offset_bit;
    const size_t last_bit = src_off + length_bit;
    while (last_bit > src_off)
    {
        const uint8_t src_mod = (uint8_t)(src_off % BYTE_SIZE);
        const uint8_t dst_mod = (uint8_t)(dst_off % BYTE_SIZE);
        const uint8_t max_mod = (src_mod > dst_mod) ? src_mod : dst_mod;
        size_t        size    = BYTE_SIZE - max_mod;
        if (size > (last_bit - src_off))
        {
            size = last_bit - src_off;
        }
        const uint8_t mask       = (uint8_t)((((BYTE_MAX << BYTE_SIZE) >> size) & BYTE_MAX) >> dst_mod);
        const uint8_t in         = (uint8_t)(((uint32_t) src[src_off / BYTE_SIZE] << src_mod) >> dst_mod);
        const uint8_t a          = dst[dst_off / BYTE_SIZE] & (uint8_t) ~mask;
        const uint8_t b          = in & mask;
        dst[dst_off / BYTE_SIZE] = a | b;
        src_off += size;
        dst_off += size;
    }
}

#endif  // CANARD_DSDL_PLATFORM_TWOS_COMPLEMENT

// --------------------------------------------- FLOAT16 SUPPORT ---------------------------------------------

#if CANARD_DSDL_PLATFORM_IEEE754

// Intentional violation of MISRA: we need this union because the alternative is far more error prone.
// We have to rely on low-level data representation details to do the conversion; unions are helpful.
typedef union  // NOSONAR
{
    uint32_t              bits;
    CanardDSDLFloatNative real;
} Float32Bits;
_Static_assert(4 == sizeof(Float32Bits), "Unsupported float model");

uint16_t canardDSDLFloat16Pack(const CanardDSDLFloatNative value)
{
    // The no-lint statements suppress the warnings about magic numbers.
    // The no-lint statements suppress the warning about the use of union. This is required for low-level bit access.
    const uint32_t    round_mask = ~(uint32_t) 0x0FFFU;                 // NOLINT NOSONAR
    const Float32Bits f32inf     = {.bits = ((uint32_t) 255U) << 23U};  // NOLINT NOSONAR
    const Float32Bits f16inf     = {.bits = ((uint32_t) 31U) << 23U};   // NOLINT NOSONAR
    const Float32Bits magic      = {.bits = ((uint32_t) 15U) << 23U};   // NOLINT NOSONAR
    Float32Bits       in         = {.real = value};                     // NOSONAR
    const uint32_t    sign       = in.bits & (((uint32_t) 1U) << 31U);  // NOLINT NOSONAR
    in.bits ^= sign;
    uint16_t out = 0;
    if (in.bits >= f32inf.bits)
    {
        out = (in.bits > f32inf.bits) ? (uint16_t) 0x7FFFU : (uint16_t) 0x7C00U;  // NOLINT NOSONAR
    }
    else
    {
        in.bits &= round_mask;
        in.real *= magic.real;
        in.bits -= round_mask;
        if (in.bits > f16inf.bits)
        {
            in.bits = f16inf.bits;
        }
        out = (uint16_t)(in.bits >> 13U);  // NOLINT NOSONAR
    }
    out |= (uint16_t)(sign >> 16U);  // NOLINT NOSONAR
    return out;
}

CanardDSDLFloatNative canardDSDLFloat16Unpack(const uint16_t value)
{
    // The no-lint statements suppress the warnings about magic numbers.
    // The no-lint statements suppress the warning about the use of union. This is required for low-level bit access.
    const Float32Bits magic   = {.bits = ((uint32_t) 0xEFU) << 23U};             // NOLINT NOSONAR
    const Float32Bits inf_nan = {.bits = ((uint32_t) 0x8FU) << 23U};             // NOLINT NOSONAR
    Float32Bits       out     = {.bits = ((uint32_t)(value & 0x7FFFU)) << 13U};  // NOLINT NOSONAR
    out.real *= magic.real;
    if (out.real >= inf_nan.real)
    {
        out.bits |= ((uint32_t) 0xFFU) << 23U;  // NOLINT NOSONAR
    }
    out.bits |= ((uint32_t)(value & 0x8000U)) << 16U;  // NOLINT NOSONAR
    return out.real;
}

#endif  // CANARD_DSDL_PLATFORM_IEEE754
