#include "qa/network_q2.h"
#include "qa/arena.h"
#include <stdlib.h>
#include <limits.h>
#include <zlib.h>

struct qa_q2_compression {
    qa_arena storage;
    z_stream inflate, deflate;
    bool inflate_open, deflate_open;
};

static voidpf compression_allocate(voidpf user, uInt count, uInt size)
{
    if (size && count > SIZE_MAX / size) return NULL;
    return qa_arena_alloc(user, (size_t)count * size, _Alignof(max_align_t), NULL);
}

static void compression_release(voidpf user, voidpf address)
{ (void)user; (void)address; }

bool qa_q2_compression_create(bool raw, qa_q2_compression **out, qa_error *error)
{
    if (!out) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing Q2 compression owner output"); return false; }
    qa_q2_compression *c = calloc(1, sizeof(*c));
    if (!c) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot create Q2 compression owner"); return false; }
    /* zlib's MAX_WBITS window and memLevel 8 deflater use 256 KiB plus
     * stream state; the inflater adds a 32 KiB window and its state. */
    if (!qa_arena_reserve(&c->storage, 512u * 1024u, error)) {
        qa_q2_compression_destroy(c); return false;
    }
    qa_arena_seal(&c->storage);
    c->inflate = (z_stream){.zalloc = compression_allocate, .zfree = compression_release, .opaque = &c->storage};
    c->deflate = c->inflate;
    int bits = raw ? -MAX_WBITS : MAX_WBITS;
    int status = inflateInit2(&c->inflate, bits);
    c->inflate_open = status == Z_OK;
    if (c->inflate_open) {
        status = deflateInit2(&c->deflate, Z_DEFAULT_COMPRESSION, Z_DEFLATED, bits, 8, Z_DEFAULT_STRATEGY);
        c->deflate_open = status == Z_OK;
    }
    if (!c->inflate_open || !c->deflate_open) {
        qa_error_set(error, status == Z_MEM_ERROR ? QA_ERROR_MEMORY : QA_ERROR_FORMAT, 0,
            "Cannot prepare Q2 compression streams");
        qa_q2_compression_destroy(c); return false;
    }
    *out = c; return true;
}

void qa_q2_compression_destroy(qa_q2_compression *c)
{
    if (!c) return;
    if (c->inflate_open) inflateEnd(&c->inflate);
    if (c->deflate_open) deflateEnd(&c->deflate);
    qa_arena_destroy(&c->storage); free(c);
}

bool qa_q2_compression_read(qa_q2_compression *c, qa_bytes bytes, qa_buffer storage,
    qa_bytes *out, qa_error *error)
{
    if (!c || !out || !storage.data || !storage.size || storage.size > UINT_MAX ||
        bytes.size > UINT_MAX || (bytes.size && !bytes.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 compression storage"); return false;
    }
    z_stream *z = &c->inflate;
    int status = inflateReset(z);
    if (status == Z_OK) {
        z->next_in = (Bytef *)bytes.data; z->avail_in = (uInt)bytes.size;
        z->next_out = storage.data; z->avail_out = (uInt)storage.size;
        status = inflate(z, Z_FINISH);
    }
    if (status != Z_STREAM_END || z->total_in != bytes.size) {
        qa_error_set(error, status == Z_MEM_ERROR ? QA_ERROR_MEMORY : QA_ERROR_FORMAT, 0,
            "Malformed or oversized Q2 compressed packet"); return false;
    }
    *out = (qa_bytes){storage.data, (size_t)z->total_out}; return true;
}

bool qa_q2_compression_write(qa_q2_compression *c, qa_bytes bytes, qa_buffer storage,
    qa_bytes *out, qa_error *error)
{
    if (!c || !out || !storage.data || !storage.size || storage.size > UINT_MAX ||
        bytes.size > UINT_MAX || (bytes.size && !bytes.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 compression storage"); return false;
    }
    z_stream *z = &c->deflate;
    int status = deflateReset(z);
    if (status == Z_OK) {
        z->next_in = (Bytef *)bytes.data; z->avail_in = (uInt)bytes.size;
        z->next_out = storage.data; z->avail_out = (uInt)storage.size;
        status = deflate(z, Z_FINISH);
    }
    if (status != Z_STREAM_END) {
        qa_error_set(error, status == Z_MEM_ERROR ? QA_ERROR_MEMORY : QA_ERROR_FORMAT, 0,
            "Q2 packet compression failed"); return false;
    }
    *out = (qa_bytes){storage.data, (size_t)z->total_out}; return true;
}

bool qa_q2_zpacket_read(qa_q2_compression *c, qa_net_reader *r, qa_buffer storage, qa_bytes *out)
{
    uint16_t compressed = qa_net_read_u16(r), expected = qa_net_read_u16(r);
    qa_bytes bytes;
    if (!out) return qa_net_reader_fail(r, "Missing Q2 zpacket output");
    if (!qa_net_read_bytes(r, compressed, &bytes)) return false;
    if (storage.size < (expected ? expected : 1u)) return qa_net_reader_fail(r, "Q2 zpacket exceeds decode storage");
    storage.size = expected ? expected : 1u;
    if (!qa_q2_compression_read(c, bytes, storage, out, r->error)) { r->failed = true; return false; }
    if (out->size != expected) return qa_net_reader_fail(r, "Malformed Q2 zpacket");
    return true;
}

bool qa_q2_zpacket_wrap(qa_q2_compression *c, qa_bytes bytes, size_t limit, uint8_t opcode,
    qa_buffer storage, qa_bytes *out, bool *wrapped, qa_error *error)
{
    if (!out || !wrapped || (bytes.size && !bytes.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 zpacket input"); return false;
    }
    *wrapped = false;
    if (bytes.size < 21 || bytes.size > 65535 || limit < 5 || bytes.data[0] == 12) return true;
    if (storage.size < 5) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing Q2 zpacket storage"); return false;
    }
    qa_bytes compressed;
    if (!qa_q2_compression_write(c, bytes, (qa_buffer){storage.data + 5, storage.size - 5}, &compressed, error)) return false;
    size_t size = compressed.size;
    if (size > 65535 || size + 5 >= bytes.size || size + 5 > limit) return true;
    uint8_t *data = storage.data;
    data[0] = opcode; data[1] = (uint8_t)size; data[2] = (uint8_t)(size >> 8);
    data[3] = (uint8_t)bytes.size; data[4] = (uint8_t)(bytes.size >> 8);
    *out = (qa_bytes){data, size + 5}; *wrapped = true; return true;
}
