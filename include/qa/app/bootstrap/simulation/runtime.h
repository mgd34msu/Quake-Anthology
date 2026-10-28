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
#endif
