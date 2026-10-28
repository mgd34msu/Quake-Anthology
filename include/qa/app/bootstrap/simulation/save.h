/* Port of src/app/bootstrap/simulation/save.ts. Decoded checkpoint
 * values are owned qa_save_value trees. The rerelease native save and
 * bots checkpoint decoders live in sibling slices; until they export
 * C decoders this module validates their checkpoint trees directly.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_SAVE_H
#define QA_APP_BOOTSTRAP_SIMULATION_SAVE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/session.h"
#include "qa/persistence/q2_classic_guest.h"
#include "qa/persistence/q2_foundation.h"
#include "qa/persistence/value.h"
qa_provider_checkpoint qa_app_sim_simulation_provider_checkpoint(qa_save_image image, const char *schema);
/* Owned tree or NULL (TS undefined). */
qa_save_value *qa_app_sim_saved_source_cvars(qa_save_image image);
typedef struct qa_app_sim_bot_connection { double client_slot; qa_saved_actor_id actor; } qa_app_sim_bot_connection;
typedef struct qa_app_sim_saved_bots { qa_save_value *value; qa_app_sim_bot_connection *connections; size_t connection_count; } qa_app_sim_saved_bots;
/* NULL when no world:bots provider. Caller frees value + connections. */
qa_app_sim_saved_bots *qa_app_sim_saved_bot_checkpoint(qa_save_image image);
const qa_guest_checkpoint *qa_app_sim_simulation_guest_checkpoint(qa_save_image image);
/* expected may be NULL. Returns false when TS returns null. */
bool qa_app_sim_native_q2_original_save(qa_save_image image, const qa_module_identity *expected, qa_q2_classic_original_save *out);
typedef struct qa_app_sim_rerelease_native_save_view { qa_save_value *value; qa_byte_view cvars; } qa_app_sim_rerelease_native_save_view;
bool qa_app_sim_native_q2_rerelease_save(qa_save_image image, const qa_module_identity *expected, qa_app_sim_rerelease_native_save_view *out);
typedef struct qa_app_sim_native_saved_client { double client_slot; const char *phase; const char *userinfo; } qa_app_sim_native_saved_client;
/* Malloc'd; strings borrow the decoded tree kept in *keep_alive. */
qa_app_sim_native_saved_client *qa_app_sim_native_q2_saved_clients(qa_save_image image, size_t *out_count, qa_save_value **keep_alive);
const qa_guest_checkpoint *qa_app_sim_simulation_quakec_checkpoint(qa_save_image image);
const qa_guest_checkpoint *qa_app_sim_simulation_qvm_checkpoint(qa_save_image image);
void qa_app_sim_validate_simulation_save(qa_save_image image);
typedef struct qa_app_sim_q3_runtime_arsenal { qa_saved_actor_id actor; qa_q3_arsenal_runtime_state state; bool has_requested_weapon; double requested_weapon; } qa_app_sim_q3_runtime_arsenal;
typedef struct qa_app_sim_q3_runtime_movement { qa_saved_actor_id actor; bool has_state; double trace_mask; bool has_fixed_msec; double fixed_msec; bool no_footsteps; bool gauntlet_hit; double debug_level; } qa_app_sim_q3_runtime_movement;
typedef struct qa_app_sim_q3_runtime_attack { qa_saved_actor_id actor; qa_q2_attack_checkpoint attack; } qa_app_sim_q3_runtime_attack;
typedef struct qa_app_sim_native_q3_runtime {
  qa_save_value *value; /* owned; frees borrowed strings */
  qa_app_sim_q3_runtime_arsenal *arsenals; size_t arsenal_count;
  qa_app_sim_q3_runtime_movement *movement; size_t movement_count;
  qa_app_sim_q3_runtime_attack *last_attacks; size_t last_attack_count;
} qa_app_sim_native_q3_runtime;
qa_app_sim_native_q3_runtime qa_app_sim_native_q3_runtime_reader(qa_save_image image);
void qa_app_sim_native_q3_runtime_free(qa_app_sim_native_q3_runtime *runtime);
/* Reader over a decoded world:simulation tree stored in *keep_alive. */
qa_save_reader qa_app_sim_simulation_save_reader(qa_save_image image, qa_save_value **keep_alive);
typedef struct qa_app_sim_saved_settings {
  double skill; const char *mode; double max_clients, seed;
  const char *initial_spawn_point, *start_items;
  double host_milliseconds;
  double *client_slots; size_t client_slot_count;
  qa_save_value *value; /* owned keep-alive */
} qa_app_sim_saved_settings;
qa_app_sim_saved_settings qa_app_sim_saved_simulation_settings(qa_save_image image);
#endif
