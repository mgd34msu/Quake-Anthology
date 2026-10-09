#ifndef QA_BINARY_H
#define QA_BINARY_H

#include "qa/common.h"

#include <limits.h>
#include <string.h>

/* Loads require a previously checked span of the corresponding width. */
static inline uint16_t qa_load_u16le(const void *data)
{
    const uint8_t *bytes = data;
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static inline uint32_t qa_load_u32le(const void *data)
{
    const uint8_t *bytes = data;
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static inline uint64_t qa_load_u64le(const void *data)
{
    const uint8_t *bytes = data;
    return (uint64_t)qa_load_u32le(bytes) |
           ((uint64_t)qa_load_u32le(bytes + 4) << 32);
}

static inline int16_t qa_load_i16le(const void *data)
{
    uint16_t value = qa_load_u16le(data);
    return value <= INT16_MAX ? (int16_t)value :
           (int16_t)(-1 - (int32_t)(UINT16_MAX - value));
}

static inline int32_t qa_load_i32le(const void *data)
{
    uint32_t value = qa_load_u32le(data);
    return value <= INT32_MAX ? (int32_t)value :
           -1 - (int32_t)(UINT32_MAX - value);
}

static inline float qa_load_f32le(const void *data)
{
    uint32_t bits = qa_load_u32le(data);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void qa_store_u16le(void *data, uint16_t value);
void qa_store_u32le(void *data, uint32_t value);
void qa_store_u64le(void *data, uint64_t value);
void qa_store_f32le(void *data, float value);
/* Returns a borrowed view. The output remains unchanged on failure. */
bool qa_bytes_slice(qa_bytes source, size_t offset, size_t size,
                    qa_bytes *out, qa_error *error);

#endif
