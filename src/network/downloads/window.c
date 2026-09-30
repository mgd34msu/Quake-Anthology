#include "qa/downloads.h"
#include "qa/network_downloads_save.h"
#include "../service_save_fields.h"
#include <stdlib.h>

typedef struct window_slot { uint64_t sequence, sent_ns; bool sent; } window_slot;
struct qa_download_window {
    qa_bytes content;
    size_t block_bytes;
    uint32_t capacity;
    uint64_t base, next, blocks, retry_ns;
    window_slot *slots;
};
static bool fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message); return false;
}
bool qa_download_window_create(qa_bytes bytes, size_t block, uint32_t capacity, uint64_t retry,
                                qa_download_window **out, qa_error *error) {
    if (!out || !block || !capacity || !retry || (bytes.size && !bytes.data) ||
        (uint64_t)capacity > SIZE_MAX / sizeof(window_slot)) return fail(error, "Invalid download window");
    qa_download_window *window = calloc(1, sizeof(*window));
    if (!window) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating download window"); return false; }
    window->slots = calloc(capacity, sizeof(*window->slots));
    if (!window->slots) { free(window); qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating download blocks"); return false; }
    window->content = bytes; window->block_bytes = block; window->capacity = capacity; window->retry_ns = retry;
    window->blocks = bytes.size / block + (bytes.size % block != 0);
    if (window->blocks == UINT64_MAX) { qa_download_window_destroy(window); return fail(error, "Download block sequence exhausted"); }
    ++window->blocks; *out = window; return true;
}
void qa_download_window_destroy(qa_download_window *window) {
    if (window) { free(window->slots); free(window); }
}
bool qa_download_window_next(qa_download_window *window, uint64_t now,
                              qa_download_block *out, bool *present, qa_error *error) {
    if (!window || !out || !present) return fail(error, "Invalid download block request");
    *present = false;
    uint64_t chosen = UINT64_MAX;
    for (uint64_t sequence = window->base; sequence < window->next; ++sequence) {
        window_slot *slot = &window->slots[sequence % window->capacity];
        if (!slot->sent || (now >= slot->sent_ns && now - slot->sent_ns >= window->retry_ns)) { chosen = sequence; break; }
    }
    if (chosen == UINT64_MAX && window->next < window->blocks && window->next - window->base < window->capacity) chosen = window->next;
    if (chosen == UINT64_MAX) return true;
    bool eof = chosen == window->blocks - 1;
    uint64_t offset = eof ? window->content.size : chosen * window->block_bytes;
    size_t count = eof ? 0 : window->content.size - (size_t)offset;
    if (count > window->block_bytes) count = window->block_bytes;
    *out = (qa_download_block){chosen, offset,
        {eof ? NULL : window->content.data + (size_t)offset, count}, eof};
    *present = true; return true;
}
bool qa_download_window_sent(qa_download_window *window, uint64_t sequence, uint64_t now, qa_error *error) {
    if (!window || sequence < window->base || sequence > window->next || sequence >= window->blocks ||
        sequence - window->base >= window->capacity) return fail(error, "Download block send is outside window");
    window_slot *slot = &window->slots[sequence % window->capacity];
    if (sequence == window->next) ++window->next;
    slot->sequence = sequence; slot->sent_ns = now; slot->sent = true; return true;
}
bool qa_download_window_acknowledge(qa_download_window *window, uint64_t sequence, qa_error *error) {
    if (!window) return fail(error, "Missing download window");
    if (sequence < window->base) return true;
    /* Source reliable block protocols acknowledge the next expected block. */
    if (sequence != window->base || sequence >= window->next) return fail(error, "Download acknowledgement is not contiguous");
    window_slot *slot = &window->slots[sequence % window->capacity];
    if (!slot->sent || slot->sequence != sequence) return fail(error, "Download acknowledgement names an unsent block");
    slot->sent = false; ++window->base; return true;
}
bool qa_download_window_complete(const qa_download_window *window) { return window && window->base == window->blocks; }

static bool window_checkpoint_valid(const qa_download_window *w)
{
    if (!w || !w->block_bytes || !w->capacity || !w->slots || !w->retry_ns ||
        (w->content.size && !w->content.data) || w->base > w->next || w->next > w->blocks ||
        w->next - w->base > w->capacity) return false;
    uint64_t blocks = w->content.size / w->block_bytes + (w->content.size % w->block_bytes != 0);
    if (blocks == UINT64_MAX || w->blocks != blocks + 1) return false;
    for (uint32_t i = 0; i < w->capacity; ++i) {
        const window_slot *slot = &w->slots[i];
        if (slot->sequence >= w->blocks || (slot->sent &&
            (slot->sequence < w->base || slot->sequence >= w->next || slot->sequence % w->capacity != i))) return false;
    }
    for (uint64_t i = w->base; i < w->next; ++i)
        if (!w->slots[i % w->capacity].sent || w->slots[i % w->capacity].sequence != i) return false;
    return true;
}
bool qa_download_window_checkpoint(const qa_download_window *window, qa_buffer *out, qa_error *error)
{
    if (!out || !window_checkpoint_valid(window) || (uint64_t)window->capacity > (SIZE_MAX - 92) / 17)
        return fail(error, "Invalid reliable download window continuation");
    size_t capacity = 92 + (size_t)window->capacity * 17;
    uint8_t *data = malloc(capacity);
    if (!data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Encoding download window continuation"); return false; }
    qa_sha256_digest digest; qa_sha256(window->content, &digest);
    qa_net_writer w; qa_net_writer_init(&w, data, capacity, error);
    bool ok = qa_net_write_u32(&w, UINT32_C(0x57444151)) && qa_net_write_u32(&w, 1) &&
        qa_net_write_u64(&w, window->content.size) && qa_net_write_data(&w, digest.bytes, sizeof(digest.bytes)) &&
        qa_net_write_u64(&w, window->block_bytes) && qa_net_write_u32(&w, window->capacity) &&
        qa_net_write_u64(&w, window->retry_ns) && qa_net_write_u64(&w, window->base) &&
        qa_net_write_u64(&w, window->next) && qa_net_write_u64(&w, window->blocks);
    for (uint32_t i = 0; ok && i < window->capacity; ++i) {
        const window_slot *slot = &window->slots[i];
        ok = qa_net_write_u64(&w, slot->sequence) && qa_net_write_u64(&w, slot->sent_ns) && qa_net_write_u8(&w, slot->sent);
    }
    if (!ok || w.failed) { free(data); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&w)}; return true;
}
bool qa_download_window_restore_checkpoint(qa_bytes record, qa_bytes content,
    qa_download_window **out, qa_error *error)
{
    if (!out || *out || (record.size && !record.data) || (content.size && !content.data))
        return fail(error, "Invalid reliable download window restore output/content");
    qa_net_reader r; qa_net_reader_init(&r, record, error);
    if (qa_net_read_u32(&r) != UINT32_C(0x57444151) || qa_net_read_u32(&r) != 1 || qa_net_read_u64(&r) != content.size)
        return fail(error, "Download window continuation schema/content size differs");
    qa_sha256_digest saved = {0}, actual; qa_sha256(content, &actual);
    if (!qa_net_read_data(&r, saved.bytes, sizeof(saved.bytes)) || !qa_sha256_equal(&saved, &actual))
        return fail(error, "Download window content digest differs from candidate resource");
    uint64_t block = qa_net_read_u64(&r); uint32_t capacity = qa_net_read_u32(&r);
    uint64_t retry = qa_net_read_u64(&r), base = qa_net_read_u64(&r), next = qa_net_read_u64(&r), blocks = qa_net_read_u64(&r);
    if (r.failed || !block || block > SIZE_MAX || !capacity || (uint64_t)capacity > SIZE_MAX / sizeof(window_slot) ||
        (uint64_t)capacity > qa_net_reader_remaining(&r) / 17 || qa_net_reader_remaining(&r) != (size_t)capacity * 17)
        return fail(error, "Invalid download window continuation slot extent");
    qa_download_window *window = NULL;
    if (!qa_download_window_create(content, (size_t)block, capacity, retry, &window, error)) return false;
    if (window->blocks != blocks) { qa_download_window_destroy(window); return fail(error, "Download window block extent differs"); }
    window->base = base; window->next = next;
    for (uint32_t i = 0; !r.failed && i < capacity; ++i) {
        window_slot *slot = &window->slots[i]; slot->sequence = qa_net_read_u64(&r);
        slot->sent_ns = qa_net_read_u64(&r); slot->sent = q3_save_bool(&r);
    }
    if (!qa_net_reader_finish(&r) || !window_checkpoint_valid(window)) {
        qa_download_window_destroy(window); return fail(error, "Invalid restored reliable download window ownership");
    }
    *out = window; return true;
}
