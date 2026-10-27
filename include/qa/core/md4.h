/*
 * Port of src/core/md4.ts: RSA Data Security, Inc. MD4 Message-Digest
 * Algorithm as used by id Software's code/qcommon/md4.c, plus the folded
 * block checksums. All arithmetic is uint32 wraparound, matching the
 * TypeScript `>>> 0` operations exactly. GPL-2.0-or-later.
 */
#ifndef QA_CORE_MD4_H
#define QA_CORE_MD4_H

#include <stddef.h>
#include <stdint.h>

typedef struct qa_md4_context {
  uint32_t state_a;
  uint32_t state_b;
  uint32_t state_c;
  uint32_t state_d;
  uint32_t count[2];
  uint8_t buffer[64];
} qa_md4_context;

/* A fresh context is already initialized, like the TS class fields. */
void qa_md4_context_initialize(qa_md4_context *context);
void qa_md4_context_update(qa_md4_context *context, const uint8_t *data,
                           size_t length);
/* Writes the 16-byte digest and zeroes the context, as the TS does. */
void qa_md4_context_final(qa_md4_context *context, uint8_t out_digest[16]);

void qa_md4(const uint8_t *data, size_t length, uint8_t out_digest[16]);
uint32_t qa_block_checksum(const uint8_t *data, size_t length);
/* `key` is validated like the TS: integer in int32/uint32 range. */
uint32_t qa_block_checksum_key(const uint8_t *data, size_t length, int64_t key);

#endif
