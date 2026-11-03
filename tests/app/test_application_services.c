/* Checks for the P2 application services pass (docs/application_layout.md):
 * openInitialConfigurationContent (qa_app_open_initial_configuration_content_native)
 * and createStartupSource (concrete registrations, no host bundle) on retail data,
 * compared with the bun oracle tests/app/oracles/application_services.ts:
 *   QTS_TEST_CORPUS=~/qfiles bun ../quake-anthology-c/tests/app/oracles/application_services.ts
 * The members of application_services.c reference members of groups P3 and P6 that
 * have no definition yet, so no test binary can link that object today.
 * GPL-2.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/app/bootstrap/configuration.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/startup_source.h"
#include "qa/contracts/identity.h"

static int checks, failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)
#define CHECK_STR(actual, expected)                                                         \
  do {                                                                                      \
    const char *a_ = (actual), *e_ = (expected);                                            \
    checks++;                                                                               \
    if (a_ == NULL || strcmp(a_, e_) != 0) {                                                \
      failures++;                                                                           \
      fprintf(stderr, "FAIL %s:%d: %s = \"%s\", expected \"%s\"\n", __FILE__, __LINE__,     \
              #actual, a_ ? a_ : "(null)", e_);                                             \
    }                                                                                       \
  } while (0)

static const char *corpus_root(void) {
  const char *root = getenv("QTS_TEST_CORPUS");
  return root != NULL && root[0] != '\0' ? root : NULL;
}

/* The sentinel print context: every print callback checks it. */
typedef struct print_ctx { unsigned sentinel; int calls; } print_ctx;
#define PRINT_SENTINEL 0x5e2a11edu
static void sentinel_print(void *ctx, const char *text) {
  print_ctx *p = ctx;
  (void)text;
  CHECK(p->sentinel == PRINT_SENTINEL);
  p->calls++;
}

typedef struct oracle_row {
  const char *game, *map, *mode;
  /* the oracle's line, field by field */
  const char *product, *source_provider, *source_content, *engine_provider, *match_provider, *movement_provider, *dialect;
  bool q3_product;
  const char *cvars[9][2]; /* name, value ("-" = absent) */
} oracle_row;

static const oracle_row rows[] = {
  { "q1-classic-id1", "e1m1", NULL, "q1-classic-id1", "q1:official", "q1:classic:id1:installed", "q1:official", "q1:official",
    "q1:movement", "q1-netquake", false,
    { {"skill", "1"}, {"deathmatch", "0"}, {"coop", "0"}, {"maxclients", "16"}, {"sv_maxclients", "-"}, {"g_gametype", "2"},
      {"sv_fps", "-"}, {"fs_restrict", "-"}, {"com_prereleaseDemo", "-"} } },
  { "q2-classic-baseq2", "base1", NULL, "q2-classic-baseq2", "q2:official", "q2:classic:baseq2:installed", "q2:official",
    "q2:official", "q1:movement", "q2-classic", false,
    { {"skill", "1"}, {"deathmatch", "0"}, {"coop", "0"}, {"maxclients", "16"}, {"sv_maxclients", "-"}, {"g_gametype", "2"},
      {"sv_fps", "-"}, {"fs_restrict", "-"}, {"com_prereleaseDemo", "-"} } },
  { "q3-baseq3", "q3dm1", "deathmatch", "q3-baseq3", "q3:official", "q3:classic:baseq3:installed", "q3:official", "q3:official",
    "q1:movement", "q3", true,
    { {"skill", "1"}, {"deathmatch", "1"}, {"coop", "0"}, {"maxclients", "-"}, {"sv_maxclients", "16"}, {"g_gametype", "0"},
      {"sv_fps", "20"}, {"fs_restrict", "0"}, {"com_prereleaseDemo", "0"} } },
};

static void check_row(const oracle_row *row, const char *corpus) {
  const char *argv[7] = { "--game", row->game, "--map", row->map, "--dedicated", "--mode", row->mode };
  size_t argc = row->mode != NULL ? 7 : 5;
  qa_content_root_search search = { corpus, NULL };
  qa_app_application_command command = qa_app_parse_application_command(argv, argc, search, NULL);
  CHECK_STR(command.kind, "run");
  qa_app_options options = command.options;
  qa_app_configuration_content *content = qa_app_open_initial_configuration_content_native(&options, NULL, NULL);
  CHECK(content != NULL);
  if (content == NULL) { qa_app_application_command_destroy(&command); return; }
  qa_app_application_configuration_selection selection = qa_app_configuration_content_selection(content);
  CHECK_STR(options.product, row->product);
  CHECK_STR(selection.source.provider, row->source_provider);
  CHECK_STR(selection.source.content, row->source_content);
  CHECK_STR(selection.engine_behavior.provider, row->engine_provider);
  CHECK_STR(selection.match.provider, row->match_provider);
  CHECK_STR(selection.movement.provider, row->movement_provider);
  const qa_content_catalog *catalog = qa_app_configuration_content_catalog(content);
  qa_command_dialect dialect = qa_app_configuration_dialect_in_catalog(catalog, content);
  CHECK_STR(qa_app_configuration_dialect_name(dialect), row->dialect);
  CHECK(options.has_q3_product == row->q3_product);
  /* createStartupSource(options, { source, match, rules: options.rules ?? "standard" }, dialect, server-console, 16). */
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "oracle"));
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = owner.session;
  context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  qa_app_application_source_selection source_selection = { selection.source, selection.match,
    options.rules != NULL ? options.rules : "standard" };
  print_ctx printer = { PRINT_SENTINEL, 0 };
  qa_cvar_registry *source = qa_app_create_startup_source(&options, source_selection, dialect, context, 16, sentinel_print, &printer);
  CHECK(source != NULL);
  for (size_t i = 0; source != NULL && i < 9; i++) {
    qa_cvar_read read;
    bool found = qa_cvar_registry_find(source, row->cvars[i][0], &read);
    if (found) qa_cvar_read_free(&read);
    char *value = found ? qa_cvar_registry_variable_string(source, row->cvars[i][0]) : NULL;
    CHECK_STR(found ? value : "-", row->cvars[i][1]);
    free(value);
  }
  qa_cvar_registry_destroy(source);
  qa_identity_owner_destroy(&owner);
  qa_app_configuration_content_destroy(content);
  qa_app_application_command_destroy(&command);
}

int main(void) {
  const char *corpus = corpus_root();
  if (corpus == NULL) {
    fprintf(stderr, "skip: QTS_TEST_CORPUS unset\n");
    printf("test_application_services: 0 checks (skipped)\n");
    return 0;
  }
  for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) check_row(&rows[i], corpus);
  printf("test_application_services: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
