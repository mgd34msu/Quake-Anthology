/* Port of src/world/session/mod-match.ts. GPL-2.0-or-later. */
#include "qa/world/session/mod_match.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static noreturn void qa_errorf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "Error: ");
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
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

struct source_entry {
  qa_provider_id owner;
  qa_mod_match_resolve_fn resolve;
  void *ctx;
};
struct qa_mod_match_state {
  qa_session_actor_registry *actors;
  qa_mod_match_resolve_fn primary;
  void *primary_ctx;
  struct source_entry *sources;
  size_t source_count;
  size_t source_capacity;
  qa_source_objective_binding *channels;
  size_t channel_count;
  size_t channel_capacity;
  bool closed;
  /* Scratch for read() actor copies and listings. */
  qa_actor_id scratch_carrier;
  qa_actor_id scratch_target;
  qa_source_objective_listing *scratch_listings;
  size_t scratch_listings_capacity;
  qa_mission_gate *scratch_gates;
  size_t scratch_gates_capacity;
  qa_actor_id *scratch_actors;
  size_t scratch_actors_capacity;
};

qa_mod_match_state *qa_mod_match_state_create(
    qa_session_actor_registry *actors, qa_mod_match_resolve_fn primary,
    void *primary_ctx) {
  qa_mod_match_state *state = calloc(1, sizeof *state);
  if (state == NULL) return NULL;
  state->actors = actors;
  state->primary = primary;
  state->primary_ctx = primary_ctx;
  return state;
}
void qa_mod_match_state_destroy(qa_mod_match_state *state) {
  if (state == NULL) return;
  qa_mod_match_state_close(state);
  free(state->sources);
  free(state->channels);
  free(state->scratch_listings);
  free(state->scratch_gates);
  free(state->scratch_actors);
  free(state);
}

qa_source_match_player_result qa_mod_match_state_player(
    qa_mod_match_state *state, qa_actor_id actor) {
  qa_source_match_player_result none = {false, {NULL, NULL, NULL, NULL,
                                                NULL, NULL}};
  if (state->closed ||
      !qa_session_actor_registry_is_live(state->actors, actor))
    return none;
  qa_owned_actor owned;
  bool has_owned =
      qa_session_actor_registry_resolve_owned(state->actors, actor, &owned);
  qa_source_match_player_result player = none;
  if (has_owned) {
    for (size_t i = 0; i < state->source_count; i++)
      if (str_eq(state->sources[i].owner, owned.owner)) {
        player =
            state->sources[i].resolve(state->sources[i].ctx, actor);
        break;
      }
  }
  if (!player.has_value) player = state->primary(state->primary_ctx, actor);
  if (player.has_value &&
      (!has_owned || !str_eq(player.value.owner, owned.owner)))
    qa_errorf("Match player belongs to another original actor owner");
  return player;
}

struct unbind_source {
  qa_mod_match_state *state;
  qa_provider_id owner;
  qa_mod_match_resolve_fn resolve;
  void *ctx;
};
static void unbind_source(void *ctx) {
  struct unbind_source *unbind = ctx;
  qa_mod_match_state *state = unbind->state;
  for (size_t i = 0; i < state->source_count; i++)
    if (str_eq(state->sources[i].owner, unbind->owner) &&
        state->sources[i].resolve == unbind->resolve &&
        state->sources[i].ctx == unbind->ctx) {
      memmove(&state->sources[i], &state->sources[i + 1],
              (state->source_count - i - 1) * sizeof *state->sources);
      state->source_count--;
      break;
    }
  free(unbind);
}
qa_source_match_unbind qa_mod_match_state_bind_source(
    qa_mod_match_state *state, qa_provider_id owner,
    qa_mod_match_resolve_fn resolve, void *resolve_ctx) {
  bool bound = false;
  for (size_t i = 0; i < state->source_count; i++)
    if (str_eq(state->sources[i].owner, owner)) bound = true;
  if (state->closed || bound)
    qa_errorf("Match source %s is already bound or closed", owner);
  if (state->source_count == state->source_capacity) {
    state->source_capacity =
        state->source_capacity != 0 ? state->source_capacity * 2 : 8;
    state->sources = xrealloc(
        state->sources, state->source_capacity * sizeof *state->sources);
  }
  state->sources[state->source_count].owner = owner;
  state->sources[state->source_count].resolve = resolve;
  state->sources[state->source_count].ctx = resolve_ctx;
  state->source_count++;
  struct unbind_source *unbind = malloc(sizeof *unbind);
  if (unbind == NULL) abort();
  unbind->state = state;
  unbind->owner = owner;
  unbind->resolve = resolve;
  unbind->ctx = resolve_ctx;
  qa_source_match_unbind result = {unbind, unbind_source};
  return result;
}

struct unbind_objective {
  qa_mod_match_state *state;
  qa_source_objective_binding binding;
};
static bool same_binding(qa_source_objective_binding a,
                         qa_source_objective_binding b) {
  return a.ctx == b.ctx && a.read == b.read && a.change == b.change &&
         str_eq(a.id, b.id);
}
static void unbind_objective(void *ctx) {
  struct unbind_objective *unbind = ctx;
  qa_mod_match_state *state = unbind->state;
  for (size_t i = 0; i < state->channel_count; i++)
    if (same_binding(state->channels[i], unbind->binding)) {
      memmove(&state->channels[i], &state->channels[i + 1],
              (state->channel_count - i - 1) * sizeof *state->channels);
      state->channel_count--;
      break;
    }
  free(unbind);
}
qa_source_match_unbind qa_mod_match_state_bind_objective(
    qa_mod_match_state *state, qa_source_objective_binding binding) {
  if (state->closed) qa_errorf("Match objective owner is closed");
  for (size_t i = 0; i < state->channel_count; i++)
    if (str_eq(state->channels[i].id, binding.id))
      qa_errorf("Objective %s is owned by %s; %s cannot claim it",
                binding.id, state->channels[i].owner, binding.owner);
  if (state->channel_count == state->channel_capacity) {
    state->channel_capacity =
        state->channel_capacity != 0 ? state->channel_capacity * 2 : 8;
    state->channels = xrealloc(
        state->channels, state->channel_capacity * sizeof *state->channels);
  }
  state->channels[state->channel_count++] = binding;
  struct unbind_objective *unbind = malloc(sizeof *unbind);
  if (unbind == NULL) abort();
  unbind->state = state;
  unbind->binding = binding;
  qa_source_match_unbind result = {unbind, unbind_objective};
  return result;
}

static qa_source_objective_binding *find_binding(qa_mod_match_state *state,
                                                 qa_objective_id id) {
  for (size_t i = 0; i < state->channel_count; i++)
    if (str_eq(state->channels[i].id, id)) return &state->channels[i];
  return NULL;
}

/* TS read(): a detached state with retired actors nulled. Carrier
 * and target point at per-call scratch (or listing scratch). */
static qa_source_objective_state_result read_binding_into(
    qa_mod_match_state *state, qa_source_objective_binding *binding,
    qa_actor_id *carrier_scratch, qa_actor_id *target_scratch) {
  qa_source_objective_state_result result;
  memset(&result, 0, sizeof result);
  qa_source_objective_state value = binding->read(binding->ctx);
  if (state->closed || find_binding(state, binding->id) != binding)
    return result;
  result.has_value = true;
  result.value = value;
  if (value.carrier != NULL &&
      qa_session_actor_registry_is_live(state->actors, *value.carrier)) {
    *carrier_scratch = *value.carrier;
    result.value.carrier = carrier_scratch;
  } else {
    result.value.carrier = NULL;
  }
  if (value.target != NULL &&
      qa_session_actor_registry_is_live(state->actors, *value.target)) {
    *target_scratch = *value.target;
    result.value.target = target_scratch;
  } else {
    result.value.target = NULL;
  }
  return result;
}
static qa_source_objective_state_result read_binding(
    qa_mod_match_state *state, qa_source_objective_binding *binding) {
  return read_binding_into(state, binding, &state->scratch_carrier,
                           &state->scratch_target);
}

qa_source_objective_state_result qa_mod_match_state_objective(
    qa_mod_match_state *state, qa_objective_id id) {
  qa_source_objective_binding *binding = find_binding(state, id);
  qa_source_objective_state_result none;
  memset(&none, 0, sizeof none);
  return binding == NULL ? none : read_binding(state, binding);
}

qa_source_objective_state_result qa_mod_match_state_change_objective(
    qa_mod_match_state *state, qa_objective_id id,
    qa_source_objective_change request) {
  qa_source_objective_binding *binding = find_binding(state, id);
  if (binding == NULL)
    qa_errorf("Objective %s has no admitted source owner", id);
  const qa_actor_id *actors[2] = {request.carrier, request.target};
  for (size_t i = 0; i < 2; i++)
    if (actors[i] != NULL &&
        !qa_session_actor_registry_is_live(state->actors, *actors[i]))
      qa_errorf("Objective change references a retired actor");
  binding->change(binding->ctx, request);
  qa_source_objective_state_result none;
  memset(&none, 0, sizeof none);
  return find_binding(state, id) == binding ? read_binding(state, binding)
                                            : none;
}

const qa_source_objective_listing *qa_mod_match_state_objectives(
    qa_mod_match_state *state, size_t *out_count) {
  if (state->channel_count > state->scratch_listings_capacity) {
    state->scratch_listings_capacity = state->channel_count;
    state->scratch_listings =
        xrealloc(state->scratch_listings, state->scratch_listings_capacity *
                                              sizeof *state->scratch_listings);
  }
  if (state->channel_count * 2 > state->scratch_actors_capacity) {
    state->scratch_actors_capacity = state->channel_count * 2;
    state->scratch_actors =
        xrealloc(state->scratch_actors, state->scratch_actors_capacity *
                                            sizeof *state->scratch_actors);
  }
  size_t count = 0;
  for (size_t i = 0; i < state->channel_count; i++) {
    qa_source_objective_binding *binding = &state->channels[i];
    qa_source_objective_state_result read = read_binding_into(
        state, binding, &state->scratch_actors[count * 2],
        &state->scratch_actors[count * 2 + 1]);
    if (!read.has_value) continue;
    state->scratch_listings[count].id = binding->id;
    state->scratch_listings[count].state = read.value;
    state->scratch_listings[count].bot_goal = binding->bot_goal;
    count++;
  }
  *out_count = count;
  return state->scratch_listings;
}

const qa_mission_gate *qa_mod_match_state_gates(qa_mod_match_state *state,
                                                size_t *out_count) {
  if (state->channel_count > state->scratch_gates_capacity) {
    state->scratch_gates_capacity = state->channel_count;
    state->scratch_gates = xrealloc(
        state->scratch_gates,
        state->scratch_gates_capacity * sizeof *state->scratch_gates);
  }
  size_t count = 0;
  for (size_t i = 0; i < state->channel_count; i++) {
    qa_source_objective_binding *binding = &state->channels[i];
    if (!binding->campaign_gate) continue;
    qa_source_objective_state_result read = read_binding(state, binding);
    if (!read.has_value) continue;
    state->scratch_gates[count].objective = binding->id;
    state->scratch_gates[count].satisfied = read.value.complete;
    count++;
  }
  *out_count = count;
  return state->scratch_gates;
}

void qa_mod_match_state_close(qa_mod_match_state *state) {
  if (state->closed) return;
  state->closed = true;
  state->source_count = 0;
  state->channel_count = 0;
}

static qa_source_match_player_result svc_player(void *ctx,
                                                qa_actor_id actor) {
  return qa_mod_match_state_player(ctx, actor);
}
static qa_source_match_unbind svc_bind_source(
    void *ctx, qa_provider_id owner,
    qa_source_match_player_result (*resolve)(void *, qa_actor_id),
    void *resolve_ctx) {
  return qa_mod_match_state_bind_source(ctx, owner, resolve, resolve_ctx);
}
static qa_source_objective_state_result svc_objective(void *ctx,
                                                      qa_objective_id id) {
  return qa_mod_match_state_objective(ctx, id);
}
static qa_source_objective_state_result svc_change(
    void *ctx, qa_objective_id id, qa_source_objective_change request) {
  return qa_mod_match_state_change_objective(ctx, id, request);
}
static qa_source_match_unbind svc_bind_objective(
    void *ctx, qa_source_objective_binding binding) {
  return qa_mod_match_state_bind_objective(ctx, binding);
}
static const qa_source_objective_listing *svc_objectives(
    void *ctx, size_t *out_count) {
  return qa_mod_match_state_objectives(ctx, out_count);
}
static const qa_mission_gate *svc_gates(void *ctx, size_t *out_count) {
  return qa_mod_match_state_gates(ctx, out_count);
}
qa_source_match_services qa_mod_match_state_as_services(
    qa_mod_match_state *state) {
  qa_source_match_services services = {state,     svc_player, svc_bind_source,
                                       svc_objective, svc_change,
                                       svc_bind_objective, svc_objectives,
                                       svc_gates};
  return services;
}
