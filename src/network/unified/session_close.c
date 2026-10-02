#include "session_internal.h"
#include "value_internal.h"

#include <stdlib.h>
#include <string.h>

bool qa_unified_session_close(qa_unified_session *s, const char *reason, qa_error *e)
{
    if (!s || !s->bound_source || !reason || s->entered)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production close lacks its actual Source callback owner");
    size_t length = 0;
    while (length <= 4096 && reason[length]) ++length;
    if (length > 4096) return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production Source close reason exceeds its retained extent");
    if (s->closing || s->disconnected) return true;
    if (s->timeout_pending) {
        const qa_unified_document *d = s->timeout_delivery ? s->timeout_delivery->document : NULL;
        if (s->close_cause == 2 && d && qa_json_string_equal(qa_unified_document_json(d),
            qa_json_get(qa_unified_document_json(d), qa_unified_session_value(d), "reason"), reason)) return true;
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production close retry changes its already retained cause or reason");
    }
    qa_buffer quoted = {0};
    qa_unified_builder json = {.maximum = s->limits.message_bytes};
    static const char prefix[] = "{\"schema\":\"qts-control\",\"version\":1,\"value\":{\"kind\":\"disconnect\",\"reason\":";
    qa_unified_held *held = calloc(1, sizeof(*held));
    if (!held) return qa_unified_session_fail(e, QA_ERROR_MEMORY, "Retaining actual Source close request");
    bool okay = qa_json_quote((qa_bytes){(const uint8_t *)reason, length}, &quoted, e) &&
        qa_unified_append(&json, prefix, sizeof(prefix) - 1, e) && qa_unified_append(&json, quoted.data, quoted.size, e) &&
        qa_unified_append(&json, "}}", 2, e) && qa_unified_document_create(QA_UNIFIED_CONTROL_DOCUMENT,
            (qa_bytes){json.data, json.size}, &held->document, e) &&
        qa_unified_document_encode(held->document, &held->wire, e);
    free(json.data); qa_buffer_free(&quoted);
    if (!okay) { qa_unified_session_delivery_free(held); return false; }
    held->bytes = held->wire.size;
    s->timeout_delivery = held; s->timeout_pending = true; s->close_cause = 2;
    return true;
}
