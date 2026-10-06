#include "qa/hash.h"
#include "qa/binary.h"

#include <string.h>

static const uint16_t crc_table[256] = {
	0x0000,	0x1021,	0x2042,	0x3063,	0x4084,	0x50a5,	0x60c6,	0x70e7,
	0x8108,	0x9129,	0xa14a,	0xb16b,	0xc18c,	0xd1ad,	0xe1ce,	0xf1ef,
	0x1231,	0x0210,	0x3273,	0x2252,	0x52b5,	0x4294,	0x72f7,	0x62d6,
	0x9339,	0x8318,	0xb37b,	0xa35a,	0xd3bd,	0xc39c,	0xf3ff,	0xe3de,
	0x2462,	0x3443,	0x0420,	0x1401,	0x64e6,	0x74c7,	0x44a4,	0x5485,
	0xa56a,	0xb54b,	0x8528,	0x9509,	0xe5ee,	0xf5cf,	0xc5ac,	0xd58d,
	0x3653,	0x2672,	0x1611,	0x0630,	0x76d7,	0x66f6,	0x5695,	0x46b4,
	0xb75b,	0xa77a,	0x9719,	0x8738,	0xf7df,	0xe7fe,	0xd79d,	0xc7bc,
	0x48c4,	0x58e5,	0x6886,	0x78a7,	0x0840,	0x1861,	0x2802,	0x3823,
	0xc9cc,	0xd9ed,	0xe98e,	0xf9af,	0x8948,	0x9969,	0xa90a,	0xb92b,
	0x5af5,	0x4ad4,	0x7ab7,	0x6a96,	0x1a71,	0x0a50,	0x3a33,	0x2a12,
	0xdbfd,	0xcbdc,	0xfbbf,	0xeb9e,	0x9b79,	0x8b58,	0xbb3b,	0xab1a,
	0x6ca6,	0x7c87,	0x4ce4,	0x5cc5,	0x2c22,	0x3c03,	0x0c60,	0x1c41,
	0xedae,	0xfd8f,	0xcdec,	0xddcd,	0xad2a,	0xbd0b,	0x8d68,	0x9d49,
	0x7e97,	0x6eb6,	0x5ed5,	0x4ef4,	0x3e13,	0x2e32,	0x1e51,	0x0e70,
	0xff9f,	0xefbe,	0xdfdd,	0xcffc,	0xbf1b,	0xaf3a,	0x9f59,	0x8f78,
	0x9188,	0x81a9,	0xb1ca,	0xa1eb,	0xd10c,	0xc12d,	0xf14e,	0xe16f,
	0x1080,	0x00a1,	0x30c2,	0x20e3,	0x5004,	0x4025,	0x7046,	0x6067,
	0x83b9,	0x9398,	0xa3fb,	0xb3da,	0xc33d,	0xd31c,	0xe37f,	0xf35e,
	0x02b1,	0x1290,	0x22f3,	0x32d2,	0x4235,	0x5214,	0x6277,	0x7256,
	0xb5ea,	0xa5cb,	0x95a8,	0x8589,	0xf56e,	0xe54f,	0xd52c,	0xc50d,
	0x34e2,	0x24c3,	0x14a0,	0x0481,	0x7466,	0x6447,	0x5424,	0x4405,
	0xa7db,	0xb7fa,	0x8799,	0x97b8,	0xe75f,	0xf77e,	0xc71d,	0xd73c,
	0x26d3,	0x36f2,	0x0691,	0x16b0,	0x6657,	0x7676,	0x4615,	0x5634,
	0xd94c,	0xc96d,	0xf90e,	0xe92f,	0x99c8,	0x89e9,	0xb98a,	0xa9ab,
	0x5844,	0x4865,	0x7806,	0x6827,	0x18c0,	0x08e1,	0x3882,	0x28a3,
	0xcb7d,	0xdb5c,	0xeb3f,	0xfb1e,	0x8bf9,	0x9bd8,	0xabbb,	0xbb9a,
	0x4a75,	0x5a54,	0x6a37,	0x7a16,	0x0af1,	0x1ad0,	0x2ab3,	0x3a92,
	0xfd2e,	0xed0f,	0xdd6c,	0xcd4d,	0xbdaa,	0xad8b,	0x9de8,	0x8dc9,
	0x7c26,	0x6c07,	0x5c64,	0x4c45,	0x3ca2,	0x2c83,	0x1ce0,	0x0cc1,
	0xef1f,	0xff3e,	0xcf5d,	0xdf7c,	0xaf9b,	0xbfba,	0x8fd9,	0x9ff8,
	0x6e17,	0x7e36,	0x4e55,	0x5e74,	0x2e93,	0x3eb2,	0x0ed1,	0x1ef0
};

uint16_t qa_crc_block(qa_bytes bytes)
{
    uint16_t crc = UINT16_C(0xffff);
    for (size_t i = 0; i < bytes.size; ++i)
        crc = (uint16_t)((crc << 8) ^ crc_table[(crc >> 8) ^ bytes.data[i]]);
    return crc;
}

static uint32_t rotate_left(uint32_t value, unsigned int shift)
{
    return (value << shift) | (value >> (32 - shift));
}

static void md4_block(qa_md4_context *context, const uint8_t *block)
{
    static const unsigned int shifts[3][4] = {{3, 7, 11, 19}, {3, 5, 9, 13}, {3, 9, 11, 15}};
    static const unsigned int third_order[16] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};
    uint32_t words[16];
    for (size_t i = 0; i < 16; i++) words[i] = qa_load_u32le(block + i * 4);
    uint32_t a = context->state[0], b = context->state[1], c = context->state[2], d = context->state[3];
    for (unsigned int round = 0; round < 3; round++) {
        for (unsigned int i = 0; i < 16; i++) {
            uint32_t function, constant;
            unsigned int index;
            if (round == 0) {
                function = (b & c) | (~b & d);
                constant = 0;
                index = i;
            } else if (round == 1) {
                function = (b & c) | (b & d) | (c & d);
                constant = 0x5a827999u;
                index = (i % 4) * 4 + i / 4;
            } else {
                function = b ^ c ^ d;
                constant = 0x6ed9eba1u;
                index = third_order[i];
            }
            uint32_t next = rotate_left(a + function + words[index] + constant, shifts[round][i % 4]);
            a = d; d = c; c = b; b = next;
        }
    }
    context->state[0] += a; context->state[1] += b;
    context->state[2] += c; context->state[3] += d;
}

void qa_md4_init(qa_md4_context *context)
{
    *context = (qa_md4_context){.state = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u}};
}

void qa_md4_update(qa_md4_context *context, qa_bytes bytes)
{
    context->length += (uint64_t)bytes.size;
    size_t offset = 0;
    if (context->used != 0) {
        size_t amount = 64 - context->used;
        if (amount > bytes.size) amount = bytes.size;
        if (amount != 0) memcpy(context->block + context->used, bytes.data, amount);
        context->used += amount;
        offset = amount;
        if (context->used == 64) {
            md4_block(context, context->block);
            context->used = 0;
        }
    }
    while (bytes.size - offset >= 64) {
        md4_block(context, bytes.data + offset);
        offset += 64;
    }
    if (offset < bytes.size) {
        context->used = bytes.size - offset;
        memcpy(context->block, bytes.data + offset, context->used);
    }
}

void qa_md4_final(qa_md4_context *context, qa_md4_digest *out)
{
    uint64_t bits = context->length << 3;
    context->block[context->used++] = 0x80;
    if (context->used > 56) {
        memset(context->block + context->used, 0, 64 - context->used);
        md4_block(context, context->block);
        context->used = 0;
    }
    memset(context->block + context->used, 0, 56 - context->used);
    qa_store_u64le(context->block + 56, bits);
    md4_block(context, context->block);
    for (size_t i = 0; i < 4; i++) qa_store_u32le(out->bytes + i * 4, context->state[i]);
    memset(context, 0, sizeof(*context));
}

void qa_md4(qa_bytes bytes, qa_md4_digest *out)
{
    qa_md4_context context;
    qa_md4_init(&context);
    qa_md4_update(&context, bytes);
    qa_md4_final(&context, out);
}

uint32_t qa_md4_fold(const qa_md4_digest *digest)
{
    return qa_load_u32le(digest->bytes) ^ qa_load_u32le(digest->bytes + 4) ^
           qa_load_u32le(digest->bytes + 8) ^ qa_load_u32le(digest->bytes + 12);
}

uint32_t qa_block_checksum(qa_bytes bytes)
{
    qa_md4_digest digest;
    qa_md4(bytes, &digest);
    return qa_md4_fold(&digest);
}

uint32_t qa_block_checksum_key(qa_bytes bytes, uint32_t key)
{
    qa_md4_context context;
    qa_md4_digest digest;
    uint8_t prefix[4];
    qa_store_u32le(prefix, key);
    qa_md4_init(&context);
    qa_md4_update(&context, (qa_bytes){prefix, sizeof(prefix)});
    qa_md4_update(&context, bytes);
    qa_md4_final(&context, &digest);
    return qa_md4_fold(&digest);
}
