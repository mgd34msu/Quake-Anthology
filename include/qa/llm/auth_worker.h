/*
 * Port of src/llm/auth-worker.ts. C accommodation: there is no worker
 * thread spawn in the C library; the loopback listener and its parent
 * message protocol are plain functions. qa_llm_auth_worker_start opens
 * the same 127.0.0.1 listener, posts the same {kind:...} messages
 * through a callback, and the owner (auth.ts's login flow, ported in
 * auth.c) pumps qa_llm_auth_worker_serve_once while it waits. The pure
 * request/response mapping is qa_llm_auth_worker_handle_request.
 * Startup validation throws plain Error in TS, so it aborts here with
 * the identical message via qa_llm_plain_error.
 * GPL-2.0-or-later.
 */
#ifndef QA_LLM_AUTH_WORKER_H
#define QA_LLM_AUTH_WORKER_H

#include "qa/llm/auth.h"

typedef enum qa_llm_auth_worker_message_kind {
  QA_LLM_AUTH_WORKER_READY,
  QA_LLM_AUTH_WORKER_CODE,
  QA_LLM_AUTH_WORKER_STATE_MISMATCH,
  QA_LLM_AUTH_WORKER_DENIED,
  QA_LLM_AUTH_WORKER_LISTEN_ERROR,
  QA_LLM_AUTH_WORKER_STOPPED,
} qa_llm_auth_worker_message_kind;

typedef struct qa_llm_auth_worker_message {
  qa_llm_auth_worker_message_kind kind;
  int port; /* ready */
  const char *code; /* code; borrowed for the duration of the call */
} qa_llm_auth_worker_message;

const char *qa_llm_auth_worker_message_kind_name(qa_llm_auth_worker_message_kind kind);

typedef void (*qa_llm_auth_worker_post_fn)(void *ctx, const qa_llm_auth_worker_message *message);

typedef struct qa_llm_auth_worker qa_llm_auth_worker;

/* workerData validation (fields() + the port/state checks). Aborts with
 * "Invalid subscription callback worker data." on any violation. */
void qa_llm_auth_worker_parse_data(const qa_llm_json *worker_data, int *out_port, char **out_state);
void qa_llm_auth_worker_validate(int requested_port, const char *expected_state);

/* Starts the listener and posts ready (or listen-error and returns
 * NULL, mirroring the worker posting then throwing). */
qa_llm_auth_worker *qa_llm_auth_worker_start(int requested_port, const char *expected_state,
                                             qa_llm_auth_worker_post_fn post, void *post_ctx);
int qa_llm_auth_worker_port(const qa_llm_auth_worker *worker);

/* The Bun.serve fetch handler: maps one request to (status, body) and
 * posts code/state-mismatch/denied. target is path[?query] as sent. */
void qa_llm_auth_worker_handle_request(qa_llm_auth_worker *worker, const char *method, const char *target,
                                       int *out_status, char **out_body);
/* Accepts and answers at most one queued connection within timeout_ms.
 * Returns false only when the listener is already stopped. */
bool qa_llm_auth_worker_serve_once(qa_llm_auth_worker *worker, int timeout_ms);
/* port.on("message"): {kind:"stop", force?} stops and posts stopped. */
void qa_llm_auth_worker_handle_message(qa_llm_auth_worker *worker, const qa_llm_json *message);
/* Stops (idempotent, posts stopped once message semantics require) and
 * frees. Pass post_stopped false for the terminate() teardown path. */
void qa_llm_auth_worker_destroy(qa_llm_auth_worker *worker, bool post_stopped);

#endif
