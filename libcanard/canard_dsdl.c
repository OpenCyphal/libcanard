// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.

#include "canard_dsdl.h"
#include <assert.h>
#include <string.h>

// --------------------------------------------- BUILD CONFIGURATION ---------------------------------------------

/// This option allows the user to improve the primitive (de-)serialization performance if the target platform
/// is little endian.
/// There are two implementations of the primitive (de-)serialization algorithms: a generic one, which is invariant
/// to the native byte order (and therefore compatible with any platform), and the optimized one which is compatible
/// with little-endian platforms only.
/// By default, the slow generic algorithm is used.
/// If the target platform is little-endian, the user can enable this option to use the optimized algorithm.
#ifndef CANARD_DSDL_CONFIG_LITTLE_ENDIAN
#    define CANARD_DSDL_CONFIG_LITTLE_ENDIAN false
#endif

/// By default, this macro resolves to the standard assert(). The user can redefine this if necessary.
/// To disable assertion checks completely, make it expand into `(void)(0)`.
#ifndef CANARD_ASSERT
// Intentional violation of MISRA: assertion macro cannot be replaced with a function definition.
#    define CANARD_ASSERT(x) assert(x)  // NOSONAR
#endif

/// This macro is needed only for testing and for library development. Do not redefine this in production.
#if defined(CANARD_CONFIG_EXPOSE_PRIVATE) && CANARD_CONFIG_EXPOSE_PRIVATE
#    define CANARD_PRIVATE
#else
#    define CANARD_PRIVATE static inline
#endif

#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 201112L)
#    error "Unsupported language: ISO C11 or a newer version is required."
#endif

// --------------------------------------------- COMMON CONSTANTS ---------------------------------------------

/// Per the DSDL specification, it is assumed that 1 byte = 8 bits.
#define BYTE_WIDTH 8U
#define BYTE_MAX 0xFFU

#define WIDTH_BIT 1U
#define WIDTH_8 8U
#define WIDTH_16 16U
#define WIDTH_32 32U
#define WIDTH_64 64U
#define WIDTH_MAX WIDTH_64

// --------------------------------------------- PRIMITIVE SERIALIZATION ---------------------------------------------

#if CANARD_DSDL_PLATFORM_TWOS_COMPLEMENT

CANARD_PRIVATE size_t chooseMin(size_t a, size_t b)
{
    return (a < b) ? a : b;
}

/// The algorithm was originally designed by Ben Dyer for Libuavcan v0:
/// https://github.com/UAVCAN/libuavcan/blob/ba696029f9625d7ea3eb00/libuavcan/src/marshal/uc_bit_array_copy.cpp#L12-L58
/// This version is modified for v1 where the bit order is the opposite.
/// If both offsets and the length are byte-aligned, the algorithm degenerates into memcpy().
/// The source and the destination shall not overlap.
CANARD_PRIVATE void copyBitArray(const size_t         length_bit,
                                 const size_t         src_offset_bit,
                                 const size_t         dst_offset_bit,
                                 const uint8_t* const src,
                                 uint8_t* const       dst)
{
    CANARD_ASSERT((src != NULL) && (dst != NULL) && (src != dst));
    CANARD_ASSERT((src < dst) ? ((src + ((src_offset_bit + length_bit + BYTE_WIDTH) / BYTE_WIDTH)) <= dst)
                              : ((dst + ((dst_offset_bit + length_bit + BYTE_WIDTH) / BYTE_WIDTH)) <= src));
    if (((length_bit % BYTE_WIDTH) == 0U) &&      //
        ((src_offset_bit % BYTE_WIDTH) == 0U) &&  //
        ((dst_offset_bit % BYTE_WIDTH) == 0U))
    {
        // Intentional violation of MISRA: Pointer arithmetics.
        // This is done to remove the API constraint that offsets be under 8 bits.
        // Fewer constraints reduce the chance of API misuse.
        (void) memcpy(dst + (dst_offset_bit / BYTE_WIDTH),  // NOSONAR NOLINT
                      src + (src_offset_bit / BYTE_WIDTH),  // NOSONAR
                      length_bit / BYTE_WIDTH);
    }
    else
    {
        size_t       src_off  = src_offset_bit;
        size_t       dst_off  = dst_offset_bit;
        const size_t last_bit = src_off + length_bit;
        while (last_bit > src_off)
        {
            const uint8_t src_mod = (uint8_t)(src_off % BYTE_WIDTH);
            const uint8_t dst_mod = (uint8_t)(dst_off % BYTE_WIDTH);
            const uint8_t max_mod = (src_mod > dst_mod) ? src_mod : dst_mod;

            const size_t size = chooseMin(BYTE_WIDTH - max_mod, last_bit - src_off);
            CANARD_ASSERT((size > 0U) && (size <= BYTE_WIDTH));

            const uint8_t mask = (uint8_t)((((1U << size) - 1U) << dst_mod) & BYTE_MAX);
            CANARD_ASSERT((mask > 0U) && (mask <= BYTE_MAX));

            // Intentional violation of MISRA: indexing on a pointer.
            // This simplifies the implementation greatly and avoids pointer arithmetics.
            const uint8_t in =
                (uint8_t)((uint8_t)(src[src_off / BYTE_WIDTH] >> src_mod) << dst_mod) & BYTE_MAX;  // NOSONAR

            // Intentional violation of MISRA: indexing on a pointer.
            // This simplifies the implementation greatly and avoids pointer arithmetics.
            const uint8_t a = dst[dst_off / BYTE_WIDTH] & ((uint8_t) ~mask);  // NOSONAR
            const uint8_t b = in & mask;

            // Intentional violation of MISRA: indexing on a pointer.
            // This simplifies the implementation greatly and avoids pointer arithmetics.
            dst[dst_off / BYTE_WIDTH] = a | b;  // NOSONAR

            src_off += size;
            dst_off += size;
        }
        CANARD_ASSERT(last_bit == src_off);
    }
}

#    if CANARD_DSDL_CONFIG_LITTLE_ENDIAN

size_t canardDSDLPrimitiveSerialize(uint8_t* const    destination,
                                    const size_t      offset_bit,
                                    const uint8_t     length_bit,
                                    const void* const value)
{
    size_t out = 0U;
    if ((destination != NULL) && (value != NULL) && (length_bit > 0U) && (length_bit <= WIDTH_MAX))
    {
        // Intentional violation of MISRA: void cast is necessary due to inherent limitations of C.
        copyBitArray(length_bit, 0U, offset_bit, (const uint8_t*) value, destination);  // NOSONAR
        out = offset_bit + length_bit;
    }
    return out;
}

#    else

size_t canardDSDLPrimitiveSerialize(uint8_t* const    destination,
                                    const size_t      offset_bit,
                                    const uint8_t     length_bit,
                                    const void* const value)
{
    size_t out = 0U;
    if ((destination != NULL) && (value != NULL))
    {
        bool    success                        = true;
        uint8_t buffer[WIDTH_MAX / BYTE_WIDTH] = {0};
        if (length_bit == WIDTH_BIT)
        {
            // Intentional violation of MISRA: void cast is necessary due to inherent limitations of C.
            buffer[0] = (*((const bool*) value) != 0) ? 1U : 0U;  // NOSONAR
        }
        else if (length_bit <= WIDTH_8)
        {
            // Intentional violation of MISRA: void cast is necessary due to inherent limitations of C.
            buffer[0] = *((const uint8_t*) value);  // NOSONAR
        }
        else if (length_bit <= WIDTH_16)
        {
            uint16_t x = *(const uint16_t*) value;
            buffer[0]  = (uint8_t)(x & BYTE_MAX);
            x >>= BYTE_WIDTH;
            buffer[1] = (uint8_t)(x & BYTE_MAX);
        }
        else if (length_bit <= WIDTH_32)
        {
            uint32_t x = *(const uint32_t*) value;
            size_t   i = 0;
            while (x > 0U)
            {
                buffer[i] = (uint8_t)(x & BYTE_MAX);
                x >>= BYTE_WIDTH;
                ++i;
            }
        }
        else if (length_bit <= WIDTH_64)
        {
            uint64_t x = *(const uint64_t*) value;  // Stuff like this makes one miss generics.
            size_t   i = 0;                         // What am I doing with my life?
            while (x > 0U)                          // Copy-pasting code like we can't automate it away in 2020.
            {
                buffer[i] = (uint8_t)(x & BYTE_MAX);
                x >>= BYTE_WIDTH;
                ++i;
            }
        }
        else
        {
            success = false;
        }

        if (success)
        {
            copyBitArray(length_bit, 0U, offset_bit, &buffer[0], destination);
            out = offset_bit + length_bit;
        }
    }
    return out;
}

#    endif  // CANARD_DSDL_CONFIG_LITTLE_ENDIAN

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
