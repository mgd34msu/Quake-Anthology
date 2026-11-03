/* Port of src/network/q3/huffman.ts. GPL-2.0-or-later. */
#include "qa/network/q3/huffman.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
#define NYT 256
#define INTERNAL 257
typedef struct { int leader; } block_t;
typedef struct { int left, right, parent, next, prev, block; int symbol; int64_t weight; } node_t;
struct qa_q3_huffman_tree {
  node_t *nodes; size_t nnodes, capnodes;
  block_t *blocks; size_t nblocks, capblocks;
  int nyt, root;
  int symbols[258]; /* symbol -> node or -1 */
};
static void check_byte(int32_t s) { if (s < 0 || s > 255) qa_range_error("Huffman symbol must be a byte"); }
static int new_block(qa_q3_huffman_tree *t, int leader) {
  if (t->nblocks == t->capblocks) {
    size_t nc = t->capblocks ? t->capblocks * 2 : 1024;
    block_t *nb = realloc(t->blocks, nc * sizeof *nb);
    if (!nb) qa_range_error("Huffman block allocation failed");
    t->blocks = nb; t->capblocks = nc;
  }
  t->blocks[t->nblocks].leader = leader;
  return (int)t->nblocks++;
}
static int new_node(qa_q3_huffman_tree *t, int symbol, int64_t weight) {
  if (t->nnodes == t->capnodes) {
    size_t nc = t->capnodes ? t->capnodes * 2 : 64;
    node_t *nn = realloc(t->nodes, nc * sizeof *nn);
    if (!nn) qa_range_error("Huffman node allocation failed");
    t->nodes = nn; t->capnodes = nc;
  }
  int id = (int)t->nnodes++;
  node_t *n = &t->nodes[id];
  n->left = n->right = n->parent = n->next = n->prev = -1;
  n->symbol = symbol; n->weight = weight;
  n->block = new_block(t, id);
  return id;
}
#define N(t, i) (&(t)->nodes[i])
static void swap_tree(qa_q3_huffman_tree *t, int a, int b) {
  int ap = N(t,a)->parent, bp = N(t,b)->parent;
  if (ap < 0) t->root = b; else if (N(t,ap)->left == a) N(t,ap)->left = b; else N(t,ap)->right = b;
  if (bp < 0) t->root = a; else if (N(t,bp)->left == b) N(t,bp)->left = a; else N(t,bp)->right = a;
  N(t,a)->parent = bp; N(t,b)->parent = ap;
}
static void swap_list(qa_q3_huffman_tree *t, int a, int b) {
  int next = N(t,a)->next; N(t,a)->next = N(t,b)->next; N(t,b)->next = next;
  int prev = N(t,a)->prev; N(t,a)->prev = N(t,b)->prev; N(t,b)->prev = prev;
  if (N(t,a)->next == a) N(t,a)->next = b;
  if (N(t,b)->next == b) N(t,b)->next = a;
  if (N(t,a)->next >= 0) N(t,N(t,a)->next)->prev = a;
  if (N(t,b)->next >= 0) N(t,N(t,b)->next)->prev = b;
  if (N(t,a)->prev >= 0) N(t,N(t,a)->prev)->next = a;
  if (N(t,b)->prev >= 0) N(t,N(t,b)->prev)->next = b;
}
static void increment(qa_q3_huffman_tree *t, int node) {
  if (node < 0) return;
  if (N(t,node)->next >= 0 && N(t,N(t,node)->next)->weight == N(t,node)->weight) {
    int leader = t->blocks[N(t,node)->block].leader;
    if (leader != N(t,node)->parent) swap_tree(t, leader, node);
    swap_list(t, leader, node);
  }
  if (N(t,node)->prev >= 0 && N(t,N(t,node)->prev)->weight == N(t,node)->weight)
    t->blocks[N(t,node)->block].leader = N(t,node)->prev;
  N(t,node)->weight++;
  if (N(t,node)->next >= 0 && N(t,N(t,node)->next)->weight == N(t,node)->weight)
    N(t,node)->block = N(t,N(t,node)->next)->block;
  else N(t,node)->block = new_block(t, node);
  if (N(t,node)->parent >= 0) {
    increment(t, N(t,node)->parent);
    if (N(t,node)->prev == N(t,node)->parent) {
      swap_list(t, node, N(t,node)->parent);
      if (t->blocks[N(t,node)->block].leader == node) t->blocks[N(t,node)->block].leader = N(t,node)->parent;
    }
  }
}
bool qa_q3_huffman_tree_init(qa_q3_huffman_tree **out) {
  qa_q3_huffman_tree *t = calloc(1, sizeof *t);
  if (!t) return false;
  for (int i = 0; i < 258; i++) t->symbols[i] = -1;
  t->nyt = new_node(t, NYT, 0);
  t->root = t->nyt;
  t->symbols[NYT] = t->nyt;
  *out = t; return true;
}
void qa_q3_huffman_tree_destroy(qa_q3_huffman_tree *t) { if (!t) return; free(t->nodes); free(t->blocks); free(t); }
void qa_q3_huffman_tree_add_reference(qa_q3_huffman_tree *t, int32_t symbol) {
  check_byte(symbol);
  int existing = t->symbols[symbol];
  if (existing >= 0) { increment(t, existing); return; }
  int leaf = new_node(t, symbol, 1);
  int branch = new_node(t, INTERNAL, 1);
  N(t,branch)->next = N(t,t->nyt)->next;
  if (N(t,branch)->next >= 0) {
    N(t,N(t,branch)->next)->prev = branch;
    if (N(t,N(t,branch)->next)->weight == 1) N(t,branch)->block = N(t,N(t,branch)->next)->block;
  }
  N(t,t->nyt)->next = branch; N(t,branch)->prev = t->nyt;
  N(t,leaf)->next = branch; N(t,branch)->prev = leaf;
  N(t,leaf)->block = N(t,branch)->block;
  N(t,t->nyt)->next = leaf; N(t,leaf)->prev = t->nyt;
  int parent = N(t,t->nyt)->parent;
  if (parent < 0) t->root = branch;
  else if (N(t,parent)->left == t->nyt) N(t,parent)->left = branch;
  else N(t,parent)->right = branch;
  N(t,branch)->right = leaf; N(t,branch)->left = t->nyt; N(t,branch)->parent = parent;
  N(t,t->nyt)->parent = branch; N(t,leaf)->parent = branch;
  t->symbols[symbol] = leaf;
  increment(t, parent);
}
static void send_node(qa_q3_huffman_tree *t, int node, qa_q3_huffman_put_bit_fn put, void *ctx) {
  if (N(t,node)->parent < 0) return;
  send_node(t, N(t,node)->parent, put, ctx);
  put(ctx, N(t,N(t,node)->parent)->right == node ? 1 : 0);
}
void qa_q3_huffman_tree_encode_symbol(qa_q3_huffman_tree *t, int32_t symbol, qa_q3_huffman_put_bit_fn put, void *put_ctx) {
  check_byte(symbol);
  int node = t->symbols[symbol];
  if (node < 0) {
    send_node(t, t->nyt, put, put_ctx);
    for (int shift = 7; shift >= 0; shift--) put(put_ctx, (int)(((uint32_t)symbol >> shift) & 1u));
  } else send_node(t, node, put, put_ctx);
}
int32_t qa_q3_huffman_tree_decode_prefix(qa_q3_huffman_tree *t, qa_q3_huffman_get_bit_fn get, void *get_ctx) {
  int node = t->root;
  while (N(t,node)->symbol == INTERNAL) {
    int next = get(get_ctx) == 0 ? N(t,node)->left : N(t,node)->right;
    if (next < 0) qa_raise("Error", 0, 0, "Invalid Huffman tree");
    node = next;
  }
  return N(t,node)->symbol;
}
int32_t qa_q3_huffman_tree_decode_symbol(qa_q3_huffman_tree *t, qa_q3_huffman_get_bit_fn get, void *get_ctx) {
  int32_t prefix = qa_q3_huffman_tree_decode_prefix(t, get, get_ctx);
  if (prefix != NYT) return prefix;
  int32_t symbol = 0;
  for (int i = 0; i < 8; i++) symbol = (symbol << 1) | get(get_ctx);
  return symbol;
}
static void codec_encode(void *ctx, int32_t s, qa_q3_huffman_put_bit_fn p, void *pc) { qa_q3_huffman_tree_encode_symbol(ctx, s, p, pc); }
static int32_t codec_decode(void *ctx, qa_q3_huffman_get_bit_fn g, void *gc) { return qa_q3_huffman_tree_decode_symbol(ctx, g, gc); }
void qa_q3_huffman_tree_as_codec(qa_q3_huffman_tree *t, qa_q3_huffman_codec *out) {
  out->encode_symbol = codec_encode; out->decode_symbol = codec_decode; out->ctx = t;
}
static const int32_t MESSAGE_COUNTS[256] = {
  250315,41193,6292,7106,3730,3750,6110,23283,33317,6950,7838,9714,9257,17259,3949,1778,
  8288,1604,1590,1663,1100,1213,1238,1134,1749,1059,1246,1149,1273,4486,2805,3472,
  21819,1159,1670,1066,1043,1012,1053,1070,1726,888,1180,850,960,780,1752,3296,
  10630,4514,5881,2685,4650,3837,2093,1867,2584,1949,1972,940,1134,1788,1670,1206,
  5719,6128,7222,6654,3710,3795,1492,1524,2215,1140,1355,971,2180,1248,1328,1195,
  1770,1078,1264,1266,1168,965,1155,1186,1347,1228,1529,1600,2617,2048,2546,3275,
  2410,3585,2504,2800,2675,6146,3663,2840,14253,3164,2221,1687,3208,2739,3512,4796,
  4091,3515,5288,4016,7937,6031,5360,3924,4892,3743,4566,4807,5852,6400,6225,8291,
  23243,7838,7073,8935,5437,4483,3641,5256,5312,5328,5370,3492,2458,1694,1821,2121,
  1916,1149,1516,1367,1236,1029,1258,1104,1245,1006,1149,1025,1241,952,1287,997,
  1713,1009,1187,879,1099,929,1078,951,1656,930,1153,1030,1262,1062,1214,1060,
  1621,930,1106,912,1034,892,1158,990,1175,850,1121,903,1087,920,1144,1056,
  3462,2240,4397,12136,7758,1345,1307,3278,1950,886,1023,1112,1077,1042,1061,1071,
  1484,1001,1096,915,1052,995,1070,876,1111,851,1059,805,1112,923,1103,817,
  1899,1872,976,841,1127,956,1159,950,7791,954,1289,933,1127,3207,1020,927,
  1355,768,1040,745,952,805,1073,740,1013,805,1008,796,996,1057,11457,13504,
};
static int32_t msg_decode(void *ctx, qa_q3_huffman_get_bit_fn g, void *gc) { return qa_q3_huffman_tree_decode_prefix(ctx, g, gc); }
const qa_q3_huffman_codec *qa_q3_create_message_huffman(void) {
  static qa_q3_huffman_codec codec;
  static qa_q3_huffman_tree *tree = NULL;
  if (tree) return &codec;
  if (!qa_q3_huffman_tree_init(&tree)) qa_range_error("Huffman allocation failed");
  for (int s = 0; s < 256; s++) for (int32_t i = 0; i < MESSAGE_COUNTS[s]; i++) qa_q3_huffman_tree_add_reference(tree, s);
  codec.encode_symbol = codec_encode; codec.decode_symbol = msg_decode; codec.ctx = tree;
  return &codec;
}
typedef struct { uint8_t *buf; size_t position; } bitw_t;
static void bit_put(void *ctx, int bit) {
  bitw_t *w = ctx; size_t off = w->position >> 3;
  w->buf[off] = (uint8_t)(w->buf[off] | (uint8_t)(bit << (w->position & 7)));
  w->position++;
}
bool qa_q3_compress_adaptive(const uint8_t *data, size_t len, uint8_t **out, size_t *out_len) {
  if (len == 0) { *out = malloc(1); *out_len = 0; return *out != NULL; }
  if (len > 65535) qa_range_error("Adaptive Huffman length exceeds 16 bits");
  uint8_t *output = calloc(2 + len * 33 + 1, 1);
  if (!output) return false;
  output[0] = (uint8_t)(len >> 8); output[1] = (uint8_t)len; /* big-endian */
  bitw_t w = { output, 16 };
  qa_q3_huffman_tree *tree = NULL;
  if (!qa_q3_huffman_tree_init(&tree)) { free(output); return false; }
  for (size_t i = 0; i < len; i++) { qa_q3_huffman_tree_encode_symbol(tree, data[i], bit_put, &w); qa_q3_huffman_tree_add_reference(tree, data[i]); }
  qa_q3_huffman_tree_destroy(tree);
  *out_len = (w.position >> 3) + 1;
  *out = output; return true;
}
static void tree_cleanup(void *tree) { qa_q3_huffman_tree_destroy(tree); }
typedef struct { const uint8_t *buf; size_t len; size_t position; } bitr_t;
static int bit_get(void *ctx) {
  bitr_t *r = ctx;
  if (r->position >= r->len * 8) qa_range_error("Truncated Huffman symbol");
  int v = (r->buf[r->position >> 3] >> (r->position & 7)) & 1;
  r->position++; return v;
}
bool qa_q3_decompress_adaptive(const uint8_t *data, size_t len, int64_t max_length, uint8_t **out, size_t *out_len) {
  if (max_length < 0) qa_range_error("Invalid decompression limit");
  if (len == 0) { *out = malloc(1); *out_len = 0; return *out != NULL; }
  if (len < 2) qa_range_error("Truncated Huffman length");
  size_t length = (size_t)(((size_t)data[0] << 8) | data[1]);
  if ((int64_t)length > max_length) length = (size_t)max_length;
  uint8_t *output = malloc(length ? length : 1);
  if (!output) return false;
  bitr_t r = { data, len, 16 };
  qa_q3_huffman_tree *tree = NULL;
  if (!qa_q3_huffman_tree_init(&tree)) { free(output); return false; }
  /* "Truncated Huffman symbol" raises from remote connect payloads; a catching
   * qa_try frees the tree and partial output, as GC does in TS. */
  qa_cleanup output_guard, tree_guard;
  qa_cleanup_push(&output_guard, free, output);
  qa_cleanup_push(&tree_guard, tree_cleanup, tree);
  for (size_t i = 0; i < length; i++) {
    int32_t sym = qa_q3_huffman_tree_decode_symbol(tree, bit_get, &r);
    output[i] = (uint8_t)sym;
    qa_q3_huffman_tree_add_reference(tree, sym);
  }
  qa_cleanup_pop(&tree_guard);
  qa_cleanup_pop(&output_guard);
  qa_q3_huffman_tree_destroy(tree);
  *out = output; *out_len = length; return true;
}
bool qa_q3_decompress_adaptive_default(const uint8_t *data, size_t len, uint8_t **out, size_t *out_len) {
  return qa_q3_decompress_adaptive(data, len, 16384, out, out_len);
}
