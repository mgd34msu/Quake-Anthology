/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QA_BINARY_H
#define QA_BINARY_H

#include "qa/common.h"

/* Loads require a previously checked span of the corresponding width. */
uint16_t qa_load_u16le(const void *data);
uint32_t qa_load_u32le(const void *data);
uint64_t qa_load_u64le(const void *data);
int16_t qa_load_i16le(const void *data);
int32_t qa_load_i32le(const void *data);
float qa_load_f32le(const void *data);
void qa_store_u16le(void *data, uint16_t value);
void qa_store_u32le(void *data, uint32_t value);
void qa_store_u64le(void *data, uint64_t value);
/* Returns a borrowed view. The output remains unchanged on failure. */
bool qa_bytes_slice(qa_bytes source, size_t offset, size_t size,
                    qa_bytes *out, qa_error *error);

#endif
