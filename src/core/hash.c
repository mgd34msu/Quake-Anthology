#include "qa/hash.h"
#include "qa/binary.h"

#include <string.h>

static uint32_t rotate_right(uint32_t value, unsigned int shift)
{
    return (value >> shift) | (value << (32 - shift));
}

static uint32_t rotate_left(uint32_t value, unsigned int shift)
{
    return (value << shift) | (value >> (32 - shift));
}

static uint32_t load_be(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static void store_be(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void sha256_block(qa_sha256_context *context, const uint8_t *block)
{
    static const uint32_t constants[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    uint32_t words[64];
    for (size_t i = 0; i < 16; i++) words[i] = load_be(block + i * 4);
    for (size_t i = 16; i < 64; i++) {
        uint32_t x = words[i - 15], y = words[i - 2];
        uint32_t sigma0 = rotate_right(x, 7) ^ rotate_right(x, 18) ^ (x >> 3);
        uint32_t sigma1 = rotate_right(y, 17) ^ rotate_right(y, 19) ^ (y >> 10);
        words[i] = words[i - 16] + sigma0 + words[i - 7] + sigma1;
    }
    uint32_t a = context->state[0], b = context->state[1], c = context->state[2], d = context->state[3];
    uint32_t e = context->state[4], f = context->state[5], g = context->state[6], h = context->state[7];
    for (size_t i = 0; i < 64; i++) {
        uint32_t sigma1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t first = h + sigma1 + choose + constants[i] + words[i];
        uint32_t sigma0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t second = sigma0 + majority;
        h = g; g = f; f = e; e = d + first;
        d = c; c = b; b = a; a = first + second;
    }
    context->state[0] += a; context->state[1] += b;
    context->state[2] += c; context->state[3] += d;
    context->state[4] += e; context->state[5] += f;
    context->state[6] += g; context->state[7] += h;
}

void qa_sha256_init(qa_sha256_context *context)
{
    *context = (qa_sha256_context){.state = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    }};
}

void qa_sha256_update(qa_sha256_context *context, qa_bytes bytes)
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
            sha256_block(context, context->block);
            context->used = 0;
        }
    }
    while (bytes.size - offset >= 64) {
        sha256_block(context, bytes.data + offset);
        offset += 64;
    }
    if (offset < bytes.size) {
        context->used = bytes.size - offset;
        memcpy(context->block, bytes.data + offset, context->used);
    }
}

void qa_sha256_final(qa_sha256_context *context, qa_sha256_digest *out)
{
    uint64_t bits = context->length << 3;
    context->block[context->used++] = 0x80;
    if (context->used > 56) {
        memset(context->block + context->used, 0, 64 - context->used);
        sha256_block(context, context->block);
        context->used = 0;
    }
    memset(context->block + context->used, 0, 56 - context->used);
    store_be(context->block + 56, (uint32_t)(bits >> 32));
    store_be(context->block + 60, (uint32_t)bits);
    sha256_block(context, context->block);
    for (size_t i = 0; i < 8; i++) store_be(out->bytes + i * 4, context->state[i]);
    memset(context, 0, sizeof(*context));
}

void qa_sha256(qa_bytes bytes, qa_sha256_digest *out)
{
    qa_sha256_context context;
    qa_sha256_init(&context);
    qa_sha256_update(&context, bytes);
    qa_sha256_final(&context, out);
}

bool qa_sha256_equal(const qa_sha256_digest *left, const qa_sha256_digest *right)
{
    return memcmp(left->bytes, right->bytes, sizeof(left->bytes)) == 0;
}

void qa_sha256_hex(const qa_sha256_digest *digest, char out[65])
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(digest->bytes); i++) {
        out[i * 2] = digits[digest->bytes[i] >> 4];
        out[i * 2 + 1] = digits[digest->bytes[i] & 15];
    }
    out[64] = '\0';
}

static int hex_digit(unsigned char byte)
{
    if (byte >= '0' && byte <= '9') return byte - '0';
    if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
    if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
    return -1;
}

bool qa_sha256_parse(const char *text, qa_sha256_digest *out, qa_error *error)
{
    if (text == NULL || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid SHA-256 text");
        return false;
    }
    if (strncmp(text, "sha256:", 7) == 0) text += 7;
    if (strlen(text) != 64) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "SHA-256 requires 64 hexadecimal digits");
        return false;
    }
    qa_sha256_digest digest;
    for (size_t i = 0; i < sizeof(digest.bytes); i++) {
        int high = hex_digit((unsigned char)text[i * 2]);
        int low = hex_digit((unsigned char)text[i * 2 + 1]);
        if (high < 0 || low < 0) {
            qa_error_set(error, QA_ERROR_FORMAT, i * 2, "invalid SHA-256 hexadecimal digit");
            return false;
        }
        digest.bytes[i] = (uint8_t)((high << 4) | low);
    }
    *out = digest;
    return true;
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
