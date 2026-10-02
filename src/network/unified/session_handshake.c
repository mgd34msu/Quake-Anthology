#include "qa/network_unified_session.h"

#include <stdio.h>
#include <string.h>

static bool token_read(const qa_json_document *json, qa_json_id object, const char *name,
    qa_unified_token *out, qa_error *e)
{
    qa_buffer text = {0};
    if (!qa_json_string(json, qa_json_get(json, object, name), &text, e)) return false;
    bool ok = qa_unified_token_parse((const char *)text.data, out, e);
    qa_buffer_free(&text);
    return ok;
}

bool qa_unified_handshake_read(qa_bytes bytes, qa_unified_handshake *out, qa_error *e)
{
    if (!out) { qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing production handshake output"); return false; }
    qa_unified_document *document = NULL;
    if (!qa_unified_document_decode(QA_UNIFIED_HANDSHAKE_DOCUMENT, bytes, &document, e)) return false;
    const qa_json_document *json = qa_unified_document_json(document);
    qa_json_id value = qa_json_get(json, qa_unified_document_root(document), "value");
    qa_json_id kind = qa_json_get(json, value, "kind");
    qa_unified_handshake handshake = {.kind = qa_json_string_equal(json,kind,"hello") ? QA_UNIFIED_HELLO :
        qa_json_string_equal(json,kind,"challenge") ? QA_UNIFIED_CHALLENGE : QA_UNIFIED_CONNECT};
    bool ok = token_read(json,value,"nonce",&handshake.nonce,e) &&
        (handshake.kind == QA_UNIFIED_HELLO || token_read(json,value,"token",&handshake.token,e));
    qa_unified_document_destroy(document);
    if (ok) *out = handshake;
    return ok;
}

bool qa_unified_handshake_write(const qa_unified_handshake *handshake, qa_buffer *out, qa_error *e)
{
    if (!handshake || !out || (unsigned)handshake->kind > QA_UNIFIED_CONNECT) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid production handshake producer"); return false;
    }
    char nonce[33], token[33], text[256];
    qa_unified_token_format(handshake->nonce, nonce);
    qa_unified_token_format(handshake->token, token);
    const char *kind = handshake->kind == QA_UNIFIED_HELLO ? "hello" :
        handshake->kind == QA_UNIFIED_CHALLENGE ? "challenge" : "connect";
    int written = handshake->kind == QA_UNIFIED_HELLO ?
        snprintf(text,sizeof(text),"{\"schema\":\"qts-connect\",\"version\":1,\"value\":{\"kind\":\"%s\",\"nonce\":\"%s\"}}",kind,nonce) :
        snprintf(text,sizeof(text),"{\"schema\":\"qts-connect\",\"version\":1,\"value\":{\"kind\":\"%s\",\"nonce\":\"%s\",\"token\":\"%s\"}}",kind,nonce,token);
    if (written < 0 || (size_t)written >= sizeof(text)) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Encoding production handshake envelope"); return false;
    }
    qa_unified_document *document = NULL;
    bool ok = qa_unified_document_create(QA_UNIFIED_HANDSHAKE_DOCUMENT,
        (qa_bytes){(const uint8_t *)text,(size_t)written},&document,e) && qa_unified_document_encode(document,out,e);
    qa_unified_document_destroy(document);
    return ok;
}
