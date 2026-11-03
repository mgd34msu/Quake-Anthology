/*
 * Port of src/foundation/binary.ts: allocation-free scalar byte
 * conversion. Loads/stores are plain arithmetic on caller bytes.
 * C accommodation: spans carry an explicit length (TypeScript typed
 * arrays carry theirs). Loads past the span abort like the TypeScript
 * RangeError; stores past the span drop bytes like JavaScript typed
 * arrays do. GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_BINARY_H
#define QA_FOUNDATION_BINARY_H

#include <stddef.h>
#include <stdint.h>

/* Rounds to binary32 and returns its IEEE-754 bit pattern as uint32. */
uint32_t qa_float32_bits(double value);
/* Returns the binary32 value stored in a uint32 bit pattern. */
float qa_float32_from_bits(uint32_t bits);
/* Returns the binary64 value stored in a uint64 bit pattern. */
double qa_float64_from_bits(uint64_t bits);
/* Returns the uint64 bit pattern of a binary64 value. */
uint64_t qa_float64_bits(double value);

/* Loads require a previously checked span of the corresponding width. */
uint16_t qa_load_u16_le(const uint8_t *data, size_t data_length, size_t offset);
uint32_t qa_load_u32_le(const uint8_t *data, size_t data_length, size_t offset);
int32_t qa_load_i32_le(const uint8_t *data, size_t data_length, size_t offset);
float qa_load_f32_le(const uint8_t *data, size_t data_length, size_t offset);
/* loadF64LE: little-endian binary64. */
double qa_load_f64_le(const uint8_t *data, size_t data_length, size_t offset);

void qa_store_u16_le(uint8_t *data, size_t data_length, size_t offset, uint16_t value);
void qa_store_u32_le(uint8_t *data, size_t data_length, size_t offset, uint32_t value);
void qa_store_f32_le(uint8_t *data, size_t data_length, size_t offset, double value);
/* storeF64LE: little-endian binary64 (low word first). */
void qa_store_f64_le(uint8_t *data, size_t data_length, size_t offset, double value);

#endif
