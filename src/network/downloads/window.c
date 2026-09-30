#include "qa/downloads.h"
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
