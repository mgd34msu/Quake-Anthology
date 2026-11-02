/* Port of src/app/bootstrap/simulation/runtime.ts (public surface,
 * frame-entry guards + ordered turns). The 8.5k-line TS composition
 * root's per-source frame pipeline stages live in the sibling
 * simulation modules; the host bundle runs the constructed pipeline
 * while this module owns the TS step/checkpoint/close guards.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/runtime.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static void assert_open(qa_app_sim_shared_simulation *s) {
  if (s->closed) qa_range_error("Simulation is closed");
}
static void begin_step(qa_app_sim_shared_simulation *s, qa_input_batch input) {
  assert_open(s);
  if (s->stepping || s->checkpoint_in_progress)
    qa_range_error("Simulation step is already running or a checkpoint is active");
  if (!isfinite(input.elapsed_milliseconds) || input.elapsed_milliseconds < 0)
    qa_range_error("Host elapsed time must be finite and nonnegative");
  s->stepping = true;
}
static qa_simulation_output sim_step(void *ctx, qa_input_batch input) {
  qa_app_sim_shared_simulation *s = ctx;
  begin_step(s, input);
  qa_simulation_output output = s->host.step(s->host.ctx, input);
  s->stepping = false;
  return output;
}
static qa_simulation_output sim_step_async(void *ctx, qa_input_batch input,
    void (*progress)(void *progress_ctx, qa_simulation_progress frame), void *progress_ctx) {
  qa_app_sim_shared_simulation *s = ctx;
  /* TS stepAsync falls through to step() for non-Q3-QVM sources,
   * which re-checks the same guards; the host bundle selects. */
  if (s->host.step_async) {
    begin_step(s, input);
    qa_simulation_output output = s->host.step_async(s->host.ctx, input, progress, progress_ctx);
    s->stepping = false;
    return output;
  }
  return sim_step(ctx, input);
}
static qa_save_image sim_checkpoint(void *ctx) {
  qa_app_sim_shared_simulation *s = ctx;
  assert_open(s);
  if (s->stepping || s->checkpoint_in_progress)
    qa_range_error("Save requires a completed frame without pending world travel");
  s->checkpoint_in_progress = true;
  qa_save_image image = s->host.checkpoint(s->host.ctx);
  s->checkpoint_in_progress = false;
  return image;
}
static void sim_close(void *ctx) {
  qa_app_sim_shared_simulation *s = ctx;
  if (s->checkpoint_in_progress)
    qa_range_error("Await checkpoint completion before closing the simulation");
  if (s->closed) return;
  s->closed = true;
  if (s->host.close) s->host.close(s->host.ctx);
}
static qa_app_sim_shared_simulation *create_internal(qa_app_simulation_options options,
    qa_app_sim_shared_simulation_host host, qa_session_actor_registry *actors, qa_session_id session, bool loading) {
  /* TS constructor: selected mods require the async load path. Only
   * the preparedMods length is representable in the C options. */
  if (!loading && options.prepared_mod_count != 0)
    qa_range_error("Selected gameplay mods require asynchronous loadSimulation");
  qa_app_sim_shared_simulation *s = calloc(1, sizeof *s);
  if (!s) qa_range_error("Out of memory");
  s->session = session; s->recipe = options.recipe; s->options = options;
  s->actors = actors; s->host = host;
  s->random = qa_app_sim_source_random_create(options.seed,
      (options.has_q2_guest && options.q2_guest.edition && strcmp(options.q2_guest.edition, "rerelease") == 0)
          ? "q2-rerelease" : "classic");
  return s;
}
qa_app_sim_shared_simulation *qa_app_sim_shared_simulation_create(qa_app_simulation_options options,
    qa_app_sim_shared_simulation_host host, qa_session_actor_registry *actors, qa_session_id session) {
  return create_internal(options, host, actors, session, false);
}
qa_app_sim_shared_simulation *qa_app_sim_load_simulation(qa_app_simulation_options options,
    qa_app_sim_shared_simulation_host host, qa_session_actor_registry *actors, qa_session_id session,
    void (*next_frame)(void *frame_ctx), void *frame_ctx) {
  qa_app_sim_shared_simulation *s = create_internal(options, host, actors, session, true);
  if (options.prepare_rerelease_navigation) {
    bool rerelease = false;
    if (options.has_q2_guest && options.q2_guest.edition && strcmp(options.q2_guest.edition, "rerelease") == 0) rerelease = true;
    for (size_t i = 0; i < options.weapon_behavior_count; i++) (void)i;
    if (rerelease) (void)options.prepare_rerelease_navigation(options.nav_ctx, s);
  }
  if (next_frame) next_frame(frame_ctx);
  return s;
}
void qa_app_sim_shared_simulation_destroy(qa_app_sim_shared_simulation *simulation) {
  if (!simulation) return;
  qa_app_sim_source_random_destroy(simulation->random);
  free(simulation);
}
qa_simulation qa_app_sim_shared_simulation_as_simulation(qa_app_sim_shared_simulation *simulation) {
  qa_simulation out; memset(&out, 0, sizeof out);
  out.ctx = simulation; out.session = simulation->session; out.recipe = simulation->recipe;
  out.step = sim_step; out.step_async = sim_step_async; out.checkpoint = sim_checkpoint; out.close = sim_close;
  return out;
}
qa_simulation_output qa_app_sim_shared_simulation_step(qa_app_sim_shared_simulation *simulation, qa_input_batch input) {
  return sim_step(simulation, input);
}
qa_save_image qa_app_sim_shared_simulation_checkpoint(qa_app_sim_shared_simulation *simulation) {
  return sim_checkpoint(simulation);
}
void qa_app_sim_shared_simulation_close(qa_app_sim_shared_simulation *simulation) { sim_close(simulation); }

typedef struct pending_entry { qa_owned_actor actor; double position[2]; } pending_entry;
struct qa_app_sim_ordered_actor_turns {
  qa_session_actor_registry *actors;
  void (*position_of)(void *ctx, qa_owned_actor actor, double out[2]); void *position_ctx;
  int64_t revision;
  bool has_cursor; double cursor[2];
  pending_entry *pending; size_t pending_count, index;
};
static bool in_visited(const qa_app_sim_actor_visited *v, qa_owned_actor actor) {
  for (size_t i = 0; i < v->count; i++)
    if (qa_same_actor(v->actors[i].id, actor.id) && v->actors[i].owner != NULL && actor.owner != NULL
        && strcmp(v->actors[i].owner, actor.owner) == 0 && v->actors[i].token == actor.token) return true;
  return false;
}
static void add_visited(qa_app_sim_actor_visited *v, qa_owned_actor actor) {
  if (v->count >= v->capacity) {
    v->capacity = v->capacity ? v->capacity * 2 : 16;
    v->actors = realloc(v->actors, v->capacity * sizeof *v->actors);
    if (!v->actors) qa_range_error("Out of memory");
  }
  v->actors[v->count++] = actor;
}
static int pending_cmp(const void *a, const void *b) {
  const pending_entry *l = a, *r = b;
  if (l->position[0] != r->position[0]) return l->position[0] < r->position[0] ? -1 : 1;
  if (l->position[1] != r->position[1]) return l->position[1] < r->position[1] ? -1 : 1;
  return 0;
}
qa_app_sim_ordered_actor_turns *qa_app_sim_ordered_actor_turns_create(qa_session_actor_registry *actors,
    void (*position_of)(void *ctx, qa_owned_actor actor, double out[2]), void *position_ctx) {
  qa_app_sim_ordered_actor_turns *t = calloc(1, sizeof *t);
  if (!t) qa_range_error("Out of memory");
  t->actors = actors; t->position_of = position_of; t->position_ctx = position_ctx; t->revision = -1;
  return t;
}
void qa_app_sim_ordered_actor_turns_destroy(qa_app_sim_ordered_actor_turns *turns) {
  if (!turns) return;
  free(turns->pending); free(turns);
}
bool qa_app_sim_ordered_actor_turns_next(qa_app_sim_ordered_actor_turns *t, qa_app_sim_actor_visited *visited, qa_owned_actor *out) {
  for (;;) {
    if (t->revision != (int64_t)qa_session_actor_registry_revision(t->actors)) {
      t->pending_count = 0;
      size_t count = 0;
      const qa_actor_observation *observations = qa_session_actor_registry_observations(t->actors, &count);
      free(t->pending);
      t->pending = count ? malloc(count * sizeof *t->pending) : NULL;
      for (size_t i = 0; i < count; i++) {
        qa_owned_actor actor;
        if (!qa_session_actor_registry_resolve_owned(t->actors, observations[i].id, &actor)) continue;
        if (in_visited(visited, actor)) continue;
        t->pending[t->pending_count].actor = actor;
        t->position_of(t->position_ctx, actor, t->pending[t->pending_count].position);
        t->pending_count++;
      }
      qsort(t->pending, t->pending_count, sizeof t->pending[0], pending_cmp);
      t->revision = (int64_t)qa_session_actor_registry_revision(t->actors);
      t->index = 0;
    }
    if (t->index >= t->pending_count) return false;
    pending_entry next = t->pending[t->index++];
    if (in_visited(visited, next.actor)) continue;
    if (t->has_cursor && (next.position[0] < t->cursor[0]
        || (next.position[0] == t->cursor[0] && next.position[1] <= t->cursor[1]))) continue;
    t->has_cursor = true; t->cursor[0] = next.position[0]; t->cursor[1] = next.position[1];
    add_visited(visited, next.actor);
    *out = next.actor;
    return true;
  }
}
/* TS step() boundary planning. Sets are exact-double deduped, then
 * sorted ascending, mirroring `new Set<number>` + sort((a,b)=>a-b). */
typedef struct deadline_set { double *values; size_t count, capacity; } deadline_set;
static void deadline_add(deadline_set *set, double value) {
  for (size_t i = 0; i < set->count; i++) if (set->values[i] == value) return;
  if (set->count >= set->capacity) {
    set->capacity = set->capacity ? set->capacity * 2 : 16;
    set->values = realloc(set->values, set->capacity * sizeof *set->values);
    if (!set->values) qa_range_error("Out of memory");
  }
  set->values[set->count++] = value;
}
static bool deadline_has(const deadline_set *set, double value) {
  for (size_t i = 0; i < set->count; i++) if (set->values[i] == value) return true;
  return false;
}
static void deadline_series(deadline_set *set, double first, double interval, double limit) {
  if (interval <= 0) { if (first <= limit) deadline_add(set, first); return; }
  for (double due = first; due <= limit; due += interval) deadline_add(set, due);
}
static int boundary_cmp(const void *a, const void *b) {
  double l = ((const qa_app_sim_frame_boundary *)a)->milliseconds;
  double r = ((const qa_app_sim_frame_boundary *)b)->milliseconds;
  return l < r ? -1 : l > r ? 1 : 0;
}
qa_app_sim_frame_plan qa_app_sim_frame_plan_compute(qa_app_sim_frame_plan_input input) {
  qa_app_sim_frame_plan plan; memset(&plan, 0, sizeof plan);
  bool q3_profile = input.profile.kind == QA_CLOCK_Q3;
  if (input.profile.kind == QA_CLOCK_Q2_CLASSIC) { plan.has_fixed = true; plan.fixed_milliseconds = 100; }
  else if (input.profile.kind == QA_CLOCK_Q2_RERELEASE) { plan.has_fixed = true; plan.fixed_milliseconds = input.profile.frame_milliseconds; }
  else if (q3_profile) { plan.has_fixed = true; plan.fixed_milliseconds = input.profile.server_frame_milliseconds; }
  double now_milliseconds = round(input.time_seconds * 1000);
  plan.map_run = input.settlement_active || (!input.paused && input.elapsed_milliseconds > 0
      && (!plan.has_fixed || input.source_scheduling_milliseconds >= now_milliseconds
          + (q3_profile ? input.profile.server_frame_milliseconds : 0)));
  plan.elapsed_seconds = input.settlement_active ? 0.1
      : !plan.has_fixed ? fmin(0.1, fmax(0.001, input.elapsed_milliseconds / 1000))
      : plan.fixed_milliseconds / 1000;
  double map_start = input.has_selected_q2 ? input.selected_q2_map_milliseconds : now_milliseconds;
  plan.map_start_milliseconds = map_start;
  deadline_set map_deadlines = {0}, deadlines = {0}, q3_deadlines = {0}, weapon_deadlines = {0};
  if (input.settlement_active) deadline_add(&map_deadlines, map_start);
  else if (plan.map_run && q3_profile)
    deadline_series(&map_deadlines, now_milliseconds + input.profile.server_frame_milliseconds,
        input.profile.server_frame_milliseconds, input.source_scheduling_milliseconds);
  else if (plan.map_run && plan.has_fixed) {
    /* Q2 runs one interval ahead; shared-host catch-up bounded at 200ms. */
    if (plan.fixed_milliseconds > 0)
      for (double due = map_start; due <= input.source_scheduling_milliseconds && due - map_start < 200;
          due += plan.fixed_milliseconds) deadline_add(&map_deadlines, due + plan.fixed_milliseconds);
    else deadline_add(&map_deadlines, map_start + plan.fixed_milliseconds);
  } else if (plan.map_run) deadline_add(&map_deadlines, map_start + plan.elapsed_seconds * 1000);
  double map_end = map_start;
  for (size_t i = 0; i < map_deadlines.count; i++) if (i == 0 || map_deadlines.values[i] > map_end) map_end = map_deadlines.values[i];
  plan.map_end_milliseconds = map_end;
  if (map_deadlines.count == 0) deadline_add(&deadlines, map_end);
  else for (size_t i = 0; i < map_deadlines.count; i++) deadline_add(&deadlines, map_deadlines.values[i]);
  if (!input.paused && input.has_selected_q3) {
    deadline_series(&q3_deadlines, input.selected_q3_next, input.selected_q3_server_frame_milliseconds, input.selected_milliseconds);
    for (size_t i = 0; i < q3_deadlines.count; i++) deadline_add(&deadlines, q3_deadlines.values[i]);
  }
  if (plan.map_run) {
    if (input.has_selected_q2) {
      deadline_series(&weapon_deadlines, input.selected_q2_next_milliseconds,
          input.selected_q2_interval_milliseconds, map_end);
      for (size_t i = 0; i < weapon_deadlines.count; i++) deadline_add(&deadlines, weapon_deadlines.values[i]);
    }
    for (size_t i = 0; i < input.extra_clock_count; i++)
      deadline_series(&deadlines, input.extra_clocks[i].first_due, input.extra_clocks[i].interval, map_end);
  }
  plan.boundary_count = deadlines.count;
  plan.boundaries = deadlines.count ? malloc(deadlines.count * sizeof *plan.boundaries) : NULL;
  if (deadlines.count && !plan.boundaries) qa_range_error("Out of memory");
  for (size_t i = 0; i < deadlines.count; i++) {
    double ms = deadlines.values[i];
    plan.boundaries[i] = (qa_app_sim_frame_boundary){ms, deadline_has(&map_deadlines, ms),
        deadline_has(&weapon_deadlines, ms), deadline_has(&q3_deadlines, ms)};
  }
  qsort(plan.boundaries, plan.boundary_count, sizeof plan.boundaries[0], boundary_cmp);
  free(map_deadlines.values); free(deadlines.values); free(q3_deadlines.values); free(weapon_deadlines.values);
  return plan;
}
void qa_app_sim_frame_plan_free(qa_app_sim_frame_plan *plan) {
  if (!plan) return;
  free(plan->boundaries); plan->boundaries = NULL; plan->boundary_count = 0;
}

/* --- Concrete SharedSimulation assembly. Mirrors the TS constructor
 * order (clock, random, actors, callbacks, scene, physics, inventory,
 * events, combat, scheduler) and the step() boundary tail
 * (scheduler.run during-physics, physics.step, commitAttachments,
 * frame-exit clock staging) over the landed sibling modules. The TS
 * createSource source games and everything hanging off them
 * (selected arsenals/monsters, guests, mods, grapple/grenades,
 * restore) have no concrete C source in this slice and abort at
 * build/step boundaries instead of being silently skipped. --- */
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/simulation/provider_frames.h"
#include "qa/content/mounts.h"
#include "qa/formats/bsp_kind.h"
#include "qa/formats/q1_map.h"
#include "qa/formats/q2_map.h"
#include "qa/persistence/save_image.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/scheduler.h"
#include "qa/world/session/clocks.h"
static noreturn void qa_error(const char *format, ...) {
  va_list args; va_start(args, format);
  fputs("Error: ", stderr); vfprintf(stderr, format, args); fputc('\n', stderr);
  va_end(args); abort();
}
typedef struct concrete_player {
  qa_app_sim_movement_player *player;
  qa_client_id client;
} concrete_player;
struct qa_app_sim_concrete_simulation {
  qa_app_simulation_options options;
  qa_session_actor_registry *actors;
  qa_session_id session;
  qa_app_sim_shared_simulation *owner;
  qa_q1_map *owned_q1_map;
  const qa_q1_map *q1_map;
  qa_decoded_world decoded;
  qa_scene_snapshot_world scene_world;
  qa_shared_scene_queries *scene;
  qa_actor_callback_table *callbacks;
  qa_app_sim_shared_physics *physics;
  qa_shared_inventory_table *inventory;
  qa_app_sim_simulation_events *events;
  qa_gameplay_authority *combat;
  qa_frame_scheduler *scheduler;
  qa_scheduler_clock *scheduler_clocks; size_t scheduler_clock_count;
  qa_source_clock clock;
  qa_frame_context source_frame;
  qa_clock_profile world_clock;
  qa_app_sim_source_random *random; bool owns_random;
  qa_owned_actor world_actor; bool has_world_actor;
  concrete_player *players; size_t player_count, player_capacity;
  double host_milliseconds, source_scheduling_milliseconds, selected_milliseconds;
  double presentation_sequence;
  bool closed;
  /* Snapshot scratch (valid until the next step). */
  qa_actor_observation *snap_actors;
  qa_body_snapshot *snap_bodies; size_t snap_body_count;
  qa_inventory_snapshot *snap_inventories; size_t snap_inventory_count;
  qa_actor_configuration *snap_configurations; size_t snap_configuration_count;
  /* Checkpoint storage (valid until the next checkpoint or close). */
  qa_save_clock ck_clock; qa_save_random ck_random; double *ck_random_words;
  qa_actor_slot_checkpoint *ck_actors;
  qa_body_checkpoint *ck_bodies; size_t ck_body_count;
  qa_combat_checkpoint *ck_combat; size_t ck_combat_count;
  qa_inventory_checkpoint *ck_inventories; size_t ck_inventory_count;
  qa_actor_configuration_checkpoint *ck_configurations; size_t ck_configuration_count;
  qa_think_checkpoint *ck_thinks; size_t ck_think_count;
  qa_provider_checkpoint ck_providers[2]; uint8_t *ck_simulation_bytes;
};
static bool c_is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static double c_seconds(qa_source_time time) {
  return time.kind == QA_SOURCE_TIME_MILLISECONDS ? time.value / 1000 : time.value;
}
static void *c_grow(void *ptr, size_t *capacity, size_t count, size_t size) {
  if (count <= *capacity) return ptr;
  size_t next = *capacity ? *capacity * 2 : 16;
  while (next < count) next *= 2;
  void *grown = realloc(ptr, next * size);
  if (!grown) qa_range_error("Out of memory");
  *capacity = next;
  return grown;
}
static concrete_player *c_player_for(qa_app_sim_concrete_simulation *c, qa_actor_id actor) {
  for (size_t i = 0; i < c->player_count; i++)
    if (qa_same_actor(c->players[i].player->actor.id, actor)) return &c->players[i];
  return NULL;
}
/* TS sourcePosition/sourceOrder. */
static void c_source_position(qa_app_sim_concrete_simulation *c, qa_actor_id actor, double out[2]) {
  qa_actor_source_result source = qa_session_actor_registry_source_of(c->actors, actor);
  const char *provider = source.has_value ? source.provider : NULL;
  if (!provider) {
    qa_actor_observation_result observed = qa_session_actor_registry_observe(c->actors, actor);
    provider = observed.has_value ? observed.value.owner : "world:unknown";
  }
  double slot = source.has_value ? (double)source.slot : (double)actor.slot;
  if (c->options.recipe.ordering.kind == QA_FRAME_ORDERING_MIXED) {
    double rank = -1;
    for (size_t i = 0; i < c->options.recipe.ordering.provider_count; i++)
      if (c_is(c->options.recipe.ordering.providers[i], provider)) { rank = (double)i; break; }
    out[0] = rank; out[1] = slot;
  } else if (!c_is(provider, c->options.recipe.map.entities.provider)) {
    out[0] = 1; out[1] = (double)actor.slot;
  } else { out[0] = 0; out[1] = slot; }
}
static double c_source_order(void *ctx, qa_actor_id a, qa_actor_id b) {
  double left[2], right[2];
  c_source_position(ctx, a, left); c_source_position(ctx, b, right);
  return left[0] != right[0] ? left[0] - right[0] : left[1] - right[1];
}
static bool c_world_actor(void *ctx, qa_actor_id *out) {
  qa_app_sim_concrete_simulation *c = ctx;
  if (!c->has_world_actor) return false;
  *out = c->world_actor.id; return true;
}
static qa_source_time c_now(void *ctx) { return ((qa_app_sim_concrete_simulation *)ctx)->source_frame.time; }
static bool c_client_for(void *ctx, qa_actor_id actor, qa_client_id *out) {
  concrete_player *player = c_player_for(ctx, actor);
  if (!player) return false;
  *out = player->client; return true;
}
static bool c_source_slot(void *ctx, qa_actor_id actor, double *out) {
  qa_actor_source_result source = qa_session_actor_registry_source_of(
      ((qa_app_sim_concrete_simulation *)ctx)->actors, actor);
  if (!source.has_value) return false;
  *out = (double)source.slot; return true;
}
static bool c_resource_for(void *ctx, qa_content_id content, const char *path, qa_resource_id *out) {
  qa_app_sim_concrete_simulation *c = ctx; (void)content;
  if (!c->options.mounts || !path) return false;
  const qa_resolved_resource_reference *reference = NULL;
  if (!qa_content_mounted_resolve((qa_content_mounted *)c->options.mounts, path, &reference)
      || !reference) return false;
  *out = reference->id; return true;
}
static void c_emit_super(void *ctx, bool has_owner, qa_presentation_owner owner, qa_content_id content,
    qa_app_sim_source_event_input source, qa_source_time time, bool has_recipient, qa_actor_id recipient,
    qa_app_sim_presentation_event *out) {
  qa_app_sim_concrete_simulation *c = ctx;
  (void)time;
  memset(out, 0, sizeof *out);
  out->kind = source.kind; out->content = content;
  out->has_owner = has_owner; out->owner = owner;
  out->has_recipient = has_recipient; out->recipient = recipient;
  out->sequence = c->presentation_sequence++;
}
/* TS SharedPhysics event option: land/water sounds through events. */
static void c_physics_event(void *ctx, qa_app_sim_physics_event event) {
  qa_app_sim_concrete_simulation *c = ctx;
  if (qa_app_sim_provider_family(c->options.recipe.map.entities.provider) != QA_GAME_FAMILY_Q1) return;
  qa_q1_event sound; memset(&sound, 0, sizeof sound);
  sound.kind = "sound"; sound.has_actor = true; sound.actor = event.actor;
  sound.path = c_is(event.kind, "land") ? "demon/dland2.wav" : "misc/h2ohit1.wav";
  sound.sound_channel = "auto"; sound.volume = 1; sound.attenuation = 1;
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  source.kind = "q1"; source.q1 = &sound;
  qa_presentation_owner owner; memset(&owner, 0, sizeof owner);
  (void)qa_app_sim_simulation_events_emit_owned(c->events, false, owner,
      c->options.recipe.map.entities.content, source, false, c->source_frame.time,
      false, event.actor);
}
static void c_think_call(void *ctx, qa_owned_actor self, qa_frame_context frame) {
  (void)qa_actor_callback_table_think(((qa_app_sim_concrete_simulation *)ctx)->callbacks, self, frame);
}
static qa_think_callback_result c_scheduler_resolve(void *ctx, qa_provider_id provider,
                                                    qa_callback_id callback) {
  (void)provider;
  qa_think_callback_result result; memset(&result, 0, sizeof result);
  if (c_is(callback, "world:think")) {
    result.has_value = true; result.value.ctx = ctx; result.value.call = c_think_call;
  }
  return result;
}
static bool c_scheduler_source_slot(void *ctx, qa_actor_id actor, int64_t *out) {
  qa_actor_source_result source = qa_session_actor_registry_source_of(
      ((qa_app_sim_concrete_simulation *)ctx)->actors, actor);
  if (!source.has_value) return false;
  *out = source.slot; return true;
}
/* TS GameplayAuthority impulse hook: numeric add onto body velocity. */
static void c_combat_impulse(void *ctx, qa_owned_actor actor, qa_vec3 impulse, qa_provider_id movement) {
  qa_app_sim_concrete_simulation *c = ctx;
  qa_shared_body_table *bodies = qa_app_sim_shared_physics_bodies(c->physics);
  qa_body_state_result read = qa_shared_body_table_read(bodies, actor.id);
  if (!read.has_value) qa_error("Damaged actor has no body");
  qa_provider_timing timing = qa_app_sim_provider_timing(c->options.recipe, movement);
  const qa_numeric_operations *numeric = qa_create_numeric_operations(&timing.numeric);
  qa_body_state body = read.value;
  body.velocity.x = numeric->add(numeric->ctx, body.velocity.x, impulse.x);
  body.velocity.y = numeric->add(numeric->ctx, body.velocity.y, impulse.y);
  body.velocity.z = numeric->add(numeric->ctx, body.velocity.z, impulse.z);
  qa_shared_body_table_write(bodies, actor, body);
}
static void c_combat_confirmed(void *ctx, qa_damage_outcome outcome) {
  qa_simulation_event_payload payload; memset(&payload, 0, sizeof payload);
  payload.kind = QA_SIM_EVENT_DAMAGE; payload.outcome = outcome;
  qa_actor_id none; memset(&none, 0, sizeof none);
  qa_app_sim_simulation_events_append(((qa_app_sim_concrete_simulation *)ctx)->events,
                                      payload, false, none);
}
/* MovementPlayer services. Touch continues with the committed
 * state (trigger touching runs through SharedPhysics in the TS
 * source turn); weapon/animation steps pass the arsenal/animation
 * through unchanged because the source arsenal that owns them in TS
 * (playerWeapon over the q1/q2/q3 source game) is not constructed in
 * this slice — movement is authoritative, weapon fire is not. */
static qa_movement_continuation c_move_touch(void *ctx, qa_movement_touch_contact contact,
                                             qa_movement_state state) {
  (void)ctx; (void)contact;
  qa_movement_continuation out; memset(&out, 0, sizeof out);
  out.is_continue = true; out.state = state; return out;
}
static qa_weapon_step_result c_move_weapon(void *ctx, qa_weapon_step_input input,
                                           qa_movement_state state) {
  (void)ctx; (void)state;
  qa_weapon_step_result out; memset(&out, 0, sizeof out);
  out.arsenal = input.arsenal; out.animation = input.animation; return out;
}
static qa_animation_step_result c_move_animation(void *ctx, qa_animation_step_input input) {
  (void)ctx;
  qa_animation_step_result out; memset(&out, 0, sizeof out);
  out.animation = input.animation; return out;
}
static void c_touch_triggers(void *ctx, qa_owned_actor actor) {
  qa_app_sim_shared_physics_touch_triggers(((qa_app_sim_concrete_simulation *)ctx)->physics, actor);
}
static bool c_is_brush(void *ctx, qa_actor_id actor) {
  return qa_app_sim_shared_physics_is_brush(((qa_app_sim_concrete_simulation *)ctx)->physics, actor);
}
/* TS options.world: the decoded ApplicationWorld. From mounts, only
 * the Q1 decode is repeated here (content.c owns Q2/Q3 decoding and
 * hands the decoded world through options.world). */
static void c_resolve_world(qa_app_sim_concrete_simulation *c) {
  if (c->options.world) {
    const qa_app_application_world *world = c->options.world;
    if (c_is(world->kind, "q1-bsp")) {
      c->q1_map = (const qa_q1_map *)world->value;
      c->decoded.kind = QA_DECODED_WORLD_Q1; c->decoded.as.q1 = &c->q1_map->world;
    } else if (c_is(world->kind, "q2-bsp")) {
      c->decoded.kind = QA_DECODED_WORLD_Q2;
      c->decoded.as.q2 = &((const qa_q2_decoded_map *)world->value)->world;
    } else if (c_is(world->kind, "q3-bsp")) {
      c->decoded.kind = QA_DECODED_WORLD_Q3;
      c->decoded.as.q3 = (const qa_q3_world_geometry *)world->value;
    } else qa_error("Concrete simulation requires a decoded BSP world");
    return;
  }
  if (!c->options.mounts) qa_error("Concrete simulation requires a mounted map world");
  qa_content_mounted *mounts = (qa_content_mounted *)c->options.mounts;
  const char *path = c->options.recipe.map.geometry.requested_path;
  size_t length = 0;
  uint8_t *bytes = NULL;
  if (c->options.recipe.map.geometry.id)
    bytes = qa_content_mounted_read_reference(mounts, &c->options.recipe.map.geometry, &length);
  if (!bytes && path) bytes = qa_content_mounted_read_path(mounts, path, &length);
  if (!bytes) qa_error("Simulation map geometry is not mounted: %s", path ? path : "");
  const char *family = qa_classify_bsp(bytes, length, path);
  if (!family || !c_is(family, "q1")) {
    free(bytes);
    qa_error("Concrete simulation requires a decoded world for %s maps", family ? family : "unknown");
  }
  c->owned_q1_map = calloc(1, sizeof *c->owned_q1_map);
  if (!c->owned_q1_map) qa_range_error("Out of memory");
  qa_q1_map_options map_options; memset(&map_options, 0, sizeof map_options);
  map_options.source = path;
  qa_q1_read_bsp(bytes, length, &map_options, c->owned_q1_map);
  free(bytes);
  c->q1_map = c->owned_q1_map;
  c->decoded.kind = QA_DECODED_WORLD_Q1; c->decoded.as.q1 = &c->q1_map->world;
}
static void c_validate_options(qa_app_simulation_options options, bool loading) {
  if (!loading && options.recipe.equipment.grapple.is_enabled
      && c_is(options.recipe.equipment.grapple.mechanic, "q3-qvm"))
    qa_error("Authored QVM grapple requires asynchronous loadSimulation");
  if (!loading && options.prepared_mod_count != 0)
    qa_error("Selected gameplay mods require asynchronous loadSimulation");
  if (options.recipe.weapons.count != 1)
    qa_error("This source arsenal requires one selected weapon provider");
  if (options.restore)
    qa_error("Concrete simulation restore is not constructed for this source");
  for (size_t i = 0; i < options.recipe.execution_count; i++) {
    qa_execution_module_kind kind = options.recipe.execution[i].kind;
    if (kind == QA_EXECUTION_MODULE_TYPESCRIPT) continue;
    if (kind == QA_EXECUTION_MODULE_NATIVE && !options.has_q2_guest)
      qa_error("Native Quake II requires its exact prepared API and ABI");
    if (kind == QA_EXECUTION_MODULE_QVM && !options.q3_guest)
      qa_error("Q3 bytecode requires a prepared native map without travel state");
    if (kind == QA_EXECUTION_MODULE_QUAKEC && !options.prepared_quakec)
      qa_error("QuakeC simulation requires the prepared native supported artifact");
    qa_error("Concrete simulation source is not constructed for this execution module");
  }
}
static qa_simulation_output c_step(void *ctx, qa_input_batch input);
static qa_simulation_output c_step_async(void *ctx, qa_input_batch input,
    void (*progress)(void *progress_ctx, qa_simulation_progress frame), void *progress_ctx);
static qa_save_image c_checkpoint(void *ctx);
static void c_close(void *ctx);
qa_app_sim_shared_simulation_host qa_app_sim_shared_simulation_concrete_host(
    qa_app_simulation_options options, qa_session_actor_registry *actors, qa_session_id session,
    qa_app_sim_concrete_simulation **out_state) {
  /* The host form is the loadSimulation assembly (async in TS). */
  c_validate_options(options, true);
  if (!actors) qa_error("Concrete simulation requires a session actor registry");
  qa_app_sim_concrete_simulation *c = calloc(1, sizeof *c);
  if (!c) qa_range_error("Out of memory");
  c->options = options; c->actors = actors; c->session = session;
  qa_provider_timing timing = qa_app_sim_provider_timing(options.recipe,
                                                         options.recipe.map.entities.provider);
  c->world_clock = timing.clock;
  /* TS constructor clock staging. */
  double initial = options.has_initial_source_milliseconds
      ? options.initial_source_milliseconds : options.prepared_quakec ? 1000 : 0;
  if (!isfinite(initial) || initial < 0)
    qa_range_error("Initial source time must be finite and nonnegative");
  bool milliseconds = timing.clock.kind == QA_CLOCK_Q2_RERELEASE || timing.clock.kind == QA_CLOCK_Q3;
  qa_source_clock_init(&c->clock,
      (qa_source_time){milliseconds ? QA_SOURCE_TIME_MILLISECONDS : QA_SOURCE_TIME_SECONDS,
                       milliseconds ? initial : initial / 1000}, 0);
  c->host_milliseconds = initial;
  c->source_scheduling_milliseconds = initial;
  c->selected_milliseconds = initial;
  c->source_frame = qa_source_clock_frame(&c->clock);
  c->random = qa_app_sim_source_random_create(options.seed,
      timing.clock.kind == QA_CLOCK_Q2_RERELEASE ? "q2-rerelease" : "classic");
  c->owns_random = true;
  c_resolve_world(c);
  c->callbacks = qa_actor_callback_table_create(actors);
  if (!c->callbacks) qa_range_error("Out of memory");
  c->scene = qa_create_scene_queries(c->decoded);
  if (!c->scene) qa_range_error("Out of memory");
  qa_app_sim_shared_physics_options physics_options; memset(&physics_options, 0, sizeof physics_options);
  physics_options.ctx = c; physics_options.actors = actors;
  physics_options.callbacks = c->callbacks; physics_options.scene = c->scene;
  physics_options.numeric = timing.numeric;
  physics_options.source_order = c_source_order; physics_options.world_actor = c_world_actor;
  physics_options.event = c_physics_event;
  physics_options.q2_edition = (options.recipe.map.entities.content
      && strstr(options.recipe.map.entities.content, ":rerelease:")) ? "rerelease" : "classic";
  c->physics = qa_app_sim_shared_physics_create(physics_options);
  c->inventory = qa_shared_inventory_table_create(actors);
  if (!c->inventory) qa_range_error("Out of memory");
  qa_app_sim_simulation_events_options event_options; memset(&event_options, 0, sizeof event_options);
  event_options.ctx = c; event_options.bodies = qa_app_sim_shared_physics_bodies(c->physics);
  event_options.now = c_now; event_options.client_for = c_client_for;
  event_options.source_slot = c_source_slot; event_options.resource_for = c_resource_for;
  event_options.emit_super = c_emit_super;
  c->events = qa_app_sim_simulation_events_create(event_options);
  qa_gameplay_hooks hooks; memset(&hooks, 0, sizeof hooks);
  hooks.ctx = c; hooks.impulse = c_combat_impulse; hooks.confirmed = c_combat_confirmed;
  c->combat = qa_gameplay_authority_create(actors, c->callbacks, hooks);
  if (!c->combat) qa_range_error("Out of memory");
  c->scheduler_clock_count = options.recipe.timing_count;
  if (c->scheduler_clock_count) {
    c->scheduler_clocks = malloc(c->scheduler_clock_count * sizeof *c->scheduler_clocks);
    if (!c->scheduler_clocks) qa_range_error("Out of memory");
    for (size_t i = 0; i < c->scheduler_clock_count; i++)
      c->scheduler_clocks[i] = (qa_scheduler_clock){options.recipe.timing[i].provider,
                                                    options.recipe.timing[i].clock};
  }
  qa_scheduler_options scheduler_options; memset(&scheduler_options, 0, sizeof scheduler_options);
  scheduler_options.actors = qa_session_actor_registry_as_contract(actors);
  scheduler_options.ordering = options.recipe.ordering;
  scheduler_options.clocks = c->scheduler_clocks;
  scheduler_options.clock_count = c->scheduler_clock_count;
  scheduler_options.ctx = c; scheduler_options.source_slot = c_scheduler_source_slot;
  scheduler_options.resolve = c_scheduler_resolve;
  c->scheduler = qa_frame_scheduler_create(scheduler_options);
  if (!c->scheduler) qa_range_error("Out of memory");
  /* TS worldActor(): source slot 0 (1022 on Q3), allocated by the
   * source spawn; the concrete spawn owns just the world actor. */
  const char *entities_provider = options.recipe.map.entities.provider;
  int64_t world_slot = entities_provider && strncmp(entities_provider, "q3:", 3) == 0 ? 1022 : 0;
  if (!qa_session_actor_registry_at_source(actors, entities_provider, world_slot, &c->world_actor))
    c->world_actor = qa_session_actor_registry_allocate_at_source(
        actors, entities_provider, world_slot, "world:worldspawn");
  c->has_world_actor = true;
  qa_shared_body_table *bodies = qa_app_sim_shared_physics_bodies(c->physics);
  if (!qa_shared_body_table_read(bodies, c->world_actor.id).has_value) {
    qa_body_state body; memset(&body, 0, sizeof body);
    qa_shared_body_table_create_body(bodies, c->world_actor, body);
    qa_app_sim_shared_solid solid; memset(&solid, 0, sizeof solid);
    solid.solid = "none";
    solid.family = c->decoded.kind == QA_DECODED_WORLD_Q1 ? "q1"
        : c->decoded.kind == QA_DECODED_WORLD_Q2 ? "q2" : "q3";
    qa_app_sim_shared_physics_set_solid(c->physics, c->world_actor, solid);
    qa_q2_motion motion; memset(&motion, 0, sizeof motion);
    motion.kind = "stationary"; motion.gravity = 1;
    motion.gravity_vector = (qa_vec3){0, 0, -1};
    qa_app_sim_shared_physics_set_motion(c->physics, c->world_actor, motion);
    qa_shared_body_table_link(bodies, c->world_actor, NULL);
  }
  if (out_state) *out_state = c;
  qa_app_sim_shared_simulation_host host; memset(&host, 0, sizeof host);
  host.ctx = c; host.step = c_step; host.step_async = c_step_async;
  host.checkpoint = c_checkpoint; host.close = c_close;
  return host;
}
static const char *c_entity_value(const qa_q1_entity *entity, const char *key) {
  for (size_t i = 0; i < entity->property_count; i++)
    if (c_is(entity->properties[i].key, key)) return entity->properties[i].value;
  return NULL;
}
qa_app_sim_player_admission qa_app_sim_concrete_simulation_admit_player(
    qa_app_sim_concrete_simulation *c, qa_client_id client) {
  if (c->closed) qa_error("Simulation is closed");
  for (size_t i = 0; i < c->player_count; i++)
    if (c->players[i].client.slot == client.slot
        && c->players[i].client.generation == client.generation)
      qa_error("Client already owns a shared player");
  qa_vec3 origin = {0, 0, 0}, angles = {0, 0, 0};
  if (c->q1_map) {
    for (size_t i = 0; i < c->q1_map->entity_count; i++) {
      const qa_q1_entity *entity = &c->q1_map->entity_list[i];
      if (!c_is(c_entity_value(entity, "classname"), "info_player_start")) continue;
      const char *text = c_entity_value(entity, "origin");
      if (text) (void)sscanf(text, "%lf %lf %lf", &origin.x, &origin.y, &origin.z);
      const char *angle = c_entity_value(entity, "angle");
      if (angle) angles.y = strtod(angle, NULL);
      break;
    }
  }
  qa_owned_actor actor = qa_session_actor_registry_allocate(
      c->actors, c->options.recipe.map.entities.provider, "world:player");
  qa_app_sim_player_movement_host host; memset(&host, 0, sizeof host);
  host.ctx = c; host.actors = c->actors;
  host.bodies = qa_app_sim_shared_physics_bodies(c->physics);
  host.combat = c->combat; host.scene = qa_shared_as_scene_queries(c->scene);
  host.rerelease_movement = qa_app_sim_shared_physics_rerelease_movement(c->physics);
  host.touch_triggers = c_touch_triggers; host.is_brush = c_is_brush;
  host.world_actor = c_world_actor;
  host.services.ctx = c;
  host.services.touch = c_move_touch;
  host.services.weapon_step = c_move_weapon;
  host.services.animation_step = c_move_animation;
  qa_arsenal_state arsenal; memset(&arsenal, 0, sizeof arsenal);
  arsenal.provider = c->options.recipe.weapons.values[0].provider;
  qa_app_sim_movement_player *player = qa_app_sim_movement_player_create(
      actor, client, c->options.recipe, host, origin, angles, arsenal, NULL, NULL);
  qa_body_state body; memset(&body, 0, sizeof body);
  body.origin = origin; body.angles = angles; body.bounds = player->standing_bounds;
  qa_shared_body_table_create_body(host.bodies, actor, body);
  qa_app_sim_shared_solid solid; memset(&solid, 0, sizeof solid);
  solid.solid = "box";
  solid.family = c->decoded.kind == QA_DECODED_WORLD_Q1 ? "q1"
      : c->decoded.kind == QA_DECODED_WORLD_Q2 ? "q2" : "q3";
  qa_app_sim_shared_physics_set_solid(c->physics, actor, solid);
  qa_q2_motion motion; memset(&motion, 0, sizeof motion);
  motion.kind = "step"; motion.gravity = 1; motion.gravity_vector = (qa_vec3){0, 0, -1};
  motion.clip_mask = 0x6000003;
  qa_app_sim_shared_physics_set_motion(c->physics, actor, motion);
  qa_shared_body_table_link(host.bodies, actor, NULL);
  qa_combat_state combat; memset(&combat, 0, sizeof combat);
  combat.health = 100; combat.mass = 200; combat.can_take_damage = true;
  qa_gameplay_authority_create_state(c->combat, actor, combat, NULL, NULL);
  qa_shared_inventory_table_create_entries(c->inventory, actor, NULL, 0);
  size_t capacity = c->player_capacity;
  c->players = c_grow(c->players, &capacity, c->player_count + 1, sizeof *c->players);
  c->player_capacity = capacity;
  c->players[c->player_count++] = (concrete_player){player, client};
  qa_app_sim_player_admission admission; memset(&admission, 0, sizeof admission);
  admission.actor = actor.id; admission.view_height = player->view_height;
  return admission;
}
/* TS snapshot(). */
static qa_world_snapshot c_snapshot(qa_app_sim_concrete_simulation *c) {
  size_t count = 0;
  const qa_actor_observation *observations = qa_session_actor_registry_observations(c->actors, &count);
  size_t capacity = 0;
  c->snap_actors = c_grow(c->snap_actors, &capacity, count ? count : 1, sizeof *c->snap_actors);
  if (count) memcpy(c->snap_actors, observations, count * sizeof *observations);
  qa_shared_body_table *bodies = qa_app_sim_shared_physics_bodies(c->physics);
  capacity = 0;
  c->snap_bodies = c_grow(c->snap_bodies, &capacity, count ? count : 1, sizeof *c->snap_bodies);
  c->snap_body_count = 0;
  for (size_t i = 0; i < count; i++) {
    qa_body_state_result read = qa_shared_body_table_read(bodies, c->snap_actors[i].id);
    if (read.has_value) c->snap_bodies[c->snap_body_count++] =
        (qa_body_snapshot){c->snap_actors[i].id, read.value};
  }
  size_t inventory_capacity = 0, configuration_capacity = 0;
  c->snap_inventories = c_grow(c->snap_inventories, &inventory_capacity,
      c->player_count ? c->player_count : 1, sizeof *c->snap_inventories);
  c->snap_configurations = c_grow(c->snap_configurations, &configuration_capacity,
      c->player_count ? c->player_count : 1, sizeof *c->snap_configurations);
  c->snap_inventory_count = 0; c->snap_configuration_count = 0;
  for (size_t i = 0; i < c->player_count; i++) {
    qa_actor_id actor = c->players[i].player->actor.id;
    size_t entry_count = 0;
    const qa_inventory_entry *entries = qa_shared_inventory_table_entries(c->inventory, actor, &entry_count);
    c->snap_inventories[c->snap_inventory_count++] = (qa_inventory_snapshot){actor, entries, entry_count};
    qa_actor_configuration configuration; memset(&configuration, 0, sizeof configuration);
    configuration.movement = c->options.recipe.movement;
    configuration.character = c->options.recipe.character;
    configuration.weapons = c->options.recipe.weapons;
    configuration.inventory = c->options.recipe.inventory;
    configuration.actor = actor;
    c->snap_configurations[c->snap_configuration_count++] = configuration;
  }
  c->scene_world.resource = c->options.recipe.map.geometry;
  c->scene_world.geometry = c->decoded;
  qa_world_snapshot snapshot; memset(&snapshot, 0, sizeof snapshot);
  snapshot.session = c->session; snapshot.frame = c->source_frame;
  snapshot.actors = c->snap_actors; snapshot.actor_count = count;
  snapshot.bodies = c->snap_bodies; snapshot.body_count = c->snap_body_count;
  snapshot.inventories = c->snap_inventories; snapshot.inventory_count = c->snap_inventory_count;
  snapshot.configurations = c->snap_configurations;
  snapshot.configuration_count = c->snap_configuration_count;
  snapshot.scene.session = c->session; snapshot.scene.time = c->source_frame.time;
  snapshot.scene.world = &c->scene_world;
  return snapshot;
}
static void c_position_of(void *ctx, qa_owned_actor actor, double out[2]) {
  c_source_position(ctx, actor.id, out);
}
/* One map boundary turn: player commands first (TS client-command
 * phase), then ordered actor turns (scheduler during-physics +
 * physics step), mirroring the TS step() tail for actors without a
 * source execution entry. */
static void c_run_frame(qa_app_sim_concrete_simulation *c, qa_input_batch input,
                        bool command_turn, double elapsed_seconds) {
  if (command_turn) {
    for (size_t i = 0; i < input.command_count; i++) {
      qa_actor_command command = input.commands[i];
      concrete_player *entry = c_player_for(c, command.actor);
      if (!entry) qa_error("Command targets an unadmitted player");
      qa_app_sim_movement_player *player = entry->player;
      if (c_is(player->profile.kind, "q1-netquake")) {
        if (command.sequence <= player->last_sequence) continue;
        qa_app_sim_movement_player_receive_netquake(player, command);
        qa_frame_context frame = c->source_frame; frame.phase = QA_FRAME_PHASE_CLIENT_COMMAND;
        qa_app_sim_movement_player_prepare_netquake(player, frame, NULL, NULL);
        (void)qa_app_sim_movement_player_physics_netquake(player, c->source_frame);
        qa_app_sim_movement_player_finish_netquake_input(player, false);
      } else {
        (void)qa_app_sim_movement_player_move(player, command, c->source_frame, NULL);
      }
    }
  }
  qa_app_sim_ordered_actor_turns *turns = qa_app_sim_ordered_actor_turns_create(c->actors, c_position_of, c);
  qa_app_sim_actor_visited visited; memset(&visited, 0, sizeof visited);
  qa_owned_actor actor;
  while (qa_app_sim_ordered_actor_turns_next(turns, &visited, &actor)) {
    if (c_player_for(c, actor.id)) continue; /* movement owners stepped above */
    qa_frame_context frame = c->source_frame; frame.phase = QA_FRAME_PHASE_ENTITY_THINK;
    (void)qa_frame_scheduler_run(c->scheduler, actor.id, frame, QA_THINK_DURING_PHYSICS);
    if (qa_session_actor_registry_is_live(c->actors, actor.id)
        && qa_shared_body_table_read(qa_app_sim_shared_physics_bodies(c->physics), actor.id).has_value)
      (void)qa_app_sim_shared_physics_step(c->physics, actor, elapsed_seconds);
    qa_app_sim_shared_physics_commit_attachments(c->physics);
  }
  free(visited.actors);
  qa_app_sim_ordered_actor_turns_destroy(turns);
}
static qa_simulation_output c_step(void *ctx, qa_input_batch input) {
  qa_app_sim_concrete_simulation *c = ctx;
  if (c->closed) qa_error("Simulation is closed");
  for (size_t i = 0; i < input.command_count; i++) {
    concrete_player *entry = c_player_for(c, input.commands[i].actor);
    if (!entry) qa_error("Command targets an unadmitted player");
    qa_command_source source = input.commands[i].source;
    if (source.kind != QA_COMMAND_SOURCE_BOT
        && (source.client.slot != entry->client.slot
            || source.client.generation != entry->client.generation
            || !qa_identity_owner_owns_client(&c->options.identity, source.client)))
      qa_error("Command client does not own this player");
  }
  c->host_milliseconds += input.elapsed_milliseconds;
  c->selected_milliseconds += input.elapsed_milliseconds;
  c->source_scheduling_milliseconds += input.elapsed_milliseconds;
  qa_app_sim_frame_plan_input plan_input; memset(&plan_input, 0, sizeof plan_input);
  plan_input.profile = c->world_clock;
  plan_input.elapsed_milliseconds = input.elapsed_milliseconds;
  plan_input.time_seconds = c_seconds(c->source_frame.time);
  plan_input.source_scheduling_milliseconds = c->source_scheduling_milliseconds;
  plan_input.selected_milliseconds = c->selected_milliseconds;
  qa_app_sim_frame_plan plan = qa_app_sim_frame_plan_compute(plan_input);
  bool commands_pending = true;
  for (size_t i = 0; i < plan.boundary_count; i++) {
    bool run = plan.boundaries[i].map;
    bool command_turn = commands_pending && (run || !plan.map_run);
    if (command_turn) commands_pending = false;
    if (run) {
      /* TS clock staging: fixed clocks advance at frame-entry and
       * enter frame-exit; free clocks hold the frame and advance at
       * frame-exit with the clamped elapsed. */
      if (plan.has_fixed) {
        bool milliseconds = qa_source_clock_frame(&c->clock).time.kind == QA_SOURCE_TIME_MILLISECONDS;
        c->source_frame = qa_source_clock_advance(&c->clock,
            (qa_source_time){milliseconds ? QA_SOURCE_TIME_MILLISECONDS : QA_SOURCE_TIME_SECONDS,
                             milliseconds ? plan.fixed_milliseconds : plan.elapsed_seconds},
            QA_FRAME_PHASE_FRAME_ENTRY);
      } else {
        c->source_frame = qa_source_clock_frame(&c->clock);
        c->source_frame.elapsed = (qa_source_time){QA_SOURCE_TIME_SECONDS, plan.elapsed_seconds};
        c->source_frame.phase = QA_FRAME_PHASE_FRAME_ENTRY;
      }
      c_run_frame(c, input, command_turn, plan.elapsed_seconds);
      if (plan.has_fixed) c->source_frame = qa_source_clock_enter(&c->clock, QA_FRAME_PHASE_FRAME_EXIT);
      else c->source_frame = qa_source_clock_advance(&c->clock,
          (qa_source_time){QA_SOURCE_TIME_SECONDS, plan.elapsed_seconds}, QA_FRAME_PHASE_FRAME_EXIT);
    } else if (command_turn) {
      c_run_frame(c, input, true, 0);
    }
  }
  qa_app_sim_frame_plan_free(&plan);
  qa_simulation_output output; memset(&output, 0, sizeof output);
  output.snapshot = c_snapshot(c);
  output.events = qa_app_sim_simulation_events_take(c->events, &output.event_count);
  return output;
}
static qa_simulation_output c_step_async(void *ctx, qa_input_batch input,
    void (*progress)(void *progress_ctx, qa_simulation_progress frame), void *progress_ctx) {
  /* TS stepAsync falls through to step() for non-guest sources. */
  qa_simulation_output output = c_step(ctx, input);
  if (progress) progress(progress_ctx,
      (qa_simulation_progress){&output, input.elapsed_milliseconds, 0});
  return output;
}
static void c_free_body_checkpoints(qa_body_checkpoint *checkpoints, size_t count) {
  if (!checkpoints) return;
  for (size_t i = 0; i < count; i++) {
    free((void *)checkpoints[i].body.ground);
    free((void *)checkpoints[i].attachment);
    if (checkpoints[i].linked) {
      free((void *)checkpoints[i].linked->state.ground);
      free((void *)checkpoints[i].linked);
    }
  }
  free(checkpoints);
}
static void c_free_checkpoint(qa_app_sim_concrete_simulation *c) {
  free(c->ck_random_words); c->ck_random_words = NULL;
  free(c->ck_actors); c->ck_actors = NULL;
  if (c->ck_bodies) c_free_body_checkpoints(c->ck_bodies, c->ck_body_count);
  c->ck_bodies = NULL; c->ck_body_count = 0;
  free(c->ck_combat); c->ck_combat = NULL; c->ck_combat_count = 0;
  free(c->ck_inventories); c->ck_inventories = NULL; c->ck_inventory_count = 0;
  free(c->ck_configurations); c->ck_configurations = NULL; c->ck_configuration_count = 0;
  free(c->ck_thinks); c->ck_thinks = NULL; c->ck_think_count = 0;
  free((uint8_t *)c->ck_providers[0].bytes.bytes); c->ck_providers[0].bytes.bytes = NULL;
  free(c->ck_simulation_bytes); c->ck_simulation_bytes = NULL;
}
static qa_save_value *c_value(qa_save_value *value) {
  if (!value) qa_range_error("Out of memory");
  return value;
}
static void c_set(qa_save_value *object, const char *key, qa_save_value *child) {
  if (!qa_save_value_set(object, key, child)) { qa_save_value_free(child); qa_range_error("Out of memory"); }
}
/* TS checkpointFromSource over the concrete owners. The
 * world:simulation tree carries the settings/scheduling fields the
 * concrete state owns; source-game captures (physics/events/players
 * sub-trees, q1:foundation, guests) belong to the unconstructed
 * source runtimes. */
static qa_save_image c_checkpoint(void *ctx) {
  qa_app_sim_concrete_simulation *c = ctx;
  if (c->closed) qa_error("Simulation is closed");
  qa_gameplay_authority_assert_idle(c->combat);
  c_free_checkpoint(c);
  const char *provider = c->options.recipe.map.entities.provider;
  qa_save_image image; memset(&image, 0, sizeof image);
  image.schema_version = 3; image.recipe = c->options.recipe; image.frame = c->source_frame;
  image.next_event_sequence = qa_app_sim_simulation_events_next_sequence(c->events);
  c->ck_clock = (qa_save_clock){provider, c->source_frame.time};
  image.clocks = &c->ck_clock; image.clock_count = 1;
  qa_app_sim_source_random_state random = qa_app_sim_source_random_checkpoint(c->random);
  memset(&c->ck_random, 0, sizeof c->ck_random);
  c->ck_random.provider = provider;
  if (c_is(random.kind, "q2-rerelease-mt19937")) {
    c->ck_random.state.kind = QA_RANDOM_STATE_Q2_RERELEASE_MT19937;
    c->ck_random.state.index = (double)random.rerelease.index;
    c->ck_random.state.draws = (double)random.rerelease.draws;
    c->ck_random_words = malloc(QA_Q2_RERELEASE_WORDS * sizeof *c->ck_random_words);
    if (!c->ck_random_words) qa_range_error("Out of memory");
    for (size_t i = 0; i < QA_Q2_RERELEASE_WORDS; i++)
      c->ck_random_words[i] = (double)random.rerelease.words[i];
    c->ck_random.state.words = (qa_double_view){c->ck_random_words, QA_Q2_RERELEASE_WORDS};
  } else {
    c->ck_random.state.kind = QA_RANDOM_STATE_GLIBC_RANDOM;
    c->ck_random.state.front = random.front; c->ck_random.state.rear = random.rear;
    c->ck_random.state.draws = (double)random.draws;
    c->ck_random_words = malloc(31 * sizeof *c->ck_random_words);
    if (!c->ck_random_words) qa_range_error("Out of memory");
    for (size_t i = 0; i < 31; i++) c->ck_random_words[i] = (double)random.words[i];
    c->ck_random.state.words = (qa_double_view){c->ck_random_words, 31};
  }
  image.random = &c->ck_random; image.random_count = 1;
  size_t actor_count = 0;
  const qa_actor_slot_checkpoint *actors = qa_session_actor_registry_checkpoint(c->actors, &actor_count);
  if (actor_count) {
    c->ck_actors = malloc(actor_count * sizeof *c->ck_actors);
    if (!c->ck_actors) qa_range_error("Out of memory");
    memcpy(c->ck_actors, actors, actor_count * sizeof *actors);
  }
  image.actors = c->ck_actors; image.actor_count = actor_count;
  /* TS captureSharedBodies over the real body table (the
   * persistence mirror in world_state.h binds different structs
   * under the same names, so the capture is open-coded here). */
  {
    qa_shared_body_table *table = qa_app_sim_shared_physics_bodies(c->physics);
    size_t count = 0;
    const qa_actor_observation *all = qa_session_actor_registry_observations(c->actors, &count);
    size_t body_capacity = 0;
    for (size_t i = 0; i < count; i++) {
      qa_body_state_result read = qa_shared_body_table_read(table, all[i].id);
      qa_body_link_state links;
      if (!read.has_value || !qa_shared_body_table_link_state(table, all[i].id, &links)) continue;
      c->ck_bodies = c_grow(c->ck_bodies, &body_capacity, c->ck_body_count + 1, sizeof *c->ck_bodies);
      qa_body_checkpoint *checkpoint = &c->ck_bodies[c->ck_body_count];
      memset(checkpoint, 0, sizeof *checkpoint);
      checkpoint->actor = qa_saved_actor_id_of(all[i].id);
      checkpoint->link_count = links.link_count;
      checkpoint->body.origin = read.value.origin;
      checkpoint->body.angles = read.value.angles;
      checkpoint->body.velocity = read.value.velocity;
      checkpoint->body.bounds = read.value.bounds;
      if (read.value.ground) {
        qa_saved_actor_id *ground = malloc(sizeof *ground);
        if (!ground) qa_range_error("Out of memory");
        *ground = qa_saved_actor_id_of(*read.value.ground);
        checkpoint->body.ground = ground;
      }
      qa_body_attachment_result attachment = qa_shared_body_table_attachment(table, all[i].id);
      if (attachment.has_value) {
        qa_saved_body_attachment *saved = malloc(sizeof *saved);
        if (!saved) qa_range_error("Out of memory");
        saved->anchor = qa_saved_actor_id_of(attachment.value.anchor);
        saved->follow_kind = attachment.value.follow_kind;
        saved->follow_offset = attachment.value.follow_offset;
        checkpoint->attachment = saved;
      }
      if (links.has_linked) {
        qa_body_checkpoint_linked *linked = calloc(1, sizeof *linked);
        if (!linked) qa_range_error("Out of memory");
        linked->absolute_bounds = links.linked.absolute_bounds;
        linked->state.origin = links.linked.state.origin;
        linked->state.angles = links.linked.state.angles;
        linked->state.velocity = links.linked.state.velocity;
        linked->state.bounds = links.linked.state.bounds;
        if (links.linked.state.ground) {
          qa_saved_actor_id *ground = malloc(sizeof *ground);
          if (!ground) qa_range_error("Out of memory");
          *ground = qa_saved_actor_id_of(*links.linked.state.ground);
          linked->state.ground = ground;
        }
        checkpoint->linked = linked;
      }
      c->ck_body_count++;
    }
  }
  image.bodies = c->ck_bodies; image.body_count = c->ck_body_count;
  size_t observation_count = 0;
  const qa_actor_observation *observations =
      qa_session_actor_registry_observations(c->actors, &observation_count);
  size_t capacity = 0;
  for (size_t i = 0; i < observation_count; i++) {
    qa_actor_id actor = observations[i].id;
    qa_saved_actor_id saved = qa_saved_actor_id_of(actor);
    qa_combat_state_result combat = qa_gameplay_authority_read(c->combat, actor);
    if (combat.has_value) {
      c->ck_combat = c_grow(c->ck_combat, &capacity, c->ck_combat_count + 1, sizeof *c->ck_combat);
      c->ck_combat[c->ck_combat_count++] = (qa_combat_checkpoint){saved, combat.value};
    }
    if (qa_shared_inventory_table_has(c->inventory, actor)) {
      size_t entries_capacity = capacity, entry_count = 0;
      const qa_inventory_entry *entries = qa_shared_inventory_table_entries(c->inventory, actor, &entry_count);
      (void)entries_capacity;
      size_t inv_capacity = 0;
      c->ck_inventories = c_grow(c->ck_inventories, &inv_capacity,
          c->ck_inventory_count + 1, sizeof *c->ck_inventories);
      c->ck_inventories[c->ck_inventory_count++] = (qa_inventory_checkpoint){saved, entries, entry_count};
    }
    qa_scheduled_think_result pending = qa_frame_scheduler_pending(c->scheduler, actor);
    if (pending.has_value) {
      size_t think_capacity = 0;
      c->ck_thinks = c_grow(c->ck_thinks, &think_capacity, c->ck_think_count + 1, sizeof *c->ck_thinks);
      qa_think_checkpoint think; memset(&think, 0, sizeof think);
      think.has_execution_provider = pending.value.timing.has_execution_provider;
      think.execution_provider = pending.value.timing.execution_provider;
      think.actor = saved; think.callback = pending.value.callback;
      think.due = pending.value.timing.due; think.boundary = pending.value.timing.boundary;
      think.provider = pending.value.timing.order.provider;
      think.sequence = pending.value.timing.order.sequence;
      c->ck_thinks[c->ck_think_count++] = think;
    }
  }
  image.combat = c->ck_combat; image.combat_count = c->ck_combat_count;
  image.inventories = c->ck_inventories; image.inventory_count = c->ck_inventory_count;
  image.thinks = c->ck_thinks; image.think_count = c->ck_think_count;
  if (c->player_count) {
    c->ck_configurations = malloc(c->player_count * sizeof *c->ck_configurations);
    if (!c->ck_configurations) qa_range_error("Out of memory");
    for (size_t i = 0; i < c->player_count; i++) {
      qa_actor_configuration_checkpoint *configuration = &c->ck_configurations[c->ck_configuration_count++];
      memset(configuration, 0, sizeof *configuration);
      configuration->movement = c->options.recipe.movement;
      configuration->character = c->options.recipe.character;
      configuration->weapons = c->options.recipe.weapons;
      configuration->inventory = c->options.recipe.inventory;
      configuration->actor = qa_saved_actor_id_of(c->players[i].player->actor.id);
    }
  }
  image.configurations = c->ck_configurations;
  image.configuration_count = c->ck_configuration_count;
  size_t source_count = 0;
  const qa_source_actor_checkpoint *sources =
      qa_session_actor_registry_source_checkpoint(c->actors, &source_count);
  if (!qa_source_actors_checkpoint(sources, source_count, &c->ck_providers[0]))
    qa_range_error("Out of memory");
  qa_save_value *simulation = c_value(qa_save_value_object());
  qa_save_value *settings = c_value(qa_save_value_object());
  c_set(settings, "skill", c_value(qa_save_value_number(c->options.skill)));
  c_set(settings, "mode", c_value(qa_save_value_string(c->options.mode ? c->options.mode : "")));
  c_set(settings, "maxClients", c_value(qa_save_value_number(c->options.max_clients)));
  c_set(settings, "seed", c_value(qa_save_value_number(c->options.seed)));
  c_set(settings, "startItems", c_value(qa_save_value_string(
      c->options.start_items ? c->options.start_items : "")));
  c_set(settings, "initialSpawnPoint", c_value(qa_save_value_string(
      c->options.initial_spawn_point ? c->options.initial_spawn_point : "")));
  c_set(simulation, "settings", settings);
  /* TS players are full captureMovementPlayer records; the concrete
   * tree carries the client slot + saved actor the settings reader
   * and restore slot matching consume (full player capture belongs
   * to the player-checkpoint wiring of the source runtimes). */
  qa_save_value *players = c_value(qa_save_value_array());
  for (size_t i = 0; i < c->player_count; i++) {
    qa_save_value *entry = c_value(qa_save_value_object());
    qa_saved_actor_id saved = qa_saved_actor_id_of(c->players[i].player->actor.id);
    qa_save_value *actor = c_value(qa_save_value_object());
    c_set(actor, "slot", c_value(qa_save_value_number(saved.slot)));
    c_set(actor, "generation", c_value(qa_save_value_number(saved.generation)));
    c_set(entry, "actor", actor);
    c_set(entry, "clientSlot", c_value(qa_save_value_number((double)c->players[i].client.slot)));
    if (!qa_save_value_push(players, entry)) { qa_save_value_free(entry); qa_range_error("Out of memory"); }
  }
  c_set(simulation, "players", players);
  c_set(simulation, "hostMilliseconds", c_value(qa_save_value_number(c->host_milliseconds)));
  c_set(simulation, "sourceSchedulingMilliseconds",
      c_value(qa_save_value_number(c->source_scheduling_milliseconds)));
  size_t simulation_length = 0;
  c->ck_simulation_bytes = qa_encode_checkpoint_value(simulation, &simulation_length);
  qa_save_value_free(simulation);
  if (!c->ck_simulation_bytes) qa_range_error("Out of memory");
  c->ck_providers[1].provider = provider;
  c->ck_providers[1].schema = "world:simulation";
  c->ck_providers[1].version = 11;
  c->ck_providers[1].bytes = (qa_byte_view){c->ck_simulation_bytes, simulation_length};
  image.providers = c->ck_providers; image.provider_count = 2;
  return image;
}
static void c_close(void *ctx) {
  qa_app_sim_concrete_simulation *c = ctx;
  if (!c || c->closed) return;
  c->closed = true;
  for (size_t i = 0; i < c->player_count; i++) qa_app_sim_movement_player_destroy(c->players[i].player);
  free(c->players);
  qa_frame_scheduler_destroy(c->scheduler);
  free(c->scheduler_clocks);
  qa_gameplay_authority_destroy(c->combat);
  qa_app_sim_simulation_events_destroy(c->events);
  qa_shared_inventory_table_destroy(c->inventory);
  qa_app_sim_shared_physics_destroy(c->physics);
  qa_shared_scene_queries_destroy(c->scene);
  qa_actor_callback_table_destroy(c->callbacks);
  if (c->owns_random) qa_app_sim_source_random_destroy(c->random);
  if (c->owned_q1_map) { qa_q1_map_destroy(c->owned_q1_map); free(c->owned_q1_map); }
  free(c->snap_actors); free(c->snap_bodies);
  free(c->snap_inventories); free(c->snap_configurations);
  c_free_checkpoint(c);
  free(c);
}
void qa_app_sim_concrete_simulation_bind(qa_app_sim_concrete_simulation *state,
                                         qa_app_sim_shared_simulation *simulation) {
  state->owner = simulation;
  if (state->owns_random && simulation->random && simulation->random != state->random) {
    qa_app_sim_source_random_destroy(state->random);
    state->random = simulation->random; state->owns_random = false;
  }
}
qa_app_sim_shared_simulation *qa_app_sim_shared_simulation_create_concrete(
    qa_app_simulation_options options, qa_session_actor_registry *actors, qa_session_id session) {
  c_validate_options(options, false);
  qa_app_sim_concrete_simulation *state = NULL;
  qa_app_sim_shared_simulation_host host =
      qa_app_sim_shared_simulation_concrete_host(options, actors, session, &state);
  qa_app_sim_shared_simulation *simulation =
      qa_app_sim_shared_simulation_create(options, host, actors, session);
  qa_app_sim_concrete_simulation_bind(state, simulation);
  return simulation;
}
qa_app_sim_shared_simulation *qa_app_sim_load_simulation_concrete(
    qa_app_simulation_options options, qa_session_actor_registry *actors, qa_session_id session,
    void (*next_frame)(void *frame_ctx), void *frame_ctx) {
  qa_app_sim_concrete_simulation *state = NULL;
  qa_app_sim_shared_simulation_host host =
      qa_app_sim_shared_simulation_concrete_host(options, actors, session, &state);
  qa_app_sim_shared_simulation *simulation =
      qa_app_sim_load_simulation(options, host, actors, session, next_frame, frame_ctx);
  qa_app_sim_concrete_simulation_bind(state, simulation);
  return simulation;
}
qa_app_sim_concrete_simulation *qa_app_sim_shared_simulation_concrete(
    qa_app_sim_shared_simulation *simulation) {
  if (!simulation || simulation->host.step != c_step) return NULL;
  return simulation->host.ctx;
}
qa_app_sim_shared_physics *qa_app_sim_concrete_simulation_physics(qa_app_sim_concrete_simulation *state) {
  return state->physics;
}
qa_app_sim_simulation_events *qa_app_sim_concrete_simulation_events(qa_app_sim_concrete_simulation *state) {
  return state->events;
}
qa_shared_scene_queries *qa_app_sim_concrete_simulation_scene(qa_app_sim_concrete_simulation *state) {
  return state->scene;
}
qa_frame_context qa_app_sim_concrete_simulation_frame(const qa_app_sim_concrete_simulation *state) {
  return state->source_frame;
}
bool qa_app_sim_concrete_simulation_world_actor(const qa_app_sim_concrete_simulation *state, qa_actor_id *out) {
  if (!state->has_world_actor) return false;
  *out = state->world_actor.id; return true;
}
