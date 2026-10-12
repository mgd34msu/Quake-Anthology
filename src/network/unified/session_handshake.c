#include "frame_internal.h"
#include <string.h>

bool qa_unified_handshake_read(qa_bytes bytes, qa_unified_handshake *out, qa_error *e)
{
    if (!out) { qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing production handshake output"); return false; }
    if (!bytes.data || !bytes.size || bytes.size > 512) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "invalid unified document kind or byte extent"); return false;
    }
    if (bytes.size < 5 || memcmp(bytes.data, "QUHS", 4)) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Unified handshake has the wrong external envelope"); return false;
    }
    qa_unified_handshake value = {0};
    if (!qa_unified_record_delta_decode(&qa_unified_handshake_layout,
        (qa_bytes){bytes.data + 4, bytes.size - 4}, NULL, &value,
        NULL, NULL, NULL, NULL, NULL, e)) return false;
    if ((unsigned)value.kind > QA_UNIFIED_CONNECT) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Unified handshake requires its actual native kind and tokens"); return false;
    }
    *out = value;
    return true;
}

bool qa_unified_handshake_write(const qa_unified_handshake *handshake, void *storage,
    size_t capacity, qa_bytes *out, qa_error *e)
{
    if (!handshake || !storage || !out || (unsigned)handshake->kind > QA_UNIFIED_CONNECT) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid production handshake producer"); return false;
    }
    size_t maximum = capacity < 512 ? capacity : 512;
    qa_unified_builder wire = {.data = storage, .capacity = maximum, .maximum = maximum};
    if (!qa_unified_append(&wire, "QUHS", 4, e) ||
        !qa_unified_record_delta_write(&qa_unified_handshake_layout, handshake, NULL, &wire, e)) return false;
    *out = (qa_bytes){wire.data, wire.size};
    return true;
}
