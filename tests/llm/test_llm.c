#define _POSIX_C_SOURCE 200809L
/* Ported cases from tests/llm/{codex,models,request,responses,session,settings}.test.ts.
 * TS mocks fetch: here a qa_llm_fetch_ref is injected at exactly the same
 * seams. Cases whose TS form needs concurrent async interleaving
 * (shared-refresh waiters, late fetch resolution after abort, a callback
 * answering while the main thread busy-loops, real loopback servers for
 * the *default* fetch) are adapted to the synchronous port: sequential
 * calls, callbacks that abort/close mid-flight, and a forked child
 * performing the OAuth loopback callback over a real socket.
 * GPL-2.0-or-later. */
#include "qa/llm/api.h"
#include "qa/llm/auth.h"
#include "qa/llm/auth_worker.h"
#include "qa/llm/codex.h"
#include "qa/llm/errors.h"
#include "qa/llm/models.h"
#include "qa/llm/request.h"
#include "qa/llm/responses.h"
#include "qa/llm/session.h"
#include "qa/llm/settings.h"
#include "qa/llm/sse.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void sleep_ms(int ms) {
  struct timespec delay = { ms / 1000, (long)(ms % 1000) * 1000000L };
  nanosleep(&delay, NULL);
}

static int failures;
static int checks;

#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      failures++; \
    } \
  } while (0)

#define CHECK_STR(actual, expected) do { \
    checks++; \
    const char *a_ = (actual); \
    if (a_ == NULL || strcmp(a_, (expected)) != 0) { \
      fprintf(stderr, "FAIL %s:%d: got \"%s\", want \"%s\"\n", __FILE__, __LINE__, a_ != NULL ? a_ : "(null)", (expected)); \
      failures++; \
    } \
  } while (0)

#define CHECK_CONTAINS(haystack, needle) do { \
    checks++; \
    const char *h_ = (haystack); \
    if (h_ == NULL || strstr(h_, (needle)) == NULL) { \
      fprintf(stderr, "FAIL %s:%d: \"%s\" does not contain \"%s\"\n", __FILE__, __LINE__, h_ != NULL ? h_ : "(null)", (needle)); \
      failures++; \
    } \
  } while (0)

#define CHECK_NOT_CONTAINS(haystack, needle) do { \
    checks++; \
    const char *h_ = (haystack); \
    if (h_ != NULL && strstr(h_, (needle)) != NULL) { \
      fprintf(stderr, "FAIL %s:%d: \"%s\" contains \"%s\"\n", __FILE__, __LINE__, h_, (needle)); \
      failures++; \
    } \
  } while (0)

/* ---- mock fetch scaffolding ---- */

typedef struct captured {
  char *url;
  char *method;
  char *body;
  qa_llm_header *headers;
  size_t header_count;
  bool redirect_error;
} captured;

static void captured_store(captured *cap, const char *url, const qa_llm_http_init *init) {
  free(cap->url);
  free(cap->method);
  free(cap->body);
  for (size_t i = 0; i < cap->header_count; i++) {
    free((void *)cap->headers[i].name);
    free((void *)cap->headers[i].value);
  }
  free(cap->headers);
  memset(cap, 0, sizeof *cap);
  cap->url = qa_llm_strdup(url);
  cap->method = qa_llm_strdup(init->method != NULL ? init->method : "");
  cap->body = init->body != NULL ? qa_llm_strndup(init->body, init->body_length) : qa_llm_strdup("");
  cap->redirect_error = init->redirect_error;
  if (init->header_count > 0) {
    cap->headers = calloc(init->header_count, sizeof *cap->headers);
    for (size_t i = 0; i < init->header_count; i++) {
      cap->headers[i].name = qa_llm_strdup(init->headers[i].name);
      cap->headers[i].value = qa_llm_strdup(init->headers[i].value);
    }
    cap->header_count = init->header_count;
  }
}

static const char *captured_header(const captured *cap, const char *name) {
  for (size_t i = 0; i < cap->header_count; i++) {
    const char *a = cap->headers[i].name;
    const char *b = name;
    bool equal = true;
    while (*a != '\0' && *b != '\0') {
      char ca = *a, cb = *b;
      if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
      if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
      if (ca != cb) { equal = false; break; }
      a++;
      b++;
    }
    if (equal && *a == '\0' && *b == '\0') return cap->headers[i].value;
  }
  return NULL;
}

typedef bool (*mock_handler)(void *ctx, const char *url, const qa_llm_http_init *init,
                             qa_llm_http_response *out, qa_llm_error *error);

typedef struct mock_fetch {
  mock_handler handler;
  void *handler_ctx;
  captured last;
  int calls;
} mock_fetch;

static bool mock_fetch_invoke(void *ctx, const char *url, const qa_llm_http_init *init,
                              qa_llm_http_response *out, qa_llm_error *error) {
  mock_fetch *mock = ctx;
  mock->calls++;
  captured_store(&mock->last, url, init);
  return mock->handler(mock->handler_ctx, url, init, out, error);
}

static qa_llm_fetch_ref mock_ref(mock_fetch *mock) {
  qa_llm_fetch_ref ref = { mock_fetch_invoke, mock };
  return ref;
}

static void sse_response(qa_llm_http_response *out, const char *text) {
  qa_llm_response_init_memory(out, 200, "text/event-stream", text, strlen(text), 1);
}

static bool fixed_response_handler(void *ctx, const char *url, const qa_llm_http_init *init,
                                   qa_llm_http_response *out, qa_llm_error *error) {
  (void)url;
  (void)init;
  (void)error;
  sse_response(out, (const char *)ctx);
  return true;
}

static char *event_json(const char *json) {
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  qa_llm_strbuf_appendf(&out, "data: %s\n\n", json);
  return qa_llm_strbuf_steal(&out);
}

static char *join2(const char *a, const char *b) {
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  qa_llm_strbuf_append(&out, a);
  qa_llm_strbuf_append(&out, b);
  return qa_llm_strbuf_steal(&out);
}

static char *make_token(const char *label, const char *account_id) {
  qa_llm_strbuf payload;
  qa_llm_strbuf_init(&payload);
  qa_llm_strbuf_appendf(&payload, "{\"https://api.openai.com/auth\":{\"chatgpt_account_id\":\"%s\"}}", account_id);
  char *encoded = qa_llm_base64url_encode((const uint8_t *)payload.data, payload.length);
  qa_llm_strbuf_free(&payload);
  qa_llm_strbuf token;
  qa_llm_strbuf_init(&token);
  qa_llm_strbuf_appendf(&token, "%s.%s.signature", label, encoded);
  free(encoded);
  return qa_llm_strbuf_steal(&token);
}

static void on_text_collect(void *ctx, const char *text) {
  qa_llm_strbuf *all = ctx;
  if (all->length > 0) qa_llm_strbuf_append_char(all, '|');
  qa_llm_strbuf_append(all, text);
}

static qa_llm_transport_request transport_input(qa_llm_abort_signal *signal) {
  qa_llm_transport_request input;
  memset(&input, 0, sizeof input);
  input.input.prompt = "hello";
  input.input.instructions = "Reply plainly.";
  input.input.signal = signal;
  input.model = "openai:test-model";
  return input;
}

/* ---- errors ---- */

static void test_errors(void) {
  qa_llm_error error;
  qa_llm_error_http(&error, 401);
  CHECK_STR(error.message, "LLM service returned HTTP 401. Check the selected provider's credential or sign in again.");
  CHECK(error.http_status == 401);
  qa_llm_error_http(&error, 429);
  CHECK_STR(error.message, "LLM service returned HTTP 429. The service limit was reached. Try again later.");
  qa_llm_error_http(&error, 404);
  CHECK_STR(error.message, "LLM service returned HTTP 404. Check the model and service settings.");
  qa_llm_error_http(&error, 503);
  CHECK_STR(error.message, "LLM service returned HTTP 503. Try again later.");
}

/* ---- codex ---- */

static void codex_credential(qa_llm_subscription_credential *credential, char *token) {
  memset(credential, 0, sizeof *credential);
  credential->access_token = token;
  credential->refresh_token = qa_llm_strdup("test-refresh");
  credential->token_type = qa_llm_strdup("Bearer");
  credential->expires_at = 1234;
}

static void test_codex(void) {
  qa_llm_error error;
  qa_llm_abort_signal signal;
  char *token = make_token("header", "test-account");
  qa_llm_subscription_credential credential;
  codex_credential(&credential, token);

  char *id = NULL;
  CHECK(qa_llm_subscription_account_id(token, &id, &error));
  CHECK_STR(id, "test-account");
  free(id);
  char *key = qa_llm_prompt_cache_key("openai:test-model");
  char expected[65];
  qa_llm_sha256_hex("quake-typescript:test-model", strlen("quake-typescript:test-model"), expected);
  CHECK_STR(key, expected);

  /* fragmented Unicode deltas over 1-byte reads */
  {
    char *e1 = event_json("{\"type\":\"response.created\"}");
    char *e2 = event_json("{\"type\":\"response.output_text.delta\",\"delta\":\"caf\xc3\xa9 \"}");
    char *e3 = event_json("{\"type\":\"response.output_text.delta\",\"delta\":\"\xf0\x9f\x8e\xae\"}");
    char *e4 = event_json("{\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}");
    char *joined = join2(join2(e1, e2), join2(e3, e4));
    mock_fetch mock = {0};
    mock.handler = fixed_response_handler;
    mock.handler_ctx = joined;
    qa_llm_signal_init(&signal);
    qa_llm_transport_request input = transport_input(&signal);
    qa_llm_strbuf deltas;
    qa_llm_strbuf_init(&deltas);
    input.input.on_text = on_text_collect;
    input.input.on_text_ctx = &deltas;
    char *result = NULL;
    CHECK(qa_llm_request_codex(&input, &credential, mock_ref(&mock), &result, &error));
    CHECK_STR(result, "caf\xc3\xa9 \xf0\x9f\x8e\xae");
    CHECK_STR(deltas.data, "caf\xc3\xa9 |\xf0\x9f\x8e\xae");
    CHECK_STR(mock.last.url, "https://chatgpt.com/backend-api/codex/responses");
    CHECK_STR(mock.last.method, "POST");
    CHECK(mock.last.redirect_error);
    const char *authorization = captured_header(&mock.last, "authorization");
    qa_llm_strbuf expected_auth;
    qa_llm_strbuf_init(&expected_auth);
    qa_llm_strbuf_appendf(&expected_auth, "Bearer %s", token);
    CHECK_STR(authorization, expected_auth.data);
    qa_llm_strbuf_free(&expected_auth);
    CHECK_STR(captured_header(&mock.last, "chatgpt-account-id"), "test-account");
    CHECK_STR(captured_header(&mock.last, "OpenAI-Beta"), "responses=experimental");
    CHECK_STR(captured_header(&mock.last, "originator"), "pi");
    CHECK_STR(captured_header(&mock.last, "accept"), "text/event-stream");
    CHECK_CONTAINS(mock.last.body, "\"model\":\"test-model\"");
    CHECK_CONTAINS(mock.last.body, "\"store\":false");
    CHECK_CONTAINS(mock.last.body, "\"stream\":true");
    CHECK_CONTAINS(mock.last.body, "\"instructions\":\"Reply plainly.\"");
    CHECK_CONTAINS(mock.last.body, "\"text\":\"hello\"");
    qa_llm_strbuf cache;
    qa_llm_strbuf_init(&cache);
    qa_llm_strbuf_appendf(&cache, "\"prompt_cache_key\":\"%s\"", key);
    CHECK_CONTAINS(mock.last.body, cache.data);
    qa_llm_strbuf_free(&cache);
    qa_llm_strbuf_free(&deltas);
    free(result);
    free(joined);
    free(e1); free(e2); free(e3); free(e4);
  }
  free(key);

  /* absent account ID rejects before fetch */
  {
    qa_llm_subscription_credential bad;
    codex_credential(&bad, qa_llm_strdup("secret.invalid.signature"));
    mock_fetch mock = {0};
    mock.handler = fixed_response_handler;
    mock.handler_ctx = (void *)"";
    qa_llm_signal_init(&signal);
    qa_llm_transport_request input = transport_input(&signal);
    char *result = NULL;
    CHECK(!qa_llm_request_codex(&input, &bad, mock_ref(&mock), &result, &error));
    CHECK_CONTAINS(error.message, "Sign in again");
    CHECK(mock.calls == 0);
    qa_llm_subscription_credential_free(&bad);
  }

  /* failure event types, truncated, malformed, empty */
  static const char *const failure_types[] = { "response.failed", "response.incomplete", "error" };
  for (size_t i = 0; i < 3; i++) {
    qa_llm_strbuf json;
    qa_llm_strbuf_init(&json);
    qa_llm_strbuf_appendf(&json, "{\"type\":\"%s\",\"error\":{\"message\":\"secret-provider-text\"}}", failure_types[i]);
    char *text = event_json(json.data);
    qa_llm_strbuf_free(&json);
    mock_fetch mock = {0};
    mock.handler = fixed_response_handler;
    mock.handler_ctx = text;
    qa_llm_signal_init(&signal);
    qa_llm_transport_request input = transport_input(&signal);
    char *result = NULL;
    CHECK(!qa_llm_request_codex(&input, &credential, mock_ref(&mock), &result, &error));
    CHECK_STR(error.message, "Subscription response failed. Try again.");
    free(text);
  }
  {
    char *text = event_json("{\"type\":\"response.output_text.delta\",\"delta\":\"quit\"}");
    mock_fetch mock = {0};
    mock.handler = fixed_response_handler;
    mock.handler_ctx = text;
    qa_llm_signal_init(&signal);
    qa_llm_transport_request input = transport_input(&signal);
    char *result = NULL;
    CHECK(!qa_llm_request_codex(&input, &credential, mock_ref(&mock), &result, &error));
    CHECK_CONTAINS(error.message, "before completion");
    free(text);
    mock_fetch mock2 = {0};
    mock2.handler = fixed_response_handler;
    mock2.handler_ctx = (void *)"data: secret-invalid-json\n\n";
    qa_llm_signal_init(&signal);
    input = transport_input(&signal);
    CHECK(!qa_llm_request_codex(&input, &credential, mock_ref(&mock2), &result, &error));
    CHECK_STR(error.message, "Invalid LLM response.");
  }
  for (int i = 0; i < 2; i++) {
    char *delta = event_json(i == 0 ? "{\"type\":\"response.output_text.delta\",\"delta\":\"\"}"
                                     : "{\"type\":\"response.output_text.delta\",\"delta\":\" \\n\"}");
    char *done = event_json("{\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}");
    char *text = join2(delta, done);
    mock_fetch mock = {0};
    mock.handler = fixed_response_handler;
    mock.handler_ctx = text;
    qa_llm_signal_init(&signal);
    qa_llm_transport_request input = transport_input(&signal);
    char *result = NULL;
    CHECK(!qa_llm_request_codex(&input, &credential, mock_ref(&mock), &result, &error));
    CHECK_CONTAINS(error.message, "Subscription returned no text.");
    free(delta); free(done); free(text);
  }

  /* HTTP status preserved without the body */
  for (int i = 0; i < 2; i++) {
    int status = i == 0 ? 401 : 403;
    mock_fetch mock = {0};
    mock.handler = fixed_response_handler;
    mock.handler_ctx = (void *)"secret-server-body";
    /* fixed handler returns 200; use a status handler instead */
    mock.handler = NULL;
    (void)mock;
    /* direct: build through a lambda-like static below */
    extern bool status_handler(void *, const char *, const qa_llm_http_init *, qa_llm_http_response *, qa_llm_error *);
    mock_fetch status_mock = {0};
    status_mock.handler = status_handler;
    status_mock.handler_ctx = (void *)(intptr_t)status;
    qa_llm_signal_init(&signal);
    qa_llm_transport_request input = transport_input(&signal);
    char *result = NULL;
    CHECK(!qa_llm_request_codex(&input, &credential, mock_ref(&status_mock), &result, &error));
    CHECK(error.http_status == status);
    CHECK_NOT_CONTAINS(error.message, "secret");
  }

  qa_llm_subscription_credential_free(&credential);
}

bool status_handler(void *ctx, const char *url, const qa_llm_http_init *init,
                    qa_llm_http_response *out, qa_llm_error *error) {
  (void)url;
  (void)init;
  (void)error;
  qa_llm_response_init_memory(out, (int)(intptr_t)ctx, "text/plain", "secret-server-body", 18, 0);
  return true;
}

typedef struct typed_response {
  const char *content_type; /* NULL omits */
  const char *text;
  int status;
} typed_response;

static bool typed_handler(void *ctx, const char *url, const qa_llm_http_init *init,
                          qa_llm_http_response *out, qa_llm_error *error) {
  (void)url; (void)init; (void)error;
  typed_response *typed = ctx;
  qa_llm_response_init_memory(out, typed->status == 0 ? 200 : typed->status, typed->content_type,
                              typed->text, strlen(typed->text), 0);
  return true;
}

static void test_codex_stream_shapes(void) {
  qa_llm_error error;
  qa_llm_abort_signal signal;
  char *token = make_token("header", "test-account");
  qa_llm_subscription_credential credential;
  codex_credential(&credential, token);
  char *d = event_json("{\"type\":\"response.output_text.delta\",\"delta\":\"answer\"}");
  char *c = event_json("{\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}");
  char *payload = join2(d, c);

  const char *types[] = { NULL, "application/octet-stream", "text/plain", "application/json" };
  for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
    typed_response typed = { types[i], payload, 200 };
    mock_fetch mock = {0};
    mock.handler = typed_handler;
    mock.handler_ctx = &typed;
    qa_llm_signal_init(&signal);
    qa_llm_transport_request input = transport_input(&signal);
    char *result = NULL;
    CHECK(qa_llm_request_codex(&input, &credential, mock_ref(&mock), &result, &error));
    CHECK_STR(result, "answer");
    free(result);
  }

  static const struct { const char *type; const char *body; const char *format; } non_sse[] = {
    { "application/json", "{\"status\":\"completed\",\"output_text\":\"secret-quit\"}", "JSON" },
    { "text/html", "<html>secret-provider-body</html>", "HTML" },
    { "text/plain; private=secret-header", "secret-plain-body", "plain text" },
    { "application/secret-header", "secret-other-body", "other content type" },
  };
  for (size_t i = 0; i < sizeof non_sse / sizeof non_sse[0]; i++) {
    typed_response typed = { non_sse[i].type, non_sse[i].body, 200 };
    mock_fetch mock = {0};
    mock.handler = typed_handler;
    mock.handler_ctx = &typed;
    qa_llm_signal_init(&signal);
    qa_llm_transport_request input = transport_input(&signal);
    qa_llm_strbuf deltas;
    qa_llm_strbuf_init(&deltas);
    input.input.on_text = on_text_collect;
    input.input.on_text_ctx = &deltas;
    char *result = NULL;
    CHECK(!qa_llm_request_codex(&input, &credential, mock_ref(&mock), &result, &error));
    qa_llm_strbuf expected;
    qa_llm_strbuf_init(&expected);
    qa_llm_strbuf_appendf(&expected, "LLM service returned no event data (HTTP 200, %s).", non_sse[i].format);
    CHECK_STR(error.message, expected.data);
    qa_llm_strbuf_free(&expected);
    CHECK(deltas.length == 0);
    qa_llm_strbuf_free(&deltas);
  }

  static const char *const rejected[] = {
    "{\"type\":\"response.refusal.delta\",\"delta\":\"secret-refusal\"}",
    "{\"type\":\"response.function_call_arguments.delta\",\"delta\":\"secret-tool\"}",
    "{\"type\":\"response.output_item.added\",\"item\":{\"type\":\"function_call\"}}",
    "{\"type\":\"response.content_part.done\",\"part\":{\"type\":\"refusal\"}}",
    "{\"type\":\"response.completed\",\"response\":{\"status\":\"completed\",\"output\":[{\"type\":\"function_call\"}]}}",
    "{\"type\":\"response.completed\",\"response\":{\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"refusal\"}]}]}}",
  };
  for (size_t i = 0; i < sizeof rejected / sizeof rejected[0]; i++) {
    char *delta = event_json("{\"type\":\"response.output_text.delta\",\"delta\":\"quit\"}");
    char *bad = event_json(rejected[i]);
    char *done = event_json("{\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}");
    char *text = join2(join2(delta, bad), done);
    mock_fetch mock = {0};
    mock.handler = fixed_response_handler;
    mock.handler_ctx = text;
    qa_llm_signal_init(&signal);
    qa_llm_transport_request input = transport_input(&signal);
    char *result = NULL;
    CHECK(!qa_llm_request_codex(&input, &credential, mock_ref(&mock), &result, &error));
    CHECK(strstr(error.message, "declined") != NULL || strstr(error.message, "unsupported") != NULL);
    free(delta); free(bad); free(done); free(text);
  }
  free(d); free(c); free(payload);
  qa_llm_subscription_credential_free(&credential);
}

/* ---- responses / OpenAI API ---- */

static void test_responses(void) {
  qa_llm_error error;
  qa_llm_abort_signal signal;
  /* completed output may supply the text (output_text parts) */
  {
    char *delta = event_json("{\"type\":\"response.output_text.delta\",\"delta\":\"caf\xc3\xa9 \xf0\x9f\x8e\xae\"}");
    char *done = event_json("{\"type\":\"response.completed\",\"response\":{\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"role\":\"assistant\",\"content\":[{\"type\":\"output_text\",\"text\":\"caf\xc3\xa9 \xf0\x9f\x8e\xae\",\"annotations\":[]}]}]}}");
    char *text = join2(delta, done);
    mock_fetch mock = {0};
    mock.handler = fixed_response_handler;
    mock.handler_ctx = text;
    qa_llm_signal_init(&signal);
    qa_llm_transport_request input;
    memset(&input, 0, sizeof input);
    input.input.prompt = "hello";
    input.input.instructions = "Reply plainly.";
    input.input.signal = &signal;
    input.model = "gpt-5.5-pro";
    input.reasoning_effort = "high";
    char *result = NULL;
    CHECK(qa_llm_request_open_ai_responses(&input, "fixture", mock_ref(&mock), &result, &error));
    CHECK_STR(result, "caf\xc3\xa9 \xf0\x9f\x8e\xae");
    CHECK_STR(mock.last.url, "https://api.openai.com/v1/responses");
    CHECK_STR(captured_header(&mock.last, "authorization"), "Bearer fixture");
    CHECK(captured_header(&mock.last, "chatgpt-account-id") == NULL);
    CHECK_STR(mock.last.body, "{\"model\":\"gpt-5.5-pro\",\"instructions\":\"Reply plainly.\",\"input\":[{\"role\":\"user\",\"content\":[{\"type\":\"input_text\",\"text\":\"hello\"}]}],\"store\":false,\"stream\":true,\"reasoning\":{\"effort\":\"high\"}}");
    free(result);
    free(delta); free(done); free(text);
  }
  /* inconsistent final text, refusal/tool/incomplete without provider text */
  {
    char *delta = event_json("{\"type\":\"response.output_text.delta\",\"delta\":\"quit\"}");
    char *done = event_json("{\"type\":\"response.completed\",\"response\":{\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"echo complete\"}]}]}}");
    char *text = join2(delta, done);
    mock_fetch mock = {0};
    mock.handler = fixed_response_handler;
    mock.handler_ctx = text;
    qa_llm_signal_init(&signal);
    qa_llm_transport_request input;
    memset(&input, 0, sizeof input);
    input.input.prompt = "hello";
    input.input.instructions = "x";
    input.input.signal = &signal;
    input.model = "gpt-5.5-pro";
    char *result = NULL;
    CHECK(!qa_llm_request_open_ai_responses(&input, "fixture", mock_ref(&mock), &result, &error));
    CHECK_CONTAINS(error.message, "inconsistent");
    free(delta); free(done); free(text);
  }
  static const char *const outputs[] = {
    "{\"type\":\"response.output_item.added\",\"item\":{\"type\":\"function_call\",\"name\":\"quit\"}}",
    "{\"type\":\"response.completed\",\"response\":{\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"refusal\",\"refusal\":\"provider-secret\"}]}]}}",
    "{\"type\":\"response.incomplete\",\"response\":{\"status\":\"incomplete\",\"error\":{\"message\":\"provider-secret\"}}}",
  };
  for (size_t i = 0; i < sizeof outputs / sizeof outputs[0]; i++) {
    char *text = event_json(outputs[i]);
    mock_fetch mock = {0};
    mock.handler = fixed_response_handler;
    mock.handler_ctx = text;
    qa_llm_signal_init(&signal);
    qa_llm_transport_request input;
    memset(&input, 0, sizeof input);
    input.input.prompt = "hello";
    input.input.instructions = "x";
    input.input.signal = &signal;
    input.model = "gpt-5.5-pro";
    char *result = NULL;
    CHECK(!qa_llm_request_open_ai_responses(&input, "fixture", mock_ref(&mock), &result, &error));
    CHECK_NOT_CONTAINS(error.message, "provider-secret");
    free(text);
  }

  /* subscription-empty-completion fixture shape via readResponses */
  {
    const char *answer = "The echo command prints specified text to the console.";
    char *e1 = event_json("{\"type\":\"response.created\",\"response\":{\"status\":\"in_progress\",\"output\":[]}}");
    char *e2 = event_json("{\"type\":\"response.output_item.added\",\"output_index\":0,\"item\":{\"type\":\"message\",\"id\":\"msg_1\",\"status\":\"in_progress\",\"role\":\"assistant\",\"content\":[]}}");
    char *e3 = event_json("{\"type\":\"response.output_text.delta\",\"output_index\":0,\"content_index\":0,\"item_id\":\"msg_1\",\"delta\":\"The echo\"}");
    char *e4 = event_json("{\"type\":\"response.output_text.delta\",\"output_index\":0,\"content_index\":0,\"item_id\":\"msg_1\",\"delta\":\" command prints specified text to the console.\"}");
    char *e5 = event_json("{\"type\":\"response.completed\",\"response\":{\"status\":\"completed\",\"output\":[]}}");
    char *prefix = join2(join2(e1, e2), join2(e3, e4));
    char *full = join2(prefix, e5);
    qa_llm_signal_init(&signal);
    qa_llm_transport_request input;
    memset(&input, 0, sizeof input);
    input.input.prompt = "hello";
    input.input.instructions = "x";
    input.input.signal = &signal;
    input.model = "m";
    qa_llm_http_response response;
    sse_response(&response, full);
    char *result = NULL;
    CHECK(qa_llm_read_responses(&input, &response, "Subscription", false, &result, &error));
    CHECK_STR(result, answer);
    free(result);
    qa_llm_response_dispose(&response);
    /* prefix without completion */
    sse_response(&response, prefix);
    CHECK(!qa_llm_read_responses(&input, &response, "Subscription", false, &result, &error));
    CHECK_CONTAINS(error.message, "before completion");
    qa_llm_response_dispose(&response);
    /* aborted signal */
    qa_llm_abort_signal aborted;
    qa_llm_signal_init(&aborted);
    aborted.aborted = true;
    input.input.signal = &aborted;
    sse_response(&response, full);
    CHECK(!qa_llm_read_responses(&input, &response, "Subscription", false, &result, &error));
    CHECK_CONTAINS(error.message, "cancelled");
    qa_llm_response_dispose(&response);
    free(e1); free(e2); free(e3); free(e4); free(e5); free(prefix); free(full);
  }
}

/* ---- Chat Completions ---- */

static bool typed_text_handler(void *ctx, const char *url, const qa_llm_http_init *init,
                               qa_llm_http_response *out, qa_llm_error *error) {
  return typed_handler(ctx, url, init, out, error);
}

typedef struct abort_on_text {
  qa_llm_abort_signal *signal;
  qa_llm_strbuf deltas;
} abort_on_text;

static void on_text_abort(void *ctx, const char *text) {
  abort_on_text *state = ctx;
  on_text_collect(&state->deltas, text);
  state->signal->aborted = true;
}

static void test_chat_completions(void) {
  qa_llm_error error;
  qa_llm_abort_signal signal;
  const char *completion =
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"caf\xc3\xa9 \xf0\x9f\x8e\xae\"},\"finish_reason\":null}]}\r\n\r\n"
    "data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\r\n\r\n"
    "data: [DONE]\r\n\r\n";
  {
    typed_response typed = { "text/event-stream; charset=utf-8", completion, 200 };
    mock_fetch mock = {0};
    mock.handler = typed_text_handler;
    mock.handler_ctx = &typed;
    qa_llm_signal_init(&signal);
    qa_llm_transport_request input;
    memset(&input, 0, sizeof input);
    input.input.prompt = "hello";
    input.input.instructions = "answer";
    input.input.signal = &signal;
    input.model = "test-model";
    qa_llm_api_credential credential = { "test-key", "https://example.test/v1" };
    char *result = NULL;
    CHECK(qa_llm_request_chat_completions(&input, &credential, mock_ref(&mock), &result, &error));
    CHECK_STR(result, "caf\xc3\xa9 \xf0\x9f\x8e\xae");
    CHECK_STR(mock.last.url, "https://example.test/v1/chat/completions");
    CHECK_CONTAINS(mock.last.body, "\"role\":\"system\"");
    free(result);
  }
  static const char *const bad_streams[] = {
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"quit\"},\"finish_reason\":null}]}\n\n",
    "data: invalid-json\n\n",
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"quit\"},\"finish_reason\":\"length\"}]}\n\ndata: [DONE]\n\n",
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[]},\"finish_reason\":\"tool_calls\"}]}\n\n",
  };
  for (size_t i = 0; i < sizeof bad_streams / sizeof bad_streams[0]; i++) {
    mock_fetch mock = {0};
    mock.handler = fixed_response_handler;
    mock.handler_ctx = (void *)bad_streams[i];
    qa_llm_signal_init(&signal);
    qa_llm_transport_request input;
    memset(&input, 0, sizeof input);
    input.input.prompt = "hello";
    input.input.instructions = "answer";
    input.input.signal = &signal;
    input.model = "test-model";
    qa_llm_api_credential credential = { "test-key", "https://example.test/v1" };
    char *result = NULL;
    CHECK(!qa_llm_request_chat_completions(&input, &credential, mock_ref(&mock), &result, &error));
    CHECK(error.message[0] != '\0');
  }
  /* media type requirement */
  {
    typed_response typed = { "application/json; secret=do-not-reflect", completion, 200 };
    mock_fetch mock = {0};
    mock.handler = typed_text_handler;
    mock.handler_ctx = &typed;
    qa_llm_signal_init(&signal);
    qa_llm_transport_request input;
    memset(&input, 0, sizeof input);
    input.input.prompt = "hello";
    input.input.instructions = "answer";
    input.input.signal = &signal;
    input.model = "test-model";
    qa_llm_api_credential credential = { "test-key", "https://example.test/v1" };
    char *result = NULL;
    CHECK(!qa_llm_request_chat_completions(&input, &credential, mock_ref(&mock), &result, &error));
    CHECK_STR(error.message, "LLM service did not return an event stream (HTTP 200, JSON).");
  }
  /* cancellation inside a delta blocks further text */
  {
    const char *body =
      "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"first\"},\"finish_reason\":null}]}\n\n"
      "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"late\"},\"finish_reason\":\"stop\"}]}\n\n"
      "data: [DONE]\n\n";
    mock_fetch mock = {0};
    mock.handler = fixed_response_handler;
    mock.handler_ctx = (void *)body;
    qa_llm_signal_init(&signal);
    abort_on_text state;
    state.signal = &signal;
    qa_llm_strbuf_init(&state.deltas);
    qa_llm_transport_request input;
    memset(&input, 0, sizeof input);
    input.input.prompt = "hello";
    input.input.instructions = "answer";
    input.input.signal = &signal;
    input.input.on_text = on_text_abort;
    input.input.on_text_ctx = &state;
    input.model = "test-model";
    qa_llm_api_credential credential = { "test-key", "https://example.test/v1" };
    char *result = NULL;
    CHECK(!qa_llm_request_chat_completions(&input, &credential, mock_ref(&mock), &result, &error));
    CHECK_CONTAINS(error.message, "cancelled");
    CHECK_STR(state.deltas.data, "first");
    qa_llm_strbuf_free(&state.deltas);
  }
}

/* ---- models (embedded copies of the TS fixtures) ---- */

static const char *API_MODELS_JSON =
  "{\"object\":\"list\",\"data\":[{\"id\":\"gpt-5\",\"object\":\"model\",\"created\":0,\"owned_by\":\"test\"},"
  "{\"id\":\"gpt-4.1\",\"object\":\"model\",\"created\":0,\"owned_by\":\"test\"},"
  "{\"id\":\"text-embedding-test\",\"object\":\"model\",\"created\":0,\"owned_by\":\"test\"},"
  "{\"id\":\"unknown-chat\",\"object\":\"model\",\"created\":0,\"owned_by\":\"test\"}]}";

static const char *CODEX_MODELS_JSON =
  "{\"models\":[{\"slug\":\"test-hidden\",\"display_name\":\"Hidden\",\"visibility\":\"hide\",\"priority\":0,\"supported_reasoning_levels\":[]},"
  "{\"slug\":\"test-reasoner\",\"display_name\":\"Test Reasoner\",\"visibility\":\"list\",\"priority\":2,\"default_reasoning_level\":\"medium\","
  "\"supported_reasoning_levels\":[{\"effort\":\"low\",\"description\":\"Fast\"},{\"effort\":\"medium\",\"description\":\"Balanced\"},{\"effort\":\"xhigh\",\"description\":\"Thorough\"}]},"
  "{\"slug\":\"test-default\",\"display_name\":\"Test Default\",\"visibility\":\"list\",\"priority\":1,\"default_reasoning_level\":\"low\","
  "\"supported_reasoning_levels\":[{\"effort\":\"low\",\"description\":\"Fast\"},{\"effort\":\"high\",\"description\":\"Thorough\"}]},"
  "{\"slug\":\"test-plain\",\"display_name\":\"Test Plain\",\"visibility\":\"list\",\"priority\":3,\"default_reasoning_level\":null,\"supported_reasoning_levels\":[]}]}";

static void test_models(void) {
  qa_llm_error error;
  qa_llm_json *value = NULL;
  CHECK(qa_llm_json_parse(CODEX_MODELS_JSON, strlen(CODEX_MODELS_JSON), &value));
  qa_llm_model_list entries;
  CHECK(qa_llm_parse_codex_models(value, &entries, &error));
  CHECK(entries.count == 3);
  if (entries.count == 3) {
    CHECK_STR(entries.items[0].id, "test-default");
    CHECK_STR(entries.items[1].id, "test-reasoner");
    CHECK_STR(entries.items[2].id, "test-plain");
    CHECK(entries.items[0].recommended);
    CHECK_STR(entries.items[0].default_reasoning_effort, "low");
    CHECK(entries.items[0].reasoning_source == QA_LLM_REASONING_PROVIDER);
    CHECK(entries.items[1].reasoning_effort_count == 3);
    if (entries.items[1].reasoning_effort_count == 3) {
      CHECK_STR(entries.items[1].reasoning_efforts[0], "low");
      CHECK_STR(entries.items[1].reasoning_efforts[1], "medium");
      CHECK_STR(entries.items[1].reasoning_efforts[2], "xhigh");
    }
    CHECK_STR(qa_llm_fastest_reasoning_effort(&entries.items[1]), "low");
    CHECK(qa_llm_fastest_reasoning_effort(&entries.items[2]) == NULL);
  }
  qa_llm_model_list_free(&entries);
  qa_llm_json_free(value);

  CHECK(qa_llm_json_parse(API_MODELS_JSON, strlen(API_MODELS_JSON), &value));
  qa_llm_model_list api;
  CHECK(qa_llm_parse_api_models(value, true, &api, &error));
  bool has_embedding = false;
  const qa_llm_model *gpt5 = NULL;
  const qa_llm_model *unknown = NULL;
  for (size_t i = 0; i < api.count; i++) {
    if (strcmp(api.items[i].id, "text-embedding-test") == 0) has_embedding = true;
    if (strcmp(api.items[i].id, "gpt-5") == 0) gpt5 = &api.items[i];
    if (strcmp(api.items[i].id, "unknown-chat") == 0) unknown = &api.items[i];
  }
  CHECK(!has_embedding);
  CHECK(gpt5 != NULL);
  if (gpt5 != NULL) {
    CHECK(gpt5->reasoning_source == QA_LLM_REASONING_REFERENCE);
    CHECK(gpt5->reasoning_effort_count == 4);
    CHECK(!gpt5->recommended);
    CHECK_STR(qa_llm_fastest_reasoning_effort(gpt5), "minimal");
  }
  CHECK(unknown != NULL && unknown->reasoning_effort_count == 0);
  qa_llm_model_list other;
  CHECK(qa_llm_parse_api_models(value, false, &other, &error));
  bool all_unknown = true;
  for (size_t i = 0; i < other.count; i++) {
    if (other.items[i].reasoning_source != QA_LLM_REASONING_UNKNOWN || other.items[i].reasoning_effort_count != 0) all_unknown = false;
  }
  CHECK(all_unknown);
  CHECK(other.count == 4);
  qa_llm_model_list_free(&api);
  qa_llm_model_list_free(&other);
  qa_llm_json_free(value);

  qa_llm_model model;
  qa_llm_api_model("gpt-6-astra", &model);
  CHECK(model.reasoning_effort_count == 5);
  qa_llm_model_clear(&model);
  qa_llm_api_model("gpt-5.5-pro", &model);
  CHECK(model.reasoning_effort_count == 3 && qa_llm_model_has_effort(&model, "medium"));
  qa_llm_model_clear(&model);
  qa_llm_api_model("gpt-5-2025-08-07", &model);
  CHECK(qa_llm_model_has_effort(&model, "minimal"));
  qa_llm_model_clear(&model);
  qa_llm_api_model("gpt-5-future", &model);
  CHECK(model.reasoning_effort_count == 0);
  CHECK(model.reasoning_source == QA_LLM_REASONING_UNKNOWN);
  qa_llm_model_clear(&model);

  char *effort = NULL;
  CHECK(qa_llm_parse_reasoning_effort_string(NULL, &effort, &error) && effort == NULL);
  CHECK(qa_llm_parse_reasoning_effort_string("xhigh", &effort, &error));
  CHECK_STR(effort, "xhigh");
  free(effort);
  effort = NULL;
  CHECK(!qa_llm_parse_reasoning_effort_string("High", &effort, &error));
  CHECK_STR(error.message, "Invalid reasoning effort.");
  CHECK(!qa_llm_parse_reasoning_effort_string("", &effort, &error));
  CHECK_STR(error.message, "The service returned invalid model metadata.");
}

/* ---- session ---- */

#define MAX_RECORDED 16

typedef struct recorded_call {
  char *prompt;
  char *instructions;
  char *session_id;
} recorded_call;

typedef struct session_fixture {
  recorded_call calls[MAX_RECORDED];
  size_t count;
  const char *config;
} session_fixture;

static bool session_fixture_transport(void *ctx, const qa_llm_request_input *input, char **out_text, qa_llm_error *error) {
  session_fixture *fixture = ctx;
  if (fixture->count < MAX_RECORDED) {
    fixture->calls[fixture->count].prompt = qa_llm_strdup(input->prompt);
    fixture->calls[fixture->count].instructions = qa_llm_strdup(input->instructions);
    fixture->calls[fixture->count].session_id = input->session_id != NULL ? qa_llm_strdup(input->session_id) : NULL;
    fixture->count++;
  }
  if (strstr(input->prompt, "READY only") != NULL) { *out_text = qa_llm_strdup("READY"); return true; }
  if (qa_llm_ends_with(input->prompt, "bad")) { /* TS input.prompt.endsWith("bad") */
    qa_llm_error_set(error, "transport failed");
    return false;
  }
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  qa_llm_strbuf_appendf(&out, "answer:%s", input->prompt);
  *out_text = qa_llm_strbuf_steal(&out);
  return true;
}

static const char *session_fixture_config(void *ctx) { return ((session_fixture *)ctx)->config; }

static void test_session(void) {
  qa_llm_error error;
  {
    session_fixture fixture = {0};
    fixture.config = "provider:model-a";
    qa_llm_session *session = qa_llm_session_create(session_fixture_transport, &fixture, session_fixture_config, &fixture);
    char *id = qa_llm_strdup(qa_llm_session_id(session));
    CHECK(qa_llm_session_prime(session, "CATALOG ONE", &error));
    CHECK(qa_llm_session_prime(session, "CATALOG ONE", &error));
    CHECK(fixture.count == 1);
    CHECK_STR(fixture.calls[0].instructions, "CATALOG ONE");
    CHECK_STR(fixture.calls[0].session_id, id);
    CHECK(qa_llm_session_primed(session));
    CHECK(qa_llm_session_active(session));

    qa_llm_request_input input;
    memset(&input, 0, sizeof input);
    input.prompt = "first";
    input.instructions = "turn one";
    input.session_instructions = "CATALOG ONE";
    char *answer = NULL;
    CHECK(qa_llm_session_request(session, &input, &answer, &error));
    CHECK_STR(answer, "answer:first");
    free(answer);
    CHECK(fixture.count == 2);
    CHECK_CONTAINS(fixture.calls[1].instructions, "CATALOG ONE");
    CHECK_CONTAINS(fixture.calls[1].instructions, "turn one");
    CHECK(qa_llm_session_turn_count(session) == 1);

    input.prompt = "second";
    input.instructions = "turn two";
    CHECK(qa_llm_session_request(session, &input, &answer, &error));
    free(answer);
    CHECK_CONTAINS(fixture.calls[2].prompt, "User: first");
    CHECK_CONTAINS(fixture.calls[2].prompt, "Assistant: answer:first");
    CHECK_STR(qa_llm_session_id(session), id);

    CHECK(qa_llm_session_prime(session, "CATALOG TWO", &error));
    CHECK_STR(qa_llm_session_id(session), id);
    CHECK(fixture.count == 4);
    CHECK_CONTAINS(fixture.calls[3].prompt, "has been updated");
    input.prompt = "third";
    input.instructions = "turn three";
    input.session_instructions = "CATALOG TWO";
    CHECK(qa_llm_session_request(session, &input, &answer, &error));
    free(answer);
    CHECK_CONTAINS(fixture.calls[4].instructions, "CATALOG TWO");
    CHECK_CONTAINS(fixture.calls[4].prompt, "User: second");
    bool all_id = true;
    for (size_t i = 0; i < fixture.count; i++) {
      if (fixture.calls[i].session_id == NULL || strcmp(fixture.calls[i].session_id, id) != 0) all_id = false;
    }
    CHECK(all_id);
    free(id);
    qa_llm_session_destroy(session);
    for (size_t i = 0; i < fixture.count; i++) {
      free(fixture.calls[i].prompt); free(fixture.calls[i].instructions); free(fixture.calls[i].session_id);
    }
  }
  {
    session_fixture fixture = {0};
    fixture.config = "provider:model-a";
    qa_llm_session *session = qa_llm_session_create(session_fixture_transport, &fixture, session_fixture_config, &fixture);
    qa_llm_request_input input;
    memset(&input, 0, sizeof input);
    input.prompt = "first";
    input.instructions = "turn";
    input.session_instructions = "CATALOG";
    char *answer = NULL;
    CHECK(qa_llm_session_request(session, &input, &answer, &error));
    free(answer);
    char *first_id = qa_llm_strdup(qa_llm_session_id(session));
    CHECK(fixture.count == 2);
    fixture.config = "provider:model-b";
    input.prompt = "second";
    CHECK(qa_llm_session_request(session, &input, &answer, &error));
    free(answer);
    CHECK(strcmp(qa_llm_session_id(session), first_id) != 0);
    CHECK(fixture.count == 4);
    CHECK_CONTAINS(fixture.calls[2].prompt, "starts an ongoing session");
    CHECK_NOT_CONTAINS(fixture.calls[3].prompt, "User: first");
    CHECK_STR(fixture.calls[3].session_id, qa_llm_session_id(session));
    free(first_id);
    qa_llm_session_destroy(session);
    for (size_t i = 0; i < fixture.count; i++) {
      free(fixture.calls[i].prompt); free(fixture.calls[i].instructions); free(fixture.calls[i].session_id);
    }
  }
  {
    session_fixture fixture = {0};
    fixture.config = "provider:model-a";
    qa_llm_session *session = qa_llm_session_create(session_fixture_transport, &fixture, session_fixture_config, &fixture);
    qa_llm_request_input input;
    memset(&input, 0, sizeof input);
    input.prompt = "kept";
    input.instructions = "turn";
    input.session_instructions = "CATALOG";
    char *answer = NULL;
    CHECK(qa_llm_session_request(session, &input, &answer, &error));
    free(answer);
    input.prompt = "bad";
    CHECK(!qa_llm_session_request(session, &input, &answer, &error));
    CHECK_STR(error.message, "transport failed");
    input.prompt = "next";
    CHECK(qa_llm_session_request(session, &input, &answer, &error));
    free(answer);
    CHECK_CONTAINS(fixture.calls[fixture.count - 1].prompt, "User: kept");
    CHECK_NOT_CONTAINS(fixture.calls[fixture.count - 1].prompt, "User: bad");
    char *old_id = qa_llm_strdup(qa_llm_session_id(session));
    qa_llm_session_invalidate(session);
    CHECK(strcmp(qa_llm_session_id(session), old_id) != 0);
    CHECK(!qa_llm_session_active(session));
    free(old_id);
    input.prompt = "fresh";
    CHECK(qa_llm_session_request(session, &input, &answer, &error));
    free(answer);
    CHECK_NOT_CONTAINS(fixture.calls[fixture.count - 1].prompt, "User: kept");
    qa_llm_session_destroy(session);
    for (size_t i = 0; i < fixture.count; i++) {
      free(fixture.calls[i].prompt); free(fixture.calls[i].instructions); free(fixture.calls[i].session_id);
    }
  }
  {
    session_fixture fixture = {0};
    fixture.config = "";
    qa_llm_session *session = qa_llm_session_create(session_fixture_transport, &fixture, session_fixture_config, &fixture);
    qa_llm_session_close(session);
    CHECK(!qa_llm_session_prime(session, "CATALOG", &error));
    CHECK_STR(error.message, "LLM session is closed.");
    qa_llm_request_input input;
    memset(&input, 0, sizeof input);
    input.prompt = "hello";
    input.instructions = "turn";
    char *answer = NULL;
    CHECK(!qa_llm_session_request(session, &input, &answer, &error));
    CHECK_STR(error.message, "LLM session is closed.");
    qa_llm_session_destroy(session);
  }
}

/* ---- settings ---- */

static char *make_temp_dir(void) {
  char *tmpl = qa_llm_strdup("/tmp/quake-llm-test-XXXXXX");
  if (mkdtemp(tmpl) == NULL) { perror("mkdtemp"); exit(2); }
  return tmpl;
}

static char *path_join(const char *dir, const char *name) {
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  qa_llm_strbuf_appendf(&out, "%s/%s", dir, name);
  return qa_llm_strbuf_steal(&out);
}

static void write_at(const char *dir, const char *name, const char *text) {
  char *path = path_join(dir, name);
  FILE *file = fopen(path, "wb");
  if (file == NULL) { perror("fopen"); exit(2); }
  fwrite(text, 1, strlen(text), file);
  fclose(file);
  free(path);
}

static char *read_at(const char *dir, const char *name) {
  char *path = path_join(dir, name);
  FILE *file = fopen(path, "rb");
  free(path);
  if (file == NULL) return NULL;
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  char chunk[4096];
  size_t got;
  while ((got = fread(chunk, 1, sizeof chunk, file)) > 0) qa_llm_strbuf_append_n(&out, chunk, got);
  fclose(file);
  return qa_llm_strbuf_steal(&out);
}

static bool file_exists(const char *dir, const char *name) {
  char *path = path_join(dir, name);
  bool exists = access(path, F_OK) == 0;
  free(path);
  return exists;
}

static int file_mode(const char *dir, const char *name) {
  char *path = path_join(dir, name);
  struct stat info;
  int mode = stat(path, &info) == 0 ? (int)(info.st_mode & 0777) : -1;
  free(path);
  return mode;
}

static void cleanup_dir(const char *dir) {
  static const char *const names[] = { "llm.json", "chatgpt.key", "other.key", "other.service" };
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
    char *path = path_join(dir, names[i]);
    unlink(path);
    free(path);
  }
  rmdir(dir);
}

static qa_llm_settings_options settings_options(const char *dir) {
  qa_llm_settings_options options;
  memset(&options, 0, sizeof options);
  options.base_directory = dir;
  return options;
}

static void seed_subscription(const char *dir, const char *label, const char *refresh,
                              double expires_at, const char *api_key) {
  char *token = make_token(label, "test-account");
  qa_llm_strbuf text;
  qa_llm_strbuf_init(&text);
  qa_llm_strbuf_appendf(&text,
    "{\"version\":1,\"apiKey\":\"%s\",\"subscription\":{\"accessToken\":\"%s\",\"refreshToken\":\"%s\",\"tokenType\":\"Bearer\",\"expiresAt\":%.0f,\"scopes\":[\"openid\"]}}",
    api_key, token, refresh, expires_at);
  write_at(dir, "chatgpt.key", text.data);
  qa_llm_strbuf_free(&text);
  free(token);
}

typedef struct models_answer {
  const char *models_json;
  const char *answer_sse;
  char *bodies[8];
  char *urls[8];
  int body_count;
  int gets;
  int posts;
} models_answer;

static bool models_answer_handler(void *ctx, const char *url, const qa_llm_http_init *init,
                                  qa_llm_http_response *out, qa_llm_error *error) {
  (void)error;
  models_answer *state = ctx;
  const char *method = init->method != NULL ? init->method : "";
  if (state->body_count < 8) {
    state->urls[state->body_count] = qa_llm_strdup(url);
  }
  if (strcmp(method, "GET") == 0) {
    state->gets++;
    if (state->body_count < 8) state->bodies[state->body_count++] = qa_llm_strdup("");
    qa_llm_response_init_memory(out, 200, "application/json", state->models_json, strlen(state->models_json), 0);
    return true;
  }
  state->posts++;
  if (state->body_count < 8) {
    state->bodies[state->body_count++] = init->body != NULL ? qa_llm_strndup(init->body, init->body_length) : qa_llm_strdup("");
  }
  sse_response(out, state->answer_sse);
  return true;
}

static const char *RESPONSES_OK =
  "data: {\"type\":\"response.output_text.delta\",\"delta\":\"ok\"}\n\n"
  "data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}\n\n";

static void test_settings_storage(void) {
  qa_llm_error error;
  char *dir = make_temp_dir();
  qa_llm_settings_options options = settings_options(dir);
  qa_llm_settings_service *service = qa_llm_settings_open(&options);
  CHECK(service != NULL);
  CHECK(qa_llm_settings_save_api_key(service, QA_LLM_PROVIDER_CHATGPT_API, " api-test ", &error));
  CHECK(qa_llm_settings_save_api_key(service, QA_LLM_PROVIDER_OTHER_API, "other-test", &error));
  CHECK(qa_llm_settings_save_other_service(service, "http://127.0.0.1:11434/v1/", "local-model", "openai-chat-completions", &error));
  CHECK(qa_llm_settings_set_model(service, QA_LLM_PROVIDER_CHATGPT_API, "my-model", &error));
  CHECK(qa_llm_settings_select_provider(service, QA_LLM_PROVIDER_OTHER_API, &error));

  qa_llm_settings_service *reopened = qa_llm_settings_open(&options);
  qa_llm_settings_snapshot snapshot;
  qa_llm_settings_read(reopened, &snapshot);
  CHECK_STR(snapshot.model, "local-model");
  CHECK(snapshot.providers.chatgpt_api.configured);
  CHECK_STR(snapshot.providers.other_api.base_url, "http://127.0.0.1:11434/v1");
  qa_llm_settings_snapshot_free(&snapshot);
  static const char *const files[] = { "chatgpt.key", "other.key", "other.service", "llm.json" };
  for (size_t i = 0; i < 4; i++) CHECK(file_mode(dir, files[i]) == 0600);
  char *key_text = read_at(dir, "chatgpt.key");
  CHECK_CONTAINS(key_text, "api-test");
  free(key_text);
  CHECK(qa_llm_settings_remove_credential(service, QA_LLM_PROVIDER_CHATGPT_API, &error));
  qa_llm_settings_read(service, &snapshot);
  CHECK(snapshot.providers.other_api.configured);
  CHECK(!snapshot.providers.chatgpt_api.configured);
  qa_llm_settings_snapshot_free(&snapshot);
  CHECK(!qa_llm_settings_save_other_service(service, "https://key:secret@example.test/v1", "", "openai-chat-completions", &error));
  qa_llm_settings_close(service);
  qa_llm_settings_destroy(service);
  qa_llm_settings_close(reopened);
  qa_llm_settings_destroy(reopened);

  /* malformed files preserved at startup and on unrelated changes */
  service = qa_llm_settings_open(&options);
  write_at(dir, "chatgpt.key", "{\"version\":1,\"secret\":");
  reopened = qa_llm_settings_open(&options);
  qa_llm_settings_read(reopened, &snapshot);
  CHECK(snapshot.error_count == 1);
  if (snapshot.error_count == 1) CHECK_STR(snapshot.errors[0].file, "chatgpt.key");
  qa_llm_settings_snapshot_free(&snapshot);
  CHECK(qa_llm_settings_select_provider(reopened, QA_LLM_PROVIDER_OTHER_API, &error));
  char *preserved = read_at(dir, "chatgpt.key");
  CHECK_STR(preserved, "{\"version\":1,\"secret\":");
  free(preserved);
  CHECK(!qa_llm_settings_save_api_key(service, QA_LLM_PROVIDER_CHATGPT_API, "replacement", &error));
  /* PLATFORM-017: removal and preference changes are the repair path for a malformed file. */
  CHECK(qa_llm_settings_remove_credential(reopened, QA_LLM_PROVIDER_CHATGPT_API, &error));
  char *repaired = read_at(dir, "chatgpt.key");
  CHECK_NOT_CONTAINS(repaired, "\"secret\":");
  free(repaired);
  write_at(dir, "llm.json", "{broken");
  CHECK(qa_llm_settings_select_provider(reopened, QA_LLM_PROVIDER_OTHER_API, &error));
  char *preferences = read_at(dir, "llm.json");
  CHECK_CONTAINS(preferences, "other-api");
  free(preferences);
  qa_llm_settings_close(service);
  qa_llm_settings_destroy(service);
  qa_llm_settings_close(reopened);
  qa_llm_settings_destroy(reopened);

  /* plain pasted key import, then replacement rereads the file */
  write_at(dir, "chatgpt.key", "pasted-key\n");
  service = qa_llm_settings_open(&options);
  CHECK(qa_llm_settings_save_api_key(service, QA_LLM_PROVIDER_CHATGPT_API, "replacement-key", &error));
  char *replaced = read_at(dir, "chatgpt.key");
  CHECK_CONTAINS(replaced, "replacement-key");
  free(replaced);
  qa_llm_settings_close(service);
  qa_llm_settings_destroy(service);
  cleanup_dir(dir);
  free(dir);
}

static void test_settings_subscription_models(void) {
  qa_llm_error error;
  char *dir = make_temp_dir();
  models_answer state = {0};
  state.models_json = CODEX_MODELS_JSON;
  state.answer_sse = RESPONSES_OK;
  mock_fetch mock = {0};
  mock.handler = models_answer_handler;
  mock.handler_ctx = &state;
  qa_llm_settings_options options = settings_options(dir);
  options.request.fetch = mock_ref(&mock);
  qa_llm_settings_service *service = qa_llm_settings_open(&options);
  seed_subscription(dir, "header", "fake-refresh", (double)qa_llm_now_ms() + 3600000, "preserved-key");

  qa_llm_model_list models;
  CHECK(qa_llm_settings_refresh_models(service, QA_LLM_PROVIDER_SUBSCRIPTION, NULL, &models, &error));
  qa_llm_model_list_free(&models);
  CHECK(state.gets == 1);
  CHECK_STR(state.urls[0], "https://chatgpt.com/backend-api/codex/models?client_version=0.162.1");
  CHECK_STR(captured_header(&mock.last, "chatgpt-account-id"), "test-account");
  qa_llm_settings_snapshot snapshot;
  qa_llm_settings_read(service, &snapshot);
  CHECK_STR(snapshot.model, "test-default");
  CHECK(snapshot.reasoning_effort == NULL);
  CHECK(snapshot.catalogs[QA_LLM_PROVIDER_SUBSCRIPTION].status == QA_LLM_CATALOG_READY);
  qa_llm_settings_snapshot_free(&snapshot);

  CHECK(qa_llm_settings_set_model(service, QA_LLM_PROVIDER_SUBSCRIPTION, "test-reasoner", &error));
  CHECK(qa_llm_settings_set_reasoning_effort(service, QA_LLM_PROVIDER_SUBSCRIPTION, "xhigh", &error));
  qa_llm_request_input input;
  memset(&input, 0, sizeof input);
  input.prompt = "hello";
  input.instructions = "commands";
  char *answer = NULL;
  CHECK(qa_llm_settings_request(service, &input, &answer, &error));
  CHECK_STR(answer, "ok");
  free(answer);
  CHECK_CONTAINS(state.bodies[1], "\"reasoning\":{\"effort\":\"xhigh\"}");

  qa_llm_settings_service *reopened = qa_llm_settings_open(&options);
  qa_llm_settings_read(reopened, &snapshot);
  CHECK_STR(snapshot.model, "test-reasoner");
  CHECK_STR(snapshot.reasoning_effort, "xhigh");
  qa_llm_settings_snapshot_free(&snapshot);
  qa_llm_settings_close(reopened);
  qa_llm_settings_destroy(reopened);

  CHECK(qa_llm_settings_set_model(service, QA_LLM_PROVIDER_SUBSCRIPTION, "test-plain", &error));
  qa_llm_settings_read(service, &snapshot);
  CHECK(snapshot.reasoning_effort == NULL);
  qa_llm_settings_snapshot_free(&snapshot);
  CHECK(qa_llm_settings_request(service, &input, &answer, &error));
  free(answer);
  CHECK_NOT_CONTAINS(state.bodies[2], "\"reasoning\"");
  CHECK(!qa_llm_settings_set_model(service, QA_LLM_PROVIDER_SUBSCRIPTION, "made-up-model", &error));
  CHECK_CONTAINS(error.message, "loaded provider list");
  qa_llm_settings_close(service);
  qa_llm_settings_destroy(service);
  for (int i = 0; i < state.body_count; i++) { free(state.bodies[i]); free(state.urls[i]); }
  cleanup_dir(dir);
  free(dir);
}

static void test_settings_api_models(void) {
  qa_llm_error error;
  char *dir = make_temp_dir();
  models_answer state = {0};
  state.models_json = API_MODELS_JSON;
  state.answer_sse = RESPONSES_OK;
  mock_fetch mock = {0};
  mock.handler = models_answer_handler;
  mock.handler_ctx = &state;
  qa_llm_settings_options options = settings_options(dir);
  options.request.fetch = mock_ref(&mock);
  qa_llm_settings_service *service = qa_llm_settings_open(&options);
  CHECK(qa_llm_settings_save_api_key(service, QA_LLM_PROVIDER_CHATGPT_API, "fake-key", &error));
  CHECK(qa_llm_settings_select_provider(service, QA_LLM_PROVIDER_CHATGPT_API, &error));
  qa_llm_model_list models;
  CHECK(qa_llm_settings_refresh_models(service, QA_LLM_PROVIDER_CHATGPT_API, NULL, &models, &error));
  qa_llm_model_list_free(&models);
  CHECK_STR(state.urls[0], "https://api.openai.com/v1/models");
  qa_llm_settings_snapshot snapshot;
  qa_llm_settings_read(service, &snapshot);
  CHECK_STR(snapshot.model, "");
  qa_llm_settings_snapshot_free(&snapshot);
  CHECK(qa_llm_settings_set_model(service, QA_LLM_PROVIDER_CHATGPT_API, "gpt-5", &error));
  CHECK(qa_llm_settings_set_reasoning_effort(service, QA_LLM_PROVIDER_CHATGPT_API, "high", &error));
  qa_llm_request_input input;
  memset(&input, 0, sizeof input);
  input.prompt = "hello";
  input.instructions = "commands";
  char *answer = NULL;
  CHECK(qa_llm_settings_request(service, &input, &answer, &error));
  CHECK_STR(answer, "ok");
  free(answer);
  CHECK_CONTAINS(state.bodies[1], "\"reasoning\":{\"effort\":\"high\"}");
  CHECK_NOT_CONTAINS(state.bodies[1], "reasoning_effort");
  CHECK(qa_llm_settings_set_model(service, QA_LLM_PROVIDER_CHATGPT_API, "gpt-4.1", &error));
  qa_llm_settings_read(service, &snapshot);
  CHECK(snapshot.reasoning_effort == NULL);
  qa_llm_settings_snapshot_free(&snapshot);
  CHECK(!qa_llm_settings_set_reasoning_effort(service, QA_LLM_PROVIDER_CHATGPT_API, "high", &error));
  CHECK_CONTAINS(error.message, "supported");
  qa_llm_settings_close(service);
  qa_llm_settings_destroy(service);
  for (int i = 0; i < state.body_count; i++) { free(state.bodies[i]); free(state.urls[i]); }
  cleanup_dir(dir);
  free(dir);
}

static void test_settings_other_catalog(void) {
  qa_llm_error error;
  char *dir = make_temp_dir();
  models_answer state = {0};
  state.models_json = API_MODELS_JSON;
  state.answer_sse = RESPONSES_OK;
  mock_fetch mock = {0};
  mock.handler = models_answer_handler;
  mock.handler_ctx = &state;
  qa_llm_settings_options options = settings_options(dir);
  options.request.fetch = mock_ref(&mock);
  qa_llm_settings_service *service = qa_llm_settings_open(&options);
  const char *base = "http://127.0.0.1:11434/v1";
  CHECK(qa_llm_settings_save_other_service(service, base, "", "openai-chat-completions", &error));
  CHECK(qa_llm_settings_save_api_key(service, QA_LLM_PROVIDER_OTHER_API, "fake-key", &error));
  qa_llm_model_list models;
  CHECK(qa_llm_settings_refresh_models(service, QA_LLM_PROVIDER_OTHER_API, NULL, &models, &error));
  CHECK(models.count == 4);
  CHECK_STR(state.urls[0], "http://127.0.0.1:11434/v1/models");
  CHECK(qa_llm_settings_save_other_service(service, base, "gpt-4.1", "openai-chat-completions", &error));
  qa_llm_settings_snapshot snapshot;
  qa_llm_settings_read(service, &snapshot);
  CHECK(snapshot.catalogs[QA_LLM_PROVIDER_OTHER_API].status == QA_LLM_CATALOG_READY);
  CHECK(snapshot.catalogs[QA_LLM_PROVIDER_OTHER_API].models.count == models.count);
  CHECK(snapshot.reasoning_efforts[QA_LLM_PROVIDER_OTHER_API] == NULL);
  qa_llm_settings_snapshot_free(&snapshot);
  qa_llm_model_list_free(&models);
  qa_llm_settings_service *reopened = qa_llm_settings_open(&options);
  qa_llm_settings_read(reopened, &snapshot);
  CHECK_STR(snapshot.providers.other_api.model, "gpt-4.1");
  qa_llm_settings_snapshot_free(&snapshot);
  qa_llm_settings_close(reopened);
  qa_llm_settings_destroy(reopened);
  CHECK(qa_llm_settings_save_other_service(service, "http://127.0.0.1:11435/v1", "", "openai-chat-completions", &error));
  qa_llm_settings_read(service, &snapshot);
  CHECK(snapshot.catalogs[QA_LLM_PROVIDER_OTHER_API].status == QA_LLM_CATALOG_IDLE);
  CHECK(snapshot.catalogs[QA_LLM_PROVIDER_OTHER_API].models.count == 0);
  qa_llm_settings_snapshot_free(&snapshot);
  qa_llm_settings_close(service);
  qa_llm_settings_destroy(service);
  for (int i = 0; i < state.body_count; i++) { free(state.bodies[i]); free(state.urls[i]); }
  cleanup_dir(dir);
  free(dir);
}

/* hanging body: every read stalls, then yields nothing */
typedef struct hanging_body {
  bool cancelled;
} hanging_body;

static qa_llm_read_result hanging_read(void *ctx, uint8_t *buffer, size_t capacity, size_t *out_length) {
  (void)ctx; (void)buffer; (void)capacity;
  sleep_ms(20);
  *out_length = 0;
  return QA_LLM_READ_OK;
}

static void hanging_cancel(void *ctx) { ((hanging_body *)ctx)->cancelled = true; }
static void hanging_free(void *ctx) { (void)ctx; }

static bool hanging_handler(void *ctx, const char *url, const qa_llm_http_init *init,
                            qa_llm_http_response *out, qa_llm_error *error) {
  (void)url; (void)init; (void)error;
  qa_llm_response_init(out);
  out->status = 200;
  out->content_type = qa_llm_strdup("text/event-stream");
  out->has_body = true;
  out->read = hanging_read;
  out->cancel = hanging_cancel;
  out->free_ctx = hanging_free;
  out->body_ctx = ctx;
  return true;
}

static void test_settings_request_guards(void) {
  qa_llm_error error;
  /* blank model and absent key fail before fetching; 401 not retried */
  {
    char *dir = make_temp_dir();
    mock_fetch mock = {0};
    mock.handler = fixed_response_handler;
    mock.handler_ctx = (void *)RESPONSES_OK;
    qa_llm_settings_options options = settings_options(dir);
    options.request.fetch = mock_ref(&mock);
    qa_llm_settings_service *service = qa_llm_settings_open(&options);
    CHECK(qa_llm_settings_select_provider(service, QA_LLM_PROVIDER_CHATGPT_API, &error));
    qa_llm_request_input input;
    memset(&input, 0, sizeof input);
    input.prompt = "hello";
    input.instructions = "answer";
    char *answer = NULL;
    CHECK(!qa_llm_settings_request(service, &input, &answer, &error));
    CHECK_CONTAINS(error.message, "Choose a Model");
    CHECK(qa_llm_settings_set_model(service, QA_LLM_PROVIDER_CHATGPT_API, "test-model", &error));
    CHECK(!qa_llm_settings_request(service, &input, &answer, &error));
    CHECK_CONTAINS(error.message, "Paste an API key");
    CHECK(mock.calls == 0);
    qa_llm_settings_close(service);
    qa_llm_settings_destroy(service);
    cleanup_dir(dir);
    free(dir);
  }
  {
    char *dir = make_temp_dir();
    mock_fetch mock = {0};
    mock.handler = status_handler;
    mock.handler_ctx = (void *)(intptr_t)401;
    qa_llm_settings_options options = settings_options(dir);
    options.request.fetch = mock_ref(&mock);
    qa_llm_settings_service *service = qa_llm_settings_open(&options);
    CHECK(qa_llm_settings_save_api_key(service, QA_LLM_PROVIDER_CHATGPT_API, "api-secret", &error));
    CHECK(qa_llm_settings_set_model(service, QA_LLM_PROVIDER_CHATGPT_API, "test-model", &error));
    CHECK(qa_llm_settings_select_provider(service, QA_LLM_PROVIDER_CHATGPT_API, &error));
    qa_llm_request_input input;
    memset(&input, 0, sizeof input);
    input.prompt = "hello";
    input.instructions = "answer";
    char *answer = NULL;
    CHECK(!qa_llm_settings_request(service, &input, &answer, &error));
    CHECK_CONTAINS(error.message, "HTTP 401");
    CHECK_NOT_CONTAINS(error.message, "api-secret");
    CHECK(mock.calls == 1);
    qa_llm_settings_close(service);
    qa_llm_settings_destroy(service);
    cleanup_dir(dir);
    free(dir);
  }
  /* unsupported persisted effort fails before a paid request */
  {
    char *dir = make_temp_dir();
    mock_fetch mock = {0};
    mock.handler = fixed_response_handler;
    mock.handler_ctx = (void *)RESPONSES_OK;
    qa_llm_settings_options options = settings_options(dir);
    options.request.fetch = mock_ref(&mock);
    qa_llm_settings_service *service = qa_llm_settings_open(&options);
    CHECK(qa_llm_settings_save_api_key(service, QA_LLM_PROVIDER_CHATGPT_API, "fixture-key", &error));
    CHECK(qa_llm_settings_set_model(service, QA_LLM_PROVIDER_CHATGPT_API, "gpt-5.5-pro", &error));
    CHECK(qa_llm_settings_select_provider(service, QA_LLM_PROVIDER_CHATGPT_API, &error));
    write_at(dir, "llm.json", "{\"version\":1,\"provider\":\"chatgpt-api\",\"models\":{\"chatgpt-subscription\":\"\",\"chatgpt-api\":\"gpt-5.5-pro\"},\"reasoningEfforts\":{\"chatgpt-subscription\":null,\"chatgpt-api\":\"low\",\"other-api\":null}}\n");
    qa_llm_request_input input;
    memset(&input, 0, sizeof input);
    input.prompt = "hello";
    input.instructions = "answer";
    char *answer = NULL;
    CHECK(!qa_llm_settings_request(service, &input, &answer, &error));
    CHECK_CONTAINS(error.message, "Model default");
    CHECK(mock.calls == 0);
    qa_llm_settings_close(service);
    qa_llm_settings_destroy(service);
    cleanup_dir(dir);
    free(dir);
  }
  /* request timeout covers an open SSE body and cancels its reader;
   * model loading timeout keeps the saved model visible */
  {
    char *dir = make_temp_dir();
    hanging_body hanging = {0};
    mock_fetch mock = {0};
    mock.handler = hanging_handler;
    mock.handler_ctx = &hanging;
    qa_llm_settings_options options = settings_options(dir);
    options.request.fetch = mock_ref(&mock);
    options.request.has_timeout = true;
    options.request.timeout_ms = 10;
    qa_llm_settings_service *service = qa_llm_settings_open(&options);
    CHECK(qa_llm_settings_save_api_key(service, QA_LLM_PROVIDER_CHATGPT_API, "key", &error));
    CHECK(qa_llm_settings_set_model(service, QA_LLM_PROVIDER_CHATGPT_API, "saved-model", &error));
    CHECK(qa_llm_settings_select_provider(service, QA_LLM_PROVIDER_CHATGPT_API, &error));
    qa_llm_model_list models;
    CHECK(!qa_llm_settings_refresh_models(service, QA_LLM_PROVIDER_CHATGPT_API, NULL, &models, &error));
    CHECK_CONTAINS(error.message, "timed out");
    qa_llm_settings_snapshot snapshot;
    qa_llm_settings_read(service, &snapshot);
    CHECK(snapshot.catalogs[QA_LLM_PROVIDER_CHATGPT_API].status == QA_LLM_CATALOG_ERROR);
    CHECK_STR(snapshot.providers.chatgpt_api.model, "saved-model");
    qa_llm_settings_snapshot_free(&snapshot);
    qa_llm_request_input input;
    memset(&input, 0, sizeof input);
    input.prompt = "hello";
    input.instructions = "answer";
    char *answer = NULL;
    hanging.cancelled = false;
    CHECK(!qa_llm_settings_request(service, &input, &answer, &error));
    CHECK_CONTAINS(error.message, "timed out");
    CHECK(hanging.cancelled);
    qa_llm_settings_close(service);
    qa_llm_settings_destroy(service);
    cleanup_dir(dir);
    free(dir);
  }
  /* cancelled discovery cannot save a default model (adapted: the mock
   * aborts the caller signal mid-fetch, then returns the payload) */
  {
    char *dir = make_temp_dir();
    qa_llm_settings_options options = settings_options(dir);
    qa_llm_settings_service *service = qa_llm_settings_open(&options);
    seed_subscription(dir, "header", "fake-refresh", (double)qa_llm_now_ms() + 3600000, "k");
    qa_llm_abort_signal caller;
    qa_llm_signal_init(&caller);
    extern bool aborting_models_handler(void *, const char *, const qa_llm_http_init *, qa_llm_http_response *, qa_llm_error *);
    mock_fetch mock = {0};
    mock.handler = aborting_models_handler;
    mock.handler_ctx = &caller;
    /* re-open with the mock wired (options are copied at open) */
    qa_llm_settings_close(service);
    qa_llm_settings_destroy(service);
    options.request.fetch = mock_ref(&mock);
    service = qa_llm_settings_open(&options);
    qa_llm_model_list models;
    CHECK(!qa_llm_settings_refresh_models(service, QA_LLM_PROVIDER_SUBSCRIPTION, &caller, &models, &error));
    CHECK_CONTAINS(error.message, "cancelled");
    qa_llm_settings_snapshot snapshot;
    qa_llm_settings_read(service, &snapshot);
    CHECK_STR(snapshot.model, "");
    CHECK(snapshot.catalogs[QA_LLM_PROVIDER_SUBSCRIPTION].status == QA_LLM_CATALOG_IDLE);
    qa_llm_settings_snapshot_free(&snapshot);
    qa_llm_settings_close(service);
    qa_llm_settings_destroy(service);
    cleanup_dir(dir);
    free(dir);
  }
}

bool aborting_models_handler(void *ctx, const char *url, const qa_llm_http_init *init,
                             qa_llm_http_response *out, qa_llm_error *error) {
  (void)url; (void)init; (void)error;
  ((qa_llm_abort_signal *)ctx)->aborted = true;
  qa_llm_response_init_memory(out, 200, "application/json", CODEX_MODELS_JSON, strlen(CODEX_MODELS_JSON), 0);
  return true;
}

/* token endpoint mock (auth.fetch) */
typedef struct token_endpoint {
  char *response_json;
  char *last_body;
  char *last_url;
  int calls;
  bool pkce_ok;
  const char *expected_challenge;
} token_endpoint;

static bool token_handler(void *ctx, const char *url, const qa_llm_http_init *init,
                          qa_llm_http_response *out, qa_llm_error *error) {
  (void)error;
  token_endpoint *endpoint = ctx;
  endpoint->calls++;
  free(endpoint->last_body);
  free(endpoint->last_url);
  endpoint->last_body = init->body != NULL ? qa_llm_strndup(init->body, init->body_length) : qa_llm_strdup("");
  endpoint->last_url = qa_llm_strdup(url);
  if (endpoint->expected_challenge != NULL) {
    const char *verifier_at = strstr(endpoint->last_body, "code_verifier=");
    endpoint->pkce_ok = false;
    if (verifier_at != NULL) {
      verifier_at += strlen("code_verifier=");
      char *challenge = qa_llm_sha256_base64url(verifier_at, strlen(verifier_at));
      endpoint->pkce_ok = strcmp(challenge, endpoint->expected_challenge) == 0;
      free(challenge);
    }
  }
  qa_llm_response_init_memory(out, 200, "application/json", endpoint->response_json, strlen(endpoint->response_json), 0);
  return true;
}

static char *token_response_json(const char *label, const char *refresh, bool with_id_token) {
  char *token = make_token(label, "test-account");
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  qa_llm_strbuf_appendf(&out, "{\"access_token\":\"%s\",\"refresh_token\":\"%s\",\"token_type\":\"Bearer\",\"expires_in\":3600,\"scope\":\"openid profile\"%s}",
                        token, refresh, with_id_token ? ",\"id_token\":\"must-not-save\"" : "");
  free(token);
  return qa_llm_strbuf_steal(&out);
}

static void test_settings_refresh(void) {
  qa_llm_error error;
  /* direct refreshSubscription: exact rotating form, missing token rejects */
  {
    qa_llm_subscription_credential credential;
    memset(&credential, 0, sizeof credential);
    credential.access_token = qa_llm_strdup("old-access");
    credential.refresh_token = qa_llm_strdup("old-refresh");
    credential.token_type = qa_llm_strdup("Bearer");
    credential.expires_at = 1;
    credential.scopes = calloc(1, sizeof *credential.scopes);
    credential.scopes[0] = qa_llm_strdup("openid");
    credential.scope_count = 1;
    token_endpoint endpoint = {0};
    endpoint.response_json = token_response_json("new", "test-refresh", true);
    qa_llm_subscription_auth_options auth;
    memset(&auth, 0, sizeof auth);
    mock_fetch mock = {0};
    mock.handler = token_handler;
    mock.handler_ctx = &endpoint;
    auth.fetch = mock_ref(&mock);
    qa_llm_abort_signal signal;
    qa_llm_signal_init(&signal);
    qa_llm_subscription_credential refreshed;
    CHECK(qa_llm_refresh_subscription(&auth, &credential, &signal, &refreshed, &error));
    CHECK_STR(refreshed.refresh_token, "test-refresh");
    CHECK_STR(endpoint.last_body, "grant_type=refresh_token&client_id=app_EMoamEEZ73f0CkXaXp7hrann&refresh_token=old-refresh");
    CHECK_STR(captured_header(&mock.last, "Content-Type"), "application/x-www-form-urlencoded");
    qa_llm_subscription_credential_free(&refreshed);
    free(endpoint.response_json);
    endpoint.response_json = qa_llm_strdup("{\"access_token\":\"new\",\"expires_in\":3600}");
    qa_llm_signal_init(&signal);
    /* PLATFORM-014: a refresh response without a new refresh_token keeps the previous one. */
    CHECK(qa_llm_refresh_subscription(&auth, &credential, &signal, &refreshed, &error));
    CHECK_STR(refreshed.refresh_token, "old-refresh");
    qa_llm_subscription_credential_free(&refreshed);
    free(endpoint.response_json);
    free(endpoint.last_body);
    free(endpoint.last_url);
    qa_llm_subscription_credential_free(&credential);
  }
  /* expiring token refreshes once for sequential requests ("concurrent"
   * in TS) and preserves the API credential */
  {
    char *dir = make_temp_dir();
    token_endpoint endpoint = {0};
    endpoint.response_json = token_response_json("new", "new-refresh", false);
    mock_fetch auth_mock = {0};
    auth_mock.handler = token_handler;
    auth_mock.handler_ctx = &endpoint;
    models_answer state = {0};
    state.models_json = CODEX_MODELS_JSON;
    state.answer_sse =
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"answer\"}\n\n"
      "data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}\n\n";
    mock_fetch mock = {0};
    mock.handler = models_answer_handler;
    mock.handler_ctx = &state;
    qa_llm_settings_options options = settings_options(dir);
    options.auth.fetch = mock_ref(&auth_mock);
    options.request.fetch = mock_ref(&mock);
    qa_llm_settings_service *service = qa_llm_settings_open(&options);
    seed_subscription(dir, "old", "old-refresh", (double)qa_llm_now_ms() + 30000, "preserved-api");
    CHECK(qa_llm_settings_set_model(service, QA_LLM_PROVIDER_SUBSCRIPTION, "test-model", &error));
    qa_llm_request_input input;
    memset(&input, 0, sizeof input);
    input.prompt = "first";
    input.instructions = "answer";
    char *answer = NULL;
    CHECK(qa_llm_settings_request(service, &input, &answer, &error));
    CHECK_STR(answer, "answer");
    free(answer);
    input.prompt = "second";
    CHECK(qa_llm_settings_request(service, &input, &answer, &error));
    free(answer);
    CHECK(endpoint.calls == 1);
    CHECK(state.posts == 2);
    char *new_token = make_token("new", "test-account");
    qa_llm_strbuf expected_auth;
    qa_llm_strbuf_init(&expected_auth);
    qa_llm_strbuf_appendf(&expected_auth, "Bearer %s", new_token);
    CHECK_STR(captured_header(&mock.last, "authorization"), expected_auth.data);
    qa_llm_strbuf_free(&expected_auth);
    free(new_token);
    char *text = read_at(dir, "chatgpt.key");
    CHECK_CONTAINS(text, "new-refresh");
    CHECK_CONTAINS(text, "preserved-api");
    free(text);
    CHECK(file_mode(dir, "chatgpt.key") == 0600);
    qa_llm_settings_snapshot snapshot;
    qa_llm_settings_read(service, &snapshot);
    CHECK(snapshot.subscription_auth == QA_LLM_SUBSCRIPTION_AUTH_IDLE);
    qa_llm_settings_snapshot_free(&snapshot);
    qa_llm_settings_close(service);
    qa_llm_settings_destroy(service);
    free(endpoint.response_json);
    free(endpoint.last_body);
    free(endpoint.last_url);
    for (int i = 0; i < state.body_count; i++) { free(state.bodies[i]); free(state.urls[i]); }
    cleanup_dir(dir);
    free(dir);
  }
  /* 401/403 retried only once after refresh */
  for (int i = 0; i < 2; i++) {
    int status = i == 0 ? 401 : 403;
    char *dir = make_temp_dir();
    token_endpoint endpoint = {0};
    endpoint.response_json = token_response_json("new", "new-refresh", false);
    mock_fetch auth_mock = {0};
    auth_mock.handler = token_handler;
    auth_mock.handler_ctx = &endpoint;
    mock_fetch mock = {0};
    mock.handler = status_handler;
    mock.handler_ctx = (void *)(intptr_t)status;
    qa_llm_settings_options options = settings_options(dir);
    options.auth.fetch = mock_ref(&auth_mock);
    options.request.fetch = mock_ref(&mock);
    qa_llm_settings_service *service = qa_llm_settings_open(&options);
    seed_subscription(dir, "old", "old-refresh", (double)qa_llm_now_ms() + 3600000, "preserved-api");
    CHECK(qa_llm_settings_set_model(service, QA_LLM_PROVIDER_SUBSCRIPTION, "test-model", &error));
    qa_llm_request_input input;
    memset(&input, 0, sizeof input);
    input.prompt = "hello";
    input.instructions = "answer";
    char *answer = NULL;
    CHECK(!qa_llm_settings_request(service, &input, &answer, &error));
    char expected[32];
    snprintf(expected, sizeof expected, "HTTP %d", status);
    CHECK_CONTAINS(error.message, expected);
    CHECK(endpoint.calls == 1);
    CHECK(mock.calls == 2);
    qa_llm_settings_close(service);
    qa_llm_settings_destroy(service);
    free(endpoint.response_json);
    free(endpoint.last_body);
    free(endpoint.last_url);
    cleanup_dir(dir);
    free(dir);
  }
  /* saved subscription effort reloads live capabilities on first request */
  {
    char *dir = make_temp_dir();
    models_answer state = {0};
    state.models_json = CODEX_MODELS_JSON;
    state.answer_sse = RESPONSES_OK;
    mock_fetch mock = {0};
    mock.handler = models_answer_handler;
    mock.handler_ctx = &state;
    qa_llm_settings_options options = settings_options(dir);
    options.request.fetch = mock_ref(&mock);
    qa_llm_settings_service *service = qa_llm_settings_open(&options);
    seed_subscription(dir, "header", "fake-refresh", (double)qa_llm_now_ms() + 3600000, "k");
    qa_llm_model_list models;
    CHECK(qa_llm_settings_refresh_models(service, QA_LLM_PROVIDER_SUBSCRIPTION, NULL, &models, &error));
    qa_llm_model_list_free(&models);
    CHECK(qa_llm_settings_set_model(service, QA_LLM_PROVIDER_SUBSCRIPTION, "test-reasoner", &error));
    CHECK(qa_llm_settings_set_reasoning_effort(service, QA_LLM_PROVIDER_SUBSCRIPTION, "xhigh", &error));
    qa_llm_settings_close(service);
    qa_llm_settings_destroy(service);
    state.gets = 0;
    state.posts = 0;
    service = qa_llm_settings_open(&options);
    qa_llm_request_input input;
    memset(&input, 0, sizeof input);
    input.prompt = "hello";
    input.instructions = "commands";
    char *answer = NULL;
    CHECK(qa_llm_settings_request(service, &input, &answer, &error));
    CHECK_STR(answer, "ok");
    free(answer);
    CHECK(state.gets == 1 && state.posts == 1);
    CHECK_CONTAINS(state.bodies[state.body_count - 1], "\"effort\":\"xhigh\"");
    qa_llm_settings_close(service);
    qa_llm_settings_destroy(service);
    for (int j = 0; j < state.body_count; j++) { free(state.bodies[j]); free(state.urls[j]); }
    cleanup_dir(dir);
    free(dir);
  }
}

/* settings prime/session over the OpenAI API transport */
typedef struct prime_state {
  char *bodies[8];
  int count;
} prime_state;

static bool prime_handler(void *ctx, const char *url, const qa_llm_http_init *init,
                          qa_llm_http_response *out, qa_llm_error *error) {
  (void)url; (void)error;
  prime_state *state = ctx;
  const char *body = init->body != NULL ? init->body : "";
  if (state->count < 8) state->bodies[state->count++] = qa_llm_strdup(body);
  const char *answer = strstr(body, "READY only") != NULL ? "READY"
      : strstr(body, "Current request:\\nsecond") != NULL ? "second answer" : "first answer";
  qa_llm_strbuf sse;
  qa_llm_strbuf_init(&sse);
  qa_llm_strbuf_appendf(&sse, "data: {\"type\":\"response.output_text.delta\",\"delta\":\"%s\"}\n\n"
                               "data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}\n\n", answer);
  sse_response(out, sse.data);
  qa_llm_strbuf_free(&sse);
  return true;
}

static void test_settings_prime_session(void) {
  qa_llm_error error;
  char *dir = make_temp_dir();
  prime_state state = {0};
  mock_fetch mock = {0};
  mock.handler = prime_handler;
  mock.handler_ctx = &state;
  qa_llm_settings_options options = settings_options(dir);
  options.request.fetch = mock_ref(&mock);
  qa_llm_settings_service *service = qa_llm_settings_open(&options);
  CHECK(qa_llm_settings_save_api_key(service, QA_LLM_PROVIDER_CHATGPT_API, "api-secret", &error));
  CHECK(qa_llm_settings_set_model(service, QA_LLM_PROVIDER_CHATGPT_API, "test-model", &error));
  CHECK(qa_llm_settings_select_provider(service, QA_LLM_PROVIDER_CHATGPT_API, &error));

  CHECK(qa_llm_settings_prime(service, "SESSION CATALOG", &error));
  CHECK(qa_llm_settings_prime(service, "SESSION CATALOG", &error));
  CHECK(state.count == 1);
  CHECK_CONTAINS(state.bodies[0], "SESSION CATALOG");
  qa_llm_strbuf cache;
  qa_llm_strbuf_init(&cache);
  qa_llm_strbuf_appendf(&cache, "\"prompt_cache_key\":\"%s\"", qa_llm_settings_session_id(service));
  CHECK_CONTAINS(state.bodies[0], cache.data);
  qa_llm_strbuf_free(&cache);

  /* OpenAI API transport shape, from request.test.ts */
  CHECK_STR(mock.last.url, "https://api.openai.com/v1/responses");
  CHECK(mock.last.redirect_error);
  CHECK_STR(captured_header(&mock.last, "authorization"), "Bearer api-secret");
  CHECK(captured_header(&mock.last, "chatgpt-account-id") == NULL);

  qa_llm_request_input input;
  memset(&input, 0, sizeof input);
  input.prompt = "first";
  input.instructions = "turn one";
  input.session_instructions = "SESSION CATALOG";
  char *answer = NULL;
  CHECK(qa_llm_settings_request(service, &input, &answer, &error));
  CHECK_STR(answer, "first answer");
  free(answer);
  input.prompt = "second";
  input.instructions = "turn two";
  CHECK(qa_llm_settings_request(service, &input, &answer, &error));
  CHECK_STR(answer, "second answer");
  free(answer);
  CHECK(state.count == 3);
  CHECK_CONTAINS(state.bodies[1], "SESSION CATALOG");
  CHECK_CONTAINS(state.bodies[1], "turn one");
  CHECK_CONTAINS(state.bodies[2], "User: first");
  CHECK_CONTAINS(state.bodies[2], "Assistant: first answer");

  char *first_session = qa_llm_strdup(qa_llm_settings_session_id(service));
  CHECK(qa_llm_settings_set_model(service, QA_LLM_PROVIDER_CHATGPT_API, "other-model", &error));
  CHECK(strcmp(qa_llm_settings_session_id(service), first_session) != 0);
  free(first_session);
  input.prompt = "fresh";
  input.instructions = "turn three";
  CHECK(qa_llm_settings_request(service, &input, &answer, &error));
  free(answer);
  CHECK(state.count == 5);
  CHECK_NOT_CONTAINS(state.bodies[4], "Assistant: first answer");
  qa_llm_strbuf_init(&cache);
  qa_llm_strbuf_appendf(&cache, "\"prompt_cache_key\":\"%s\"", qa_llm_settings_session_id(service));
  CHECK_CONTAINS(state.bodies[4], cache.data);
  qa_llm_strbuf_free(&cache);
  qa_llm_settings_close(service);
  qa_llm_settings_destroy(service);
  for (int i = 0; i < state.count; i++) free(state.bodies[i]);
  cleanup_dir(dir);
  free(dir);
}

/* ---- auth worker protocol (direct function calls) ---- */

typedef struct posted {
  qa_llm_auth_worker_message_kind kinds[8];
  char *codes[8];
  int ports[8];
  int count;
} posted;

static void record_post(void *ctx, const qa_llm_auth_worker_message *message) {
  posted *state = ctx;
  if (state->count < 8) {
    state->kinds[state->count] = message->kind;
    state->ports[state->count] = message->port;
    state->codes[state->count] = message->code != NULL ? qa_llm_strdup(message->code) : NULL;
    state->count++;
  }
}

static void test_auth_worker(void) {
  qa_llm_json *data = qa_llm_json_object();
  qa_llm_json_object_set(data, "port", qa_llm_json_number(0));
  qa_llm_json_object_set(data, "state", qa_llm_json_string("expected-state"));
  int port = -1;
  char *state = NULL;
  qa_llm_auth_worker_parse_data(data, &port, &state);
  CHECK(port == 0);
  CHECK_STR(state, "expected-state");
  qa_llm_json_free(data);
  free(state);

  posted messages = {0};
  qa_llm_auth_worker *worker = qa_llm_auth_worker_start(0, "expected-state", record_post, &messages);
  CHECK(worker != NULL);
  CHECK(messages.count == 1 && messages.kinds[0] == QA_LLM_AUTH_WORKER_READY);
  CHECK(qa_llm_auth_worker_port(worker) > 0);
  CHECK_STR(qa_llm_auth_worker_message_kind_name(QA_LLM_AUTH_WORKER_CODE), "code");

  int status = 0;
  char *body = NULL;
  qa_llm_auth_worker_handle_request(worker, "GET", "/other", &status, &body);
  CHECK(status == 404);
  CHECK_STR(body, "Not found");
  free(body);
  qa_llm_auth_worker_handle_request(worker, "POST", "/auth/callback", &status, &body);
  CHECK(status == 405);
  free(body);
  int before_mismatch = messages.count;
  qa_llm_auth_worker_handle_request(worker, "GET", "/auth/callback?state=wrong&code=x", &status, &body);
  CHECK(status == 400);
  CHECK_STR(body, "Sign-in failed. State mismatch.");
  CHECK(messages.count == before_mismatch); /* a stray request posts nothing */
  free(body);
  qa_llm_auth_worker_handle_request(worker, "GET", "/auth/callback?state=expected-state&error=access_denied", &status, &body);
  CHECK(status == 400);
  CHECK_STR(body, "Sign-in was not authorized.");
  CHECK(messages.kinds[messages.count - 1] == QA_LLM_AUTH_WORKER_DENIED);
  free(body);
  qa_llm_auth_worker_handle_request(worker, "GET", "/auth/callback?state=expected-state&code=code%20x", &status, &body);
  CHECK(status == 200);
  CHECK_STR(body, "Authorization received. You can return to the game.");
  CHECK(messages.kinds[messages.count - 1] == QA_LLM_AUTH_WORKER_CODE);
  CHECK_STR(messages.codes[messages.count - 1], "code x");
  free(body);
  qa_llm_json *stop = qa_llm_json_object();
  qa_llm_json_object_set(stop, "kind", qa_llm_json_string("stop"));
  qa_llm_json_object_set(stop, "force", qa_llm_json_bool(true));
  qa_llm_auth_worker_handle_message(worker, stop);
  qa_llm_json_free(stop);
  CHECK(messages.kinds[messages.count - 1] == QA_LLM_AUTH_WORKER_STOPPED);
  CHECK(!qa_llm_auth_worker_serve_once(worker, 1));
  qa_llm_auth_worker_destroy(worker, false);
  for (int i = 0; i < messages.count; i++) free(messages.codes[i]);
}

/* ---- subscription OAuth sign-in over a real loopback socket ---- */

static void percent_decode_into(char *out, size_t capacity, const char *text, size_t length) {
  size_t used = 0;
  for (size_t i = 0; i < length && used + 1 < capacity; i++) {
    char c = text[i];
    if (c == '%' && i + 2 < length) {
      int hi = -1, lo = -1;
      char a = text[i + 1], b = text[i + 2];
      if (a >= '0' && a <= '9') hi = a - '0'; else if (a >= 'a' && a <= 'f') hi = a - 'a' + 10; else if (a >= 'A' && a <= 'F') hi = a - 'A' + 10;
      if (b >= '0' && b <= '9') lo = b - '0'; else if (b >= 'a' && b <= 'f') lo = b - 'a' + 10; else if (b >= 'A' && b <= 'F') lo = b - 'A' + 10;
      if (hi >= 0 && lo >= 0) { out[used++] = (char)(hi * 16 + lo); i += 2; continue; }
    }
    out[used++] = c == '+' ? ' ' : c;
  }
  out[used] = '\0';
}

static bool query_param(const char *url, const char *name, char *out, size_t capacity) {
  const char *query = strchr(url, '?');
  if (query == NULL) return false;
  query++;
  size_t name_len = strlen(name);
  while (*query != '\0') {
    const char *amp = strchr(query, '&');
    size_t span = amp != NULL ? (size_t)(amp - query) : strlen(query);
    if (span > name_len && memcmp(query, name, name_len) == 0 && query[name_len] == '=') {
      percent_decode_into(out, capacity, query + name_len + 1, span - name_len - 1);
      return true;
    }
    if (amp == NULL) break;
    query = amp + 1;
  }
  return false;
}

typedef struct browser_state {
  int mode; /* 0 correct, 1 wrong state, 2 cancel, 3 nothing, 4 record only */
  int runs;
  pid_t child;
  char challenge[256];
  char scope[128];
  char callback[1024];
  qa_llm_settings_service *service;
} browser_state;

static int browser_request(const char *callback, const char *state, bool with_error);
static void browser_child(const char *callback, const char *state, bool with_error) {
  _exit(browser_request(callback, state, with_error));
}
/* PLATFORM-015: a wrong-state request gets 400 without provider text, then the
 * real callback still succeeds (the stray request does not abort sign-in). */
static void browser_child_stray_then_real(const char *callback, const char *state) {
  if (browser_request(callback, "wrong-state-secret", true) != 3) _exit(4);
  _exit(browser_request(callback, state, false));
}
static int browser_request(const char *callback, const char *state, bool with_error) {
  /* callback is http://localhost:PORT/auth/callback */
  const char *port_at = strrchr(callback, ':');
  int port = port_at != NULL ? atoi(port_at + 1) : 0;
  sleep_ms(50);
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return 4;
  struct sockaddr_in address;
  memset(&address, 0, sizeof address);
  address.sin_family = AF_INET;
  address.sin_port = htons((uint16_t)port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, (struct sockaddr *)&address, sizeof address) != 0) return 4;
  char request[2048];
  snprintf(request, sizeof request,
           "GET /auth/callback?state=%s&code=test-code%s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n",
           state, with_error ? "&error=provider-secret" : "");
  if (write(fd, request, strlen(request)) < 0) return 4;
  char response[4096];
  size_t length = 0;
  ssize_t got;
  while (length + 1 < sizeof response && (got = read(fd, response + length, sizeof response - 1 - length)) > 0) {
    length += (size_t)got;
  }
  response[length] = '\0';
  close(fd);
  if (strstr(response, " 200 ") != NULL && strstr(response, "Authorization received. You can return to the game.") != NULL) return 0;
  if (strstr(response, " 400 ") != NULL && strstr(response, "secret-") == NULL) return 3;
  return 4;
}

static bool scripted_browser(void *ctx, const char *url, qa_llm_abort_signal *signal, qa_llm_error *error) {
  (void)signal; (void)error;
  browser_state *state = ctx;
  state->runs++;
  query_param(url, "code_challenge", state->challenge, sizeof state->challenge);
  query_param(url, "scope", state->scope, sizeof state->scope);
  char oauth_state[256] = "";
  char redirect[1024] = "";
  query_param(url, "state", oauth_state, sizeof oauth_state);
  query_param(url, "redirect_uri", redirect, sizeof redirect);
  snprintf(state->callback, sizeof state->callback, "%s", redirect);
  if (state->mode == 2) {
    qa_llm_settings_cancel_sign_in(state->service);
    return true;
  }
  if (state->mode == 3 || state->mode == 4) return true;
  pid_t pid = fork();
  if (pid == 0) {
    if (state->mode == 1) browser_child_stray_then_real(redirect, oauth_state);
    browser_child(redirect, oauth_state, false);
    _exit(4);
  }
  state->child = pid;
  return pid > 0;
}

static int wait_child(browser_state *state) {
  if (state->child <= 0) return -1;
  int status = 0;
  waitpid(state->child, &status, 0);
  state->child = 0;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static bool connect_refused(const char *callback) {
  const char *port_at = strrchr(callback, ':');
  int port = port_at != NULL ? atoi(port_at + 1) : 0;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return false;
  struct sockaddr_in address;
  memset(&address, 0, sizeof address);
  address.sin_family = AF_INET;
  address.sin_port = htons((uint16_t)port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bool refused = connect(fd, (struct sockaddr *)&address, sizeof address) != 0;
  close(fd);
  return refused;
}

static void test_sign_in(void) {
  qa_llm_error error;
  /* real loopback callback verifies PKCE, stores tokens, keeps API key */
  {
    char *dir = make_temp_dir();
    browser_state browser = {0};
    token_endpoint endpoint = {0};
    endpoint.response_json = token_response_json("test-access-label", "test-refresh", true);
    /* access_token in TS fixture is plain "test-access"; any token works */
    free(endpoint.response_json);
    endpoint.response_json = qa_llm_strdup("{\"access_token\":\"test-access\",\"refresh_token\":\"test-refresh\",\"token_type\":\"Bearer\",\"expires_in\":3600,\"scope\":\"openid profile\",\"id_token\":\"must-not-save\"}");
    endpoint.expected_challenge = browser.challenge; /* filled pre-token-call */
    mock_fetch auth_mock = {0};
    auth_mock.handler = token_handler;
    auth_mock.handler_ctx = &endpoint;
    qa_llm_settings_options options = settings_options(dir);
    options.auth.has_callback_port = true;
    options.auth.callback_port = 0;
    options.auth.open_browser = scripted_browser;
    options.auth.open_browser_ctx = &browser;
    options.auth.fetch = mock_ref(&auth_mock);
    qa_llm_settings_service *service = qa_llm_settings_open(&options);
    browser.service = service;
    CHECK(qa_llm_settings_save_api_key(service, QA_LLM_PROVIDER_CHATGPT_API, "api-preserved", &error));
    CHECK(qa_llm_settings_sign_in_subscription(service, &error));
    CHECK(wait_child(&browser) == 0); /* full response before listener closed */
    CHECK(endpoint.calls == 1);
    CHECK(endpoint.pkce_ok);
    CHECK_STR(browser.scope, "openid profile email offline_access");
    CHECK_CONTAINS(endpoint.last_body, "client_id=app_EMoamEEZ73f0CkXaXp7hrann");
    CHECK_CONTAINS(endpoint.last_body, "grant_type=authorization_code");
    qa_llm_settings_snapshot snapshot;
    qa_llm_settings_read(service, &snapshot);
    CHECK(snapshot.providers.subscription.configured);
    CHECK(snapshot.providers.chatgpt_api.configured);
    qa_llm_settings_snapshot_free(&snapshot);
    char *text = read_at(dir, "chatgpt.key");
    CHECK_CONTAINS(text, "api-preserved");
    CHECK_NOT_CONTAINS(text, "must-not-save");
    free(text);
    CHECK(qa_llm_settings_remove_credential(service, QA_LLM_PROVIDER_SUBSCRIPTION, &error));
    qa_llm_settings_read(service, &snapshot);
    CHECK(snapshot.providers.chatgpt_api.configured);
    CHECK(!snapshot.providers.subscription.configured);
    qa_llm_settings_snapshot_free(&snapshot);
    qa_llm_settings_close(service);
    qa_llm_settings_destroy(service);
    free(endpoint.response_json);
    free(endpoint.last_body);
    free(endpoint.last_url);
    cleanup_dir(dir);
    free(dir);
  }
  /* PLATFORM-015: a wrong-state request gets 400 without requesting tokens or
   * reflecting provider text, and does not abort sign-in. */
  {
    char *dir = make_temp_dir();
    browser_state browser = {0};
    browser.mode = 1;
    token_endpoint endpoint = {0};
    endpoint.response_json = token_response_json("stray", "test-refresh", true);
    mock_fetch auth_mock = {0};
    auth_mock.handler = token_handler;
    auth_mock.handler_ctx = &endpoint;
    qa_llm_settings_options options = settings_options(dir);
    options.auth.has_callback_port = true;
    options.auth.callback_port = 0;
    options.auth.open_browser = scripted_browser;
    options.auth.open_browser_ctx = &browser;
    options.auth.fetch = mock_ref(&auth_mock);
    qa_llm_settings_service *service = qa_llm_settings_open(&options);
    browser.service = service;
    CHECK(qa_llm_settings_sign_in_subscription(service, &error));
    CHECK(wait_child(&browser) == 0); /* 400 without provider text, then 200 */
    CHECK(endpoint.calls == 1);
    qa_llm_settings_snapshot snapshot;
    qa_llm_settings_read(service, &snapshot);
    CHECK(snapshot.subscription_auth == QA_LLM_SUBSCRIPTION_AUTH_IDLE);
    CHECK(snapshot.subscription_auth_message == NULL || strstr(snapshot.subscription_auth_message, "secret") == NULL);
    qa_llm_settings_snapshot_free(&snapshot);
    qa_llm_settings_close(service);
    qa_llm_settings_destroy(service);
    free(endpoint.response_json);
    free(endpoint.last_body);
    free(endpoint.last_url);
    cleanup_dir(dir);
    free(dir);
  }
  /* cancel settles, stops callback, prevents saves; then a restart works */
  {
    char *dir = make_temp_dir();
    browser_state browser = {0};
    browser.mode = 2;
    token_endpoint endpoint = {0};
    endpoint.response_json = qa_llm_strdup("{\"access_token\":\"test-access\",\"refresh_token\":\"test-refresh\",\"token_type\":\"Bearer\",\"expires_in\":3600}");
    mock_fetch auth_mock = {0};
    auth_mock.handler = token_handler;
    auth_mock.handler_ctx = &endpoint;
    qa_llm_settings_options options = settings_options(dir);
    options.auth.has_callback_port = true;
    options.auth.callback_port = 0;
    options.auth.open_browser = scripted_browser;
    options.auth.open_browser_ctx = &browser;
    options.auth.fetch = mock_ref(&auth_mock);
    qa_llm_settings_service *service = qa_llm_settings_open(&options);
    browser.service = service;
    CHECK(qa_llm_settings_sign_in_subscription(service, &error)); /* TS resolves */
    qa_llm_settings_snapshot snapshot;
    qa_llm_settings_read(service, &snapshot);
    CHECK(snapshot.subscription_auth == QA_LLM_SUBSCRIPTION_AUTH_IDLE);
    qa_llm_settings_snapshot_free(&snapshot);
    CHECK(connect_refused(browser.callback));
    CHECK(!file_exists(dir, "chatgpt.key"));
    browser.mode = 0;
    CHECK(qa_llm_settings_sign_in_subscription(service, &error));
    CHECK(wait_child(&browser) == 0);
    CHECK(browser.runs == 2);
    qa_llm_settings_read(service, &snapshot);
    CHECK(snapshot.providers.subscription.configured);
    qa_llm_settings_snapshot_free(&snapshot);
    qa_llm_settings_close(service);
    qa_llm_settings_destroy(service);
    free(endpoint.response_json);
    free(endpoint.last_body);
    free(endpoint.last_url);
    cleanup_dir(dir);
    free(dir);
  }
  /* callback timeout and occupied port settle with safe errors */
  {
    char *dir = make_temp_dir();
    browser_state browser = {0};
    browser.mode = 3;
    qa_llm_settings_options options = settings_options(dir);
    options.auth.has_callback_port = true;
    options.auth.callback_port = 0;
    options.auth.has_callback_timeout = true;
    options.auth.callback_timeout_ms = 30;
    options.auth.open_browser = scripted_browser;
    options.auth.open_browser_ctx = &browser;
    qa_llm_settings_service *service = qa_llm_settings_open(&options);
    browser.service = service;
    CHECK(!qa_llm_settings_sign_in_subscription(service, &error));
    CHECK_CONTAINS(error.message, "timed out");
    qa_llm_settings_close(service);
    qa_llm_settings_destroy(service);

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(listener, (struct sockaddr *)&address, sizeof address) == 0);
    CHECK(listen(listener, 4) == 0);
    socklen_t length = sizeof address;
    getsockname(listener, (struct sockaddr *)&address, &length);
    browser_state blocked = {0};
    blocked.mode = 4;
    options = settings_options(dir);
    options.auth.has_callback_port = true;
    options.auth.callback_port = ntohs(address.sin_port);
    options.auth.open_browser = scripted_browser;
    options.auth.open_browser_ctx = &blocked;
    service = qa_llm_settings_open(&options);
    blocked.service = service;
    CHECK(!qa_llm_settings_sign_in_subscription(service, &error));
    CHECK_CONTAINS(error.message, "local sign-in callback");
    CHECK(blocked.runs == 0);
    qa_llm_settings_close(service);
    qa_llm_settings_destroy(service);
    close(listener);
    cleanup_dir(dir);
    free(dir);
  }
  /* default browser (no openBrowser) is unavailable, as in TS */
  {
    qa_llm_subscription_auth_options auth;
    memset(&auth, 0, sizeof auth);
    auth.has_callback_port = true;
    auth.callback_port = 0;
    qa_llm_abort_signal signal;
    qa_llm_signal_init(&signal);
    qa_llm_subscription_credential credential;
    CHECK(!qa_llm_login_subscription(&auth, &signal, &credential, &error));
    CHECK_STR(error.message, "Sign-in browser is unavailable.");
  }
}

int main(void) {
  test_errors();
  test_codex();
  test_codex_stream_shapes();
  test_responses();
  test_chat_completions();
  test_models();
  test_session();
  test_settings_storage();
  test_settings_subscription_models();
  test_settings_api_models();
  test_settings_other_catalog();
  test_settings_request_guards();
  test_settings_refresh();
  test_settings_prime_session();
  test_auth_worker();
  test_sign_in();
  printf("llm: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
