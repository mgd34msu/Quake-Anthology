/* Port of src/compat/q2/native-mod-invocations.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_invocations.h"
#include <stdlib.h>
typedef qa_compat_q2_native_mod_invocations invocations;
typedef qa_compat_q2_native_mod_retirement retirement;
void qa_compat_q2_native_mod_invocations_init(invocations *s, const qa_compat_q2_native_mod_host *host) {
  s->host = host; s->scopes = NULL; s->scope_count = s->scope_capacity = 0; s->retired = NULL; s->retired_count = s->retired_capacity = 0;
}
void qa_compat_q2_native_mod_invocations_destroy(invocations *s) { free(s->scopes); free(s->retired); s->scopes = NULL; s->retired = NULL; s->scope_count = s->retired_count = 0; s->scope_capacity = s->retired_capacity = 0; }
static retirement *find_retirement(invocations *s, const void *token) {
  for (size_t i = 0; i < s->retired_count; i++) if (s->retired[i].authority == token) return &s->retired[i];
  return NULL;
}
static void delete_retirement(invocations *s, const void *token) {
  retirement *entry = find_retirement(s, token); if (entry == NULL) return;
  *entry = s->retired[--s->retired_count];
}
qa_compat_q2_native_mod_invocation_kind qa_compat_q2_native_mod_invocations_run(invocations *s, void *current_ctx, bool (*current)(void *), void *invoke_ctx, void (*invoke)(void *)) {
  const qa_compat_q2_runner *runner = s->host->runner;
  qa_compat_q2_native_mod_invocation_scope scope = {current_ctx, current};
  void *before = runner->capture_processor(runner->ctx);
  double depth = qa_compat_q2_native_depth(runner);
  const qa_compat_q2_native_mod_invocation_scope **slot = qa_compat_q2_grow((void **)&s->scopes, &s->scope_count, &s->scope_capacity, sizeof *s->scopes);
  *slot = &scope;
  qa_compat_q2_native_thrown thrown;
  qa_compat_q2_native_mod_invocation_kind kind = QA_Q2INV_COMPLETED;
  bool threw = !qa_compat_q2_native_try(invoke, invoke_ctx, &thrown), rethrow = false;
  if (threw) {
    retirement *entry = find_retirement(s, thrown.token);
    rethrow = entry == NULL || entry->authority->current(entry->authority->ctx);
    if (!rethrow) {
      const qa_compat_q2_native_mod_invocation_scope *owner = entry->scope;
      for (size_t i = 0; i < s->scope_count; i++) if (!s->scopes[i]->current(s->scopes[i]->ctx)) { owner = s->scopes[i]; break; }
      rethrow = owner != &scope;
    }
    if (!rethrow) {
      delete_retirement(s, thrown.token); qa_compat_q2_native_unwind(runner, depth); runner->restore_processor(runner->ctx, before);
      kind = QA_Q2INV_RETIRED;
    }
  }
  if (runner->free_processor != NULL) runner->free_processor(runner->ctx, before);
  if (s->scope_count == 0 || s->scopes[--s->scope_count] != &scope) qa_range_error("Native source invocation ownership changed");
  if (s->scope_count == 0) s->retired_count = 0;
  if (rethrow) qa_compat_q2_native_rethrow(&thrown);
  return kind;
}
typedef struct guard_state { const qa_compat_q2_native_mod_region_authority *authority; void *ctx; void (*invoke)(void *); } guard_state;
static void guard_body(void *value) {
  guard_state *state = value;
  if (!state->authority->current(state->authority->ctx)) qa_compat_q2_native_throw(state->authority, state->authority->error);
  state->invoke(state->ctx);
}
void qa_compat_q2_native_mod_invocations_guard(invocations *s, const qa_compat_q2_native_mod_region_authority *authority, void *invoke_ctx, void (*invoke)(void *)) {
  if (s->scope_count == 0) qa_range_error("Native donor has no owning source invocation");
  const qa_compat_q2_native_mod_invocation_scope *scope = s->scopes[s->scope_count - 1];
  retirement *entry = find_retirement(s, authority);
  if (entry == NULL) entry = qa_compat_q2_grow((void **)&s->retired, &s->retired_count, &s->retired_capacity, sizeof *entry);
  entry->authority = authority; entry->scope = scope;
  guard_state state = {authority, invoke_ctx, invoke};
  qa_compat_q2_native_thrown thrown;
  bool threw = !qa_compat_q2_native_try(guard_body, &state, &thrown);
  bool retiring = threw && thrown.token == authority && !authority->current(authority->ctx);
  if (!retiring) delete_retirement(s, authority);
  if (threw) qa_compat_q2_native_rethrow(&thrown);
}
