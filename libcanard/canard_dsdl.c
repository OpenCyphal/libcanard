// This software is distributed under the terms of the MIT License.
// Copyright (c) 2016-2020 UAVCAN Development Team.
// Author: Pavel Kirienko <pavel.kirienko@zubax.com>

#include "canard_dsdl.h"
#include <assert.h>
#include <float.h>
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

#define CANARD_DSDL_PLATFORM_IEEE754                                                              \
    ((FLT_RADIX == 2) && (FLT_MANT_DIG == 24) && (FLT_MIN_EXP == -125) && (FLT_MAX_EXP == 128) && \
     (DBL_MANT_DIG == 53) && (DBL_MIN_EXP == -1021) && (DBL_MAX_EXP == 1024))
_Static_assert(CANARD_DSDL_PLATFORM_IEEE754,
               "Currently, the module requires that the target platform shall use an IEEE 754-compatible floating "
               "point model. It is possible to support other platforms, but this has not been done yet. "
               "If your platform is not IEEE 754-compatible, please reach the maintainers via http://forum.uavcan.org");

// --------------------------------------------- COMMON ITEMS ---------------------------------------------

/// Per the DSDL specification, it is assumed that 1 byte = 8 bits.
#define BYTE_WIDTH 8U
#define BYTE_MAX 0xFFU

#define WIDTH16 16U
#define WIDTH32 32U
#define WIDTH64 64U

// --------------------------------------------- FLOAT16 SUPPORT ---------------------------------------------

_Static_assert(WIDTH32 == sizeof(CanardDSDLFloat32) * BYTE_WIDTH, "Unsupported floating point model");
_Static_assert(WIDTH64 == sizeof(CanardDSDLFloat64) * BYTE_WIDTH, "Unsupported floating point model");

// Intentional violation of MISRA: we need this union because the alternative is far more error prone.
// We have to rely on low-level data representation details to do the conversion; unions are helpful.
typedef union  // NOSONAR
{
    uint32_t          bits;
    CanardDSDLFloat32 real;
} Float32Bits;
_Static_assert(4 == sizeof(Float32Bits), "Unsupported float model");

CANARD_PRIVATE uint16_t float16Pack(const CanardDSDLFloat32 value);
CANARD_PRIVATE uint16_t float16Pack(const CanardDSDLFloat32 value)
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

CANARD_PRIVATE CanardDSDLFloat32 float16Unpack(const uint16_t value);
CANARD_PRIVATE CanardDSDLFloat32 float16Unpack(const uint16_t value)
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

// --------------------------------------------- PRIMITIVE SERIALIZATION ---------------------------------------------

CANARD_PRIVATE size_t chooseMin(size_t a, size_t b);
CANARD_PRIVATE size_t chooseMin(size_t a, size_t b)
{
    return (a < b) ? a : b;
}

/// The algorithm was originally designed by Ben Dyer for Libuavcan v0:
/// https://github.com/UAVCAN/libuavcan/blob/ba696029f9625d7ea3eb00/libuavcan/src/marshal/uc_bit_array_copy.cpp#L12-L58
/// This version is modified for v1 where the bit order is the opposite.
/// If both offsets and the length are byte-aligned, the algorithm degenerates to memcpy().
/// The source and the destination shall not overlap.
CANARD_PRIVATE void copyBitArray(const size_t         length_bit,
                                 const size_t         src_offset_bit,
                                 const size_t         dst_offset_bit,
                                 const uint8_t* const src,
                                 uint8_t* const       dst);
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

            const uint8_t size = (uint8_t) chooseMin(BYTE_WIDTH - max_mod, last_bit - src_off);
            CANARD_ASSERT((size > 0U) && (size <= BYTE_WIDTH));

            const uint8_t mask = (uint8_t)((((1U << size) - 1U) << dst_mod) & BYTE_MAX);
            CANARD_ASSERT(mask > 0U);

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

CANARD_PRIVATE size_t getBitCopySize(const size_t buf_size_bytes,
                                     const size_t offset_bit,
                                     const size_t copy_length_bit);
CANARD_PRIVATE size_t getBitCopySize(const size_t buf_size_bytes, const size_t offset_bit, const size_t copy_length_bit)
{
    const size_t buf_size_bit  = buf_size_bytes * BYTE_WIDTH;
    const size_t remaining_bit = buf_size_bit - chooseMin(buf_size_bit, offset_bit);
    return chooseMin(remaining_bit, copy_length_bit);
}

// --------------------------------------------- PUBLIC API ---------------------------------------------

void canardDSDLSetBit(uint8_t* const buf, const size_t off_bit, const bool value)
{
    CANARD_ASSERT(buf != NULL);
    const uint8_t x = value ? 1U : 0U;
    copyBitArray(1U, 0U, off_bit, &x, buf);
}

void canardDSDLSetUxx(uint8_t* const buf, const size_t off_bit, const uint64_t value, const uint8_t len_bit)
{
    _Static_assert(WIDTH64 == sizeof(uint64_t) * BYTE_WIDTH, "Unexpected size of uint64_t");
    CANARD_ASSERT(buf != NULL);
    const size_t saturated_len_bit = chooseMin(len_bit, WIDTH64);
#if CANARD_DSDL_CONFIG_LITTLE_ENDIAN
    copyBitArray(saturated_len_bit, 0U, off_bit, (const uint8_t*) &value, buf);
#else
    uint8_t  tmp[sizeof(uint64_t)] = {0};
    uint64_t x                     = value;
    size_t   i                     = 0;
    while (x > 0U)  // This conversion is independent of the native byte order. Slow but works everywhere.
    {
        tmp[i] = (uint8_t)(x & BYTE_MAX);
        x >>= BYTE_WIDTH;
        ++i;
    }
    copyBitArray(saturated_len_bit, 0U, off_bit, &tmp[0], buf);
#endif
}

void canardDSDLSetIxx(uint8_t* const buf, const size_t off_bit, const int64_t value, const uint8_t len_bit)
{
    // The naive sign conversion seems to be safe and portable according to the C standard:
    // 6.3.1.3.3: if the new type is unsigned, the value is converted by repeatedly adding or subtracting one more
    // than the maximum value that can be represented in the new type until the value is in the range of the new type.
    canardDSDLSetUxx(buf, off_bit, (uint64_t) value, len_bit);
}

void canardDSDLSetF16(uint8_t* const buf, const size_t off_bit, const CanardDSDLFloat32 value)
{
    canardDSDLSetUxx(buf, off_bit, float16Pack(value), WIDTH16);
}

void canardDSDLSetF32(uint8_t* const buf, const size_t off_bit, const CanardDSDLFloat32 value)
{
    // Intentional violation of MISRA: use union to perform fast conversion from an IEEE 754-compatible native
    // representation into a serializable integer. The assumptions about the target platform properties are made
    // clear. In the future we may add a more generic conversion that is platform-invariant.
    _Static_assert(CANARD_DSDL_PLATFORM_IEEE754, "IEEE 754 required");
    union  // NOSONAR
    {
        CanardDSDLFloat32 fl;
        uint32_t          in;
    } tmp = {value};  // NOSONAR
    _Static_assert(WIDTH32 == sizeof(tmp) * BYTE_WIDTH, "IEEE 754 required");
    canardDSDLSetUxx(buf, off_bit, tmp.in, sizeof(tmp) * BYTE_WIDTH);
}

void canardDSDLSetF64(uint8_t* const buf, const size_t off_bit, const CanardDSDLFloat64 value)
{
    // Intentional violation of MISRA: use union to perform fast conversion from an IEEE 754-compatible native
    // representation into a serializable integer. The assumptions about the target platform properties are made
    // clear. In the future we may add a more generic conversion that is platform-invariant.
    _Static_assert(CANARD_DSDL_PLATFORM_IEEE754, "IEEE 754 required");
    union  // NOSONAR
    {
        CanardDSDLFloat64 fl;
        uint64_t          in;
    } tmp = {value};  // NOSONAR
    _Static_assert(WIDTH64 == sizeof(tmp) * BYTE_WIDTH, "IEEE 754 required");
    canardDSDLSetUxx(buf, off_bit, tmp.in, sizeof(tmp) * BYTE_WIDTH);
}

bool canardDSDLGetBit(const uint8_t* const buf, const size_t buf_size, const size_t off_bit)
{
    return canardDSDLGetU8(buf, buf_size, off_bit, 1U) == 1U;
}

uint8_t canardDSDLGetU8(const uint8_t* const buf, const size_t buf_size, const size_t off_bit, const uint8_t len_bit)
{
    CANARD_ASSERT(buf != NULL);
    const size_t copy_size = getBitCopySize(buf_size, off_bit, chooseMin(len_bit, BYTE_WIDTH));
    uint8_t      x         = 0;
    copyBitArray(copy_size, off_bit, 0U, buf, &x);
    return x;
}

uint16_t canardDSDLGetU16(const uint8_t* const buf, const size_t buf_size, const size_t off_bit, const uint8_t len_bit)
{
    CANARD_ASSERT(buf != NULL);
    const size_t copy_size = getBitCopySize(buf_size, off_bit, chooseMin(len_bit, WIDTH16));
#if CANARD_DSDL_CONFIG_LITTLE_ENDIAN
    uint16_t x = 0U;
    copyBitArray(copy_size, off_bit, 0U, buf, (uint8_t*) &x);
    return x;
#else
    uint8_t tmp[sizeof(uint16_t)] = {0};
    copyBitArray(copy_size, off_bit, 0U, buf, &tmp[0]);
    return tmp[0] | (uint16_t)(((uint16_t) tmp[1]) << BYTE_WIDTH);
#endif
}

uint32_t canardDSDLGetU32(const uint8_t* const buf, const size_t buf_size, const size_t off_bit, const uint8_t len_bit)
{
    CANARD_ASSERT(buf != NULL);
    const size_t copy_size = getBitCopySize(buf_size, off_bit, chooseMin(len_bit, WIDTH32));
    uint32_t     x         = 0U;
#if CANARD_DSDL_CONFIG_LITTLE_ENDIAN
    copyBitArray(copy_size, off_bit, 0U, buf, (uint8_t*) &x);
#else
    uint8_t tmp[sizeof(uint32_t)] = {0};
    copyBitArray(copy_size, off_bit, 0U, buf, &tmp[0]);
    for (size_t i = sizeof(tmp); i > 0U; --i)
    {
        x <<= BYTE_WIDTH;
        CANARD_ASSERT(i > 0U);
        x |= tmp[i - 1U];
    }
#endif
    return x;
}

uint64_t canardDSDLGetU64(const uint8_t* const buf, const size_t buf_size, const size_t off_bit, const uint8_t len_bit)
{
    CANARD_ASSERT(buf != NULL);
    const size_t copy_size = getBitCopySize(buf_size, off_bit, chooseMin(len_bit, WIDTH64));
    uint64_t     x         = 0U;
#if CANARD_DSDL_CONFIG_LITTLE_ENDIAN
    copyBitArray(copy_size, off_bit, 0U, buf, (uint8_t*) &x);
#else
    uint8_t tmp[sizeof(uint64_t)] = {0};
    copyBitArray(copy_size, off_bit, 0U, buf, &tmp[0]);
    for (size_t i = sizeof(tmp); i > 0U; --i)
    {
        x <<= BYTE_WIDTH;
        CANARD_ASSERT(i > 0U);
        x |= tmp[i - 1U];
    }
#endif
    return x;
}

int8_t canardDSDLGetI8(const uint8_t* const buf, const size_t buf_size, const size_t off_bit, const uint8_t len_bit)
{
    uint8_t    u        = canardDSDLGetU8(buf, buf_size, off_bit, len_bit);
    const bool negative = (len_bit > 0U) && ((u & (1ULL << (len_bit - 1U))) != 0U);
    u |= ((len_bit < BYTE_WIDTH) && negative) ? ((uint8_t) ~((1ULL << len_bit) - 1U)) : 0U;
    return negative ? ((-(int8_t) ~u) - 1) : (int8_t) u;
}

int16_t canardDSDLGetI16(const uint8_t* const buf, const size_t buf_size, const size_t off_bit, const uint8_t len_bit)
{
    uint16_t   u        = canardDSDLGetU16(buf, buf_size, off_bit, len_bit);
    const bool negative = (len_bit > 0U) && ((u & (1ULL << (len_bit - 1U))) != 0U);
    u |= ((len_bit < WIDTH16) && negative) ? ((uint16_t) ~((1ULL << len_bit) - 1U)) : 0U;
    return negative ? ((-(int16_t) ~u) - 1) : (int16_t) u;
}

int32_t canardDSDLGetI32(const uint8_t* const buf, const size_t buf_size, const size_t off_bit, const uint8_t len_bit)
{
    uint32_t   u        = canardDSDLGetU32(buf, buf_size, off_bit, len_bit);
    const bool negative = (len_bit > 0U) && ((u & (1ULL << (len_bit - 1U))) != 0U);
    u |= ((len_bit < WIDTH32) && negative) ? ((uint32_t) ~((1ULL << len_bit) - 1U)) : 0U;
    return negative ? ((-(int32_t) ~u) - 1) : (int32_t) u;
}

int64_t canardDSDLGetI64(const uint8_t* const buf, const size_t buf_size, const size_t off_bit, const uint8_t len_bit)
{
    uint64_t   u        = canardDSDLGetU64(buf, buf_size, off_bit, len_bit);
    const bool negative = (len_bit > 0U) && ((u & (1ULL << (len_bit - 1U))) != 0U);
    u |= ((len_bit < WIDTH64) && negative) ? ((uint64_t) ~((1ULL << len_bit) - 1U)) : 0U;
    return negative ? ((-(int64_t) ~u) - 1) : (int64_t) u;
}

CanardDSDLFloat32 canardDSDLGetF16(const uint8_t* const buf, const size_t buf_size, const size_t off_bit)
{
    return float16Unpack(canardDSDLGetU16(buf, buf_size, off_bit, WIDTH16));
}

CanardDSDLFloat32 canardDSDLGetF32(const uint8_t* const buf, const size_t buf_size, const size_t off_bit)
{
    // Intentional violation of MISRA: use union to perform fast conversion to an IEEE 754-compatible native
    // representation into a serializable integer. The assumptions about the target platform properties are made
    // clear. In the future we may add a more generic conversion that is platform-invariant.
    _Static_assert(CANARD_DSDL_PLATFORM_IEEE754, "IEEE 754 required");
    union  // NOSONAR
    {
        uint32_t          in;
        CanardDSDLFloat32 fl;
    } tmp = {canardDSDLGetU32(buf, buf_size, off_bit, WIDTH32)};  // NOSONAR
    _Static_assert(WIDTH32 == sizeof(tmp) * BYTE_WIDTH, "IEEE 754 required");
    return tmp.fl;
}

CanardDSDLFloat64 canardDSDLGetF64(const uint8_t* const buf, const size_t buf_size, const size_t off_bit)
{
    // Intentional violation of MISRA: use union to perform fast conversion to an IEEE 754-compatible native
    // representation into a serializable integer. The assumptions about the target platform properties are made
    // clear. In the future we may add a more generic conversion that is platform-invariant.
    _Static_assert(CANARD_DSDL_PLATFORM_IEEE754, "IEEE 754 required");
    union  // NOSONAR
    {
        uint64_t          in;
        CanardDSDLFloat64 fl;
    } tmp = {canardDSDLGetU64(buf, buf_size, off_bit, WIDTH64)};  // NOSONAR
    _Static_assert(WIDTH64 == sizeof(tmp) * BYTE_WIDTH, "IEEE 754 required");
    return tmp.fl;
}
