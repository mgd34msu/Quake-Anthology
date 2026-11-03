/* Port of src/app/bootstrap/simulation/runtime.ts: SharedSimulation, the one
 * unified engine. One SharedSimulation composes content by recipe (map
 * entities, movement, weapons and characters of different games in one
 * world); createSource is one dispatch over the six source kinds (native Q2
 * classic/rerelease guest, Q3 QVM guest, QuakeC, TS Q3, TS Q1, TS Q2) and
 * step is one frame pipeline.
 *
 * This is the real class. The movement-only "concrete host" in runtime.h
 * (qa_app_sim_shared_simulation) is the temporary path main.c uses until the
 * connection step; it is a different type.
 *
 * The method layout (TS method, C function, C file, status) is
 * docs/runtime_layout.md. TS async methods are synchronous here, with an
 * explicit next_frame pump (PORTING.md). TS throws reach the caller through
 * qa_raise. Returned arrays are owned by the simulation and valid until the
 * next call of the same method. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_SHARED_SIMULATION_H
#define QA_APP_BOOTSTRAP_SIMULATION_SHARED_SIMULATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/players.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/session.h"
#include "qa/core/cvars/index.h"
#include "qa/world/actors/body.h"

typedef struct qa_app_shared_simulation qa_app_shared_simulation;
/* TS SimulationProgress callback of stepAsync (awaited). */
typedef void (*qa_app_shared_sim_progress_fn)(void *ctx, qa_simulation_progress frame);
/* TS nextFrame: () => Promise<void>. */
typedef void (*qa_app_shared_sim_next_frame_fn)(void *ctx);

/* TS createSimulation(options) / new SharedSimulation(options). The options
 * value is copied; what it points to is borrowed for the simulation's life.
 * options.world is a qa_app_application_world (content.h). */
qa_app_shared_simulation *qa_app_shared_simulation_create(qa_app_simulation_options options);
/* TS loadSimulation(options, nextFrame) / SharedSimulation.load. */
qa_app_shared_simulation *qa_app_shared_simulation_load(qa_app_simulation_options options,
    qa_app_shared_sim_next_frame_fn next_frame, void *frame_ctx);
/* Frees a closed (or never opened) simulation. */
void qa_app_shared_simulation_destroy(qa_app_shared_simulation *sim);

/* Public TS members. */
qa_session_id qa_app_shared_sim_session(const qa_app_shared_simulation *sim);
const qa_executable_recipe *qa_app_shared_sim_recipe(const qa_app_shared_simulation *sim);
qa_session_actor_registry *qa_app_shared_sim_actors(qa_app_shared_simulation *sim);
qa_shared_body_table *qa_app_shared_sim_bodies(qa_app_shared_simulation *sim);
double qa_app_shared_sim_time_seconds(const qa_app_shared_simulation *sim);
/* TS takeAdmissionSettleMilliseconds(): the Q3 first-admission settle frames' source time, then zero. */
double qa_app_shared_sim_take_admission_settle_milliseconds(qa_app_shared_simulation *sim);
/* TS source.kind: "loading"|"quakec"|"q1"|"q3"|"q3-qvm"|"q2-native"|"q2". */
const char *qa_app_shared_sim_source_kind(const qa_app_shared_simulation *sim);

qa_simulation_output qa_app_shared_sim_step(qa_app_shared_simulation *sim, qa_input_batch input);
/* progress NULL is the TS undefined. */
qa_simulation_output qa_app_shared_sim_step_async(qa_app_shared_simulation *sim, qa_input_batch input,
    qa_app_shared_sim_progress_fn progress, void *progress_ctx);
qa_simulation_output qa_app_shared_sim_current_output(qa_app_shared_simulation *sim);
void qa_app_shared_sim_close(qa_app_shared_simulation *sim);

/* Public TS methods implemented in the phase B files (docs/runtime_layout.md). */
const qa_actor_id *qa_app_shared_sim_players(qa_app_shared_simulation *sim, size_t *out_count);
const qa_client_id *qa_app_shared_sim_client_identities(qa_app_shared_simulation *sim, size_t *out_count);
qa_app_sim_movement_player *qa_app_shared_sim_movement_player(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_sim_player_admission qa_app_shared_sim_admit_player(qa_app_shared_simulation *sim, qa_client_id client);
void qa_app_shared_sim_disconnect_player(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_notify_client_event(qa_app_shared_simulation *sim, const char *kind, qa_actor_id actor);
void qa_app_shared_sim_observe_client_command(qa_app_shared_simulation *sim, const qa_actor_command *input);
void qa_app_shared_sim_player_command(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name,
    const char *const *args, size_t arg_count);
qa_app_sim_player_view qa_app_shared_sim_player_view(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_sim_player_ui qa_app_shared_sim_player_ui(qa_app_shared_simulation *sim, qa_actor_id actor);
const qa_app_simulation_presentation *qa_app_shared_sim_presentations(qa_app_shared_simulation *sim, size_t *out_count);
qa_cvar_registry *qa_app_shared_sim_q2_server_cvars(qa_app_shared_simulation *sim);
const char *qa_app_shared_sim_source_entity_text(qa_app_shared_simulation *sim);
bool qa_app_shared_sim_q1_paused(qa_app_shared_simulation *sim);
#endif
