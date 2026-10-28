/* Port of src/app/bootstrap/simulation/q3/guest-records.ts over landed
 * qa_qvm_game_data/registry/body/scene owners. s.* access goes through
 * a qa_qvmx_view over the shared entity's writable view bytes.
 * Capture returns an owned qa_save_value array. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_RECORDS_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_RECORDS_H
#include "qa/app/bootstrap/simulation/q3/types.h"
#include <stdint.h>
#include "qa/compat/qvm/game_data.h"
/* Visibility link mirrors network/q3 visibility.h qa_q3_visibility_link;
 * that header's product enum collides with game/state.h in one TU. */
typedef struct qa_app_q3_visibility_link {
  int64_t areanum, areanum2; const int64_t *clusters; size_t cluster_count; int64_t last_cluster;
} qa_app_q3_visibility_link;
#include "qa/persistence/value.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/registry.h"
#include "qa/world/collision/index.h"

typedef struct qa_app_q3_guest_record_host {
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_shared_scene_queries *scene;
  qa_provider_id provider;
  void *ctx;
  void (*collision)(void *ctx, qa_owned_actor actor, qa_actor_collision collision);
  void (*admit)(void *ctx, qa_owned_actor actor); /* NULL absent */
} qa_app_q3_guest_record_host;

typedef struct qa_app_q3_guest_records qa_app_q3_guest_records;
qa_app_q3_guest_records *qa_app_q3_guest_records_create(qa_qvm_game_data *data, qa_app_q3_guest_record_host host);
void qa_app_q3_guest_records_destroy(qa_app_q3_guest_records *records);
qa_qvm_game_data *qa_app_q3_guest_records_data(qa_app_q3_guest_records *records);
const qa_app_q3_guest_record_host *qa_app_q3_guest_records_host_of(const qa_app_q3_guest_records *records);
void qa_app_q3_guest_records_set_player_velocity_writer(qa_app_q3_guest_records *records,
    void (*write)(void *ctx, qa_owned_actor actor, qa_vec3 velocity), void *ctx);
qa_qvm_shared_entity *qa_app_q3_guest_records_entity(qa_app_q3_guest_records *records, double slot);
void qa_app_q3_guest_records_player(qa_app_q3_guest_records *records, double slot, qa_q3_player_state *out,
    double stats[16], double persistent[16], double powerups[16], double ammo[16]);
void qa_app_q3_guest_records_set_player_view_roll(qa_app_q3_guest_records *records, qa_actor_id actor, double roll);
qa_owned_actor qa_app_q3_guest_records_actor(qa_app_q3_guest_records *records, double slot);
void qa_app_q3_guest_records_register_foreign(qa_app_q3_guest_records *records, double slot, qa_owned_actor actor);
bool qa_app_q3_guest_records_slot(qa_app_q3_guest_records *records, qa_actor_id actor, double *out_slot);
double qa_app_q3_guest_records_require_slot(qa_app_q3_guest_records *records, qa_actor_id actor);
bool qa_app_q3_guest_records_reference(qa_app_q3_guest_records *records, double slot, qa_actor_id *out);
qa_body_state qa_app_q3_guest_records_body(qa_app_q3_guest_records *records, double slot, qa_actor_id *ground_storage);
qa_actor_collision qa_app_q3_guest_records_collision(qa_app_q3_guest_records *records, double slot, qa_actor_id *owner_storage);
void qa_app_q3_guest_records_link(qa_app_q3_guest_records *records, double slot);
void qa_app_q3_guest_records_unlink(qa_app_q3_guest_records *records, double slot);
bool qa_app_q3_guest_records_visibility(qa_app_q3_guest_records *records, double slot, qa_app_q3_visibility_link *out);
qa_save_value *qa_app_q3_guest_records_capture_checkpoint(qa_app_q3_guest_records *records);
void qa_app_q3_guest_records_restore_checkpoint(qa_app_q3_guest_records *records, const qa_save_value *value);
void qa_app_q3_guest_records_release_client(qa_app_q3_guest_records *records, double slot);
void qa_app_q3_guest_records_retire_input_client(qa_app_q3_guest_records *records, double slot);
void qa_app_q3_guest_records_finish_input_retirement(qa_app_q3_guest_records *records, double slot);
bool qa_app_q3_guest_records_is_input_retired(qa_app_q3_guest_records *records, double slot);
/* TS withInputMotion: run executes synchronously (async results are pumped by callers). */
void qa_app_q3_guest_records_with_input_motion(qa_app_q3_guest_records *records, double slot,
    void (*run)(void *ctx), void *run_ctx);
void qa_app_q3_guest_records_close(qa_app_q3_guest_records *records);
#endif
