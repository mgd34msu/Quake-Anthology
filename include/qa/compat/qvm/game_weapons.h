/* Port of src/compat/qvm/game-weapons.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_GAME_WEAPONS_H
#define QA_COMPAT_QVM_GAME_WEAPONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/artifacts.h"
#include "qa/compat/qvm/game.h"
#include "qa/compat/qvm/game_equipment_movement.h"
#include "qa/compat/qvm/game_inventory.h"
#include "qa/compat/qvm/mod_weapon_stage.h"
#include "qa/compat/qvm/regions.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/source_items.h"
#include "qa/contracts/source_match.h"

typedef struct qa_qvm_primary_weapon_region_stop { double entry, join; } qa_qvm_primary_weapon_region_stop;
typedef struct qa_qvm_primary_weapon_damage_factor {
  double entry, result;
  qa_qvm_primary_weapon_region_stop stop;
} qa_qvm_primary_weapon_damage_factor;
typedef struct qa_qvm_primary_weapon_delay_player { double movement_global, player_offset; } qa_qvm_primary_weapon_delay_player;
typedef struct qa_qvm_primary_weapon_teleport {
  double entry;
  qa_qvm_region_evaluation region;
  qa_qvm_region_evaluation objectives;
  double spawn, view;
} qa_qvm_primary_weapon_teleport;
typedef struct qa_qvm_primary_weapon_availability {
  double movement_type;
  const double *excluded;
  size_t excluded_count;
  double health, team, spectator_team, flags, respawn_flag;
} qa_qvm_primary_weapon_availability;
typedef struct qa_qvm_primary_weapon_powerups { double quad, haste, flight; } qa_qvm_primary_weapon_powerups;
typedef struct qa_qvm_primary_weapon_torso_animation { double entry, attack, melee; } qa_qvm_primary_weapon_torso_animation;
typedef struct qa_qvm_primary_weapon_water_level { double entity_offset, movement_offset; } qa_qvm_primary_weapon_water_level;
/* ammo_is_inventory is the TS ammo === "inventory". */
typedef struct qa_qvm_primary_weapon_drop {
  double entry, argument, weapon;
  bool ammo_is_inventory;
  double ammo;
  qa_qvm_primary_weapon_region_stop region;
} qa_qvm_primary_weapon_drop;
typedef struct qa_qvm_primary_weapon_give_named { double entry, join, name, item; } qa_qvm_primary_weapon_give_named;
typedef struct qa_qvm_primary_weapon_give {
  double entry, argument, weapons, ammo;
  qa_qvm_primary_weapon_give_named named;
} qa_qvm_primary_weapon_give;

/* TS QvmPrimaryWeaponProfile. match is optional (has_match). */
typedef struct qa_qvm_primary_weapon_profile {
  qa_module_identity module;
  bool has_match;
  qa_source_primary_match match;
  qa_qvm_abi_profile abi_profile;
  qa_qvm_equipment_movement_profile equipment_movement;
  double entity_stride, client_stride, client_pointer;
  qa_qvm_weapon_dispatcher_definition stage;
  qa_qvm_primary_weapon_damage_factor damage_factor;
  const qa_source_equipment_context *equipment_contexts;
  size_t equipment_context_count;
  qa_qvm_region_evaluation delay;
  qa_qvm_primary_weapon_delay_player delay_player;
  qa_qvm_primary_weapon_teleport teleport;
  double max_health, persistent_max_health;
  qa_qvm_primary_weapon_availability availability;
  qa_qvm_primary_weapon_powerups powerups;
  qa_qvm_primary_weapon_torso_animation torso_animation;
  qa_qvm_primary_weapon_water_level water_level;
  qa_qvm_primary_weapon_drop drop;
  qa_qvm_primary_weapon_give give;
} qa_qvm_primary_weapon_profile;

/* TS Services.drop result. has_ammo_words false is TS undefined. */
typedef struct qa_qvm_primary_weapon_drop_result {
  double weapon, ammo;
  bool has_ammo_words;
  const qa_qvm_inventory_word *ammo_words;
  size_t ammo_word_count;
} qa_qvm_primary_weapon_drop_result;

/* TS Services (module-private interface). False returns are TS null;
 * give category keeps its exact strings ("weapons" | "ammo"). */
typedef struct qa_qvm_primary_weapon_services {
  void *ctx;
  bool (*actor)(void *ctx, double slot, qa_actor_id *out);
  bool (*slot)(void *ctx, qa_actor_id actor, double *out);
  bool (*selected)(void *ctx, qa_actor_id actor);
  /* Optional (has_equipment_movement); false return is TS null. */
  bool has_equipment_movement;
  bool (*equipment_movement)(void *ctx, qa_actor_id actor, qa_qvm_equipment_motion *out);
  void (*attempted)(void *ctx, qa_actor_id actor, double weapon);
  void (*accepted)(void *ctx, qa_actor_id actor, double weapon);
  void (*completed)(void *ctx, qa_actor_id actor, bool reached_attack_decision);
  void (*give)(void *ctx, qa_actor_id actor, const char *category);
  bool (*give_item)(void *ctx, qa_actor_id actor, const char *name);
  bool (*drop)(void *ctx, qa_actor_id actor, qa_qvm_primary_weapon_drop_result *out);
} qa_qvm_primary_weapon_services;

typedef enum qa_qvm_primary_powerup {
  QA_QVM_PRIMARY_POWERUP_QUAD,
  QA_QVM_PRIMARY_POWERUP_HASTE,
  QA_QVM_PRIMARY_POWERUP_FLIGHT
} qa_qvm_primary_powerup;

typedef struct qa_qvm_primary_weapons qa_qvm_primary_weapons;

typedef int32_t (*qa_qvm_primary_weapon_run_fn)(void *ctx);

/* NULL only on OOM. `game`/`artifact` are borrowed; the profile is
 * copied by value (its lists stay borrowed). */
qa_qvm_primary_weapons *qa_qvm_primary_weapons_create(qa_qvm_game *game,
                                                      const qa_resolved_qvm_artifact *artifact,
                                                      qa_qvm_primary_weapon_profile profile,
                                                      qa_qvm_primary_weapon_services services);
void qa_qvm_primary_weapons_destroy(qa_qvm_primary_weapons *weapons);
void qa_qvm_primary_weapons_close(qa_qvm_primary_weapons *weapons);

/* False (with *out unset) is the TS null match getter. */
bool qa_qvm_primary_weapons_get_match(const qa_qvm_primary_weapons *weapons,
                                      qa_source_primary_match *out);
double qa_qvm_primary_weapons_score(qa_qvm_primary_weapons *weapons, qa_actor_id actor);
void qa_qvm_primary_weapons_set_score(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                      double score);
/* kind keeps its exact strings ("client-command" | "movement-slice"). */
int32_t qa_qvm_primary_weapons_equipment_movement(qa_qvm_primary_weapons *weapons,
                                                  qa_qvm_module_function_call *call, const char *kind,
                                                  qa_qvm_primary_weapon_run_fn run, void *run_ctx);
bool qa_qvm_primary_weapons_settled(qa_qvm_primary_weapons *weapons, qa_actor_id actor);
/* Returns the item or NULL (TS null). */
qa_item_id qa_qvm_primary_weapons_active(qa_qvm_primary_weapons *weapons, qa_actor_id actor);
void qa_qvm_primary_weapons_drop_objectives(qa_qvm_primary_weapons *weapons, qa_actor_id actor);
void qa_qvm_primary_weapons_spawn_point(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                        qa_vec3 *out_origin, qa_vec3 *out_angles);
void qa_qvm_primary_weapons_teleport_state(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                           qa_vec3 origin, qa_vec3 velocity, qa_vec3 angles,
                                           double hold_milliseconds);
double qa_qvm_primary_weapons_damage_factor(qa_qvm_primary_weapons *weapons, qa_actor_id actor);
/* Returns the item or NULL (TS null). */
qa_item_id qa_qvm_primary_weapons_equipment_context(qa_qvm_primary_weapons *weapons,
                                                    qa_provider_id provider);
double qa_qvm_primary_weapons_equipment_delay(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                              qa_provider_id provider, double milliseconds);
double qa_qvm_primary_weapons_weapon_delay(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                           double milliseconds);
void qa_qvm_primary_weapons_attack_animation(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                             bool melee);
double qa_qvm_primary_weapons_water_level(qa_qvm_primary_weapons *weapons, qa_actor_id actor);
double qa_qvm_primary_weapons_max_health(qa_qvm_primary_weapons *weapons, qa_actor_id actor);
void qa_qvm_primary_weapons_set_max_health(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                           double health);
bool qa_qvm_primary_weapons_available(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                      bool attacking);
double qa_qvm_primary_weapons_powerup_until(qa_qvm_primary_weapons *weapons, qa_actor_id actor,
                                            qa_qvm_primary_powerup powerup);
void qa_qvm_primary_weapons_teleport(qa_qvm_primary_weapons *weapons, qa_actor_id actor);

#endif
