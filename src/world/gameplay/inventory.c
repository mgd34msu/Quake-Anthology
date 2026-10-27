/* Port of src/world/gameplay/inventory.ts. GPL-2.0-or-later. */
#include "qa/world/gameplay/inventory.h"

#include <math.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static noreturn void qa_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}
static noreturn void qa_retired(void) {
  fprintf(stderr, "Error: %s\n", QA_SOURCE_ITEM_RETIRED_MESSAGE);
  abort();
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
/* isDeepStrictEqual / Object.is number equality. */
static bool num_eq(double a, double b) {
  if (isnan(a) && isnan(b)) return true;
  return memcmp(&a, &b, sizeof a) == 0;
}
static bool entry_equal(qa_inventory_entry a, qa_inventory_entry b) {
  if (!str_eq(a.item, b.item) || !num_eq(a.count, b.count) || !num_eq(a.capacity, b.capacity))
    return false;
  if (a.has_count_policy != b.has_count_policy) return false;
  if (a.has_count_policy &&
      (!str_eq(a.count_policy.kind, b.count_policy.kind) ||
       !str_eq(a.count_policy.arithmetic, b.count_policy.arithmetic))) return false;
  return true;
}

static double quantity(double value) {
  if (!isfinite(value) || value < 0)
    qa_range_error("Inventory quantity must be finite and nonnegative");
  return value;
}
static double source_count(qa_inventory_entry entry, double value) {
  if (!isfinite(value)) qa_range_error("Inventory counter must be finite");
  if (!entry.has_count_policy || !str_eq(entry.count_policy.kind, "source-counter"))
    return quantity(value);
  if (str_eq(entry.count_policy.arithmetic, "binary32")) {
    double rounded = (double)(float)value;
    if (!isfinite(rounded)) qa_range_error("Inventory counter exceeds binary32 range");
    return rounded;
  }
  if (str_eq(entry.count_policy.arithmetic, "int32")) return (double)(int32_t)value;
  return value; /* binary64 */
}
static qa_inventory_entry copy_entry(qa_inventory_entry entry) {
  (void)quantity(entry.capacity);
  entry.count = source_count(entry, entry.count);
  return entry;
}

qa_inventory_give_transition qa_inventory_give(qa_inventory_entry entry, double count) {
  (void)quantity(count);
  double room = entry.capacity - entry.count;
  double given = count < (room > 0 ? room : 0) ? count : (room > 0 ? room : 0);
  qa_inventory_give_transition transition;
  transition.is_write = false;
  transition.entry = entry;
  transition.given = 0;
  if (given == 0) return transition;
  qa_inventory_entry next = entry;
  next.count = entry.count + source_count(entry, given);
  next = copy_entry(next);
  transition.is_write = true;
  transition.entry = next;
  transition.given = next.count - entry.count;
  return transition;
}

/* --- storage --- */
typedef struct bound_items bound_items;
typedef struct bound_pickups bound_pickups;
typedef struct inv_store inv_store;
struct bound_items {
  qa_source_item_admission *admissions; /* owned copies w/ copied actions */
  const qa_source_item_action **action_arrays;
  size_t count;
  qa_provider_id owner;
  qa_inventory_state_binding state;
  void *invoke_ctx;
  void (*invoke)(void *ctx, qa_item_id item, qa_source_item_action action);
  bool active;
};
struct bound_pickups {
  int64_t id;
  qa_provider_id owner;
  qa_captured_original_pickup_rule *rules;
  size_t rule_count;
  qa_item_id *items;
  size_t item_count;
};
struct inv_store {
  qa_owned_actor actor;
  qa_inventory_state_binding primary;
  bound_items **groups;
  size_t group_count, group_capacity;
  /* item -> group map (parallel arrays) */
  qa_item_id *item_keys;
  bound_items **item_groups;
  size_t item_count, item_capacity;
  bound_pickups **pickups; /* distinct bindings */
  size_t pickup_count, pickup_capacity;
};

struct qa_shared_inventory_table {
  qa_session_actor_registry *actors;
  qa_mod_operation give_op, consume_op, configure_op, adjust_op;
  inv_store **stores;
  size_t store_count, store_capacity;
  int64_t next_pickup_id;
  int64_t release_subscription;
  /* scratch */
  qa_inventory_entry *scratch;
  size_t scratch_count, scratch_capacity;
  qa_inventory_entry *scratch2;
  size_t scratch2_count, scratch2_capacity;
  qa_source_item_definition *scratch_defs;
  size_t scratch_defs_capacity;
  qa_inventory_source_group *scratch_groups;
  size_t scratch_groups_capacity;
  qa_current_original_pickup *scratch_matches;
  size_t scratch_matches_capacity;
  void **owned_states; /* heap closure states freed at destroy */
  size_t owned_count, owned_capacity;
};

static void own_state(qa_shared_inventory_table *table, void *ptr) {
  if (table->owned_count == table->owned_capacity) {
    table->owned_capacity = table->owned_capacity != 0 ? table->owned_capacity * 2 : 16;
    table->owned_states = xrealloc(table->owned_states,
                                   table->owned_capacity * sizeof *table->owned_states);
  }
  table->owned_states[table->owned_count++] = ptr;
}

static inv_store *find_store(qa_shared_inventory_table *table, qa_owned_actor actor) {
  for (size_t i = 0; i < table->store_count; i++) {
    if (qa_same_owned_actor(table->stores[i]->actor, actor)) return table->stores[i];
  }
  return NULL;
}
static bound_items *group_for_item(inv_store *store, qa_item_id item) {
  for (size_t i = 0; i < store->item_count; i++) {
    if (str_eq(store->item_keys[i], item)) return store->item_groups[i];
  }
  return NULL;
}
static bound_pickups *pickup_for_item(inv_store *store, qa_item_id item) {
  for (size_t i = 0; i < store->pickup_count; i++) {
    for (size_t j = 0; j < store->pickups[i]->item_count; j++) {
      if (str_eq(store->pickups[i]->items[j], item)) return store->pickups[i];
    }
  }
  return NULL;
}

static void free_group(bound_items *group) {
  if (group == NULL) return;
  for (size_t i = 0; i < group->count; i++) free((void *)group->action_arrays[i]);
  free(group->action_arrays);
  free(group->admissions);
  free(group);
}
static void free_pickups(bound_pickups *binding) {
  if (binding == NULL) return;
  qa_free_captured_original_pickup_rules(binding->rules, binding->rule_count);
  free(binding->items);
  free(binding);
}
static void free_store(inv_store *store) {
  if (store == NULL) return;
  for (size_t i = 0; i < store->group_count; i++) free_group(store->groups[i]);
  for (size_t i = 0; i < store->pickup_count; i++) free_pickups(store->pickups[i]);
  free(store->groups);
  free(store->item_keys);
  free(store->item_groups);
  free(store->pickups);
  free(store);
}

static void on_release(void *ctx, qa_owned_actor actor) {
  qa_shared_inventory_table *table = ctx;
  for (size_t i = 0; i < table->store_count; i++) {
    if (qa_same_owned_actor(table->stores[i]->actor, actor)) {
      free_store(table->stores[i]);
      memmove(&table->stores[i], &table->stores[i + 1],
              (table->store_count - i - 1) * sizeof *table->stores);
      table->store_count--;
      return;
    }
  }
}

qa_shared_inventory_table *qa_shared_inventory_table_create(qa_session_actor_registry *actors) {
  qa_shared_inventory_table *table = calloc(1, sizeof *table);
  if (table == NULL) return NULL;
  table->actors = actors;
  (void)qa_mod_operation_init(&table->give_op, "inventory.give",
                              sizeof(qa_inventory_give_request), sizeof(double));
  (void)qa_mod_operation_init(&table->consume_op, "inventory.consume",
                              sizeof(qa_inventory_consume_request), sizeof(bool));
  (void)qa_mod_operation_init(&table->configure_op, "inventory.configure",
                              sizeof(qa_inventory_configure_request), 1);
  (void)qa_mod_operation_init(&table->adjust_op, "inventory.adjust-source-counter",
                              sizeof(qa_inventory_adjust_request), sizeof(double));
  table->release_subscription = qa_session_actor_registry_on_release(actors, on_release, table);
  return table;
}
void qa_shared_inventory_table_destroy(qa_shared_inventory_table *table) {
  if (table == NULL) return;
  qa_session_actor_registry_off_release(table->actors, table->release_subscription);
  for (size_t i = 0; i < table->store_count; i++) free_store(table->stores[i]);
  free(table->stores);
  for (size_t i = 0; i < table->owned_count; i++) free(table->owned_states[i]);
  free(table->owned_states);
  qa_mod_operation_destroy(&table->give_op);
  qa_mod_operation_destroy(&table->consume_op);
  qa_mod_operation_destroy(&table->configure_op);
  qa_mod_operation_destroy(&table->adjust_op);
  free(table->scratch);
  free(table->scratch2);
  free(table->scratch_defs);
  free(table->scratch_groups);
  free(table->scratch_matches);
  free(table);
}
qa_mod_operation *qa_shared_inventory_table_give_operation(qa_shared_inventory_table *table) { return &table->give_op; }
qa_mod_operation *qa_shared_inventory_table_consume_operation(qa_shared_inventory_table *table) { return &table->consume_op; }
qa_mod_operation *qa_shared_inventory_table_configure_operation(qa_shared_inventory_table *table) { return &table->configure_op; }
qa_mod_operation *qa_shared_inventory_table_adjust_operation(qa_shared_inventory_table *table) { return &table->adjust_op; }

/* Combined binding read: fills scratch (which scratch set by caller). */
static void assert_store_current(qa_shared_inventory_table *table, qa_owned_actor actor,
                                 inv_store *store) {
  qa_session_actor_registry_assert_owned(table->actors, actor);
  if (find_store(table, actor) != store) qa_error("Inventory binding is no longer current");
}
static const qa_inventory_entry *store_read(qa_shared_inventory_table *table, qa_owned_actor actor,
                                            inv_store *store, size_t *out_count,
                                            qa_inventory_entry **scratch, size_t *capacity) {
  assert_store_current(table, actor, store);
  size_t primary_count = 0;
  const qa_inventory_entry *primary = store->primary.read(store->primary.ctx, &primary_count);
  if (store->item_count == 0) {
    *out_count = primary_count;
    return primary;
  }
  size_t total = 0;
  for (size_t i = 0; i < primary_count; i++) {
    if (group_for_item(store, primary[i].item) == NULL) total++;
  }
  /* Group reads borrow their own storage; collect after reserving. */
  const qa_inventory_entry **group_arrays = xcalloc(store->group_count * sizeof *group_arrays);
  size_t *group_counts = xcalloc(store->group_count * sizeof *group_counts);
  for (size_t g = 0; g < store->group_count; g++) {
    group_arrays[g] = store->groups[g]->state.read(store->groups[g]->state.ctx, &group_counts[g]);
    total += group_counts[g];
  }
  if (total > *capacity) {
    *scratch = xrealloc(*scratch, total * sizeof **scratch);
    *capacity = total;
  }
  size_t at = 0;
  for (size_t i = 0; i < primary_count; i++) {
    if (group_for_item(store, primary[i].item) == NULL) (*scratch)[at++] = primary[i];
  }
  for (size_t g = 0; g < store->group_count; g++) {
    for (size_t i = 0; i < group_counts[g]; i++) (*scratch)[at++] = group_arrays[g][i];
  }
  free(group_arrays);
  free(group_counts);
  *out_count = at;
  return *scratch;
}
static void store_write(qa_shared_inventory_table *table, qa_owned_actor actor, inv_store *store,
                        qa_inventory_entry entry) {
  assert_store_current(table, actor, store);
  bound_items *group = group_for_item(store, entry.item);
  qa_inventory_state_binding *binding = group != NULL ? &group->state : &store->primary;
  binding->write(binding->ctx, entry);
}
static bool store_mutable_capacity(qa_shared_inventory_table *table, qa_owned_actor actor,
                                   inv_store *store, qa_item_id item) {
  assert_store_current(table, actor, store);
  bound_items *group = group_for_item(store, item);
  qa_inventory_state_binding *binding = group != NULL ? &group->state : &store->primary;
  return binding->mutable_capacity != NULL && binding->mutable_capacity(binding->ctx, item);
}

void qa_shared_inventory_table_bind(qa_shared_inventory_table *table, qa_owned_actor actor,
                                    qa_inventory_state_binding binding) {
  qa_session_actor_registry_assert_owned(table->actors, actor);
  if (find_store(table, actor) != NULL) qa_error("Actor already has an inventory binding");
  inv_store *store = xcalloc(sizeof *store);
  store->actor = actor;
  store->primary = binding;
  if (table->store_count == table->store_capacity) {
    table->store_capacity = table->store_capacity != 0 ? table->store_capacity * 2 : 8;
    table->stores = xrealloc(table->stores, table->store_capacity * sizeof *table->stores);
  }
  table->stores[table->store_count++] = store;
}

static const qa_inventory_entry *group_entries(bound_items *group, size_t *out_count,
                                               qa_inventory_entry **scratch, size_t *capacity) {
  if (!group->active) qa_error("Source item binding is no longer current");
  size_t count = 0;
  const qa_inventory_entry *entries = group->state.read(group->state.ctx, &count);
  bool bad = count != group->count;
  if (!bad) {
    for (size_t i = 0; i < count && !bad; i++) {
      for (size_t j = i + 1; j < count; j++) {
        if (str_eq(entries[i].item, entries[j].item)) bad = true;
      }
    }
    for (size_t d = 0; d < group->count && !bad; d++) {
      bool found = false;
      for (size_t i = 0; i < count; i++) {
        if (str_eq(entries[i].item, group->admissions[d].definition.item)) found = true;
      }
      if (!found) bad = true;
    }
  }
  if (bad) qa_error("Source item storage differs from its admitted definitions");
  if (count > *capacity) {
    *scratch = xrealloc(*scratch, count * sizeof **scratch);
    *capacity = count;
  }
  for (size_t i = 0; i < count; i++) (*scratch)[i] = copy_entry(entries[i]);
  *out_count = count;
  return *scratch;
}

/* --- lease --- */
typedef struct lease_state {
  qa_shared_inventory_table *table;
  qa_owned_actor actor;
  inv_store *store;
  bound_items *group;
  qa_provider_id owner;
} lease_state;
static bool lease_current(lease_state *state) {
  qa_owned_actor resolved;
  return state->group->active &&
         qa_session_actor_registry_resolve_owned(state->table->actors, state->actor.id, &resolved) &&
         qa_same_owned_actor(resolved, state->actor) &&
         find_store(state->table, state->actor) == state->store &&
         /* group still in store->groups */
         (state->store->group_count > 0 &&
          /* linear membership */
          (state->group->active));
}
static bool store_has_group(inv_store *store, bound_items *group) {
  for (size_t i = 0; i < store->group_count; i++) {
    if (store->groups[i] == group) return true;
  }
  return false;
}
static bool lease_current_fn(void *ctx) {
  lease_state *state = ctx;
  return lease_current(state) && store_has_group(state->store, state->group);
}
typedef struct lease_publish {
  lease_state *state;
  qa_inventory_entry after;
  bool published;
} lease_publish;
static void lease_publish_canonical(void *ctx, const void *request, void *out) {
  (void)out;
  lease_publish *publish = ctx;
  const qa_inventory_configure_request *value = request;
  if (!lease_current_fn(publish->state)) qa_retired();
  if (!qa_same_owned_actor(value->actor, publish->state->actor) ||
      !entry_equal(value->entry, publish->after))
    qa_error("Committed source inventory publication was changed");
  publish->published = true;
}
static void lease_publish_before(void *ctx) {
  lease_publish *publish = ctx;
  if (!publish->published)
    qa_error("Committed source inventory requires publication before observers");
}
static void lease_stored(void *ctx, const qa_source_item_store *changes, size_t count) {
  lease_state *state = ctx;
  qa_shared_inventory_table *table = state->table;
  if (!lease_current_fn(state)) qa_retired();
  qa_inventory_entry *local = NULL;
  size_t local_capacity = 0;
  size_t actual_count = 0;
  const qa_inventory_entry *actual = group_entries(state->group, &actual_count, &local, &local_capacity);
  qa_inventory_entry *afters = xcalloc((count != 0 ? count : 1) * sizeof *afters);
  for (size_t i = 0; i < count; i++) {
    qa_inventory_entry before = copy_entry(changes[i].before);
    qa_inventory_entry after = copy_entry(changes[i].after);
    bool seen_before = false;
    for (size_t j = 0; j < i; j++) {
      if (str_eq(afters[j].item, after.item)) seen_before = true;
    }
    bool admitted = group_for_item(state->store, after.item) == state->group;
    const qa_inventory_entry *found = NULL;
    for (size_t k = 0; k < actual_count; k++) {
      if (str_eq(actual[k].item, after.item)) found = &actual[k];
    }
    if (!str_eq(before.item, after.item) || !admitted || seen_before ||
        found == NULL || !entry_equal(*found, after))
      qa_error("Committed inventory differs from its source binding");
    afters[i] = after;
  }
  for (size_t i = 0; i < count; i++) {
    if (!lease_current_fn(state)) qa_retired();
    qa_inventory_configure_request request = {state->actor, afters[i]};
    lease_publish publish = {state, afters[i], false};
    qa_mod_canonical canonical = {&publish, lease_publish_canonical};
    qa_mod_before_observers before_obs = {&publish, lease_publish_before};
    char sink = 0;
    qa_mod_operation_dispatch(&table->configure_op, &request, canonical, &before_obs, &sink);
    if (!lease_current_fn(state)) qa_retired();
  }
  free(afters);
  free(local);
}
static void lease_close(void *ctx) {
  lease_state *state = ctx;
  bound_items *group = state->group;
  if (!group->active) return;
  group->active = false;
  inv_store *store = state->store;
  for (size_t i = 0; i < store->group_count; i++) {
    if (store->groups[i] == group) {
      memmove(&store->groups[i], &store->groups[i + 1],
              (store->group_count - i - 1) * sizeof *store->groups);
      store->group_count--;
      break;
    }
  }
  /* Remove pickup bindings delegated to this group's items. */
  for (size_t p = 0; p < store->pickup_count;) {
    bool delegate = false;
    for (size_t d = 0; d < group->count; d++) {
      if (pickup_for_item(store, group->admissions[d].definition.item) == store->pickups[p])
        delegate = true;
    }
    if (delegate) {
      free_pickups(store->pickups[p]);
      memmove(&store->pickups[p], &store->pickups[p + 1],
              (store->pickup_count - p - 1) * sizeof *store->pickups);
      store->pickup_count--;
    } else p++;
  }
  for (size_t i = 0; i < store->item_count;) {
    if (store->item_groups[i] == group) {
      memmove(&store->item_keys[i], &store->item_keys[i + 1],
              (store->item_count - i - 1) * sizeof *store->item_keys);
      memmove(&store->item_groups[i], &store->item_groups[i + 1],
              (store->item_count - i - 1) * sizeof *store->item_groups);
      store->item_count--;
    } else i++;
  }
  /* The group struct itself is freed with the store (no longer listed);
   * retain it dangling-free by leaking to table-owned states is wrong:
   * free it now that maps no longer reference it. */
  free_group(group);
}

qa_source_item_lease qa_shared_inventory_table_bind_items(qa_shared_inventory_table *table,
                                                          qa_owned_actor actor,
                                                          qa_inventory_item_binding requested) {
  qa_session_actor_registry_assert_owned(table->actors, actor);
  inv_store *store = find_store(table, actor);
  if (store == NULL || requested.item_count == 0)
    qa_error("Source items require an existing primary inventory and a nonempty group");
  size_t primary_count = 0;
  const qa_inventory_entry *primary = store->primary.read(store->primary.ctx, &primary_count);
  char message[2048];
  bound_items *group = xcalloc(sizeof *group);
  group->count = requested.item_count;
  group->owner = requested.owner;
  group->state = requested.state;
  group->invoke_ctx = requested.invoke_ctx;
  group->invoke = requested.invoke;
  group->active = true;
  group->admissions = xcalloc(requested.item_count * sizeof *group->admissions);
  group->action_arrays = xcalloc(requested.item_count * sizeof *group->action_arrays);
  for (size_t i = 0; i < requested.item_count; i++) {
    const qa_source_item_definition *definition = &requested.items[i].definition;
    const char *admission = requested.items[i].admission;
    bool seen = false;
    for (size_t j = 0; j < i; j++) {
      if (str_eq(group->admissions[j].definition.item, definition->item)) seen = true;
    }
    bool in_primary = false;
    for (size_t p = 0; p < primary_count; p++) {
      if (str_eq(primary[p].item, definition->item)) in_primary = true;
    }
    /* TS definition.source.provider: provider reference id field. */
    if (seen || !str_eq(definition->source.provider, requested.owner) ||
        definition->label == NULL || definition->label[0] == '\0' ||
        group_for_item(store, definition->item) != NULL ||
        (str_eq(admission, "add") ? in_primary : !in_primary)) {
      (void)snprintf(message, sizeof message,
                     "Source item %s conflicts with its current owner or admission",
                     definition->item);
      qa_error(message);
    }
    if (definition->actions != NULL) {
      bool bad = definition->action_count == 0 || requested.invoke == NULL;
      for (size_t a = 0; a < definition->action_count && !bad; a++) {
        for (size_t b = a + 1; b < definition->action_count; b++) {
          if (definition->actions[a] == definition->actions[b]) bad = true;
        }
      }
      if (bad) qa_error("Source item actions require distinct declared operations and their owner");
    }
    if (definition->is_weapon && definition->actions != NULL) {
      for (size_t a = 0; a < definition->action_count; a++) {
        if (definition->actions[a] == QA_SOURCE_ITEM_ACTION_USE)
          qa_error("Source weapons select through their weapon-slot owner, not a separate use action");
      }
    }
    group->admissions[i] = requested.items[i];
    if (definition->actions != NULL) {
      qa_source_item_action *copy = xcalloc(definition->action_count * sizeof *copy);
      memcpy(copy, definition->actions, definition->action_count * sizeof *copy);
      group->action_arrays[i] = copy;
      group->admissions[i].definition.actions = copy;
    }
  }
  qa_inventory_entry *local = NULL;
  size_t local_capacity = 0, actual_count = 0;
  (void)group_entries(group, &actual_count, &local, &local_capacity);
  free(local);
  for (size_t i = 0; i < group->count; i++) {
    qa_item_id item = group->admissions[i].definition.item;
    if (pickup_for_item(store, item) != NULL) {
      (void)snprintf(message, sizeof message, "Source item %s already has a pickup delegate", item);
      qa_error(message);
    }
  }
  for (size_t i = 0; i < group->count; i++) {
    if (store->item_count == store->item_capacity) {
      store->item_capacity = store->item_capacity != 0 ? store->item_capacity * 2 : 8;
      store->item_keys = xrealloc(store->item_keys, store->item_capacity * sizeof *store->item_keys);
      store->item_groups = xrealloc(store->item_groups, store->item_capacity * sizeof *store->item_groups);
    }
    store->item_keys[store->item_count] = group->admissions[i].definition.item;
    store->item_groups[store->item_count] = group;
    store->item_count++;
  }
  if (store->group_count == store->group_capacity) {
    store->group_capacity = store->group_capacity != 0 ? store->group_capacity * 2 : 4;
    store->groups = xrealloc(store->groups, store->group_capacity * sizeof *store->groups);
  }
  store->groups[store->group_count++] = group;
  lease_state *state = xcalloc(sizeof *state);
  state->table = table;
  state->actor = actor;
  state->store = store;
  state->group = group;
  state->owner = requested.owner;
  own_state(table, state);
  qa_source_item_lease lease;
  lease.ctx = state;
  lease.current = lease_current_fn;
  lease.stored = lease_stored;
  lease.close = lease_close;
  return lease;
}

typedef struct action_state {
  qa_shared_inventory_table *table;
  qa_owned_actor owner;
  qa_actor_id actor;
  inv_store *store;
  bound_items *group;
  qa_item_id item;
  qa_source_item_action action;
} action_state;
static void action_call(void *ctx) {
  action_state *state = ctx;
  qa_owned_actor resolved;
  if (!qa_session_actor_registry_resolve_owned(state->table->actors, state->actor, &resolved) ||
      !qa_same_owned_actor(resolved, state->owner) || !state->group->active ||
      find_store(state->table, state->owner) != state->store ||
      group_for_item(state->store, state->item) != state->group)
    qa_error("Source item action belongs to a retired inventory owner");
  state->group->invoke(state->group->invoke_ctx, state->item, state->action);
}
qa_source_item_action_invocation qa_shared_inventory_table_item_action(
    qa_shared_inventory_table *table, qa_actor_id actor, qa_item_id item,
    qa_source_item_action action) {
  qa_source_item_action_invocation result;
  result.has_value = false;
  result.ctx = NULL;
  result.call = NULL;
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(table->actors, actor, &owner)) return result;
  inv_store *store = find_store(table, owner);
  bound_items *group = store != NULL ? group_for_item(store, item) : NULL;
  if (group == NULL || !group->active) return result;
  bool declared = false;
  for (size_t i = 0; i < group->count; i++) {
    if (str_eq(group->admissions[i].definition.item, item)) {
      for (size_t a = 0; a < group->admissions[i].definition.action_count; a++) {
        if (group->admissions[i].definition.actions[a] == action) declared = true;
      }
    }
  }
  if (!declared) return result;
  if (group->invoke == NULL) qa_error("Admitted source item action has no owner");
  action_state *state = xcalloc(sizeof *state);
  state->table = table;
  state->owner = owner;
  state->actor = actor;
  state->store = store;
  state->group = group;
  state->item = item;
  state->action = action;
  own_state(table, state);
  result.has_value = true;
  result.ctx = state;
  result.call = action_call;
  return result;
}

const qa_source_item_definition *qa_shared_inventory_table_item_definitions(
    qa_shared_inventory_table *table, qa_actor_id actor, size_t *out_count) {
  qa_owned_actor owner;
  *out_count = 0;
  if (!qa_session_actor_registry_resolve_owned(table->actors, actor, &owner)) return NULL;
  inv_store *store = find_store(table, owner);
  if (store == NULL) return NULL;
  size_t total = 0;
  for (size_t g = 0; g < store->group_count; g++) total += store->groups[g]->count;
  if (total > table->scratch_defs_capacity) {
    table->scratch_defs = xrealloc(table->scratch_defs, total * sizeof *table->scratch_defs);
    table->scratch_defs_capacity = total;
  }
  size_t at = 0;
  for (size_t g = 0; g < store->group_count; g++) {
    for (size_t i = 0; i < store->groups[g]->count; i++)
      table->scratch_defs[at++] = store->groups[g]->admissions[i].definition;
  }
  *out_count = total;
  return table->scratch_defs;
}

qa_inventory_source_items_result qa_shared_inventory_table_source_items(
    qa_shared_inventory_table *table, qa_owned_actor actor) {
  qa_inventory_source_items_result result;
  memset(&result, 0, sizeof result);
  qa_session_actor_registry_assert_owned(table->actors, actor);
  inv_store *store = find_store(table, actor);
  if (store == NULL || store->group_count == 0) return result;
  size_t primary_count = 0;
  const qa_inventory_entry *primary = store->primary.read(store->primary.ctx, &primary_count);
  if (primary_count > table->scratch_capacity) {
    table->scratch = xrealloc(table->scratch, primary_count * sizeof *table->scratch);
    table->scratch_capacity = primary_count;
  }
  for (size_t i = 0; i < primary_count; i++) table->scratch[i] = copy_entry(primary[i]);
  if (store->group_count > table->scratch_groups_capacity) {
    table->scratch_groups = xrealloc(table->scratch_groups,
                                     store->group_count * sizeof *table->scratch_groups);
    table->scratch_groups_capacity = store->group_count;
  }
  for (size_t g = 0; g < store->group_count; g++) {
    table->scratch_groups[g].owner = store->groups[g]->owner;
    table->scratch_groups[g].items = store->groups[g]->admissions;
    table->scratch_groups[g].item_count = store->groups[g]->count;
  }
  result.has_value = true;
  result.primary = table->scratch;
  result.primary_count = primary_count;
  result.groups = table->scratch_groups;
  result.group_count = store->group_count;
  return result;
}

bool qa_shared_inventory_table_item_owner(qa_shared_inventory_table *table, qa_actor_id actor,
                                          qa_item_id item, qa_provider_id *out) {
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(table->actors, actor, &owner)) return false;
  inv_store *store = find_store(table, owner);
  bound_items *group = store != NULL ? group_for_item(store, item) : NULL;
  if (group == NULL) return false;
  *out = group->owner;
  return true;
}

int64_t qa_shared_inventory_table_bind_pickup(qa_shared_inventory_table *table,
                                              qa_owned_actor actor,
                                              qa_inventory_pickup_binding requested) {
  qa_session_actor_registry_assert_owned(table->actors, actor);
  inv_store *store = find_store(table, actor);
  size_t rule_count = 0;
  qa_captured_original_pickup_rule *rules =
      qa_capture_original_pickup_rules(requested.rules, requested.rule_count, &rule_count);
  /* Distinct inventory-write items, in first-appearance order. */
  qa_item_id *items = NULL;
  size_t item_count = 0, item_capacity = 0;
  char message[2048];
  for (size_t r = 0; r < rule_count; r++) {
    for (size_t w = 0; w < rules[r].writes.count; w++) {
      qa_pickup_write write = rules[r].writes.values[w];
      if (write.kind != QA_PICKUP_RESOURCE_INVENTORY) continue;
      bool seen = false;
      for (size_t i = 0; i < item_count; i++) {
        if (str_eq(items[i], write.item)) seen = true;
      }
      if (!seen) {
        if (item_count == item_capacity) {
          item_capacity = item_capacity != 0 ? item_capacity * 2 : 8;
          items = xrealloc(items, item_capacity * sizeof *items);
        }
        items[item_count++] = write.item;
      }
    }
  }
  if (item_count == 0) qa_error("Original inventory pickup delegate has no inventory writes");
  for (size_t r = 0; r < rule_count; r++) {
    for (size_t w = 0; w < rules[r].writes.count; w++) {
      qa_pickup_write write = rules[r].writes.values[w];
      if (write.kind != QA_PICKUP_RESOURCE_INVENTORY) continue;
      bool admitted = false;
      if (store != NULL) {
        size_t entry_count = 0;
        const qa_inventory_entry *entries =
            store_read(table, actor, store, &entry_count, &table->scratch, &table->scratch_capacity);
        for (size_t i = 0; i < entry_count; i++) {
          if (str_eq(entries[i].item, write.item)) admitted = true;
        }
      }
      if (!admitted) {
        (void)snprintf(message, sizeof message, "Original pickup destination %s was not admitted", write.item);
        qa_error(message);
      }
      bound_items *source = group_for_item(store, write.item);
      if (source != NULL && !str_eq(source->owner, requested.owner)) {
        (void)snprintf(message, sizeof message,
                       "Original pickup destination %s belongs to another source", write.item);
        qa_error(message);
      }
      if (pickup_for_item(store, write.item) != NULL) {
        (void)snprintf(message, sizeof message,
                       "Original pickup destination %s already has a grant owner", write.item);
        qa_error(message);
      }
      if (!str_eq(write.fields, "count") && !store_mutable_capacity(table, actor, store, write.item)) {
        (void)snprintf(message, sizeof message,
                       "Original pickup destination %s has no mutable capacity", write.item);
        qa_error(message);
      }
    }
  }
  if (store == NULL) qa_error("Original pickup inventory was not admitted");
  for (size_t p = 0; p < store->pickup_count; p++) {
    for (size_t r = 0; r < store->pickups[p]->rule_count; r++) {
      for (size_t o = 0; o < store->pickups[p]->rules[r].offered_count; o++) {
        qa_item_id offered = store->pickups[p]->rules[r].offered[o];
        for (size_t n = 0; n < rule_count; n++) {
          for (size_t m = 0; m < rules[n].offered_count; m++) {
            if (str_eq(rules[n].offered[m], offered)) {
              (void)snprintf(message, sizeof message,
                             "Ambiguous original inventory pickup for %s", offered);
              qa_error(message);
            }
          }
        }
      }
    }
  }
  bound_pickups *binding = xcalloc(sizeof *binding);
  binding->id = ++table->next_pickup_id;
  binding->owner = requested.owner;
  binding->rules = rules;
  binding->rule_count = rule_count;
  binding->items = items;
  binding->item_count = item_count;
  if (store->pickup_count == store->pickup_capacity) {
    store->pickup_capacity = store->pickup_capacity != 0 ? store->pickup_capacity * 2 : 4;
    store->pickups = xrealloc(store->pickups, store->pickup_capacity * sizeof *store->pickups);
  }
  store->pickups[store->pickup_count++] = binding;
  return binding->id;
}

void qa_shared_inventory_table_unbind_pickup(qa_shared_inventory_table *table,
                                             qa_owned_actor actor, int64_t binding_id) {
  inv_store *store = find_store(table, actor);
  if (store == NULL) return;
  for (size_t i = 0; i < store->pickup_count; i++) {
    if (store->pickups[i]->id == binding_id) {
      free_pickups(store->pickups[i]);
      memmove(&store->pickups[i], &store->pickups[i + 1],
              (store->pickup_count - i - 1) * sizeof *store->pickups);
      store->pickup_count--;
      return;
    }
  }
}

typedef struct pickup_current_state {
  qa_shared_inventory_table *table;
  qa_owned_actor actor;
  inv_store *store;
  bound_pickups *binding;
  qa_pickup_write write;
} pickup_current_state;
static bool pickup_current(void *ctx) {
  pickup_current_state *state = ctx;
  qa_owned_actor resolved;
  if (!qa_session_actor_registry_resolve_owned(state->table->actors, state->actor.id, &resolved) ||
      !qa_same_owned_actor(resolved, state->actor) ||
      find_store(state->table, state->actor) != state->store) return false;
  for (size_t i = 0; i < state->binding->item_count; i++) {
    qa_item_id item = state->binding->items[i];
    if (pickup_for_item(state->store, item) != state->binding) return false;
    size_t entry_count = 0;
    /* store_read asserts liveness; use the second scratch to nest. */
    const qa_inventory_entry *entries = store_read(
        state->table, state->actor, state->store, &entry_count,
        &state->table->scratch2, &state->table->scratch2_capacity);
    bool found = false;
    for (size_t e = 0; e < entry_count; e++) {
      if (str_eq(entries[e].item, item)) found = true;
    }
    if (!found) return false;
  }
  return str_eq(state->write.fields, "count") ||
         store_mutable_capacity(state->table, state->actor, state->store, state->write.item);
}

qa_original_pickup_resolution qa_shared_inventory_table_resolve_pickup(
    qa_shared_inventory_table *table, qa_owned_actor actor, qa_original_pickup_offer offer) {
  qa_original_pickup_resolution resolution;
  resolution.matches = NULL;
  resolution.match_count = 0;
  resolution.blocks_primary = false;
  qa_session_actor_registry_assert_owned(table->actors, actor);
  inv_store *store = find_store(table, actor);
  if (store == NULL) return resolution;
  size_t total = 0;
  for (size_t p = 0; p < store->pickup_count; p++) {
    for (size_t r = 0; r < store->pickups[p]->rule_count; r++) total += store->pickups[p]->rules[r].writes.count;
  }
  if (total > table->scratch_matches_capacity) {
    table->scratch_matches = xrealloc(table->scratch_matches, total * sizeof *table->scratch_matches);
    table->scratch_matches_capacity = total;
  }
  size_t at = 0;
  for (size_t p = 0; p < store->pickup_count; p++) {
    bound_pickups *binding = store->pickups[p];
    for (size_t r = 0; r < binding->rule_count; r++) {
      qa_captured_original_pickup_rule *rule = &binding->rules[r];
      bool offered = false;
      for (size_t o = 0; o < rule->offered_count; o++) {
        if (str_eq(rule->offered[o], offer.item)) offered = true;
      }
      if (!offered) continue;
      for (size_t w = 0; w < rule->writes.count; w++) {
        qa_pickup_write write = rule->writes.values[w];
        if (write.kind != QA_PICKUP_RESOURCE_INVENTORY) continue;
        pickup_current_state *state = xcalloc(sizeof *state);
        state->table = table;
        state->actor = actor;
        state->store = store;
        state->binding = binding;
        state->write = write;
        own_state(table, state);
        qa_current_original_pickup *match = &table->scratch_matches[at++];
        match->ctx = state;
        match->owner = binding->owner;
        match->operation = rule->operation;
        match->captured = rule->operation;
        match->captured.id = rule->id;
        match->captured.offered = rule->offered;
        match->captured.offered_count = rule->offered_count;
        match->captured.writes = rule->writes;
        match->captured.take = rule->take;
        match->captured.ctx = rule->take_ctx;
        match->write = write;
        match->current = pickup_current;
      }
    }
  }
  resolution.matches = table->scratch_matches;
  resolution.match_count = at;
  resolution.blocks_primary = offer.default_resource != NULL &&
      offer.default_resource->kind == QA_PICKUP_RESOURCE_INVENTORY &&
      group_for_item(store, offer.default_resource->item) != NULL;
  return resolution;
}

/* Local create() binding. */
typedef struct local_items {
  qa_inventory_entry *entries;
  size_t count, capacity;
} local_items;
static const qa_inventory_entry *local_read(void *ctx, size_t *out_count) {
  local_items *items = ctx;
  *out_count = items->count;
  return items->entries;
}
static void local_write(void *ctx, qa_inventory_entry entry) {
  local_items *items = ctx;
  for (size_t i = 0; i < items->count; i++) {
    if (str_eq(items->entries[i].item, entry.item)) {
      items->entries[i] = copy_entry(entry);
      return;
    }
  }
  if (items->count == items->capacity) {
    items->capacity = items->capacity != 0 ? items->capacity * 2 : 8;
    items->entries = xrealloc(items->entries, items->capacity * sizeof *items->entries);
  }
  items->entries[items->count++] = copy_entry(entry);
}
static bool local_mutable(void *ctx, qa_item_id item) {
  (void)ctx;
  (void)item;
  return true;
}
void qa_shared_inventory_table_create_entries(qa_shared_inventory_table *table,
                                              qa_owned_actor actor,
                                              const qa_inventory_entry *entries,
                                              size_t entry_count) {
  local_items *items = xcalloc(sizeof *items);
  own_state(table, items);
  for (size_t i = 0; i < entry_count; i++) {
    for (size_t j = 0; j < i; j++) {
      if (str_eq(entries[i].item, entries[j].item))
        qa_range_error("Duplicate inventory item %s", entries[i].item);
    }
    local_write(items, entries[i]);
  }
  qa_inventory_state_binding binding;
  binding.ctx = items;
  binding.read = local_read;
  binding.entry = NULL;
  binding.write = local_write;
  binding.mutable_capacity = local_mutable;
  qa_shared_inventory_table_bind(table, actor, binding);
}

const qa_inventory_entry *qa_shared_inventory_table_entries(qa_shared_inventory_table *table,
                                                            qa_actor_id actor,
                                                            size_t *out_count) {
  qa_owned_actor owner;
  *out_count = 0;
  if (!qa_session_actor_registry_resolve_owned(table->actors, actor, &owner)) return NULL;
  inv_store *store = find_store(table, owner);
  if (store == NULL) return NULL;
  size_t count = 0;
  const qa_inventory_entry *entries =
      store_read(table, owner, store, &count, &table->scratch2, &table->scratch2_capacity);
  if (count > table->scratch_capacity) {
    table->scratch = xrealloc(table->scratch, count * sizeof *table->scratch);
    table->scratch_capacity = count;
  }
  for (size_t i = 0; i < count; i++) table->scratch[i] = copy_entry(entries[i]);
  *out_count = count;
  return table->scratch;
}

bool qa_shared_inventory_table_has(qa_shared_inventory_table *table, qa_actor_id actor) {
  qa_owned_actor owner;
  return qa_session_actor_registry_resolve_owned(table->actors, actor, &owner) &&
         find_store(table, owner) != NULL;
}

bool qa_shared_inventory_table_mutable_capacity(qa_shared_inventory_table *table,
                                                qa_actor_id actor, qa_item_id item) {
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(table->actors, actor, &owner)) return false;
  inv_store *store = find_store(table, owner);
  if (store == NULL) return false;
  size_t count = 0;
  const qa_inventory_entry *entries =
      store_read(table, owner, store, &count, &table->scratch, &table->scratch_capacity);
  bool found = false;
  for (size_t i = 0; i < count; i++) {
    if (str_eq(entries[i].item, item)) found = true;
  }
  return found && store_mutable_capacity(table, owner, store, item);
}

double qa_shared_inventory_table_count(qa_shared_inventory_table *table, qa_actor_id actor,
                                       qa_item_id item) {
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(table->actors, actor, &owner)) return 0;
  inv_store *store = find_store(table, owner);
  bound_items *group = store != NULL ? group_for_item(store, item) : NULL;
  qa_inventory_state_binding *source =
      group != NULL ? &group->state : store != NULL ? &store->primary : NULL;
  if (group != NULL && group->state.entry != NULL) {
    qa_inventory_entry entry;
    if (!group->state.entry(group->state.ctx, item, &entry)) return 0;
    (void)quantity(entry.capacity);
    return source_count(entry, entry.count);
  }
  double result = 0;
  bool found = false;
  if (source != NULL) {
    size_t count = 0;
    const qa_inventory_entry *entries = source->read(source->ctx, &count);
    for (size_t i = 0; i < count; i++) {
      (void)quantity(entries[i].capacity);
      double value = source_count(entries[i], entries[i].count);
      if (!found && str_eq(entries[i].item, item)) { result = value; found = true; }
    }
  }
  return result;
}

/* --- canonical operations --- */
static bool consume_canonical(qa_shared_inventory_table *table, qa_inventory_consume_request request) {
  qa_session_actor_registry_assert_owned(table->actors, request.actor);
  (void)quantity(request.count);
  inv_store *store = find_store(table, request.actor);
  if (store == NULL) return request.count == 0;
  size_t count = 0;
  const qa_inventory_entry *entries =
      store_read(table, request.actor, store, &count, &table->scratch, &table->scratch_capacity);
  const qa_inventory_entry *entry = NULL;
  qa_inventory_entry found_entry;
  for (size_t i = 0; i < count; i++) {
    if (str_eq(entries[i].item, request.item)) { entry = &entries[i]; found_entry = entries[i]; break; }
  }
  if (entry == NULL) return request.count == 0;
  if (found_entry.count < request.count) return false;
  qa_inventory_entry next = found_entry;
  next.count = found_entry.count - source_count(found_entry, request.count);
  store_write(table, request.actor, store, copy_entry(next));
  return true;
}
static double give_canonical(qa_shared_inventory_table *table, qa_inventory_give_request request) {
  qa_session_actor_registry_assert_owned(table->actors, request.actor);
  (void)quantity(request.count);
  inv_store *store = find_store(table, request.actor);
  if (store == NULL) return 0;
  size_t count = 0;
  const qa_inventory_entry *entries =
      store_read(table, request.actor, store, &count, &table->scratch, &table->scratch_capacity);
  bool found = false;
  qa_inventory_entry entry;
  for (size_t i = 0; i < count; i++) {
    if (str_eq(entries[i].item, request.item)) { found = true; entry = entries[i]; break; }
  }
  if (!found) return 0;
  qa_inventory_give_transition transition = qa_inventory_give(entry, request.count);
  if (!transition.is_write) return 0;
  store_write(table, request.actor, store, transition.entry);
  return transition.given;
}
static void configure_canonical_impl(qa_shared_inventory_table *table, qa_owned_actor actor,
                                     qa_inventory_entry entry,
                                     void (*committed)(void *, qa_inventory_committed_change),
                                     void *committed_ctx) {
  qa_session_actor_registry_assert_owned(table->actors, actor);
  inv_store *store = find_store(table, actor);
  if (store == NULL) qa_error("Actor has no inventory binding");
  bound_items *group = group_for_item(store, entry.item);
  size_t count = 0;
  const qa_inventory_entry *entries =
      store_read(table, actor, store, &count, &table->scratch, &table->scratch_capacity);
  bool has_previous = false;
  qa_inventory_entry previous;
  for (size_t i = 0; i < count; i++) {
    if (str_eq(entries[i].item, entry.item)) { has_previous = true; previous = entries[i]; break; }
  }
  qa_inventory_entry value = entry;
  if (!value.has_count_policy && has_previous && previous.has_count_policy) {
    value.has_count_policy = true;
    value.count_policy = previous.count_policy;
  }
  store_write(table, actor, store, copy_entry(value));
  if (committed != NULL) {
    qa_session_actor_registry_assert_owned(table->actors, actor);
    if (find_store(table, actor) != store || group_for_item(store, entry.item) != group)
      qa_error("Inventory owner changed during observed store");
    size_t after_count = 0;
    const qa_inventory_entry *after_entries =
        store_read(table, actor, store, &after_count, &table->scratch, &table->scratch_capacity);
    bool found_after = false;
    qa_inventory_entry after;
    for (size_t i = 0; i < after_count; i++) {
      if (str_eq(after_entries[i].item, entry.item)) { found_after = true; after = after_entries[i]; break; }
    }
    if (!found_after) qa_error("Observed inventory entry disappeared during its store");
    qa_inventory_committed_change change;
    change.actor = actor;
    change.has_before = has_previous;
    if (has_previous) change.before = copy_entry(previous);
    change.after = copy_entry(after);
    committed(committed_ctx, change);
  }
}
static double adjust_canonical(qa_shared_inventory_table *table, qa_inventory_adjust_request request) {
  qa_session_actor_registry_assert_owned(table->actors, request.actor);
  inv_store *store = find_store(table, request.actor);
  bool found = false;
  qa_inventory_entry entry;
  if (store != NULL) {
    size_t count = 0;
    const qa_inventory_entry *entries =
        store_read(table, request.actor, store, &count, &table->scratch, &table->scratch_capacity);
    for (size_t i = 0; i < count; i++) {
      if (str_eq(entries[i].item, request.item)) { found = true; entry = entries[i]; break; }
    }
  }
  if (!found || !entry.has_count_policy || !str_eq(entry.count_policy.kind, "source-counter"))
    qa_error("Item is not a signed source counter");
  qa_inventory_entry next = entry;
  next.count = source_count(entry, entry.count) + source_count(entry, request.delta);
  next = copy_entry(next);
  store_write(table, request.actor, store, next);
  return next.count;
}

typedef struct op_state { qa_shared_inventory_table *table; } op_state;
static void consume_dispatch(void *ctx, const void *request, void *out) {
  *(bool *)out = consume_canonical(((op_state *)ctx)->table, *(const qa_inventory_consume_request *)request);
}
static void give_dispatch(void *ctx, const void *request, void *out) {
  *(double *)out = give_canonical(((op_state *)ctx)->table, *(const qa_inventory_give_request *)request);
}
static void adjust_dispatch(void *ctx, const void *request, void *out) {
  *(double *)out = adjust_canonical(((op_state *)ctx)->table, *(const qa_inventory_adjust_request *)request);
}
static void configure_dispatch(void *ctx, const void *request, void *out) {
  (void)out;
  const qa_inventory_configure_request *value = request;
  configure_canonical_impl(((op_state *)ctx)->table, value->actor, value->entry, NULL, NULL);
}

bool qa_shared_inventory_table_consume(qa_shared_inventory_table *table, qa_owned_actor actor,
                                       qa_item_id item, double count) {
  qa_inventory_consume_request request = {actor, item, count};
  if (!qa_mod_operation_active(&table->consume_op)) return consume_canonical(table, request);
  qa_session_actor_registry_assert_owned(table->actors, actor);
  op_state state = {table};
  qa_mod_canonical canonical = {&state, consume_dispatch};
  bool result = false;
  qa_mod_operation_dispatch(&table->consume_op, &request, canonical, NULL, &result);
  return result;
}
double qa_shared_inventory_table_give(qa_shared_inventory_table *table, qa_owned_actor actor,
                                      qa_item_id item, double count) {
  qa_inventory_give_request request = {actor, item, count};
  if (!qa_mod_operation_active(&table->give_op)) return give_canonical(table, request);
  qa_session_actor_registry_assert_owned(table->actors, actor);
  op_state state = {table};
  qa_mod_canonical canonical = {&state, give_dispatch};
  double result = 0;
  qa_mod_operation_dispatch(&table->give_op, &request, canonical, NULL, &result);
  return result;
}
double qa_shared_inventory_table_adjust_source_counter(qa_shared_inventory_table *table,
                                                       qa_owned_actor actor, qa_item_id item,
                                                       double delta) {
  qa_inventory_adjust_request request = {actor, item, delta};
  if (!qa_mod_operation_active(&table->adjust_op)) return adjust_canonical(table, request);
  qa_session_actor_registry_assert_owned(table->actors, actor);
  op_state state = {table};
  qa_mod_canonical canonical = {&state, adjust_dispatch};
  double result = 0;
  qa_mod_operation_dispatch(&table->adjust_op, &request, canonical, NULL, &result);
  return result;
}

/* The observed configure needs its own canonical (below). */
typedef struct observed_state {
  qa_shared_inventory_table *table;
  qa_owned_actor actor;
  qa_inventory_entry entry;
  void (*committed)(void *ctx, qa_inventory_committed_change change);
  void *committed_ctx;
  bool stored;
} observed_state;
static void observed_committed(void *ctx, qa_inventory_committed_change change) {
  observed_state *state = ctx;
  state->committed(state->committed_ctx, change);
  state->stored = true;
}
static void observed_canonical(void *ctx, const void *request, void *out) {
  (void)out;
  observed_state *state = ctx;
  const qa_inventory_configure_request *value = request;
  if (!qa_same_owned_actor(value->actor, state->actor) ||
      !str_eq(value->entry.item, state->entry.item))
    qa_error("Observed inventory store changed actor or item");
  configure_canonical_impl(state->table, value->actor, value->entry, observed_committed, state);
}
static void observed_before(void *ctx) {
  observed_state *state = ctx;
  if (!state->stored)
    qa_error("Observed source inventory requires its canonical store before observers");
}
void qa_shared_inventory_table_configure(
    qa_shared_inventory_table *table, qa_owned_actor actor, qa_inventory_entry entry,
    void (*committed)(void *ctx, qa_inventory_committed_change change), void *committed_ctx) {
  if (qa_mod_operation_active(&table->configure_op))
    qa_session_actor_registry_assert_owned(table->actors, actor);
  qa_inventory_configure_request request = {actor, entry};
  if (committed == NULL) {
    if (!qa_mod_operation_active(&table->configure_op)) {
      configure_canonical_impl(table, actor, entry, NULL, NULL);
      return;
    }
    op_state state = {table};
    qa_mod_canonical canonical = {&state, configure_dispatch};
    char sink = 0;
    qa_mod_operation_dispatch(&table->configure_op, &request, canonical, NULL, &sink);
    return;
  }
  observed_state state = {table, actor, entry, committed, committed_ctx, false};
  qa_mod_canonical canonical = {&state, observed_canonical};
  qa_mod_before_observers before = {&state, observed_before};
  char sink = 0;
  qa_mod_operation_dispatch(&table->configure_op, &request, canonical, &before, &sink);
}

static const qa_inventory_entry *c_entries(void *ctx, qa_actor_id actor, size_t *out_count) {
  return qa_shared_inventory_table_entries(ctx, actor, out_count);
}
static double c_count(void *ctx, qa_actor_id actor, qa_item_id item) {
  return qa_shared_inventory_table_count(ctx, actor, item);
}
static bool c_consume(void *ctx, qa_owned_actor actor, qa_item_id item, double count) {
  return qa_shared_inventory_table_consume(ctx, actor, item, count);
}
static double c_give(void *ctx, qa_owned_actor actor, qa_item_id item, double count) {
  return qa_shared_inventory_table_give(ctx, actor, item, count);
}
qa_inventory_table qa_shared_inventory_table_as_contract(qa_shared_inventory_table *table) {
  qa_inventory_table contract;
  contract.ctx = table;
  contract.entries = c_entries;
  contract.count = c_count;
  contract.consume = c_consume;
  contract.give = c_give;
  return contract;
}
