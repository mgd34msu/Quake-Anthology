/*
 * Confirmation-gate review checks for src/llm. Each expected value comes
 * from running the TypeScript under bun. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/llm/auth.h"
#include "qa/llm/codex.h"
#include "qa/llm/errors.h"
#include "qa/llm/models.h"
#include "qa/llm/request.h"
#include "qa/llm/session.h"
#include "qa/llm/sse.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

static qa_llm_sse_action count_event(void *ctx, const qa_llm_sse_event *event, qa_llm_error *error) {
  (void)event;
  (void)error;
  (*(int *)ctx)++;
  return QA_LLM_SSE_CONTINUE;
}

/* bun: a chunk "data: {\"x\":1}\n\n" + 0xC0 rejects with "Could not read the LLM
 * response stream." and delivers no event (the fatal decoder throws on the
 * whole chunk; C0 can never start a sequence). C waited for more bytes and
 * delivered the event first. */
static void test_sse_invalid_tail(void) {
  static const char text[] = "data: {\"x\":1}\n\n\xc0";
  qa_llm_http_response response;
  qa_llm_response_init_memory(&response, 200, "text/event-stream", text, sizeof text - 1, 0);
  qa_llm_abort_signal signal;
  qa_llm_signal_init(&signal);
  qa_llm_error error;
  qa_llm_error_clear(&error);
  int events = 0;
  CHECK(!qa_llm_consume_sse(&response, &signal, count_event, &events, true, &error));
  CHECK(events == 0);
  CHECK(strcmp(error.message, "Could not read the LLM response stream.") == 0);
  qa_llm_response_dispose(&response);
}

static qa_llm_json *parse(const char *text) {
  qa_llm_json *value = NULL;
  CHECK(qa_llm_json_parse(text, strlen(text), &value));
  return value;
}

/* bun: display_name null falls back to the slug (`??`): ["m1"]; C failed with
 * "The service returned invalid model metadata.". */
static void test_codex_display_name_null(void) {
  qa_llm_json *value = parse(
      "{\"models\":[{\"visibility\":\"list\",\"slug\":\"m1\",\"supported_reasoning_levels\":[],"
      "\"priority\":1,\"display_name\":null}]}");
  qa_llm_model_list list;
  qa_llm_error error;
  qa_llm_error_clear(&error);
  CHECK(qa_llm_parse_codex_models(value, &list, &error));
  CHECK(list.count == 1 && strcmp(list.items[0].name, "m1") == 0);
  qa_llm_model_list_free(&list);
  qa_llm_json_free(value);
}

/* bun: an id of 200 "é" (200 UTF-16 units, 400 bytes) is accepted (length 1);
 * C counted bytes and rejected it. */
static void test_model_length_utf16(void) {
  char text[1024];
  size_t at = 0;
  at += (size_t)snprintf(text, sizeof text, "{\"data\":[{\"id\":\"");
  for (int i = 0; i < 200; i++) {
    text[at++] = (char)0xc3;
    text[at++] = (char)0xa9;
  }
  snprintf(text + at, sizeof text - at, "\"}]}");
  qa_llm_json *value = parse(text);
  qa_llm_model_list list;
  qa_llm_error error;
  qa_llm_error_clear(&error);
  CHECK(qa_llm_parse_api_models(value, false, &list, &error));
  CHECK(list.count == 1);
  qa_llm_model_list_free(&list);
  qa_llm_json_free(value);
}

/* bun: chatgpt_account_id " " (em space) trims to "" and is rejected; C's
 * ASCII trim accepted it. A payload with an invalid UTF-8 byte elsewhere
 * decodes with U+FFFD (Buffer.toString) and yields "acct"; C rejected it. */
static void test_account_id(void) {
  static const char em_payload[] = "eyJodHRwczovL2FwaS5vcGVuYWkuY29tL2F1dGgiOnsiY2hhdGdwdF9hY2NvdW50X2lkIjoi4oCDIn19";
  char token[512];
  snprintf(token, sizeof token, "h.%s.s", em_payload);
  char *id = NULL;
  qa_llm_error error;
  qa_llm_error_clear(&error);
  CHECK(!qa_llm_subscription_account_id(token, &id, &error));
  CHECK(strcmp(error.message, "Subscription credential has no valid ChatGPT account ID. Sign in again.") == 0);
  snprintf(token, sizeof token, "h.%s.s",
           "eyJuIjoi_yIsImh0dHBzOi8vYXBpLm9wZW5haS5jb20vYXV0aCI6eyJjaGF0Z3B0X2FjY291bnRfaWQiOiJhY2N0In19");
  CHECK(qa_llm_subscription_account_id(token, &id, &error));
  CHECK(id != NULL && strcmp(id, "acct") == 0);
  free(id);
}

typedef struct seen_signals {
  qa_llm_abort_signal *signals[4];
  int count;
} seen_signals;

static bool record_transport(void *ctx, const qa_llm_request_input *input, char **out_text, qa_llm_error *error) {
  (void)error;
  seen_signals *seen = ctx;
  if (seen->count < 4) seen->signals[seen->count++] = input->signal;
  *out_text = qa_llm_strdup("READY");
  return true;
}

/* TS PLATFORM-013: the warm-up turn run by request() receives the caller's
 * signal. C primed without it. */
static void test_session_prime_signal(void) {
  seen_signals seen = {{NULL}, 0};
  qa_llm_session *session = qa_llm_session_create(record_transport, &seen, NULL, NULL);
  qa_llm_abort_signal signal;
  qa_llm_signal_init(&signal);
  qa_llm_request_input input;
  memset(&input, 0, sizeof input);
  input.prompt = "hi";
  input.instructions = "turn";
  input.session_instructions = "CATALOG";
  input.signal = &signal;
  char *answer = NULL;
  qa_llm_error error;
  qa_llm_error_clear(&error);
  CHECK(qa_llm_session_request(session, &input, &answer, &error));
  free(answer);
  CHECK(seen.count == 2 && seen.signals[0] == &signal);
  qa_llm_session_destroy(session);
}

/* JS trim strips every WhiteSpace/LineTerminator code point (U+2003, U+FEFF
 * at both ends). */
static void test_trim(void) {
  char *trimmed = qa_llm_trim_dup("\xe2\x80\x83 a b\xef\xbb\xbf\xe2\x80\xa8");
  CHECK(strcmp(trimmed, "a b") == 0);
  free(trimmed);
  CHECK(qa_llm_utf16_length("a\xc3\xa9\xf0\x9f\x98\x80") == 4);
}

int main(void) {
  test_sse_invalid_tail();
  test_codex_display_name_null();
  test_model_length_utf16();
  test_account_id();
  test_session_prime_signal();
  test_trim();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
