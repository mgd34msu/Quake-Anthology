#ifndef QA_JSON_WRITER_H
#define QA_JSON_WRITER_H
#include "qa/common.h"

typedef struct qa_json_writer_scope {
    bool object, pending_key, nonempty;
} qa_json_writer_scope;
typedef struct qa_json_writer {
    qa_buffer bytes;
    size_t capacity, depth;
    qa_json_writer_scope scopes[128];
    bool root, failed;
    qa_error failure;
} qa_json_writer;
/* Zero initialization is valid. Writes retain the first error. Finish transfers
 * a complete, NUL-terminated document; destroy also handles failed writers. */
void qa_json_writer_destroy(qa_json_writer *);
void qa_json_writer_object(qa_json_writer *);
void qa_json_writer_array(qa_json_writer *);
void qa_json_writer_end(qa_json_writer *);
void qa_json_writer_key(qa_json_writer *, const char *);
void qa_json_writer_string(qa_json_writer *, const char *);
void qa_json_writer_bytes(qa_json_writer *, qa_bytes utf8);
void qa_json_writer_number(qa_json_writer *, double);
void qa_json_writer_bool(qa_json_writer *, bool);
void qa_json_writer_null(qa_json_writer *);
bool qa_json_writer_finish(qa_json_writer *, qa_buffer *, qa_error *);
#endif
