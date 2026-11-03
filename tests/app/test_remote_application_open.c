/* C checks for remote-application.ts openSource's early errors (raised before
 * content, renderer or transport opens) and the source-handler/lifetime setup.
 * Expected messages from TS under bun: tests/app/oracles/remote_application_open.ts.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/app/bootstrap/remote_application_internal.h"
#include "qa/foundation/support.h"

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { const char *a_ = (a), *b_ = (b); checks++; \
  if (a_ == NULL || b_ == NULL || strcmp(a_, b_) != 0) { failures++; \
    fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, a_ ? a_ : "(null)", b_ ? b_ : "(null)"); } } while (0)

static int host_sentinel;
static int prints;
static void host_print(void *ctx, const char *text) {
  CHECK(ctx == &host_sentinel);
  (void)text;
  prints++;
}
static void demo_complete(void *ctx, const char *reason) {
  CHECK(ctx == &host_sentinel);
  (void)reason;
}
static qa_app_options base_options(void) {
  qa_app_options options;
  memset(&options, 0, sizeof options);
  options.product = strdup("q1-classic-id1");
  options.map = strdup("start");
  options.character_model = strdup("player");
  options.movement = QA_GAME_FAMILY_Q1;
  options.character = QA_GAME_FAMILY_Q1;
  options.renderer = "cpu";
  options.width = 640; options.height = 480; options.seats = 1; options.gamma = 1;
  options.mode = "singleplayer";
  options.network_kind = "offline";
  return options;
}
typedef struct open_call {
  const qa_app_options *options; const qa_remote_application_host *host;
  bool demo; qa_app_demo_resource resource;
  qa_remote_application *result;
} open_call;
static void open_body(void *ctx) {
  open_call *call = ctx;
  if (call->demo)
    call->result = qa_remote_application_open_demo_borrowed(NULL, call->options, call->host, &call->resource, false, demo_complete,
        &host_sentinel);
  else call->result = qa_remote_application_open(call->options, call->host);
}
static const char *open_error(const qa_app_options *options, bool demo, qa_caught_error *caught) {
  qa_remote_application_host host;
  memset(&host, 0, sizeof host);
  host.base.ctx = &host_sentinel;
  host.base.print = host_print;
  open_call call;
  memset(&call, 0, sizeof call);
  call.options = options;
  call.host = &host;
  call.demo = demo;
  call.resource.kind = QA_APP_DEMO_Q1;
  call.resource.path = "demo1.dem";
  if (qa_try(open_body, &call, caught)) return NULL;
  return caught->message;
}

int main(void) {
  qa_caught_error caught;
  /* bun: offline  RemoteApplication requires a native connect address */
  qa_app_options offline = base_options();
  CHECK_STR(open_error(&offline, false, &caught), "RemoteApplication requires a native connect address");
  qa_app_options_destroy(&offline);
  /* bun: q2-model  Remote Q2 character selection requires an installed male, female or cyborg player appearance */
  qa_app_options q2 = base_options();
  free(q2.product); q2.product = strdup("q2-classic-baseq2");
  free(q2.character_model); q2.character_model = strdup("grunt");
  q2.movement = QA_GAME_FAMILY_Q2; q2.character = QA_GAME_FAMILY_Q2;
  q2.network_kind = "q2-client";
  q2.network_remote = strdup("127.0.0.1:27910");
  CHECK_STR(open_error(&q2, false, &caught),
      "Remote Q2 character selection requires an installed male, female or cyborg player appearance");
  qa_app_options_destroy(&q2);
  /* bun: recorded-browser  Recorded playback requires the retained server browser */
  qa_app_options demo = base_options();
  CHECK_STR(open_error(&demo, true, &caught), "Recorded playback requires the retained server browser");
  qa_app_options_destroy(&demo);
  CHECK(prints == 0);
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
