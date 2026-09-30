#ifndef QA_LLM_STREAM_H
#define QA_LLM_STREAM_H
#include "qa/llm.h"
typedef struct llm_text { qa_buffer buffer; size_t capacity; } llm_text;
bool llm_fail(qa_error *, const char *);
bool llm_text_add(llm_text *, qa_bytes, qa_error *);
bool llm_text_string(llm_text *, const char *, qa_error *);
void llm_text_clear(llm_text *);
typedef struct llm_stream {
    qa_llm_provider provider;
    llm_text raw, data, event, text;
    size_t cursor, total_bytes;
    uint8_t utf8_tail[4]; size_t utf8_count;
    bool have_data, received, stopped, completed, finished, done;
    void *context;
    bool (*emit)(void *, qa_bytes, qa_error *);
} llm_stream;
void llm_stream_destroy(llm_stream *);
bool llm_stream_feed(llm_stream *, qa_bytes, qa_error *);
bool llm_stream_finish(llm_stream *, qa_error *);
#endif
