/*
 * Port of src/world/session/mod-match.ts: borrowed source match and
 * objective bindings over the session actor registry. Implements
 * the contracts qa_source_match_services through the adapter. TS
 * plain Error throws abort identically. Returned listings are
 * scratch owned by the state, invalidated by the next call.
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SESSION_MOD_MATCH_H
#define QA_WORLD_SESSION_MOD_MATCH_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/source_match.h"
#include "qa/world/actors/registry.h"

typedef qa_source_match_player_result (*qa_mod_match_resolve_fn)(
    void *ctx, qa_actor_id actor);

typedef struct qa_mod_match_state qa_mod_match_state;

/* The registry and primary resolver are borrowed. NULL only on OOM. */
qa_mod_match_state *qa_mod_match_state_create(
    qa_session_actor_registry *actors, qa_mod_match_resolve_fn primary,
    void *primary_ctx);
void qa_mod_match_state_destroy(qa_mod_match_state *state);
qa_source_match_player_result qa_mod_match_state_player(
    qa_mod_match_state *state, qa_actor_id actor);
qa_source_match_unbind qa_mod_match_state_bind_source(
    qa_mod_match_state *state, qa_provider_id owner,
    qa_mod_match_resolve_fn resolve, void *resolve_ctx);
qa_source_match_unbind qa_mod_match_state_bind_objective(
    qa_mod_match_state *state, qa_source_objective_binding binding);
qa_source_objective_state_result qa_mod_match_state_objective(
    qa_mod_match_state *state, qa_objective_id id);
qa_source_objective_state_result qa_mod_match_state_change_objective(
    qa_mod_match_state *state, qa_objective_id id,
    qa_source_objective_change request);
const qa_source_objective_listing *qa_mod_match_state_objectives(
    qa_mod_match_state *state, size_t *out_count);
const qa_mission_gate *qa_mod_match_state_gates(qa_mod_match_state *state,
                                                size_t *out_count);
void qa_mod_match_state_close(qa_mod_match_state *state);
/* Contracts adapter: the returned table borrows the state. */
qa_source_match_services qa_mod_match_state_as_services(
    qa_mod_match_state *state);

#endif
