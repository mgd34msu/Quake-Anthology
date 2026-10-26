/* Port of src/foundation/binary.ts. GPL-2.0-or-later. */
#include "qa/foundation/binary.h"

#include <string.h>

#include "qa/foundation/support.h"

uint32_t qa_float32_bits(double value) {
  float narrowed = (float)value;
  uint32_t bits;
  memcpy(&bits, &narrowed, sizeof bits);
  return bits;
}

float qa_float32_from_bits(uint32_t bits) {
  float value;
  memcpy(&value, &bits, sizeof value);
  return value;
}

double qa_float64_from_bits(uint64_t bits) {
  double value;
  memcpy(&value, &bits, sizeof value);
  return value;
}

uint64_t qa_float64_bits(double value) {
  uint64_t bits;
  memcpy(&bits, &value, sizeof bits);
  return bits;
}

static uint8_t byte_at(const uint8_t *data, size_t data_length, size_t offset) {
  if (offset >= data_length) {
    qa_range_error("Binary load offset is outside its checked span");
  }
  return data[offset];
}

uint16_t qa_load_u16_le(const uint8_t *data, size_t data_length, size_t offset) {
  return (uint16_t)(byte_at(data, data_length, offset) |
                    (byte_at(data, data_length, offset + 1) << 8));
}

uint32_t qa_load_u32_le(const uint8_t *data, size_t data_length, size_t offset) {
  return (uint32_t)byte_at(data, data_length, offset) |
         ((uint32_t)byte_at(data, data_length, offset + 1) << 8) |
         ((uint32_t)byte_at(data, data_length, offset + 2) << 16) |
         ((uint32_t)byte_at(data, data_length, offset + 3) << 24);
}

int32_t qa_load_i32_le(const uint8_t *data, size_t data_length, size_t offset) {
  uint32_t bits = qa_load_u32_le(data, data_length, offset);
  int32_t value;
  memcpy(&value, &bits, sizeof value);
  return value;
}

float qa_load_f32_le(const uint8_t *data, size_t data_length, size_t offset) {
  return qa_float32_from_bits(qa_load_u32_le(data, data_length, offset));
}

static void store_byte(uint8_t *data, size_t data_length, size_t offset, uint8_t value) {
  /* JavaScript typed-array stores outside the span are silently dropped. */
  if (offset < data_length) data[offset] = value;
}

void qa_store_u16_le(uint8_t *data, size_t data_length, size_t offset, uint16_t value) {
  store_byte(data, data_length, offset, (uint8_t)(value & 0xffu));
  store_byte(data, data_length, offset + 1, (uint8_t)((value >> 8) & 0xffu));
}

void qa_store_u32_le(uint8_t *data, size_t data_length, size_t offset, uint32_t value) {
  store_byte(data, data_length, offset, (uint8_t)(value & 0xffu));
  store_byte(data, data_length, offset + 1, (uint8_t)((value >> 8) & 0xffu));
  store_byte(data, data_length, offset + 2, (uint8_t)((value >> 16) & 0xffu));
  store_byte(data, data_length, offset + 3, (uint8_t)((value >> 24) & 0xffu));
}

void qa_store_f32_le(uint8_t *data, size_t data_length, size_t offset, double value) {
  qa_store_u32_le(data, data_length, offset, qa_float32_bits(value));
}
