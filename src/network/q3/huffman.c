#include "huffman_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct huffman_code { uint32_t bits; unsigned width; } huffman_code;
/* Seeded by MESSAGE_COUNTS in symbol order using the donor sibling-property
 * training algorithm. These immutable tables need no runtime training.
 * Branch zero is the root; negative children encode -(symbol + 1).
 * Symbol 256 is the source NYT leaf, retained for decoder semantics. */
static const huffman_code message_codes[256] = {
    { UINT32_C(0x2), 2 }, { UINT32_C(0x1b), 5 }, { UINT32_C(0x48), 7 }, { UINT32_C(0x6c), 7 },
    { UINT32_C(0xa1), 8 }, { UINT32_C(0x11), 8 }, { UINT32_C(0x10), 7 }, { UINT32_C(0x3f), 6 },
    { UINT32_C(0x15), 5 }, { UINT32_C(0x34), 7 }, { UINT32_C(0x69), 7 }, { UINT32_C(0xb), 7 },
    { UINT32_C(0x13), 7 }, { UINT32_C(0x2d), 6 }, { UINT32_C(0x39), 8 }, { UINT32_C(0xac), 9 },
    { UINT32_C(0x25), 7 }, { UINT32_C(0x58), 9 }, { UINT32_C(0x1f0), 9 }, { UINT32_C(0x1f8), 9 },
    { UINT32_C(0x1dd), 10 }, { UINT32_C(0x3f3), 10 }, { UINT32_C(0x22b), 10 }, { UINT32_C(0x323), 10 },
    { UINT32_C(0xf4), 9 }, { UINT32_C(0x18d), 10 }, { UINT32_C(0xab), 10 }, { UINT32_C(0x363), 10 },
    { UINT32_C(0x1eb), 10 }, { UINT32_C(0x43), 8 }, { UINT32_C(0x4f), 9 }, { UINT32_C(0xd4), 8 },
    { UINT32_C(0x37), 6 }, { UINT32_C(0xd3), 10 }, { UINT32_C(0x44), 9 }, { UINT32_C(0x2cd), 10 },
    { UINT32_C(0x3c5), 10 }, { UINT32_C(0x3f9), 10 }, { UINT32_C(0x30d), 10 }, { UINT32_C(0x3cd), 10 },
    { UINT32_C(0x94), 9 }, { UINT32_C(0x1ac), 10 }, { UINT32_C(0x33), 10 }, { UINT32_C(0x14), 10 },
    { UINT32_C(0x271), 10 }, { UINT32_C(0x2f0), 10 }, { UINT32_C(0x1f4), 9 }, { UINT32_C(0x78), 8 },
    { UINT32_C(0x27), 7 }, { UINT32_C(0xc3), 8 }, { UINT32_C(0xef), 8 }, { UINT32_C(0x197), 9 },
    { UINT32_C(0x53), 8 }, { UINT32_C(0xb1), 8 }, { UINT32_C(0xd), 9 }, { UINT32_C(0x161), 9 },
    { UINT32_C(0x7), 9 }, { UINT32_C(0xf1), 9 }, { UINT32_C(0x199), 9 }, { UINT32_C(0x191), 10 },
    { UINT32_C(0x123), 10 }, { UINT32_C(0xbc), 9 }, { UINT32_C(0x144), 9 }, { UINT32_C(0x1f3), 10 },
    { UINT32_C(0xcf), 8 }, { UINT32_C(0x50), 7 }, { UINT32_C(0x7c), 7 }, { UINT32_C(0x4), 7 },
    { UINT32_C(0x21), 8 }, { UINT32_C(0x51), 8 }, { UINT32_C(0x80), 9 }, { UINT32_C(0x70), 9 },
    { UINT32_C(0x13d), 9 }, { UINT32_C(0x63), 10 }, { UINT32_C(0x2d7), 10 }, { UINT32_C(0x371), 10 },
    { UINT32_C(0x19d), 9 }, { UINT32_C(0x2ab), 10 }, { UINT32_C(0x1c7), 10 }, { UINT32_C(0x333), 10 },
    { UINT32_C(0x12c), 9 }, { UINT32_C(0x9d), 10 }, { UINT32_C(0x16b), 10 }, { UINT32_C(0x36b), 10 },
    { UINT32_C(0x1d3), 10 }, { UINT32_C(0x171), 10 }, { UINT32_C(0x1e3), 10 }, { UINT32_C(0x233), 10 },
    { UINT32_C(0xd7), 10 }, { UINT32_C(0x2cb), 10 }, { UINT32_C(0x170), 9 }, { UINT32_C(0xa8), 9 },
    { UINT32_C(0xc7), 9 }, { UINT32_C(0x105), 9 }, { UINT32_C(0xeb), 9 }, { UINT32_C(0xd8), 8 },
    { UINT32_C(0xf3), 9 }, { UINT32_C(0x3c), 8 }, { UINT32_C(0x1ab), 9 }, { UINT32_C(0x18f), 9 },
    { UINT32_C(0x97), 9 }, { UINT32_C(0x30), 7 }, { UINT32_C(0x41), 8 }, { UINT32_C(0x14f), 9 },
    { UINT32_C(0x1c), 6 }, { UINT32_C(0x28), 8 }, { UINT32_C(0xbd), 9 }, { UINT32_C(0xc4), 9 },
    { UINT32_C(0x98), 8 }, { UINT32_C(0x8f), 9 }, { UINT32_C(0xc), 8 }, { UINT32_C(0xb3), 8 },
    { UINT32_C(0x85), 8 }, { UINT32_C(0x8c), 8 }, { UINT32_C(0x47), 8 }, { UINT32_C(0x79), 8 },
    { UINT32_C(0x59), 7 }, { UINT32_C(0x40), 7 }, { UINT32_C(0x17), 8 }, { UINT32_C(0x19), 8 },
    { UINT32_C(0x4b), 8 }, { UINT32_C(0xe1), 8 }, { UINT32_C(0xa3), 8 }, { UINT32_C(0x73), 8 },
    { UINT32_C(0x6f), 8 }, { UINT32_C(0x68), 7 }, { UINT32_C(0x8), 7 }, { UINT32_C(0x65), 7 },
    { UINT32_C(0x1f), 6 }, { UINT32_C(0x29), 7 }, { UINT32_C(0x4c), 7 }, { UINT32_C(0x7d), 7 },
    { UINT32_C(0xf), 8 }, { UINT32_C(0x83), 8 }, { UINT32_C(0x1), 8 }, { UINT32_C(0x87), 8 },
    { UINT32_C(0x67), 8 }, { UINT32_C(0xe7), 8 }, { UINT32_C(0x57), 8 }, { UINT32_C(0x74), 8 },
    { UINT32_C(0x1cb), 9 }, { UINT32_C(0x1c4), 9 }, { UINT32_C(0x81), 9 }, { UINT32_C(0x4d), 9 },
    { UINT32_C(0x131), 9 }, { UINT32_C(0x163), 10 }, { UINT32_C(0x180), 9 }, { UINT32_C(0x3d7), 10 },
    { UINT32_C(0x2b), 10 }, { UINT32_C(0x145), 10 }, { UINT32_C(0x6b), 10 }, { UINT32_C(0x3d), 10 },
    { UINT32_C(0x32b), 10 }, { UINT32_C(0xf9), 10 }, { UINT32_C(0xe3), 10 }, { UINT32_C(0x245), 10 },
    { UINT32_C(0x12b), 10 }, { UINT32_C(0x31), 10 }, { UINT32_C(0x3eb), 10 }, { UINT32_C(0x1b9), 10 },
    { UINT32_C(0x114), 9 }, { UINT32_C(0x1f9), 10 }, { UINT32_C(0x133), 10 }, { UINT32_C(0x2c), 10 },
    { UINT32_C(0x2dd), 10 }, { UINT32_C(0x1c1), 10 }, { UINT32_C(0x31d), 10 }, { UINT32_C(0x1d1), 10 },
    { UINT32_C(0x138), 9 }, { UINT32_C(0x61), 10 }, { UINT32_C(0x2e3), 10 }, { UINT32_C(0x345), 10 },
    { UINT32_C(0x26b), 10 }, { UINT32_C(0xcd), 10 }, { UINT32_C(0xcb), 10 }, { UINT32_C(0x14d), 10 },
    { UINT32_C(0x38), 9 }, { UINT32_C(0x3c1), 10 }, { UINT32_C(0x23d), 10 }, { UINT32_C(0x3bc), 10 },
    { UINT32_C(0xc5), 10 }, { UINT32_C(0x3ac), 10 }, { UINT32_C(0x3e3), 10 }, { UINT32_C(0x299), 10 },
    { UINT32_C(0x3d3), 10 }, { UINT32_C(0x214), 10 }, { UINT32_C(0x203), 10 }, { UINT32_C(0x1bc), 10 },
    { UINT32_C(0x29d), 10 }, { UINT32_C(0x381), 10 }, { UINT32_C(0x263), 10 }, { UINT32_C(0x8d), 10 },
    { UINT32_C(0x54), 8 }, { UINT32_C(0x103), 9 }, { UINT32_C(0x5d), 8 }, { UINT32_C(0x20), 6 },
    { UINT32_C(0x9), 7 }, { UINT32_C(0x3c7), 10 }, { UINT32_C(0x307), 10 }, { UINT32_C(0xb8), 8 },
    { UINT32_C(0x1f1), 9 }, { UINT32_C(0x22c), 10 }, { UINT32_C(0x45), 10 }, { UINT32_C(0x3), 10 },
    { UINT32_C(0x11d), 10 }, { UINT32_C(0x1c5), 10 }, { UINT32_C(0x34d), 10 }, { UINT32_C(0x1d), 10 },
    { UINT32_C(0x0), 9 }, { UINT32_C(0x3b9), 10 }, { UINT32_C(0xdd), 10 }, { UINT32_C(0x181), 10 },
    { UINT32_C(0x10d), 10 }, { UINT32_C(0xb9), 10 }, { UINT32_C(0x1cd), 10 }, { UINT32_C(0x394), 10 },
    { UINT32_C(0x1bd), 10 }, { UINT32_C(0x194), 10 }, { UINT32_C(0x38d), 10 }, { UINT32_C(0x158), 10 },
    { UINT32_C(0x3bd), 10 }, { UINT32_C(0xc1), 10 }, { UINT32_C(0x3dd), 10 }, { UINT32_C(0xf8), 10 },
    { UINT32_C(0xd1), 9 }, { UINT32_C(0x91), 9 }, { UINT32_C(0x99), 10 }, { UINT32_C(0x2f8), 10 },
    { UINT32_C(0x23), 10 }, { UINT32_C(0x71), 10 }, { UINT32_C(0x2d3), 10 }, { UINT32_C(0x391), 10 },
    { UINT32_C(0x49), 7 }, { UINT32_C(0x231), 10 }, { UINT32_C(0x107), 10 }, { UINT32_C(0x261), 10 },
    { UINT32_C(0x223), 10 }, { UINT32_C(0x18), 8 }, { UINT32_C(0x205), 10 }, { UINT32_C(0x2c1), 10 },
    { UINT32_C(0x1d7), 10 }, { UINT32_C(0xf0), 10 }, { UINT32_C(0x2c5), 10 }, { UINT32_C(0x300), 10 },
    { UINT32_C(0x3d1), 10 }, { UINT32_C(0x3a8), 10 }, { UINT32_C(0x21d), 10 }, { UINT32_C(0x500), 11 },
    { UINT32_C(0x5), 10 }, { UINT32_C(0x358), 10 }, { UINT32_C(0x2f9), 10 }, { UINT32_C(0x1a8), 10 },
    { UINT32_C(0x2b9), 10 }, { UINT32_C(0x28d), 10 }, { UINT32_C(0x2f), 7 }, { UINT32_C(0x24), 6 },
};

static const int16_t message_branches[256][2] = {
    { 1, 71 }, { 2, -1 }, { 3, 38 }, { 4, 19 }, { 5, 12 }, { 6, -196 },
    { 7, -118 }, { 8, 11 }, { -209, 9 }, { 10, -244 }, { -257, -248 }, { -71, -147 },
    { 13, 14 }, { -7, -66 }, { -102, 15 }, { 16, 17 }, { -72, -91 }, { 18, -19 },
    { -242, -46 }, { 20, 26 }, { 21, 22 }, { -127, -3 }, { 23, -126 }, { -106, 24 },
    { -92, 25 }, { -252, -246 }, { 27, 32 }, { 28, 29 }, { -238, -109 }, { 30, -96 },
    { -18, 31 }, { -220, -250 }, { 33, 35 }, { 34, -200 }, { -177, -169 }, { -48, 36 },
    { 37, -20 }, { -224, -228 }, { 39, 56 }, { 40, 45 }, { 41, -256 }, { -68, 42 },
    { 43, 44 }, { -35, -63 }, { -108, -142 }, { 46, 53 }, { 47, 52 }, { 48, 50 },
    { 49, -161 }, { -44, -186 }, { -41, 51 }, { -218, -216 }, { -193, -32 }, { -10, 54 },
    { -140, 55 }, { -25, -47 }, { 57, 66 }, { 58, 60 }, { 59, -131 }, { -111, -114 },
    { 61, -4 }, { 62, 64 }, { 63, -81 }, { -164, -202 }, { -16, 65 }, { -42, -182 },
    { -105, 67 }, { 68, -67 }, { -98, 69 }, { -62, 70 }, { -188, -180 }, { 72, 172 },
    { 73, 124 }, { 74, 106 }, { 75, 89 }, { 76, 84 }, { 77, 80 }, { -135, 78 },
    { -143, 79 }, { -212, -190 }, { -103, 81 }, { 82, 83 }, { -222, -240 }, { -166, -178 },
    { 85, 86 }, { -69, -5 }, { 87, -122 }, { 88, -56 }, { -170, -236 }, { 90, 97 },
    { 91, 94 }, { -6, 92 }, { -226, 93 }, { -60, -232 }, { -70, 95 }, { -225, 96 },
    { -168, -245 }, { 98, 101 }, { 99, -54 }, { 100, -145 }, { -158, -234 }, { 102, 105 },
    { 103, 104 }, { -230, -45 }, { -86, -76 }, { -58, -201 }, { 107, 110 }, { 108, 109 },
    { -197, -233 }, { -130, -11 }, { 111, 115 }, { 112, -117 }, { -120, 113 }, { 114, -59 },
    { -227, -184 }, { 116, 120 }, { -15, 117 }, { 118, 119 }, { -214, -253 }, { -160, -210 },
    { -116, 121 }, { 122, 123 }, { -154, -251 }, { -162, -38 }, { 125, 139 }, { 126, -9 },
    { 127, 138 }, { 128, 131 }, { 129, -113 }, { 130, -94 }, { -249, -239 }, { 132, 135 },
    { 133, 134 }, { -203, -156 }, { -150, -172 }, { 136, 137 }, { -181, -243 }, { -206, -37 },
    { -17, -128 }, { 140, 154 }, { 141, -14 }, { 142, 148 }, { 143, 145 }, { -55, 144 },
    { -213, -39 }, { 146, 147 }, { -192, -254 }, { -26, -219 }, { 149, 151 }, { -144, 150 },
    { -176, -207 }, { 152, 153 }, { -174, -36 }, { -215, -40 }, { 155, 166 }, { 156, 162 },
    { 157, 160 }, { 158, 159 }, { -208, -247 }, { -205, -167 }, { 161, -77 }, { -82, -189 },
    { -195, 163 }, { 164, 165 }, { -211, -165 }, { -21, -223 }, { 167, -132 }, { 168, 170 },
    { 169, -73 }, { -152, -179 }, { -107, 171 }, { -217, -221 }, { 173, 226 }, { 174, 207 },
    { 175, 193 }, { 176, 181 }, { 177, 180 }, { 178, -134 }, { 179, -194 }, { -204, -187 },
    { -30, -50 }, { 182, 186 }, { 183, -123 }, { 184, 185 }, { -229, -237 }, { -61, -24 },
    { 187, 190 }, { 188, 189 }, { -74, -191 }, { -146, -28 }, { 191, 192 }, { -155, -171 },
    { -87, -183 }, { 194, 199 }, { -13, 195 }, { -53, 196 }, { 197, 198 }, { -34, -231 },
    { -85, -185 }, { 200, 204 }, { 201, -112 }, { 202, 203 }, { -43, -88 }, { -163, -80 },
    { -124, 205 }, { -97, 206 }, { -64, -22 }, { 208, -2 }, { 209, 213 }, { -12, 210 },
    { -121, 211 }, { 212, -141 }, { -175, -90 }, { 214, 220 }, { 215, 218 }, { 216, 217 },
    { -149, -23 }, { -157, -153 }, { 219, -99 }, { -27, -78 }, { 221, 224 }, { 222, 223 },
    { -151, -173 }, { -83, -84 }, { -95, 225 }, { -29, -159 }, { 227, 246 }, { 228, 238 },
    { 229, 236 }, { 230, 233 }, { 231, -136 }, { -57, 232 }, { -235, -199 }, { -115, 234 },
    { -93, 235 }, { -79, -198 }, { -49, 237 }, { -137, -138 }, { 239, -33 }, { 240, 242 },
    { -119, 241 }, { -101, -52 }, { -139, 243 }, { 244, 245 }, { -89, -75 }, { -241, -148 },
    { 247, 255 }, { 248, 253 }, { 249, 251 }, { -133, 250 }, { -110, -100 }, { 252, -65 },
    { -31, -104 }, { -255, 254 }, { -125, -51 }, { -129, -8 },
};

void qa_q3_huffman_code(uint8_t symbol, uint32_t *bits, unsigned *width)
{
    *bits = message_codes[symbol].bits;
    *width = message_codes[symbol].width;
}

uint32_t qa_q3_huffman_symbol(qa_net_reader *reader)
{
    int node = 0;
    while (node >= 0 && !reader->failed)
        node = message_branches[node][qa_net_read_bits(reader, 1)];
    return reader->failed ? 0 : (uint32_t)(-node - 1);
}

enum { HUFF_NYT = 256, HUFF_INTERNAL = 257, HUFF_NODES = 513 };
typedef struct adaptive_node adaptive_node;
typedef struct adaptive_block adaptive_block;
struct adaptive_block {
    adaptive_node *leader;
    adaptive_block *free_next;
};
struct adaptive_node {
    adaptive_node *left, *right, *parent, *next, *prev;
    adaptive_block *block;
    uint32_t weight;
    unsigned symbol;
};
typedef struct adaptive_tree {
    adaptive_node nodes[HUFF_NODES];
    adaptive_block blocks[HUFF_NODES];
    adaptive_node *symbols[256], *root, *nyt;
    adaptive_block *free_blocks;
    unsigned node_count, block_count;
} adaptive_tree;

static adaptive_block *new_block(adaptive_tree *tree, adaptive_node *leader)
{
    adaptive_block *block = tree->free_blocks;
    if (block != NULL) tree->free_blocks = block->free_next;
    else block = &tree->blocks[tree->block_count++];
    *block = (adaptive_block){ .leader = leader };
    return block;
}

static void release_block(adaptive_tree *tree, adaptive_block *block)
{
    block->leader = NULL;
    block->free_next = tree->free_blocks;
    tree->free_blocks = block;
}

static void init_tree(adaptive_tree *tree)
{
    memset(tree, 0, sizeof(*tree));
    tree->root = tree->nyt = &tree->nodes[tree->node_count++];
    tree->nyt->symbol = HUFF_NYT;
}

static void swap_tree(adaptive_tree *tree, adaptive_node *a, adaptive_node *b)
{
    adaptive_node *ap = a->parent, *bp = b->parent;
    if (ap == NULL) tree->root = b;
    else if (ap->left == a) ap->left = b;
    else ap->right = b;
    if (bp == NULL) tree->root = a;
    else if (bp->left == b) bp->left = a;
    else bp->right = a;
    a->parent = bp;
    b->parent = ap;
}

static void swap_list(adaptive_node *a, adaptive_node *b)
{
    adaptive_node *next = a->next, *prev = a->prev;
    a->next = b->next;
    b->next = next;
    a->prev = b->prev;
    b->prev = prev;
    if (a->next == a) a->next = b;
    if (b->next == b) b->next = a;
    if (a->next != NULL) a->next->prev = a;
    if (b->next != NULL) b->next->prev = b;
    if (a->prev != NULL) a->prev->next = a;
    if (b->prev != NULL) b->prev->next = b;
}

static void increment(adaptive_tree *tree, adaptive_node *node)
{
    if (node == NULL) return;
    if (node->next != NULL && node->next->weight == node->weight) {
        adaptive_node *leader = node->block->leader;
        if (leader != node->parent) swap_tree(tree, leader, node);
        swap_list(leader, node);
    }
    if (node->prev != NULL && node->prev->weight == node->weight)
        node->block->leader = node->prev;
    else release_block(tree, node->block);
    ++node->weight;
    if (node->next != NULL && node->next->weight == node->weight)
        node->block = node->next->block;
    else node->block = new_block(tree, node);
    if (node->parent != NULL) {
        increment(tree, node->parent);
        if (node->prev == node->parent) {
            swap_list(node, node->parent);
            if (node->block->leader == node) node->block->leader = node->parent;
        }
    }
}

static void add_reference(adaptive_tree *tree, uint8_t symbol)
{
    if (tree->symbols[symbol] != NULL) {
        increment(tree, tree->symbols[symbol]);
        return;
    }
    /* A byte alphabet adds at most 256 leaves and 256 branches to NYT. */
    adaptive_node *leaf = &tree->nodes[tree->node_count++];
    adaptive_node *branch = &tree->nodes[tree->node_count++];
    branch->symbol = HUFF_INTERNAL;
    branch->weight = 1;
    branch->next = tree->nyt->next;
    if (branch->next != NULL) branch->next->prev = branch;
    branch->block = branch->next != NULL && branch->next->weight == 1
                    ? branch->next->block : new_block(tree, branch);
    leaf->symbol = symbol;
    leaf->weight = 1;
    leaf->next = branch;
    branch->prev = leaf;
    leaf->block = branch->block;
    tree->nyt->next = leaf;
    leaf->prev = tree->nyt;
    adaptive_node *parent = tree->nyt->parent;
    if (parent == NULL) tree->root = branch;
    else if (parent->left == tree->nyt) parent->left = branch;
    else parent->right = branch;
    branch->right = leaf;
    branch->left = tree->nyt;
    branch->parent = parent;
    tree->nyt->parent = branch;
    leaf->parent = branch;
    tree->symbols[symbol] = leaf;
    increment(tree, parent);
}

static bool send_prefix(const adaptive_node *node, qa_net_writer *writer)
{
    uint8_t reverse[256];
    unsigned count = 0;
    while (node->parent != NULL) {
        reverse[count++] = node->parent->right == node ? 1 : 0;
        node = node->parent;
    }
    while (count != 0)
        if (!qa_net_write_bits(writer, reverse[--count], 1)) return false;
    return true;
}

static bool send_symbol(const adaptive_tree *tree, uint8_t symbol, qa_net_writer *writer)
{
    if (tree->symbols[symbol] != NULL) return send_prefix(tree->symbols[symbol], writer);
    if (!send_prefix(tree->nyt, writer)) return false;
    for (unsigned shift = 8; shift != 0; --shift)
        if (!qa_net_write_bits(writer, ((uint32_t)symbol >> (shift - 1)) & 1u, 1)) return false;
    return true;
}

static uint8_t receive_symbol(const adaptive_tree *tree, qa_net_reader *reader)
{
    const adaptive_node *node = tree->root;
    while (node->symbol == HUFF_INTERNAL && !reader->failed)
        node = qa_net_read_bits(reader, 1) == 0 ? node->left : node->right;
    uint32_t symbol = node->symbol;
    if (!reader->failed && symbol == HUFF_NYT) {
        symbol = 0;
        for (unsigned index = 0; index < 8 && !reader->failed; ++index)
            symbol = symbol * 2u + qa_net_read_bits(reader, 1);
    }
    return (uint8_t)symbol;
}

bool qa_q3_huffman_compress(qa_bytes bytes, qa_buffer *out, qa_error *error)
{
    if (out == NULL || (bytes.size != 0 && bytes.data == NULL) || bytes.size > UINT16_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 adaptive Huffman input");
        return false;
    }
    if (bytes.size == 0) {
        *out = (qa_buffer){0};
        return true;
    }
    /* Worst case is a 256-bit prefix and an eight-bit NYT literal. */
    size_t capacity = 3 + bytes.size * 33;
    uint8_t *data = malloc(capacity);
    if (data == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Q3 adaptive Huffman output");
        return false;
    }
    qa_net_writer writer;
    qa_net_writer_init(&writer, data, capacity, error);
    data[0] = (uint8_t)(bytes.size >> 8);
    data[1] = (uint8_t)bytes.size;
    writer.bit = 16;
    adaptive_tree tree;
    init_tree(&tree);
    for (size_t index = 0; index < bytes.size; ++index) {
        if (!send_symbol(&tree, bytes.data[index], &writer)) {
            free(data);
            return false;
        }
        add_reference(&tree, bytes.data[index]);
    }
    size_t size = writer.bit / 8 + 1;
    uint8_t *trimmed = realloc(data, size);
    *out = (qa_buffer){ .data = trimmed == NULL ? data : trimmed, .size = size };
    return true;
}

bool qa_q3_huffman_decompress(qa_bytes bytes, size_t maximum, qa_buffer *out, qa_error *error)
{
    if (out == NULL || (bytes.size != 0 && bytes.data == NULL) || bytes.size > SIZE_MAX / 8) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 adaptive Huffman input");
        return false;
    }
    if (bytes.size == 0) {
        *out = (qa_buffer){0};
        return true;
    }
    if (bytes.size < 2) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Truncated Q3 adaptive Huffman length");
        return false;
    }
    size_t size = (size_t)bytes.data[0] * 256 + bytes.data[1];
    if (size > maximum) size = maximum;
    if (size == 0) {
        *out = (qa_buffer){0};
        return true;
    }
    uint8_t *data = malloc(size);
    if (data == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Q3 adaptive Huffman output");
        return false;
    }
    qa_net_reader reader;
    qa_net_reader_init(&reader, bytes, error);
    reader.bit = 16;
    adaptive_tree tree;
    init_tree(&tree);
    for (size_t index = 0; index < size; ++index) {
        data[index] = receive_symbol(&tree, &reader);
        if (reader.failed) {
            free(data);
            return false;
        }
        add_reference(&tree, data[index]);
    }
    *out = (qa_buffer){ .data = data, .size = size };
    return true;
}
