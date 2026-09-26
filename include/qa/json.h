#ifndef QA_JSON_H
#define QA_JSON_H

#include "qa/common.h"

typedef struct qa_json_document qa_json_document;
typedef uint32_t qa_json_id;
#define QA_JSON_NONE UINT32_MAX

typedef enum qa_json_kind {
    QA_JSON_NULL, QA_JSON_BOOL, QA_JSON_NUMBER, QA_JSON_STRING,
    QA_JSON_ARRAY, QA_JSON_OBJECT, QA_JSON_INVALID
} qa_json_kind;

/* The document owns its index and borrows source bytes until destruction.
 * Parsing checks the complete document, including UTF-8 and escaped Unicode.
 * Failed calls leave outputs unchanged. No document mutation after parsing. */
bool qa_json_parse(qa_bytes source, qa_json_document **out, qa_error *error);
void qa_json_destroy(qa_json_document *document);
qa_json_id qa_json_root(const qa_json_document *document);
qa_json_kind qa_json_type(const qa_json_document *document, qa_json_id id);
size_t qa_json_size(const qa_json_document *document, qa_json_id container);
/* Indexing is constant-time and preserves source order, including duplicate
 * keys. Hashed lookup returns the last matching key, as JSON.parse does. */
qa_json_id qa_json_at(const qa_json_document *document, qa_json_id container, size_t index);
qa_json_id qa_json_key_at(const qa_json_document *document, qa_json_id object, size_t index);
qa_json_id qa_json_get(const qa_json_document *document, qa_json_id object, const char *key);
qa_bytes qa_json_source(const qa_json_document *document, qa_json_id id);
bool qa_json_bool(const qa_json_document *document, qa_json_id id, bool *out, qa_error *error);
bool qa_json_number(const qa_json_document *document, qa_json_id id, double *out, qa_error *error);
bool qa_json_i64(const qa_json_document *document, qa_json_id id, int64_t *out, qa_error *error);
bool qa_json_u64(const qa_json_document *document, qa_json_id id, uint64_t *out, qa_error *error);
/* Decoded strings are owned UTF-8, NUL terminated; size excludes the NUL.
 * Embedded NULs remain in the counted bytes. Release with qa_buffer_free. */
bool qa_json_string(const qa_json_document *document, qa_json_id id, qa_buffer *out, qa_error *error);
bool qa_json_string_equal(const qa_json_document *document, qa_json_id id, const char *text);
/* Encode a counted UTF-8 string as a complete quoted JSON value. */
bool qa_json_quote(qa_bytes text, qa_buffer *out, qa_error *error);

#endif
