/*
 * Port of src/llm/auth.ts. The TypeScript passes `unknown` JSON values
 * between llm modules and exports record() from this file, so the C JSON
 * DOM (parse/stringify with JS key order) lives here too. The worker
 * thread becomes the in-process listener of auth_worker.h, driven from
 * qa_llm_login_subscription's synchronous wait loop; Promise races
 * become abort/deadline checks in that loop.
 * GPL-2.0-or-later.
 */
#ifndef QA_LLM_AUTH_H
#define QA_LLM_AUTH_H

#include "qa/llm/request.h"

/* JSON value (TS `unknown` after JSON.parse). Object keys keep JS
 * insertion order; object_set replaces in place like a JS property. */
typedef enum qa_llm_json_kind {
  QA_LLM_JSON_NULL,
  QA_LLM_JSON_BOOL,
  QA_LLM_JSON_NUMBER,
  QA_LLM_JSON_STRING,
  QA_LLM_JSON_ARRAY,
  QA_LLM_JSON_OBJECT,
} qa_llm_json_kind;

typedef struct qa_llm_json {
  qa_llm_json_kind kind;
  bool boolean;
  double number;
  char *string;
  struct qa_llm_json **items; /* array elements or object values */
  char **keys; /* object keys, parallel to items; NULL for arrays */
  size_t count;
  size_t capacity;
} qa_llm_json;

qa_llm_json *qa_llm_json_null(void);
qa_llm_json *qa_llm_json_bool(bool value);
qa_llm_json *qa_llm_json_number(double value);
qa_llm_json *qa_llm_json_string(const char *value);
qa_llm_json *qa_llm_json_array(void);
qa_llm_json *qa_llm_json_object(void);
void qa_llm_json_free(qa_llm_json *value);
void qa_llm_json_array_push(qa_llm_json *array, qa_llm_json *item);
void qa_llm_json_object_set(qa_llm_json *object, const char *key, qa_llm_json *value);
/* item["key"] as a borrowed pointer; NULL mirrors undefined. */
qa_llm_json *qa_llm_json_get(const qa_llm_json *object, const char *key);
bool qa_llm_json_is_array(const qa_llm_json *value);
bool qa_llm_json_is_object(const qa_llm_json *value);
/* JSON.parse; false mirrors the SyntaxError (callers map messages). */
bool qa_llm_json_parse(const char *text, size_t length, qa_llm_json **out);
/* JSON.stringify (compact, insertion ordered). Caller frees. */
char *qa_llm_json_stringify(const qa_llm_json *value);

/* record(value): false with "Invalid LLM settings data." unless object. */
bool qa_llm_record(const qa_llm_json *value, qa_llm_error *error);

/* SubscriptionCredential; strings/scopes owned. */
typedef struct qa_llm_subscription_credential {
  char *access_token;
  char *refresh_token;
  char *token_type;
  double expires_at;
  char **scopes;
  size_t scope_count;
} qa_llm_subscription_credential;

void qa_llm_subscription_credential_free(qa_llm_subscription_credential *credential);
/* parseSubscription(value). */
bool qa_llm_parse_subscription(const qa_llm_json *value, qa_llm_subscription_credential *out, qa_llm_error *error);
/* The credential as stored by settings' JSON.stringify. Caller frees. */
qa_llm_json *qa_llm_subscription_to_json(const qa_llm_subscription_credential *credential);

typedef bool (*qa_llm_open_browser_fn)(void *ctx, const char *url, qa_llm_abort_signal *signal, qa_llm_error *error);

/* SubscriptionAuthOptions; has_* mirror undefined. */
typedef struct qa_llm_subscription_auth_options {
  const char *authorize_url;
  const char *token_url;
  bool has_callback_port;
  int callback_port;
  bool has_callback_timeout;
  double callback_timeout_ms;
  bool has_fetch_timeout;
  double fetch_timeout_ms;
  qa_llm_open_browser_fn open_browser; /* NULL: TS default (unavailable) */
  void *open_browser_ctx;
  qa_llm_fetch_ref fetch;
} qa_llm_subscription_auth_options;

/* cancelled() / checkAbort (subscription wording). */
void qa_llm_subscription_cancelled(qa_llm_error *error);
bool qa_llm_check_abort(qa_llm_abort_signal *signal, qa_llm_error *error);

/* loginSubscription / refreshSubscription (synchronous; see header). */
bool qa_llm_login_subscription(const qa_llm_subscription_auth_options *options, qa_llm_abort_signal *signal,
                               qa_llm_subscription_credential *out, qa_llm_error *error);
bool qa_llm_refresh_subscription(const qa_llm_subscription_auth_options *options,
                                 const qa_llm_subscription_credential *credential, qa_llm_abort_signal *signal,
                                 qa_llm_subscription_credential *out, qa_llm_error *error);

#endif
