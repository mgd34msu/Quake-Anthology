/*
 * Port of src/llm/sse.ts: consumeSse over the pull-reader response of
 * request.h. The onEvent callback returns continue/stop (TS true), or
 * fails with the callback's own LlmSettingsError in *error.
 * GPL-2.0-or-later.
 */
#ifndef QA_LLM_SSE_H
#define QA_LLM_SSE_H

#include "qa/llm/request.h"

#define QA_LLM_MAX_STREAM_BYTES 1048576

typedef struct qa_llm_sse_event {
  const char *event;
  const char *data;
} qa_llm_sse_event;

typedef enum qa_llm_sse_action {
  QA_LLM_SSE_CONTINUE,
  QA_LLM_SSE_STOP,
  QA_LLM_SSE_FAIL,
} qa_llm_sse_action;

typedef qa_llm_sse_action (*qa_llm_sse_on_event_fn)(void *ctx, const qa_llm_sse_event *event, qa_llm_error *error);

/* options.requireContentType defaults true in TS; pass explicitly. */
bool qa_llm_consume_sse(qa_llm_http_response *response, qa_llm_abort_signal *signal,
                        qa_llm_sse_on_event_fn on_event, void *ctx, bool require_content_type, qa_llm_error *error);

#endif
