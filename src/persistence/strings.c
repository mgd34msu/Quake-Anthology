#include "internal.h"

bool qa_save_strings_encode(const qa_strings *strings, qa_buffer *out, qa_error *error)
{
    if (!strings || !out) return persistence_fail(error, QA_ERROR_ARGUMENT, "Invalid saved string table");
    size_t count = qa_strings_count(strings), size = 12;
    if (count > UINT32_MAX) return persistence_fail(error, QA_ERROR_MEMORY, "Saved string table exceeds index space");
    for (size_t i = 0; i < count; ++i) {
        qa_bytes text = qa_strings_text(strings, (qa_string_id)(i + 1));
        if (size > SIZE_MAX - 8 || text.size > SIZE_MAX - size - 8)
            return persistence_fail(error, QA_ERROR_MEMORY, "Saved string table codec size overflow");
        size += 8 + text.size;
    }
    qa_buffer buffer = {malloc(size), size};
    if (!buffer.data) return persistence_fail(error, QA_ERROR_MEMORY, "Allocating saved string table codec");
    qa_net_writer w;
    qa_net_writer_init(&w, buffer.data, size, error);
    qa_net_write_data(&w, "QAST", 4); qa_net_write_u32(&w, 1); qa_net_write_u32(&w, (uint32_t)count);
    for (size_t i = 0; i < count; ++i) {
        qa_bytes text = qa_strings_text(strings, (qa_string_id)(i + 1));
        qa_net_write_u64(&w, text.size); qa_net_write_data(&w, text.data, text.size);
    }
    if (w.failed) { qa_buffer_free(&buffer); return false; }
    *out = buffer;
    return true;
}

bool qa_save_strings_decode(qa_bytes bytes, qa_strings **out, qa_error *error)
{
    if (!out || !bytes.data || bytes.size < 12 || memcmp(bytes.data, "QAST", 4))
        return persistence_fail(error, QA_ERROR_FORMAT, "Invalid saved string table signature");
    qa_net_reader r;
    qa_net_reader_init(&r, bytes, error); r.bit = 32;
    uint32_t version = qa_net_read_u32(&r), count = qa_net_read_u32(&r);
    if (version != 1 || count > qa_net_reader_remaining(&r) / 8)
        return persistence_fail(error, QA_ERROR_FORMAT, "Invalid saved string table extent");
    qa_strings *strings = NULL;
    if (!qa_strings_create(&strings, error)) return false;
    bool ok = true;
    for (uint32_t i = 0; ok && i < count; ++i) {
        uint64_t length = qa_net_read_u64(&r);
        if (r.failed || length > qa_net_reader_remaining(&r)) {
            ok = qa_net_reader_fail(&r, "Saved string exceeds checkpoint"); break;
        }
        qa_bytes text;
        qa_string_id id;
        ok = qa_net_read_bytes(&r, (size_t)length, &text) && qa_strings_intern(strings, text, &id, error);
        if (ok && id != (qa_string_id)(i + 1u))
            ok = persistence_fail(error, QA_ERROR_FORMAT, "Duplicate saved string table entry");
    }
    if (ok) ok = qa_net_reader_finish(&r);
    if (!ok) { qa_strings_destroy(strings); return false; }
    *out = strings;
    return true;
}
