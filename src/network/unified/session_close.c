#include "session_internal.h"
#include "value_internal.h"
#include "qa/text.h"
#include "qa/network_unified_control.h"

#include <stdlib.h>
#include <string.h>

bool qa_unified_session_close(qa_unified_session *s, const char *reason, qa_error *e)
{
    if (!s || !s->bound_source || !reason || s->entered)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production close lacks its actual Source callback owner");
    char retained[3073]; size_t length = 0, cursor = 0, units = 0;
    qa_bytes original = {(const uint8_t *)reason, strlen(reason)};
    uint32_t scalar;
    while (units < 1024 && qa_utf8_next(original, &cursor, &scalar)) {
        size_t width = scalar > 0xffff ? 2u : 1u;
        if (width > 1024 - units) { scalar = 0xfffd; width = 1; }
        length += qa_utf8_encode(scalar, retained + length); units += width;
    }
    retained[length] = 0;
    if (s->closing || s->disconnected) return true;
    if (s->timeout_pending) {
        const qa_unified_document *d = s->timeout_delivery ? s->timeout_delivery->document : NULL;
        const qa_unified_control *control=qa_unified_document_control(d);
        if (s->close_cause==2 && control && control->kind==QA_UNIFIED_CONTROL_DISCONNECT &&
            !strcmp(control->value.disconnect,retained)) return true;
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production close retry changes its already retained cause or reason");
    }
    qa_unified_control value={.kind=QA_UNIFIED_CONTROL_DISCONNECT,.value.disconnect=retained};
    qa_unified_held *held = calloc(1, sizeof(*held));
    if (!held) return qa_unified_session_fail(e, QA_ERROR_MEMORY, "Retaining actual Source close request");
    held->kind = QA_UNIFIED_CONTROL_DOCUMENT;
    bool okay=qa_unified_document_create_control(&value,&held->document,e) &&
        qa_unified_document_encode(held->document,&held->wire,e);
    if (!okay) { qa_unified_session_delivery_free(held); return false; }
    held->bytes = held->wire.size;
    s->timeout_delivery = held; s->timeout_pending = true; s->close_cause = 2;
    return true;
}
