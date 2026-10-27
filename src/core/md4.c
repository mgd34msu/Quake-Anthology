/* Port of src/core/md4.ts. GPL-2.0-or-later. */
#include "qa/core/md4.h"

#include <string.h>

#include "qa/foundation/support.h"

static uint32_t rotate_left(uint32_t value, unsigned shift) {
  return (value << shift) | (value >> (32u - shift));
}
static uint32_t f_fn(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) | (~x & z);
}
static uint32_t g_fn(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) | (x & z) | (y & z);
}
static uint32_t h_fn(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
static uint32_t ff(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t x,
                   unsigned shift) {
  return rotate_left(a + f_fn(b, c, d) + x, shift);
}
static uint32_t gg(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t x,
                   unsigned shift) {
  return rotate_left(a + g_fn(b, c, d) + x + 0x5a827999u, shift);
}
static uint32_t hh(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t x,
                   unsigned shift) {
  return rotate_left(a + h_fn(b, c, d) + x + 0x6ed9eba1u, shift);
}

static uint32_t load32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static void store32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static void transform(qa_md4_context *context, const uint8_t *block,
                      size_t block_length, size_t offset) {
  if (offset > block_length || block_length - offset < 64) {
    qa_range_error("MD4 block is truncated");
  }
  const uint8_t *p = block + offset;
  uint32_t x[16];
  for (int i = 0; i < 16; i++) x[i] = load32(p + 4 * i);
  uint32_t a = context->state_a, b = context->state_b, c = context->state_c,
           d = context->state_d;

  a = ff(a, b, c, d, x[0], 3);  d = ff(d, a, b, c, x[1], 7);
  c = ff(c, d, a, b, x[2], 11); b = ff(b, c, d, a, x[3], 19);
  a = ff(a, b, c, d, x[4], 3);  d = ff(d, a, b, c, x[5], 7);
  c = ff(c, d, a, b, x[6], 11); b = ff(b, c, d, a, x[7], 19);
  a = ff(a, b, c, d, x[8], 3);  d = ff(d, a, b, c, x[9], 7);
  c = ff(c, d, a, b, x[10], 11); b = ff(b, c, d, a, x[11], 19);
  a = ff(a, b, c, d, x[12], 3);  d = ff(d, a, b, c, x[13], 7);
  c = ff(c, d, a, b, x[14], 11); b = ff(b, c, d, a, x[15], 19);

  a = gg(a, b, c, d, x[0], 3);   d = gg(d, a, b, c, x[4], 5);
  c = gg(c, d, a, b, x[8], 9);   b = gg(b, c, d, a, x[12], 13);
  a = gg(a, b, c, d, x[1], 3);   d = gg(d, a, b, c, x[5], 5);
  c = gg(c, d, a, b, x[9], 9);   b = gg(b, c, d, a, x[13], 13);
  a = gg(a, b, c, d, x[2], 3);   d = gg(d, a, b, c, x[6], 5);
  c = gg(c, d, a, b, x[10], 9);  b = gg(b, c, d, a, x[14], 13);
  a = gg(a, b, c, d, x[3], 3);   d = gg(d, a, b, c, x[7], 5);
  c = gg(c, d, a, b, x[11], 9);  b = gg(b, c, d, a, x[15], 13);

  a = hh(a, b, c, d, x[0], 3);   d = hh(d, a, b, c, x[8], 9);
  c = hh(c, d, a, b, x[4], 11);  b = hh(b, c, d, a, x[12], 15);
  a = hh(a, b, c, d, x[2], 3);   d = hh(d, a, b, c, x[10], 9);
  c = hh(c, d, a, b, x[6], 11);  b = hh(b, c, d, a, x[14], 15);
  a = hh(a, b, c, d, x[1], 3);   d = hh(d, a, b, c, x[9], 9);
  c = hh(c, d, a, b, x[5], 11);  b = hh(b, c, d, a, x[13], 15);
  a = hh(a, b, c, d, x[3], 3);   d = hh(d, a, b, c, x[11], 9);
  c = hh(c, d, a, b, x[7], 11);  b = hh(b, c, d, a, x[15], 15);

  context->state_a += a;
  context->state_b += b;
  context->state_c += c;
  context->state_d += d;
}

static const uint8_t padding_bytes[64] = {0x80};

void qa_md4_context_initialize(qa_md4_context *context) {
  context->count[0] = 0;
  context->count[1] = 0;
  context->state_a = 0x67452301u;
  context->state_b = 0xefcdab89u;
  context->state_c = 0x98badcfeu;
  context->state_d = 0x10325476u;
  memset(context->buffer, 0, sizeof context->buffer);
}

void qa_md4_context_update(qa_md4_context *context, const uint8_t *data,
                           size_t length) {
  if (length > 0xffffffffu) {
    qa_range_error("MD4 update length exceeds unsigned int");
  }
  size_t index = (context->count[0] >> 3) & 63u;
  uint32_t added_bits = (uint32_t)((uint32_t)length << 3);
  uint32_t before = context->count[0];
  context->count[0] = before + added_bits;
  context->count[1] = context->count[1] +
                      (context->count[0] < added_bits ? 1u : 0u) +
                      (uint32_t)(length >> 29);
  size_t part_length = 64 - index;
  size_t offset = 0;
  if (length >= part_length) {
    memcpy(context->buffer + index, data, part_length);
    transform(context, context->buffer, sizeof context->buffer, 0);
    for (offset = part_length; offset + 63 < length; offset += 64) {
      transform(context, data, length, offset);
    }
    index = 0;
  }
  memcpy(context->buffer + index, data + offset, length - offset);
  (void)before;
}

void qa_md4_context_final(qa_md4_context *context, uint8_t out_digest[16]) {
  uint8_t bits[8];
  store32(bits, context->count[0]);
  store32(bits + 4, context->count[1]);
  size_t index = (context->count[0] >> 3) & 63u;
  qa_md4_context_update(context, padding_bytes,
                        index < 56 ? 56 - index : 120 - index);
  qa_md4_context_update(context, bits, sizeof bits);
  store32(out_digest, context->state_a);
  store32(out_digest + 4, context->state_b);
  store32(out_digest + 8, context->state_c);
  store32(out_digest + 12, context->state_d);
  context->state_a = 0;
  context->state_b = 0;
  context->state_c = 0;
  context->state_d = 0;
  context->count[0] = 0;
  context->count[1] = 0;
  memset(context->buffer, 0, sizeof context->buffer);
}

void qa_md4(const uint8_t *data, size_t length, uint8_t out_digest[16]) {
  qa_md4_context context;
  qa_md4_context_initialize(&context);
  qa_md4_context_update(&context, data, length);
  qa_md4_context_final(&context, out_digest);
}

static uint32_t fold_digest(const uint8_t digest[16]) {
  return load32(digest) ^ load32(digest + 4) ^ load32(digest + 8) ^
         load32(digest + 12);
}

uint32_t qa_block_checksum(const uint8_t *data, size_t length) {
  uint8_t digest[16];
  qa_md4(data, length, digest);
  return fold_digest(digest);
}

uint32_t qa_block_checksum_key(const uint8_t *data, size_t length,
                               int64_t key) {
  if (key < -0x80000000LL || key > 0xffffffffLL) {
    qa_range_error("Checksum key %lld is outside signed-int32/uint32 range",
                   (long long)key);
  }
  uint8_t key_bytes[4];
  store32(key_bytes, (uint32_t)key);
  qa_md4_context context;
  qa_md4_context_initialize(&context);
  qa_md4_context_update(&context, key_bytes, sizeof key_bytes);
  qa_md4_context_update(&context, data, length);
  uint8_t digest[16];
  qa_md4_context_final(&context, digest);
  return fold_digest(digest);
}
