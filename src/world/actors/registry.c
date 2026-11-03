/* Port of src/world/actors/registry.ts. GPL-2.0-or-later. */
#include "qa/world/actors/registry.h"

#include <math.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

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

bool qa_same_owned_actor(qa_owned_actor left, qa_owned_actor right) {
  return qa_same_actor(left.id, right.id) && str_eq(left.owner, right.owner) &&
         left.token == right.token;
}

/* --- session generations (TS WeakMap<SessionId, Map<number, number>>) --- */
typedef struct gen_map {
  const void *token;
  int64_t *slots;   /* parallel arrays: slot -> next generation */
  int64_t *values;
  size_t count;
  size_t capacity;
  struct gen_map *next;
} gen_map;
static gen_map *generation_maps;

static gen_map *gen_map_for(const void *token) {
  for (gen_map *map = generation_maps; map != NULL; map = map->next) {
    if (map->token == token) return map;
  }
  gen_map *map = calloc(1, sizeof *map);
  if (map == NULL) abort();
  map->token = token;
  map->next = generation_maps;
  generation_maps = map;
  return map;
}
static int64_t gen_get(gen_map *map, int64_t slot, bool *found) {
  for (size_t i = 0; i < map->count; i++) {
    if (map->slots[i] == slot) { *found = true; return map->values[i]; }
  }
  *found = false;
  return 0;
}
static void gen_set(gen_map *map, int64_t slot, int64_t value) {
  for (size_t i = 0; i < map->count; i++) {
    if (map->slots[i] == slot) { map->values[i] = value; return; }
  }
  if (map->count == map->capacity) {
    map->capacity = map->capacity != 0 ? map->capacity * 2 : 8;
    map->slots = xrealloc(map->slots, map->capacity * sizeof *map->slots);
    map->values = xrealloc(map->values, map->capacity * sizeof *map->values);
  }
  map->slots[map->count] = slot;
  map->values[map->count] = value;
  map->count++;
}

int64_t qa_next_actor_generation(qa_session_id session, int64_t slot, int64_t minimum) {
  if (slot < 0 || slot > QA_MAX_SAFE_INTEGER)
    qa_range_error("Actor slot must be a nonnegative safe integer");
  if (minimum < 0 || minimum > QA_MAX_SAFE_INTEGER)
    qa_range_error("Actor generation must be a nonnegative safe integer");
  gen_map *generations = gen_map_for(session.token);
  bool found = false;
  int64_t stored = gen_get(generations, slot, &found);
  int64_t generation = minimum > (found ? stored : 0) ? minimum : (found ? stored : 0);
  if (generation >= QA_MAX_SAFE_INTEGER)
    qa_range_error("Actor generation is exhausted");
  gen_set(generations, slot, generation + 1);
  return generation;
}

/* --- registry --- */
typedef struct slot {
  int64_t generation;
  bool has_actor;
  qa_owned_actor actor;
  bool has_observation;
  qa_actor_observation observation;
  const char *definition;
  bool has_source;
  qa_provider_id source_provider;
  int64_t source_slot;
} slot;

typedef struct release_sub {
  int64_t id;
  qa_actor_release_fn fn;
  void *ctx;
  bool active;
} release_sub;

typedef struct restored_entry {
  int64_t saved_slot;
  int64_t saved_generation;
  qa_owned_actor actor;
} restored_entry;

typedef struct restored_source_row {
  qa_source_actor_checkpoint value;
} restored_source_row;

struct qa_session_actor_registry {
  const qa_identity_owner *identities;
  qa_session_id session;
  int64_t capacity;
  slot *slots;
  size_t slot_count;
  size_t slot_capacity;
  release_sub *releases;
  size_t release_count;
  size_t release_capacity;
  int64_t next_release_id;
  gen_map *generations;
  restored_entry *restored;
  size_t restored_count;
  size_t restored_capacity;
  /* restored sources grouped implicitly by provider string */
  restored_source_row *restored_sources;
  size_t restored_source_count;
  size_t restored_source_capacity;
  qa_actor_slot_checkpoint *history;
  int64_t *history_base;
  size_t history_count;
  struct saved_ref { char *key; qa_actor_id value; } *saved_refs;
  size_t saved_ref_count;
  size_t saved_ref_capacity;
  bool closed;
  double revision;
  /* scratch outputs */
  qa_owned_actor *scratch_owned;
  size_t scratch_owned_capacity;
  qa_actor_observation *scratch_obs;
  size_t scratch_obs_capacity;
  qa_actor_slot_checkpoint *scratch_checkpoints;
  size_t scratch_checkpoints_capacity;
  qa_source_actor_checkpoint *scratch_sources;
  size_t scratch_sources_capacity;
};

qa_session_actor_registry *qa_session_actor_registry_create(
    const qa_identity_owner *identities, int64_t capacity) {
  if (capacity <= 0 || capacity > QA_MAX_SAFE_INTEGER)
    qa_range_error("Actor capacity must be a positive safe integer");
  qa_session_actor_registry *registry = calloc(1, sizeof *registry);
  if (registry == NULL) return NULL;
  registry->identities = identities;
  registry->session = identities->session;
  registry->capacity = capacity;
  registry->generations = gen_map_for(registry->session.token);
  return registry;
}

void qa_session_actor_registry_destroy(qa_session_actor_registry *registry) {
  if (registry == NULL) return;
  for (size_t i = 0; i < registry->saved_ref_count; i++) free(registry->saved_refs[i].key);
  free(registry->saved_refs);
  free(registry->history);
  free(registry->history_base);
  free(registry->restored);
  free(registry->restored_sources);
  free(registry->releases);
  free(registry->slots);
  free(registry->scratch_owned);
  free(registry->scratch_obs);
  free(registry->scratch_checkpoints);
  free(registry->scratch_sources);
  free(registry);
}

qa_session_id qa_session_actor_registry_session(const qa_session_actor_registry *registry) {
  return registry->session;
}
int64_t qa_session_actor_registry_capacity(const qa_session_actor_registry *registry) {
  return registry->capacity;
}
double qa_session_actor_registry_revision(const qa_session_actor_registry *registry) {
  return registry->revision;
}

static slot *push_slot(qa_session_actor_registry *registry) {
  if (registry->slot_count == registry->slot_capacity) {
    registry->slot_capacity = registry->slot_capacity != 0 ? registry->slot_capacity * 2 : 16;
    registry->slots = xrealloc(registry->slots,
                               registry->slot_capacity * sizeof *registry->slots);
  }
  slot *value = &registry->slots[registry->slot_count++];
  memset(value, 0, sizeof *value);
  return value;
}

bool qa_session_actor_registry_is_live(const qa_session_actor_registry *registry,
                                       qa_actor_id actor) {
  if (!qa_identity_owner_owns_actor(registry->identities, actor)) return false;
  if (actor.slot < 0 || (size_t)actor.slot >= registry->slot_count) return false;
  const slot *current = &registry->slots[actor.slot];
  return current->has_actor && qa_same_actor(current->actor.id, actor);
}

static slot *require_slot(const qa_session_actor_registry *registry, qa_owned_actor actor) {
  if (!qa_session_actor_registry_is_live(registry, actor.id) ||
      actor.id.slot < 0 || (size_t)actor.id.slot >= registry->slot_count ||
      !qa_same_owned_actor(registry->slots[actor.id.slot].actor, actor))
    qa_range_error("Stale or foreign actor authority");
  return &registry->slots[actor.id.slot];
}

void qa_session_actor_registry_assert_owned(const qa_session_actor_registry *registry,
                                            qa_owned_actor actor) {
  (void)require_slot(registry, actor);
}

qa_owned_actor qa_session_actor_registry_allocate(qa_session_actor_registry *registry,
                                                  qa_provider_id owner,
                                                  const char *definition) {
  if (registry->closed) qa_error("Actor registry is closed");
  size_t index = registry->slot_count;
  for (size_t i = 0; i < registry->slot_count; i++) {
    if (!registry->slots[i].has_actor) { index = i; break; }
  }
  if (index == registry->slot_count) {
    if ((int64_t)index >= registry->capacity) qa_range_error("Actor registry is full");
    push_slot(registry);
  }
  slot *value = &registry->slots[index];
  value->generation = qa_next_actor_generation(registry->session, (int64_t)index,
                                               value->generation);
  qa_actor_id id = qa_identity_owner_actor(registry->identities, (int64_t)index,
                                           value->generation);
  qa_owned_actor actor = qa_identity_owner_owned_actor(registry->identities, id, owner);
  value->actor = actor;
  value->has_actor = true;
  value->definition = definition;
  value->observation.id = actor.id;
  value->observation.owner = actor.owner;
  value->observation.definition = definition;
  value->has_observation = true;
  registry->revision++;
  return actor;
}

void qa_session_actor_registry_bind_source(qa_session_actor_registry *registry,
                                           qa_owned_actor actor, qa_provider_id provider,
                                           int64_t source_slot) {
  if (registry->closed) qa_error("Actor registry is closed");
  if (source_slot < 0 || source_slot > QA_MAX_SAFE_INTEGER)
    qa_range_error("Source slot must be a nonnegative safe integer");
  slot *value = require_slot(registry, actor);
  if (value->has_source) qa_range_error("Actor already has a source binding");
  for (size_t i = 0; i < registry->slot_count; i++) {
    const slot *other = &registry->slots[i];
    if (other->has_actor && other->has_source &&
        str_eq(other->source_provider, provider) && other->source_slot == source_slot)
      qa_range_error("Source slot %s/%lld is occupied", provider, (long long)source_slot);
  }
  value->has_source = true;
  value->source_provider = provider;
  value->source_slot = source_slot;
  registry->revision++;
}

qa_owned_actor qa_session_actor_registry_allocate_at_source(
    qa_session_actor_registry *registry, qa_provider_id owner, int64_t source_slot,
    const char *definition) {
  qa_owned_actor actor = qa_session_actor_registry_allocate(registry, owner, definition);
  qa_session_actor_registry_bind_source(registry, actor, owner, source_slot);
  return actor;
}

bool qa_session_actor_registry_at_source(const qa_session_actor_registry *registry,
                                         qa_provider_id owner, int64_t slot_value,
                                         qa_owned_actor *out) {
  for (size_t i = 0; i < registry->slot_count; i++) {
    const slot *other = &registry->slots[i];
    if (other->has_actor && other->has_source &&
        str_eq(other->source_provider, owner) && other->source_slot == slot_value) {
      *out = other->actor;
      return true;
    }
  }
  return false;
}

qa_actor_source_result qa_session_actor_registry_source_of(
    const qa_session_actor_registry *registry, qa_actor_id actor) {
  qa_actor_source_result result;
  result.has_value = false;
  result.provider = NULL;
  result.slot = 0;
  if (!qa_session_actor_registry_is_live(registry, actor)) return result;
  const slot *value = &registry->slots[actor.slot];
  if (value->has_source) {
    result.has_value = true;
    result.provider = value->source_provider;
    result.slot = value->source_slot;
  }
  return result;
}

static restored_entry *find_restored(const qa_session_actor_registry *registry,
                                     int64_t saved_slot) {
  for (size_t i = 0; i < registry->restored_count; i++) {
    if (registry->restored[i].saved_slot == saved_slot) return &registry->restored[i];
  }
  return NULL;
}
/* Map.get(slot) with a number key: a fraction, NaN or out-of-range slot matches no
 * integral key (a C cast would truncate onto one, or be undefined). */
static restored_entry *find_restored_number(const qa_session_actor_registry *registry,
                                            double saved_slot) {
  if (!(saved_slot == trunc(saved_slot) && fabs(saved_slot) <= 9007199254740991.0)) return NULL;
  return find_restored(registry, (int64_t)saved_slot);
}
static void set_restored(qa_session_actor_registry *registry, int64_t saved_slot,
                         int64_t saved_generation, qa_owned_actor actor) {
  restored_entry *existing = find_restored(registry, saved_slot);
  if (existing != NULL) {
    existing->saved_generation = saved_generation;
    existing->actor = actor;
    return;
  }
  if (registry->restored_count == registry->restored_capacity) {
    registry->restored_capacity = registry->restored_capacity != 0 ? registry->restored_capacity * 2 : 8;
    registry->restored = xrealloc(registry->restored,
                                  registry->restored_capacity * sizeof *registry->restored);
  }
  registry->restored[registry->restored_count].saved_slot = saved_slot;
  registry->restored[registry->restored_count].saved_generation = saved_generation;
  registry->restored[registry->restored_count].actor = actor;
  registry->restored_count++;
}
static void remove_restored_for_actor(qa_session_actor_registry *registry, qa_owned_actor actor) {
  for (size_t i = 0; i < registry->restored_count; i++) {
    if (qa_same_owned_actor(registry->restored[i].actor, actor)) {
      memmove(&registry->restored[i], &registry->restored[i + 1],
              (registry->restored_count - i - 1) * sizeof *registry->restored);
      registry->restored_count--;
      return;
    }
  }
}

static char *dup_key(const char *prefix, double slot_value, double generation) {
  char buffer[256];
  if (prefix != NULL) (void)snprintf(buffer, sizeof buffer, "%s:%s/%s", prefix, QA_JS_NUMBER(slot_value), QA_JS_NUMBER(generation));
  else (void)snprintf(buffer, sizeof buffer, "%s/%s", QA_JS_NUMBER(slot_value), QA_JS_NUMBER(generation));
  char *key = malloc(strlen(buffer) + 1);
  if (key == NULL) abort();
  strcpy(key, buffer);
  return key;
}
static bool find_saved_ref(const qa_session_actor_registry *registry, const char *key,
                           qa_actor_id *out) {
  for (size_t i = 0; i < registry->saved_ref_count; i++) {
    if (strcmp(registry->saved_refs[i].key, key) == 0) { *out = registry->saved_refs[i].value; return true; }
  }
  return false;
}
/* Map.set: an existing key keeps its position and takes the new value
 * (rebindRestoredSource replaces the identity restore recorded). */
static void set_saved_ref(qa_session_actor_registry *registry, char *key, qa_actor_id value) {
  for (size_t i = 0; i < registry->saved_ref_count; i++) {
    if (strcmp(registry->saved_refs[i].key, key) == 0) {
      registry->saved_refs[i].value = value;
      free(key);
      return;
    }
  }
  if (registry->saved_ref_count == registry->saved_ref_capacity) {
    registry->saved_ref_capacity = registry->saved_ref_capacity != 0 ? registry->saved_ref_capacity * 2 : 8;
    registry->saved_refs = xrealloc(registry->saved_refs,
                                    registry->saved_ref_capacity * sizeof *registry->saved_refs);
  }
  registry->saved_refs[registry->saved_ref_count].key = key;
  registry->saved_refs[registry->saved_ref_count].value = value;
  registry->saved_ref_count++;
}

typedef struct release_call {
  qa_actor_release_fn fn;
  void *ctx;
  qa_owned_actor actor;
} release_call;
static void run_release_call(void *arg) {
  release_call *call = arg;
  call->fn(call->ctx, call->actor);
}
void qa_session_actor_registry_release(qa_session_actor_registry *registry,
                                       qa_owned_actor actor) {
  slot *value = require_slot(registry, actor);
  value->has_actor = false;
  value->has_observation = false;
  value->has_source = false;
  value->generation++;
  registry->revision++;
  remove_restored_for_actor(registry, actor);
  /* Snapshot subscriptions: callbacks may subscribe/unsubscribe. */
  size_t count = registry->release_count;
  release_sub *snapshot = NULL;
  if (count != 0) {
    snapshot = malloc(count * sizeof *snapshot);
    if (snapshot == NULL) abort();
    memcpy(snapshot, registry->releases, count * sizeof *snapshot);
  }
  /* Every callback runs; failures are collected into one AggregateError. */
  size_t errors = 0;
  for (size_t i = 0; i < count; i++) {
    if (!snapshot[i].active) continue;
    release_call call = {snapshot[i].fn, snapshot[i].ctx, actor};
    qa_caught_error caught;
    if (!qa_try(run_release_call, &call, &caught)) errors++;
  }
  free(snapshot);
  if (errors > 0) qa_raise("AggregateError", 0, 0, "Actor release callbacks failed");
}

qa_actor_observation_result qa_session_actor_registry_observe(
    const qa_session_actor_registry *registry, qa_actor_id actor) {
  qa_actor_observation_result result;
  result.has_value = false;
  memset(&result.value, 0, sizeof result.value);
  if (!qa_session_actor_registry_is_live(registry, actor)) return result;
  const slot *value = &registry->slots[actor.slot];
  if (!value->has_actor || !value->has_observation) return result;
  result.has_value = true;
  result.value = value->observation;
  return result;
}

bool qa_session_actor_registry_resolve_owned(const qa_session_actor_registry *registry,
                                             qa_actor_id actor, qa_owned_actor *out) {
  if (!qa_session_actor_registry_is_live(registry, actor)) return false;
  *out = registry->slots[actor.slot].actor;
  return true;
}

const qa_owned_actor *qa_session_actor_registry_owned_by(
    qa_session_actor_registry *registry, qa_provider_id owner, size_t *out_count) {
  size_t count = 0;
  for (size_t i = 0; i < registry->slot_count; i++) {
    if (registry->slots[i].has_actor && str_eq(registry->slots[i].actor.owner, owner)) count++;
  }
  if (count > registry->scratch_owned_capacity) {
    registry->scratch_owned = xrealloc(registry->scratch_owned,
                                       count * sizeof *registry->scratch_owned);
    registry->scratch_owned_capacity = count;
  }
  size_t at = 0;
  for (size_t i = 0; i < registry->slot_count; i++) {
    if (registry->slots[i].has_actor && str_eq(registry->slots[i].actor.owner, owner))
      registry->scratch_owned[at++] = registry->slots[i].actor;
  }
  *out_count = count;
  return registry->scratch_owned;
}

const qa_actor_observation *qa_session_actor_registry_observations(
    qa_session_actor_registry *registry, size_t *out_count) {
  size_t count = 0;
  for (size_t i = 0; i < registry->slot_count; i++) {
    if (registry->slots[i].has_observation && registry->slots[i].has_actor) count++;
  }
  if (count > registry->scratch_obs_capacity) {
    registry->scratch_obs = xrealloc(registry->scratch_obs, count * sizeof *registry->scratch_obs);
    registry->scratch_obs_capacity = count;
  }
  size_t at = 0;
  for (size_t i = 0; i < registry->slot_count; i++) {
    if (registry->slots[i].has_observation && registry->slots[i].has_actor)
      registry->scratch_obs[at++] = registry->slots[i].observation;
  }
  *out_count = count;
  return registry->scratch_obs;
}

const qa_actor_slot_checkpoint *qa_session_actor_registry_checkpoint(
    qa_session_actor_registry *registry, size_t *out_count) {
  size_t length = registry->slot_count;
  for (size_t i = 0; i < registry->generations->count; i++) {
    size_t needed = (size_t)registry->generations->slots[i] + 1;
    if (needed > length) length = needed;
  }
  if (length > registry->scratch_checkpoints_capacity) {
    registry->scratch_checkpoints = xrealloc(
        registry->scratch_checkpoints, length * sizeof *registry->scratch_checkpoints);
    registry->scratch_checkpoints_capacity = length;
  }
  for (size_t index = 0; index < length; index++) {
    qa_actor_slot_checkpoint *out = &registry->scratch_checkpoints[index];
    memset(out, 0, sizeof *out);
    out->id.slot = (double)index;
    const slot *value = index < registry->slot_count ? &registry->slots[index] : NULL;
    if (value == NULL || !value->has_actor) {
      bool found = false;
      int64_t stored = gen_get(registry->generations, (int64_t)index, &found);
      int64_t generation = value != NULL ? value->generation : 0;
      if (found && stored > generation) generation = stored;
      out->id.generation = (double)generation;
      out->lifetime_is_active = false;
      out->freed_at = NULL;
    } else {
      out->id.generation = (double)value->generation;
      out->lifetime_is_active = true;
      out->owner = value->actor.owner;
      out->definition = value->definition;
    }
  }
  *out_count = length;
  return registry->scratch_checkpoints;
}

const qa_source_actor_checkpoint *qa_session_actor_registry_source_checkpoint(
    qa_session_actor_registry *registry, size_t *out_count) {
  size_t count = 0;
  for (size_t i = 0; i < registry->slot_count; i++) {
    if (registry->slots[i].has_actor && registry->slots[i].has_source) count++;
  }
  if (count > registry->scratch_sources_capacity) {
    registry->scratch_sources = xrealloc(registry->scratch_sources,
                                         count * sizeof *registry->scratch_sources);
    registry->scratch_sources_capacity = count;
  }
  size_t at = 0;
  for (size_t i = 0; i < registry->slot_count; i++) {
    const slot *value = &registry->slots[i];
    if (value->has_actor && value->has_source) {
      registry->scratch_sources[at].provider = value->source_provider;
      registry->scratch_sources[at].source_slot = (double)value->source_slot;
      registry->scratch_sources[at].actor.slot = (double)value->actor.id.slot;
      registry->scratch_sources[at].actor.generation = (double)value->actor.id.generation;
      at++;
    }
  }
  *out_count = count;
  return registry->scratch_sources;
}

bool qa_session_actor_registry_resolve_saved(const qa_session_actor_registry *registry,
                                             qa_saved_actor_id saved, qa_owned_actor *out) {
  restored_entry *restored = find_restored_number(registry, saved.slot);
  if (restored != NULL) {
    if ((double)restored->saved_generation == saved.generation &&
        qa_session_actor_registry_is_live(registry, restored->actor.id)) {
      *out = restored->actor;
      return true;
    }
    return false;
  }
  /* this.slots[saved.slot]: a fraction or NaN reads undefined. */
  if (!(saved.slot >= 0 && saved.slot < (double)registry->slot_count) || saved.slot != trunc(saved.slot))
    return false;
  const slot *value = &registry->slots[(size_t)saved.slot];
  if (value->has_actor && (double)value->actor.id.generation == saved.generation) {
    *out = value->actor;
    return true;
  }
  return false;
}

void qa_session_actor_registry_rebind_restored_source(qa_session_actor_registry *registry,
                                                      qa_provider_id provider) {
  if (registry->closed) qa_error("Actor registry is closed");
  size_t matching = 0;
  for (size_t i = 0; i < registry->restored_source_count; i++) {
    if (str_eq(registry->restored_sources[i].value.provider, provider)) matching++;
  }
  if (matching == 0) qa_range_error("Source has no pending checkpoint reconstruction");
  for (size_t i = 0; i < registry->restored_source_count; i++) {
    qa_source_actor_checkpoint source = registry->restored_sources[i].value;
    if (!str_eq(source.provider, provider)) continue;
    if (find_restored_number(registry, source.actor.slot) != NULL)
      qa_range_error("Saved source actor has not been retired before reconstruction");
    qa_owned_actor actor;
    if (!qa_session_actor_registry_at_source(registry, provider, (int64_t)source.source_slot, &actor))
      qa_range_error("Reconstructed source is missing saved slot %s/%s", provider, QA_JS_NUMBER(source.source_slot));
    for (size_t j = 0; j < registry->restored_count; j++) {
      if (qa_same_owned_actor(registry->restored[j].actor, actor))
        qa_range_error("Reconstructed actor already has a saved identity");
    }
  }
  /* Apply after full validation, then drop the provider's rows. */
  for (size_t i = 0; i < registry->restored_source_count; i++) {
    qa_source_actor_checkpoint source = registry->restored_sources[i].value;
    if (!str_eq(source.provider, provider)) continue;
    qa_owned_actor actor;
    (void)qa_session_actor_registry_at_source(registry, provider, (int64_t)source.source_slot, &actor);
    set_restored(registry, (int64_t)source.actor.slot, (int64_t)source.actor.generation, actor);
    set_saved_ref(registry, dup_key(NULL, source.actor.slot, source.actor.generation), actor.id);
  }
  size_t keep = 0;
  for (size_t i = 0; i < registry->restored_source_count; i++) {
    if (!str_eq(registry->restored_sources[i].value.provider, provider))
      registry->restored_sources[keep++] = registry->restored_sources[i];
  }
  registry->restored_source_count = keep;
}

static bool is_safe_nonnegative(double value) {
  return value >= 0 && value <= 9007199254740991.0 && value == (double)(int64_t)value;
}

qa_actor_id qa_session_actor_registry_reference_saved(qa_session_actor_registry *registry,
                                                      qa_saved_actor_id saved,
                                                      const char *domain) {
  if (domain != NULL && strcmp(domain, "current") == 0) {
    const slot *value = (saved.slot >= 0 && (size_t)saved.slot < registry->slot_count)
                            ? &registry->slots[(size_t)saved.slot] : NULL;
    if (!is_safe_nonnegative(saved.slot) || !is_safe_nonnegative(saved.generation) ||
        value == NULL || saved.generation > (double)value->generation ||
        (!value->has_actor && saved.generation == (double)value->generation))
      qa_range_error("Invalid current actor history reference");
    if (value->has_actor && (double)value->actor.id.generation == saved.generation)
      return value->actor.id;
    char *key = dup_key("current", saved.slot, saved.generation);
    qa_actor_id existing;
    if (find_saved_ref(registry, key, &existing)) { free(key); return existing; }
    qa_actor_id reference = qa_identity_owner_actor(registry->identities, (int64_t)saved.slot,
                                                    (int64_t)saved.generation);
    set_saved_ref(registry, key, reference);
    return reference;
  }
  char *key = dup_key(NULL, saved.slot, saved.generation);
  qa_actor_id existing;
  if (find_saved_ref(registry, key, &existing)) { free(key); return existing; }
  const qa_actor_slot_checkpoint *history =
      (saved.slot >= 0 && (size_t)saved.slot < registry->history_count)
          ? &registry->history[(size_t)saved.slot] : NULL;
  if (!is_safe_nonnegative(saved.slot) || !is_safe_nonnegative(saved.generation) ||
      history == NULL || saved.generation > history->id.generation ||
      (!history->lifetime_is_active && saved.generation == history->id.generation)) {
    free(key);
    qa_range_error("Invalid historical actor checkpoint reference");
  }
  int64_t generation = registry->history_base[(size_t)saved.slot] + (int64_t)saved.generation;
  qa_actor_id reference = qa_identity_owner_actor(registry->identities, (int64_t)saved.slot,
                                                  generation);
  set_saved_ref(registry, key, reference);
  return reference;
}

qa_session_actor_registry *qa_session_actor_registry_restore(
    const qa_identity_owner *identities, const qa_actor_slot_checkpoint *checkpoints,
    size_t checkpoint_count, const qa_source_actor_checkpoint *sources, size_t source_count,
    int64_t capacity) {
  qa_session_actor_registry *registry =
      qa_session_actor_registry_create(identities, capacity);
  if (registry == NULL) return NULL;
  if ((int64_t)checkpoint_count > capacity)
    qa_range_error("Actor checkpoint exceeds capacity");
  registry->history = xrealloc(NULL, (checkpoint_count != 0 ? checkpoint_count : 1) * sizeof *registry->history);
  registry->history_base = xrealloc(NULL, (checkpoint_count != 0 ? checkpoint_count : 1) * sizeof *registry->history_base);
  registry->history_count = checkpoint_count;
  for (size_t index = 0; index < checkpoint_count; index++) {
    const qa_actor_slot_checkpoint *checkpoint = &checkpoints[index];
    if (checkpoint->id.slot != (double)index || !is_safe_nonnegative(checkpoint->id.generation))
      qa_range_error("Invalid actor slot checkpoint");
    bool found = false;
    int64_t base = gen_get(registry->generations, (int64_t)index, &found);
    if (!found) base = 0;
    int64_t restored_generation = base + (int64_t)checkpoint->id.generation;
    if (restored_generation < 0 || restored_generation >= QA_MAX_SAFE_INTEGER)
      qa_range_error("Restored actor generation is exhausted");
    registry->history[index] = *checkpoint;
    registry->history_base[index] = base;
    int64_t generation = !checkpoint->lifetime_is_active
                             ? restored_generation
                             : qa_next_actor_generation(registry->session, (int64_t)index,
                                                        restored_generation);
    slot *value = push_slot(registry);
    value->generation = generation;
    value->definition = !checkpoint->lifetime_is_active ? "world:free" : checkpoint->definition;
    if (checkpoint->lifetime_is_active) {
      qa_actor_id id = qa_identity_owner_actor(identities, (int64_t)index, generation);
      qa_owned_actor actor = qa_identity_owner_owned_actor(identities, id, checkpoint->owner);
      value->actor = actor;
      value->has_actor = true;
      value->observation.id = actor.id;
      value->observation.owner = actor.owner;
      value->observation.definition = checkpoint->definition;
      value->has_observation = true;
      registry->revision++;
      set_restored(registry, (int64_t)index, (int64_t)checkpoint->id.generation, actor);
      set_saved_ref(registry, dup_key(NULL, (double)index, checkpoint->id.generation), actor.id);
    } else {
      gen_set(registry->generations, (int64_t)index, generation);
    }
  }
  for (size_t i = 0; i < source_count; i++) {
    const qa_source_actor_checkpoint *source = &sources[i];
    restored_entry *restored = find_restored_number(registry, source->actor.slot);
    qa_owned_actor actor;
    bool have_actor = restored != NULL && (double)restored->saved_generation == source->actor.generation;
    if (have_actor) actor = restored->actor;
    if (!have_actor || !str_eq(actor.owner, source->provider) ||
        !is_safe_nonnegative(source->source_slot))
      qa_range_error("Invalid source actor checkpoint");
    slot *value = require_slot(registry, actor);
    qa_owned_actor occupied;
    if (qa_session_actor_registry_at_source(registry, source->provider,
                                            (int64_t)source->source_slot, &occupied) ||
        value->has_source)
      qa_range_error("Duplicate source actor binding");
    value->has_source = true;
    value->source_provider = source->provider;
    value->source_slot = (int64_t)source->source_slot;
    if (registry->restored_source_count == registry->restored_source_capacity) {
      registry->restored_source_capacity =
          registry->restored_source_capacity != 0 ? registry->restored_source_capacity * 2 : 8;
      registry->restored_sources = xrealloc(
          registry->restored_sources,
          registry->restored_source_capacity * sizeof *registry->restored_sources);
    }
    registry->restored_sources[registry->restored_source_count++].value = *source;
    registry->revision++;
  }
  return registry;
}

int64_t qa_session_actor_registry_on_release(qa_session_actor_registry *registry,
                                             qa_actor_release_fn fn, void *ctx) {
  if (registry->release_count == registry->release_capacity) {
    registry->release_capacity = registry->release_capacity != 0 ? registry->release_capacity * 2 : 8;
    registry->releases = xrealloc(registry->releases,
                                  registry->release_capacity * sizeof *registry->releases);
  }
  int64_t id = ++registry->next_release_id;
  registry->releases[registry->release_count].id = id;
  registry->releases[registry->release_count].fn = fn;
  registry->releases[registry->release_count].ctx = ctx;
  registry->releases[registry->release_count].active = true;
  registry->release_count++;
  return id;
}

void qa_session_actor_registry_off_release(qa_session_actor_registry *registry, int64_t id) {
  for (size_t i = 0; i < registry->release_count; i++) {
    if (registry->releases[i].id == id) {
      memmove(&registry->releases[i], &registry->releases[i + 1],
              (registry->release_count - i - 1) * sizeof *registry->releases);
      registry->release_count--;
      return;
    }
  }
}

typedef struct close_release {
  qa_session_actor_registry *registry;
  qa_owned_actor actor;
} close_release;
static void run_close_release(void *arg) {
  close_release *call = arg;
  qa_session_actor_registry_release(call->registry, call->actor);
}
void qa_session_actor_registry_close(qa_session_actor_registry *registry) {
  if (registry->closed) return;
  registry->closed = true;
  size_t errors = 0;
  for (size_t i = 0; i < registry->slot_count; i++) {
    if (registry->slots[i].has_actor) {
      close_release call = {registry, registry->slots[i].actor};
      qa_caught_error caught;
      if (!qa_try(run_close_release, &call, &caught)) errors++;
    }
  }
  registry->release_count = 0;
  if (errors > 0) qa_raise("AggregateError", 0, 0, "Actor registry shutdown failed");
}

/* --- contracts adapter --- */
static qa_owned_actor contract_allocate(void *ctx, qa_provider_id owner, const char *definition) {
  return qa_session_actor_registry_allocate(ctx, owner, definition);
}
static void contract_release(void *ctx, qa_owned_actor actor) {
  qa_session_actor_registry_release(ctx, actor);
}
static bool contract_is_live(void *ctx, qa_actor_id actor) {
  return qa_session_actor_registry_is_live(ctx, actor);
}
static qa_actor_observation_result contract_observe(void *ctx, qa_actor_id actor) {
  return qa_session_actor_registry_observe(ctx, actor);
}
static const qa_owned_actor *contract_owned_by(void *ctx, qa_provider_id owner, size_t *out_count) {
  return qa_session_actor_registry_owned_by(ctx, owner, out_count);
}
qa_actor_registry qa_session_actor_registry_as_contract(qa_session_actor_registry *registry) {
  qa_actor_registry table;
  table.ctx = registry;
  table.session = registry->session;
  table.allocate = contract_allocate;
  table.release = contract_release;
  table.is_live = contract_is_live;
  table.observe = contract_observe;
  table.owned_by = contract_owned_by;
  return table;
}
