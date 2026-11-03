/*
 * Port of tests/core/cvar-transfer.test.ts against qa/core/cvars/index.h.
 * toEqual on captured states compares their checkpoint encodings; thrown
 * Errors are caught with qa_try and their messages compared.
 * GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/identity.h"
#include "qa/core/cvars/index.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/persistence/value.h"

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

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *copy = malloc(length + 1);
  memcpy(copy, text, length + 1);
  return copy;
}

typedef struct changes {
  char values[8][32];
  int count;
} changes;
static char *accept_all(void *ctx, const char *value) {
  (void)ctx;
  (void)value;
  return NULL;
}
static void record_change(void *ctx, const char *value) {
  changes *list = ctx;
  if (list->count < 8) snprintf(list->values[list->count], 32, "%s", value);
  list->count++;
}
static char *positive_required(void *ctx, const char *value) {
  (void)ctx;
  return qa_js_string_to_number(value) > 0 ? NULL : dup_text("positive required");
}

static qa_cvar_registry *registry(qa_command_dialect dialect, qa_session_id session) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context.session = session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  return qa_cvar_registry_create(&options);
}
static void reg(qa_cvar_registry *cvars, const char *name, const char *value, int32_t flags) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_register(cvars, name, value, flags, &snapshot)) qa_cvar_read_free(&snapshot);
}
static void set(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(cvars, name, value, false, &snapshot)) qa_cvar_read_free(&snapshot);
}
static void drop_effects(qa_cvar_registry *cvars) {
  size_t count = 0;
  qa_cvar_effect *effects = qa_cvar_registry_take_effects(cvars, &count);
  for (size_t i = 0; i < count; i++) qa_cvar_effect_free(&effects[i]);
  free(effects);
}
/* takeEffects().map(effect => kind === "userinfo" ? value : kind) joined by ",". */
static bool effects_are(qa_cvar_registry *cvars, const char *want) {
  size_t count = 0;
  qa_cvar_effect *effects = qa_cvar_registry_take_effects(cvars, &count);
  char joined[256] = "";
  for (size_t i = 0; i < count; i++) {
    const char *text = effects[i].kind == QA_CVAR_EFFECT_USERINFO ? effects[i].value : "other";
    size_t used = strlen(joined);
    snprintf(joined + used, sizeof joined - used, "%s%s", i > 0 ? "," : "", text);
    qa_cvar_effect_free(&effects[i]);
  }
  free(effects);
  bool same = strcmp(joined, want) == 0;
  if (!same) fprintf(stderr, "  effects %s\n", joined);
  return same;
}
static bool value_is(qa_cvar_registry *cvars, const char *name, const char *want) {
  char *value = qa_cvar_registry_variable_string(cvars, name);
  bool same = value != NULL && strcmp(value, want) == 0;
  free(value);
  return same;
}
static bool same_state(qa_save_value *a, qa_save_value *b) {
  size_t a_length = 0, b_length = 0;
  uint8_t *a_bytes = qa_encode_checkpoint_value(a, &a_length);
  uint8_t *b_bytes = qa_encode_checkpoint_value(b, &b_length);
  bool same = a_bytes != NULL && b_bytes != NULL && a_length == b_length && memcmp(a_bytes, b_bytes, a_length) == 0;
  free(a_bytes);
  free(b_bytes);
  return same;
}
static void publish_run(void *raw) {
  qa_cvar_transfer *transfer = raw;
  transfer->publish(transfer->ctx);
}
static void validate_run(void *raw) {
  qa_cvar_transfer *transfer = raw;
  transfer->validate_publication(transfer->ctx);
}
static bool raises(void (*run)(void *), qa_cvar_transfer *transfer, const char *text) {
  qa_caught_error caught;
  return !qa_try(run, transfer, &caught) && strstr(caught.message, text) != NULL;
}

typedef struct owners {
  qa_identity_owner identity;
  qa_cvar_registry *live, *candidate;
} owners;
static void owners_open(owners *o) {
  if (!qa_identity_owner_create(&o->identity, "cvar-transfer")) abort();
  o->live = registry(QA_COMMAND_DIALECT_Q1_QUAKEWORLD, o->identity.session);
  o->candidate = registry(QA_COMMAND_DIALECT_Q1_QUAKEWORLD, o->identity.session);
  reg(o->live, "name", "before", QA_CVAR_FLAG_USER_INFO);
  reg(o->candidate, "name", "before", QA_CVAR_FLAG_USER_INFO);
  drop_effects(o->live);
  drop_effects(o->candidate);
  set(o->live, "name", "old-pending");
}
static void owners_close(owners *o) {
  qa_cvar_registry_destroy(o->live);
  qa_cvar_registry_destroy(o->candidate);
  qa_identity_owner_destroy(&o->identity);
}

static void test_candidate_effects(void) {
  owners o;
  owners_open(&o);
  qa_save_value *old_state = qa_cvar_registry_capture_world_transfer_state(o.live);
  changes changed = {0};
  qa_cvar_value_binding binding = {&changed, accept_all, record_change};
  (void)qa_cvar_registry_bind_value(o.live, "name", binding);
  qa_cvar_transfer *transfer = qa_cvar_registry_prepare_transfer(o.live, o.candidate);
  set(o.candidate, "name", "candidate");
  transfer->validate_publication(transfer->ctx);
  qa_save_value *now = qa_cvar_registry_capture_world_transfer_state(o.live);
  CHECK(same_state(now, old_state));
  qa_save_value_free(now);
  CHECK(changed.count == 0);
  transfer->publish(transfer->ctx);
  CHECK(value_is(o.live, "name", "candidate"));
  CHECK(changed.count == 1 && strcmp(changed.values[0], "candidate") == 0);
  CHECK(effects_are(o.live, "old-pending,candidate"));
  CHECK(raises(publish_run, transfer, "already published"));
  transfer->destroy(transfer->ctx);
  free(transfer);
  qa_save_value_free(old_state);
  owners_close(&o);
}

static void test_stale_candidate(void) {
  owners o;
  owners_open(&o);
  qa_cvar_transfer *transfer = qa_cvar_registry_prepare_transfer(o.live, o.candidate);
  set(o.candidate, "name", "candidate");
  set(o.live, "name", "late-live");
  CHECK(raises(validate_run, transfer, "changed during preparation"));
  CHECK(raises(publish_run, transfer, "changed during preparation"));
  CHECK(value_is(o.live, "name", "late-live"));
  CHECK(effects_are(o.live, "old-pending,late-live"));
  transfer->destroy(transfer->ctx);
  free(transfer);
  owners_close(&o);
}

static char *reciprocal(void *ctx, const char *value) {
  (void)ctx;
  return dup_text(QA_JS_NUMBER(1 / qa_js_string_to_number(value)));
}
static bool reciprocal_write(void *ctx, const char *value, char **out_value, char **out_message) {
  (void)out_message;
  *out_value = reciprocal(ctx, value);
  return true;
}
static void register_gamma(qa_cvar_registry *cvars) {
  reg(cvars, "gamma", "2", 0);
  qa_cvar_alias alias;
  memset(&alias, 0, sizeof alias);
  alias.name = "brightness";
  alias.target = "gamma";
  alias.documentation.summary = "Inverse gamma";
  alias.documentation.usage = "brightness";
  alias.conversion.read = reciprocal;
  alias.conversion.write = reciprocal_write;
  qa_cvar_registry_register_alias(cvars, alias);
}
static bool vm_value_is(qa_cvar_registry *cvars, int64_t handle, const char *want) {
  qa_cvar_snapshot snapshot;
  bool found = qa_cvar_registry_read_vm(cvars, (double)handle, &snapshot);
  bool same = found && strcmp(snapshot.value, want) == 0;
  if (found) qa_cvar_read_free(&snapshot);
  return same;
}

static void test_shared_handles(void) {
  qa_identity_owner identity;
  if (!qa_identity_owner_create(&identity, "transfer-handles")) abort();
  qa_cvar_registry *live = registry(QA_COMMAND_DIALECT_Q3, identity.session);
  qa_cvar_registry *candidate = registry(QA_COMMAND_DIALECT_Q3, identity.session);
  register_gamma(live);
  register_gamma(candidate);
  int64_t retained = qa_cvar_registry_bind_vm(live, "brightness", "1", 0);
  qa_cvar_transfer *transfer = qa_cvar_registry_prepare_transfer(live, candidate);
  int64_t created = qa_cvar_registry_bind_vm(candidate, "guest-setting", "7", 0);
  set(candidate, "brightness", "0.25");
  transfer->publish(transfer->ctx);
  CHECK(vm_value_is(live, retained, "0.25"));
  CHECK(vm_value_is(live, created, "7"));
  CHECK(value_is(live, "gamma", "4"));
  set(live, "brightness", "0.5");
  CHECK(value_is(live, "gamma", "2"));
  transfer->destroy(transfer->ctx);
  free(transfer);
  qa_cvar_registry_destroy(live);
  qa_cvar_registry_destroy(candidate);
  qa_identity_owner_destroy(&identity);
}

static void test_q1_value_aba(void) {
  qa_identity_owner identity;
  if (!qa_identity_owner_create(&identity, "transfer-aba")) abort();
  qa_cvar_registry *live = registry(QA_COMMAND_DIALECT_Q1_NETQUAKE, identity.session);
  qa_cvar_registry *candidate = registry(QA_COMMAND_DIALECT_Q1_NETQUAKE, identity.session);
  reg(live, "gamma", "1", 0);
  qa_save_value *before = qa_cvar_registry_capture_world_transfer_state(live);
  qa_cvar_transfer *transfer = qa_cvar_registry_prepare_transfer(live, candidate);
  set(live, "gamma", "2");
  set(live, "gamma", "1");
  qa_save_value *after = qa_cvar_registry_capture_world_transfer_state(live);
  CHECK(same_state(after, before));
  CHECK(raises(publish_run, transfer, "changed during preparation"));
  qa_save_value_free(after);
  qa_save_value_free(before);
  transfer->destroy(transfer->ctx);
  free(transfer);
  qa_cvar_registry_destroy(live);
  qa_cvar_registry_destroy(candidate);
  qa_identity_owner_destroy(&identity);
}

static void test_candidate_clone(void) {
  qa_identity_owner identity;
  if (!qa_identity_owner_create(&identity, "candidate-clone")) abort();
  qa_cvar_registry *live = registry(QA_COMMAND_DIALECT_Q3, identity.session);
  reg(live, "level", "2", 0);
  changes changed = {0};
  qa_cvar_value_binding binding = {&changed, positive_required, record_change};
  (void)qa_cvar_registry_bind_value(live, "level", binding);
  qa_cvar_transfer *next = qa_cvar_registry_prepare_candidate(live, NULL, NULL, false);
  set(next->cvars, "level", "0");
  CHECK(value_is(next->cvars, "level", "2"));
  set(next->cvars, "level", "3");
  CHECK(value_is(live, "level", "2"));
  CHECK(changed.count == 0);
  next->publish(next->ctx);
  CHECK(value_is(live, "level", "3"));
  CHECK(changed.count == 1 && strcmp(changed.values[0], "3") == 0);
  CHECK(raises(publish_run, next, "already published"));
  next->destroy(next->ctx);
  free(next);
  qa_cvar_registry_destroy(live);
  qa_identity_owner_destroy(&identity);
}

int main(void) {
  test_candidate_effects();
  test_stale_candidate();
  test_shared_handles();
  test_q1_value_aba();
  test_candidate_clone();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
