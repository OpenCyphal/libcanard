// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#include "canard_dsdl.h"
#include <assert.h>

#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 201112L)
#    error "Unsupported language: ISO C11 or a newer version is required."
#endif

#ifndef CANARD_ASSERT
#    define CANARD_ASSERT assert
#endif

#if CANARD_DSDL_PLATFORM_TWOS_COMPLEMENT

// TODO implement

#endif  // CANARD_DSDL_PLATFORM_TWOS_COMPLEMENT

#if CANARD_DSDL_PLATFORM_IEEE754

// Intentional violation of MISRA: we need this union because the alternative is far more error prone.
// We have to rely on low-level data representation details to do the conversion; unions are helpful.
typedef union  // NOSONAR
{
    uint32_t              bits;
    CanardDSDLFloatNative real;
} Float32Bits;
static_assert(4 == sizeof(Float32Bits), "Unsupported float model");

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
