/* Port of src/world/gameplay/authority.ts. GPL-2.0-or-later. */
#include "qa/world/gameplay/authority.h"

#include <math.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/world/handles.h"

static noreturn void qa_error(const char *message) {
  qa_raise("Error", 0, 0, "%s", message);
}
static bool str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size != 0 ? size : 1);
  if (next == NULL) abort();
  return next;
}
static void *xcalloc(size_t size) {
  void *value = calloc(1, size != 0 ? size : 1);
  if (value == NULL) abort();
  return value;
}

qa_armor_state qa_copy_armor(qa_armor_state armor) { return armor; }
qa_combat_state qa_copy_combat(qa_combat_state state) {
  state.armor = qa_copy_armor(state.armor);
  return state;
}
qa_damage_request qa_capture_request(qa_damage_request request) {
  if (!isfinite(request.amount) || !isfinite(request.knockback))
    qa_range_error("Damage and knockback must be finite");
  return request;
}
static bool regular_equal(qa_regular_armor_state a, qa_regular_armor_state b) {
  if (a.kind != b.kind) return false;
  switch (a.kind) {
    case QA_REGULAR_ARMOR_NONE: return true;
    case QA_REGULAR_ARMOR_Q1:
      return a.points == b.points && a.absorption == b.absorption && str_eq(a.item, b.item);
    case QA_REGULAR_ARMOR_Q2:
      return a.points == b.points && a.normal_protection == b.normal_protection &&
             a.energy_protection == b.energy_protection && str_eq(a.item, b.item);
    case QA_REGULAR_ARMOR_Q3:
      return a.points == b.points && a.protection == b.protection;
    case QA_REGULAR_ARMOR_SOURCE:
      return a.points == b.points && str_eq(a.item, b.item);
  }
  return false;
}
static bool powered_equal(qa_powered_protection_state a, qa_powered_protection_state b) {
  return str_eq(a.kind, b.kind) && (str_eq(a.kind, "none") || a.cells == b.cells);
}
static bool armor_equal(qa_armor_state a, qa_armor_state b) {
  return regular_equal(a.regular, b.regular) && powered_equal(a.powered, b.powered);
}
qa_armor_state qa_normalize_legacy_power_only_armor(qa_armor_state legacy,
                                                    qa_armor_state current,
                                                    qa_item_id placeholder) {
  qa_regular_armor_state regular = legacy.regular;
  if (current.regular.kind == QA_REGULAR_ARMOR_NONE && regular.kind == QA_REGULAR_ARMOR_Q2 &&
      str_eq(regular.item, placeholder) && regular.points == 0 &&
      regular.normal_protection == 0 && regular.energy_protection == 0 &&
      str_eq(legacy.powered.kind, current.powered.kind) &&
      !str_eq(legacy.powered.kind, "none") && legacy.powered.cells == current.powered.cells) {
    legacy.regular.kind = QA_REGULAR_ARMOR_NONE;
    return legacy;
  }
  return legacy;
}
static bool actor_ptr_eq(const qa_actor_id *a, const qa_actor_id *b) {
  if ((a == NULL) != (b == NULL)) return false;
  return a == NULL || qa_same_actor(*a, *b);
}
static bool request_equal(qa_damage_request a, qa_damage_request b) {
  return qa_same_actor(a.target, b.target) && a.amount == b.amount &&
         a.knockback == b.knockback && a.attack.sequence == b.attack.sequence &&
         actor_ptr_eq(a.attack.attacker, b.attack.attacker) &&
         actor_ptr_eq(a.attack.inflictor, b.attack.inflictor) &&
         str_eq(a.attack.combat_provider, b.attack.combat_provider) &&
         str_eq(a.delivery, b.delivery);
}

/* --- internal storage --- */
typedef struct binding_entry {
  qa_owned_actor actor;
  qa_combat_state_binding binding;
  bool copied;
  /* TS binding object identity: rebind and bindDamageAdjustment/Admission
   * replace the binding object; C updates the entry in place and bumps this. */
  uint64_t version;
  /* TS bindDamageAdjustment/Admission copy the binding with a new closure:
   * that closure's context is separate from binding.ctx. */
  bool has_adjust_ctx, has_admit_ctx;
  void *adjust_ctx, *admit_ctx;
} binding_entry;
typedef struct cell_entry {
  qa_owned_actor actor;
  qa_power_armor_cell_binding binding;
} cell_entry;
typedef struct bound_protection {
  qa_protection_binding binding;
  qa_captured_original_pickup_rule *pickups;
  size_t pickup_count;
} bound_protection;
typedef struct protection_slot {
  qa_protection_claim claim;
  bound_protection *binding;
  qa_armor_stage_removal removal;
  bool has_removal;
} protection_slot;
typedef struct protection_entry {
  qa_owned_actor actor;
  protection_slot *regular;
  protection_slot *powered;
} protection_entry;
typedef struct policy_entry {
  int64_t id;
  qa_combat_policy policy;
} policy_entry;
typedef struct cursor cursor;
struct cursor {
  qa_damage_request request;
  qa_actor_id attacker_storage, inflictor_storage;
  qa_damage_mutation *mutations;
  size_t mutation_count, mutation_capacity;
  double health;
  qa_armor_state armor;
  bool has_velocity;
  qa_vec3 velocity;
  bool active;
  bool has_reaction;
  qa_source_damage_result reaction;
};
typedef struct cursor_entry {
  qa_owned_actor actor;
  cursor *value;
} cursor_entry;

struct qa_gameplay_authority {
  qa_session_actor_registry *actors;
  qa_actor_callback_table *callbacks;
  qa_gameplay_hooks hooks;
  qa_mod_operation damage_op;
  binding_entry **bindings;
  size_t binding_count, binding_capacity;
  cell_entry *cells;
  size_t cell_count, cell_capacity;
  protection_entry **protections;
  size_t protection_count, protection_capacity;
  policy_entry *policies;
  size_t policy_count, policy_capacity;
  int64_t next_policy_id;
  uint64_t next_binding_version;
  cursor_entry *cursors;
  size_t cursor_count, cursor_capacity;
  int active_hits;
  int active_pickups;
  /* TS dispatchedDamage (WeakSet of captured requests in a damage dispatch):
   * C requests are values, so membership is value equality. */
  qa_damage_request *dispatched;
  size_t dispatched_count, dispatched_capacity;
  int64_t release_subscription;
  void **retained; /* outcome mutation arrays + closure states */
  size_t retained_count, retained_capacity;
  qa_current_original_pickup *scratch_matches;
  size_t scratch_matches_capacity;
  qa_item_id *scratch_items;
  size_t scratch_items_capacity;
};

static void retain(qa_gameplay_authority *authority, void *ptr) {
  if (authority->retained_count == authority->retained_capacity) {
    authority->retained_capacity =
        authority->retained_capacity != 0 ? authority->retained_capacity * 2 : 32;
    authority->retained = xrealloc(authority->retained,
                                   authority->retained_capacity * sizeof *authority->retained);
  }
  authority->retained[authority->retained_count++] = ptr;
}

static binding_entry *find_binding_entry(qa_gameplay_authority *authority, qa_owned_actor actor) {
  for (size_t i = 0; i < authority->binding_count; i++) {
    if (qa_same_owned_actor(authority->bindings[i]->actor, actor)) return authority->bindings[i];
  }
  return NULL;
}
static cell_entry *find_cells(qa_gameplay_authority *authority, qa_owned_actor actor) {
  for (size_t i = 0; i < authority->cell_count; i++) {
    if (qa_same_owned_actor(authority->cells[i].actor, actor)) return &authority->cells[i];
  }
  return NULL;
}
static protection_entry *find_protection(qa_gameplay_authority *authority, qa_owned_actor actor) {
  for (size_t i = 0; i < authority->protection_count; i++) {
    if (qa_same_owned_actor(authority->protections[i]->actor, actor))
      return authority->protections[i];
  }
  return NULL;
}
static protection_slot *slot_of(protection_entry *entry, qa_protection_channel channel) {
  if (entry == NULL) return NULL;
  return channel == QA_PROTECTION_REGULAR ? entry->regular : entry->powered;
}
static bound_protection *protection_binding_of(qa_gameplay_authority *authority,
                                               qa_owned_actor actor,
                                               qa_protection_channel channel) {
  protection_slot *slot = slot_of(find_protection(authority, actor), channel);
  return slot != NULL ? slot->binding : NULL;
}
static binding_entry *require_binding(qa_gameplay_authority *authority, qa_owned_actor actor) {
  qa_session_actor_registry_assert_owned(authority->actors, actor);
  binding_entry *entry = find_binding_entry(authority, actor);
  if (entry == NULL) qa_error("Actor has no combat binding");
  return entry;
}

static void free_bound_protection(bound_protection *binding) {
  if (binding == NULL) return;
  qa_free_captured_original_pickup_rules(binding->pickups, binding->pickup_count);
  free(binding);
}
static void run_removal(void *arg) {
  qa_armor_stage_removal *removal = arg;
  removal->remove(removal->ctx);
}
static void on_release(void *ctx, qa_owned_actor actor) {
  qa_gameplay_authority *authority = ctx;
  protection_entry *protection = find_protection(authority, actor);
  qa_armor_stage_removal removals[2];
  bool has_removals[2] = {false, false};
  if (protection != NULL) {
    protection_slot *slots[2] = {protection->regular, protection->powered};
    for (int i = 0; i < 2; i++) {
      if (slots[i] != NULL && slots[i]->has_removal && slots[i]->removal.remove != NULL) {
        removals[i] = slots[i]->removal;
        has_removals[i] = true;
      }
    }
    for (size_t i = 0; i < authority->protection_count; i++) {
      if (authority->protections[i] == protection) {
        memmove(&authority->protections[i], &authority->protections[i + 1],
                (authority->protection_count - i - 1) * sizeof *authority->protections);
        authority->protection_count--;
        break;
      }
    }
  }
  /* Each removal runs in its own try; failures become one AggregateError
   * after the bindings and cells are dropped. */
  size_t errors = 0;
  for (int i = 0; i < 2; i++) {
    qa_caught_error caught;
    if (has_removals[i] && !qa_try(run_removal, &removals[i], &caught)) errors++;
  }
  if (protection != NULL) {
    free_bound_protection(protection->regular != NULL ? protection->regular->binding : NULL);
    free_bound_protection(protection->powered != NULL ? protection->powered->binding : NULL);
    free(protection->regular);
    free(protection->powered);
    free(protection);
  }
  for (size_t i = 0; i < authority->binding_count; i++) {
    if (qa_same_owned_actor(authority->bindings[i]->actor, actor)) {
      free(authority->bindings[i]);
      memmove(&authority->bindings[i], &authority->bindings[i + 1],
              (authority->binding_count - i - 1) * sizeof *authority->bindings);
      authority->binding_count--;
      break;
    }
  }
  for (size_t i = 0; i < authority->cell_count; i++) {
    if (qa_same_owned_actor(authority->cells[i].actor, actor)) {
      memmove(&authority->cells[i], &authority->cells[i + 1],
              (authority->cell_count - i - 1) * sizeof *authority->cells);
      authority->cell_count--;
      break;
    }
  }
  if (errors != 0) qa_raise("AggregateError", 0, 0, "Failed to detach actor protection");
}

qa_gameplay_authority *qa_gameplay_authority_create(qa_session_actor_registry *actors,
                                                    qa_actor_callback_table *callbacks,
                                                    qa_gameplay_hooks hooks) {
  qa_gameplay_authority *authority = calloc(1, sizeof *authority);
  if (authority == NULL) return NULL;
  authority->actors = actors;
  authority->callbacks = callbacks;
  authority->hooks = hooks;
  (void)qa_mod_operation_init(&authority->damage_op, "damage", sizeof(qa_damage_request),
                              sizeof(qa_damage_outcome));
  authority->release_subscription =
      qa_session_actor_registry_on_release(actors, on_release, authority);
  return authority;
}
void qa_gameplay_authority_destroy(qa_gameplay_authority *authority) {
  if (authority == NULL) return;
  qa_session_actor_registry_off_release(authority->actors, authority->release_subscription);
  for (size_t i = 0; i < authority->binding_count; i++) free(authority->bindings[i]);
  free(authority->bindings);
  free(authority->cells);
  for (size_t i = 0; i < authority->protection_count; i++) {
    protection_entry *entry = authority->protections[i];
    free_bound_protection(entry->regular != NULL ? entry->regular->binding : NULL);
    free_bound_protection(entry->powered != NULL ? entry->powered->binding : NULL);
    free(entry->regular);
    free(entry->powered);
    free(entry);
  }
  free(authority->protections);
  free(authority->policies);
  for (size_t i = 0; i < authority->cursor_count; i++) {
    free(authority->cursors[i].value->mutations);
    free(authority->cursors[i].value);
  }
  free(authority->cursors);
  free(authority->dispatched);
  for (size_t i = 0; i < authority->retained_count; i++) free(authority->retained[i]);
  free(authority->retained);
  qa_mod_operation_destroy(&authority->damage_op);
  free(authority->scratch_matches);
  free(authority->scratch_items);
  free(authority);
}
qa_mod_operation *qa_gameplay_authority_damage_operation(qa_gameplay_authority *authority) {
  return &authority->damage_op;
}

int64_t qa_gameplay_authority_register(qa_gameplay_authority *authority, qa_combat_policy policy) {
  for (size_t i = 0; i < authority->policy_count; i++) {
    if (str_eq(authority->policies[i].policy.id, policy.id)) {
      char message[1024];
      (void)snprintf(message, sizeof message, "Combat policy already registered: %s", policy.id);
      qa_error(message);
    }
  }
  if (authority->policy_count == authority->policy_capacity) {
    authority->policy_capacity = authority->policy_capacity != 0 ? authority->policy_capacity * 2 : 8;
    authority->policies = xrealloc(authority->policies,
                                   authority->policy_capacity * sizeof *authority->policies);
  }
  int64_t id = ++authority->next_policy_id;
  authority->policies[authority->policy_count].id = id;
  authority->policies[authority->policy_count].policy = policy;
  authority->policy_count++;
  return id;
}
void qa_gameplay_authority_unregister(qa_gameplay_authority *authority, int64_t registration) {
  for (size_t i = 0; i < authority->policy_count; i++) {
    if (authority->policies[i].id == registration) {
      memmove(&authority->policies[i], &authority->policies[i + 1],
              (authority->policy_count - i - 1) * sizeof *authority->policies);
      authority->policy_count--;
      return;
    }
  }
}

static binding_entry *add_binding(qa_gameplay_authority *authority, qa_owned_actor actor,
                                  qa_combat_state_binding binding, bool copied) {
  binding_entry *entry = xcalloc(sizeof *entry);
  entry->actor = actor;
  entry->binding = binding;
  entry->copied = copied;
  entry->version = ++authority->next_binding_version;
  if (authority->binding_count == authority->binding_capacity) {
    authority->binding_capacity =
        authority->binding_capacity != 0 ? authority->binding_capacity * 2 : 8;
    authority->bindings = xrealloc(authority->bindings,
                                   authority->binding_capacity * sizeof *authority->bindings);
  }
  authority->bindings[authority->binding_count++] = entry;
  return entry;
}
void qa_gameplay_authority_bind(qa_gameplay_authority *authority, qa_owned_actor actor,
                                qa_combat_state_binding binding) {
  qa_session_actor_registry_assert_owned(authority->actors, actor);
  if (find_binding_entry(authority, actor) != NULL)
    qa_error("Actor already has a combat binding");
  (void)add_binding(authority, actor, binding, false);
}
void qa_gameplay_authority_rebind(qa_gameplay_authority *authority, qa_owned_actor actor,
                                  qa_combat_state_binding binding) {
  qa_session_actor_registry_assert_owned(authority->actors, actor);
  if (find_protection(authority, actor) != NULL)
    qa_error("Cannot replace a combat binding with a reserved protection owner");
  binding_entry *entry = find_binding_entry(authority, actor);
  if (entry != NULL) {
    entry->binding = binding;
    /* A new binding object: not one create() copied. */
    entry->copied = false;
    entry->version = ++authority->next_binding_version;
    entry->has_adjust_ctx = entry->has_admit_ctx = false;
    entry->adjust_ctx = entry->admit_ctx = NULL;
  } else (void)add_binding(authority, actor, binding, false);
}
void qa_gameplay_authority_bind_damage_adjustment(
    qa_gameplay_authority *authority, qa_owned_actor actor,
    qa_damage_adjustment_result (*adjust_damage)(void *ctx, qa_damage_request request),
    void *adjust_ctx) {
  binding_entry *entry = require_binding(authority, actor);
  if (entry->binding.adjust_damage != NULL)
    qa_error("Actor already has source damage adjustment");
  entry->binding.adjust_damage = adjust_damage;
  entry->version = ++authority->next_binding_version;
  entry->has_adjust_ctx = true;
  entry->adjust_ctx = adjust_ctx;
}
void qa_gameplay_authority_bind_damage_admission(
    qa_gameplay_authority *authority, qa_owned_actor actor,
    const char *(*admit_damage)(void *ctx, qa_damage_request request), void *admit_ctx) {
  binding_entry *entry = require_binding(authority, actor);
  if (entry->binding.admit_damage != NULL)
    qa_error("Actor already has source damage admission");
  entry->binding.admit_damage = admit_damage;
  entry->version = ++authority->next_binding_version;
  entry->has_admit_ctx = true;
  entry->admit_ctx = admit_ctx;
}
void qa_gameplay_authority_bind_power_armor_cells(qa_gameplay_authority *authority,
                                                  qa_owned_actor actor,
                                                  qa_power_armor_cell_binding cells) {
  qa_session_actor_registry_assert_owned(authority->actors, actor);
  if (find_cells(authority, actor) != NULL)
    qa_error("Power armor cells already have an inventory binding");
  protection_entry *protection = find_protection(authority, actor);
  if (protection != NULL && protection->powered != NULL)
    qa_error("Cannot replace primary power ownership after a component reserved it");
  if (authority->cell_count == authority->cell_capacity) {
    authority->cell_capacity = authority->cell_capacity != 0 ? authority->cell_capacity * 2 : 8;
    authority->cells = xrealloc(authority->cells, authority->cell_capacity * sizeof *authority->cells);
  }
  authority->cells[authority->cell_count].actor = actor;
  authority->cells[authority->cell_count].binding = cells;
  authority->cell_count++;
}
static qa_combat_state read_state(qa_gameplay_authority *authority, qa_owned_actor actor,
                                  binding_entry *entry) {
  qa_combat_state state = entry->binding.read(entry->binding.ctx);
  bound_protection *regular = protection_binding_of(authority, actor, QA_PROTECTION_REGULAR);
  bound_protection *power = protection_binding_of(authority, actor, QA_PROTECTION_POWERED);
  cell_entry *cells = find_cells(authority, actor);
  qa_powered_protection_state powered;
  if (power != NULL) powered = power->binding.read_powered(power->binding.ctx);
  else if (cells == NULL || str_eq(state.armor.powered.kind, "none")) powered = state.armor.powered;
  else {
    powered = state.armor.powered;
    powered.cells = cells->binding.read(cells->binding.ctx);
  }
  qa_combat_state result = state;
  if (authority->hooks.team != NULL)
    result.team = authority->hooks.team(authority->hooks.ctx, actor.id, state.team);
  result.armor.regular =
      regular != NULL ? regular->binding.read_regular(regular->binding.ctx) : state.armor.regular;
  result.armor.powered = powered;
  return qa_copy_combat(result);
}

static cursor *current_cursor(qa_gameplay_authority *authority, qa_owned_actor actor) {
  cursor *current = NULL;
  for (size_t i = 0; i < authority->cursor_count; i++) {
    if (qa_same_owned_actor(authority->cursors[i].actor, actor))
      current = authority->cursors[i].value;
  }
  return current;
}
static void advance_cursors(qa_gameplay_authority *authority, qa_owned_actor actor,
                            qa_damage_mutation mutation) {
  for (size_t i = 0; i < authority->cursor_count; i++) {
    if (!qa_same_owned_actor(authority->cursors[i].actor, actor)) continue;
    cursor *value = authority->cursors[i].value;
    switch (mutation.kind) {
      case QA_DAMAGE_MUTATION_HEALTH: value->health = mutation.health_after; break;
      case QA_DAMAGE_MUTATION_ARMOR: value->armor = qa_copy_armor(mutation.armor_after); break;
      case QA_DAMAGE_MUTATION_SOURCE_VELOCITY:
        value->velocity = mutation.velocity_after;
        value->has_velocity = true;
        break;
      case QA_DAMAGE_MUTATION_IMPULSE: value->has_velocity = false; break;
    }
  }
}
static void assert_cursor(qa_gameplay_authority *authority, qa_owned_actor actor, cursor *value) {
  if (!value->active) qa_error("Source damage observer is closed");
  qa_session_actor_registry_assert_owned(authority->actors, actor);
}
static void cursor_push(cursor *value, qa_damage_mutation mutation) {
  if (value->mutation_count == value->mutation_capacity) {
    value->mutation_capacity = value->mutation_capacity != 0 ? value->mutation_capacity * 2 : 8;
    value->mutations = xrealloc(value->mutations,
                                value->mutation_capacity * sizeof *value->mutations);
  }
  value->mutations[value->mutation_count++] = mutation;
}
static void observe_store(qa_gameplay_authority *authority, qa_owned_actor actor,
                          binding_entry *entry, cursor *value, qa_damage_mutation write) {
  assert_cursor(authority, actor, value);
  if (value->has_reaction) qa_error("Source damage store follows its reaction boundary");
  switch (write.kind) {
    case QA_DAMAGE_MUTATION_HEALTH:
      if (write.health_before != value->health || !isfinite(write.health_after) ||
          entry->binding.read(entry->binding.ctx).health != write.health_after)
        qa_error("Invalid observed source health store");
      break;
    case QA_DAMAGE_MUTATION_ARMOR:
      if (!armor_equal(write.armor_before, value->armor) ||
          !armor_equal(read_state(authority, actor, entry).armor, write.armor_after))
        qa_error("Invalid observed source armor store");
      if (armor_equal(write.armor_before, write.armor_after)) return;
      break;
    case QA_DAMAGE_MUTATION_SOURCE_VELOCITY: {
      double parts[6] = {write.velocity_before.x, write.velocity_before.y, write.velocity_before.z,
                         write.velocity_after.x, write.velocity_after.y, write.velocity_after.z};
      bool finite = true;
      for (int i = 0; i < 6; i++) finite = finite && isfinite(parts[i]);
      if (!finite || (value->has_velocity &&
                      (value->velocity.x != write.velocity_before.x ||
                       value->velocity.y != write.velocity_before.y ||
                       value->velocity.z != write.velocity_before.z)))
        qa_error("Invalid observed source velocity store");
      break;
    }
    case QA_DAMAGE_MUTATION_IMPULSE: break;
  }
  advance_cursors(authority, actor, write);
  cursor_push(value, write);
}
static void observe_public_write(qa_gameplay_authority *authority, qa_owned_actor actor,
                                 binding_entry *entry, bool health_kind) {
  cursor *value = current_cursor(authority, actor);
  if (value == NULL || !qa_session_actor_registry_is_live(authority->actors, actor.id)) return;
  qa_damage_mutation write;
  memset(&write, 0, sizeof write);
  if (health_kind) {
    write.kind = QA_DAMAGE_MUTATION_HEALTH;
    write.health_before = value->health;
    write.health_after = entry->binding.read(entry->binding.ctx).health;
    if (write.health_before == write.health_after) return;
  } else {
    write.kind = QA_DAMAGE_MUTATION_ARMOR;
    write.armor_before = value->armor;
    write.armor_after = read_state(authority, actor, entry).armor;
    if (armor_equal(write.armor_before, write.armor_after)) return;
  }
  if (!value->has_reaction) {
    observe_store(authority, actor, entry, value, write);
    return;
  }
  advance_cursors(authority, actor, write);
}
static cursor *open_cursor(qa_gameplay_authority *authority, qa_owned_actor actor,
                           qa_damage_request request, qa_combat_state initial) {
  if (current_cursor(authority, actor) != NULL) {
    binding_entry *entry = require_binding(authority, actor);
    observe_public_write(authority, actor, entry, true);
    observe_public_write(authority, actor, entry, false);
  }
  cursor *value = xcalloc(sizeof *value);
  value->request = request;
  if (request.attack.attacker != NULL) {
    value->attacker_storage = *request.attack.attacker;
    value->request.attack.attacker = &value->attacker_storage;
  }
  if (request.attack.inflictor != NULL) {
    value->inflictor_storage = *request.attack.inflictor;
    value->request.attack.inflictor = &value->inflictor_storage;
  }
  value->health = initial.health;
  value->armor = initial.armor;
  value->active = true;
  if (authority->cursor_count == authority->cursor_capacity) {
    authority->cursor_capacity =
        authority->cursor_capacity != 0 ? authority->cursor_capacity * 2 : 8;
    authority->cursors = xrealloc(authority->cursors,
                                  authority->cursor_capacity * sizeof *authority->cursors);
  }
  authority->cursors[authority->cursor_count].actor = actor;
  authority->cursors[authority->cursor_count].value = value;
  authority->cursor_count++;
  return value;
}
static void close_cursor(qa_gameplay_authority *authority, qa_owned_actor actor, cursor *value) {
  value->active = false;
  for (size_t i = 0; i < authority->cursor_count; i++) {
    if (authority->cursors[i].value == value &&
        qa_same_owned_actor(authority->cursors[i].actor, actor)) {
      memmove(&authority->cursors[i], &authority->cursors[i + 1],
              (authority->cursor_count - i - 1) * sizeof *authority->cursors);
      authority->cursor_count--;
      break;
    }
  }
  /* Mutations may be referenced by a returned decision: retain them. */
  retain(authority, value->mutations);
  retain(authority, value);
}
/* try { ... } finally { closeCursor(target, cursor) } */
typedef struct cursor_scope {
  qa_gameplay_authority *authority;
  qa_owned_actor actor;
  cursor *value;
} cursor_scope;
static void cursor_scope_release(void *arg) {
  cursor_scope *scope = arg;
  close_cursor(scope->authority, scope->actor, scope->value);
}
/* try { ... } finally { activeHits-- } */
static void active_hits_release(void *arg) { ((qa_gameplay_authority *)arg)->active_hits--; }
static bool is_dispatched(const qa_gameplay_authority *authority, qa_damage_request request) {
  for (size_t i = 0; i < authority->dispatched_count; i++) {
    if (request_equal(authority->dispatched[i], request)) return true;
  }
  return false;
}
static void dispatched_pop(void *arg) { ((qa_gameplay_authority *)arg)->dispatched_count--; }
static cursor *cursor_for(qa_gameplay_authority *authority, qa_owned_actor actor,
                          qa_damage_request request) {
  cursor *found = NULL;
  for (size_t i = 0; i < authority->cursor_count; i++) {
    if (qa_same_owned_actor(authority->cursors[i].actor, actor) &&
        authority->cursors[i].value->active &&
        request_equal(authority->cursors[i].value->request, request))
      found = authority->cursors[i].value;
  }
  if (found == NULL) qa_error("Original armor stage has no active damage observation");
  return found;
}

static qa_damage_decision build_decision(qa_gameplay_authority *authority,
                                         qa_damage_request request,
                                         const qa_damage_mutation *mutations, size_t count,
                                         qa_combat_result result) {
  qa_damage_mutation *copy = xcalloc((count != 0 ? count : 1) * sizeof *copy);
  if (count != 0) memcpy(copy, mutations, count * sizeof *copy);
  retain(authority, copy);
  qa_damage_decision decision;
  memset(&decision, 0, sizeof decision);
  decision.request = request;
  decision.mutations = copy;
  decision.mutation_count = count;
  decision.applied_damage = result.applied_damage;
  decision.reaction = result.reaction;
  decision.has_feedback = result.has_feedback;
  decision.feedback_kind = result.feedback_kind;
  decision.feedback_power_armor = result.feedback_power_armor;
  decision.feedback_armor = result.feedback_armor;
  decision.feedback_blood = result.feedback_blood;
  decision.feedback_knockback = result.feedback_knockback;
  decision.feedback_battlesuit = result.feedback_battlesuit;
  return decision;
}
static qa_combat_result none_result(void) {
  qa_combat_result result;
  memset(&result, 0, sizeof result);
  result.reaction = "none";
  return result;
}

static void write_armor_internal(qa_gameplay_authority *authority, qa_owned_actor actor,
                                 binding_entry *entry, qa_armor_state armor) {
  qa_combat_state_binding *binding = &entry->binding;
  bound_protection *regular = protection_binding_of(authority, actor, QA_PROTECTION_REGULAR);
  bound_protection *power = protection_binding_of(authority, actor, QA_PROTECTION_POWERED);
  uint64_t version = entry->version;
  qa_armor_state primary = qa_copy_armor(binding->read(binding->ctx).armor);
  qa_armor_state effective = read_state(authority, actor, entry).armor;
  bool writes_regular = !regular_equal(effective.regular, armor.regular);
  bool writes_power = !powered_equal(effective.powered, armor.powered);
  qa_armor_state original;
  original.regular = regular == NULL ? armor.regular : primary.regular;
  original.powered = power == NULL ? armor.powered : primary.powered;
  if (entry->copied && original.regular.kind == QA_REGULAR_ARMOR_SOURCE)
    qa_error("Copied primary armor cannot own a source formula");
  if (binding->validate_armor != NULL) binding->validate_armor(binding->ctx, original);
  if (regular != NULL) regular->binding.validate_write_regular(regular->binding.ctx, armor.regular);
  if (power != NULL) power->binding.validate_write_powered(power->binding.ctx, armor.powered);
  /* ownership() checks after each component write. */
  if (regular != NULL && writes_regular) {
    regular->binding.write_regular(regular->binding.ctx, armor.regular);
    qa_session_actor_registry_assert_owned(authority->actors, actor);
    if (find_binding_entry(authority, actor) != entry || entry->version != version ||
        protection_binding_of(authority, actor, QA_PROTECTION_REGULAR) != regular ||
        protection_binding_of(authority, actor, QA_PROTECTION_POWERED) != power)
      qa_error("Armor ownership changed during a protection write");
  }
  if (writes_power && !powered_equal(read_state(authority, actor, entry).armor.powered,
                                     effective.powered))
    qa_error("Powered armor changed during a regular write");
  if (power != NULL && writes_power) {
    power->binding.write_powered(power->binding.ctx, armor.powered);
    qa_session_actor_registry_assert_owned(authority->actors, actor);
    if (find_binding_entry(authority, actor) != entry || entry->version != version ||
        protection_binding_of(authority, actor, QA_PROTECTION_REGULAR) != regular ||
        protection_binding_of(authority, actor, QA_PROTECTION_POWERED) != power)
      qa_error("Armor ownership changed during a protection write");
  }
  if (power == NULL && writes_power) {
    cell_entry *cells = find_cells(authority, actor);
    if (cells != NULL && !str_eq(armor.powered.kind, "none") &&
        cells->binding.read(cells->binding.ctx) != armor.powered.cells) {
      cells->binding.write(cells->binding.ctx, armor.powered.cells);
      qa_session_actor_registry_assert_owned(authority->actors, actor);
      if (find_binding_entry(authority, actor) != entry || entry->version != version ||
          protection_binding_of(authority, actor, QA_PROTECTION_REGULAR) != regular ||
          protection_binding_of(authority, actor, QA_PROTECTION_POWERED) != power)
        qa_error("Armor ownership changed during a protection write");
    }
  }
  qa_session_actor_registry_assert_owned(authority->actors, actor);
  if (find_binding_entry(authority, actor) != entry || entry->version != version ||
      protection_binding_of(authority, actor, QA_PROTECTION_REGULAR) != regular ||
      protection_binding_of(authority, actor, QA_PROTECTION_POWERED) != power)
    qa_error("Armor ownership changed during a protection write");
  qa_armor_state latest = binding->read(binding->ctx).armor;
  if (regular == NULL && writes_regular && !regular_equal(primary.regular, latest.regular))
    qa_error("Primary regular armor changed during a protection write");
  qa_armor_state updated;
  updated.regular = regular == NULL && writes_regular ? armor.regular : latest.regular;
  updated.powered = power == NULL && writes_power ? armor.powered : latest.powered;
  if (binding->validate_armor != NULL) binding->validate_armor(binding->ctx, updated);
  if (!armor_equal(latest, updated)) binding->write_armor(binding->ctx, qa_copy_armor(updated));
}

typedef struct absorb_observer_state {
  qa_gameplay_authority *authority;
  qa_owned_actor actor;
  binding_entry *entry;
  cursor *value;
  bound_protection *protection;
  bool open;
} absorb_observer_state;
/* The observer's ctx is a closure handle: a component that keeps the
 * observer and calls it after its stage gets the TS "closed" throw. */
static void close_observer_handle(void *token) { qa_world_handle_close(token); }
static void absorb_stored(void *ctx, qa_protection_store change) {
  absorb_observer_state *state = qa_world_handle_get(ctx);
  if (state == NULL || !state->open) qa_error("Armor observer is closed");
  if (protection_binding_of(state->authority, state->actor, state->protection->binding.channel) !=
      state->protection)
    qa_error("Armor owner was removed");
  bound_protection *regular_owner =
      protection_binding_of(state->authority, state->actor, QA_PROTECTION_REGULAR);
  bound_protection *powered_owner =
      protection_binding_of(state->authority, state->actor, QA_PROTECTION_POWERED);
  if ((change.has_regular &&
       (regular_owner == NULL || !str_eq(regular_owner->binding.claim.owner,
                                         state->protection->binding.claim.owner))) ||
      (change.has_powered &&
       (powered_owner == NULL || !str_eq(powered_owner->binding.claim.owner,
                                         state->protection->binding.claim.owner))))
    qa_error("Component reported protection owned by another provider");
  qa_damage_mutation write;
  memset(&write, 0, sizeof write);
  write.kind = QA_DAMAGE_MUTATION_ARMOR;
  write.armor_before.regular = change.has_regular ? change.regular_before : state->value->armor.regular;
  write.armor_before.powered = change.has_powered ? change.powered_before : state->value->armor.powered;
  write.armor_after.regular = change.has_regular ? change.regular_after : state->value->armor.regular;
  write.armor_after.powered = change.has_powered ? change.powered_after : state->value->armor.powered;
  observe_store(state->authority, state->actor, state->entry, state->value, write);
}
static qa_armor_stage_result absorb_protection(qa_gameplay_authority *authority,
                                               qa_owned_actor actor, binding_entry *entry,
                                               cursor *value, qa_armor_stage_input input,
                                               bound_protection *protection) {
  assert_cursor(authority, actor, value);
  if (!request_equal(input.request, value->request) || !isfinite(input.amount))
    qa_error("Invalid armor stage input");
  double geo[9] = {input.direction.x, input.direction.y, input.direction.z,
                   input.point.x, input.point.y, input.point.z,
                   input.normal.x, input.normal.y, input.normal.z};
  for (int i = 0; i < 9; i++) {
    if (!isfinite(geo[i])) qa_error("Armor stage geometry must be finite");
  }
  qa_armor_stage_result zero = {0};
  if (input.amount <= 0 || input.flags.no_armor ||
      (protection->binding.channel == QA_PROTECTION_POWERED ? input.flags.no_power_armor
                                                            : input.flags.no_regular_armor))
    return zero;
  absorb_observer_state state = {authority, actor, entry, value, protection, true};
  qa_protection_observer observer;
  observer.ctx = qa_world_handle_open(&state);
  observer.stored = absorb_stored;
  qa_cleanup node; /* finally { open = false } */
  qa_cleanup_push(&node, close_observer_handle, observer.ctx);
  qa_armor_stage_result result = protection->binding.absorb(protection->binding.ctx, input, observer);
  qa_cleanup_pop(&node);
  qa_world_handle_close(observer.ctx);
  state.open = false;
  if (!isfinite(result.saved) || result.saved < 0 || result.saved > input.amount)
    qa_error("Armor savings must be within the current damage amount");
  if (qa_session_actor_registry_is_live(authority->actors, actor.id)) {
    qa_combat_state current = read_state(authority, actor, entry);
    if (!armor_equal(current.armor, value->armor))
      qa_error("Armor stage omitted a committed armor observation");
    if (current.health != value->health)
      qa_error("Armor stage changed health without damage observation");
  }
  return result;
}

static void validate_mutations(qa_combat_state initial, const qa_damage_mutation *mutations,
                               size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (mutations[i].kind == QA_DAMAGE_MUTATION_SOURCE_VELOCITY)
      qa_error("Observed source velocity cannot be replayed by a combat policy");
  }
  double health = initial.health;
  qa_armor_state armor = initial.armor;
  for (size_t i = 0; i < count; i++) {
    if (mutations[i].kind == QA_DAMAGE_MUTATION_HEALTH) {
      if (mutations[i].health_before != health || !isfinite(mutations[i].health_after))
        qa_error("Invalid ordered health mutation");
      health = mutations[i].health_after;
    } else if (mutations[i].kind == QA_DAMAGE_MUTATION_ARMOR) {
      if (!armor_equal(mutations[i].armor_before, armor))
        qa_error("Invalid ordered armor mutation");
      armor = mutations[i].armor_after;
    }
  }
}
static void commit_mutations(qa_gameplay_authority *authority, qa_owned_actor actor,
                             binding_entry *entry, qa_combat_state initial,
                             const qa_damage_mutation *mutations, size_t count) {
  validate_mutations(initial, mutations, count);
  for (size_t i = 0; i < count; i++) {
    qa_damage_mutation mutation = mutations[i];
    qa_session_actor_registry_assert_owned(authority->actors, actor);
    switch (mutation.kind) {
      case QA_DAMAGE_MUTATION_SOURCE_VELOCITY:
        qa_error("Observed source velocity cannot be replayed by a combat policy");
      case QA_DAMAGE_MUTATION_HEALTH:
        if (entry->binding.read(entry->binding.ctx).health != mutation.health_before)
          qa_error("Combat health changed before its decision committed");
        entry->binding.write_health(entry->binding.ctx, mutation.health_after);
        observe_public_write(authority, actor, entry, true);
        break;
      case QA_DAMAGE_MUTATION_ARMOR:
        if (!armor_equal(read_state(authority, actor, entry).armor, mutation.armor_before))
          qa_error("Combat armor changed before its decision committed");
        write_armor_internal(authority, actor, entry, mutation.armor_after);
        observe_public_write(authority, actor, entry, false);
        break;
      case QA_DAMAGE_MUTATION_IMPULSE: {
        cursor *value = current_cursor(authority, actor);
        authority->hooks.impulse(authority->hooks.ctx, actor, mutation.impulse,
                                 mutation.movement_provider);
        advance_cursors(authority, actor, mutation);
        if (value != NULL) cursor_push(value, mutation);
        break;
      }
    }
  }
}
/* TS drops each progress value once it is consumed (GC). The progress owns
 * its heap mutation array (or NULL; qa_combat_progress), so the authority,
 * its only consumer, releases it after committing. */
/* A continuation or armor stage dropped without being resumed releases its
 * state through its discard (qa_combat_progress.discard); a resume frees it. */
static void discard_progress(qa_combat_progress *progress) {
  if (!str_eq(progress->kind, "complete") && progress->discard != NULL) progress->discard(progress->ctx);
  progress->discard = NULL;
}
static void release_progress_mutations(qa_combat_progress *progress) {
  free((void *)progress->mutations);
  progress->mutations = NULL;
  progress->mutation_count = 0;
}
typedef struct reservation_state {
  qa_gameplay_authority *authority;
  qa_owned_actor actor;
  qa_protection_channel channel;
  protection_slot *slot_value;
  binding_entry *primary;
  bool has_stage;
  qa_source_armor_stage stage;
  qa_protection_claim claim;
} reservation_state;

typedef struct intercept_state {
  reservation_state *reservation;
  bound_protection *binding;
} intercept_state;
static double stage_intercept(void *ctx, qa_armor_stage_input input,
                              qa_armor_stage_original_fn original, void *original_ctx) {
  intercept_state *state = ctx;
  reservation_state *reservation = state->reservation;
  protection_entry *entry = find_protection(reservation->authority, reservation->actor);
  if (slot_of(entry, reservation->channel) != reservation->slot_value ||
      reservation->slot_value->binding == NULL)
    return original(original_ctx);
  cursor *value = cursor_for(reservation->authority, reservation->actor, input.request);
  return absorb_protection(reservation->authority, reservation->actor, reservation->primary,
                           value, input, state->binding).saved;
}
typedef struct close_observe {
  reservation_state *state;
  cursor *value;
  bound_protection *bound;
} close_observe;
static void close_observe_release(void *arg);
static void reservation_close_impl(reservation_state *state) {
  qa_gameplay_authority *authority = state->authority;
  protection_entry *entry = find_protection(authority, state->actor);
  if (slot_of(entry, state->channel) != state->slot_value) return;
  cursor *value = current_cursor(authority, state->actor);
  if (value != NULL && !value->has_reaction &&
      !armor_equal(read_state(authority, state->actor, state->primary).armor, value->armor))
    qa_error("Protection closed with an unobserved armor store");
  if (state->channel == QA_PROTECTION_REGULAR) entry->regular = NULL;
  else entry->powered = NULL;
  if (entry->regular == NULL && entry->powered == NULL) {
    for (size_t i = 0; i < authority->protection_count; i++) {
      if (authority->protections[i] == entry) {
        memmove(&authority->protections[i], &authority->protections[i + 1],
                (authority->protection_count - i - 1) * sizeof *authority->protections);
        authority->protection_count--;
        break;
      }
    }
    free(entry);
  }
  qa_armor_stage_removal removal = state->slot_value->removal;
  bool has_removal = state->slot_value->has_removal;
  state->slot_value->has_removal = false;
  bound_protection *bound = state->slot_value->binding;
  state->slot_value->binding = NULL;
  /* try { remove?.() } finally { observe the armor store } */
  close_observe scope = {state, value, bound};
  qa_cleanup node;
  qa_cleanup_push(&node, close_observe_release, &scope);
  if (has_removal && removal.remove != NULL) removal.remove(removal.ctx);
  qa_cleanup_pop(&node);
  close_observe_release(&scope);
}
static void close_observe_release(void *arg) {
  close_observe *scope = arg;
  reservation_state *state = scope->state;
  qa_gameplay_authority *authority = state->authority;
  cursor *value = scope->value;
  bound_protection *bound = scope->bound;
  if (value != NULL && qa_session_actor_registry_is_live(authority->actors, state->actor.id)) {
    qa_damage_mutation write;
    memset(&write, 0, sizeof write);
    write.kind = QA_DAMAGE_MUTATION_ARMOR;
    write.armor_before = value->armor;
    write.armor_after = read_state(authority, state->actor, state->primary).armor;
    if (!value->has_reaction) observe_store(authority, state->actor, state->primary, value, write);
    else advance_cursors(authority, state->actor, write);
  }
  free_bound_protection(bound);
  /* The slot stays allocated (retained): a later close or stage intercept
   * compares against it, as TS compares slot identity. */
  retain(authority, state->slot_value);
}
static void reservation_close_fn(void *ctx) { reservation_close_impl(ctx); }
static void reservation_close_cleanup(void *ctx) { reservation_close_impl(ctx); }
static qa_armor_stage_removal reservation_bind_fn(void *ctx, qa_protection_binding original) {
  reservation_state *state = ctx;
  qa_gameplay_authority *authority = state->authority;
  bound_protection *bound = xcalloc(sizeof *bound);
  bound->binding = original;
  size_t pickup_count = 0;
  bound->pickups = qa_capture_original_pickup_rules(original.pickups, original.pickup_count,
                                                    &pickup_count);
  bound->pickup_count = pickup_count;
  if (authority->active_hits != 0) qa_error("Cannot attach protection during combat execution");
  qa_session_actor_registry_assert_owned(authority->actors, state->actor);
  protection_entry *entry = find_protection(authority, state->actor);
  if (slot_of(entry, state->channel) != state->slot_value || state->slot_value->binding != NULL)
    qa_error("Protection reservation is closed or bound");
  if (original.channel != state->channel ||
      !str_eq(original.claim.owner, state->claim.owner) ||
      !str_eq(original.claim.rule, state->claim.rule) ||
      !str_eq(original.claim.admission.kind, state->claim.admission.kind) ||
      (str_eq(original.claim.admission.kind, "replace-primary") &&
       (!str_eq(state->claim.admission.kind, "replace-primary") ||
        !str_eq(original.claim.admission.owner, state->claim.admission.owner))))
    qa_error("Protection binding differs from its reservation");
  state->slot_value->binding = bound;
  /* try { attach the source stage } catch { close(); throw } */
  qa_cleanup node;
  qa_cleanup_push(&node, reservation_close_cleanup, state);
  if (state->has_stage) {
    intercept_state *intercept = xcalloc(sizeof *intercept);
    intercept->reservation = state;
    intercept->binding = bound;
    retain(authority, intercept);
    qa_armor_stage_removal removal =
        state->stage.bind(state->stage.ctx, stage_intercept, intercept);
    protection_entry *current_entry = find_protection(authority, state->actor);
    if (slot_of(current_entry, state->channel) != state->slot_value) {
      if (removal.remove != NULL) removal.remove(removal.ctx);
      qa_error("Protection reservation closed during attachment");
    }
    state->slot_value->removal = removal;
    state->slot_value->has_removal = removal.remove != NULL;
  }
  qa_cleanup_pop(&node);
  qa_armor_stage_removal close_removal;
  close_removal.ctx = state;
  close_removal.remove = reservation_close_fn;
  return close_removal;
}

qa_protection_reservation qa_gameplay_authority_reserve_protection(
    qa_gameplay_authority *authority, qa_owned_actor actor, qa_protection_channel channel,
    qa_protection_claim claim) {
  if (authority->active_hits != 0)
    qa_error("Cannot reserve protection during combat execution");
  binding_entry *primary = require_binding(authority, actor);
  protection_entry *entry = find_protection(authority, actor);
  if (slot_of(entry, channel) != NULL)
    qa_error("Protection channel already has a component owner");
  qa_combat_protection_declaration declared =
      channel == QA_PROTECTION_REGULAR ? primary->binding.protection_regular
                                       : primary->binding.protection_powered;
  const char *owner = NULL;
  bool owner_is_null = false;
  if (!declared.declared) {
    if (channel == QA_PROTECTION_REGULAR || find_cells(authority, actor) != NULL) owner = actor.owner;
    else owner_is_null = true;
  } else if (declared.owner_is_null) owner_is_null = true;
  else owner = declared.owner;
  if ((str_eq(claim.admission.kind, "claim") && !owner_is_null) ||
      (str_eq(claim.admission.kind, "replace-primary") &&
       (owner_is_null || !str_eq(owner, claim.admission.owner))))
    qa_error("Protection admission does not match primary ownership");
  if (primary->binding.source_damage != NULL && !declared.has_stage) {
    char message[256];
    (void)snprintf(message, sizeof message, "Original source combat has no declared %s armor stage",
                   channel == QA_PROTECTION_REGULAR ? "regular" : "powered");
    qa_error(message);
  }
  protection_slot *slot_value = xcalloc(sizeof *slot_value);
  slot_value->claim = claim;
  if (entry == NULL) {
    entry = xcalloc(sizeof *entry);
    entry->actor = actor;
    if (authority->protection_count == authority->protection_capacity) {
      authority->protection_capacity =
          authority->protection_capacity != 0 ? authority->protection_capacity * 2 : 8;
      authority->protections = xrealloc(
          authority->protections, authority->protection_capacity * sizeof *authority->protections);
    }
    authority->protections[authority->protection_count++] = entry;
  }
  if (channel == QA_PROTECTION_REGULAR) entry->regular = slot_value;
  else entry->powered = slot_value;
  reservation_state *state = xcalloc(sizeof *state);
  state->authority = authority;
  state->actor = actor;
  state->channel = channel;
  state->slot_value = slot_value;
  state->primary = primary;
  state->has_stage = declared.has_stage;
  state->stage = declared.stage;
  state->claim = claim;
  retain(authority, state);
  qa_protection_reservation reservation;
  reservation.channel = channel;
  reservation.ctx = state;
  reservation.bind = reservation_bind_fn;
  reservation.close = reservation_close_fn;
  return reservation;
}

qa_armor_stage_removal qa_gameplay_authority_bind_protection(qa_gameplay_authority *authority,
                                                             qa_owned_actor actor,
                                                             qa_protection_binding binding) {
  qa_protection_reservation reservation =
      qa_gameplay_authority_reserve_protection(authority, actor, binding.channel, binding.claim);
  return reservation.bind(reservation.ctx, binding);
}

bool qa_gameplay_authority_protection_owner(qa_gameplay_authority *authority,
                                            qa_owned_actor actor, qa_protection_channel channel,
                                            qa_provider_id *out) {
  qa_session_actor_registry_assert_owned(authority->actors, actor);
  protection_slot *slot_value = slot_of(find_protection(authority, actor), channel);
  if (slot_value == NULL) return false;
  *out = slot_value->claim.owner;
  return true;
}
const qa_item_id *qa_gameplay_authority_protection_inventory_items(
    qa_gameplay_authority *authority, qa_owned_actor actor, qa_protection_channel channel,
    size_t *out_count) {
  qa_session_actor_registry_assert_owned(authority->actors, actor);
  *out_count = 0;
  protection_slot *slot_value = slot_of(find_protection(authority, actor), channel);
  if (slot_value == NULL) return NULL;
  if (slot_value->binding == NULL) qa_error("Protection reservation has not been bound");
  size_t count = slot_value->binding->binding.inventory_item_count;
  if (count > authority->scratch_items_capacity) {
    authority->scratch_items = xrealloc(authority->scratch_items,
                                        count * sizeof *authority->scratch_items);
    authority->scratch_items_capacity = count;
  }
  if (count != 0)
    memcpy(authority->scratch_items, slot_value->binding->binding.inventory_items,
           count * sizeof *authority->scratch_items);
  *out_count = count;
  return authority->scratch_items;
}

typedef struct pickup_current_state {
  qa_gameplay_authority *authority;
  qa_owned_actor actor;
  protection_slot *slot_value;
  bound_protection *binding;
  qa_protection_channel channel;
} pickup_current_state;
static bool authority_pickup_current(void *ctx) {
  pickup_current_state *state = ctx;
  protection_entry *entry = find_protection(state->authority, state->actor);
  return qa_session_actor_registry_is_live(state->authority->actors, state->actor.id) &&
         slot_of(entry, state->channel) == state->slot_value &&
         state->slot_value->binding == state->binding;
}
qa_original_pickup_resolution qa_gameplay_authority_resolve_pickup(
    qa_gameplay_authority *authority, qa_owned_actor actor, qa_original_pickup_offer offer) {
  qa_original_pickup_resolution resolution;
  resolution.matches = NULL;
  resolution.match_count = 0;
  resolution.blocks_primary = false;
  qa_session_actor_registry_assert_owned(authority->actors, actor);
  protection_entry *entry = find_protection(authority, actor);
  protection_slot *slots[2] = {entry != NULL ? entry->regular : NULL,
                               entry != NULL ? entry->powered : NULL};
  size_t total = 0;
  for (int s = 0; s < 2; s++) {
    if (slots[s] != NULL && slots[s]->binding != NULL) {
      for (size_t r = 0; r < slots[s]->binding->pickup_count; r++)
        total += slots[s]->binding->pickups[r].writes.count;
    }
  }
  if (total > authority->scratch_matches_capacity) {
    authority->scratch_matches = xrealloc(authority->scratch_matches,
                                          total * sizeof *authority->scratch_matches);
    authority->scratch_matches_capacity = total;
  }
  size_t at = 0;
  for (int s = 0; s < 2; s++) {
    if (slots[s] == NULL || slots[s]->binding == NULL) continue;
    bound_protection *binding = slots[s]->binding;
    for (size_t r = 0; r < binding->pickup_count; r++) {
      qa_captured_original_pickup_rule *rule = &binding->pickups[r];
      bool offered = false;
      for (size_t o = 0; o < rule->offered_count; o++) {
        if (str_eq(rule->offered[o], offer.item)) offered = true;
      }
      if (!offered) continue;
      for (size_t w = 0; w < rule->writes.count; w++) {
        qa_pickup_write write = rule->writes.values[w];
        if (write.kind != QA_PICKUP_RESOURCE_PROTECTION || write.channel != binding->binding.channel)
          continue;
        pickup_current_state *state = xcalloc(sizeof *state);
        state->authority = authority;
        state->actor = actor;
        state->slot_value = slots[s];
        state->binding = binding;
        state->channel = binding->binding.channel;
        retain(authority, state);
        qa_current_original_pickup *match = &authority->scratch_matches[at++];
        match->ctx = state;
        match->owner = binding->binding.claim.owner;
        match->operation = rule->operation;
        match->captured = rule->operation;
        match->captured.id = rule->id;
        match->captured.offered = rule->offered;
        match->captured.offered_count = rule->offered_count;
        match->captured.writes = rule->writes;
        match->captured.take = rule->take;
        match->captured.ctx = rule->take_ctx;
        match->write = write;
        match->current = authority_pickup_current;
      }
    }
  }
  resolution.matches = authority->scratch_matches;
  resolution.match_count = at;
  resolution.blocks_primary =
      offer.default_resource != NULL &&
      offer.default_resource->kind == QA_PICKUP_RESOURCE_PROTECTION &&
      slot_of(entry, offer.default_resource->channel) != NULL;
  return resolution;
}

typedef struct pickup_observer_state {
  qa_gameplay_authority *authority;
  qa_owned_actor actor;
  binding_entry *primary; /* may be NULL */
  uint64_t primary_version;
  bound_protection *regular;
  bound_protection *powered;
  qa_provider_id owner;
  bool open;
  void *token; /* the observer's closure handle */
  bool failed; /* TS failure.value ??= { error }: the first throw is kept */
  qa_caught_error failure;
} pickup_observer_state;
typedef struct pickup_store_call {
  pickup_observer_state *state;
  qa_protection_store change;
} pickup_store_call;
static void pickup_stored_body(void *arg) {
  pickup_store_call *call = arg;
  pickup_observer_state *state = call->state;
  qa_protection_store change = call->change;
  if (!state->open) qa_error("Pickup protection observer is closed");
  qa_session_actor_registry_assert_owned(state->authority->actors, state->actor);
  if (find_binding_entry(state->authority, state->actor) != state->primary ||
      (state->primary != NULL && state->primary->version != state->primary_version))
    qa_error("Pickup combat binding changed");
  if (state->primary == NULL)
    qa_error("Inventory-only pickup cannot report protection without a combat binding");
  if ((change.has_regular &&
       (state->regular == NULL || !str_eq(state->regular->binding.claim.owner, state->owner) ||
        protection_binding_of(state->authority, state->actor, QA_PROTECTION_REGULAR) != state->regular)) ||
      (change.has_powered &&
       (state->powered == NULL || !str_eq(state->powered->binding.claim.owner, state->owner) ||
        protection_binding_of(state->authority, state->actor, QA_PROTECTION_POWERED) != state->powered)))
    qa_error("Pickup reported protection outside its current owner");
  qa_armor_state after = read_state(state->authority, state->actor, state->primary).armor;
  if ((change.has_regular && !regular_equal(change.regular_after, after.regular)) ||
      (change.has_powered && !powered_equal(change.powered_after, after.powered)))
    qa_error("Pickup protection report does not match committed source state");
  cursor *value = current_cursor(state->authority, state->actor);
  if (value == NULL) return;
  qa_damage_mutation write;
  memset(&write, 0, sizeof write);
  write.kind = QA_DAMAGE_MUTATION_ARMOR;
  write.armor_before.regular = change.has_regular ? change.regular_before : value->armor.regular;
  write.armor_before.powered = change.has_powered ? change.powered_before : value->armor.powered;
  write.armor_after = after;
  if (!value->has_reaction) {
    observe_store(state->authority, state->actor, state->primary, value, write);
    return;
  }
  advance_cursors(state->authority, state->actor, write);
}
static void pickup_stored(void *ctx, qa_protection_store change) {
  pickup_observer_state *state = qa_world_handle_get(ctx);
  if (state == NULL) qa_error("Pickup protection observer is closed");
  pickup_store_call call = {state, change};
  qa_caught_error caught;
  if (!qa_try(pickup_stored_body, &call, &caught)) {
    if (!state->failed) {
      state->failed = true;
      state->failure = caught;
    }
    qa_rethrow(&caught);
  }
}
/* finally { open = false; activePickups-- } */
static void pickup_scope_release(void *arg) {
  pickup_observer_state *state = arg;
  state->open = false;
  qa_world_handle_close(state->token);
  state->authority->active_pickups--;
}
void *qa_gameplay_authority_with_pickup_protection(
    qa_gameplay_authority *authority, qa_owned_actor actor, qa_provider_id owner,
    void *(*operation)(void *ctx, qa_protection_observer observer), void *operation_ctx) {
  qa_session_actor_registry_assert_owned(authority->actors, actor);
  pickup_observer_state state;
  state.authority = authority;
  state.actor = actor;
  state.primary = find_binding_entry(authority, actor);
  state.primary_version = state.primary != NULL ? state.primary->version : 0;
  state.regular = protection_binding_of(authority, actor, QA_PROTECTION_REGULAR);
  state.powered = protection_binding_of(authority, actor, QA_PROTECTION_POWERED);
  state.owner = owner;
  state.open = true;
  state.failed = false;
  qa_protection_observer observer;
  state.token = qa_world_handle_open(&state);
  observer.ctx = state.token;
  observer.stored = pickup_stored;
  authority->active_pickups++;
  qa_cleanup node;
  qa_cleanup_push(&node, pickup_scope_release, &state);
  void *result = operation(operation_ctx, observer);
  cursor *value = current_cursor(authority, actor);
  if (state.failed) qa_rethrow(&state.failure);
  if (qa_session_actor_registry_is_live(authority->actors, actor.id)) {
    if (find_binding_entry(authority, actor) != state.primary ||
        (state.primary != NULL && state.primary->version != state.primary_version))
      qa_error("Pickup combat binding changed");
    if (state.primary != NULL && value != NULL &&
        !armor_equal(read_state(authority, actor, state.primary).armor, value->armor))
      qa_error("Pickup omitted a committed protection store");
  }
  qa_cleanup_pop(&node);
  pickup_scope_release(&state);
  return result;
}

bool qa_gameplay_authority_copied_primary_armor(qa_gameplay_authority *authority,
                                                qa_owned_actor actor, qa_armor_state *out) {
  qa_session_actor_registry_assert_owned(authority->actors, actor);
  binding_entry *entry = find_binding_entry(authority, actor);
  if (entry == NULL || !entry->copied) return false;
  qa_armor_state armor = entry->binding.read(entry->binding.ctx).armor;
  cell_entry *cells = find_cells(authority, actor);
  if (cells != NULL && !str_eq(armor.powered.kind, "none"))
    armor.powered.cells = cells->binding.read(cells->binding.ctx);
  *out = qa_copy_armor(armor);
  return true;
}
void qa_gameplay_authority_assert_idle(qa_gameplay_authority *authority) {
  if (authority->active_hits != 0 || authority->active_pickups != 0)
    qa_error("Cannot checkpoint during combat or pickup execution");
}

/* Copied (create) binding state. */
typedef struct copied_state {
  qa_combat_state state;
  const char *(*admit_damage)(void *ctx, qa_damage_request request);
  void *admit_ctx;
} copied_state;
static qa_combat_state copied_read(void *ctx) { return ((copied_state *)ctx)->state; }
static void copied_write_health(void *ctx, double health) {
  copied_state *state = ctx;
  state->state = qa_copy_combat(state->state);
  state->state.health = health;
}
static void copied_write_armor(void *ctx, qa_armor_state armor) {
  copied_state *state = ctx;
  state->state = qa_copy_combat(state->state);
  state->state.armor = armor;
}
static void copied_write_traits(void *ctx, qa_combat_traits traits) {
  copied_state *state = ctx;
  state->state = qa_copy_combat(state->state);
  state->state.can_take_damage = traits.can_take_damage;
  state->state.mass = traits.mass;
  state->state.invulnerable = traits.invulnerable;
  state->state.team = traits.team;
  /* TS spread only carries noKnockback when previously present. */
  state->state.has_no_knockback = traits.has_no_knockback;
  state->state.no_knockback = traits.no_knockback;
}
static const char *copied_admit(void *ctx, qa_damage_request request) {
  copied_state *state = ctx;
  return state->admit_damage(state->admit_ctx, request);
}
void qa_gameplay_authority_create_state(
    qa_gameplay_authority *authority, qa_owned_actor actor, qa_combat_state initial,
    const char *(*admit_damage)(void *ctx, qa_damage_request request), void *admit_ctx) {
  if (initial.armor.regular.kind == QA_REGULAR_ARMOR_SOURCE)
    qa_error("Copied primary armor cannot own a source formula");
  copied_state *state = xcalloc(sizeof *state);
  state->state = qa_copy_combat(initial);
  state->admit_damage = admit_damage;
  state->admit_ctx = admit_ctx;
  retain(authority, state);
  qa_combat_state_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.ctx = state;
  binding.read = copied_read;
  binding.write_health = copied_write_health;
  binding.write_armor = copied_write_armor;
  binding.write_traits = copied_write_traits;
  if (admit_damage != NULL) binding.admit_damage = copied_admit;
  qa_session_actor_registry_assert_owned(authority->actors, actor);
  if (find_binding_entry(authority, actor) != NULL)
    qa_error("Actor already has a combat binding");
  (void)add_binding(authority, actor, binding, true);
}

qa_combat_state_result qa_gameplay_authority_read(qa_gameplay_authority *authority,
                                                  qa_actor_id actor) {
  qa_combat_state_result result;
  result.has_value = false;
  memset(&result.value, 0, sizeof result.value);
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(authority->actors, actor, &owner)) return result;
  binding_entry *entry = find_binding_entry(authority, owner);
  if (entry == NULL) return result;
  result.has_value = true;
  result.value = read_state(authority, owner, entry);
  return result;
}
void qa_gameplay_authority_set_health(qa_gameplay_authority *authority, qa_owned_actor actor,
                                      double health) {
  if (!isfinite(health)) qa_range_error("Health must be finite");
  binding_entry *entry = require_binding(authority, actor);
  entry->binding.write_health(entry->binding.ctx, health);
  observe_public_write(authority, actor, entry, true);
}
void qa_gameplay_authority_set_armor(qa_gameplay_authority *authority, qa_owned_actor actor,
                                     qa_armor_state armor) {
  binding_entry *entry = require_binding(authority, actor);
  write_armor_internal(authority, actor, entry, armor);
  observe_public_write(authority, actor, entry, false);
}
qa_armor_state qa_gameplay_authority_normalize_legacy_armor(qa_gameplay_authority *authority,
                                                            qa_owned_actor actor,
                                                            qa_armor_state armor) {
  binding_entry *entry = require_binding(authority, actor);
  return entry->binding.normalize_legacy_armor != NULL
             ? entry->binding.normalize_legacy_armor(entry->binding.ctx, armor)
             : armor;
}
void qa_gameplay_authority_set_regular_points(qa_gameplay_authority *authority,
                                              qa_owned_actor actor, double points,
                                              const qa_regular_armor_state *initial) {
  if (!isfinite(points)) qa_range_error("Armor points must be finite");
  binding_entry *entry = require_binding(authority, actor);
  qa_armor_state armor = read_state(authority, actor, entry).armor;
  if (armor.regular.kind != QA_REGULAR_ARMOR_NONE) {
    armor.regular.points = points;
    qa_gameplay_authority_set_armor(authority, actor, armor);
    return;
  }
  if (points == 0) return;
  bool has_selected = false;
  qa_regular_armor_state selected;
  if (protection_binding_of(authority, actor, QA_PROTECTION_REGULAR) == NULL &&
      entry->binding.empty_regular_armor != NULL) {
    selected = entry->binding.empty_regular_armor(entry->binding.ctx, points);
    has_selected = true;
  } else if (initial != NULL) {
    selected = *initial;
    has_selected = true;
  }
  if (!has_selected) qa_error("Armor points require an explicit regular armor selection");
  selected.points = points;
  armor.regular = selected;
  qa_gameplay_authority_set_armor(authority, actor, armor);
}
void qa_gameplay_authority_set_regular_armor(qa_gameplay_authority *authority,
                                             qa_owned_actor actor, qa_regular_armor_state regular) {
  binding_entry *entry = require_binding(authority, actor);
  qa_armor_state armor = read_state(authority, actor, entry).armor;
  armor.regular = regular;
  qa_gameplay_authority_set_armor(authority, actor, armor);
}
void qa_gameplay_authority_set_powered_protection(qa_gameplay_authority *authority,
                                                  qa_owned_actor actor,
                                                  qa_powered_protection_state powered) {
  binding_entry *entry = require_binding(authority, actor);
  qa_armor_state armor = read_state(authority, actor, entry).armor;
  armor.powered = powered;
  qa_gameplay_authority_set_armor(authority, actor, armor);
}
void qa_gameplay_authority_set_traits(qa_gameplay_authority *authority, qa_owned_actor actor,
                                      qa_combat_trait_changes changes) {
  binding_entry *entry = require_binding(authority, actor);
  if (entry->binding.write_traits == NULL)
    qa_error("Source combat binding does not expose trait mutation");
  qa_combat_state current = entry->binding.read(entry->binding.ctx);
  qa_combat_traits traits;
  traits.can_take_damage = changes.has_can_take_damage ? changes.can_take_damage : current.can_take_damage;
  traits.mass = changes.has_mass ? changes.mass : current.mass;
  traits.invulnerable = changes.has_invulnerable ? changes.invulnerable : current.invulnerable;
  traits.team = changes.has_team ? changes.team : current.team;
  traits.has_no_knockback = changes.has_no_knockback ? true : current.has_no_knockback;
  traits.no_knockback = changes.has_no_knockback ? changes.no_knockback : current.no_knockback;
  if (!isfinite(traits.mass)) qa_range_error("Actor mass must be finite");
  entry->binding.write_traits(entry->binding.ctx, traits);
}
static qa_damage_outcome stale_outcome(qa_damage_request request) {
  qa_damage_outcome outcome;
  memset(&outcome, 0, sizeof outcome);
  outcome.kind = "stale-target";
  outcome.request = request;
  return outcome;
}
static qa_damage_outcome committed_outcome(qa_gameplay_authority *authority,
                                           qa_damage_request request,
                                           qa_damage_decision decision, qa_actor_id target) {
  (void)request;
  qa_damage_outcome outcome;
  memset(&outcome, 0, sizeof outcome);
  outcome.kind = "committed";
  outcome.decision = decision;
  qa_combat_state_result current = qa_gameplay_authority_read(authority, target);
  outcome.survived = current.has_value && current.value.health > 0;
  return outcome;
}

typedef struct source_observer_state {
  qa_gameplay_authority *authority;
  qa_owned_actor target;
  binding_entry *entry;
  cursor *value;
  qa_damage_request request;
} source_observer_state;
static qa_damage_decision source_decision(source_observer_state *state,
                                          qa_source_damage_result result) {
  if (!isfinite(result.applied_damage)) qa_error("Source applied damage must be finite");
  qa_combat_result combat;
  memset(&combat, 0, sizeof combat);
  combat.applied_damage = result.applied_damage;
  combat.reaction = result.reaction;
  return build_decision(state->authority, state->value->request, state->value->mutations,
                        state->value->mutation_count, combat);
}
static void source_stored(void *ctx, qa_damage_mutation write) {
  source_observer_state *state = qa_world_handle_get(ctx);
  if (state == NULL) qa_error("Source damage observer is closed");
  if (write.kind == QA_DAMAGE_MUTATION_ARMOR) {
    bound_protection *regular =
        protection_binding_of(state->authority, state->target, QA_PROTECTION_REGULAR);
    bound_protection *powered =
        protection_binding_of(state->authority, state->target, QA_PROTECTION_POWERED);
    qa_damage_mutation adjusted = write;
    if (regular != NULL) {
      adjusted.armor_before.regular = state->value->armor.regular;
      adjusted.armor_after.regular = state->value->armor.regular;
    }
    if (powered != NULL) {
      adjusted.armor_before.powered = state->value->armor.powered;
      adjusted.armor_after.powered = state->value->armor.powered;
    }
    observe_store(state->authority, state->target, state->entry, state->value, adjusted);
    return;
  }
  observe_store(state->authority, state->target, state->entry, state->value, write);
}
static void source_before_reaction(void *ctx, qa_source_damage_result result) {
  source_observer_state *state = qa_world_handle_get(ctx);
  if (state == NULL) qa_error("Source damage observer is closed");
  assert_cursor(state->authority, state->target, state->value);
  if (state->value->has_reaction) qa_error("Source damage reaction was already observed");
  qa_damage_decision captured = source_decision(state, result);
  state->value->has_reaction = true;
  state->value->reaction.applied_damage = captured.applied_damage;
  state->value->reaction.reaction = captured.reaction;
  state->authority->hooks.before_reaction(state->authority->hooks.ctx, state->target, captured);
}

static qa_damage_outcome run_source_canonical(
    qa_gameplay_authority *authority, qa_damage_request input, bool captured_input,
    qa_source_damage_result (*execute)(void *ctx, qa_source_damage_observer observer,
                                       qa_damage_request request),
    void *execute_ctx) {
  qa_damage_request request = captured_input ? input : qa_capture_request(input);
  qa_owned_actor target;
  if (!qa_session_actor_registry_resolve_owned(authority->actors, request.target, &target))
    return stale_outcome(request);
  bool allowed = authority->hooks.damage_allowed == NULL ||
                 authority->hooks.damage_allowed(authority->hooks.ctx, request);
  if (!qa_session_actor_registry_is_live(authority->actors, target.id))
    return stale_outcome(request);
  binding_entry *entry = require_binding(authority, target);
  qa_combat_state initial = read_state(authority, target, entry);
  cursor *value = open_cursor(authority, target, request, initial);
  cursor_scope scope = {authority, target, value};
  qa_cleanup node;
  qa_cleanup_push(&node, cursor_scope_release, &scope);
  source_observer_state state = {authority, target, entry, value, value->request};
  qa_source_damage_result result;
  if (!allowed) {
    result.applied_damage = 0;
    result.reaction = "none";
  } else {
    qa_source_damage_observer observer;
    observer.ctx = qa_world_handle_open(&state);
    observer.stored = source_stored;
    observer.before_reaction = source_before_reaction;
    qa_cleanup handle_node;
    qa_cleanup_push(&handle_node, close_observer_handle, observer.ctx);
    result = execute(execute_ctx, observer, value->request);
    qa_cleanup_pop(&handle_node);
    qa_world_handle_close(observer.ctx);
  }
  qa_cleanup_pop(&node);
  close_cursor(authority, target, value); /* retained: still readable below */
  qa_damage_decision completed = source_decision(&state, result);
  bool had_reaction = value->has_reaction;
  qa_source_damage_result observed_reaction = value->reaction;
  if (had_reaction && (observed_reaction.applied_damage != completed.applied_damage ||
                       !str_eq(observed_reaction.reaction, completed.reaction)))
    qa_error("Source damage result disagrees with its reaction boundary");
  if (!had_reaction) {
    if (!str_eq(completed.reaction, "none"))
      qa_error("Source damage omitted its reaction boundary");
    if (qa_session_actor_registry_is_live(authority->actors, target.id))
      authority->hooks.before_reaction(authority->hooks.ctx, target, completed);
  }
  qa_damage_outcome outcome = committed_outcome(authority, request, completed, target.id);
  authority->hooks.confirmed(authority->hooks.ctx, outcome);
  return outcome;
}

typedef struct compose_state {
  qa_gameplay_authority *authority;
  qa_damage_outcome (*canonical)(void *ctx, qa_damage_request request, bool captured);
  void *canonical_ctx;
} compose_state;
static void compose_canonical(void *ctx, const void *request, void *out) {
  compose_state *state = ctx;
  qa_gameplay_authority *authority = state->authority;
  qa_damage_request captured = qa_capture_request(*(const qa_damage_request *)request);
  if (authority->dispatched_count == authority->dispatched_capacity) {
    authority->dispatched_capacity =
        authority->dispatched_capacity != 0 ? authority->dispatched_capacity * 2 : 4;
    authority->dispatched = xrealloc(authority->dispatched,
                                     authority->dispatched_capacity * sizeof *authority->dispatched);
  }
  authority->dispatched[authority->dispatched_count++] = captured;
  qa_cleanup node; /* finally { dispatchedDamage.delete(captured) } */
  qa_cleanup_push(&node, dispatched_pop, authority);
  *(qa_damage_outcome *)out = state->canonical(state->canonical_ctx, captured, true);
  qa_cleanup_pop(&node);
  authority->dispatched_count--;
}
static qa_damage_outcome compose_damage(
    qa_gameplay_authority *authority, qa_damage_request input,
    qa_damage_outcome (*canonical)(void *ctx, qa_damage_request request, bool captured),
    void *canonical_ctx) {
  compose_state state = {authority, canonical, canonical_ctx};
  qa_mod_canonical fn = {&state, compose_canonical};
  qa_damage_request captured = qa_capture_request(input);
  qa_damage_outcome outcome;
  qa_mod_operation_dispatch(&authority->damage_op, &captured, fn, NULL, &outcome);
  return outcome;
}

typedef struct source_execute_state {
  qa_gameplay_authority *authority;
  qa_source_damage_result (*execute)(void *ctx, qa_source_damage_observer observer,
                                     qa_damage_request request);
  void *execute_ctx;
} source_execute_state;
static qa_damage_outcome source_canonical_adapter(void *ctx, qa_damage_request request,
                                                  bool captured) {
  source_execute_state *state = ctx;
  return run_source_canonical(state->authority, request, captured, state->execute,
                              state->execute_ctx);
}
qa_damage_outcome qa_gameplay_authority_run_source_damage(
    qa_gameplay_authority *authority, qa_damage_request input,
    qa_source_damage_result (*execute)(void *ctx, qa_source_damage_observer observer,
                                       qa_damage_request request),
    void *execute_ctx) {
  authority->active_hits++;
  qa_cleanup node;
  qa_cleanup_push(&node, active_hits_release, authority);
  qa_damage_outcome outcome;
  bool dispatched = is_dispatched(authority, input);
  if (qa_mod_operation_active(&authority->damage_op) && !dispatched) {
    source_execute_state state = {authority, execute, execute_ctx};
    outcome = compose_damage(authority, input, source_canonical_adapter, &state);
  } else {
    outcome = run_source_canonical(authority, input, dispatched, execute, execute_ctx);
  }
  qa_cleanup_pop(&node);
  authority->active_hits--;
  return outcome;
}

void qa_gameplay_authority_source_reaction(qa_gameplay_authority *authority,
                                           qa_damage_request input,
                                           qa_source_damage_result result) {
  qa_damage_request request = qa_capture_request(input);
  qa_owned_actor target;
  if (!qa_session_actor_registry_resolve_owned(authority->actors, request.target, &target))
    return;
  if (!isfinite(result.applied_damage)) qa_error("Source applied damage must be finite");
  qa_combat_result combat;
  memset(&combat, 0, sizeof combat);
  combat.applied_damage = result.applied_damage;
  combat.reaction = result.reaction;
  qa_damage_decision decision = build_decision(authority, request, NULL, 0, combat);
  authority->hooks.before_reaction(authority->hooks.ctx, target, decision);
}

typedef struct current_state_ctx {
  qa_gameplay_authority *authority;
  qa_actor_id target;
  bool has_attacker;
  qa_actor_id attacker;
} current_state_ctx;
static qa_combat_state_result current_target(void *ctx) {
  current_state_ctx *state = ctx;
  return qa_gameplay_authority_read(state->authority, state->target);
}
static qa_combat_state_result current_attacker(void *ctx) {
  current_state_ctx *state = ctx;
  qa_combat_state_result result;
  result.has_value = false;
  memset(&result.value, 0, sizeof result.value);
  if (!state->has_attacker) return result;
  return qa_gameplay_authority_read(state->authority, state->attacker);
}

static qa_damage_outcome apply_canonical(qa_gameplay_authority *authority,
                                         qa_damage_request input, bool captured_input) {
  qa_damage_request request = captured_input ? input : qa_capture_request(input);
  qa_owned_actor target;
  if (!qa_session_actor_registry_resolve_owned(authority->actors, request.target, &target))
    return stale_outcome(request);
  binding_entry *entry = require_binding(authority, target);
  if (entry->binding.source_damage != NULL)
    return entry->binding.source_damage(entry->binding.ctx, request);
  qa_combat_policy *policy = NULL;
  for (size_t i = 0; i < authority->policy_count; i++) {
    if (str_eq(authority->policies[i].policy.id, request.attack.combat_provider))
      policy = &authority->policies[i].policy;
  }
  if (policy == NULL) {
    char message[1024];
    (void)snprintf(message, sizeof message, "Missing combat policy: %s",
                   request.attack.combat_provider != NULL ? request.attack.combat_provider : "");
    qa_error(message);
  }
  const char *admission = entry->binding.admit_damage != NULL
                              ? entry->binding.admit_damage(entry->has_admit_ctx ? entry->admit_ctx : entry->binding.ctx, request)
                              : NULL;
  if (str_eq(admission, "handled")) {
    qa_damage_decision decision = build_decision(authority, request, NULL, 0, none_result());
    qa_damage_outcome outcome = committed_outcome(authority, request, decision, target.id);
    authority->hooks.confirmed(authority->hooks.ctx, outcome);
    return outcome;
  }
  if (!qa_session_actor_registry_is_live(authority->actors, target.id))
    return stale_outcome(request);
  qa_damage_adjustment_result adjustment;
  adjustment.has_value = false;
  if (entry->binding.adjust_damage != NULL)
    adjustment = entry->binding.adjust_damage(entry->has_adjust_ctx ? entry->adjust_ctx : entry->binding.ctx, request);
  if (!qa_session_actor_registry_is_live(authority->actors, target.id))
    return stale_outcome(request);
  if (adjustment.has_value) {
    request.amount = adjustment.value.amount;
    request.knockback = adjustment.value.knockback;
    request = qa_capture_request(request);
  }
  bool allowed = authority->hooks.damage_allowed == NULL ||
                 authority->hooks.damage_allowed(authority->hooks.ctx, request);
  if (!qa_session_actor_registry_is_live(authority->actors, target.id))
    return stale_outcome(request);
  bool prepared_cancel = !allowed;
  bool has_prepared = false;
  double prepared_amount = 0;
  if (allowed && policy->prepare != NULL) {
    qa_combat_state target_state = read_state(authority, target, entry);
    qa_combat_state_result attacker_state;
    attacker_state.has_value = false;
    memset(&attacker_state.value, 0, sizeof attacker_state.value);
    if (request.attack.attacker != NULL)
      attacker_state = qa_gameplay_authority_read(authority, *request.attack.attacker);
    qa_damage_preparation prepared = policy->prepare(
        policy->ctx, request, target_state,
        attacker_state.has_value ? &attacker_state.value : NULL);
    if (!prepared.is_continue) prepared_cancel = true;
    else { has_prepared = true; prepared_amount = prepared.amount; }
  }
  if (!qa_session_actor_registry_is_live(authority->actors, target.id))
    return stale_outcome(request);
  if (has_prepared && !isfinite(prepared_amount))
    qa_range_error("Prepared source damage must be finite");
  qa_combat_state initial = read_state(authority, target, entry);
  cursor *value = open_cursor(authority, target, request, initial);
  cursor_scope scope = {authority, target, value};
  qa_cleanup cursor_node;
  qa_cleanup_push(&cursor_node, cursor_scope_release, &scope);
  request = value->request;
  current_state_ctx state_ctx;
  state_ctx.authority = authority;
  state_ctx.target = target.id;
  state_ctx.has_attacker = request.attack.attacker != NULL;
  if (state_ctx.has_attacker) state_ctx.attacker = *request.attack.attacker;
  qa_current_combat_state current;
  current.ctx = &state_ctx;
  current.target = current_target;
  current.attacker = current_attacker;
  qa_damage_decision decision;
  bool have_decision = false;
  qa_combat_progress progress;
  memset(&progress, 0, sizeof progress);
  if (prepared_cancel) {
    progress.kind = "complete";
    progress.request = request;
    progress.result = none_result();
  } else {
    qa_combat_state_result attacker_state = current_attacker(&state_ctx);
    policy->decide(policy->ctx, request, initial,
                   attacker_state.has_value ? &attacker_state.value : NULL,
                   has_prepared, prepared_amount, &progress);
  }
  for (;;) {
    if (!request_equal(progress.request, request))
      qa_error("Combat policy replaced attack provenance");
    qa_combat_state_result latest = qa_gameplay_authority_read(authority, target.id);
    if (!latest.has_value) {
      if (progress.mutation_count != 0)
        qa_error("Combat continuation mutated a removed actor");
      release_progress_mutations(&progress);
      discard_progress(&progress);
      decision = build_decision(authority, request, value->mutations, value->mutation_count,
                                none_result());
      have_decision = true;
      break;
    }
    commit_mutations(authority, target, entry, latest.value, progress.mutations,
                     progress.mutation_count);
    release_progress_mutations(&progress);
    if (str_eq(progress.kind, "complete")) {
      if (!isfinite(progress.result.applied_damage))
        qa_error("Combat applied damage must be finite");
      decision = build_decision(authority, request, value->mutations, value->mutation_count,
                                progress.result);
      have_decision = true;
      break;
    }
    if (!qa_session_actor_registry_is_live(authority->actors, target.id)) {
      discard_progress(&progress);
      decision = build_decision(authority, request, value->mutations, value->mutation_count,
                                none_result());
      have_decision = true;
      break;
    }
    if (str_eq(progress.kind, "source-continuation")) {
      qa_combat_progress next;
      memset(&next, 0, sizeof next);
      progress.resume_source(progress.ctx, current, &next);
      progress = next;
      continue;
    }
    /* armor-stage */
    if (!request_equal(progress.input.request, request) || !isfinite(progress.input.amount))
      qa_error("Invalid armor stage input");
    bound_protection *component =
        protection_binding_of(authority, target, progress.channel);
    qa_armor_stage_result stage_result;
    if (component != NULL) {
      stage_result = absorb_protection(authority, target, entry, value, progress.input, component);
    } else {
      qa_combat_state state_now = read_state(authority, target, entry);
      qa_armor_result absorbed = progress.fallback(progress.ctx, state_now.armor);
      double saved = progress.channel == QA_PROTECTION_REGULAR ? absorbed.regular_saved
                                                                : absorbed.power_saved;
      bool other_unchanged;
      if (progress.channel == QA_PROTECTION_REGULAR) {
        qa_armor_state masked = absorbed.armor;
        masked.regular = state_now.armor.regular;
        other_unchanged = absorbed.power_saved == 0 && armor_equal(masked, state_now.armor);
      } else {
        other_unchanged = absorbed.regular_saved == 0 &&
                          regular_equal(absorbed.armor.regular, state_now.armor.regular);
      }
      if (!other_unchanged || !isfinite(saved))
        qa_error("Armor fallback changed another channel or returned invalid savings");
      if (!armor_equal(absorbed.armor, state_now.armor)) {
        qa_damage_mutation write;
        memset(&write, 0, sizeof write);
        write.kind = QA_DAMAGE_MUTATION_ARMOR;
        write.armor_before = state_now.armor;
        write.armor_after = absorbed.armor;
        commit_mutations(authority, target, entry, state_now, &write, 1);
      }
      stage_result.saved = saved;
    }
    if (!qa_session_actor_registry_is_live(authority->actors, target.id)) {
      discard_progress(&progress);
      decision = build_decision(authority, request, value->mutations, value->mutation_count,
                                none_result());
      have_decision = true;
      break;
    }
    qa_combat_progress next;
    memset(&next, 0, sizeof next);
    progress.resume_armor(progress.ctx, stage_result, current, &next);
    progress = next;
  }
  (void)have_decision;
  if (policy->after_health != NULL &&
      qa_session_actor_registry_is_live(authority->actors, target.id)) {
    bool has_health = false;
    for (size_t i = 0; i < decision.mutation_count; i++) {
      if (decision.mutations[i].kind == QA_DAMAGE_MUTATION_HEALTH) has_health = true;
    }
    if (has_health) {
      const char *reaction = policy->after_health(policy->ctx, decision, current);
      qa_combat_result result;
      memset(&result, 0, sizeof result);
      result.applied_damage = decision.applied_damage;
      result.reaction = reaction;
      result.has_feedback = decision.has_feedback;
      result.feedback_kind = decision.feedback_kind;
      result.feedback_power_armor = decision.feedback_power_armor;
      result.feedback_armor = decision.feedback_armor;
      result.feedback_blood = decision.feedback_blood;
      result.feedback_knockback = decision.feedback_knockback;
      result.feedback_battlesuit = decision.feedback_battlesuit;
      decision = build_decision(authority, request, decision.mutations,
                                decision.mutation_count, result);
    }
  }
  if (qa_session_actor_registry_is_live(authority->actors, target.id))
    authority->hooks.before_reaction(authority->hooks.ctx, target, decision);
  if (qa_session_actor_registry_is_live(authority->actors, target.id)) {
    double kick = decision.has_feedback && str_eq(decision.feedback_kind, "q2")
                      ? decision.feedback_knockback
                      : request.knockback;
    qa_pain_reaction reaction;
    reaction.attack = &value->request.attack;
    reaction.self = target;
    reaction.attacker = value->request.attack.attacker;
    reaction.kick = kick;
    reaction.damage = decision.applied_damage;
    if (str_eq(decision.reaction, "death")) {
      qa_death_reaction death;
      death.pain = reaction;
      death.inflictor = value->request.attack.inflictor;
      death.point = request.point;
      (void)qa_actor_callback_table_die(authority->callbacks, death);
    } else if (str_eq(decision.reaction, "pain")) {
      (void)qa_actor_callback_table_pain(authority->callbacks, reaction);
    }
  }
  qa_cleanup_pop(&cursor_node);
  close_cursor(authority, target, value);
  qa_damage_outcome outcome = committed_outcome(authority, request, decision, target.id);
  authority->hooks.confirmed(authority->hooks.ctx, outcome);
  return outcome;
}

typedef struct apply_state {
  qa_gameplay_authority *authority;
  qa_damage_outcome (*source_damage)(void *ctx, qa_damage_request request);
  void *source_damage_ctx;
  bool has_target;
  qa_owned_actor target;
  binding_entry *entry;
  uint64_t entry_version;
  qa_damage_outcome (*original)(void *ctx, qa_damage_request request);
} apply_state;
static qa_damage_outcome apply_canonical_adapter(void *ctx, qa_damage_request request,
                                                 bool captured) {
  apply_state *state = ctx;
  if (state->source_damage != NULL && state->has_target && state->entry != NULL) {
    qa_owned_actor current;
    if (qa_session_actor_registry_resolve_owned(state->authority->actors, request.target,
                                                &current) &&
        qa_same_owned_actor(current, state->target) &&
        find_binding_entry(state->authority, current) == state->entry &&
        state->entry->version == state->entry_version)
      return state->source_damage(state->source_damage_ctx, request);
  }
  return apply_canonical(state->authority, request, captured);
}
qa_damage_outcome qa_gameplay_authority_apply(
    qa_gameplay_authority *authority, qa_damage_request input,
    qa_damage_outcome (*source_damage)(void *ctx, qa_damage_request request),
    void *source_damage_ctx) {
  apply_state state;
  memset(&state, 0, sizeof state);
  state.authority = authority;
  state.source_damage = source_damage;
  state.source_damage_ctx = source_damage_ctx;
  if (source_damage != NULL &&
      qa_session_actor_registry_resolve_owned(authority->actors, input.target, &state.target)) {
    state.has_target = true;
    state.entry = require_binding(authority, state.target);
    state.entry_version = state.entry->version;
    state.original = state.entry->binding.source_damage;
  }
  authority->active_hits++;
  qa_cleanup node;
  qa_cleanup_push(&node, active_hits_release, authority);
  qa_damage_outcome outcome;
  bool dispatched = is_dispatched(authority, input);
  if (qa_mod_operation_active(&authority->damage_op) && !dispatched)
    outcome = compose_damage(authority, input, apply_canonical_adapter, &state);
  else
    outcome = apply_canonical_adapter(&state, input, dispatched);
  qa_cleanup_pop(&node);
  authority->active_hits--;
  return outcome;
}

static qa_damage_outcome c_apply(void *ctx, qa_damage_request request) {
  return qa_gameplay_authority_apply(ctx, request, NULL, NULL);
}
qa_damage_authority qa_gameplay_authority_as_contract(qa_gameplay_authority *authority) {
  qa_damage_authority table;
  table.ctx = authority;
  table.apply = c_apply;
  return table;
}
