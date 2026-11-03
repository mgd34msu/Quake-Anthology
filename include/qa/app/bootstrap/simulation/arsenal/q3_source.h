/* Port of src/app/bootstrap/simulation/arsenal/q3-source.ts (Q3SelectedSource).
 * The source owns the real content/q3 runtimes as TS does: Q3EntityRecords,
 * EntityPool, Q3WorldAdapter, Q3CombatBridge, MissileRuntime, WeaponRuntime,
 * PersonalPortalRuntime (missionpack) and GameRandom. Its public members are
 * the TS readonly members other modules read. TS overrides traceActor on its
 * world to project hit actors; C has no subclassing, so world_server and
 * world_spatial are the world's ServerWorld/ActorSpatialQueries faces with that
 * override, and every runtime the source builds uses them.
 * Host callbacks all receive host.ctx (configstrings carries its own ctx).
 * Create returns NULL only on OOM; destroy closes an open source first.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q3_SOURCE_H
#define QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_Q3_SOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/arsenal/q3.h"
#include "qa/app/bootstrap/simulation/arsenal/q3_source_legacy.h"
#include "qa/app/bootstrap/simulation/physics.h"
#include "qa/app/bootstrap/simulation/q3/presentation.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q3/base/combat_bridge.h"
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/item_lifecycle.h"
#include "qa/content/q3/base/game/missile.h"
#include "qa/content/q3/base/game/numeric.h"
#include "qa/content/q3/base/game/personal_portal.h"
#include "qa/content/q3/base/game/utilities.h"
#include "qa/content/q3/base/game/weapon.h"
#include "qa/content/q3/base/records.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/entity_state.h"
#include "qa/content/q3/base/world_adapter.h"
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/contracts/world.h"
#include "qa/movement/q3/types.h"
#include "qa/persistence/value.h"

/* TS Q3SelectedClientPose. */
typedef struct qa_app_q3_selected_client_pose {
  qa_vec3 angles;
  double view_height, max_health;
  qa_q3_base_team team;
  double quad_until, haste_until;
} qa_app_q3_selected_client_pose;
/* TS Q3SelectedClientEffects. */
typedef struct qa_app_q3_selected_client_effects {
  double teleport_bit;
  qa_vec3 view_angles, delta_angles;
  double pm_flags, pm_time, invulnerability_time, max_health;
} qa_app_q3_selected_client_effects;
/* TS Q3SelectedEquipmentState = Pick<Q3ArsenalRuntimeState, ...>. */
typedef qa_app_q3_equipment_state qa_app_q3_selected_equipment_state;

/* TS host.execute's step closure. */
typedef void (*qa_app_q3_selected_source_step_fn)(void *step_ctx, double previous, double time);

/* TS Q3SelectedSourceHost: the Q3RecordHost, Q3WorldAdapterHost and
 * Q3CombatBridgeHost members it picks, then its own. NULL fn pointers are TS
 * undefined only where marked optional. */
typedef struct qa_app_q3_selected_source_host {
  void *ctx;
  /* Q3RecordHost: actors, bodies, combat, inventory, callbacks. */
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  qa_actor_callback_table *callbacks;
  /* Q3WorldAdapterHost: queries, collision, curves, playerCurveClip. */
  qa_shared_scene_queries *queries;
  void (*collision)(void *ctx, qa_owned_actor actor, qa_actor_collision collision);
  bool (*curves)(void *ctx);
  bool (*player_curve_clip)(void *ctx);
  /* Q3CombatBridgeHost picks. */
  qa_provider_id combat_provider, inventory_provider, movement_provider;
  bool has_source_damage_modifier; qa_source_damage_modifier source_damage_modifier; /* optional */
  bool has_damage_powerup_owner; qa_provider_id damage_powerup_owner;                /* optional */
  qa_victim_armor_context (*armor_context)(void *ctx, qa_damage_request request);
  double (*game_type)(void *ctx);
  bool (*friendly_fire)(void *ctx);
  double (*knockback)(void *ctx);
  double (*intermission_queued)(void *ctx);
  void (*check_hurt_carrier)(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *attacker);
  /* Own members. */
  qa_provider_id provider;
  qa_q3_base_product product;
  bool equipment_source; /* TS equipment.kind: true "source", false "primary" */
  bool has_weapon_effects; /* TS weaponEffects (optional) */
  double (*weapon_damage_factor)(void *ctx, qa_actor_id actor);
  double (*weapon_firing_delay)(void *ctx, qa_actor_id actor, double milliseconds, double persistent_powerup);
  qa_content_id content;
  qa_q3_config_string_store configstrings;
  const char *(*userinfo)(void *ctx, qa_actor_id actor); /* borrowed until the next call */
  const qa_weapon_behavior_projectile_port *weapon_behavior; /* optional (NULL) */
  double max_clients, seed;
  double (*now)(void *ctx);
  qa_owned_actor (*world_actor)(void *ctx);
  bool (*player)(void *ctx, qa_actor_id actor, qa_app_q3_selected_client_pose *out); /* false is TS null */
  double (*quad_factor)(void *ctx, const qa_actor_id *actor); /* actor NULL is TS undefined */
  double (*proximity_timeout)(void *ctx);
  double (*model_index)(void *ctx, const char *path);
  double (*sound_index)(void *ctx, const char *path);
  void (*print)(void *ctx, const char *text);
  void (*execute)(void *ctx, qa_owned_actor actor, qa_app_q3_selected_source_step_fn step, void *step_ctx);
  void (*event)(void *ctx, qa_owned_actor actor, const qa_q3_base_entity_state *state, double time);
  void (*client_changed)(void *ctx, qa_owned_actor actor, qa_app_q3_selected_client_effects before,
      qa_app_q3_selected_client_effects after);
  bool (*check_obelisk_attack)(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *attacker);
  void (*drop_objectives)(void *ctx, qa_owned_actor actor);
  void (*return_pickup)(void *ctx, qa_actor_id actor);
  void (*spawn_point)(void *ctx, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles);
} qa_app_q3_selected_source_host;

/* Insertion-ordered TS Map/Set keyed by OwnedActor (private storage). */
typedef struct qa_app_q3_selected_source_entry {
  qa_owned_actor actor;
  bool present;
  double slot, event, time;
  qa_app_q3_selected_client_effects effects;
} qa_app_q3_selected_source_entry;
typedef struct qa_app_q3_selected_source_map {
  qa_app_q3_selected_source_entry *items;
  size_t count, capacity, live;
} qa_app_q3_selected_source_map;
struct qa_app_q3_selected_source_step;

/* TS Q3SelectedSource. */
/* TS PresentedClient: inputs of the last client presentation configstring written for a slot. */
typedef struct qa_app_q3_presented_client {
  bool set; /* false = TS undefined */
  char *userinfo;
  double team, max_health, wins, losses, team_leader, game_type;
} qa_app_q3_presented_client;
typedef struct qa_app_q3_selected_source {
  qa_app_q3_selected_source_host host;
  qa_q3_base_entity_records *records;
  qa_q3_entity_pool pool;
  qa_q3_base_world_adapter world;
  qa_q3_base_server_world world_server;
  qa_q3_base_actor_spatial_queries world_spatial;
  qa_q3_base_combat_bridge bridge;
  qa_q3_missile_runtime missiles;
  qa_q3_weapon_runtime weapons;
  qa_q3_personal_portal_runtime *personal_portal; /* NULL for baseq3 (TS null); points at portal_storage */
  qa_game_random random;
  /* Private TS members. */
  qa_q3_personal_portal_runtime portal_storage;
  qa_app_q3_selected_source_map projected, client_effects, executing, published;
  double depth;
  int64_t unobserve;
  double previous;
  bool has_clock;
  double clock;
  bool closed;
  double revision;
  qa_app_q3_source_presentation_state *restored_presentation;
  struct qa_app_q3_selected_source_step *steps;
  qa_actor_id solid_owner;
  /* TS presented: (PresentedClient | undefined)[] indexed by slot. */
  qa_app_q3_presented_client *presented; size_t presented_count;
} qa_app_q3_selected_source;

/* TS new Q3SelectedSource(host, restored): restored NULL is TS undefined. */
qa_app_q3_selected_source *qa_app_q3_selected_source_create(qa_app_q3_selected_source_host host, const qa_save_value *restored);
void qa_app_q3_selected_source_destroy(qa_app_q3_selected_source *source);
/* TS getters. */
double qa_app_q3_selected_source_generation(const qa_app_q3_selected_source *source);
bool qa_app_q3_selected_source_owns_equipment(const qa_app_q3_selected_source *source);
/* TS presentationBaseline: NULL is TS null; borrowed from the source. */
const qa_app_q3_source_presentation_state *qa_app_q3_selected_source_presentation_baseline(const qa_app_q3_selected_source *source);
bool qa_app_q3_selected_source_active(const qa_app_q3_selected_source *source);
/* TS bridge member (shared simulation reads it). */
qa_q3_base_combat_bridge *qa_app_q3_selected_source_bridge(qa_app_q3_selected_source *source);
/* TS records.nativeByActor(actor)?.classname: NULL is TS undefined; the string is the entity's (valid until it changes). */
const char *qa_app_q3_selected_source_native_classname(qa_app_q3_selected_source *source, qa_actor_id actor);
/* TS actor(slot): false is TS null. */
bool qa_app_q3_selected_source_actor(qa_app_q3_selected_source *source, double slot, qa_actor_id *out);
bool qa_app_q3_selected_source_live(qa_app_q3_selected_source *source, qa_actor_id actor);
qa_q2_motion qa_app_q3_selected_source_motion(qa_app_q3_selected_source *source, qa_owned_actor actor, qa_body_state body);
/* TS collision(actor). The returned owner points into the source until the next call. */
qa_app_sim_shared_solid qa_app_q3_selected_source_collision(qa_app_q3_selected_source *source, qa_owned_actor actor);
/* TS fixedPose: false is TS null. */
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
/* TS grapplePoint / pull: false is TS null. */
bool qa_app_q3_selected_source_grapple_point(qa_app_q3_selected_source *source, qa_actor_id actor, qa_vec3 *out);
bool qa_app_q3_selected_source_pull(qa_app_q3_selected_source *source, qa_owned_actor actor, qa_vec3 *out);
qa_app_q3_selected_equipment_state qa_app_q3_selected_source_equipment(qa_app_q3_selected_source *source, qa_owned_actor actor);
/* TS inventory(actor) rows; returns the TS row count, writing at most capacity rows. */
typedef struct qa_app_q3_source_inventory_row {
  char item[128]; /* TS ItemId "q3:<className>" */
  const char *label; double count; bool usable; const char *icon; /* icon NULL is TS null */
} qa_app_q3_source_inventory_row;
size_t qa_app_q3_selected_source_inventory(qa_app_q3_selected_source *source, qa_owned_actor actor,
    qa_app_q3_source_inventory_row *out, size_t capacity);
double qa_app_q3_selected_source_firing_delay(qa_app_q3_selected_source *source, qa_owned_actor actor, double milliseconds);
double qa_app_q3_selected_source_speed_multiplier(qa_app_q3_selected_source *source, qa_actor_id actor);
bool qa_app_q3_selected_source_pickup_allowed(qa_app_q3_selected_source *source, qa_original_pickup_offer offer);
qa_q3_source_pickup_admission qa_app_q3_selected_source_take_pickup(qa_app_q3_selected_source *source,
    qa_q3_source_pickup_descriptor offer);
void qa_app_q3_selected_source_died(qa_app_q3_selected_source *source, qa_actor_id actor);
void qa_app_q3_selected_source_consume(qa_app_q3_selected_source *source, qa_owned_actor actor, double item);
bool qa_app_q3_selected_source_give_holdable(qa_app_q3_selected_source *source, qa_owned_actor actor, const char *name);
void qa_app_q3_selected_source_restore_equipment(qa_app_q3_selected_source *source, qa_owned_actor actor,
    qa_app_q3_selected_equipment_state saved);
void qa_app_q3_selected_source_fire(qa_app_q3_selected_source *source, qa_owned_actor actor, double weapon, qa_weapon_step_input input);
void qa_app_q3_selected_source_use_holdable(qa_app_q3_selected_source *source, qa_owned_actor actor, double event);
void qa_app_q3_selected_source_synchronize(qa_app_q3_selected_source *source);
/* TS sourceState(): owned state, NULL only on OOM. */
qa_app_q3_source_presentation_state *qa_app_q3_selected_source_source_state(qa_app_q3_selected_source *source);
/* TS presentations(): see qa_app_q3_pool_models. */
size_t qa_app_q3_selected_source_presentations(qa_app_q3_selected_source *source, qa_app_simulation_presentation *out, size_t capacity);
/* TS capture(): owned tree, NULL only on OOM. */
qa_save_value *qa_app_q3_selected_source_capture(qa_app_q3_selected_source *source);
/* TS restoreLegacy(saved, clients): clients pair actors[i] with states[i]. */
void qa_app_q3_selected_source_restore_legacy(qa_app_q3_selected_source *source, const qa_app_legacy_q3_source *saved,
    const qa_owned_actor *client_actors, const qa_app_q3_selected_arsenal_checkpoint *client_states, size_t client_count);
void qa_app_q3_selected_source_restore(qa_app_q3_selected_source *source, const qa_save_value *value);
void qa_app_q3_selected_source_close(qa_app_q3_selected_source *source);
/* TS private project(actor), used by restoreLegacy's projection callback. NULL is TS null. */
qa_q3_game_entity *qa_app_q3_selected_source_project(qa_app_q3_selected_source *source, qa_actor_id actor);
#endif
