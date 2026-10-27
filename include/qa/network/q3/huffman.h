/* Port of src/network/q3/huffman.ts (huffman.c adaptive tree). GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_HUFFMAN_H
#define QA_NETWORK_Q3_HUFFMAN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef void (*qa_q3_huffman_put_bit_fn)(void *ctx, int bit);
typedef int (*qa_q3_huffman_get_bit_fn)(void *ctx);
/* TS HuffmanCodec. */
typedef struct qa_q3_huffman_codec {
  void (*encode_symbol)(void *ctx, int32_t symbol, qa_q3_huffman_put_bit_fn put, void *put_ctx);
  int32_t (*decode_symbol)(void *ctx, qa_q3_huffman_get_bit_fn get, void *get_ctx);
  void *ctx;
} qa_q3_huffman_codec;
/* TS HuffmanTree. Opaque; init/destroy own node/block pools. */
typedef struct qa_q3_huffman_tree qa_q3_huffman_tree;
bool qa_q3_huffman_tree_init(qa_q3_huffman_tree **out);
void qa_q3_huffman_tree_destroy(qa_q3_huffman_tree *t);
void qa_q3_huffman_tree_add_reference(qa_q3_huffman_tree *t, int32_t symbol);
void qa_q3_huffman_tree_encode_symbol(qa_q3_huffman_tree *t, int32_t symbol, qa_q3_huffman_put_bit_fn put, void *put_ctx);
int32_t qa_q3_huffman_tree_decode_prefix(qa_q3_huffman_tree *t, qa_q3_huffman_get_bit_fn get, void *get_ctx);
int32_t qa_q3_huffman_tree_decode_symbol(qa_q3_huffman_tree *t, qa_q3_huffman_get_bit_fn get, void *get_ctx);
void qa_q3_huffman_tree_as_codec(qa_q3_huffman_tree *t, qa_q3_huffman_codec *out);
/* Shared trained MSG codec; decode_symbol is decodePrefix (TS). Never freed. */
const qa_q3_huffman_codec *qa_q3_create_message_huffman(void);
bool qa_q3_compress_adaptive(const uint8_t *data, size_t len, uint8_t **out, size_t *out_len);
bool qa_q3_decompress_adaptive(const uint8_t *data, size_t len, int64_t max_length, uint8_t **out, size_t *out_len);
bool qa_q3_decompress_adaptive_default(const uint8_t *data, size_t len, uint8_t **out, size_t *out_len);
#endif
