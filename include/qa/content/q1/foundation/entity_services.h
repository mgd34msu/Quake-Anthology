/*
 * Port of src/content/q1/foundation/entity-services.ts. GPL-2.0-or-later.
 * Damage/combat-context/pusher/projectile-behavior host seams are the
 * qa_q1_host mirrors in types.h; see unresolved notes there.
 */
#ifndef QA_CONTENT_Q1_FOUNDATION_ENTITY_SERVICES_H
#define QA_CONTENT_Q1_FOUNDATION_ENTITY_SERVICES_H

#include "qa/content/q1/foundation/callbacks.h"
#include "qa/content/q1/foundation/extensions.h"
#include "qa/content/q1/foundation/precache.h"
#include "qa/contracts/pickups.h"

qa_vec3 qa_q1_drop_punch(qa_vec3 angles, double elapsed, const qa_numeric_operations *numeric);
qa_item_id qa_q1_ammo_item(qa_q1_weapon weapon); /* NULL = TS null */

typedef bool (*qa_q1_path_touch_fn)(void *ctx, qa_q1_actor *corner, qa_q1_actor *mover);
typedef void (*qa_q1_spawn_fn)(qa_q1_services *game, qa_q1_actor *entity);

/* Mirrored PickupAdmission subset (TS src/contracts/pickups.ts /
 * src/world/gameplay/pickups.ts) as fn+ctx; NULL table = TS null. */
typedef struct qa_q1_pickup_admission {
  void *ctx;
  bool (*ammo)(void *ctx, qa_owned_actor actor, qa_item_id item, double amount);
  bool (*weapon)(void *ctx, qa_owned_actor actor, qa_item_id item, const char *selection);
  bool (*owns)(void *ctx, qa_actor_id actor, qa_item_id item);
  bool (*cargo_taken)(void *ctx, qa_owned_actor actor, const char *selection);
} qa_q1_pickup_admission;

/* Mirrored MonsterMission (TS src/content/monsters/authored.ts). */
typedef struct qa_q1_monster_mission {
  void *ctx;
  void (*spawned)(void *ctx);
  void (*started)(void *ctx);
  void (*found_target)(void *ctx);
  bool (*use)(void *ctx, const qa_actor_id *activator);
  void (*killed)(void *ctx, const qa_actor_id *by);
  bool (*route)(void *ctx, qa_actor_id *out_goal); /* false = null */
  bool (*combat_route)(void *ctx, qa_actor_id *out_goal, bool *stand_ground);
} qa_q1_monster_mission;

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
  const qa_q1_pickup_admission *pickup_admission;
  /* Registered extensions (borrowed pointers, insertion order). */
  struct { const qa_q1_weapon_definition *def; } *registered_weapons; size_t registered_weapon_count, registered_weapon_capacity;
  const qa_q1_player_extension **player_extensions; size_t player_extension_count, player_extension_capacity;
  const qa_q1_weapon_rules **weapon_rules; size_t weapon_rule_count, weapon_rule_capacity;
  const qa_q1_state_extension **state_extensions; size_t state_extension_count, state_extension_capacity;
  qa_q1_weapon weapon_order[24]; size_t weapon_order_count; char *weapon_order_owner;
  struct { char *id; qa_q1_path_touch_fn fn; void *ctx; } *path_touches; size_t path_touch_count, path_touch_capacity;
  struct { char *classname; qa_q1_spawn_fn fn; } *spawners; size_t spawner_count, spawner_capacity;
  struct { qa_actor_id actor; const qa_q1_monster_mission *mission; } *missions; size_t mission_count, mission_capacity;
  /* Inventory-lite fallback storage when host inventory fns are NULL. */
  struct { qa_actor_id actor; char *item; double count, capacity; bool has_capacity; } *inventory; size_t inventory_count, inventory_capacity;
  struct { qa_actor_id actor; bool has; } *inventory_owners; size_t inventory_owner_count, inventory_owner_capacity;
};

/* False only on OOM. Registers SUB_Remove/SUB_Null/DelayThink +
 * mover/spawn/pickup/weapon/monster module callbacks. */
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
bool qa_q1_fire_registered_weapon(qa_q1_services *game, qa_q1_player_state *player);
void qa_q1_register_state_extension(qa_q1_services *game, const qa_q1_state_extension *extension);
void qa_q1_spawn_entity(qa_q1_services *game, qa_q1_actor *entity);
void qa_q1_register_spawn(qa_q1_services *game, const char *classname, qa_q1_spawn_fn handler);
void qa_q1_replace_spawn(qa_q1_services *game, const char *classname, qa_q1_spawn_fn handler);
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
void qa_q1_set_health(qa_q1_services *game, qa_owned_actor actor, double health);
qa_q1_body qa_q1_get_body(qa_q1_services *game, qa_q1_actor *entity);
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
double qa_q1_damage(qa_q1_services *game, qa_actor_id target, qa_actor_id inflictor, const qa_actor_id *attacker,
                    double amount, qa_q1_weapon weapon, const char *delivery, const char *death_type);
double qa_q1_powerup_expires(qa_q1_services *game, qa_actor_id actor, qa_q1_powerup powerup);
qa_q1_source_target qa_q1_get_source_target(qa_q1_services *game, qa_actor_id actor);
qa_q1_monster_target_result qa_q1_monster_target(qa_q1_services *game, qa_actor_id actor);
bool qa_q1_can_damage(qa_q1_services *game, qa_actor_id target, qa_actor_id inflictor);
void qa_q1_radius_damage(qa_q1_services *game, qa_actor_id inflictor, const qa_actor_id *attacker, double amount,
                         const qa_actor_id *ignore, qa_q1_weapon weapon, const char *death_type);
void qa_q1_calc_move(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 destination, double speed, qa_q1_bound_action done);
void qa_q1_physics_step(qa_q1_services *game, double seconds, double elapsed_seconds);
void qa_q1_physics_entity(qa_q1_services *game, qa_owned_actor actor, double seconds, double elapsed_seconds);
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
void qa_q1_player_input(qa_q1_services *game, qa_owned_actor actor, bool attack, bool jump, const double *teleport_until);
bool qa_q1_attack(qa_q1_services *game, qa_owned_actor actor, qa_vec3 view_angles, double seconds, double water_level);
bool qa_q1_weapon_input(qa_q1_services *game, qa_owned_actor actor, bool pressed, qa_vec3 view_angles, double seconds, double water_level);
bool qa_q1_select_weapon(qa_q1_services *game, qa_owned_actor actor, qa_q1_weapon weapon);
qa_q1_weapon qa_q1_choose_best(qa_q1_services *game, qa_owned_actor actor);
void qa_q1_give_powerup(qa_q1_services *game, qa_q1_player_state *player, qa_q1_powerup powerup, double duration);
void qa_q1_drop_shells(qa_q1_services *game, qa_vec3 origin);
size_t qa_q1_presentations(qa_q1_services *game, qa_q1_presentation *out, size_t capacity);
void qa_q1_check_water_transition(qa_q1_services *game, qa_q1_actor *entity);
void qa_q1_resume_thinks(qa_q1_services *game);
/* Inventory via host or fallback. */
double qa_q1_inventory_count(qa_q1_services *game, qa_actor_id actor, qa_item_id item);
double qa_q1_inventory_give(qa_q1_services *game, qa_owned_actor actor, qa_item_id item, double count);
bool qa_q1_inventory_consume(qa_q1_services *game, qa_owned_actor actor, qa_item_id item, double count);
const qa_q1_monster_mission *qa_q1_mission(qa_q1_services *game, qa_actor_id actor);
void qa_q1_set_mission(qa_q1_services *game, qa_actor_id actor, const qa_q1_monster_mission *mission);
double qa_q1_host_random(qa_q1_services *game);
qa_q1_trace qa_q1_host_trace(qa_q1_services *game, qa_q1_trace_request request);
void qa_q1_emit(qa_q1_services *game, qa_q1_event event);

#endif
