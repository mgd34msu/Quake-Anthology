/* Port of src/app/bootstrap/simulation/arsenal/q3-source.ts.
 * The TS class owns Q3EntityRecords/EntityPool/world/combat/missile/
 * weapon runtimes; in C those landed content/q3/base/game runtimes are
 * reached through the host entity-view + operation table below (the
 * wiring layer adapts them). All selection/publication/projection
 * policy in the TS class body is implemented here over the views.
 * Capture returns an owned qa_save_value tree. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q3_SOURCE_H
#define QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q3_SOURCE_H
#include "qa/app/bootstrap/simulation/arsenal/q3.h"
#include "qa/content/q3/base/shared/entity_state.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/world.h"
#include "qa/movement/q3/types.h"

typedef struct qa_app_q3_selected_client_pose {
  qa_vec3 angles;
  double view_height, max_health, team, quad_until, haste_until;
} qa_app_q3_selected_client_pose;
typedef struct qa_app_q3_selected_client_effects {
  double teleport_bit;
  qa_vec3 view_angles, delta_angles;
  double pm_flags, pm_time, invulnerability_time, max_health;
} qa_app_q3_selected_client_effects;
typedef qa_app_q3_equipment_state qa_app_q3_selected_equipment_state;

/* GameEntity/GameClient fields this module reads or writes. */
typedef struct qa_app_q3_source_entity_view {
  bool present, inuse, has_client;
  double slot;
  qa_owned_actor actor;
  double clipmask, contents, owner_num, health;
  bool model_is_inline;
  double model_index;
  /* client (has_client) */
  double e_flags, pm_flags, pm_time, invulnerability_time, event_sequence;
  qa_vec3 view_angles, delta_angles, grapple_point;
  double max_health_stat, holdable_item_stat, persistent_powerup_stat, ping, weapon;
  bool fire_held, has_hook;
  double haste_powerup;
  bool has_persistant_powerup;
  double persistant_powerup_tag;
} qa_app_q3_source_entity_view;

typedef struct qa_app_q3_selected_source_host {
  void *ctx;
  qa_provider_id provider;
  const char *product; /* "baseq3"|"missionpack" */
  const char *equipment_kind; /* "source"|"primary" */
  const char *content; /* ProviderReference content id */
  double max_clients, seed;
  /* actors/bodies/configstrings */
  void (*assert_owned)(void *ctx, qa_owned_actor actor);
  bool (*is_live)(void *ctx, qa_actor_id actor);
  bool (*resolve_owned)(void *ctx, qa_actor_id actor, qa_owned_actor *out);
  bool (*body_read)(void *ctx, qa_actor_id actor, qa_body_state *out); /* false = TS null */
  void (*configstring_set)(void *ctx, double index, const char *value);
  const char *(*userinfo)(void *ctx, qa_actor_id actor);
  double (*now)(void *ctx);
  qa_owned_actor (*world_actor)(void *ctx);
  bool (*player_pose)(void *ctx, qa_actor_id actor, qa_app_q3_selected_client_pose *out); /* false = TS null */
  double (*quad_factor)(void *ctx, const qa_actor_id *actor);
  double (*game_type)(void *ctx);
  void (*execute)(void *ctx, qa_owned_actor actor, void (*step)(void *step_ctx, double previous, double time), void *step_ctx);
  void (*event)(void *ctx, qa_owned_actor actor, qa_q3_base_entity_state state, double time);
  void (*client_changed)(void *ctx, qa_owned_actor actor, qa_app_q3_selected_client_effects before, qa_app_q3_selected_client_effects after);
  void (*drop_objectives)(void *ctx, qa_owned_actor actor);
  void (*return_pickup)(void *ctx, qa_actor_id actor);
  void (*spawn_point)(void *ctx, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles);
  bool has_weapon_effects;
  double (*weapon_damage_factor)(void *ctx, qa_actor_id actor);
  double (*weapon_firing_delay)(void *ctx, qa_actor_id actor, double milliseconds, double persistent_powerup);
  /* records/pool runtime operations */
  qa_app_q3_source_entity_view (*entity_at)(void *ctx, double slot);
  qa_app_q3_source_entity_view (*entity_by_actor)(void *ctx, qa_actor_id actor);
  void (*entity_write)(void *ctx, qa_app_q3_source_entity_view view);
  void (*attach)(void *ctx, double slot, qa_owned_actor actor, bool player);
  void (*release_entity)(void *ctx, double slot);
  void (*reset_client)(void *ctx, double slot, bool for_projection);
  double (*pool_num_entities)(void *ctx);
  void (*pool_restore_counts)(void *ctx, double num_entities, double max_clients);
  void (*refresh_client_config)(void *ctx, qa_app_q3_source_entity_view view, qa_app_q3_selected_client_pose pose);
  void (*run_client_powerups)(void *ctx, double slot);
  void (*expand_invulnerability)(void *ctx, double slot);
  void (*drop_movement_timers)(void *ctx, double slot, double milliseconds);
  void (*client_timer_actions)(void *ctx, double slot, double milliseconds);
  bool (*invulnerability_blocks)(void *ctx, double slot, qa_vec3 direction, qa_vec3 point, double means_of_death);
  bool (*check_gauntlet_attack)(void *ctx, double slot);
  void (*hook_free)(void *ctx, double slot);
  double (*client_speed_multiplier)(void *ctx, double slot);
  bool (*armor_can_be_grabbed)(void *ctx, double slot);
  double (*persistent_powerup_tag_at)(void *ctx, double slot); /* missionpack inventory tag */
  bool (*can_item_be_grabbed)(void *ctx, double slot, double game_type, double model_index, double dropped, double generic1);
  double (*pickup_holdable)(void *ctx, double pickup_slot, double player_slot);
  double (*pickup_persistent)(void *ctx, double pickup_slot, double player_slot, const char *handicap);
  void (*set_pickup_item)(void *ctx, double slot, double item_index, double count, double generic1);
  void (*toss_persistent)(void *ctx, double slot, void (*on_pickup)(void *cb_ctx, qa_app_q3_source_entity_view pickup), void *cb_ctx);
  void (*return_persistent)(void *ctx, double slot);
  double (*pool_spawn)(void *ctx);
  void (*pool_free)(void *ctx, double slot);
  void (*add_predictable_event)(void *ctx, double slot, double event, double time);
  void (*fire_weapon)(void *ctx, double slot);
  void (*use_holdable)(void *ctx, double slot, double event, bool missionpack);
  void (*teleport_player)(void *ctx, double slot, qa_vec3 origin, qa_vec3 angles);
  void (*entity_think)(void *ctx, double slot); /* NULL think aborts in host with TS text */
  bool (*has_think)(void *ctx, double slot);
  void (*clear_nextthink)(void *ctx, double slot);
  void (*publish_entity)(void *ctx, qa_app_q3_source_entity_view view); /* snapshot+event publication */
  qa_save_value *(*capture_graph)(void *ctx);
  void (*restore_graph)(void *ctx, const qa_save_reader *reader);
  void (*close_runtime)(void *ctx);
} qa_app_q3_selected_source_host;

typedef struct qa_app_q3_selected_source qa_app_q3_selected_source;
/* restored NULL = TS undefined. */
qa_app_q3_selected_source *qa_app_q3_selected_source_create(qa_app_q3_selected_source_host host, const qa_save_value *restored);
void qa_app_q3_selected_source_destroy(qa_app_q3_selected_source *source);
const qa_app_q3_selected_source_host *qa_app_q3_selected_source_host_of(const qa_app_q3_selected_source *source);
double qa_app_q3_selected_source_generation(const qa_app_q3_selected_source *source);
bool qa_app_q3_selected_source_owns_equipment(const qa_app_q3_selected_source *source);
bool qa_app_q3_selected_source_active(const qa_app_q3_selected_source *source);
/* TS presentationBaseline !== null (state rebuilt via q3/presentation). */
bool qa_app_q3_selected_source_has_presentation_baseline(const qa_app_q3_selected_source *source);
bool qa_app_q3_selected_source_actor(qa_app_q3_selected_source *source, double slot, qa_actor_id *out);
bool qa_app_q3_selected_source_live(qa_app_q3_selected_source *source, qa_actor_id actor);
/* TS actors.onRelease cleanup. */
void qa_app_q3_selected_source_release_actor(qa_app_q3_selected_source *source, qa_owned_actor actor);
/* TS records schedule/runThink seams. */
void qa_app_q3_selected_source_schedule(qa_app_q3_selected_source *source, qa_owned_actor actor);
void qa_app_q3_selected_source_run_think(qa_app_q3_selected_source *source, qa_owned_actor actor, double time);
typedef struct qa_app_q3_source_motion {
  const char *kind; qa_vec3 velocity, angular_velocity; double gravity; qa_vec3 gravity_vector;
  double clipmask; bool has_owner; qa_actor_id owner;
} qa_app_q3_source_motion;
qa_app_q3_source_motion qa_app_q3_selected_source_motion(qa_app_q3_selected_source *source, qa_owned_actor actor, qa_body_state body);
typedef struct qa_app_q3_source_solid {
  const char *family, *solid; bool has_model; double model; bool has_owner; qa_actor_id owner;
} qa_app_q3_source_solid;
qa_app_q3_source_solid qa_app_q3_selected_source_collision(qa_app_q3_selected_source *source, qa_owned_actor actor);
bool qa_app_q3_selected_source_fixed_pose(qa_app_q3_selected_source *source, qa_owned_actor actor,
    qa_q3_postures postures, qa_fixed_movement_pose *out);
void qa_app_q3_selected_source_advance_movement(qa_app_q3_selected_source *source, qa_owned_actor actor, double milliseconds);
void qa_app_q3_selected_source_end_command(qa_app_q3_selected_source *source, qa_owned_actor actor, double milliseconds);
bool qa_app_q3_selected_source_blocks_damage(qa_app_q3_selected_source *source, qa_damage_request request);
void qa_app_q3_selected_source_admit(qa_app_q3_selected_source *source, qa_owned_actor actor);
void qa_app_q3_selected_source_respawn(qa_app_q3_selected_source *source, qa_actor_id actor);
bool qa_app_q3_selected_source_gauntlet_hit(qa_app_q3_selected_source *source, qa_owned_actor actor);
void qa_app_q3_selected_source_command(qa_app_q3_selected_source *source, qa_owned_actor actor, bool attack, bool selected, bool alive);
void qa_app_q3_selected_source_release_hook(qa_app_q3_selected_source *source, qa_actor_id actor);
bool qa_app_q3_selected_source_grapple_point(qa_app_q3_selected_source *source, qa_actor_id actor, qa_vec3 *out);
bool qa_app_q3_selected_source_pull(qa_app_q3_selected_source *source, qa_owned_actor actor, qa_vec3 *out);
qa_app_q3_selected_equipment_state qa_app_q3_selected_source_equipment(qa_app_q3_selected_source *source, qa_owned_actor actor);
typedef struct qa_app_q3_source_inventory_row { char item[128]; const char *label; double count; bool usable; const char *icon; } qa_app_q3_source_inventory_row;
size_t qa_app_q3_selected_source_inventory(qa_app_q3_selected_source *source, qa_owned_actor actor,
    qa_app_q3_source_inventory_row *out, size_t capacity);
double qa_app_q3_selected_source_firing_delay(qa_app_q3_selected_source *source, qa_owned_actor actor, double milliseconds);
double qa_app_q3_selected_source_speed_multiplier(qa_app_q3_selected_source *source, qa_actor_id actor);
bool qa_app_q3_selected_source_pickup_allowed(qa_app_q3_selected_source *source, qa_original_pickup_offer offer);
typedef struct qa_app_q3_source_pickup_descriptor {
  double item_type; const char *class_name; qa_actor_id player_actor, item_actor;
  double game_type, count, generic1; bool dropped;
} qa_app_q3_source_pickup_descriptor;
typedef struct qa_app_q3_source_pickup_admission { const char *kind; double respawn_seconds; } qa_app_q3_source_pickup_admission;
qa_app_q3_source_pickup_admission qa_app_q3_selected_source_take_pickup(qa_app_q3_selected_source *source,
    qa_app_q3_source_pickup_descriptor offer);
void qa_app_q3_selected_source_died(qa_app_q3_selected_source *source, qa_actor_id actor);
void qa_app_q3_selected_source_consume(qa_app_q3_selected_source *source, qa_owned_actor actor, double item);
bool qa_app_q3_selected_source_give_holdable(qa_app_q3_selected_source *source, qa_owned_actor actor, const char *name);
void qa_app_q3_selected_source_restore_equipment(qa_app_q3_selected_source *source, qa_owned_actor actor,
    qa_app_q3_selected_equipment_state saved);
void qa_app_q3_selected_source_fire(qa_app_q3_selected_source *source, qa_owned_actor actor, double weapon, qa_weapon_step_input input);
void qa_app_q3_selected_source_use_holdable(qa_app_q3_selected_source *source, qa_owned_actor actor, double event);
void qa_app_q3_selected_source_synchronize(qa_app_q3_selected_source *source);
qa_save_value *qa_app_q3_selected_source_capture(qa_app_q3_selected_source *source);
void qa_app_q3_selected_source_restore(qa_app_q3_selected_source *source, const qa_save_value *value);
void qa_app_q3_selected_source_close(qa_app_q3_selected_source *source);
/* Legacy restore entry (TS restoreLegacy): clients pair actors with checkpoints. */
void qa_app_q3_selected_source_restore_legacy(qa_app_q3_selected_source *source, const qa_save_value *saved,
    const qa_owned_actor *client_actors, const qa_app_q3_selected_arsenal_checkpoint *client_states, size_t client_count);
#endif
