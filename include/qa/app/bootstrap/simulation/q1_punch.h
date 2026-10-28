/* Port of src/app/bootstrap/simulation/q1-punch.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q1_PUNCH_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q1_PUNCH_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"
typedef struct qa_app_sim_q1_punch_owner {
  void *ctx;
  qa_vec3 (*read)(void *ctx);
  void (*write)(void *ctx, qa_vec3 angles);
} qa_app_sim_q1_punch_owner;
/* TS SessionActorRegistry view used here. resolve_owned returns false
 * for null; resolve_saved likewise. on_release is driven by the
 * caller invoking qa_app_sim_q1_player_punch_release. */
typedef struct qa_app_sim_q1_punch_actors {
  void *ctx;
  bool (*resolve_owned)(void *ctx, qa_actor_id actor, qa_owned_actor *out);
  bool (*resolve_saved)(void *ctx, qa_saved_actor_id saved, qa_owned_actor *out);
} qa_app_sim_q1_punch_actors;
typedef struct qa_app_sim_q1_player_punch qa_app_sim_q1_player_punch;
qa_app_sim_q1_player_punch *qa_app_sim_q1_player_punch_create(qa_app_sim_q1_punch_actors actors,
    bool (*source)(void *ctx, qa_actor_id actor, qa_app_sim_q1_punch_owner *out), void *source_ctx);
void qa_app_sim_q1_player_punch_destroy(qa_app_sim_q1_player_punch *punch);
void qa_app_sim_q1_player_punch_release(qa_app_sim_q1_player_punch *punch, qa_owned_actor actor);
qa_vec3 qa_app_sim_q1_player_punch_read(qa_app_sim_q1_player_punch *punch, qa_actor_id actor);
void qa_app_sim_q1_player_punch_write(qa_app_sim_q1_player_punch *punch, qa_actor_id actor, qa_vec3 value);
qa_vec3 qa_app_sim_q1_player_punch_advance(qa_app_sim_q1_player_punch *punch, qa_actor_id actor, double elapsed, qa_numeric_operations numeric);
typedef struct qa_app_sim_q1_punch_capture_entry { qa_saved_actor_id actor; qa_vec3 angles; } qa_app_sim_q1_punch_capture_entry;
/* Malloc'd; caller frees. */
qa_app_sim_q1_punch_capture_entry *qa_app_sim_q1_player_punch_capture(qa_app_sim_q1_player_punch *punch, size_t *out_count);
void qa_app_sim_q1_player_punch_restore(qa_app_sim_q1_player_punch *punch, const qa_save_reader *reader);
#endif
