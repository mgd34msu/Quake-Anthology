/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QA_HASH_H
#define QA_HASH_H

#include "qa/common.h"

typedef struct qa_sha256_digest { uint8_t bytes[32]; } qa_sha256_digest;
typedef struct qa_md4_digest { uint8_t bytes[16]; } qa_md4_digest;

typedef struct qa_sha256_context {
    uint32_t state[8];
    uint64_t length;
    size_t used;
    uint8_t block[64];
} qa_sha256_context;

typedef struct qa_md4_context {
    uint32_t state[4];
    uint64_t length;
    size_t used;
    uint8_t block[64];
} qa_md4_context;

/* Update accepts checked byte spans. Final consumes and clears the context. */
void qa_sha256_init(qa_sha256_context *context);
void qa_sha256_update(qa_sha256_context *context, qa_bytes bytes);
void qa_sha256_final(qa_sha256_context *context, qa_sha256_digest *out);
void qa_sha256(qa_bytes bytes, qa_sha256_digest *out);
bool qa_sha256_equal(const qa_sha256_digest *left, const qa_sha256_digest *right);
void qa_sha256_hex(const qa_sha256_digest *digest, char out[65]);
/* Accepts 64 hex digits with an optional "sha256:" prefix. */
bool qa_sha256_parse(const char *text, qa_sha256_digest *out, qa_error *error);

/* MD4 is required by original content/network checksums, not authentication. */
void qa_md4_init(qa_md4_context *context);
void qa_md4_update(qa_md4_context *context, qa_bytes bytes);
void qa_md4_final(qa_md4_context *context, qa_md4_digest *out);
void qa_md4(qa_bytes bytes, qa_md4_digest *out);
uint32_t qa_md4_fold(const qa_md4_digest *digest);
uint32_t qa_block_checksum(qa_bytes bytes);
uint32_t qa_block_checksum_key(qa_bytes bytes, uint32_t key);

#endif
