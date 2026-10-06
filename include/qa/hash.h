#ifndef QA_HASH_H
#define QA_HASH_H

#include "qa/common.h"

typedef struct qa_md4_digest { uint8_t bytes[16]; } qa_md4_digest;

typedef struct qa_md4_context {
    uint32_t state[4];
    uint64_t length;
    size_t used;
    uint8_t block[64];
} qa_md4_context;

/* Original CRC_Block: CRC-16-CCITT with initial value 0xffff. */
uint16_t qa_crc_block(qa_bytes bytes);

/* MD4 is required by original content/network checksums, not authentication. */
void qa_md4_init(qa_md4_context *context);
void qa_md4_update(qa_md4_context *context, qa_bytes bytes);
void qa_md4_final(qa_md4_context *context, qa_md4_digest *out);
void qa_md4(qa_bytes bytes, qa_md4_digest *out);
uint32_t qa_md4_fold(const qa_md4_digest *digest);
uint32_t qa_block_checksum(qa_bytes bytes);
uint32_t qa_block_checksum_key(qa_bytes bytes, uint32_t key);

#endif
