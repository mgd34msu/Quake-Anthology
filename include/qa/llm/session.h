/*
 * Port of src/llm/session.ts: LlmSession. The Promise chain that
 * serializes turns collapses to direct synchronous calls (the C caller
 * is single-threaded through a turn); ordering, generation checks and
 * transcript trimming are unchanged. Turn/transcript limits count bytes
 * here where TS counts UTF-16 code units (identical for ASCII).
 * GPL-2.0-or-later.
 */
#ifndef QA_LLM_SESSION_H
#define QA_LLM_SESSION_H

#include "qa/llm/request.h"

/* LlmSessionTransport. *out_text is malloc'd on success. */
typedef bool (*qa_llm_session_transport_fn)(void *ctx, const qa_llm_request_input *input,
                                            char **out_text, qa_llm_error *error);
/* fingerprint(): borrowed text, copied by the session; NULL means "". */
typedef const char *(*qa_llm_session_fingerprint_fn)(void *ctx);

typedef struct qa_llm_session qa_llm_session;

qa_llm_session *qa_llm_session_create(qa_llm_session_transport_fn transport, void *transport_ctx,
                                      qa_llm_session_fingerprint_fn fingerprint, void *fingerprint_ctx);
void qa_llm_session_destroy(qa_llm_session *session);

const char *qa_llm_session_id(const qa_llm_session *session);
bool qa_llm_session_active(const qa_llm_session *session);
bool qa_llm_session_primed(const qa_llm_session *session);
size_t qa_llm_session_turn_count(const qa_llm_session *session);

bool qa_llm_session_prime(qa_llm_session *session, const char *instructions, qa_llm_error *error);
bool qa_llm_session_request(qa_llm_session *session, const qa_llm_request_input *input,
                            char **out_text, qa_llm_error *error);
void qa_llm_session_invalidate(qa_llm_session *session);
void qa_llm_session_close(qa_llm_session *session);

#endif
