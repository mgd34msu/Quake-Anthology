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
