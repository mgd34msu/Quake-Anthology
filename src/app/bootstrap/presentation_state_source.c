/* Port of src/app/bootstrap/presentation-state.ts, the constructor call
 * SimulationEvents' super makes in the runtime (simulation/runtime.ts:
 * new SimulationEvents(..., { content, acceptedContents, entities, alive }))
 * and the runtime's lightStyles pass-through. The runtime declares these in
 * shared_simulation_internal.h with its own fog-option type; this file
 * converts it to SimulationQ1FogOptions (qa_app_sim_q1_fog_options).
 * GPL-2.0-or-later. */
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/presentation_state.h"
#include "qa/foundation/support.h"
#include "simulation/shared_simulation_internal.h"

/* The runtime's alive takes an OwnedActor; TS alive takes an ActorId. The
 * adapter is owned by the state (qa_app_presentation_state_adopt). */
typedef struct source_fog { void *ctx; bool (*alive)(void *ctx, qa_owned_actor actor); } source_fog;
static bool fog_alive(void *ctx, qa_actor_id actor) {
  source_fog *fog = ctx;
  qa_owned_actor owned; memset(&owned, 0, sizeof owned);
  owned.id = actor;
  return fog->alive(fog->ctx, owned);
}
qa_app_presentation_state *qa_app_presentation_state_create_source(qa_source_time (*now)(void *ctx),
    bool (*source_slot)(void *ctx, qa_actor_id actor, double *out), void *ctx, const qa_app_presentation_fog_options *fog) {
  if (!fog) return qa_app_presentation_state_create(now, source_slot, ctx, NULL);
  source_fog *adapter = malloc(sizeof *adapter);
  if (!adapter) return NULL;
  adapter->ctx = fog->ctx; adapter->alive = fog->alive;
  qa_app_sim_q1_fog_options options; memset(&options, 0, sizeof options);
  options.content = fog->content;
  options.accepted_contents = fog->accepted_contents; options.accepted_content_count = fog->accepted_content_count;
  options.has_accepted = true;
  options.entities = fog->entities ? fog->entities : "";
  options.alive_ctx = adapter; options.alive = fog_alive;
  qa_app_presentation_state *state = qa_app_presentation_state_create(now, source_slot, ctx, &options);
  if (!state) { free(adapter); return NULL; }
  qa_app_presentation_state_adopt(state, adapter, free);
  return state;
}
const qa_scene_light_style *qa_app_shared_sim_light_styles(qa_app_shared_simulation *sim, double seconds, size_t *out_count) {
  return qa_app_presentation_state_light_styles(sim->presentation, seconds, out_count);
}
