/* Port of src/app/bootstrap/simulation/q3/runtime.ts (Q3SourceRuntime).
 * The ~25 landed content/q3 subsystems are constructed by the wiring
 * layer and reached through the subsystem table below; level clock,
 * lifecycle, event publication, native checkpoint framing and the
 * load/frame/client flow live here. Entity views are host snapshots.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_RUNTIME_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_RUNTIME_H
#include "qa/app/bootstrap/simulation/q3/presentation.h"
#include "qa/app/bootstrap/simulation/q3/types.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/contracts/time.h"
#include "qa/persistence/value.h"

typedef struct qa_app_q3_spawn_outcome {
  const char *kind; /* "dispatched"|"filtered"|"unknown" */
  const char *route, *classname, *reason;
  double slot;
} qa_app_q3_spawn_outcome;
typedef struct qa_app_q3_world_variable { const char *key, *value; } qa_app_q3_world_variable;
typedef struct qa_app_q3_spawn_report {
  qa_app_q3_world_variable *world_variables; /* malloc'd strings owned */
  size_t world_variable_count;
  qa_app_q3_spawn_outcome *outcomes; /* malloc'd; strings borrowed from host storage */
  size_t outcome_count;
} qa_app_q3_spawn_report;
void qa_app_q3_spawn_report_destroy(qa_app_q3_spawn_report *report);

typedef struct qa_app_q3_runtime_entity_view {
  bool present, inuse, has_client, never_free, physics_object;
  double slot, e_type, event, event_time, flags, weapon;
  qa_q3_entity_state_record state;
  qa_owned_actor actor;
  qa_vec3 current_origin;
  const char *classname;
  double next_slot; /* teamchain; -1 none */
} qa_app_q3_runtime_entity_view;

typedef struct qa_app_q3_source_subsystems {
  void *ctx;
  /* settings */
  void (*settings_register)(void *ctx, const char *build_date);
  void (*settings_update)(void *ctx);
  double (*settings_integer)(void *ctx, const char *name);
  double (*settings_number)(void *ctx, const char *name);
  const char *(*settings_string)(void *ctx, const char *name);
  double (*settings_modification_count)(void *ctx, const char *name);
  qa_save_value *(*settings_capture)(void *ctx);
  void (*settings_restore)(void *ctx, const qa_save_value *value);
  /* random */
  double (*random_seed)(void *ctx);
  void (*random_reset)(void *ctx, double seed);
  /* records/pool */
  qa_app_q3_runtime_entity_view (*entity_by_actor)(void *ctx, qa_actor_id actor);
  qa_app_q3_runtime_entity_view (*entity_at)(void *ctx, double slot);
  double (*pool_max_clients)(void *ctx);
  double (*pool_num_entities)(void *ctx);
  void (*pool_initialize_clients)(void *ctx, double max_clients);
  void (*records_activate)(void *ctx, double slot);
  void (*records_close)(void *ctx);
  qa_app_q3_runtime_entity_view (*records_attach)(void *ctx, double slot, qa_owned_actor actor);
  const char *(*expire_events)(void *ctx, double slot);
  bool (*linked)(void *ctx, double slot);
  /* graph/level checkpoints keyed by name */
  qa_save_value *(*capture_part)(void *ctx, const char *name);
  void (*restore_part)(void *ctx, const char *name, const qa_save_value *value);
  bool has_personal_portal;
  /* load */
  void (*memory_initialize)(void *ctx);
  double (*sound_index)(void *ctx, const char *path);
  double (*model_index)(void *ctx, const char *path);
  void (*session_initialize_world)(void *ctx);
  void (*process_ip_bans)(void *ctx);
  void (*init_body_queue)(void *ctx);
  void (*registered_items_clear)(void *ctx, double game_type);
  void (*registered_items_save)(void *ctx);
  qa_app_q3_spawn_report (*spawn_entities)(void *ctx);
  void (*find_entity_teams)(void *ctx);
  void (*check_team_items)(void *ctx);
  void (*remap_teams)(void *ctx, double level_time);
  /* frame */
  bool (*missiles_run_owned)(void *ctx, qa_owned_actor actor);
  void (*missiles_run)(void *ctx, double slot);
  void (*missiles_close)(void *ctx);
  void (*run_item)(void *ctx, double slot);
  void (*run_mover)(void *ctx, double slot);
  void (*run_client)(void *ctx, double slot);
  void (*run_think)(void *ctx, double slot, double time);
  void (*client_end_frame)(void *ctx, double slot);
  void (*match_checks)(void *ctx);
  void (*team_check_status)(void *ctx);
  /* clients */
  const char *(*admission_connect)(void *ctx, double client, bool first, bool bot); /* NULL admitted */
  void (*admission_begin)(void *ctx, double client);
  void (*admission_disconnect)(void *ctx, double slot);
  void (*client_think)(void *ctx, double slot, qa_q3_base_user_command command);
  void (*command_dispatch)(void *ctx, double slot, const char *const *argv, size_t argc);
  void (*weapon_fire)(void *ctx, double slot);
  void (*set_entity_weapon)(void *ctx, double slot, double weapon);
  double (*client_weapon)(void *ctx, double slot);
  void (*session_write_world)(void *ctx);
  void (*session_write_client)(void *ctx, double slot);
  bool (*client_connected)(void *ctx, double slot);
  /* combat/misc */
  void (*bridge_before_reaction)(void *ctx, qa_damage_decision decision);
  void (*player_die)(void *ctx, double slot, double damage, double means_of_death);
  double (*observe_supply)(void *ctx, qa_actor_id pickup, qa_actor_id recipient);
  bool (*step_arsenal)(void *ctx, qa_actor_id actor, bool pressed); /* stepHoldable fallback when no arsenal */
  /* presentation pool view */
  qa_q3_entity_pool *(*pool)(void *ctx);
  const char *content;
} qa_app_q3_source_subsystems;

typedef struct qa_app_q3_source_runtime_mode {
  bool restore;
  const qa_save_value *state;
} qa_app_q3_source_runtime_mode;

typedef struct qa_app_q3_source_runtime qa_app_q3_source_runtime;
/* Validation errors abort; NULL only on OOM. */
qa_app_q3_source_runtime *qa_app_q3_source_runtime_create(qa_app_q3_source_options options,
    qa_app_q3_source_host host, qa_app_q3_source_subsystems subsystems, qa_app_q3_source_runtime_mode mode);
void qa_app_q3_source_runtime_destroy(qa_app_q3_source_runtime *runtime);
const qa_app_q3_source_options *qa_app_q3_source_runtime_options(const qa_app_q3_source_runtime *runtime);
qa_app_q3_source_host *qa_app_q3_source_runtime_host(qa_app_q3_source_runtime *runtime);
double qa_app_q3_source_runtime_level_time(const qa_app_q3_source_runtime *runtime);
qa_save_value *qa_app_q3_source_runtime_capture_native_state(qa_app_q3_source_runtime *runtime);
#include <stdint.h>
uint8_t *qa_app_q3_source_runtime_capture_native_bytes(qa_app_q3_source_runtime *runtime, size_t *out_length);
void qa_app_q3_source_runtime_finish_native_restore(qa_app_q3_source_runtime *runtime);
double qa_app_q3_source_runtime_game_type(qa_app_q3_source_runtime *runtime);
qa_app_q3_source_presentation_state qa_app_q3_source_runtime_source_state(qa_app_q3_source_runtime *runtime,
    qa_app_q3_source_presentation_state out);
size_t qa_app_q3_source_runtime_presentations(qa_app_q3_source_runtime *runtime, qa_app_simulation_presentation *out, size_t capacity);
const qa_app_q3_spawn_report *qa_app_q3_source_runtime_spawn_report(qa_app_q3_source_runtime *runtime);
double qa_app_q3_source_runtime_observe_supply(qa_app_q3_source_runtime *runtime, qa_actor_id pickup, qa_actor_id recipient);
double qa_app_q3_source_runtime_quad_damage_factor(qa_app_q3_source_runtime *runtime);
bool qa_app_q3_source_runtime_step_holdable(qa_app_q3_source_runtime *runtime, qa_actor_id actor, bool pressed);
const char *qa_app_q3_source_runtime_restart_compatibility(qa_app_q3_source_runtime *runtime);
void qa_app_q3_source_runtime_close(qa_app_q3_source_runtime *runtime);
const qa_app_q3_spawn_report *qa_app_q3_source_runtime_load(qa_app_q3_source_runtime *runtime);
typedef struct qa_app_q3_foreign_spawn { double ordinal; const char *classname; } qa_app_q3_foreign_spawn;
size_t qa_app_q3_source_runtime_foreign_spawns(qa_app_q3_source_runtime *runtime, qa_app_q3_foreign_spawn *out, size_t capacity);
void qa_app_q3_source_runtime_begin_frame(qa_app_q3_source_runtime *runtime, qa_frame_context frame);
void qa_app_q3_source_runtime_run_actor(qa_app_q3_source_runtime *runtime, qa_owned_actor actor);
void qa_app_q3_source_runtime_end_frame(qa_app_q3_source_runtime *runtime);
qa_app_q3_runtime_entity_view qa_app_q3_source_runtime_prepare_client(qa_app_q3_source_runtime *runtime,
    qa_owned_actor actor, double client);
/* Denial aborts with the TS Q3ClientAdmissionDenied message. */
qa_app_q3_runtime_entity_view qa_app_q3_source_runtime_admit_player(qa_app_q3_source_runtime *runtime,
    qa_owned_actor actor, double client);
typedef struct qa_app_q3_captured_session {
  qa_app_q3_source_session_carry carry;
  qa_app_q3_source_session_carry_client *clients; /* owned strings */
} qa_app_q3_captured_session;
qa_app_q3_captured_session qa_app_q3_source_runtime_capture_session(qa_app_q3_source_runtime *runtime);
void qa_app_q3_captured_session_destroy(qa_app_q3_captured_session *session);
void qa_app_q3_source_runtime_disconnect_player(qa_app_q3_source_runtime *runtime, qa_actor_id actor);
void qa_app_q3_source_runtime_player_think(qa_app_q3_source_runtime *runtime, qa_actor_command input);
void qa_app_q3_source_runtime_player_command(qa_app_q3_source_runtime *runtime, qa_actor_id actor,
    const char *name, const char *const *args, size_t argc);
void qa_app_q3_source_runtime_player_weapon(qa_app_q3_source_runtime *runtime, qa_actor_id actor);
void qa_app_q3_source_runtime_before_reaction(qa_app_q3_source_runtime *runtime, qa_owned_actor actor,
    qa_damage_decision decision);
void qa_app_q3_source_runtime_adjust_area_portal_state(qa_app_q3_source_runtime *runtime, double slot, bool open);
#endif
