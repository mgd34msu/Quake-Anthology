/* Port of src/app/bootstrap/simulation/runtime.ts (public surface,
 * frame-entry guards + ordered turns). The 8.5k-line TS composition
 * root's per-source frame pipeline stages live in the sibling
 * simulation modules; the host bundle runs the constructed pipeline
 * while this module owns the TS step/checkpoint/close guards.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/runtime.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
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
      qa_js_sort(t->pending, t->pending_count, sizeof t->pending[0], pending_cmp);
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
  double now_milliseconds = qa_js_round(input.time_seconds * 1000);
  plan.map_run = input.settlement_active || (!input.paused && input.elapsed_milliseconds > 0
      && (!plan.has_fixed || input.source_scheduling_milliseconds >= now_milliseconds
          + (q3_profile ? input.profile.server_frame_milliseconds : 0)));
  plan.elapsed_seconds = input.settlement_active ? 0.1
      : !plan.has_fixed ? qa_js_min(0.1, qa_js_max(0.001, input.elapsed_milliseconds / 1000))
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
  qa_js_sort(plan.boundaries, plan.boundary_count, sizeof plan.boundaries[0], boundary_cmp);
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
#include "qa/formats/q3_map/entities.h"
#include "qa/formats/q3_map/world.h"
#include "qa/persistence/save_image.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/scheduler.h"
#include "qa/world/session/clocks.h"
#include "qa/app/bootstrap/simulation/quakec_source.h"
#include "qa/compat/qc.h"
#include "qa/compat/qc/save.h"
#include "qa/core/cvars/index.h"
#include "qa/formats/q12_model.h"
#include "qa/world/actors/source_slots.h"
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
  qa_q2_decoded_map *owned_q2_map;
  const qa_q2_decoded_map *q2_map;
  qa_q3_decoded_world *owned_q3_world;
  const qa_q3_world_geometry *q3_geometry;
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
  struct c_qc_game *qc; /* TS QuakeCSource source game, when built */
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
/* TS Q2MapResources.readMaterial over the recipe mounts (content.ts
 * reads textures/<name>.mat from the map content; a missing material
 * is NULL). The returned buffer is freed by the Q2 decoder. */
static uint8_t *c_read_material(void *ctx, const char *path, size_t *out_length) {
  /* TS mapContent.open(path): a missing material is null, while
   * qa_content_mounted_read_path aborts on a missing resource. */
  qa_content_opened_resource opened; memset(&opened, 0, sizeof opened);
  if (!qa_content_mounted_open((qa_content_mounted *)ctx, path, NULL, NULL, &opened)) return NULL;
  *out_length = opened.byte_length;
  return opened.bytes;
}
/* TS options.world: the decoded ApplicationWorld (content.ts
 * loadApplicationContent decode). When no decoded world is handed
 * in, the same classifyBsp dispatch is repeated over the mounted
 * recipe geometry: q1 readQ1Bsp, q2 readQ2Bsp+toQ2WorldGeometry
 * (qa_decode_q2_map), else decodeQ3World. A {NULL, NULL} world view
 * is the C host-content form of "no decoded world" and also takes
 * the mounts path. */
static void c_resolve_world(qa_app_sim_concrete_simulation *c) {
  if (c->options.world) {
    const qa_app_application_world *world = c->options.world;
    if (world->kind != NULL || world->value != NULL) {
      if (c_is(world->kind, "q1-bsp")) {
        c->q1_map = (const qa_q1_map *)world->value;
        c->decoded.kind = QA_DECODED_WORLD_Q1; c->decoded.as.q1 = &c->q1_map->world;
      } else if (c_is(world->kind, "q2-bsp")) {
        c->q2_map = (const qa_q2_decoded_map *)world->value;
        c->decoded.kind = QA_DECODED_WORLD_Q2; c->decoded.as.q2 = &c->q2_map->world;
      } else if (c_is(world->kind, "q3-bsp")) {
        c->q3_geometry = (const qa_q3_world_geometry *)world->value;
        c->decoded.kind = QA_DECODED_WORLD_Q3; c->decoded.as.q3 = c->q3_geometry;
      } else qa_error("Concrete simulation requires a decoded BSP world");
      return;
    }
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
  if (family && c_is(family, "q1")) {
    c->owned_q1_map = calloc(1, sizeof *c->owned_q1_map);
    if (!c->owned_q1_map) qa_range_error("Out of memory");
    qa_q1_map_options map_options; memset(&map_options, 0, sizeof map_options);
    map_options.source = path;
    qa_q1_read_bsp(bytes, length, &map_options, c->owned_q1_map);
    c->q1_map = c->owned_q1_map;
    c->decoded.kind = QA_DECODED_WORLD_Q1; c->decoded.as.q1 = &c->q1_map->world;
  } else if (family && c_is(family, "q2")) {
    c->owned_q2_map = calloc(1, sizeof *c->owned_q2_map);
    if (!c->owned_q2_map) qa_range_error("Out of memory");
    qa_q2_map_resources resources; memset(&resources, 0, sizeof resources);
    resources.ctx = mounts; resources.read_material = c_read_material;
    qa_decode_q2_map(bytes, length, path, &resources, c->owned_q2_map);
    c->q2_map = c->owned_q2_map;
    c->decoded.kind = QA_DECODED_WORLD_Q2; c->decoded.as.q2 = &c->q2_map->world;
  } else if (family && c_is(family, "q3")) {
    c->owned_q3_world = calloc(1, sizeof *c->owned_q3_world);
    if (!c->owned_q3_world) qa_range_error("Out of memory");
    qa_q3_decode_world(bytes, length, path, c->owned_q3_world);
    c->q3_geometry = &c->owned_q3_world->world;
    c->decoded.kind = QA_DECODED_WORLD_Q3; c->decoded.as.q3 = c->q3_geometry;
  } else {
    free(bytes);
    qa_error("Concrete simulation requires a decoded world for %s maps", family ? family : "unknown");
  }
  free(bytes);
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
  /* TS constructor execution checks (native/qvm/quakec), in order. */
  bool has_native = false, has_qvm_server = false, has_any_qvm = false, has_quakec = false;
  const qa_execution_module *quakec = NULL;
  for (size_t i = 0; i < options.recipe.execution_count; i++) {
    const qa_execution_module *module = &options.recipe.execution[i];
    if (module->kind == QA_EXECUTION_MODULE_NATIVE) has_native = true;
    if (module->kind == QA_EXECUTION_MODULE_QVM) {
      has_any_qvm = true;
      if (c_is(module->role, "server-game")) has_qvm_server = true;
    }
    if (module->kind == QA_EXECUTION_MODULE_QUAKEC) { has_quakec = true; quakec = module; }
  }
  if (has_native) {
    if (!options.has_q2_guest || options.recipe.execution_count != 1 || options.has_travel)
      qa_error("Native Quake II requires its exact prepared API and ABI");
    /* The native Q2 guest world (classic/rerelease guest services
     * over the guest runtime) has no concrete builder composition. */
    qa_error("Concrete simulation source is not constructed for this execution module");
  } else if (options.has_q2_guest) {
    qa_error("Prepared Quake II native module differs from selected execution");
  }
  if (has_qvm_server) {
    if (!options.q3_guest)
      qa_error("Q3 bytecode requires a prepared native map without travel state");
    /* The Q3 QVM server game has no concrete builder composition. */
    qa_error("Concrete simulation source is not constructed for this execution module");
  } else if (options.q3_guest || has_any_qvm) {
    qa_error("Prepared Q3 guest does not match the selected server execution");
  }
  if (has_quakec) {
    const qa_app_sim_prepared_quakec_source *prepared = options.prepared_quakec;
    if (!prepared || options.recipe.execution_count != 1
        || prepared->execution.kind != QA_EXECUTION_MODULE_QUAKEC
        || !c_is(prepared->execution.owner.provider, quakec->owner.provider)
        || !c_is(prepared->execution.owner.content, quakec->owner.content))
      qa_error("QuakeC simulation requires the prepared native supported artifact");
    if (quakec->api.kind == QA_GAME_API_QUAKEC
        && quakec->api.as.quakec.kind == QA_QUAKEC_API_Q1_QUAKEWORLD && options.max_clients > 32)
      qa_error("Native QuakeWorld supports at most 32 clients");
    if (!options.original_save_candidate && options.has_initial_source_milliseconds
        && options.initial_source_milliseconds != 1000 && !options.restore)
      qa_error("QuakeC travel must use the same native ABI and a fresh source clock");
  } else if (options.prepared_quakec) {
    qa_error("Prepared QuakeC artifact does not match the selected execution");
  }
  for (size_t i = 0; i < options.recipe.execution_count; i++) {
    qa_execution_module_kind kind = options.recipe.execution[i].kind;
    if (kind == QA_EXECUTION_MODULE_TYPESCRIPT || kind == QA_EXECUTION_MODULE_QUAKEC) continue;
    qa_error("Concrete simulation source is not constructed for this execution module");
  }
}
/* --- TS QuakeCSource (quakec-source.ts) over the landed compat/qc
 * machine and hosts. Constructed by the concrete builder when the
 * recipe execution is QuakeC with a prepared artifact. The id1
 * damage/pickup/weapon-stage function boundaries and client
 * admission into QC slots are not composed here: QC runs its own
 * T_Damage/touch code and shared players stay concrete movement
 * players (noted unresolved). --- */
typedef struct c_qc_model { char *name; double index; qa_bounds bounds; } c_qc_model;
typedef struct c_qc_precache { char *key; qa_compat_qc_precached_resource value; } c_qc_precache;
typedef struct c_qc_resource {
  char *name; qa_resolved_resource_reference resource; bool has_bounds; qa_bounds bounds;
} c_qc_resource;
typedef struct c_qc_actor_ctx {
  struct c_qc_game *game; qa_owned_actor actor; qa_actor_id owner, enemy;
} c_qc_actor_ctx;
struct c_qc_game {
  qa_app_sim_concrete_simulation *concrete;
  const qa_app_sim_prepared_quakec_source *prepared;
  qa_compat_qc_program *program;
  bool quakeworld;
  int64_t reserved;
  double current_time;
  bool spawning, change_level_issued;
  char *change_level_map;
  qa_compat_qc_entity_memory entities;
  qa_compat_qc_source_slot_storage_state slot_storage_state;
  qa_source_actor_slots *slots;
  qa_source_actor_slots_options slots_options;
  qa_compat_qc_world_host world_host;
  qa_cvar_registry *cvars;
  qa_compat_qc_client_host clients;
  qa_compat_qc_presentation_world presentation_world;
  qa_compat_qc_presentation_state presentation_state;
  qa_compat_qc_netquake_message_services nq;
  qa_compat_qc_quakeworld_message_services qw;
  qa_compat_qc_broadcast_messages messages;
  qa_compat_qc_movement_state_compat movement_state;
  qa_compat_qc_touch_state touch_state;
  qa_compat_qc_aim_state aim_state;
  qa_compat_qc_aim_target_ref *aim_targets; size_t aim_target_count, aim_target_capacity;
  qa_compat_qc_host_binding host_bindings[160]; size_t host_binding_count;
  qa_compat_qc_builtin_registry registry;
  qa_compat_qc_machine machine;
  bool has_machine;
  qa_compat_qc_actor_state actor_state;
  qa_random_source random;
  c_qc_model *models; size_t model_count_entries, model_capacity;
  double model_count, sound_count;
  c_qc_precache *precached; size_t precached_count, precached_capacity;
  c_qc_resource *resources; size_t resource_count, resource_capacity;
  /* Individually allocated: physics sources keep these pointers across growth. */
  c_qc_actor_ctx **actor_ctxs; size_t actor_ctx_count, actor_ctx_capacity;
  size_t spawn_count, start_frame_count, think_count;
};
static char *c_strdup(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (!copy) qa_range_error("Out of memory");
  memcpy(copy, text, length);
  return copy;
}
static int64_t c_qc_field(struct c_qc_game *g, const char *name) {
  return qa_compat_qc_machine_field_offset(&g->machine, name);
}
static bool c_qc_source_slot(struct c_qc_game *g, qa_actor_id actor, int64_t *out) {
  qa_actor_source_result source = qa_session_actor_registry_source_of(g->concrete->actors, actor);
  if (!source.has_value || !c_is(source.provider, g->prepared->execution.owner.provider)) return false;
  *out = source.slot;
  return true;
}
static bool c_qc_is_reserved_client(struct c_qc_game *g, qa_actor_id actor) {
  int64_t slot = 0;
  return c_qc_source_slot(g, actor, &slot) && slot > 0 && slot <= g->reserved;
}
static void c_qc_print(struct c_qc_game *g, const char *text) {
  (void)g;
  fputs(text, stdout);
}
/* TS QuakeCSource.invoke. */
static void c_qc_invoke(struct c_qc_game *g, int64_t callback, int64_t slot, int64_t other, double time) {
  qa_compat_qc_machine *machine = &g->machine;
  int64_t self_offset = qa_compat_qc_machine_global_offset(machine, "self");
  int64_t other_offset = qa_compat_qc_machine_global_offset(machine, "other");
  int32_t self = qa_compat_qc_words_int(&machine->globals, self_offset);
  int32_t saved_other = qa_compat_qc_words_int(&machine->globals, other_offset);
  qa_compat_qc_words_set_int(&machine->globals, self_offset,
      (double)qa_compat_qc_entity_memory_reference(&g->entities, slot));
  qa_compat_qc_words_set_int(&machine->globals, other_offset, (double)other);
  qa_compat_qc_words_set_float(&machine->globals,
      qa_compat_qc_machine_global_offset(machine, "time"), time);
  qa_compat_qc_machine_execute(machine, callback, 0);
  qa_compat_qc_words_set_int(&machine->globals, self_offset, self);
  qa_compat_qc_words_set_int(&machine->globals, other_offset, saved_other);
}
/* TS QuakeCSource.runThink. */
static void c_qc_run_think(struct c_qc_game *g, qa_owned_actor actor, qa_frame_context frame) {
  int64_t slot = 0;
  if (!c_qc_source_slot(g, actor.id, &slot)) return;
  qa_compat_qc_words *words = qa_compat_qc_entity_memory_at(&g->entities, slot);
  double due = qa_compat_qc_words_float(words, c_qc_field(g, "nextthink"));
  qa_clock_profile profile; memset(&profile, 0, sizeof profile);
  profile.kind = QA_CLOCK_Q1_NETQUAKE;
  profile.minimum_frame_seconds = 0.001; profile.maximum_frame_seconds = 0.1;
  qa_source_time_result time = qa_think_callback_time(profile,
      (qa_source_time){QA_SOURCE_TIME_SECONDS, due}, frame);
  if (!time.has_value) return;
  qa_compat_qc_words_set_float(words, c_qc_field(g, "nextthink"), 0);
  g->think_count++;
  c_qc_invoke(g, qa_compat_qc_words_int(words, c_qc_field(g, "think")), slot, 0, time.value.value);
}
static void c_qc_think_callback(void *ctx, qa_owned_actor self, qa_frame_context frame) {
  c_qc_run_think(ctx, self, frame);
}
static void c_qc_touch_callback(void *ctx, qa_touch_contact contact) {
  struct c_qc_game *g = ctx;
  int64_t slot = 0;
  if (!c_qc_source_slot(g, contact.self.id, &slot)) return;
  qa_compat_qc_words *words = qa_compat_qc_entity_memory_at(&g->entities, slot);
  int32_t callback = qa_compat_qc_words_int(words, c_qc_field(g, "touch"));
  if (callback != 0 && qa_compat_qc_words_float(words, c_qc_field(g, "solid")) != 0)
    c_qc_invoke(g, callback, slot,
        qa_compat_qc_world_host_reference(&g->world_host, contact.other), g->current_time);
}
/* Physics source binding over QcActorState (TS actorMotion /
 * actorCollision / actorFlags / writeActorFlags for quakec). */
static bool c_qc_phys_collision(void *ctx, qa_app_sim_shared_solid *out) {
  c_qc_actor_ctx *entry = ctx;
  qa_compat_qc_shared_solid solid;
  if (!qa_compat_qc_actor_state_collision(&entry->game->actor_state, entry->actor, &solid))
    qa_error("Missing live QC collision");
  memset(out, 0, sizeof *out);
  out->solid = solid.solid; out->has_model = solid.has_model; out->model = solid.model;
  out->family = solid.family;
  if (solid.has_owner) { entry->owner = solid.owner; out->owner = &entry->owner; }
  out->has_monster = solid.has_monster; out->monster = solid.monster;
  out->has_dead_monster = solid.has_dead_monster; out->dead_monster = solid.dead_monster;
  out->q1_corpse = solid.q1_corpse; out->has_item = solid.has_item; out->item = solid.item;
  return true;
}
static bool c_qc_phys_motion(void *ctx, qa_q2_motion *out) {
  c_qc_actor_ctx *entry = ctx;
  qa_body_state_result body = qa_shared_body_table_read(
      qa_app_sim_shared_physics_bodies(entry->game->concrete->physics), entry->actor.id);
  if (!body.has_value
      || !qa_compat_qc_actor_state_motion(&entry->game->actor_state, entry->actor, body.value, out))
    qa_error("Missing live QC motion");
  return true;
}
static qa_app_sim_shared_physics_flags c_qc_phys_flags(void *ctx) {
  c_qc_actor_ctx *entry = ctx;
  qa_compat_qc_shared_physics_flags flags =
      qa_compat_qc_actor_state_flags(&entry->game->actor_state, entry->actor);
  qa_app_sim_shared_physics_flags out; memset(&out, 0, sizeof out);
  out.has_always_touch = flags.has_always_touch; out.always_touch = flags.always_touch;
  out.has_fly = flags.has_fly; out.fly = flags.fly;
  out.has_swim = flags.has_swim; out.swim = flags.swim;
  out.has_partial_ground = flags.has_partial_ground; out.partial_ground = flags.partial_ground;
  out.has_dead = flags.has_dead; out.dead = flags.dead;
  out.has_player = flags.has_player; out.player = flags.player;
  out.has_water_level = flags.has_water_level; out.water_level = flags.water_level;
  out.has_water_type = flags.has_water_type; out.water_type = flags.water_type;
  if (flags.has_enemy && !flags.enemy_is_null) { entry->enemy = flags.enemy; out.has_enemy = true; out.enemy = &entry->enemy; }
  else if (flags.has_enemy) out.has_enemy = true;
  out.has_delta_yaw = flags.has_delta_yaw; out.delta_yaw = flags.delta_yaw;
  out.has_team_slave = flags.has_team_slave; out.team_slave = flags.team_slave;
  { /* TS QuakeCActorState.flags() literal order. */
    static const unsigned char order[] = {QA_APP_SIM_FLAG_FLY, QA_APP_SIM_FLAG_SWIM, QA_APP_SIM_FLAG_PARTIAL_GROUND, QA_APP_SIM_FLAG_PLAYER,
                                          QA_APP_SIM_FLAG_WATER_LEVEL, QA_APP_SIM_FLAG_WATER_TYPE, QA_APP_SIM_FLAG_DEAD};
    qa_app_sim_physics_flags_order(&out, order, sizeof order);
  }
  return out;
}
static void c_qc_phys_write_flags(void *ctx, qa_app_sim_shared_physics_flags changes) {
  c_qc_actor_ctx *entry = ctx;
  qa_compat_qc_shared_physics_flags flags; memset(&flags, 0, sizeof flags);
  flags.has_always_touch = changes.has_always_touch; flags.always_touch = changes.always_touch;
  flags.has_fly = changes.has_fly; flags.fly = changes.fly;
  flags.has_swim = changes.has_swim; flags.swim = changes.swim;
  flags.has_partial_ground = changes.has_partial_ground; flags.partial_ground = changes.partial_ground;
  flags.has_dead = changes.has_dead; flags.dead = changes.dead;
  flags.has_player = changes.has_player; flags.player = changes.player;
  flags.has_water_level = changes.has_water_level; flags.water_level = changes.water_level;
  flags.has_water_type = changes.has_water_type; flags.water_type = changes.water_type;
  flags.has_enemy = changes.has_enemy;
  flags.enemy_is_null = changes.enemy == NULL;
  if (changes.enemy) flags.enemy = *changes.enemy;
  flags.has_delta_yaw = changes.has_delta_yaw; flags.delta_yaw = changes.delta_yaw;
  flags.has_team_slave = changes.has_team_slave; flags.team_slave = changes.team_slave;
  qa_compat_qc_actor_state_write_flags(&entry->game->actor_state, entry->actor, flags);
}
static void c_qc_phys_write_avelocity(void *ctx, qa_vec3 value) {
  c_qc_actor_ctx *entry = ctx;
  qa_compat_qc_actor_state_write_angular_velocity(&entry->game->actor_state, entry->actor, value);
}
/* TS QuakeCSource.admit (callbacks + physics source; the combat
 * authority binding over QC fields is not composed). */
static void c_qc_admit(void *ctx, qa_owned_actor actor, int64_t slot) {
  struct c_qc_game *g = ctx;
  (void)slot;
  qa_actor_callbacks callbacks; memset(&callbacks, 0, sizeof callbacks);
  callbacks.ctx = g; callbacks.think = c_qc_think_callback; callbacks.touch = c_qc_touch_callback;
  qa_actor_callback_table_bind(g->concrete->callbacks, actor, callbacks);
  size_t capacity = g->actor_ctx_capacity;
  g->actor_ctxs = c_grow(g->actor_ctxs, &capacity, g->actor_ctx_count + 1, sizeof *g->actor_ctxs);
  g->actor_ctx_capacity = capacity;
  c_qc_actor_ctx *entry = calloc(1, sizeof *entry);
  if (!entry) qa_range_error("Out of memory");
  g->actor_ctxs[g->actor_ctx_count++] = entry;
  entry->game = g; entry->actor = actor;
  qa_app_sim_source_body_physics source; memset(&source, 0, sizeof source);
  source.ctx = entry; source.collision = c_qc_phys_collision; source.motion = c_qc_phys_motion;
  source.flags = c_qc_phys_flags; source.write_flags = c_qc_phys_write_flags;
  source.write_angular_velocity = c_qc_phys_write_avelocity;
  (void)qa_app_sim_shared_physics_bind_source(g->concrete->physics, actor, source);
}
/* TS QuakeCSource models map + precache (resources prepared from
 * the concrete mounts, mirroring prepareQuakeCResources). */
static void c_qc_model_set(struct c_qc_game *g, const char *name, double index, qa_bounds bounds) {
  for (size_t i = 0; i < g->model_count_entries; i++)
    if (strcmp(g->models[i].name, name) == 0) { g->models[i].index = index; g->models[i].bounds = bounds; return; }
  size_t capacity = g->model_capacity;
  g->models = c_grow(g->models, &capacity, g->model_count_entries + 1, sizeof *g->models);
  g->model_capacity = capacity;
  g->models[g->model_count_entries++] = (c_qc_model){c_strdup(name), index, bounds};
}
static bool c_qc_model_lookup(void *ctx, const char *name, qa_compat_qc_spatial_model *out) {
  struct c_qc_game *g = ctx;
  if (name[0] == '\0') {
    memset(out, 0, sizeof *out);
    out->index = 0;
    return true;
  }
  for (size_t i = 0; i < g->model_count_entries; i++)
    if (strcmp(g->models[i].name, name) == 0) {
      memset(out, 0, sizeof *out);
      out->index = g->models[i].index; out->bounds = g->models[i].bounds;
      return true;
    }
  return false;
}
static bool c_qc_name_is_resource(const char *name) {
  size_t length = strlen(name);
  return length > 4 && (strcmp(name + length - 4, ".mdl") == 0 || strcmp(name + length - 4, ".spr") == 0
      || strcmp(name + length - 4, ".bsp") == 0 || strcmp(name + length - 4, ".wav") == 0);
}
static void c_qc_resource_add(struct c_qc_game *g, const char *name) {
  qa_app_sim_concrete_simulation *c = g->concrete;
  if (!c->options.mounts || !c_qc_name_is_resource(name)) return;
  for (size_t i = 0; i < g->resource_count; i++) if (strcmp(g->resources[i].name, name) == 0) return;
  qa_content_mounted *mounts = (qa_content_mounted *)c->options.mounts;
  bool wav = strcmp(name + strlen(name) - 4, ".wav") == 0;
  char *path = NULL;
  if (wav) {
    size_t length = strlen(name) + strlen("sound/") + 1;
    path = malloc(length);
    if (!path) qa_range_error("Out of memory");
    (void)snprintf(path, length, "sound/%s", name);
  }
  qa_content_opened_resource opened; memset(&opened, 0, sizeof opened);
  bool found = qa_content_mounted_open(mounts, wav ? path : name, NULL, NULL, &opened);
  free(path);
  if (!found) return; /* TS: conditional precache fails only if requested */
  c_qc_resource resource; memset(&resource, 0, sizeof resource);
  resource.name = c_strdup(name);
  if (opened.reference) resource.resource = *opened.reference;
  if (strcmp(name + strlen(name) - 4, ".mdl") == 0 || strcmp(name + strlen(name) - 4, ".spr") == 0) {
    qa_q12_model model;
    qa_parse_q12_model(opened.bytes, opened.byte_length, name, &model);
    qa_q12_model_destroy(&model);
    resource.has_bounds = true;
    resource.bounds = (qa_bounds){{-16, -16, -16}, {16, 16, 16}};
  } else if (strcmp(name + strlen(name) - 4, ".bsp") == 0) {
    qa_q1_map *map = calloc(1, sizeof *map);
    if (!map) qa_range_error("Out of memory");
    qa_q1_map_options options; memset(&options, 0, sizeof options);
    options.source = name;
    qa_q1_read_bsp(opened.bytes, opened.byte_length, &options, map);
    if (map->world.model_count == 0) qa_error("No world model in %s", name);
    resource.has_bounds = true;
    resource.bounds = map->world.models[0].bounds;
    qa_q1_map_destroy(map); free(map);
  }
  free(opened.bytes);
  size_t capacity = g->resource_capacity;
  g->resources = c_grow(g->resources, &capacity, g->resource_count + 1, sizeof *g->resources);
  g->resource_capacity = capacity;
  g->resources[g->resource_count++] = resource;
}
static bool c_qc_map_model_index(struct c_qc_game *g, const char *name, double *out) {
  const char *geometry = g->concrete->options.recipe.map.geometry.requested_path;
  if (c_is(name, geometry)) { *out = 1; return true; }
  if (name[0] != '*') return false;
  /* /^\*[1-9][0-9]*$/ then Number(): strtol also took "*01", "* 1" and "*+1". */
  bool digits = name[1] >= '1' && name[1] <= '9';
  for (const char *p = name + 1; digits && *p != '\0'; p++) digits = *p >= '0' && *p <= '9';
  double model = digits ? qa_js_string_to_number(name + 1) : -1;
  size_t models = g->concrete->q1_map ? g->concrete->q1_map->world.model_count : 0;
  if (model > 9007199254740991.0 || model < 1 || model >= (double)models)
    qa_error("Invalid actual QC inline model %s", name);
  *out = model + 1;
  return true;
}
static qa_compat_qc_precached_resource c_qc_precache_run(struct c_qc_game *g, const char *kind, const char *name) {
  size_t key_length = strlen(kind) + strlen(name) + 2;
  char *key = malloc(key_length);
  if (!key) qa_range_error("Out of memory");
  (void)snprintf(key, key_length, "%s:%s", kind, name);
  for (size_t i = 0; i < g->precached_count; i++)
    if (strcmp(g->precached[i].key, key) == 0) { qa_compat_qc_precached_resource value = g->precached[i].value; free(key); return value; }
  qa_compat_qc_precached_resource value; memset(&value, 0, sizeof value);
  double map_index = 0;
  if (c_is(kind, "model") && c_qc_map_model_index(g, name, &map_index)) {
    value.index = map_index;
    value.resource = g->concrete->options.recipe.map.geometry;
  } else {
    const c_qc_resource *resource = NULL;
    for (size_t i = 0; i < g->resource_count; i++)
      if (strcmp(g->resources[i].name, name) == 0) resource = &g->resources[i];
    if (!resource) { free(key); qa_error("Unprepared actual QC %s resource %s", kind, name); }
    double index = c_is(kind, "model") ? g->model_count++ : g->sound_count++;
    if (index >= 256) { free(key); qa_error("NetQuake %s precache limit exceeded", kind); }
    if (c_is(kind, "model")) {
      if (!resource->has_bounds) { free(key); qa_error("Missing actual model bounds %s", name); }
      c_qc_model_set(g, name, index, resource->bounds);
    }
    value.index = index; value.resource = resource->resource;
  }
  size_t capacity = g->precached_capacity;
  g->precached = c_grow(g->precached, &capacity, g->precached_count + 1, sizeof *g->precached);
  g->precached_capacity = capacity;
  g->precached[g->precached_count++] = (c_qc_precache){key, value};
  return value;
}
static qa_compat_qc_precached_resource c_qc_precache_service(void *ctx, const char *kind, const char *name) {
  return c_qc_precache_run(ctx, kind, name);
}
static bool c_qc_lookup_service(void *ctx, const char *kind, const char *name, qa_compat_qc_precached_resource *out) {
  struct c_qc_game *g = ctx;
  size_t key_length = strlen(kind) + strlen(name) + 2;
  char *key = malloc(key_length);
  if (!key) qa_range_error("Out of memory");
  (void)snprintf(key, key_length, "%s:%s", kind, name);
  bool found = false;
  for (size_t i = 0; i < g->precached_count; i++)
    if (strcmp(g->precached[i].key, key) == 0) { *out = g->precached[i].value; found = true; }
  free(key);
  return found;
}
static bool c_qc_loading(void *ctx) { return ((struct c_qc_game *)ctx)->spawning; }
static bool c_qc_client_service(void *ctx, qa_actor_id actor) { return c_qc_is_reserved_client(ctx, actor); }
static bool c_qc_native_false(void *ctx) { (void)ctx; return false; }
static bool c_qc_phs(void *ctx) {
  return qa_cvar_registry_variable_value(((struct c_qc_game *)ctx)->cvars, "sv_phs") != 0;
}
/* Presentation routing: no NetQuake/QW wire consumer is attached in
 * the concrete builder, so routed source messages are dropped here
 * (the TS local/native presentation paths are unresolved). */
static void c_qc_route_nq(void *ctx, const qa_netquake_message *messages, size_t count,
    qa_compat_qc_message_destination destination, const qa_actor_id *view_targets,
    const bool *view_has, size_t view_count) {
  (void)ctx; (void)messages; (void)count; (void)destination;
  (void)view_targets; (void)view_has; (void)view_count;
}
static void c_qc_route_qw(void *ctx, const qa_compat_qc_routed_message *entries, size_t count,
    qa_compat_qc_message_destination destination) {
  (void)ctx; (void)entries; (void)count; (void)destination;
}
static void c_qc_print_service(void *ctx, const char *text) { c_qc_print(ctx, text); }
static void c_qc_message_service(void *ctx, qa_network_event event, qa_actor_id actor) {
  struct c_qc_game *g = ctx;
  (void)event;
  /* TS: only an admitted (active) QC client may receive messages;
   * the concrete builder admits no QC clients yet. */
  (void)actor; (void)g;
  qa_error("QC message requires an admitted client");
}
static void c_qc_emit_event(struct c_qc_game *g, qa_content_id content, qa_q1_event event,
                            bool has_recipient, qa_actor_id recipient) {
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  source.kind = "q1"; source.q1 = &event;
  qa_presentation_owner owner; memset(&owner, 0, sizeof owner);
  (void)qa_app_sim_simulation_events_emit_owned(g->concrete->events, false, owner, content,
      source, false, g->concrete->source_frame.time, has_recipient, recipient);
}
static void c_qc_emit_service(void *ctx, qa_content_id content, qa_q1_event event) {
  qa_actor_id none; memset(&none, 0, sizeof none);
  c_qc_emit_event(ctx, content, event, false, none);
}
static void c_qc_broadcast_emit(void *ctx, qa_q1_event effect, const qa_actor_id *recipient) {
  struct c_qc_game *g = ctx;
  qa_actor_id none; memset(&none, 0, sizeof none);
  c_qc_emit_event(g, g->prepared->execution.owner.content, effect,
                  recipient != NULL, recipient ? *recipient : none);
}
static void c_qc_register_resource(void *ctx, qa_content_id content, const char *path,
                                   qa_resolved_resource_reference resource) {
  (void)ctx; (void)content; (void)path; (void)resource;
}
static qa_owned_actor c_qc_presentation_actor(void *ctx, int64_t slot) {
  return qa_compat_qc_world_host_actor(&((struct c_qc_game *)ctx)->world_host, slot);
}
static bool c_qc_presentation_slot_actor(void *ctx, int64_t slot, qa_actor_id *out) {
  qa_owned_actor actor;
  if (!qa_source_actor_slots_at(((struct c_qc_game *)ctx)->slots, slot, &actor)) return false;
  *out = actor.id;
  return true;
}
/* Extra host builtins installed by the TS QuakeCSource constructor. */
static void c_qc_host_dprint(void *ctx, qa_compat_qc_machine *machine) {
  struct c_qc_game *g = ctx;
  if (qa_cvar_registry_variable_value(g->cvars, "developer") != 0) {
    char *text = qa_compat_qc_machine_var_string(machine, 0);
    if (text) { c_qc_print(g, text); free(text); }
  }
}
static void c_qc_host_cvar(void *ctx, qa_compat_qc_machine *machine) {
  qa_compat_qc_machine_return_float(machine,
      qa_cvar_registry_variable_value(((struct c_qc_game *)ctx)->cvars,
                                      qa_compat_qc_machine_arg_string(machine, 0)));
}
static void c_qc_host_cvar_set(void *ctx, qa_compat_qc_machine *machine) {
  struct c_qc_game *g = ctx;
  const char *name = qa_compat_qc_machine_arg_string(machine, 0);
  char *name_copy = c_strdup(name);
  const char *value = qa_compat_qc_machine_arg_string(machine, 1);
  qa_cvar_snapshot snapshot;
  (void)qa_cvar_registry_set(g->cvars, name_copy, value, false, &snapshot);
  if (strcmp(name_copy, "sv_gravity") == 0)
    qa_app_sim_shared_physics_set_world_gravity(g->concrete->physics,
        qa_cvar_registry_variable_value(g->cvars, name_copy));
  free(name_copy);
}
static void c_qc_host_setcolor(void *ctx, qa_compat_qc_machine *machine) {
  (void)ctx;
  qa_compat_qc_words *words = qa_compat_qc_entity_memory_from_reference(
      machine->entities, qa_compat_qc_machine_arg_int(machine, 0));
  qa_compat_qc_words_set_float(words, qa_compat_qc_machine_field_offset(machine, "colormap"),
      trunc(qa_compat_qc_machine_arg_float(machine, 1)));
}
static void c_qc_host_setspawnparms(void *ctx, qa_compat_qc_machine *machine) {
  (void)ctx; (void)machine;
  qa_error("QC setspawnparms requires a current source client");
}
static void c_qc_host_changelevel(void *ctx, qa_compat_qc_machine *machine) {
  struct c_qc_game *g = ctx;
  if (g->change_level_issued) return;
  g->change_level_issued = true;
  free(g->change_level_map);
  g->change_level_map = c_strdup(qa_compat_qc_machine_arg_string(machine, 0));
}
static double c_qc_random_integer(void *ctx) { return qa_app_sim_source_random_next_integer(ctx); }
static double c_qc_random_unit(void *ctx) { return qa_app_sim_source_random_next_unit(ctx); }
static qa_random_state c_qc_random_checkpoint(void *ctx) {
  qa_app_sim_source_random_state state = qa_app_sim_source_random_checkpoint(ctx);
  qa_random_state out; memset(&out, 0, sizeof out);
  out.kind = c_is(state.kind, "q2-rerelease-mt19937") ? QA_RANDOM_STATE_Q2_RERELEASE_MT19937
                                                     : QA_RANDOM_STATE_GLIBC_RANDOM;
  return out;
}
static qa_source_time c_qc_slots_now(void *ctx) {
  return (qa_source_time){QA_SOURCE_TIME_SECONDS, ((struct c_qc_game *)ctx)->current_time};
}
static void c_qc_slots_unlink(void *ctx, qa_owned_actor actor) {
  struct c_qc_game *g = ctx;
  qa_shared_body_table_unlink(qa_app_sim_shared_physics_bodies(g->concrete->physics), actor);
}
static void c_qc_slots_exhausted(void *ctx, int64_t slot) {
  (void)ctx; (void)slot;
  qa_error("QC source edicts exhausted");
}
static double c_qc_point_leaf(void *ctx, qa_vec3 point) {
  return qa_shared_point_leaf(((struct c_qc_game *)ctx)->concrete->scene, point);
}
static double c_qc_leaf_cluster(void *ctx, double leaf) {
  return qa_shared_leaf_cluster(((struct c_qc_game *)ctx)->concrete->scene, leaf);
}
static bool c_qc_cluster_visible(void *ctx, double from, double to, const char *kind) {
  return qa_shared_cluster_visible(((struct c_qc_game *)ctx)->concrete->scene, from, to, kind);
}
static bool c_qc_server_active(void *ctx) { return !((struct c_qc_game *)ctx)->spawning; }
static double c_qc_server_time(void *ctx) { return ((struct c_qc_game *)ctx)->current_time; }
static void c_qc_touch_triggers_service(void *ctx, qa_owned_actor actor) {
  qa_app_sim_shared_physics_touch_triggers(((struct c_qc_game *)ctx)->concrete->physics, actor);
}
static bool c_qc_actor_state_source_slot(void *ctx, qa_actor_id actor, int64_t *out) {
  return c_qc_source_slot(ctx, actor, out);
}
static bool c_qc_actor_state_reference(void *ctx, int64_t reference, qa_actor_id *out) {
  struct c_qc_game *g = ctx;
  qa_owned_actor actor;
  if (!qa_source_actor_slots_at(g->slots, qa_compat_qc_entity_memory_slot(&g->entities, reference), &actor))
    return false;
  *out = actor.id;
  return true;
}
static bool c_qc_actor_state_is_client(void *ctx, qa_actor_id actor) {
  return c_qc_is_reserved_client(ctx, actor);
}
static double c_qc_aim_threshold(void *ctx) {
  return qa_cvar_registry_variable_value(((struct c_qc_game *)ctx)->cvars, "sv_aim");
}
static double c_qc_teamplay(void *ctx) {
  return qa_cvar_registry_variable_value(((struct c_qc_game *)ctx)->cvars, "teamplay");
}
static const qa_compat_qc_aim_target_ref *c_qc_aim_targets(void *ctx, size_t *out_count) {
  struct c_qc_game *g = ctx;
  g->aim_target_count = 0;
  int64_t count = qa_compat_qc_entity_memory_get_count(&g->entities);
  for (int64_t slot = 1; slot < count; slot++) {
    qa_owned_actor actor;
    if (!qa_source_actor_slots_at(g->slots, slot, &actor)) continue;
    if (!qa_session_actor_registry_is_live(g->concrete->actors, actor.id)) continue;
    size_t capacity = g->aim_target_capacity;
    g->aim_targets = c_grow(g->aim_targets, &capacity, g->aim_target_count + 1, sizeof *g->aim_targets);
    g->aim_target_capacity = capacity;
    g->aim_targets[g->aim_target_count++] =
        (qa_compat_qc_aim_target_ref){actor.id, qa_compat_qc_entity_memory_reference(&g->entities, slot)};
  }
  *out_count = g->aim_target_count;
  return g->aim_targets;
}
static void c_qc_host_add(struct c_qc_game *g, const qa_compat_qc_host_binding *bindings, size_t count) {
  for (size_t i = 0; i < count; i++) {
    bool replaced = false;
    for (size_t j = 0; j < g->host_binding_count; j++)
      if (c_is(g->host_bindings[j].name, bindings[i].name)) { g->host_bindings[j] = bindings[i]; replaced = true; }
    if (!replaced) {
      if (g->host_binding_count >= sizeof g->host_bindings / sizeof g->host_bindings[0])
        qa_error("QC host builtin table overflow");
      g->host_bindings[g->host_binding_count++] = bindings[i];
    }
  }
}
static void c_qc_host_set(struct c_qc_game *g, const char *name, void (*fn)(void *, qa_compat_qc_machine *)) {
  qa_compat_qc_host_binding binding = {name, {g, fn}};
  c_qc_host_add(g, &binding, 1);
}
static void c_qc_cvar_register(struct c_qc_game *g, const char *name, const char *value) {
  qa_cvar_read read;
  if (!qa_cvar_registry_find(g->cvars, name, &read)) {
    qa_cvar_snapshot snapshot;
    (void)qa_cvar_registry_register(g->cvars, name, value, 0, &snapshot);
  }
}
static bool c_qc_is_free_service(void *ctx, int64_t slot) {
  return qa_compat_qc_world_host_is_free_entity(&((struct c_qc_game *)ctx)->world_host, slot);
}
static int64_t c_qc_foreign_reference(void *ctx, qa_actor_id actor) {
  (void)ctx; (void)actor;
  /* TS QcBorrowedActors requires source classname metadata for
   * actors outside the QC slot table; not composed here. */
  qa_error("QC borrowed actor requires source classname metadata");
}
static void c_qc_prepare_resources(struct c_qc_game *g) {
  qa_compat_qc_program *program = g->program;
  for (size_t offset = 0; offset < program->string_length;) {
    char *name = qa_compat_qc_byte_string(program->strings, program->string_length, (int64_t)offset);
    if (!name) qa_range_error("Out of memory");
    if (c_qc_name_is_resource(name)) c_qc_resource_add(g, name);
    offset += strlen(name) + 1;
    free(name);
  }
  const qa_q1_map *map = g->concrete->q1_map;
  if (map) for (size_t i = 0; i < map->entity_count; i++)
    for (size_t j = 0; j < map->entity_list[i].property_count; j++)
      c_qc_resource_add(g, map->entity_list[i].properties[j].value);
}
/* TS QuakeCSource constructor (fresh, non-restore path). */
static struct c_qc_game *c_qc_create(qa_app_sim_concrete_simulation *c) {
  const qa_app_sim_prepared_quakec_source *prepared = c->options.prepared_quakec;
  qa_compat_qc_program *program = (qa_compat_qc_program *)&prepared->program;
  struct c_qc_game *g = calloc(1, sizeof *g);
  if (!g) qa_range_error("Out of memory");
  g->concrete = c; g->prepared = prepared; g->program = program;
  g->quakeworld = c_is(prepared->kind, "quakeworld");
  if (!isfinite(c->options.max_clients) || c->options.max_clients < 1 || c->options.max_clients >= 2048
      || c->options.max_clients != trunc(c->options.max_clients))
    qa_error("Invalid dedicated QC source timing or reserved clients");
  if (g->quakeworld && c->options.max_clients > 32)
    qa_error("Native QuakeWorld supports at most 32 clients");
  g->reserved = g->quakeworld ? 32 : (int64_t)c->options.max_clients;
  g->current_time = c_seconds(c->source_frame.time);
  g->spawning = true;
  g->random = (qa_random_source){c->random, c_qc_random_integer, c_qc_random_unit, c_qc_random_checkpoint};
  if (!qa_compat_qc_entity_memory_init(&g->entities, qa_compat_qc_classic_entity_layout(program),
                                        g->quakeworld ? 768 : 2048, g->reserved + 1))
    qa_range_error("Out of memory");
  qa_compat_qc_edict_metadata_layout metadata = {0, g->quakeworld ? 100 : 92};
  qa_source_slot_storage storage = qa_compat_qc_create_source_slot_storage(
      program, &g->entities, metadata, &g->slot_storage_state);
  memset(&g->slots_options, 0, sizeof g->slots_options);
  g->slots_options.provider = prepared->execution.owner.provider;
  g->slots_options.capacity = (double)g->entities.capacity;
  g->slots_options.lifetime = qa_quake_edict_lifetime((double)(g->reserved + 1), NULL);
  g->slots_options.storage = storage;
  g->slots_options.ctx = g;
  g->slots_options.now = c_qc_slots_now;
  g->slots_options.unlink = c_qc_slots_unlink;
  g->slots_options.exhausted = c_qc_slots_exhausted;
  g->slots = qa_source_actor_slots_create(c->actors, g->slots_options);
  if (!g->slots) qa_range_error("Out of memory");
  for (int64_t slot = 0; slot <= g->reserved; slot++)
    (void)qa_source_actor_slots_bind_existing(g->slots, slot,
        slot == 0 ? "quakec:worldspawn" : "quakec:reserved-client");
  size_t world_models = c->q1_map ? c->q1_map->world.model_count : 1;
  g->model_count = (double)world_models + 1;
  g->sound_count = 1;
  const char *geometry_path = c->options.recipe.map.geometry.requested_path;
  if (geometry_path) c_qc_model_set(g, geometry_path, 1, qa_shared_model_bounds(c->scene, 0));
  for (size_t model = 1; model < world_models; model++) {
    char name[32];
    (void)snprintf(name, sizeof name, "*%zu", model);
    c_qc_model_set(g, name, (double)model + 1, qa_shared_model_bounds(c->scene, (double)model));
  }
  c_qc_prepare_resources(g);
  qa_compat_qc_world_host_options world_options; memset(&world_options, 0, sizeof world_options);
  world_options.program = program; world_options.entities = &g->entities;
  world_options.actors = c->actors; world_options.slots = g->slots;
  world_options.slots_options = g->slots_options;
  world_options.bodies = qa_app_sim_shared_physics_bodies(c->physics);
  world_options.scene = qa_shared_as_scene_queries(c->scene);
  world_options.numeric = qa_q1_donor_profile;
  world_options.ctx = g; world_options.model = c_qc_model_lookup;
  world_options.foreign_reference = c_qc_foreign_reference;
  world_options.admit = c_qc_admit;
  if (!qa_compat_qc_world_host_init(&g->world_host, world_options)) qa_range_error("Out of memory");
  qa_cvar_registry_options cvar_options; memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = g->quakeworld ? QA_COMMAND_DIALECT_Q1_QUAKEWORLD : QA_COMMAND_DIALECT_Q1_NETQUAKE;
  cvar_options.context.session = c->session;
  cvar_options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  g->cvars = qa_cvar_registry_create(&cvar_options);
  if (!g->cvars) qa_range_error("Out of memory");
  char skill[32], max_clients[32];
  (void)snprintf(skill, sizeof skill, "%s", QA_JS_NUMBER(trunc(c->options.skill)));
  (void)snprintf(max_clients, sizeof max_clients, "%s", QA_JS_NUMBER(trunc(c->options.max_clients)));
  const struct { const char *name, *value; } defaults[] = {
    {"skill", skill}, {"deathmatch", c_is(c->options.mode, "deathmatch") ? "1" : "0"},
    {"coop", c_is(c->options.mode, "coop") ? "1" : "0"}, {"teamplay", "0"}, {"sv_cheats", "0"},
    {"sv_aim", g->quakeworld ? "2" : "0.93"}, {"sv_gravity", "800"}, {"sv_maxspeed", "320"},
    {"samelevel", "0"}, {"timelimit", "0"}, {"fraglimit", "0"}, {"gamecfg", "0"},
    {"registered", "1"}, {"maxclients", max_clients}, {"campaign", "0"}, {"developer", "0"},
  };
  for (size_t i = 0; i < sizeof defaults / sizeof defaults[0]; i++)
    c_qc_cvar_register(g, defaults[i].name, defaults[i].value);
  qa_compat_qc_client_host_services client_services; memset(&client_services, 0, sizeof client_services);
  client_services.scene.ctx = g;
  client_services.scene.point_leaf = c_qc_point_leaf;
  client_services.scene.leaf_cluster = c_qc_leaf_cluster;
  client_services.scene.cluster_visible = c_qc_cluster_visible;
  client_services.scene.leaf_count = c->q1_map ? c->q1_map->world.leaf_count : 0;
  client_services.max_clients = g->reserved;
  client_services.ctx = g; client_services.server_time = c_qc_server_time;
  if (!qa_compat_qc_client_host_init(&g->clients, &g->world_host, client_services))
    qa_range_error("Out of memory");
  g->presentation_world = (qa_compat_qc_presentation_world){g, program, &g->entities,
      g->world_host.host_entries, g->world_host.host_count,
      c_qc_presentation_actor, c_qc_presentation_slot_actor};
  memset(&g->nq, 0, sizeof g->nq);
  g->nq.ctx = g; g->nq.message_dialect = "known-retail"; g->nq.has_message_dialect = true;
  g->nq.native = c_qc_native_false; g->nq.loading = c_qc_loading;
  g->nq.client = c_qc_client_service; g->nq.route = c_qc_route_nq;
  memset(&g->qw, 0, sizeof g->qw);
  g->qw.ctx = g; g->qw.loading = c_qc_loading; g->qw.client = c_qc_client_service;
  g->qw.phs = c_qc_phs; g->qw.route = c_qc_route_qw;
  qa_compat_qc_presentation_services presentation_services;
  memset(&presentation_services, 0, sizeof presentation_services);
  presentation_services.ctx = g;
  presentation_services.qw = g->quakeworld ? &g->qw : NULL;
  presentation_services.nq = g->quakeworld ? NULL : &g->nq;
  presentation_services.content = prepared->execution.owner.content;
  presentation_services.precache = c_qc_precache_service;
  presentation_services.lookup = c_qc_lookup_service;
  presentation_services.loading = c_qc_loading;
  presentation_services.print = c_qc_print_service;
  presentation_services.message = c_qc_message_service;
  presentation_services.emit = c_qc_emit_service;
  presentation_services.register_resource = c_qc_register_resource;
  qa_compat_qc_presentation_bindings presentation = qa_compat_qc_create_presentation_bindings(
      &g->presentation_world, presentation_services, &g->presentation_state);
  qa_compat_qc_movement_services movement_services; memset(&movement_services, 0, sizeof movement_services);
  movement_services.scene = qa_shared_as_scene_queries(c->scene);
  movement_services.random = g->random;
  movement_services.ctx = g; movement_services.touch_triggers = c_qc_touch_triggers_service;
  qa_compat_qc_movement_bindings movement = qa_compat_qc_create_movement_bindings(
      &g->world_host, movement_services, &g->movement_state);
  if (!qa_compat_qc_broadcast_messages_init(&g->messages, &g->presentation_world, g,
        c_qc_broadcast_emit, g->quakeworld ? &g->qw : NULL, g->quakeworld ? NULL : &g->nq))
    qa_range_error("Out of memory");
  c_qc_host_add(g, g->world_host.host_entries, g->world_host.host_count);
  c_qc_host_add(g, presentation.entries, presentation.count);
  c_qc_host_add(g, movement.entries, movement.count);
  c_qc_host_add(g, g->clients.host_entries, g->clients.host_count);
  c_qc_host_add(g, g->messages.host_entries, g->messages.host_count);
  c_qc_host_set(g, "dprint", c_qc_host_dprint);
  c_qc_host_set(g, "cvar", c_qc_host_cvar);
  c_qc_host_set(g, "cvar_set", c_qc_host_cvar_set);
  c_qc_host_set(g, "setcolor", c_qc_host_setcolor);
  c_qc_host_set(g, "setspawnparms", c_qc_host_setspawnparms);
  c_qc_host_set(g, "changelevel", c_qc_host_changelevel);
  memset(&g->aim_state, 0, sizeof g->aim_state);
  g->aim_state.world = &g->world_host; g->aim_state.ctx = g;
  g->aim_state.aim_threshold = c_qc_aim_threshold; g->aim_state.teamplay = c_qc_teamplay;
  g->aim_state.targets = c_qc_aim_targets;
  qa_compat_qc_host_binding aim = {"aim", qa_compat_qc_create_aim_binding(&g->world_host, &g->aim_state)};
  c_qc_host_add(g, &aim, 1);
  bool has_remove = false;
  for (size_t i = 0; i < g->host_binding_count; i++) if (c_is(g->host_bindings[i].name, "remove")) has_remove = true;
  if (!has_remove) qa_error("Missing QC remove builtin");
  qa_compat_qc_builtin_services builtin_services; memset(&builtin_services, 0, sizeof builtin_services);
  builtin_services.kind = qa_compat_qc_host_kind_for_program(program);
  builtin_services.random = &g->random;
  builtin_services.ctx = g; builtin_services.is_free_entity = c_qc_is_free_service;
  builtin_services.host = g->host_bindings; builtin_services.host_count = g->host_binding_count;
  memset(&g->registry, 0, sizeof g->registry);
  if (!qa_compat_qc_create_builtins(builtin_services, &g->registry)) qa_range_error("Out of memory");
  qa_compat_qc_machine_options machine_options; memset(&machine_options, 0, sizeof machine_options);
  machine_options.program = program; machine_options.entities = &g->entities;
  machine_options.numeric = qa_create_numeric_operations(&qa_q1_donor_profile);
  machine_options.builtins = &g->registry;
  machine_options.ctx = g; machine_options.server_active = c_qc_server_active;
  if (!qa_compat_qc_machine_init(&g->machine, machine_options)) qa_range_error("Out of memory");
  g->has_machine = true;
  qa_compat_qc_actor_state_options state_options; memset(&state_options, 0, sizeof state_options);
  state_options.machine = &g->machine;
  state_options.rerelease = c->options.recipe.engine_behavior.content
      && strncmp(c->options.recipe.engine_behavior.content, "q1:rerelease:", strlen("q1:rerelease:")) == 0;
  state_options.ctx = g;
  state_options.source_slot = c_qc_actor_state_source_slot;
  state_options.reference = c_qc_actor_state_reference;
  state_options.is_client = c_qc_actor_state_is_client;
  qa_compat_qc_actor_state_init(&g->actor_state, state_options);
  for (int64_t slot = 0; slot <= g->reserved; slot++) (void)qa_compat_qc_world_host_actor(&g->world_host, slot);
  return g;
}
static void c_qc_destroy(struct c_qc_game *g) {
  if (!g) return;
  if (g->has_machine) qa_compat_qc_machine_destroy(&g->machine);
  qa_compat_qc_registry_destroy(&g->registry);
  qa_compat_qc_broadcast_messages_destroy(&g->messages);
  qa_compat_qc_client_host_destroy(&g->clients);
  if (g->cvars) qa_cvar_registry_destroy(g->cvars);
  qa_compat_qc_world_host_destroy(&g->world_host);
  qa_source_actor_slots_destroy(g->slots);
  qa_compat_qc_entity_memory_destroy(&g->entities);
  for (size_t i = 0; i < g->model_count_entries; i++) free(g->models[i].name);
  free(g->models);
  for (size_t i = 0; i < g->precached_count; i++) free(g->precached[i].key);
  free(g->precached);
  for (size_t i = 0; i < g->resource_count; i++) free(g->resources[i].name);
  free(g->resources);
  for (size_t i = 0; i < g->actor_ctx_count; i++) free(g->actor_ctxs[i]);
  free(g->actor_ctxs); free(g->aim_targets); free(g->change_level_map);
  free(g);
}
/* TS QuakeCSource.spawnMap over the decoded Q1 entity list. */
static void c_qc_spawn_map(struct c_qc_game *g) {
  qa_app_sim_concrete_simulation *c = g->concrete;
  if (!g->spawning) qa_error("QC map was already spawned");
  if (!c->q1_map) qa_error("QuakeC simulation requires a Q1 BSP entity list");
  qa_compat_qc_machine *machine = &g->machine;
  qa_compat_qc_words *world = qa_compat_qc_entity_memory_at(&g->entities, 0);
  const char *map = c->options.recipe.map.geometry.requested_path;
  int64_t word2 = c_qc_field(g, "model");
  double value3 = (double)qa_compat_qc_strings_allocate(&machine->strings, map ? map : "");
  qa_compat_qc_words_set_int(world, word2, value3);
  qa_compat_qc_words_set_float(world, c_qc_field(g, "modelindex"), 1);
  qa_compat_qc_words_set_float(world, c_qc_field(g, "solid"), 4);
  qa_compat_qc_words_set_float(world, c_qc_field(g, "movetype"), 7);
  qa_compat_qc_words_set_float(&machine->globals,
      qa_compat_qc_machine_global_offset(machine, "time"), g->current_time);
  for (size_t i = 0; i < 4; i++) {
    static const char *const names[4] = {"skill", "deathmatch", "coop", "teamplay"};
    int64_t offset = 0;
    if (qa_compat_qc_machine_global_offset_or_null(machine, names[i], &offset))
      qa_compat_qc_words_set_float(&machine->globals, offset,
          qa_cvar_registry_variable_value(g->cvars, names[i]));
  }
  char *map_name = c_strdup(map ? map : "");
  if (strncmp(map_name, "maps/", 5) == 0) memmove(map_name, map_name + 5, strlen(map_name + 5) + 1);
  size_t name_length = strlen(map_name);
  if (name_length > 4 && strcmp(map_name + name_length - 4, ".bsp") == 0) map_name[name_length - 4] = '\0';
  qa_compat_qc_words * w = &machine->globals;
  int64_t word = qa_compat_qc_machine_global_offset(machine, "mapname");
  double value2 = (double)qa_compat_qc_strings_allocate(&machine->strings, map_name);
  qa_compat_qc_words_set_int(w, word, value2);
  free(map_name);
  qa_owned_actor world_actor = qa_compat_qc_world_host_actor(&g->world_host, 0);
  for (size_t ordinal = 0; ordinal < c->q1_map->entity_count; ordinal++) {
    const qa_q1_entity *entity = &c->q1_map->entity_list[ordinal];
    qa_owned_actor actor = ordinal == 0 ? world_actor
                                        : qa_source_actor_slots_allocate(g->slots, "quakec:authored");
    int64_t slot = 0;
    if (!c_qc_source_slot(g, actor.id, &slot)) qa_error("Missing authored QC source slot");
    (void)qa_compat_qc_world_host_actor(&g->world_host, slot);
    qa_compat_qc_text_pair *pairs = NULL;
    if (entity->property_count) {
      pairs = malloc(entity->property_count * sizeof *pairs);
      if (!pairs) qa_range_error("Out of memory");
      for (size_t i = 0; i < entity->property_count; i++)
        pairs[i] = (qa_compat_qc_text_pair){entity->properties[i].key, entity->properties[i].value};
    }
    qa_compat_qc_entity_parse_result_compat applied;
    if (!qa_compat_qc_apply_entity_pairs(machine, slot, pairs, entity->property_count, &applied))
      qa_range_error("Out of memory");
    qa_compat_qc_text_pairs_free(&applied.unknown);
    qa_compat_qc_text_pairs_free(&applied.retained);
    free(pairs);
    double excluded = c_is(c->options.mode, "deathmatch") ? 2048
        : c->options.skill == 0 ? 256 : c->options.skill == 1 ? 512 : 1024;
    qa_compat_qc_words *words = qa_compat_qc_entity_memory_at(&g->entities, slot);
    if ((qa_js_to_int32(qa_compat_qc_words_float(words, c_qc_field(g, "spawnflags"))) & qa_js_to_int32(excluded)) != 0) /* spawnflags & excluded on a QC float */ {
      (void)qa_source_actor_slots_free(g->slots, actor);
      continue;
    }
    const char *classname = qa_compat_qc_strings_get(&machine->strings,
        qa_compat_qc_words_int(words, c_qc_field(g, "classname")));
    char *classname_copy = c_strdup(classname);
    const qa_compat_qc_function *spawn = classname_copy[0]
        ? qa_compat_qc_program_function_named_map(g->program, classname_copy) : NULL;
    if (!spawn) {
      char message[512];
      (void)snprintf(message, sizeof message, "%s for: %s\n",
                     classname_copy[0] ? "No spawn function" : "No classname", classname_copy);
      c_qc_print(g, message);
      free(classname_copy);
      (void)qa_source_actor_slots_free(g->slots, actor);
      continue;
    }
    free(classname_copy);
    g->spawn_count++;
    c_qc_invoke(g, spawn->index, slot, 0, g->current_time);
    qa_compat_qc_broadcast_messages_flush_signon(&g->messages);
  }
  g->spawning = false;
  qa_app_sim_shared_physics_set_world_gravity(c->physics,
      qa_cvar_registry_variable_value(g->cvars, "sv_gravity"));
  qa_compat_qc_broadcast_messages_flush(&g->messages);
}
/* TS QuakeCSource.beginFrame / endFrame. */
static void c_qc_begin_frame(struct c_qc_game *g, qa_frame_context frame) {
  if (frame.time.kind != QA_SOURCE_TIME_SECONDS || frame.elapsed.kind != QA_SOURCE_TIME_SECONDS
      || !isfinite(frame.time.value))
    qa_error("QC frame requires source seconds");
  g->current_time = frame.time.value;
  qa_compat_qc_words_set_float(&g->machine.globals,
      qa_compat_qc_machine_global_offset(&g->machine, "frametime"), frame.elapsed.value);
  qa_app_sim_shared_physics_set_world_gravity(g->concrete->physics,
      qa_cvar_registry_variable_value(g->cvars, "sv_gravity"));
  g->start_frame_count++;
  c_qc_invoke(g, qa_compat_qc_program_function_named(g->program, "StartFrame")->index,
              0, 0, g->current_time);
}
static void c_qc_end_frame(struct c_qc_game *g) {
  size_t effect_count = 0;
  qa_cvar_effect *effects = qa_cvar_registry_take_effects(g->cvars, &effect_count);
  for (size_t i = 0; i < effect_count; i++) qa_cvar_effect_free(&effects[i]);
  free(effects);
  qa_compat_qc_broadcast_messages_flush(&g->messages);
  int64_t offset = qa_compat_qc_machine_global_offset(&g->machine, "force_retouch");
  double current = qa_compat_qc_words_float(&g->machine.globals, offset);
  if (current != 0) {
    const qa_numeric_operations *numeric = qa_create_numeric_operations(&qa_q1_donor_profile);
    qa_compat_qc_words_set_float(&g->machine.globals, offset,
        numeric->subtract(numeric->ctx, current, 1));
  }
}
/* TS executeQuakeCActor + executeQuakeCPhysics (actor-execution.ts).
 * Movetype 7 uses the shared physics pusher step instead of the QC
 * pusher services; movetype 8 inlines moveQ1Noclip. */
static void c_qc_execute_actor(struct c_qc_game *g, qa_owned_actor actor,
                               qa_frame_context frame, double elapsed_seconds) {
  qa_app_sim_concrete_simulation *c = g->concrete;
  if (c_qc_is_reserved_client(g, actor.id)) return;
  int64_t slot = 0;
  if (!c_qc_source_slot(g, actor.id, &slot)) return;
  qa_compat_qc_words *words = qa_compat_qc_entity_memory_at(&g->entities, slot);
  double move = qa_compat_qc_words_float(words, c_qc_field(g, "movetype"));
  if (qa_compat_qc_words_float(&g->machine.globals,
        qa_compat_qc_machine_global_offset(&g->machine, "force_retouch")) != 0) {
    qa_compat_qc_world_host_link(&g->world_host, slot);
    qa_app_sim_shared_physics_touch_triggers(c->physics, actor);
  }
  if (move == 7) {
    (void)qa_app_sim_shared_physics_step(c->physics, actor, elapsed_seconds);
    return;
  }
  if (move == 4) {
    (void)qa_app_sim_shared_physics_step(c->physics, actor, elapsed_seconds);
    if (qa_session_actor_registry_is_live(c->actors, actor.id)) c_qc_run_think(g, actor, frame);
    return;
  }
  if (move != 0 && move != 5 && move != 6 && move != 8 && move != 9 && move != 10)
    qa_error("Unsupported nonclient QuakeC movetype %s", QA_JS_NUMBER(move));
  c_qc_run_think(g, actor, frame);
  if (!qa_session_actor_registry_is_live(c->actors, actor.id) || move == 0) return;
  if (move == 8) {
    qa_shared_body_table *bodies = qa_app_sim_shared_physics_bodies(c->physics);
    qa_body_state_result read = qa_shared_body_table_read(bodies, actor.id);
    if (!read.has_value) return;
    qa_q2_motion motion;
    if (!qa_compat_qc_actor_state_motion(&g->actor_state, actor, read.value, &motion)) return;
    qa_body_state body = read.value;
    const qa_numeric_operations *n = qa_create_numeric_operations(&qa_q1_donor_profile);
    body.origin = (qa_vec3){(float)(body.origin.x + body.velocity.x * elapsed_seconds),
                            (float)(body.origin.y + body.velocity.y * elapsed_seconds),
                            (float)(body.origin.z + body.velocity.z * elapsed_seconds)};
    body.angles = (qa_vec3){(float)(body.angles.x + motion.angular_velocity.x * elapsed_seconds),
                            (float)(body.angles.y + motion.angular_velocity.y * elapsed_seconds),
                            (float)(body.angles.z + motion.angular_velocity.z * elapsed_seconds)};
    (void)n;
    qa_shared_body_table_write(bodies, actor, body);
    qa_shared_body_table_link(bodies, actor, NULL);
  } else {
    (void)qa_app_sim_shared_physics_step(c->physics, actor, elapsed_seconds);
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
  /* TS createSource + spawn dispatch: a QuakeC execution constructs
   * the QuakeCSource game and runs spawnMap (authored spawn
   * functions over progs.dat); its world actor is source slot 0. */
  bool quakec_source = false;
  for (size_t i = 0; i < options.recipe.execution_count; i++)
    if (options.recipe.execution[i].kind == QA_EXECUTION_MODULE_QUAKEC) quakec_source = true;
  if (quakec_source) {
    c->qc = c_qc_create(c);
    c_qc_spawn_map(c->qc);
    if (!qa_source_actor_slots_at(c->qc->slots, 0, &c->world_actor))
      qa_error("Missing authored QC source slot");
    c->has_world_actor = true;
    if (out_state) *out_state = c;
    qa_app_sim_shared_simulation_host host; memset(&host, 0, sizeof host);
    host.ctx = c; host.step = c_step; host.step_async = c_step_async;
    host.checkpoint = c_checkpoint; host.close = c_close;
    return host;
  }
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
  } else {
    /* Q2/Q3 spawn origins come from the retained entity lump (TS
     * tagSpawnOrigins parses options.world.entities the same way). */
    const char *text = c->decoded.kind == QA_DECODED_WORLD_Q2 && c->decoded.as.q2
        ? c->decoded.as.q2->entities
        : c->decoded.kind == QA_DECODED_WORLD_Q3 && c->decoded.as.q3
            ? c->decoded.as.q3->entities : NULL;
    if (text) {
      size_t count = 0;
      qa_q3_entity *entities = qa_q3_parse_entities(text, NULL, &count);
      for (size_t i = 0; i < count; i++) {
        if (!c_is(qa_q3_entity_get(&entities[i], "classname"), "info_player_start")) continue;
        const char *spawn = qa_q3_entity_get(&entities[i], "origin");
        if (spawn) (void)sscanf(spawn, "%lf %lf %lf", &origin.x, &origin.y, &origin.z);
        const char *angle = qa_q3_entity_get(&entities[i], "angle");
        if (angle) angles.y = strtod(angle, NULL);
        break;
      }
      qa_q3_entities_destroy(entities, count);
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
/* TS preparesQ1Clients: source.kind "q1" (or a NetQuake QuakeC
 * source, not constructed here). The concrete source is the shared
 * movement source over the map entities provider. */
static bool c_prepares_q1_clients(const qa_app_sim_concrete_simulation *c) {
  const char *provider = c->options.recipe.map.entities.provider;
  return provider != NULL && strncmp(provider, "q1:", 3) == 0;
}
static bool c_netquake(const qa_app_sim_movement_player *player) {
  return c_is(player->profile.kind, "q1-netquake");
}
/* TS providerFrame projection for an actor turn: the frame a source
 * callback sees is the shared source frame projected into the
 * actor provider's clock (identity when the clock kinds match, and
 * for providers with no recipe timing the shared frame is used, as
 * on the TS generic-actor path). */
static qa_frame_context c_actor_frame(qa_app_sim_concrete_simulation *c, qa_actor_id actor) {
  qa_actor_source_result source = qa_session_actor_registry_source_of(c->actors, actor);
  const char *provider = source.has_value ? source.provider : NULL;
  if (!provider) {
    qa_actor_observation_result observed = qa_session_actor_registry_observe(c->actors, actor);
    if (observed.has_value) provider = observed.value.owner;
  }
  if (!provider) return c->source_frame;
  for (size_t i = 0; i < c->options.recipe.timing_count; i++)
    if (c_is(c->options.recipe.timing[i].provider, provider))
      return qa_app_sim_provider_frame(c->source_frame, c->world_clock, c->options.recipe.timing[i].clock);
  return c->source_frame;
}
/* TS step() run-entry client preparation (preparesQ1Clients): fresh
 * NetQuake commands are received, then every NetQuake player is
 * prepared in the client-command phase. Receiving stamps
 * lastSequence, so the command phase below skips these commands and
 * movement happens in the player's ordered turn instead. Bot
 * commands (botServices.frame) are not constructed in concrete. */
static void c_prepare_q1_clients(qa_app_sim_concrete_simulation *c, qa_input_batch input) {
  for (size_t i = 0; i < input.command_count; i++) {
    qa_actor_command command = input.commands[i];
    concrete_player *entry = c_player_for(c, command.actor);
    if (!entry) qa_error("Command targets an unadmitted player");
    qa_app_sim_movement_player *player = entry->player;
    if (!c_netquake(player) || command.sequence <= player->last_sequence) continue;
    qa_app_sim_movement_player_receive_netquake(player, command);
  }
  for (size_t i = 0; i < c->player_count; i++) {
    qa_app_sim_movement_player *player = c->players[i].player;
    if (!c_netquake(player) || player->has_cutscene || player->intermission) continue;
    qa_frame_context frame = c->source_frame; frame.phase = QA_FRAME_PHASE_CLIENT_COMMAND;
    qa_app_sim_movement_player_prepare_netquake(player, frame, NULL, NULL);
  }
}
/* TS step() command phase (commandTurn commands only): one movement
 * turn per fresh command, attachments committed after each. The
 * movement frame carries the host elapsed in milliseconds, clamped
 * to [1, 100] for NetQuake profiles. On a run boundary of a Q1
 * source the NetQuake commands were already received above and the
 * sequence check skips them here. */
static void c_run_commands(qa_app_sim_concrete_simulation *c, qa_input_batch input) {
  for (size_t i = 0; i < input.command_count; i++) {
    qa_actor_command command = input.commands[i];
    concrete_player *entry = c_player_for(c, command.actor);
    if (!entry) qa_error("Command targets an unadmitted player");
    qa_app_sim_movement_player *player = entry->player;
    if (command.sequence <= player->last_sequence) continue;
    if (player->has_cutscene) {
      /* TS execute() cutscene branch: record buttons, no movement. */
      player->previous_buttons = player->buttons;
      player->last_sequence = command.sequence;
      continue;
    }
    qa_frame_context frame = c->source_frame;
    frame.phase = QA_FRAME_PHASE_CLIENT_COMMAND;
    frame.elapsed = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS,
        c_netquake(player) ? qa_js_min(100, qa_js_max(1, input.elapsed_milliseconds))
                           : input.elapsed_milliseconds};
    (void)qa_app_sim_movement_player_move(player, command, frame, NULL);
    qa_app_sim_shared_physics_commit_attachments(c->physics);
  }
}
/* TS step() ordered actor turns for one boundary. Dispatch, in TS
 * order, over the actors the concrete builder owns: NetQuake client
 * players physics in their turn on run boundaries of a Q1 source;
 * other players already moved in the command phase (their weapon
 * turn is the c_move_weapon pass-through); every other actor takes
 * the generic path — scheduler during-physics think in its provider
 * frame, then the shared physics step — and only on run boundaries.
 * Source execution entries (TS actorExecutions: q1/q2/q3/QuakeC,
 * selected/monster/foreign sources on q2/q3-only boundaries) are
 * not constructed in concrete, so non-run boundaries dispatch no
 * generic turn, exactly as the TS `if (!run) continue` tail. */
static void c_run_actor_turns(qa_app_sim_concrete_simulation *c, bool run, bool prepares,
                              double elapsed_seconds) {
  qa_app_sim_ordered_actor_turns *turns = qa_app_sim_ordered_actor_turns_create(c->actors, c_position_of, c);
  qa_app_sim_actor_visited visited; memset(&visited, 0, sizeof visited);
  qa_owned_actor actor;
  while (qa_app_sim_ordered_actor_turns_next(turns, &visited, &actor)) {
    concrete_player *entry = c_player_for(c, actor.id);
    if (run && entry && prepares && c_netquake(entry->player)) {
      qa_app_sim_movement_player *player = entry->player;
      if (qa_session_actor_registry_is_live(c->actors, actor.id)
          && !player->has_cutscene && !player->intermission) {
        qa_frame_context frame = c->source_frame; frame.phase = QA_FRAME_PHASE_ENTITY_PHYSICS;
        (void)qa_app_sim_movement_player_physics_netquake(player, frame);
      } else qa_app_sim_movement_player_finish_netquake_input(player, false);
      qa_app_sim_shared_physics_commit_attachments(c->physics);
      continue;
    }
    if (!run) continue;
    if (entry) { /* TS playerWeapon turn; identity in concrete. */
      qa_app_sim_shared_physics_commit_attachments(c->physics);
      continue;
    }
    qa_frame_context frame = c_actor_frame(c, actor.id);
    frame.phase = QA_FRAME_PHASE_ENTITY_THINK;
    if (c->qc) {
      /* TS executeActor quakec entry: think + physics by movetype. */
      int64_t qc_slot = 0;
      if (c_qc_source_slot(c->qc, actor.id, &qc_slot)) {
        c_qc_execute_actor(c->qc, actor, frame, elapsed_seconds);
        qa_app_sim_shared_physics_commit_attachments(c->physics);
        continue;
      }
    }
    (void)qa_frame_scheduler_run(c->scheduler, actor.id, frame, QA_THINK_DURING_PHYSICS);
    if (qa_session_actor_registry_is_live(c->actors, actor.id))
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
  /* No concrete paused source (TS lmctf pause) or selected/foreign
   * source clocks exist yet: selected/scheduling always accumulate
   * and the plan carries no q2/q3 weapon or extra-clock deadlines. */
  c->selected_milliseconds += input.elapsed_milliseconds;
  c->source_scheduling_milliseconds += input.elapsed_milliseconds;
  qa_app_sim_frame_plan_input plan_input; memset(&plan_input, 0, sizeof plan_input);
  plan_input.profile = c->world_clock;
  plan_input.elapsed_milliseconds = input.elapsed_milliseconds;
  plan_input.time_seconds = c_seconds(c->source_frame.time);
  plan_input.source_scheduling_milliseconds = c->source_scheduling_milliseconds;
  plan_input.selected_milliseconds = c->selected_milliseconds;
  qa_app_sim_frame_plan plan = qa_app_sim_frame_plan_compute(plan_input);
  bool prepares = c_prepares_q1_clients(c);
  bool commands_pending = true;
  for (size_t i = 0; i < plan.boundary_count; i++) {
    /* TS: const run = boundary.map, commandTurn = commandsPending
     * && (run || !mapRun). Commands land on the first map boundary
     * (or the single idle boundary when the map does not run). */
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
      if (prepares) c_prepare_q1_clients(c, input);
      /* TS source beginFrame: QuakeCSource.StartFrame at entry. */
      if (c->qc) c_qc_begin_frame(c->qc, c->source_frame);
    }
    if (command_turn) c_run_commands(c, input);
    c_run_actor_turns(c, run, prepares, plan.elapsed_seconds);
    if (run && c->qc) c_qc_end_frame(c->qc);
    if (run) {
      if (plan.has_fixed) c->source_frame = qa_source_clock_enter(&c->clock, QA_FRAME_PHASE_FRAME_EXIT);
      else c->source_frame = qa_source_clock_advance(&c->clock,
          (qa_source_time){QA_SOURCE_TIME_SECONDS, plan.elapsed_seconds}, QA_FRAME_PHASE_FRAME_EXIT);
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
  c_qc_destroy(c->qc); c->qc = NULL;
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
  if (c->owned_q2_map) { qa_q2_decoded_map_destroy(c->owned_q2_map); free(c->owned_q2_map); }
  if (c->owned_q3_world) { qa_q3_decoded_world_destroy(c->owned_q3_world); free(c->owned_q3_world); }
  free(c->snap_actors); free(c->snap_bodies);
  free(c->snap_inventories); free(c->snap_configurations);
  c_free_checkpoint(c);
  free(c);
}
void qa_app_sim_concrete_simulation_bind(qa_app_sim_concrete_simulation *state,
                                         qa_app_sim_shared_simulation *simulation) {
  state->owner = simulation;
  if (state->owns_random && simulation->random && simulation->random != state->random) {
    /* One stream, TS this.random. Keep the concrete stream: a source game built
     * with the host has already bound it and may have drawn from it. */
    qa_app_sim_source_random_destroy(simulation->random);
    simulation->random = state->random; state->owns_random = false;
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
qa_frame_scheduler *qa_app_sim_concrete_simulation_scheduler(qa_app_sim_concrete_simulation *state) {
  return state->scheduler;
}
qa_actor_callback_table *qa_app_sim_concrete_simulation_callbacks(qa_app_sim_concrete_simulation *state) {
  return state->callbacks;
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
bool qa_app_sim_concrete_simulation_quakec_stats(
    const qa_app_sim_concrete_simulation *state, qa_app_sim_concrete_quakec_stats *out) {
  if (!state->qc) return false;
  memset(out, 0, sizeof *out);
  out->kind = state->qc->prepared->kind;
  out->spawn_count = state->qc->spawn_count;
  out->start_frame_count = state->qc->start_frame_count;
  out->think_count = state->qc->think_count;
  out->precache_count = state->qc->precached_count;
  out->entity_count = qa_compat_qc_entity_memory_get_count(&state->qc->entities);
  return true;
}
