/* Port of src/world/gameplay/original-pickups.ts. GPL-2.0-or-later. */
#include "qa/world/gameplay/original_pickups.h"

#include <math.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/world/handles.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"

static noreturn void qa_error(const char *message) {
  qa_raise("Error", 0, 0, "%s", message);
}
static bool str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
static void *xcalloc(size_t size) {
  void *value = calloc(1, size != 0 ? size : 1);
  if (value == NULL) abort();
  return value;
}
static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size != 0 ? size : 1);
  if (next == NULL) abort();
  return next;
}

void qa_pickup_write_key(qa_pickup_write write, char *buffer, size_t buffer_size) {
  if (write.kind == QA_PICKUP_RESOURCE_PROTECTION) {
    (void)snprintf(buffer, buffer_size, "protection:%s",
                   write.channel == QA_PROTECTION_REGULAR ? "regular" : "powered");
  } else {
    (void)snprintf(buffer, buffer_size, "inventory:%s", write.item != NULL ? write.item : "");
  }
}

qa_captured_original_pickup_rule *qa_capture_original_pickup_rules(
    const qa_original_pickup_rule *rules, size_t rule_count, size_t *out_count) {
  qa_captured_original_pickup_rule *captured =
      calloc(rule_count != 0 ? rule_count : 1, sizeof *captured);
  if (captured == NULL) { *out_count = 0; return NULL; }
  for (size_t i = 0; i < rule_count; i++) {
    const qa_original_pickup_rule *rule = &rules[i];
    if (rule->id == NULL || rule->id[0] == '\0' || rule->offered_count == 0)
      qa_error("Original pickup rules require unique IDs and offered items");
    for (size_t j = 0; j < i; j++) {
      if (str_eq(rules[j].id, rule->id))
        qa_error("Original pickup rules require unique IDs and offered items");
    }
    for (size_t o = 0; o < rule->offered_count; o++) {
      for (size_t j = 0; j < i; j++) {
        for (size_t k = 0; k < rules[j].offered_count; k++) {
          if (str_eq(rules[j].offered[k], rule->offered[o])) {
            char message[2048];
            (void)snprintf(message, sizeof message, "Ambiguous original pickup rule for %s",
                           rule->offered[o]);
            qa_error(message);
          }
        }
      }
    }
    if (rule->writes.count == 0)
      qa_error("Original pickup requires a nonempty write set");
    for (size_t w = 0; w < rule->writes.count; w++) {
      char key_a[1024];
      qa_pickup_write_key(rule->writes.values[w], key_a, sizeof key_a);
      for (size_t x = w + 1; x < rule->writes.count; x++) {
        char key_b[1024];
        qa_pickup_write_key(rule->writes.values[x], key_b, sizeof key_b);
        if (strcmp(key_a, key_b) == 0)
          qa_error("Original pickup has duplicate resource writes");
      }
    }
    qa_item_id *offered = xcalloc(rule->offered_count * sizeof *offered);
    memcpy(offered, rule->offered, rule->offered_count * sizeof *offered);
    qa_pickup_write *writes = xcalloc(rule->writes.count * sizeof *writes);
    memcpy(writes, rule->writes.values, rule->writes.count * sizeof *writes);
    captured[i].operation = *rule;
    captured[i].id = rule->id;
    captured[i].offered = offered;
    captured[i].offered_count = rule->offered_count;
    captured[i].writes.values = writes;
    captured[i].writes.count = rule->writes.count;
    captured[i].take_ctx = rule->ctx;
    captured[i].take = rule->take;
  }
  *out_count = rule_count;
  return captured;
}
void qa_free_captured_original_pickup_rules(qa_captured_original_pickup_rule *rules,
                                            size_t rule_count) {
  if (rules == NULL) return;
  for (size_t i = 0; i < rule_count; i++) {
    free((void *)rules[i].offered);
    free((void *)rules[i].writes.values);
  }
  free(rules);
}

typedef struct pickup_scope {
  qa_owned_actor recipient;
  qa_owned_actor pickup;
  qa_original_pickup_offer request;
  qa_pickup_cargo_entry *cargo_copy;
  qa_pickup_resource resource_copy;
  bool open;
  int consumption; /* 0 live, 1 removing, 2 consumed */
  bool accepted;
  /* Owned until the scope closes (TS drops them with the scope). The
   * selection and lifetime closures reach their state through handles, so a
   * source calling them after the close sees the TS closed-scope result. */
  qa_current_original_pickup *matches;
  struct selection_state *selection;
  void *selection_token;
  struct lifetime_state *lifetime;
  void *lifetime_token;
} pickup_scope;

struct qa_shared_original_pickup_admission {
  qa_session_actor_registry *actors;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  bool (*eligible)(void *ctx, qa_original_pickup_offer offer);
  void *eligible_ctx;
  qa_actor_id *touching;
  size_t touching_count, touching_capacity;
};

qa_shared_original_pickup_admission *qa_shared_original_pickup_admission_create(
    qa_session_actor_registry *actors, qa_gameplay_authority *combat,
    qa_shared_inventory_table *inventory,
    bool (*eligible)(void *ctx, qa_original_pickup_offer offer), void *eligible_ctx) {
  qa_shared_original_pickup_admission *admission = calloc(1, sizeof *admission);
  if (admission == NULL) return NULL;
  admission->actors = actors;
  admission->combat = combat;
  admission->inventory = inventory;
  admission->eligible = eligible;
  admission->eligible_ctx = eligible_ctx;
  return admission;
}
void qa_shared_original_pickup_admission_destroy(qa_shared_original_pickup_admission *admission) {
  if (admission == NULL) return;
  free(admission->touching);
  free(admission);
}
void qa_shared_original_pickup_admission_assert_idle(qa_shared_original_pickup_admission *admission) {
  if (admission->touching_count != 0)
    qa_error("Cannot checkpoint during pickup execution");
}

static bool is_touching(qa_shared_original_pickup_admission *admission, qa_actor_id actor) {
  for (size_t i = 0; i < admission->touching_count; i++) {
    if (qa_same_actor(admission->touching[i], actor)) return true;
  }
  return false;
}
static void set_touching(qa_shared_original_pickup_admission *admission, qa_actor_id actor, bool on) {
  if (on) {
    if (admission->touching_count == admission->touching_capacity) {
      admission->touching_capacity =
          admission->touching_capacity != 0 ? admission->touching_capacity * 2 : 8;
      admission->touching = xrealloc(admission->touching,
                                     admission->touching_capacity * sizeof *admission->touching);
    }
    admission->touching[admission->touching_count++] = actor;
  } else {
    for (size_t i = 0; i < admission->touching_count; i++) {
      if (qa_same_actor(admission->touching[i], actor)) {
        memmove(&admission->touching[i], &admission->touching[i + 1],
                (admission->touching_count - i - 1) * sizeof *admission->touching);
        admission->touching_count--;
        return;
      }
    }
  }
}

static pickup_scope *open_scope(qa_shared_original_pickup_admission *admission,
                                qa_original_pickup_offer offer) {
  qa_owned_actor recipient, pickup;
  if (!qa_session_actor_registry_resolve_owned(admission->actors, offer.recipient, &recipient) ||
      !qa_session_actor_registry_resolve_owned(admission->actors, offer.pickup, &pickup) ||
      is_touching(admission, pickup.id))
    return NULL;
  if (!isfinite(offer.time.value) || (offer.count.is_override && !isfinite(offer.count.amount)))
    qa_range_error("Pickup time and count must be finite");
  if (offer.cargo != NULL) {
    bool bad = false;
    for (size_t i = 0; i < offer.cargo_count; i++) {
      if (!isfinite(offer.cargo[i].count) ||
          (str_eq(offer.cargo[i].kind, "weapon") && offer.cargo[i].count != 1)) bad = true;
      for (size_t j = i + 1; j < offer.cargo_count; j++) {
        if (str_eq(offer.cargo[i].item, offer.cargo[j].item)) bad = true;
      }
    }
    if (bad) qa_range_error("Pickup cargo has invalid counts or duplicate items");
  }
  pickup_scope *scope = xcalloc(sizeof *scope);
  scope->recipient = recipient;
  scope->pickup = pickup;
  scope->request = offer;
  if (offer.cargo != NULL && offer.cargo_count != 0) {
    scope->cargo_copy = xcalloc(offer.cargo_count * sizeof *scope->cargo_copy);
    memcpy(scope->cargo_copy, offer.cargo, offer.cargo_count * sizeof *scope->cargo_copy);
    scope->request.cargo = scope->cargo_copy;
  }
  if (offer.default_resource != NULL) {
    scope->resource_copy = *offer.default_resource;
    scope->request.default_resource = &scope->resource_copy;
  }
  scope->open = true;
  set_touching(admission, pickup.id, true);
  return scope;
}
static bool scope_current(qa_shared_original_pickup_admission *admission, pickup_scope *scope) {
  qa_owned_actor resolved;
  if (!scope->open) return false;
  if (!qa_session_actor_registry_resolve_owned(admission->actors, scope->recipient.id, &resolved) ||
      !qa_same_owned_actor(resolved, scope->recipient)) return false;
  if (scope->consumption == 2)
    return !qa_session_actor_registry_resolve_owned(admission->actors, scope->pickup.id, &resolved);
  return qa_session_actor_registry_resolve_owned(admission->actors, scope->pickup.id, &resolved) &&
         qa_same_owned_actor(resolved, scope->pickup);
}
static void close_scope(qa_shared_original_pickup_admission *admission, pickup_scope *scope) {
  scope->open = false;
  set_touching(admission, scope->pickup.id, false);
  /* TS lets the scope go here; its closures stay callable through handles. */
  qa_world_handle_close(scope->selection_token);
  qa_world_handle_close(scope->lifetime_token);
  free(scope->selection);
  free(scope->lifetime);
  free(scope->matches);
  free(scope->cargo_copy);
  free(scope);
}

static bool writes_equal(qa_pickup_writes a, qa_pickup_writes b) {
  if (a.count != b.count) return false;
  for (size_t i = 0; i < a.count; i++) {
    if (a.values[i].kind != b.values[i].kind || a.values[i].channel != b.values[i].channel ||
        !str_eq(a.values[i].item, b.values[i].item) ||
        !str_eq(a.values[i].fields, b.values[i].fields)) return false;
  }
  return true;
}
static bool same_operation(qa_original_pickup_rule a, qa_original_pickup_rule b) {
  return str_eq(a.id, b.id) && a.take == b.take && a.ctx == b.ctx;
}

typedef struct selection_state {
  qa_shared_original_pickup_admission *admission;
  pickup_scope *scope;
  qa_current_original_pickup *matches;
  size_t match_count;
  bool used;
} selection_state;
static bool selection_current(selection_state *state) {
  if (!scope_current(state->admission, state->scope)) return false;
  for (size_t i = 0; i < state->match_count; i++) {
    if (!state->matches[i].current(state->matches[i].ctx)) return false;
  }
  return true;
}
/* After its scope closed: TS current(scope) is false (scope.open). */
static bool selection_current_fn(void *ctx) {
  selection_state *state = qa_world_handle_get(ctx);
  return state != NULL && selection_current(state);
}

typedef struct grant_state {
  selection_state *selection;
  qa_pickup_writes declared;
  qa_protection_observer stores;
  void *token; /* the take() observer's closure handle */
  bool open;
  qa_original_pickup_decision result;
  bool failed; /* TS failure.value !== null */
  qa_caught_error failure;
} grant_state;
static bool grant_current(void *ctx) {
  grant_state *state = qa_world_handle_get(ctx);
  return state != NULL && state->open && selection_current(state->selection);
}
typedef struct grant_store_call { grant_state *state; qa_protection_store change; } grant_store_call;
static void grant_stored_body(void *arg) {
  grant_store_call *call = arg;
  grant_state *state = call->state;
  qa_protection_store change = call->change;
  if (!state->open) qa_error("Original pickup observer is closed");
  if (!selection_current(state->selection))
    qa_error("Original pickup resource binding is no longer current");
  bool regular_declared = false, powered_declared = false;
  for (size_t i = 0; i < state->declared.count; i++) {
    if (state->declared.values[i].kind == QA_PICKUP_RESOURCE_PROTECTION) {
      if (state->declared.values[i].channel == QA_PROTECTION_REGULAR) regular_declared = true;
      else powered_declared = true;
    }
  }
  if ((change.has_regular && !regular_declared) || (change.has_powered && !powered_declared))
    qa_error("Original pickup changed undeclared protection");
  state->stores.stored(state->stores.ctx, change);
}
/* The observer records a throw before rethrowing it; the grant rethrows it even
 * when take() catches it and returns. */
static void grant_stored(void *ctx, qa_protection_store change) {
  grant_state *state = qa_world_handle_get(ctx);
  if (state == NULL) qa_error("Original pickup observer is closed");
  grant_store_call call = {state, change};
  if (!qa_try(grant_stored_body, &call, &state->failure)) {
    state->failed = true;
    qa_rethrow(&state->failure);
  }
}
static void grant_close(void *arg) {
  grant_state *state = arg;
  state->open = false;
  qa_world_handle_close(state->token);
}
static void *grant_operation(void *ctx, qa_protection_observer stores) {
  grant_state *state = ctx;
  state->stores = stores;
  state->open = true;
  const qa_current_original_pickup *selected = &state->selection->matches[0];
  qa_original_pickup_execution execution;
  state->token = qa_world_handle_open(state);
  execution.observer.ctx = state->token;
  execution.observer.stored = grant_stored;
  execution.writes = state->declared;
  execution.current_ctx = state->token;
  execution.current = grant_current;
  qa_cleanup node; /* finally { open = false } */
  qa_cleanup_push(&node, grant_close, state);
  state->result = selected->captured.take(selected->captured.ctx,
                                          state->selection->scope->request, execution);
  if (state->failed) qa_rethrow(&state->failure);
  qa_cleanup_pop(&node);
  grant_close(state);
  return NULL;
}
static qa_original_pickup_outcome selection_grant(void *ctx) {
  selection_state *state = qa_world_handle_get(ctx);
  /* After its scope closed TS returns "stale" from current() (a grant
   * already used before the close throws in TS; C no longer has it). */
  if (state == NULL) return QA_ORIGINAL_PICKUP_OUTCOME_STALE;
  if (state->used) qa_error("Original source pickup grant already consumed");
  state->used = true;
  if (!selection_current(state)) return QA_ORIGINAL_PICKUP_OUTCOME_STALE;
  grant_state grant;
  grant.selection = state;
  grant.declared = state->matches[0].captured.writes;
  grant.open = false;
  grant.token = NULL;
  grant.failed = false;
  grant.result = QA_ORIGINAL_PICKUP_REFUSED;
  (void)qa_gameplay_authority_with_pickup_protection(
      state->admission->combat, state->scope->recipient, state->matches[0].owner,
      grant_operation, &grant);
  if (!selection_current(state)) return QA_ORIGINAL_PICKUP_OUTCOME_STALE;
  state->scope->accepted = grant.result == QA_ORIGINAL_PICKUP_ACCEPTED;
  return state->scope->accepted ? QA_ORIGINAL_PICKUP_OUTCOME_ACCEPTED
                                 : QA_ORIGINAL_PICKUP_OUTCOME_REFUSED;
}

static qa_source_pickup_selection make_selection(qa_shared_original_pickup_admission *admission,
                                                 pickup_scope *scope) {
  qa_source_pickup_selection selection;
  memset(&selection, 0, sizeof selection);
  bool allowed = admission->eligible == NULL ||
                 admission->eligible(admission->eligible_ctx, scope->request);
  if (!scope_current(admission, scope)) { selection.kind = QA_SOURCE_PICKUP_STALE; return selection; }
  if (!allowed) { selection.kind = QA_SOURCE_PICKUP_BLOCKED; return selection; }
  if (str_eq(scope->request.grant, "source-effect")) {
    if (!str_eq(scope->pickup.owner, scope->request.source))
      qa_error("Original pickup effect belongs to another source owner");
    selection.kind = QA_SOURCE_PICKUP_ORIGINAL;
    return selection;
  }
  qa_original_pickup_resolution armor =
      qa_gameplay_authority_resolve_pickup(admission->combat, scope->recipient, scope->request);
  qa_original_pickup_resolution items =
      qa_shared_inventory_table_resolve_pickup(admission->inventory, scope->recipient, scope->request);
  size_t total = armor.match_count + items.match_count;
  if (total == 0) {
    selection.kind = (armor.blocks_primary || items.blocks_primary) ? QA_SOURCE_PICKUP_BLOCKED
                                                                    : QA_SOURCE_PICKUP_ORIGINAL;
    return selection;
  }
  qa_current_original_pickup *matches = xcalloc(total * sizeof *matches);
  free(scope->matches);
  scope->matches = matches;
  if (armor.match_count != 0)
    memcpy(matches, armor.matches, armor.match_count * sizeof *matches);
  if (items.match_count != 0)
    memcpy(matches + armor.match_count, items.matches, items.match_count * sizeof *matches);
  for (size_t i = 0; i < total; i++) {
    if (!str_eq(matches[i].owner, matches[0].owner) ||
        !same_operation(matches[i].operation, matches[0].operation)) {
      char message[2048];
      (void)snprintf(message, sizeof message, "Multiple original pickup owners accept %s",
                     scope->request.item);
      qa_error(message);
    }
  }
  qa_pickup_writes declared = matches[0].captured.writes;
  /* Distinct write keys across matches must cover declared exactly. */
  char (*keys)[1024] = xcalloc(total * sizeof *keys);
  size_t key_count = 0;
  for (size_t i = 0; i < total; i++) {
    char key[1024];
    qa_pickup_write_key(matches[i].write, key, sizeof key);
    bool seen = false;
    for (size_t k = 0; k < key_count; k++) {
      if (strcmp(keys[k], key) == 0) seen = true;
    }
    if (!seen) (void)snprintf(keys[key_count++], sizeof keys[0], "%s", key);
  }
  bool incomplete = key_count != declared.count;
  for (size_t d = 0; d < declared.count && !incomplete; d++) {
    char key[1024];
    qa_pickup_write_key(declared.values[d], key, sizeof key);
    bool found = false;
    for (size_t k = 0; k < key_count; k++) {
      if (strcmp(keys[k], key) == 0) found = true;
    }
    if (!found) incomplete = true;
  }
  for (size_t i = 0; i < total && !incomplete; i++) {
    if (!writes_equal(matches[i].captured.writes, declared)) incomplete = true;
  }
  free(keys);
  if (incomplete) {
    char message[2048];
    (void)snprintf(message, sizeof message,
                   "Original pickup %s has an incomplete or changed resource write set",
                   scope->request.item);
    qa_error(message);
  }
  if (str_eq(scope->request.grant, "map-coupled")) {
    char message[2048];
    (void)snprintf(message, sizeof message,
                   "Original pickup replacement for %s requires its map lifecycle",
                   scope->request.item);
    qa_error(message);
  }
  selection_state *state = xcalloc(sizeof *state);
  state->admission = admission;
  state->scope = scope;
  state->matches = matches;
  state->match_count = total;
  scope->selection = state;
  scope->selection_token = qa_world_handle_open(state);
  selection.kind = QA_SOURCE_PICKUP_REPLACEMENT;
  selection.ctx = scope->selection_token;
  selection.current = selection_current_fn;
  selection.grant = selection_grant;
  return selection;
}

typedef struct lifetime_state {
  qa_shared_original_pickup_admission *admission;
  pickup_scope *scope; /* NULL for the stale lifetime */
  qa_source_pickup_selection selection;
} lifetime_state;
static void consume_stale(void *ctx, void (*remove)(void *remove_ctx), void *remove_ctx) {
  (void)ctx; /* TS: the stale lifetime closes over nothing */
  (void)remove;
  (void)remove_ctx;
  qa_error("Stale pickup cannot be consumed");
}
static void consume_pickup(void *ctx, void (*remove)(void *remove_ctx), void *remove_ctx) {
  lifetime_state *state = qa_world_handle_get(ctx);
  /* After the scope closed TS fails its current(scope) check. */
  if (state == NULL) qa_error("Original pickup consumption lost its source scope");
  pickup_scope *scope = state->scope;
  qa_source_pickup_selection *selected = &state->selection;
  if (!scope->accepted || scope->consumption != 0 || !scope_current(state->admission, scope) ||
      selected->kind == QA_SOURCE_PICKUP_STALE || selected->kind == QA_SOURCE_PICKUP_BLOCKED ||
      (selected->kind == QA_SOURCE_PICKUP_REPLACEMENT && !selected->current(selected->ctx)))
    qa_error("Original pickup consumption lost its source scope");
  scope->consumption = 1;
  remove(remove_ctx);
  qa_owned_actor resolved;
  if (!scope->open ||
      !qa_session_actor_registry_resolve_owned(state->admission->actors, scope->recipient.id,
                                               &resolved) ||
      !qa_same_owned_actor(resolved, scope->recipient) ||
      qa_session_actor_registry_resolve_owned(state->admission->actors, scope->pickup.id, &resolved))
    qa_error("Original pickup consumption did not retire its exact pickup");
  scope->consumption = 2;
  if (selected->kind == QA_SOURCE_PICKUP_REPLACEMENT && !selected->current(selected->ctx))
    qa_error("Original pickup consumption changed its recipient binding");
}

typedef struct scope_guard {
  qa_shared_original_pickup_admission *admission;
  pickup_scope *scope;
} scope_guard;
static void close_scope_cleanup(void *arg) {
  scope_guard *guard = arg;
  close_scope(guard->admission, guard->scope);
}

void *qa_shared_original_pickup_admission_run_source(
    qa_shared_original_pickup_admission *admission, qa_original_pickup_offer offer,
    void *(*execute)(void *ctx, qa_source_pickup_selection selection,
                     qa_source_pickup_lifetime lifetime),
    void *execute_ctx) {
  pickup_scope *scope = open_scope(admission, offer);
  if (scope == NULL) {
    qa_source_pickup_selection stale;
    memset(&stale, 0, sizeof stale);
    stale.kind = QA_SOURCE_PICKUP_STALE;
    qa_source_pickup_lifetime lifetime;
    lifetime.ctx = NULL;
    lifetime.consume_pickup = consume_stale;
    return execute(execute_ctx, stale, lifetime);
  }
  /* try { ... } catch { close(scope); throw } */
  scope_guard guard = {admission, scope};
  qa_cleanup node;
  qa_cleanup_push(&node, close_scope_cleanup, &guard);
  qa_source_pickup_selection selected = make_selection(admission, scope);
  if (selected.kind == QA_SOURCE_PICKUP_ORIGINAL) scope->accepted = true;
  lifetime_state *state = xcalloc(sizeof *state);
  state->admission = admission;
  state->scope = scope;
  state->selection = selected;
  scope->lifetime = state;
  scope->lifetime_token = qa_world_handle_open(state);
  qa_source_pickup_lifetime lifetime;
  lifetime.ctx = scope->lifetime_token;
  lifetime.consume_pickup = consume_pickup;
  void *result = execute(execute_ctx, selected, lifetime);
  qa_cleanup_pop(&node);
  close_scope(admission, scope);
  return result;
}

qa_original_pickup_outcome qa_shared_original_pickup_admission_touch(
    qa_shared_original_pickup_admission *admission, qa_original_pickup_offer offer,
    qa_original_pickup_continuation continuation) {
  pickup_scope *scope = open_scope(admission, offer);
  if (scope == NULL) return QA_ORIGINAL_PICKUP_OUTCOME_STALE;
  /* try { ... } finally { close(scope) } */
  scope_guard guard = {admission, scope};
  qa_cleanup node;
  qa_cleanup_push(&node, close_scope_cleanup, &guard);
  qa_original_pickup_outcome outcome = QA_ORIGINAL_PICKUP_OUTCOME_REFUSED;
  if (continuation.eligible != NULL && !continuation.eligible(continuation.ctx)) {
    qa_cleanup_pop(&node);
    close_scope(admission, scope);
    return QA_ORIGINAL_PICKUP_OUTCOME_REFUSED;
  }
  if (!scope_current(admission, scope)) {
    qa_cleanup_pop(&node);
    close_scope(admission, scope);
    return QA_ORIGINAL_PICKUP_OUTCOME_STALE;
  }
  qa_source_pickup_selection selected = make_selection(admission, scope);
  if (selected.kind == QA_SOURCE_PICKUP_REPLACEMENT) outcome = selected.grant(selected.ctx);
  else if (selected.kind == QA_SOURCE_PICKUP_ORIGINAL &&
           continuation.original(continuation.ctx))
    outcome = QA_ORIGINAL_PICKUP_OUTCOME_ACCEPTED;
  if (outcome == QA_ORIGINAL_PICKUP_OUTCOME_STALE || !scope_current(admission, scope) ||
      (selected.kind == QA_SOURCE_PICKUP_REPLACEMENT && !selected.current(selected.ctx))) {
    qa_cleanup_pop(&node);
    close_scope(admission, scope);
    return QA_ORIGINAL_PICKUP_OUTCOME_STALE;
  }
  continuation.complete(continuation.ctx, outcome == QA_ORIGINAL_PICKUP_OUTCOME_ACCEPTED);
  qa_cleanup_pop(&node);
  close_scope(admission, scope);
  return outcome;
}

static void *c_run_source(void *ctx, qa_original_pickup_offer offer,
                          void *(*execute)(void *execute_ctx, qa_source_pickup_selection selection,
                                           qa_source_pickup_lifetime lifetime),
                          void *execute_ctx) {
  return qa_shared_original_pickup_admission_run_source(ctx, offer, execute, execute_ctx);
}
static qa_original_pickup_outcome c_touch(void *ctx, qa_original_pickup_offer offer,
                                          qa_original_pickup_continuation continuation) {
  return qa_shared_original_pickup_admission_touch(ctx, offer, continuation);
}
qa_original_pickup_admission qa_shared_original_pickup_admission_as_contract(
    qa_shared_original_pickup_admission *admission) {
  qa_original_pickup_admission table;
  table.ctx = admission;
  table.run_source = c_run_source;
  table.touch = c_touch;
  return table;
}
