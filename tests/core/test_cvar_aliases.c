/*
 * Port of tests/core/cvar-aliases.test.ts against qa/core/cvars/index.h.
 * TS `toThrow` cases on plain Errors run in a forked child and must abort (the
 * C registry aborts on these declaration errors, see the header). Not ported:
 * `retained.value` (TS find() returns live getters; C reads are copies, a
 * documented accommodation) and the Object.keys shape checks (JS object shape).
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

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

#define CHECK_ABORTS(...)                                                                   \
  do {                                                                                      \
    checks++;                                                                               \
    fflush(stdout);                                                                         \
    pid_t pid = fork();                                                                     \
    if (pid == 0) {                                                                         \
      (void)prctl(PR_SET_DUMPABLE, 0);                                                      \
      if (freopen("/dev/null", "w", stderr) == NULL) _exit(2);                              \
      __VA_ARGS__;                                                                          \
      _exit(0);                                                                             \
    }                                                                                       \
    int status = 0;                                                                         \
    waitpid(pid, &status, 0);                                                               \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {                            \
      failures++;                                                                           \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
    }                                                                                       \
  } while (0)

typedef struct fixture {
  qa_identity_owner identity;
  qa_cvar_registry *cvars;
  char output[4096];
  char effects[16][32];
  int effect_count;
} fixture;

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *copy = malloc(length + 1);
  memcpy(copy, text, length + 1);
  return copy;
}
static void fixture_print(void *ctx, const char *text) {
  fixture *f = ctx;
  size_t used = strlen(f->output);
  snprintf(f->output + used, sizeof f->output - used, "%s", text);
}
static int no_cheats(void *ctx) {
  (void)ctx;
  return 0;
}
static bool positive(const char *value) {
  double number = qa_js_string_to_number(value);
  return isfinite(number) && number > 0;
}
static char *validate_positive(void *ctx, const char *value) {
  (void)ctx;
  return positive(value) ? NULL : dup_text("must be positive");
}
static void record_change(void *ctx, const char *value) {
  fixture *f = ctx;
  if (f->effect_count < 16) snprintf(f->effects[f->effect_count], 32, "%s", value);
  f->effect_count++;
}
static char *reciprocal_text(const char *value) {
  return dup_text(QA_JS_NUMBER(1 / qa_js_string_to_number(value)));
}
static char *inverse_read(void *ctx, const char *value) {
  (void)ctx;
  return reciprocal_text(value);
}
static bool inverse_write(void *ctx, const char *value, char **out_value, char **out_message) {
  (void)ctx;
  if (!positive(value)) {
    *out_message = dup_text("must be positive");
    return false;
  }
  *out_value = reciprocal_text(value);
  return true;
}

static void fixture_open(fixture *f, qa_command_dialect dialect, int32_t flags, const char *name) {
  memset(f, 0, sizeof *f);
  if (!qa_identity_owner_create(&f->identity, name)) abort();
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context.session = f->identity.session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  options.print = fixture_print;
  options.print_ctx = f;
  options.cheats_allowed = no_cheats;
  f->cvars = qa_cvar_registry_create(&options);
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_register(f->cvars, "canonical", "2", flags, &snapshot)) qa_cvar_read_free(&snapshot);
  qa_cvar_value_binding binding = {f, validate_positive, record_change};
  (void)qa_cvar_registry_bind_value(f->cvars, "canonical", binding);
  qa_cvar_alias alternate;
  memset(&alternate, 0, sizeof alternate);
  alternate.name = "alternate";
  alternate.target = "canonical";
  alternate.documentation.summary = "Same value as canonical.";
  alternate.documentation.usage = "alternate [value]";
  alternate.conversion.is_identity = true;
  qa_cvar_registry_register_alias(f->cvars, alternate);
  qa_cvar_alias inverse;
  memset(&inverse, 0, sizeof inverse);
  inverse.name = "inverse";
  inverse.target = "canonical";
  inverse.documentation.summary = "Reciprocal of canonical.";
  inverse.documentation.usage = "inverse [value]";
  inverse.conversion.read = inverse_read;
  inverse.conversion.write = inverse_write;
  qa_cvar_registry_register_alias(f->cvars, inverse);
}
static void fixture_close(fixture *f) {
  qa_cvar_registry_destroy(f->cvars);
  qa_identity_owner_destroy(&f->identity);
}

static bool value_is(qa_cvar_registry *cvars, const char *name, const char *want) {
  char *value = qa_cvar_registry_variable_string(cvars, name);
  bool same = value != NULL && strcmp(value, want) == 0;
  free(value);
  return same;
}
static bool read_field(qa_cvar_registry *cvars, const char *name, qa_cvar_read *out) {
  memset(out, 0, sizeof *out);
  return qa_cvar_registry_find(cvars, name, out);
}
static void set_value(qa_cvar_registry *cvars, const char *name, const char *value, bool force) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(cvars, name, value, force, &snapshot)) qa_cvar_read_free(&snapshot);
}
static void execute(qa_command_buffer *commands, const char *text) {
  qa_command_buffer_append(commands, text, NULL, -1);
  (void)qa_command_buffer_execute(commands);
}
static bool archive_is_canonical(qa_cvar_registry *cvars, const char *value) {
  size_t count = 0;
  qa_cvar_archive_entry *entries = qa_cvar_registry_archive_entries(cvars, NULL, NULL, &count);
  bool same = count == 1 && strcmp(entries[0].name, "canonical") == 0 && strcmp(entries[0].value, value) == 0;
  for (size_t i = 0; i < count; i++) {
    free(entries[i].name);
    free(entries[i].value);
  }
  free(entries);
  return same;
}
static void free_snapshots(qa_cvar_snapshot *snapshots, size_t count) {
  for (size_t i = 0; i < count; i++) qa_cvar_read_free(&snapshots[i]);
  free(snapshots);
}

static void test_shared_policy(qa_command_dialect dialect) {
  fixture f;
  fixture_open(&f, dialect, QA_CVAR_FLAG_ARCHIVE, "aliases");
  qa_cvar_registry *cvars = f.cvars;
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context = qa_cvar_registry_context(cvars);
  options.cvars = cvars;
  qa_command_buffer *commands = qa_command_buffer_create(&options);
  execute(commands, "inverse 0.25\n");
  CHECK(value_is(cvars, "canonical", "4"));
  CHECK(value_is(cvars, "alternate", "4"));
  qa_cvar_read read;
  CHECK(read_field(cvars, "inverse", &read) && strcmp(read.reset_value, "0.5") == 0);
  qa_cvar_read_free(&read);
  CHECK(f.effect_count == 1 && strcmp(f.effects[0], "4") == 0);
  CHECK(qa_cvar_registry_index_count(cvars) == 1);
  size_t count = 0;
  qa_cvar_snapshot *snapshots = qa_cvar_registry_canonical_snapshots(cvars, 0, &count);
  CHECK(count == 1);
  free_snapshots(snapshots, count);
  snapshots = qa_cvar_registry_snapshots(cvars, 0, &count);
  CHECK(count == 3);
  free_snapshots(snapshots, count);
  bool folded = read_field(cvars, "ALTERNATE", &read);
  if (folded) qa_cvar_read_free(&read);
  CHECK(folded == (dialect == QA_COMMAND_DIALECT_Q3));
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set_console(cvars, "inverse", "0.5", &snapshot)) qa_cvar_read_free(&snapshot);
  CHECK(value_is(cvars, "canonical", "2"));
  qa_cvar_registry_set_command_flags(cvars, "inverse", "0.125", QA_CVAR_COMMAND_FLAG_ARCHIVE);
  CHECK(value_is(cvars, "canonical", "8"));
  CHECK(archive_is_canonical(cvars, "8"));
  if (qa_cvar_registry_stage(cvars, "inverse", "0.25", &snapshot)) qa_cvar_read_free(&snapshot);
  CHECK(read_field(cvars, "inverse", &read) && read.latched_value != NULL && strcmp(read.latched_value, "0.25") == 0);
  qa_cvar_read_free(&read);
  CHECK(read_field(cvars, "alternate", &read) && read.latched_value != NULL && strcmp(read.latched_value, "4") == 0);
  qa_cvar_read_free(&read);
  snapshots = qa_cvar_registry_apply_latched(cvars, "inverse", &count);
  free_snapshots(snapshots, count);
  CHECK(value_is(cvars, "canonical", "4"));
  qa_cvar_registry_reset_console(cvars, "inverse", false);
  CHECK(value_is(cvars, "canonical", "2"));
  if (qa_cvar_registry_set_value(cvars, "inverse", 0.25, &snapshot)) qa_cvar_read_free(&snapshot);
  CHECK(value_is(cvars, "canonical", "4"));
  qa_cvar_registry_clear_modified(cvars, "inverse");
  CHECK(read_field(cvars, "canonical", &read) && !read.modified);
  qa_cvar_read_free(&read);
  set_value(cvars, "inverse", "0", false);
  CHECK(value_is(cvars, "canonical", "4"));
  CHECK(strstr(f.output, "must be positive") != NULL);
  if (qa_cvar_registry_register(cvars, "inverse", "999", QA_CVAR_FLAG_CHEAT, &snapshot)) qa_cvar_read_free(&snapshot);
  CHECK(read_field(cvars, "canonical", &read) && read.flags == QA_CVAR_FLAG_ARCHIVE);
  qa_cvar_read_free(&read);
  qa_save_value *saved = qa_cvar_registry_capture_save_state(cvars);
  const qa_save_value *order = qa_save_value_get(saved, "order");
  CHECK(order != NULL && order->item_count == 1 && strcmp(order->items[0]->text, "canonical") == 0);
  const qa_save_value *variables = qa_save_value_get(saved, "variables");
  CHECK(variables != NULL && variables->item_count == 1);
  set_value(cvars, "alternate", "16", false);
  qa_cvar_registry_restore_save_state(cvars, saved);
  CHECK(value_is(cvars, "inverse", "0.25"));
  fixture reopened;
  fixture_open(&reopened, dialect, QA_CVAR_FLAG_ARCHIVE, "aliases-reopened");
  qa_cvar_archive_entry archived = {"inverse", "0.125"};
  qa_cvar_registry_apply_archive(reopened.cvars, &archived, 1);
  CHECK(archive_is_canonical(reopened.cvars, "8"));
  fixture_close(&reopened);
  if (qa_command_dialect_is_q2(dialect)) {
    if (qa_cvar_registry_full_set(cvars, "inverse", "0.125", QA_Q2_CVAR_FLAG_ARCHIVE, &snapshot))
      qa_cvar_read_free(&snapshot);
    CHECK(value_is(cvars, "canonical", "8"));
  }
  set_value(cvars, "inverse", "0.25", false);
  execute(commands, "toggle inverse 0.25 0.5\n");
  CHECK(value_is(cvars, "canonical", "2"));
  f.effect_count = 0;
  execute(commands, "resetall\n");
  CHECK(f.effect_count == 1 && strcmp(f.effects[0], "2") == 0);
  qa_save_value_free(saved);
  qa_command_buffer_destroy(commands);
  fixture_close(&f);
}

static void test_protection_and_latching(qa_command_dialect dialect) {
  const int32_t q3_flags[] = {QA_CVAR_FLAG_READ_ONLY, QA_CVAR_FLAG_CHEAT, QA_CVAR_FLAG_INIT};
  const int32_t q2_flags[] = {QA_Q2_CVAR_FLAG_READ_ONLY, QA_Q2_CVAR_FLAG_CHEAT, QA_Q2_CVAR_FLAG_NO_SET};
  for (int i = 0; i < 3; i++) {
    fixture f;
    fixture_open(&f, dialect, dialect == QA_COMMAND_DIALECT_Q3 ? q3_flags[i] : q2_flags[i], "aliases-protected");
    set_value(f.cvars, "inverse", "0.25", false);
    CHECK(value_is(f.cvars, "canonical", "2"));
    set_value(f.cvars, "inverse", "0.25", true);
    CHECK(value_is(f.cvars, "canonical", "4"));
    fixture_close(&f);
  }
  fixture f;
  fixture_open(&f, dialect, dialect == QA_COMMAND_DIALECT_Q3 ? QA_CVAR_FLAG_LATCH : QA_Q2_CVAR_FLAG_LATCH, "aliases-latch");
  qa_cvar_registry_set_server_active(f.cvars, true);
  set_value(f.cvars, "inverse", "0.25", false);
  CHECK(value_is(f.cvars, "canonical", "2"));
  qa_cvar_read read;
  CHECK(read_field(f.cvars, "inverse", &read) && read.latched_value != NULL && strcmp(read.latched_value, "0.25") == 0);
  qa_cvar_read_free(&read);
  size_t count = 0;
  qa_cvar_snapshot *latched = qa_cvar_registry_apply_latched(f.cvars, "alternate", &count);
  free_snapshots(latched, count);
  CHECK(value_is(f.cvars, "inverse", "0.25"));
  fixture_close(&f);
}

static void test_vm_aliases(void) {
  fixture f;
  fixture_open(&f, QA_COMMAND_DIALECT_Q3, QA_CVAR_FLAG_ARCHIVE, "aliases-vm");
  qa_vm_cvar identity, inverse;
  CHECK(qa_cvar_registry_register_vm(f.cvars, "alternate", "99", 0, &identity));
  CHECK(qa_cvar_registry_register_vm(f.cvars, "inverse", "99", 0, &inverse));
  CHECK(identity.numeric == 2);
  CHECK(inverse.numeric == 0.5);
  set_value(f.cvars, "canonical", "4", false);
  qa_vm_cvar_update(&identity);
  qa_vm_cvar_update(&inverse);
  CHECK(identity.numeric == 4);
  CHECK(inverse.numeric == 0.25);
  int64_t handle = qa_cvar_registry_bind_vm(f.cvars, "inverse", "99", 0);
  qa_save_value *saved = qa_cvar_registry_capture_save_state(f.cvars);
  const qa_save_value *variables = qa_save_value_get(saved, "variables");
  size_t live = 0;
  for (size_t i = 0; variables != NULL && i < variables->item_count; i++)
    if (variables->items[i]->kind != QA_SAVE_NULL) live++;
  CHECK(live == 1);
  fixture reopened;
  fixture_open(&reopened, QA_COMMAND_DIALECT_Q3, QA_CVAR_FLAG_ARCHIVE, "aliases-vm-reopened");
  qa_cvar_registry_restore_save_state(reopened.cvars, saved);
  qa_cvar_snapshot snapshot;
  CHECK(qa_cvar_registry_read_vm(reopened.cvars, (double)handle, &snapshot) && snapshot.numeric_value == 0.25);
  qa_cvar_read_free(&snapshot);
  CHECK(qa_cvar_registry_bind_vm(reopened.cvars, "inverse", "99", 0) == handle);
  fixture_close(&reopened);
  set_value(f.cvars, "canonical", "8", false);
  qa_cvar_registry_restore_save_state(f.cvars, saved);
  qa_vm_cvar_update(&inverse);
  CHECK(inverse.numeric == 0.25);
  qa_save_value_free(saved);
  qa_vm_cvar_destroy(&identity);
  qa_vm_cvar_destroy(&inverse);
  fixture_close(&f);
}

static void register_identity_alias(qa_cvar_registry *cvars, const char *name, const char *target) {
  qa_cvar_alias alias;
  memset(&alias, 0, sizeof alias);
  alias.name = name;
  alias.target = target;
  alias.documentation.summary = "Alias";
  alias.documentation.usage = "alias";
  alias.conversion.is_identity = true;
  qa_cvar_registry_register_alias(cvars, alias);
}
static char *accept_all(void *ctx, const char *value) {
  (void)ctx;
  (void)value;
  return NULL;
}
static void ignore_change(void *ctx, const char *value) {
  (void)ctx;
  (void)value;
}

static void test_alias_declarations(void) {
  fixture f;
  fixture_open(&f, QA_COMMAND_DIALECT_Q3, QA_CVAR_FLAG_ARCHIVE, "aliases-declarations");
  static const char *const pairs[][2] = {
      {"canonical", "canonical"}, {"loop", "loop"}, {"next", "inverse"}, {"alternate", "canonical"}, {"absent", "missing"}};
  for (int i = 0; i < 5; i++) CHECK_ABORTS(register_identity_alias(f.cvars, pairs[i][0], pairs[i][1]));
  qa_cvar_value_binding binding = {NULL, accept_all, ignore_change};
  CHECK_ABORTS((void)qa_cvar_registry_bind_value(f.cvars, "alternate", binding));
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_register(f.cvars, "name", "player", QA_CVAR_FLAG_USER_INFO, &snapshot)) qa_cvar_read_free(&snapshot);
  const int32_t flags[] = {QA_CVAR_FLAG_USER_INFO, QA_CVAR_FLAG_SERVER_INFO, QA_CVAR_FLAG_SYSTEM_INFO};
  for (int i = 0; i < 3; i++) {
    CHECK_ABORTS((void)qa_cvar_registry_register(f.cvars, "alternate", "3", flags[i], &snapshot));
    CHECK_ABORTS((void)qa_cvar_registry_bind_vm(f.cvars, "inverse", "3", flags[i]));
    CHECK_ABORTS((void)qa_cvar_registry_bind_vm(f.cvars, "alternate", "3", flags[i]));
  }
  CHECK_ABORTS(register_identity_alias(f.cvars, "nickname", "name"));
  qa_cvar_registry_set_command_flags(f.cvars, "alternate", "3", QA_CVAR_COMMAND_FLAG_USERINFO);
  CHECK(strstr(f.output, "protocol info-key") != NULL);
  char *info = qa_cvar_registry_info_string(f.cvars, QA_CVAR_FLAG_USER_INFO, -1);
  CHECK(info != NULL && strcmp(info, "\\name\\player") == 0);
  free(info);
  fixture_close(&f);
}

int main(void) {
  const qa_command_dialect all[] = {QA_COMMAND_DIALECT_Q1_NETQUAKE, QA_COMMAND_DIALECT_Q1_QUAKEWORLD,
                                    QA_COMMAND_DIALECT_Q2_CLASSIC, QA_COMMAND_DIALECT_Q2_RERELEASE,
                                    QA_COMMAND_DIALECT_Q3};
  for (int i = 0; i < 5; i++) test_shared_policy(all[i]);
  for (int i = 2; i < 5; i++) test_protection_and_latching(all[i]);
  test_vm_aliases();
  test_alias_declarations();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
