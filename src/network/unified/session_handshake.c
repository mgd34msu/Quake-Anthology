#include "frame_internal.h"

bool qa_unified_handshake_read(qa_bytes bytes, qa_unified_handshake *out, qa_error *e)
{
    if (!out) { qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing production handshake output"); return false; }
    qa_unified_document *document = NULL;
    if (!qa_unified_document_decode(QA_UNIFIED_HANDSHAKE_DOCUMENT, bytes, NULL, &document, e)) return false;
    *out = *qa_unified_document_handshake(document);
    qa_unified_document_destroy(document);
    return true;
}

bool qa_unified_handshake_write(const qa_unified_handshake *handshake, qa_buffer *out, qa_error *e)
{
    if (!handshake || !out || (unsigned)handshake->kind > QA_UNIFIED_CONNECT) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid production handshake producer"); return false;
    }
    qa_unified_document *document = NULL;
    bool okay = qa_unified_document_create_handshake(handshake, &document, e) &&
        qa_unified_document_encode(document, out, e);
    qa_unified_document_destroy(document);
    return okay;
}
