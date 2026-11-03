/*
 * Port of src/content/q1/foundation/entity-services.ts. GPL-2.0-or-later.
 * Bodies, combat, inventory, callbacks and liveness are the shared world
 * tables of game->host (TS Q1FoundationHost), called directly. Released
 * actors leave the entity/player maps through the registry onRelease
 * listener; their structs stay allocated until qa_q1_services_free (TS
 * GC), since callers may still hold them (a remove inside a touch).
 */
#ifndef QA_CONTENT_Q1_FOUNDATION_ENTITY_SERVICES_H
#define QA_CONTENT_Q1_FOUNDATION_ENTITY_SERVICES_H

#include "qa/content/q1/foundation/callbacks.h"
#include "qa/content/q1/foundation/extensions.h"
#include "qa/content/q1/foundation/precache.h"
#include "qa/content/monsters/authored.h"
#include "qa/contracts/pickups.h"
#include "qa/world/gameplay/policies.h"

qa_vec3 qa_q1_drop_punch(qa_vec3 angles, double elapsed, const qa_numeric_operations *numeric);
qa_item_id qa_q1_ammo_item(qa_q1_weapon weapon); /* NULL = TS null */

struct qa_q1_pickup_def; /* private to pickups.c */

typedef bool (*qa_q1_path_touch_fn)(void *ctx, qa_q1_actor *corner, qa_q1_actor *mover);
typedef void (*qa_q1_spawn_fn)(void *ctx, qa_q1_services *game, qa_q1_actor *entity);

/* TS authoredPathFollower(mover) result; advance(name, goal, pauseUntil). */
typedef struct qa_q1_authored_path_follower {
  const char *targetname;
  bool has_enemy; qa_actor_id enemy;
  void *ctx;
  void (*advance)(void *ctx, const char *name, const qa_actor_id *goal, double pause_until);
} qa_q1_authored_path_follower;

/* One per-player timestamp (TS WeakMap<Q1PlayerState, number>). */
typedef struct qa_q1_player_timer { const qa_q1_player_state *player; double at; } qa_q1_player_timer;

struct qa_q1_services {
  qa_q1_host host;
  qa_q1_options options;
  qa_q1_callback_registry *named;
  qa_q1_precache_registry *precaches;
  qa_q1_actor **entities; size_t entity_count, entity_capacity;
  qa_q1_player_state **players; size_t player_count, player_capacity;
  double time, frame_seconds, force_retouch;
  qa_q1_basis basis;
  double total_secrets, found_secrets, total_monsters, killed_monsters, world_type;
  char *map_name;
  qa_q1_actor *world, *sight_entity;
  double sight_time;
  bool has_intermission; char *intermission_map; bool has_intermission_cause;
  qa_actor_id intermission_cause; double intermission_exit_after;
  double sequence, next_dynamic_slot;
  bool base_team_health;
  const qa_q1_pickup_rules *pickup_rules;
  const qa_pickup_admission *pickup_admission; /* TS pickupAdmission; NULL = null */
  /* Registered extensions (borrowed pointers, insertion order). */
  struct { const qa_q1_weapon_definition *def; } *registered_weapons; size_t registered_weapon_count, registered_weapon_capacity;
  const qa_q1_player_extension **player_extensions; size_t player_extension_count, player_extension_capacity;
  const qa_q1_weapon_rules **weapon_rules; size_t weapon_rule_count, weapon_rule_capacity;
  const qa_q1_state_extension **state_extensions; size_t state_extension_count, state_extension_capacity;
  qa_q1_weapon weapon_order[24]; size_t weapon_order_count; char *weapon_order_owner;
  struct { char *id; qa_q1_path_touch_fn fn; void *ctx; } *path_touches; size_t path_touch_count, path_touch_capacity;
  struct { char *classname; qa_q1_spawn_fn fn; void *ctx; } *spawners; size_t spawner_count, spawner_capacity;
  struct { qa_actor_id actor; const qa_monster_mission *mission; } *missions; size_t mission_count, mission_capacity;
  /* TS authoredTargets (borrowed targets, insertion order). */
  struct { qa_actor_id actor; qa_authored_target *target; } *authored_targets;
  size_t authored_target_count, authored_target_capacity;
  /* TS authoredPathFollower; fn NULL = TS null, false = null result. */
  struct { void *ctx; bool (*fn)(void *ctx, qa_actor_id mover, qa_q1_authored_path_follower *out); } authored_path_follower;
  /* TS sourceDoorKeys / sourceMessageAllPlayers / sourceGrenadeFix fields. */
  bool source_door_keys, source_message_all_players, source_grenade_fix;
  /* TS pickups.ts WeakSets mg3Pickups / sourceNoMegaRot keyed by game. */
  bool mg3_pickups, source_no_mega_rot;
  /* TS pickups.ts sourceItemStart WeakMap; fn NULL = no entry (has() false).
   * fn false = TS null result; *use NULL = TS null use. */
  struct { void *ctx; bool (*fn)(void *ctx, qa_q1_actor *entity, const char **place, const char **use); } source_item_start;
  /* TS sourceBulletHole / sourceBloodyNightmare / sourceFogPush; fn NULL = TS null. */
  struct { void *ctx; void (*fn)(void *ctx, qa_owned_actor shooter, qa_vec3 position, qa_vec3 normal); } source_bullet_hole;
  struct { void *ctx; bool (*fn)(void *ctx); } source_bloody_nightmare;
  struct { void *ctx; bool (*fn)(void *ctx, qa_actor_id player, qa_q1_actor *source); } source_fog_push;
  /* TS sourceDamageEffects (insertion-ordered, ids owned). */
  struct { char *id; qa_q1_damage_source_effects effects; } *source_damage_effects;
  size_t source_damage_effect_count, source_damage_effect_capacity;
  /* TS worldPrecaches: run in order when worldspawn spawns. */
  struct { void *ctx; void (*fn)(void *ctx, qa_q1_services *game); } *world_precaches;
  size_t world_precache_count, world_precache_capacity;
  /* Contexts of callbacks bound in host.callbacks (owned). */
  void **actor_bindings; size_t actor_binding_count, actor_binding_capacity;
  /* Released entities/players, kept until free (TS GC; see header note). */
  qa_q1_actor **released_entities; size_t released_entity_count, released_entity_capacity;
  qa_q1_player_state **released_players; size_t released_player_count, released_player_capacity;
  /* TS protectSound / waterborne (per player) and powerupCues rows. */
  struct { const qa_q1_player_state *player; double at; } *protect_sounds; size_t protect_sound_count, protect_sound_capacity;
  /* TS weapons.ts quadSoundAt and spawns.ts windFlySound WeakMaps (per player,
   * not saved): qa_q1_player_timer_get/set. */
  qa_q1_player_timer *quad_sounds; size_t quad_sound_count, quad_sound_capacity;
  qa_q1_player_timer *wind_fly_sounds; size_t wind_fly_sound_count, wind_fly_sound_capacity;
  /* Storage behind the ammo list of the last weapon offer returned by
   * qa_q1_observe_supply / qa_q1_preview_supply (valid until the next call). */
  qa_pickup_ammo_grant observed_supply_ammo;
  const qa_q1_player_state **waterborne; size_t waterborne_count, waterborne_capacity;
  struct { const qa_q1_player_state *player; qa_q1_powerup powerup; bool warned; double flash_at, hum; } *powerup_cues;
  size_t powerup_cue_count, powerup_cue_capacity;
  /* TS legacyAnimations: version 5 grunt and dog cursors restored for the
   * base controller to adopt (insertion order). */
  struct { qa_owned_actor owner; qa_q1_legacy_animation animation; } *legacy_animations;
  size_t legacy_animation_count, legacy_animation_capacity;
  /* TS sourceWorldWords: QC globals kept on world; a foreign world has no Q1
   * world actor, so they live here (insertion order, names owned) and the
   * "mg3:world-words" state extension saves them. */
  struct { char *name; double value; } *source_world_words;
  size_t source_world_word_count, source_world_word_capacity;
  /* TS foreignPickup: pickups for classnames the Q1 foundation does not define,
   * supplied by the composition (Q3 items); fn NULL = TS null. The pickup
   * definition is private to pickups.c (see qa_q1_foreign_q3_pickup). */
  struct { void *ctx; bool (*fn)(void *ctx, const qa_q1_actor *entity, struct qa_q1_pickup_def *out); } foreign_pickup;
  /* TS Q1StateExtension.restoreAbsent, per registered extension (optional). */
  struct { const qa_q1_state_extension *extension; void (*restore_absent)(void *ctx); } *restore_absents;
  size_t restore_absent_count, restore_absent_capacity;
  int64_t release_subscription; /* host.actors.onRelease id */
  bool keeping_owned_spawns; /* TS private keepingOwnedSpawns (adjoin) */
  /* Game-lifetime copies behind emitted event strings and argument arrays
   * (see qa_q1_emit). Owned; deduplicated by content. */
  struct qa_q1_emit_intern *emit_intern;
};

/* TS game.options.skill / teamplay ?? 0 / gravity, read live (see qa_q1_options.live). */
double qa_q1_option_skill(const qa_q1_services *game);
double qa_q1_option_teamplay(const qa_q1_services *game);
double qa_q1_option_gravity(const qa_q1_services *game);

/* TS legacyAnimations.set(owner, animation). */
void qa_q1_legacy_animations_set(qa_q1_services *game, qa_owned_actor owner, qa_q1_legacy_animation animation);
/* False only on OOM. Registers SUB_Remove/SUB_Null/DelayThink +
 * mover/spawn/pickup/weapon module callbacks. */
bool qa_q1_services_init(qa_q1_services *game, qa_q1_host host, qa_q1_options options);
void qa_q1_services_free(qa_q1_services *game);
qa_provider_id qa_q1_services_provider(const qa_q1_services *game);
bool qa_q1_services_uses_id1(const qa_q1_services *game);
const char *qa_q1_services_precache_model(qa_q1_services *game, const char *path);
const char *qa_q1_services_precache_sound(qa_q1_services *game, const char *path);
void qa_q1_services_set_time(qa_q1_services *game, double value); /* fround, TS setter */

void qa_q1_set_gravity(qa_q1_services *game, qa_actor_id actor, double scale);
void qa_q1_control_player(qa_q1_services *game, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles, qa_vec3 view_offset);
void qa_q1_set_base_team_health(qa_q1_services *game, bool enabled);
void qa_q1_register_path_touch(qa_q1_services *game, const char *id, qa_q1_path_touch_fn fn, void *ctx);
bool qa_q1_source_path_touch(qa_q1_services *game, qa_q1_actor *corner, qa_q1_actor *mover);
void qa_q1_register_weapon_rules(qa_q1_services *game, const qa_q1_weapon_rules *rules);
bool qa_q1_consume_weapon_ammo(qa_q1_services *game, qa_q1_player_state *player, qa_item_id item, double amount);
void qa_q1_weapon_before_fire(qa_q1_services *game, qa_q1_player_state *player);
double qa_q1_weapon_attack_delay(qa_q1_services *game, qa_q1_player_state *player, double initial);
double qa_q1_weapon_frame_delay(qa_q1_services *game, qa_q1_player_state *player, double initial);
double qa_q1_nail_speed(qa_q1_services *game, qa_q1_player_state *player, double initial);
void qa_q1_register_pickup_rules(qa_q1_services *game, const qa_q1_pickup_rules *rules);
void qa_q1_register_weapon(qa_q1_services *game, const qa_q1_weapon_definition *definition);
void qa_q1_replace_weapon(qa_q1_services *game, const qa_q1_weapon_definition *definition);
void qa_q1_register_weapon_order(qa_q1_services *game, const char *id, const qa_q1_weapon *order, size_t count);
void qa_q1_register_player_extension(qa_q1_services *game, const qa_q1_player_extension *extension);
bool qa_q1_inventory_capacity(qa_q1_services *game, qa_actor_id actor, qa_item_id item, double *out);
const char *qa_q1_services_weapon_model(qa_q1_services *game, qa_q1_weapon weapon, qa_q1_player_state *player);
qa_item_id qa_q1_services_weapon_item(qa_q1_services *game, qa_q1_weapon weapon);
qa_item_id qa_q1_services_weapon_ammo(qa_q1_services *game, qa_q1_weapon weapon);
bool qa_q1_weapon_available(qa_q1_services *game, qa_q1_player_state *player, qa_q1_weapon weapon, const char *purpose);
/* TS ammoCount override of weaponAvailable/chooseBest; count NULL = inventory count. */
typedef double (*qa_q1_ammo_count_fn)(void *ctx, qa_item_id item);
bool qa_q1_weapon_available_counted(qa_q1_services *game, qa_q1_player_state *player, qa_q1_weapon weapon,
                                    const char *purpose, qa_q1_ammo_count_fn count, void *count_ctx);
bool qa_q1_fire_registered_weapon(qa_q1_services *game, qa_q1_player_state *player);
void qa_q1_register_state_extension(qa_q1_services *game, const qa_q1_state_extension *extension);
void qa_q1_spawn_entity(qa_q1_services *game, qa_q1_actor *entity);
/* TS spawnEntity(entity, { deathmatch }): the spawn sees options.deathmatch
 * replaced for its duration (TS spawnOptions). deathmatch NULL = TS {}. */
void qa_q1_spawn_entity_context(qa_q1_services *game, qa_q1_actor *entity, const double *deathmatch);
/* TS worldPrecaches.push(precache): run in order when worldspawn spawns. */
void qa_q1_add_world_precache(qa_q1_services *game, void (*fn)(void *ctx, qa_q1_services *game), void *ctx);
void qa_q1_register_spawn(qa_q1_services *game, const char *classname, qa_q1_spawn_fn handler, void *ctx);
void qa_q1_replace_spawn(qa_q1_services *game, const char *classname, qa_q1_spawn_fn handler, void *ctx);
bool qa_q1_has_spawn(const qa_q1_services *game, const char *classname); /* TS hasSpawn */
/* TS Q1StateExtension.restoreAbsent: runs (with extension->ctx) when a save
 * written before the extension existed has no entry for it; without it a
 * missing entry is an error. NULL fn = none. */
void qa_q1_set_state_extension_restore_absent(qa_q1_services *game, const qa_q1_state_extension *extension,
                                              void (*restore_absent)(void *ctx));
/* The restoreAbsent of a registered extension, or NULL. */
void (*qa_q1_state_extension_restore_absent(const qa_q1_services *game, const qa_q1_state_extension *extension))(void *ctx);
/* TS pathGoal(name): QC find(world, targetname, name) as a walking monster uses
 * it; "" matches the first edict, which is the first client. False = null. */
bool qa_q1_path_goal(qa_q1_services *game, const char *name, qa_actor_id *out);
/* TS sourceWorldWords get/set/clear. get returns false when absent. */
bool qa_q1_source_world_word_get(const qa_q1_services *game, const char *name, double *out);
void qa_q1_source_world_word_set(qa_q1_services *game, const char *name, double value);
void qa_q1_source_world_words_clear(qa_q1_services *game);
/* TS adjoin(register): runs register_fn so the spawn handlers it adds never
 * displace handlers the selected program already owns. */
void qa_q1_adjoin(qa_q1_services *game, void (*register_fn)(void *ctx, qa_q1_services *game), void *ctx);
qa_q1_actor *qa_q1_create(qa_q1_services *game, const char *classname, const qa_q1_entity *source, const double *source_ordinal);
qa_q1_actor *qa_q1_attach_existing(qa_q1_services *game, qa_owned_actor actor, const char *classname, const qa_q1_entity *source, const double *source_ordinal);
qa_q1_actor *qa_q1_clone_entity(qa_q1_services *game, qa_q1_actor *source);
void qa_q1_initialize_weapon_inventory(qa_q1_services *game, qa_owned_actor actor);
qa_q1_player_state *qa_q1_attach_player(qa_q1_services *game, qa_owned_actor actor, qa_q1_weapon weapon,
                                       bool initialize_inventory, const double *max_health);
qa_q1_player_state *qa_q1_player(qa_q1_services *game, qa_actor_id actor);
qa_q1_actor *qa_q1_get_entity(qa_q1_services *game, const qa_actor_id *actor);
bool qa_q1_is_player(qa_q1_services *game, const qa_actor_id *actor);
bool qa_q1_live(qa_q1_services *game, const qa_q1_actor *entity);
double qa_q1_health(qa_q1_services *game, qa_actor_id actor);
qa_q1_body qa_q1_get_body(qa_q1_services *game, qa_q1_actor *entity);
/* TS setBody(entity, patch: Partial<BodyState>). ground is read only when
 * has_ground (NULL = TS null). */
typedef struct qa_q1_body_patch {
  bool has_origin; qa_vec3 origin;
  bool has_angles; qa_vec3 angles;
  bool has_velocity; qa_vec3 velocity;
  bool has_bounds; qa_bounds bounds;
  bool has_ground; const qa_actor_id *ground;
} qa_q1_body_patch;
void qa_q1_set_body(qa_q1_services *game, qa_q1_actor *entity, const qa_q1_body_patch *patch);
void qa_q1_set_body_patch(qa_q1_services *game, qa_q1_actor *entity, const qa_q1_body *patch,
                          bool origin, bool angles, bool velocity, bool bounds, bool ground);
void qa_q1_link(qa_q1_services *game, qa_q1_actor *entity);
void qa_q1_set_origin(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 origin);
void qa_q1_set_bounds(qa_q1_services *game, qa_q1_actor *entity, qa_bounds bounds);
void qa_q1_remove(qa_q1_services *game, qa_q1_actor *entity);
void qa_q1_schedule(qa_q1_services *game, qa_q1_actor *entity, double delay, qa_q1_bound_action callback);
void qa_q1_schedule_at(qa_q1_services *game, qa_q1_actor *entity, double due_seconds, qa_q1_bound_action callback);
void qa_q1_cancel(qa_q1_services *game, qa_q1_actor *entity);
void qa_q1_run_think(qa_q1_services *game, qa_q1_actor *entity); /* TS bound think dispatch */
void qa_q1_sound(qa_q1_services *game, qa_actor_id actor, const char *path, qa_q1_sound_channel channel, double attenuation, double volume);
void qa_q1_sound_defaults(qa_q1_services *game, qa_actor_id actor, const char *path);
void qa_q1_message(qa_q1_services *game, const qa_actor_id *player, const char *text, bool center, const qa_q1_msg_arg *args, size_t arg_count);
void qa_q1_effect(qa_q1_services *game, const char *effect, qa_vec3 origin, const qa_actor_id *actor, double amount);
size_t qa_q1_find(qa_q1_services *game, const char *targetname, qa_q1_actor **out, size_t capacity);
void qa_q1_use_targets(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *activator);
/* TS useTargets for an AuthoredTarget that is not a Q1Actor. */
void qa_q1_use_authored_targets(qa_q1_services *game, const qa_authored_target *entity, const qa_actor_id *activator);
/* weapon NULL = TS null; delivery NULL = "direct"; death_type NULL = "";
 * armor_effect NULL = absent ("bypass" | "half-effectiveness"). */
qa_damage_outcome qa_q1_damage(qa_q1_services *game, qa_actor_id target, qa_actor_id inflictor,
                               const qa_actor_id *attacker, double amount, qa_q1_weapon weapon,
                               const char *delivery, const char *death_type, const char *armor_effect);
/* TS combatContext(request): the Q1 combat policy context. */
qa_q1_combat_context qa_q1_combat_context_for(qa_q1_services *game, qa_damage_request request);
double qa_q1_powerup_expires(qa_q1_services *game, qa_actor_id actor, qa_q1_powerup powerup);
qa_q1_source_target qa_q1_get_source_target(qa_q1_services *game, qa_actor_id actor);
qa_q1_monster_target_result qa_q1_monster_target(qa_q1_services *game, qa_actor_id actor);
bool qa_q1_can_damage(qa_q1_services *game, qa_actor_id target, qa_actor_id inflictor);
void qa_q1_radius_damage(qa_q1_services *game, qa_actor_id inflictor, const qa_actor_id *attacker, double amount,
                         const qa_actor_id *ignore, qa_q1_weapon weapon, const char *death_type);
void qa_q1_calc_move(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 destination, double speed, qa_q1_bound_action done);
void qa_q1_physics_step(qa_q1_services *game, double seconds, double elapsed_seconds);
void qa_q1_services_physics_entity(qa_q1_services *game, qa_owned_actor actor, double seconds, double elapsed_seconds);
void qa_q1_apply_projectile_behavior(qa_q1_services *game, qa_owned_actor actor, double seconds);
void qa_q1_launch_projectile_behavior(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id shooter, qa_q1_weapon weapon, const char *role);
void qa_q1_player_frame(qa_q1_services *game, qa_owned_actor actor, double seconds, const double *water_level);
void qa_q1_weapon_punch(qa_q1_services *game, qa_q1_player_state *player, double pitch);
bool qa_q1_advance_punch(qa_q1_services *game, qa_actor_id actor, double elapsed, const qa_numeric_operations *numeric, qa_vec3 *out);
void qa_q1_weapon_frame(qa_q1_services *game, qa_owned_actor actor, double seconds);
void qa_q1_player_after_physics(qa_q1_services *game, qa_owned_actor actor, double seconds);
void qa_q1_travel(qa_q1_services *game, const char *map, const qa_actor_id *cause);
void qa_q1_begin_intermission(qa_q1_services *game, const char *map, const qa_actor_id *cause);
bool qa_q1_request_intermission_exit(qa_q1_services *game, double seconds, bool pressed);
qa_q1_basis qa_q1_make_vectors(qa_q1_services *game, qa_vec3 angles);
void qa_q1_begin_frame(qa_q1_services *game, double seconds, double elapsed_seconds);
void qa_q1_services_player_input(qa_q1_services *game, qa_owned_actor actor, bool attack, bool jump, const double *teleport_until);
bool qa_q1_attack(qa_q1_services *game, qa_owned_actor actor, qa_vec3 view_angles, double seconds, double water_level);
bool qa_q1_weapon_input(qa_q1_services *game, qa_owned_actor actor, bool pressed, qa_vec3 view_angles, double seconds, double water_level);
bool qa_q1_select_weapon(qa_q1_services *game, qa_owned_actor actor, qa_q1_weapon weapon);
qa_q1_weapon qa_q1_choose_best(qa_q1_services *game, qa_owned_actor actor);
qa_q1_weapon qa_q1_choose_best_counted(qa_q1_services *game, qa_owned_actor actor, qa_q1_ammo_count_fn count, void *count_ctx);
void qa_q1_give_powerup(qa_q1_services *game, qa_q1_player_state *player, qa_q1_powerup powerup, double duration);
void qa_q1_drop_shells(qa_q1_services *game, qa_vec3 origin);
size_t qa_q1_presentations(qa_q1_services *game, qa_q1_presentation *out, size_t capacity);
void qa_q1_check_water_transition(qa_q1_services *game, qa_q1_actor *entity);
void qa_q1_resume_thinks(qa_q1_services *game);
const qa_monster_mission *qa_q1_mission(qa_q1_services *game, qa_actor_id actor);
void qa_q1_set_mission(qa_q1_services *game, qa_actor_id actor, const qa_monster_mission *mission);
/* TS authoredTargets get/set/delete; target NULL deletes. Borrowed. */
qa_authored_target *qa_q1_authored_target(qa_q1_services *game, qa_actor_id actor);
void qa_q1_set_authored_target(qa_q1_services *game, qa_actor_id actor, qa_authored_target *target);
/* TS registerDamageSourceEffects; duplicate ids abort. */
void qa_q1_register_damage_source_effects(qa_q1_services *game, const char *id, qa_q1_damage_source_effects effects);
/* TS damageSourceEffects: folds every registered table in registration order.
 * The ctx of the returned table is game. */
qa_q1_damage_source_effects qa_q1_damage_source_effects_of(qa_q1_services *game);
/* TS bindActorCallbacks: binds the entity's slots in host.callbacks. */
void qa_q1_bind_actor_callbacks(qa_q1_services *game, qa_q1_actor *entity);
/* TS WeakMap get ?? 0 / set over a per-player timer table. */
double qa_q1_player_timer_get(const qa_q1_player_timer *table, size_t count, const qa_q1_player_state *player);
void qa_q1_player_timer_set(qa_q1_player_timer **table, size_t *count, size_t *capacity,
                            const qa_q1_player_state *player, double at);
double qa_q1_host_random(qa_q1_services *game);
qa_q1_trace qa_q1_host_trace(qa_q1_services *game, qa_q1_trace_request request);
/* Forwards to host.emit. Emit sites may format strings and argument arrays on
 * the stack (Rogue team names, door key messages), so the event handed to the
 * host points only at game-owned interned copies: the struct itself is
 * borrowed for the call, but a host may keep a shallow copy of it, and every
 * string, args and parts pointer in it stays valid until qa_q1_services_free.
 * Copies are deduplicated by content, so memory grows with distinct texts. */
void qa_q1_emit(qa_q1_services *game, qa_q1_event event);

#endif
