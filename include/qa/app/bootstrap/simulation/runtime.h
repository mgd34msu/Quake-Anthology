/* Port of src/app/bootstrap/simulation/runtime.ts. SharedSimulation
 * is the composition root: this header owns its public surface,
 * ordered actor turns, and the create/load entry points. The frame
 * pipeline stages run through the host bundle (source games, guests
 * and mods are constructed by the sibling slices); deep per-stage
 * wiring is tracked as unresolved for parent reconciliation.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_RUNTIME_H
#define QA_APP_BOOTSTRAP_SIMULATION_RUNTIME_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/players.h"
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/app/bootstrap/simulation/save.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/session.h"
#include "qa/world/actors/registry.h"
typedef struct qa_app_sim_shared_simulation_host {
  void *ctx;
  qa_simulation_output (*step)(void *ctx, qa_input_batch input);
  qa_simulation_output (*step_async)(void *ctx, qa_input_batch input,
      void (*progress)(void *progress_ctx, qa_simulation_progress frame), void *progress_ctx);
  qa_save_image (*checkpoint)(void *ctx);
  void (*close)(void *ctx);
} qa_app_sim_shared_simulation_host;
struct qa_app_sim_shared_simulation {
  qa_session_id session;
  qa_executable_recipe recipe;
  qa_app_simulation_options options;
  qa_session_actor_registry *actors;
  qa_app_sim_source_random *random;
  qa_app_sim_shared_simulation_host host;
  bool closed;
  bool stepping;
  bool checkpoint_in_progress;
};
/* TS SharedSimulation statics + loadSimulation: async pumped through
 * next_frame. The host bundle supplies the constructed pipeline. */
qa_app_sim_shared_simulation *qa_app_sim_shared_simulation_create(qa_app_simulation_options options,
    qa_app_sim_shared_simulation_host host, qa_session_actor_registry *actors, qa_session_id session);
qa_app_sim_shared_simulation *qa_app_sim_load_simulation(qa_app_simulation_options options,
    qa_app_sim_shared_simulation_host host, qa_session_actor_registry *actors, qa_session_id session,
    void (*next_frame)(void *frame_ctx), void *frame_ctx);
void qa_app_sim_shared_simulation_destroy(qa_app_sim_shared_simulation *simulation);
qa_simulation qa_app_sim_shared_simulation_as_simulation(qa_app_sim_shared_simulation *simulation);
qa_simulation_output qa_app_sim_shared_simulation_step(qa_app_sim_shared_simulation *simulation, qa_input_batch input);
qa_save_image qa_app_sim_shared_simulation_checkpoint(qa_app_sim_shared_simulation *simulation);
void qa_app_sim_shared_simulation_close(qa_app_sim_shared_simulation *simulation);
/* TS SimulationPresentationAccess surface (types.h) backed by the host. */
typedef struct qa_app_sim_ordered_actor_turns qa_app_sim_ordered_actor_turns;
qa_app_sim_ordered_actor_turns *qa_app_sim_ordered_actor_turns_create(qa_session_actor_registry *actors,
    void (*position_of)(void *ctx, qa_owned_actor actor, double out[2]), void *position_ctx);
void qa_app_sim_ordered_actor_turns_destroy(qa_app_sim_ordered_actor_turns *turns);
/* Caller-owned visited set (actor ids compared with qa_same_actor on
 * owned identity: id + owner). */
typedef struct qa_app_sim_actor_visited { qa_owned_actor *actors; size_t count, capacity; } qa_app_sim_actor_visited;
bool qa_app_sim_ordered_actor_turns_next(qa_app_sim_ordered_actor_turns *turns, qa_app_sim_actor_visited *visited, qa_owned_actor *out);
/* TS SharedSimulation.step() deadline/boundary planning (runtime.ts
 * step: fixed interval, mapRun, elapsed clamp, map/Q2-weapon/Q3
 * deadline sets merged and sorted). Pure stage body: the host bundle
 * accumulates scheduling/selected milliseconds exactly as TS does
 * before the plan, then executes one boundary turn per entry.
 * extra_clocks are the monster/foreign Q2 clocks: first due is
 * round(seconds(clock.time)*1000)+interval in TS. A non-positive
 * interval contributes its first due once (TS profiles are > 0). */
typedef struct qa_app_sim_frame_clock { double first_due, interval; } qa_app_sim_frame_clock;
typedef struct qa_app_sim_frame_boundary { double milliseconds; bool map, q2, q3; } qa_app_sim_frame_boundary;
typedef struct qa_app_sim_frame_plan_input {
  qa_clock_profile profile;
  bool settlement_active, paused;
  double elapsed_milliseconds, time_seconds, source_scheduling_milliseconds;
  bool has_selected_q2;
  double selected_q2_map_milliseconds, selected_q2_next_milliseconds, selected_q2_interval_milliseconds;
  bool has_selected_q3;
  double selected_q3_next, selected_milliseconds, selected_q3_server_frame_milliseconds;
  const qa_app_sim_frame_clock *extra_clocks; size_t extra_clock_count;
} qa_app_sim_frame_plan_input;
typedef struct qa_app_sim_frame_plan {
  bool has_fixed; double fixed_milliseconds;
  bool map_run; double elapsed_seconds;
  double map_start_milliseconds, map_end_milliseconds;
  qa_app_sim_frame_boundary *boundaries; size_t boundary_count;
} qa_app_sim_frame_plan;
qa_app_sim_frame_plan qa_app_sim_frame_plan_compute(qa_app_sim_frame_plan_input input);
void qa_app_sim_frame_plan_free(qa_app_sim_frame_plan *plan);
/* --- Concrete SharedSimulation assembly (TS loadSimulation over the
 * landed sibling modules: scene queries, shared physics, simulation
 * events, gameplay authority, inventory, frame scheduler, movement
 * players). options.world, when set, is a borrowed
 * qa_app_application_world (content.h); when NULL (or a {NULL, NULL}
 * world view) the map geometry is read from options.mounts (a
 * qa_content_mounted) through recipe.map.geometry and decoded here
 * with the content.ts classifyBsp dispatch (Q1/Q2/Q3 BSP). The
 * returned host's close destroys the
 * concrete state; qa_app_sim_shared_simulation_destroy after close is
 * safe. Source games: a QuakeC execution module with
 * options.prepared_quakec (a qa_app_sim_prepared_quakec_source)
 * constructs the TS QuakeCSource game (compat/qc machine + world/
 * presentation/movement/client hosts): spawnMap runs the authored
 * spawn functions, StartFrame runs per map frame and entity thinks
 * run through the QC nextthink fields. QVM/native guest source
 * games and the QC damage/pickup/weapon-stage authority boundaries
 * are not constructed and abort at build. --- */
#include "qa/app/bootstrap/simulation/events.h"
#include "qa/app/bootstrap/simulation/physics.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/collision/index.h"
#include "qa/world/scheduler.h"
typedef struct qa_app_sim_concrete_simulation qa_app_sim_concrete_simulation;
qa_app_sim_shared_simulation_host qa_app_sim_shared_simulation_concrete_host(
    qa_app_simulation_options options, qa_session_actor_registry *actors, qa_session_id session,
    qa_app_sim_concrete_simulation **out_state);
/* Bind the owning simulation (its SourceRandom becomes the concrete
 * random stream). Called by the create/load wrappers below. */
void qa_app_sim_concrete_simulation_bind(qa_app_sim_concrete_simulation *state,
                                         qa_app_sim_shared_simulation *simulation);
qa_app_sim_shared_simulation *qa_app_sim_shared_simulation_create_concrete(
    qa_app_simulation_options options, qa_session_actor_registry *actors, qa_session_id session);
qa_app_sim_shared_simulation *qa_app_sim_load_simulation_concrete(
    qa_app_simulation_options options, qa_session_actor_registry *actors, qa_session_id session,
    void (*next_frame)(void *frame_ctx), void *frame_ctx);
/* NULL when the simulation was not built by the concrete builder. */
qa_app_sim_concrete_simulation *qa_app_sim_shared_simulation_concrete(
    qa_app_sim_shared_simulation *simulation);
qa_app_sim_shared_physics *qa_app_sim_concrete_simulation_physics(qa_app_sim_concrete_simulation *state);
qa_app_sim_simulation_events *qa_app_sim_concrete_simulation_events(qa_app_sim_concrete_simulation *state);
/* The concrete frame scheduler + callback table (TS this.scheduler /
 * this.callbacks): per-boundary turns run scheduled "world:think"
 * callbacks through the table. Borrowed; owned by the state. */
qa_frame_scheduler *qa_app_sim_concrete_simulation_scheduler(qa_app_sim_concrete_simulation *state);
qa_actor_callback_table *qa_app_sim_concrete_simulation_callbacks(qa_app_sim_concrete_simulation *state);
qa_shared_scene_queries *qa_app_sim_concrete_simulation_scene(qa_app_sim_concrete_simulation *state);
qa_frame_context qa_app_sim_concrete_simulation_frame(const qa_app_sim_concrete_simulation *state);
bool qa_app_sim_concrete_simulation_world_actor(const qa_app_sim_concrete_simulation *state, qa_actor_id *out);
/* TS admitPlayer for the shared (non-guest) source: allocates the
 * client actor at the map's info_player_start (decoded Q1 entity
 * list, or the retained Q2/Q3 entity lump; origin zero otherwise),
 * creates its body,
 * combat (health 100) and inventory, and binds a MovementPlayer. */
qa_app_sim_player_admission qa_app_sim_concrete_simulation_admit_player(
    qa_app_sim_concrete_simulation *state, qa_client_id client);
/* QuakeC source-game activity (TS QuakeCSource over progs.dat):
 * authored spawn functions invoked by spawnMap, StartFrame calls,
 * entity think invocations and distinct precache registrations.
 * Returns false when no QuakeC source game was constructed. */
typedef struct qa_app_sim_concrete_quakec_stats {
  const char *kind; /* "netquake"|"quakeworld" */
  size_t spawn_count, start_frame_count, think_count, precache_count;
  int64_t entity_count;
} qa_app_sim_concrete_quakec_stats;
bool qa_app_sim_concrete_simulation_quakec_stats(
    const qa_app_sim_concrete_simulation *state, qa_app_sim_concrete_quakec_stats *out);
#endif
