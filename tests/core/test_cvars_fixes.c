/*
 * QuakeWorld localinfo store (TS CvarRegistry.localInfo) and the
 * serverinfo/localinfo builtins, ported for the world infokey fallback.
 * Expected values from bun (TS src/core/commands/index.ts, src/core/cvars):
 *   append "serverinfo dq 1\nlocalinfo secret abc\nlocalinfo *bad 1\n
 *   localinfo other xyz\nlocalinfo secret def\nlocalinfo\nlocalinfo bad;key 1\n
 *   localinfo k\nserverinfo\n" on q1-quakeworld:
 *   localInfo [["secret","def"],["other","xyz"]]
 *   output "Can't set * keys\nsecret              def\nother               xyz\n
 *           usage: localinfo [ <key> <value> ]\nusage: localinfo [ <key> <value> ]\n
 *           dq                  1\n"
 *   then `localinfo secret ""` leaves [["other","xyz"]].
 *   tests/console/shared-setting-cvars.test.ts: NetQuake has no serverinfo.
 * GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                    \
  do {                                                                                 \
    checks++;                                                                          \
    if (!(cond)) {                                                                     \
      failures++;                                                                      \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
    }                                                                                  \
  } while (0)

typedef struct sink { char text[1024]; size_t length; int sentinel; } sink;
static void print_into(void *ctx, const char *text, const qa_command_context *source) {
  (void)source;
  sink *out = ctx;
  CHECK(out->sentinel == 0x51f1);
  size_t length = strlen(text);
  if (out->length + length < sizeof out->text) {
    memcpy(out->text + out->length, text, length + 1);
    out->length += length;
  }
}
static qa_command_buffer *make_buffer(qa_command_dialect dialect, qa_command_context context,
                                      qa_cvar_registry *cvars, sink *out) {
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context = context;
  options.cvars = cvars;
  options.print = print_into;
  options.print_ctx = out;
  return qa_command_buffer_create(&options);
}
static qa_cvar_registry *make_cvars(qa_command_dialect dialect, qa_command_context context) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context = context;
  return qa_cvar_registry_create(&options);
}

static void test_localinfo(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "qw-info"));
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = identity.session;
  context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  qa_cvar_registry *registry = make_cvars(QA_COMMAND_DIALECT_Q1_QUAKEWORLD, context);
  sink out;
  memset(&out, 0, sizeof out);
  out.sentinel = 0x51f1;
  qa_command_buffer *commands = make_buffer(QA_COMMAND_DIALECT_Q1_QUAKEWORLD, context, registry, &out);
  CHECK(registry != NULL && commands != NULL);
  qa_command_buffer_append(commands,
      "serverinfo dq 1\nlocalinfo secret abc\nlocalinfo *bad 1\nlocalinfo other xyz\nlocalinfo secret def\n"
      "localinfo\nlocalinfo bad;key 1\nlocalinfo k\nserverinfo\n", NULL, -1);
  (void)qa_command_buffer_execute(commands);
  CHECK(qa_cvar_registry_local_info_count(registry) == 2);
  const char *key = NULL, *value = NULL;
  CHECK(qa_cvar_registry_local_info_entry(registry, 0, &key, &value) &&
        strcmp(key, "secret") == 0 && strcmp(value, "def") == 0);
  CHECK(qa_cvar_registry_local_info_entry(registry, 1, &key, &value) &&
        strcmp(key, "other") == 0 && strcmp(value, "xyz") == 0);
  CHECK(qa_cvar_registry_local_info_get(registry, "*bad") == NULL);
  CHECK(strcmp(out.text,
      "Can't set * keys\nsecret              def\nother               xyz\n"
      "usage: localinfo [ <key> <value> ]\nusage: localinfo [ <key> <value> ]\n"
      "dq                  1\n") == 0);
  if (strcmp(out.text, "Can't set * keys\nsecret              def\nother               xyz\n"
      "usage: localinfo [ <key> <value> ]\nusage: localinfo [ <key> <value> ]\ndq                  1\n") != 0)
    fprintf(stderr, "output was: [%s]\n", out.text);
  qa_cvar_read dq;
  CHECK(qa_cvar_registry_find(registry, "dq", &dq) && strcmp(dq.value, "1") == 0);
  qa_cvar_read_free(&dq);
  qa_command_buffer_append(commands, "localinfo secret \"\"\n", NULL, -1);
  (void)qa_command_buffer_execute(commands);
  CHECK(qa_cvar_registry_local_info_get(registry, "secret") == NULL);
  CHECK(qa_cvar_registry_local_info_count(registry) == 1);
  CHECK(strcmp(qa_cvar_registry_local_info_get(registry, "other"), "xyz") == 0);
  qa_command_buffer_destroy(commands);
  qa_cvar_registry_destroy(registry);

  /* NetQuake registers no serverinfo builtin. */
  qa_cvar_registry *other = make_cvars(QA_COMMAND_DIALECT_Q1_NETQUAKE, context);
  sink nq;
  memset(&nq, 0, sizeof nq);
  nq.sentinel = 0x51f1;
  qa_command_buffer *netquake = make_buffer(QA_COMMAND_DIALECT_Q1_NETQUAKE, context, other, &nq);
  qa_command_buffer_append(netquake, "serverinfo dq 1\n", NULL, -1);
  (void)qa_command_buffer_execute(netquake);
  qa_cvar_read missing;
  CHECK(!qa_cvar_registry_find(other, "dq", &missing));
  qa_cvar_read_free(&missing);
  qa_command_buffer_destroy(netquake);
  qa_cvar_registry_destroy(other);
  qa_identity_owner_destroy(&identity);
}

int main(void) {
  test_localinfo();
  if (failures) {
    fprintf(stderr, "%d of %d check(s) failed\n", failures, checks);
    return 1;
  }
  printf("test_cvars_fixes: %d checks ok\n", checks);
  return 0;
}
