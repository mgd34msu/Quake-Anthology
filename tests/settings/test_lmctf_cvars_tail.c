/* Reproduces tests/content/q2/lmctf/console.test.ts over registry writes; the
 * command buffer path is covered by tests/core. Rules are read after a pull
 * because a C struct has no live accessors. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/settings/server/lmctf_cvars.h"

static int checks, failures;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

static void set(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  (void)qa_cvar_registry_set(cvars, name, value, false, &snapshot);
  qa_cvar_read_free(&snapshot);
}
static bool text_is(qa_cvar_registry *cvars, const char *name, const char *expected) {
  char *value = qa_cvar_registry_variable_string(cvars, name);
  const bool same = strcmp(value, expected) == 0;
  free(value);
  return same;
}

int main(void) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q2_CLASSIC;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&options);
  qa_lmctf_rules rules;
  qa_create_lmctf_rules(&rules);
  rules.countdown_seconds = 7;
  qa_bind_lmctf_console_rules(cvars, &rules);
  CHECK(text_is(cvars, "countdown_time", "7"));
  qa_cvar_snapshot read;
  CHECK(qa_cvar_registry_get(cvars, "countdown_time", &read));
  CHECK(strcmp(read.reset_value, "15") == 0);
  qa_cvar_read_free(&read);
  CHECK(text_is(cvars, "maplist_file", "maplist.txt"));

  set(cvars, "refpassword", "league referee");
  set(cvars, "rcon_password", "owner-secret");
  set(cvars, "autolock", "1");
  set(cvars, "timelimit", "20");
  set(cvars, "countdown_time", "30");
  set(cvars, "maplist_file", "league.txt");
  qa_lmctf_rules_pull_cvars(cvars, &rules);
  CHECK(strcmp(rules.ref_password, "league referee") == 0);
  CHECK(strcmp(rules.rcon_password, "owner-secret") == 0);
  CHECK(rules.auto_lock && rules.time_limit_minutes == 20 && rules.countdown_seconds == 30);
  CHECK(qa_cvar_registry_get(cvars, "timelimit", &read));
  CHECK(read.flags == QA_Q2_CVAR_FLAG_SERVER_INFO);
  qa_cvar_read_free(&read);
  CHECK(qa_cvar_registry_get(cvars, "refpassword", &read));
  CHECK(read.flags == 0);
  qa_cvar_read_free(&read);

  set(cvars, "autolock", "0.5");
  qa_lmctf_rules_pull_cvars(cvars, &rules);
  CHECK(!rules.auto_lock);
  set(cvars, "autolock", "1");

  qa_lmctf_rules_set_number(cvars, &rules, "timelimit", 12);
  CHECK(qa_cvar_registry_variable_value(cvars, "timelimit") == 12);
  CHECK(rules.time_limit_minutes == 12);

  /* A replacement rules object keeps registry values. */
  qa_lmctf_rules next;
  qa_create_lmctf_rules(&next);
  qa_bind_lmctf_console_rules(cvars, &next);
  CHECK(strcmp(next.ref_password, "league referee") == 0 && next.time_limit_minutes == 12);
  CHECK(text_is(cvars, "maplist_file", "league.txt"));
  qa_lmctf_rules_set_string(cvars, &next, "refpassword", "");
  CHECK(strcmp(next.ref_password, "") == 0);
  CHECK(next.auto_lock);
  /* A setter value that aliases the rules string survives the pull. */
  qa_lmctf_rules_set_string(cvars, &next, "rcon_password", next.rcon_password);
  CHECK(strcmp(next.rcon_password, "owner-secret") == 0);

  qa_lmctf_rules_destroy(&rules);
  qa_lmctf_rules_destroy(&next);
  qa_cvar_registry_destroy(cvars);
  printf("test_lmctf_cvars_tail: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
