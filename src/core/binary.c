/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qa/binary.h"

#include <float.h>
#include <limits.h>
#include <string.h>

_Static_assert(CHAR_BIT == 8, "Quake formats require eight-bit bytes");
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
               FLT_MAX_EXP == 128, "Quake formats require binary32 floats");

uint16_t qa_load_u16le(const void *data)
{
    const uint8_t *bytes = data;
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

uint32_t qa_load_u32le(const void *data)
{
    const uint8_t *bytes = data;
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

uint64_t qa_load_u64le(const void *data)
{
    const uint8_t *bytes = data;
    return (uint64_t)qa_load_u32le(bytes) |
           ((uint64_t)qa_load_u32le(bytes + 4) << 32);
}

int16_t qa_load_i16le(const void *data)
{
    uint16_t value = qa_load_u16le(data);
    return value <= INT16_MAX ? (int16_t)value :
           (int16_t)(-1 - (int32_t)(UINT16_MAX - value));
}

int32_t qa_load_i32le(const void *data)
{
    uint32_t value = qa_load_u32le(data);
    return value <= INT32_MAX ? (int32_t)value :
           -1 - (int32_t)(UINT32_MAX - value);
}

float qa_load_f32le(const void *data)
{
    uint32_t bits = qa_load_u32le(data);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void qa_store_u16le(void *data, uint16_t value)
{
    uint8_t *bytes = data;
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

void qa_store_u32le(void *data, uint32_t value)
{
    uint8_t *bytes = data;
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

void qa_store_u64le(void *data, uint64_t value)
{
    uint8_t *bytes = data;
    qa_store_u32le(bytes, (uint32_t)value);
    qa_store_u32le(bytes + 4, (uint32_t)(value >> 32));
}

bool qa_bytes_slice(qa_bytes source, size_t offset, size_t size,
                    qa_bytes *out, qa_error *error)
{
    if (out == NULL || (source.data == NULL && source.size != 0)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, offset, "invalid byte span");
        return false;
    }
    if (size > source.size || offset > source.size - size) {
        qa_error_set(error, QA_ERROR_FORMAT, offset,
                     "range of %zu bytes exceeds %zu-byte input", size, source.size);
        return false;
    }
    *out = (qa_bytes){source.data == NULL ? NULL : source.data + offset, size};
    return true;
}
