/*
 * Port of src/content/q1/missionpacks/types.ts.
 * Reuses the landed qa_q1b_* Q1 foundation mirrors
 * (include/qa/content/q1/base/foundation_types.h; TS owners:
 * src/content/q1/foundation/entity.ts, entity-services.ts, types.ts).
 * qa_q1mp_game wraps qa_q1b_game with the mission-pack-only host
 * surface the q1b mirror does not carry (weapon/pickup/player-extension
 * registration, inventory give/consume returns, message arguments,
 * aim, weapon model/punch, controlPlayer, Q1Base provider). TS string
 * unions (pack, weapon, powerup, edition) stay C strings.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_TYPES_H
#define QA_CONTENT_Q1_MISSIONPACKS_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q1/base/foundation_types.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"

typedef qa_q1b_actor qa_q1mp_actor;
typedef qa_q1b_player_state qa_q1mp_player_state;
typedef qa_q1b_opt_actor qa_q1mp_opt_actor;
typedef qa_q1b_body qa_q1mp_body;
typedef qa_q1b_basis qa_q1mp_basis;
typedef qa_q1b_channel qa_q1mp_channel;
typedef const char *qa_q1mp_pack; /* TS Q1MissionPack */

/* TS MissionWeaponDefinition. pickup/ammo NULL = TS null. */
typedef struct qa_q1mp_mission_weapon_definition {
  const char *id;
  const char *model;
  const char *pickup; /* nullable */
  const char *world_model;
  const char *ammo; /* nullable, TS ItemId */
  double pickup_ammo;
  double rank;
} qa_q1mp_mission_weapon_definition;
extern const qa_q1mp_mission_weapon_definition qa_q1mp_mission_weapons[8];
#define QA_Q1MP_MISSION_WEAPONS_COUNT 8

typedef struct qa_q1mp_game qa_q1mp_game;

/* TS Q1WeaponDefinition subset passed to game.registerWeapon. */
typedef struct qa_q1mp_weapon_registration {
  const char *id;
  const char *ammo; /* nullable */
  const char *model;
  double rank;
  void *ctx;
  bool (*best_available)(void *ctx, qa_q1mp_game *game, qa_q1mp_player_state *player);
  bool (*fire)(void *ctx, qa_q1mp_game *game, qa_q1mp_player_state *player);
  void (*animate)(void *ctx, qa_q1mp_game *game, qa_q1mp_player_state *player, double seconds);
} qa_q1mp_weapon_registration;

/* TS Q1PickupRules (foundation/extensions.ts) as registered here. */
typedef struct qa_q1mp_pickup_rules {
  const char *id;
  void *ctx;
  bool (*weapon_leave)(void *ctx, qa_q1mp_game *game);
  double (*weapon_rank)(void *ctx, const char *weapon);
  bool (*auto_switch)(void *ctx, qa_q1mp_game *game, qa_q1mp_player_state *player, bool was_owned);
  const char *(*weapon_granted)(void *ctx, qa_q1mp_game *game, qa_q1mp_player_state *player, const char *weapon);
  double (*respawn)(void *ctx, qa_q1mp_game *game, qa_q1mp_actor *entity, double delay);
} qa_q1mp_pickup_rules;

/* TS Q1PlayerExtension fields used by this slice. */
typedef struct qa_q1mp_player_extension {
  const char *id;
  void *ctx;
  void (*attach)(void *ctx, qa_q1mp_game *game, qa_q1mp_player_state *player);
  void (*frame)(void *ctx, qa_q1mp_game *game, qa_q1mp_player_state *player);
  void (*after_physics)(void *ctx, qa_q1mp_game *game, qa_q1mp_player_state *player, double seconds);
  void (*before_physics)(void *ctx, qa_q1mp_game *game, qa_q1mp_player_state *player, double seconds);
  /* TS damage preparation hook (player.ts); returns prepared amount,
   * has=false mirrors TS undefined. */
  bool has_damage;
  double (*damage)(void *ctx, qa_q1mp_game *game, qa_actor_id target, qa_q1mp_opt_actor attacker, double amount, const char *weapon);
  uint8_t *(*capture_travel)(void *ctx, qa_q1mp_game *game, qa_q1mp_player_state *player, size_t *out_length);
  void (*restore_travel)(void *ctx, qa_q1mp_game *game, qa_q1mp_player_state *player, const uint8_t *bytes, size_t length);
} qa_q1mp_player_extension;

/* TS Q1SourceFinale (base/rules.ts): kind "finale"|"sell-screen"|other. */
typedef struct qa_q1mp_source_finale {
  const char *kind;
  const char *text;
  double track;
  bool has; /* false = TS undefined */
} qa_q1mp_source_finale;
typedef struct qa_q1mp_intermission_rule {
  const char *id;
  void *ctx;
  qa_q1mp_source_finale (*finale)(void *ctx, double stage);
} qa_q1mp_intermission_rule;

/* Mirror of Q1Base surface used here (TS owner:
 * src/content/q1/base/provider.ts + base/rules.ts campaign/levelRules).
 * has_finished_finale/official_campaign/registered are TS properties
 * read at call time through the getters when set. */
typedef struct qa_q1mp_q1base {
  void *ctx;
  double (*campaign_read_flags)(void *ctx);
  void (*campaign_write_flags)(void *ctx, double flags);
  bool (*has_finished_finale)(void *ctx);
  bool has_official_campaign; bool official_campaign;
  bool has_registered; bool registered;
  void (*register_intermission_rule)(void *ctx, const qa_q1mp_intermission_rule *rule);
  void (*defer_exit)(void *ctx, double at);
  void (*begin_cutscene)(void *ctx, const char *map, qa_q1mp_opt_actor cause, double at);
  qa_q1mp_source_finale (*advance_finale)(void *ctx, double at);
} qa_q1mp_q1base;

/* TS Q1EntityServices extensions beyond qa_q1b_game. NULL extras are
 * guarded exactly where the TS guards optional services; required
 * extras are called only on paths the TS reaches unconditionally. */
struct qa_q1mp_game {
  qa_q1b_game *base;
  void *ctx;
  qa_q1mp_q1base *q1_base; /* borrowed; TS q1Base(game) */
  void (*register_weapon)(void *ctx, const qa_q1mp_weapon_registration *weapon);
  void (*register_weapon_order)(void *ctx, const char *id, const char *const *order, size_t count);
  void (*register_pickup_rules)(void *ctx, const qa_q1mp_pickup_rules *rules);
  void (*register_player_extension)(void *ctx, const qa_q1mp_player_extension *extension);
  const char *(*weapon_model)(void *ctx, const char *weapon);
  void (*weapon_punch)(void *ctx, qa_q1mp_player_state *player, double punch);
  double (*inventory_give)(void *ctx, qa_owned_actor actor, qa_item_id item, double count);
  void (*inventory_consume)(void *ctx, qa_owned_actor actor, qa_item_id item, double count);
  void (*message_args)(void *ctx, qa_q1mp_opt_actor player, const char *text, bool center,
                       const char *const *args, size_t arg_count);
  qa_vec3 (*aim)(void *ctx, qa_owned_actor actor, qa_vec3 forward);
  void (*control_player)(void *ctx, qa_actor_id player, const char *kind, qa_vec3 origin, qa_vec3 angles, qa_vec3 view_offset);
  void (*set_gravity)(void *ctx, qa_actor_id actor, double scale);
  /* TS game.registerDamagePreparation / source effects (player.ts). */
  void (*register_damage_preparation)(void *ctx, const char *id, void *hook_ctx,
                                      double (*prepare)(void *hook_ctx, qa_q1mp_game *game, qa_actor_id target,
                                                        qa_q1mp_opt_actor attacker, double amount, bool *out_handled));
  /* TS weaponAttackDelay/weaponFrameDelay; NULL = identity delay. */
  double (*weapon_attack_delay)(void *ctx, qa_q1mp_player_state *player, double delay);
  double (*weapon_frame_delay)(void *ctx, qa_q1mp_player_state *player, double delay);
  /* TS launchProjectileBehavior; NULL = no-op. */
  void (*launch_projectile_behavior)(void *ctx, qa_q1mp_actor *entity, qa_actor_id owner, const char *weapon, const char *kind);
  /* TS inventory.consume returns bool. */
  bool (*inventory_consume_bool)(void *ctx, qa_owned_actor actor, qa_item_id item, double count);
  /* TS host.emit weapon/beam/particles/sound/finale/ambient events not
   * carried by qa_q1b_event; NULL = dropped presentation event. */
  void (*emit_weapon)(void *ctx, qa_actor_id player, const char *weapon, const char *view_model, double frame, double punch);
  void (*emit_beam)(void *ctx, const char *style, qa_actor_id actor, qa_vec3 start, qa_vec3 end);
  void (*emit_particles)(void *ctx, qa_vec3 origin, qa_vec3 direction, double color, double count);
  void (*emit_sound_event)(void *ctx, qa_actor_id actor, const char *path, qa_q1mp_channel channel, double volume, double attenuation);
  void (*emit_finale)(void *ctx, const char *text, double stage);
  void (*emit_ambient)(void *ctx, qa_vec3 origin, const char *path, double volume, double attenuation);
  void (*emit_colored_explosion)(void *ctx, qa_vec3 origin, double color_start, double color_length);
  void (*emit_achievement)(void *ctx, qa_q1mp_opt_actor player, const char *id);
  void (*emit_server_command)(void *ctx, const char *text);
  void (*emit_monster_killed)(void *ctx, qa_actor_id actor, double total, double found);
  void (*emit_message_event)(void *ctx, qa_actor_id player, const char *text, const char *const *args, size_t arg_count, bool center);
  /* TS givePowerup(player, kind, seconds?). */
  void (*give_powerup)(void *ctx, qa_q1mp_player_state *player, const char *powerup, bool has_seconds, double seconds);
  /* TS registerDamageSourceEffects / registerWeaponRules /
   * registerPathTouch / replaceSpawn surfaces. */
  void (*register_damage_source_effects)(void *ctx, const char *id, void *hook);
  void (*register_weapon_rules)(void *ctx, const char *id, void *hook);
  void (*register_path_touch)(void *ctx, const char *id, void *hook_ctx,
                              bool (*touch)(void *hook_ctx, qa_q1mp_actor *corner, qa_q1mp_actor *mover));
  void (*replace_spawn)(void *ctx, const char *classname, qa_q1b_spawn_fn fn, void *fn_ctx);
  bool (*controls_trajectory)(void *ctx, qa_actor_id actor);
};

/* Convenience: the wrapped q1b game (never NULL in ported calls). */
static inline qa_q1b_game *qa_q1mp_g(qa_q1mp_game *game) { return game->base; }

void qa_q1mp_set_mission_reference(qa_q1mp_actor *entity, const char *key, qa_q1mp_opt_actor actor);
qa_q1mp_opt_actor qa_q1mp_mission_reference(qa_q1mp_game *game, qa_q1mp_actor *entity, const char *key);
void qa_q1mp_set_mission_number(qa_q1mp_actor *entity, const char *key, double value);
qa_vec3 qa_q1mp_velocity_angles(qa_vec3 velocity);
qa_vec3 qa_q1mp_grenade_velocity(qa_q1mp_game *game, qa_vec3 angles, qa_vec3 aimed);
void qa_q1mp_move_missile(qa_q1mp_game *game, qa_q1mp_actor *entity, qa_vec3 velocity);

#endif
