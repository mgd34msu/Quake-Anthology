/*
 * Mirrors of src/content/q1/foundation types whose C owner has not
 * landed, for the src/content/q1/base port. Replace when the owner
 * lands. Collision-proof qa_q1b_ names; TS owners:
 *  - qa_q1b_actor / qa_q1b_monster_state: Q1Actor / Q1Monster,
 *    src/content/q1/foundation/entity.ts
 *  - qa_q1b_game: Q1EntityServices,
 *    src/content/q1/foundation/entity-services.ts
 *  - qa_q1b_host / qa_q1b_options / qa_q1b_player_state /
 *    qa_q1b_trace(+request) / qa_q1b_event / qa_q1b_channel:
 *    Q1FoundationHost / Q1FoundationOptions / Q1PlayerState /
 *    Q1Trace(+Request) / Q1Event / Q1SoundChannel,
 *    src/content/q1/foundation/types.ts
 *  - qa_q1b_handlers / qa_q1b_named / qa_q1b_state_extension:
 *    Q1CallbackHandlers / Q1CallbackRegistry / Q1StateExtension,
 *    src/content/q1/foundation/callbacks.ts
 *  - qa_q1b_player_extension / qa_q1b_pickup_rules: Q1PlayerExtension /
 *    Q1PickupRules, src/content/q1/foundation/extensions.ts
 *  - qa_q1b_body: BodyState, src/contracts/world.ts (via world/actors)
 *  - qa_q1b_monster_mission: MonsterMission,
 *    src/content/monsters/authored.ts (surface used here)
 *  - doorDown / pathEndTime: src/content/q1/foundation/movers.ts /
 *    monsters.ts (declared here, implemented in foundation_types.c)
 * Only the surface this slice uses is mirrored. Game/host methods are
 * fn pointers; TS string unions stay C strings; ActorId|null becomes
 * qa_q1b_opt_actor. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q1_BASE_FOUNDATION_TYPES_H
#define QA_CONTENT_Q1_BASE_FOUNDATION_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/monsters/target.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/pickups.h"

typedef struct qa_q1b_opt_actor { bool has; qa_actor_id id; } qa_q1b_opt_actor;
typedef struct qa_q1b_opt_owned { bool has; qa_owned_actor actor; } qa_q1b_opt_owned;
#define QA_Q1B_NO_ACTOR ((qa_q1b_opt_actor){0})
static inline qa_q1b_opt_actor qa_q1b_some_actor(qa_actor_id id) {
  qa_q1b_opt_actor value = {true, id};
  return value;
}
bool qa_q1b_owned_equal(qa_owned_actor a, qa_owned_actor b);

/* TS Q1SoundChannel: name, or NULL name + numeric (-1|5|6|7). */
typedef struct qa_q1b_channel { const char *name; double numeric; } qa_q1b_channel;
static inline qa_q1b_channel qa_q1b_channel_named(const char *name) {
  qa_q1b_channel value = {name, 0};
  return value;
}
static inline qa_q1b_channel qa_q1b_channel_numeric(double numeric) {
  qa_q1b_channel value = {NULL, numeric};
  return value;
}

/* TS BodyState subset (origin/angles/velocity/bounds/ground). */
typedef struct qa_q1b_body {
  qa_vec3 origin; qa_vec3 angles; qa_vec3 velocity; qa_bounds bounds;
  qa_q1b_opt_actor ground;
} qa_q1b_body;
typedef struct qa_q1b_body_patch {
  bool has_origin; qa_vec3 origin;
  bool has_angles; qa_vec3 angles;
  bool has_velocity; qa_vec3 velocity;
  bool has_bounds; qa_bounds bounds;
  bool has_ground; qa_q1b_opt_actor ground;
} qa_q1b_body_patch;

typedef struct qa_q1b_trace {
  double fraction; qa_vec3 end; qa_vec3 normal; qa_q1b_opt_actor actor;
  bool start_solid; bool all_solid; bool sky; bool in_open; bool in_water;
} qa_q1b_trace;
typedef struct qa_q1b_trace_request {
  qa_vec3 start; qa_vec3 end; qa_bounds bounds; qa_q1b_opt_actor ignore;
  bool monsters; bool missile;
} qa_q1b_trace_request;
typedef qa_angle_vectors qa_q1b_basis;

/* TS Q1MessagePart (args are string|number). */
typedef struct qa_q1b_message_arg { bool is_number; double number; const char *text; } qa_q1b_message_arg;
typedef struct qa_q1b_message_part {
  const char *text; const qa_q1b_message_arg *args; size_t arg_count;
} qa_q1b_message_part;

/* TS Q1Event subset emitted by this slice (flat). */
typedef struct qa_q1b_event {
  const char *kind;
  qa_q1b_opt_actor actor; qa_q1b_opt_actor player;
  const char *path; const char *text; bool center;
  const qa_q1b_message_part *parts; size_t part_count;
  const char *effect; double amount;
  qa_vec3 origin; qa_vec3 angles; double frame; double color_map; double skin;
  double volume; double attenuation;
  double lightstyle; const char *pattern;
  const char *beam_style; qa_vec3 start; qa_vec3 end;
  double total; double found; const char *id;
  const char *map; double exit_after; double track; double stage;
} qa_q1b_event;

/* TS Q1Monster (entity.monster). mode/species are TS strings. */
typedef struct qa_q1b_monster_state {
  const char *species; const char *mode;
  double frame_index; const double *sequence; size_t sequence_count; double first_frame;
  qa_q1b_opt_actor enemy; qa_q1b_opt_actor old_enemy;
  char path[128];
  double pause_until; double attack_finished; double pain_finished; double search_until;
  bool death_drop; bool refired;
} qa_q1b_monster_state;

typedef struct qa_q1b_game qa_q1b_game;
typedef struct qa_q1b_actor qa_q1b_actor;
typedef struct qa_q1b_player_state qa_q1b_player_state;

/* Bound named/direct callbacks. has=false mirrors TS null. */
typedef struct qa_q1b_action {
  bool has; const char *name; qa_q1b_game *game; qa_q1b_actor *entity;
  void *ctx; void (*fn)(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity);
} qa_q1b_action;
typedef struct qa_q1b_use {
  bool has; const char *name; qa_q1b_game *game; qa_q1b_actor *entity;
  void *ctx;
  void (*fn)(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity,
             qa_q1b_opt_actor other, qa_q1b_opt_actor activator);
} qa_q1b_use;
typedef struct qa_q1b_touch {
  bool has; const char *name; qa_q1b_game *game; qa_q1b_actor *entity;
  void *ctx;
  void (*fn)(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity, qa_actor_id other,
             bool has_normal, qa_vec3 normal, const void *surface);
} qa_q1b_touch;
typedef struct qa_q1b_pain {
  bool has; const char *name; qa_q1b_game *game; qa_q1b_actor *entity;
  void *ctx;
  void (*fn)(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity,
             qa_q1b_opt_actor attacker, double damage);
} qa_q1b_pain;
typedef struct qa_q1b_die {
  bool has; const char *name; qa_q1b_game *game; qa_q1b_actor *entity;
  void *ctx;
  void (*fn)(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity,
             qa_q1b_opt_actor attacker);
} qa_q1b_die;
typedef struct qa_q1b_blocked {
  bool has; const char *name; qa_q1b_game *game; qa_q1b_actor *entity;
  void *ctx;
  void (*fn)(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity, qa_actor_id other);
} qa_q1b_blocked;
void qa_q1b_action_call(const qa_q1b_action *cb);
void qa_q1b_use_call(const qa_q1b_use *cb, qa_q1b_opt_actor other, qa_q1b_opt_actor activator);
void qa_q1b_touch_call(const qa_q1b_touch *cb, qa_actor_id other, bool has_normal, qa_vec3 normal, const void *surface);
void qa_q1b_pain_call(const qa_q1b_pain *cb, qa_q1b_opt_actor attacker, double damage);
void qa_q1b_die_call(const qa_q1b_die *cb, qa_q1b_opt_actor attacker);
void qa_q1b_blocked_call(const qa_q1b_blocked *cb, qa_actor_id other);

/* TS Q1CallbackHandlers. NULL fn = absent handler. */
typedef struct qa_q1b_handlers {
  void *ctx;
  void (*action)(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity);
  void (*use)(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity,
              qa_q1b_opt_actor other, qa_q1b_opt_actor activator);
  void (*touch)(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity, qa_actor_id other,
                bool has_normal, qa_vec3 normal, const void *surface);
  void (*pain)(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity,
               qa_q1b_opt_actor attacker, double damage);
  void (*die)(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity,
              qa_q1b_opt_actor attacker);
  void (*blocked)(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity, qa_actor_id other);
} qa_q1b_handlers;

/* TS Q1CallbackRegistry (game.named). */
typedef struct qa_q1b_named {
  void *ctx;
  void (*register_named)(void *ctx, const char *name, const qa_q1b_handlers *handlers);
  qa_q1b_action (*action)(void *ctx, qa_q1b_actor *entity, const char *name);
  qa_q1b_use (*use)(void *ctx, qa_q1b_actor *entity, const char *name);
  qa_q1b_touch (*touch)(void *ctx, qa_q1b_actor *entity, const char *name);
  qa_q1b_pain (*pain)(void *ctx, qa_q1b_actor *entity, const char *name);
  qa_q1b_die (*die)(void *ctx, qa_q1b_actor *entity, const char *name);
  qa_q1b_blocked (*blocked)(void *ctx, qa_q1b_actor *entity, const char *name);
} qa_q1b_named;

/* TS Q1Actor. String fields are fixed buffers; fields/references are
 * the TS Maps as owned ordered lists. state is the door state string. */
typedef struct qa_q1b_field_entry { char *key; char *value; } qa_q1b_field_entry;
typedef struct qa_q1b_ref_entry { char *key; qa_q1b_opt_actor value; } qa_q1b_ref_entry;
struct qa_q1b_actor {
  qa_owned_actor actor;
  char classname[64];
  bool has_source_ordinal; double source_ordinal;
  char model[128]; double frame; double skin; int32_t effects;
  const char *solid; const char *movement;
  qa_q1b_field_entry *fields; size_t field_count; size_t field_capacity;
  qa_q1b_ref_entry *references; size_t reference_count; size_t reference_capacity;
  char target[128]; char targetname[128]; char killtarget[128]; char message[512];
  double delay; int32_t spawnflags; double sounds; double wait; double speed;
  double damage; double max_health; bool aimed_damage; double next_think;
  qa_q1b_action think; qa_q1b_use use; qa_q1b_touch touch;
  qa_q1b_pain pain; qa_q1b_die die; qa_q1b_blocked blocked;
  bool has_move; qa_vec3 move_destination; qa_q1b_action move_done;
  qa_q1b_opt_actor owner; qa_q1b_opt_actor activator;
  char original_model[128];
  qa_vec3 pos1; qa_vec3 pos2; qa_vec3 dest1; qa_vec3 dest2; qa_vec3 movedir; qa_vec3 mangle;
  const char *state; /* "bottom"|"up"|"top"|"down" */
  qa_q1b_actor **door_group; size_t door_group_count;
  bool has_trigger_bounds; qa_bounds trigger_bounds;
  double attack_finished; double count; bool activated;
  qa_q1b_monster_state *monster; /* borrowed; owned by the controller */
  const char *projectile; const char *projectile_weapon;
  qa_vec3 angular_velocity; double water_level; int water_type;
  int32_t movement_flags; double ideal_yaw; double yaw_speed;
  const char *attack_state;
  qa_q1b_action path_end;
};
void qa_q1b_actor_init(qa_q1b_actor *entity, qa_owned_actor actor, const char *classname);
void qa_q1b_actor_destroy(qa_q1b_actor *entity);
void qa_q1b_fields_set(qa_q1b_actor *entity, const char *key, const char *value);
const char *qa_q1b_fields_get(const qa_q1b_actor *entity, const char *key);
bool qa_q1b_fields_has(const qa_q1b_actor *entity, const char *key);
void qa_q1b_references_set(qa_q1b_actor *entity, const char *key, qa_q1b_opt_actor value);
qa_q1b_opt_actor qa_q1b_references_get(const qa_q1b_actor *entity, const char *key);
const char *qa_q1b_actor_text(const qa_q1b_actor *entity, const char *key);
double qa_q1b_actor_number(const qa_q1b_actor *entity, const char *key);
double qa_q1b_actor_number_or(const qa_q1b_actor *entity, const char *key, double fallback);
qa_vec3 qa_q1b_actor_vector(const qa_q1b_actor *entity, const char *key);
qa_vec3 qa_q1b_parse_vector(const char *text);
bool qa_q1b_actor_damageable(qa_q1b_game *game, const qa_q1b_actor *entity);
void qa_q1b_actor_set_damageable(qa_q1b_game *game, qa_q1b_actor *entity, bool value);
qa_vec3 qa_q1b_move_direction(qa_q1b_game *game, qa_vec3 angles); /* game may be NULL */

/* TS Q1PlayerState. powerups is the TS Map as an owned list. */
typedef struct qa_q1b_powerup_entry { const char *name; double expires; } qa_q1b_powerup_entry;
struct qa_q1b_player_state {
  double alpha; double scale;
  qa_owned_actor actor;
  const char *weapon; /* TS Q1Weapon */
  bool primary_holstered;
  double attack_finished; bool attack_held; bool jump_held; double teleport_until;
  double weapon_frame; double weapon_animation_at; double weapon_animation_base;
  bool continuous_firing; double next_weapon_frame; double lightning_sound_at;
  qa_vec3 punch_angles; double nail_side;
  double max_health; double mega_rot_at; double hostile_until;
  qa_vec3 view_angles; double water_level; double air_finished; double drown_damage;
  double drown_at; double hazard_at;
  const char *auto_switch; /* "always"|"new"|"never" */
  qa_q1b_powerup_entry *powerups; size_t powerup_count; size_t powerup_capacity;
};
double qa_q1b_powerups_get(const qa_q1b_player_state *player, const char *name); /* 0 absent (TS Map.get) */
bool qa_q1b_powerups_has(const qa_q1b_player_state *player, const char *name);
void qa_q1b_powerups_set(qa_q1b_player_state *player, const char *name, double expires);
void qa_q1b_powerups_clear(qa_q1b_player_state *player);

/* TS Q1FoundationOptions subset read by this slice. */
typedef struct qa_q1b_options {
  const char *edition; /* "classic" | "rerelease" */
  const char *precache_program; /* NULL or "id1" */
  int skill; double deathmatch; bool coop;
  const char *campaign;
  bool has_teamplay; double teamplay;
  bool has_no_exit; double no_exit;
} qa_q1b_options;

/* TS Q1FoundationHost. Optional TS methods may be NULL. */
typedef void (*qa_q1b_on_release_fn)(void *ctx, qa_owned_actor actor);
typedef struct qa_q1b_host {
  void *ctx;
  double (*random)(void *ctx);
  qa_q1b_trace (*trace)(void *ctx, const qa_q1b_trace_request *request);
  const char *(*contents)(void *ctx, qa_vec3 point);
  bool (*walk_move)(void *ctx, qa_owned_actor actor, double yaw, double distance);
  void (*change_yaw)(void *ctx, qa_owned_actor actor);
  void (*move_to_goal)(void *ctx, qa_owned_actor actor, qa_actor_id goal, double distance, const char *mode);
  bool (*check_bottom)(void *ctx, qa_actor_id actor);
  void (*schedule_think)(void *ctx, qa_owned_actor actor, double due_seconds);
  void (*cancel_think)(void *ctx, qa_owned_actor actor);
  void (*emit)(void *ctx, const qa_q1b_event *event);
  size_t (*players)(void *ctx, qa_actor_id *out, size_t capacity);
  qa_q1b_opt_actor (*check_client)(void *ctx, qa_owned_actor observer);
  const char *(*classname)(void *ctx, qa_actor_id actor);
  void (*set_gravity)(void *ctx, qa_actor_id actor, double scale);
  void (*powerup)(void *ctx, qa_owned_actor actor, const char *powerup, double expires_seconds);
  double (*powerup_expires)(void *ctx, qa_actor_id actor, const char *powerup);
  double (*source_damage_multiplier)(void *ctx, qa_actor_id attacker);
  bool (*bodies_read)(void *ctx, qa_actor_id actor, qa_q1b_body *out);
  void (*bodies_write)(void *ctx, qa_owned_actor actor, const qa_q1b_body *body);
  void (*bodies_link)(void *ctx, qa_owned_actor actor);
  bool (*combat_read)(void *ctx, qa_actor_id actor, qa_combat_state *out);
  void (*combat_set_health)(void *ctx, qa_owned_actor actor, double health);
  void (*combat_set_armor)(void *ctx, qa_owned_actor actor, qa_armor_state armor);
  void (*combat_set_can_take_damage)(void *ctx, qa_owned_actor actor, bool value);
  bool (*actors_resolve_owned)(void *ctx, qa_actor_id actor, qa_owned_actor *out);
  bool (*actors_resolve_saved)(void *ctx, int64_t slot, int64_t generation, qa_owned_actor *out);
  qa_actor_id (*actors_reference_saved)(void *ctx, int64_t slot, int64_t generation);
  bool (*actors_is_live)(void *ctx, qa_actor_id actor);
  void (*actors_release)(void *ctx, qa_owned_actor actor);
  void (*actors_on_release)(void *ctx, qa_q1b_on_release_fn fn, void *fn_ctx);
  void (*actors_assert_owned)(void *ctx, qa_owned_actor actor);
  double (*inventory_count)(void *ctx, qa_actor_id actor, qa_item_id item);
  void (*inventory_give)(void *ctx, qa_owned_actor actor, qa_item_id item, double count);
  void (*inventory_configure)(void *ctx, qa_owned_actor actor, const qa_inventory_entry *entry);
  size_t (*inventory_entries)(void *ctx, qa_actor_id actor, qa_inventory_entry *out, size_t capacity);
  bool (*inventory_has)(void *ctx, qa_actor_id actor);
  void (*inventory_create)(void *ctx, qa_owned_actor actor, const qa_inventory_entry *entries, size_t count);
} qa_q1b_host;

/* TS MonsterMission surface used by base monsters. */
typedef struct qa_q1b_combat_route {
  bool has; qa_q1b_opt_actor goal; bool stand_ground;
} qa_q1b_combat_route;
typedef struct qa_q1b_monster_mission {
  void *ctx;
  bool ambush;
  void (*found_target)(void *ctx);
  qa_q1b_opt_actor (*route)(void *ctx);
  qa_q1b_combat_route (*combat_route)(void *ctx);
  void (*killed)(void *ctx, qa_q1b_opt_actor enemy);
  void (*spawned)(void *ctx);
  void (*started)(void *ctx);
  bool (*use)(void *ctx, qa_q1b_opt_actor activator);
} qa_q1b_monster_mission;

/* TS Q1PlayerExtension / Q1PickupRules / Q1StateExtension. */
typedef struct qa_q1b_player_extension {
  const char *id; void *ctx;
  void (*attach)(void *ctx, qa_q1b_game *game, qa_q1b_player_state *player);
  /* capture_travel returns owned bytes; NULL = TS undefined. */
  uint8_t *(*capture_travel)(void *ctx, qa_q1b_game *game, qa_q1b_player_state *player, size_t *out_length);
  void (*restore_travel)(void *ctx, qa_q1b_game *game, qa_q1b_player_state *player, const uint8_t *bytes, size_t length);
} qa_q1b_player_extension;
typedef struct qa_q1b_pickup_rules {
  const char *id; void *ctx;
  /* weapon_granted returns Q1Weapon in TS; this slice ignores it. */
  const char *(*weapon_granted)(void *ctx, qa_q1b_game *game, qa_q1b_player_state *player, const char *weapon);
  /* weapon_rank / auto_switch use has_* for TS optional-with-default. */
  bool has_weapon_rank; double (*weapon_rank)(void *ctx, const char *weapon);
  bool has_auto_switch; bool (*auto_switch)(void *ctx, qa_q1b_game *game, qa_q1b_player_state *player, bool was_owned);
} qa_q1b_pickup_rules;
typedef struct qa_q1b_state_extension {
  const char *id; void *ctx;
  uint8_t *(*capture)(void *ctx, size_t *out_length);
  void (*restore)(void *ctx, const uint8_t *bytes, size_t length);
  void (*clone)(void *ctx, qa_q1b_actor *source, qa_q1b_actor *target);
} qa_q1b_state_extension;
typedef void (*qa_q1b_spawn_fn)(void *ctx, qa_q1b_game *game, qa_q1b_actor *entity);

/* TS Q1EntityServices mirror. time assignment must go through
 * qa_q1b_game_set_time (TS setter applies Math.fround). */
struct qa_q1b_game {
  void *ctx;
  const qa_q1b_host *host;
  qa_q1b_named named;
  qa_q1b_options options;
  bool uses_id1_precaches;
  double time; double frame_seconds; double force_retouch;
  char map_name[64];
  qa_q1b_actor *world; qa_q1b_actor *sight_entity; double sight_time;
  double total_secrets; double found_secrets;
  double total_monsters; double killed_monsters; double world_type;
  qa_q1b_basis basis;
  bool has_intermission; char intermission_map[64]; qa_q1b_opt_actor intermission_cause; double intermission_exit_after;
  const qa_q1b_pickup_rules *pickup_rules; /* NULL = TS null */
  const qa_pickup_admission *pickup_admission; /* NULL = TS null */
  const char *(*precache_model)(qa_q1b_game *game, const char *path);
  const char *(*precache_sound)(qa_q1b_game *game, const char *path);
  qa_q1b_body (*body)(qa_q1b_game *game, qa_q1b_actor *entity);
  void (*set_body)(qa_q1b_game *game, qa_q1b_actor *entity, const qa_q1b_body_patch *patch);
  void (*link)(qa_q1b_game *game, qa_q1b_actor *entity);
  void (*set_origin)(qa_q1b_game *game, qa_q1b_actor *entity, qa_vec3 origin);
  void (*set_bounds)(qa_q1b_game *game, qa_q1b_actor *entity, qa_bounds bounds);
  void (*remove)(qa_q1b_game *game, qa_q1b_actor *entity);
  qa_q1b_actor *(*create)(qa_q1b_game *game, const char *classname);
  qa_q1b_actor *(*entity)(qa_q1b_game *game, qa_q1b_opt_actor actor);
  size_t (*find)(qa_q1b_game *game, const char *targetname, qa_q1b_actor **out, size_t capacity);
  size_t (*entities)(qa_q1b_game *game, qa_q1b_actor **out, size_t capacity);
  double (*health)(qa_q1b_game *game, qa_actor_id actor);
  bool (*live)(qa_q1b_game *game, qa_q1b_actor *entity);
  bool (*is_player)(qa_q1b_game *game, qa_q1b_opt_actor actor);
  qa_q1b_player_state *(*player)(qa_q1b_game *game, qa_actor_id actor);
  void (*sound)(qa_q1b_game *game, qa_q1b_actor *entity, const char *path,
                qa_q1b_channel channel, double attenuation, double volume);
  void (*sound_owned)(qa_q1b_game *game, qa_owned_actor actor, const char *path,
                      qa_q1b_channel channel, double attenuation, double volume);
  void (*message)(qa_q1b_game *game, qa_q1b_opt_actor player, const char *text, bool center);
  void (*effect)(qa_q1b_game *game, const char *effect, qa_vec3 origin,
                 qa_q1b_opt_actor actor, double amount);
  void (*schedule)(qa_q1b_game *game, qa_q1b_actor *entity, double delay, qa_q1b_action callback);
  void (*schedule_at)(qa_q1b_game *game, qa_q1b_actor *entity, double due_seconds, qa_q1b_action callback);
  void (*cancel)(qa_q1b_game *game, qa_q1b_actor *entity);
  qa_damage_outcome (*damage)(qa_q1b_game *game, qa_actor_id target, qa_actor_id inflictor,
                              qa_q1b_opt_actor attacker, double amount, const char *weapon,
                              const char *delivery, const char *death_type, const char *armor_effect);
  bool (*can_damage)(qa_q1b_game *game, qa_actor_id target, qa_actor_id inflictor);
  void (*radius_damage)(qa_q1b_game *game, qa_actor_id inflictor, qa_q1b_opt_actor attacker,
                        double amount, qa_q1b_opt_actor ignore, const char *weapon, const char *death_type);
  void (*calc_move)(qa_q1b_game *game, qa_q1b_actor *entity, qa_vec3 destination, double speed, qa_q1b_action done);
  qa_q1b_basis (*make_vectors)(qa_q1b_game *game, qa_vec3 angles);
  void (*register_spawn)(qa_q1b_game *game, const char *classname, qa_q1b_spawn_fn fn, void *fn_ctx);
  void (*register_state_extension)(qa_q1b_game *game, const qa_q1b_state_extension *extension);
  void (*register_player_extension)(qa_q1b_game *game, const qa_q1b_player_extension *extension);
  size_t (*player_extensions)(qa_q1b_game *game, const qa_q1b_player_extension **out, size_t capacity);
  const qa_q1b_player_extension *(*player_extension_get)(qa_q1b_game *game, const char *id);
  size_t (*registered_weapon_keys)(qa_q1b_game *game, const char **out, size_t capacity);
  void (*use_targets)(qa_q1b_game *game, qa_q1b_actor *entity, qa_q1b_opt_actor activator);
  qa_item_id (*weapon_item)(qa_q1b_game *game, const char *weapon);
  qa_item_id (*weapon_ammo)(qa_q1b_game *game, const char *weapon);
  bool (*select_weapon)(qa_q1b_game *game, qa_owned_actor actor, const char *weapon);
  const char *(*choose_best)(qa_q1b_game *game, qa_owned_actor actor);
  void (*spawn_entity)(qa_q1b_game *game, qa_q1b_actor *entity);
  qa_q1b_actor *(*clone_entity)(qa_q1b_game *game, qa_q1b_actor *source);
  void (*travel)(qa_q1b_game *game, const char *map, qa_q1b_opt_actor cause);
  void (*begin_intermission)(qa_q1b_game *game, const char *map, qa_q1b_opt_actor cause);
  double (*powerup_expires)(qa_q1b_game *game, qa_actor_id actor, const char *powerup);
  /* NULL observation = TS null (returns false). */
  bool (*monster_target)(qa_q1b_game *game, qa_actor_id actor, qa_monster_target_observation *out);
  qa_q1b_monster_mission *(*monster_mission)(qa_q1b_game *game, qa_actor_id actor);
};
void qa_q1b_game_set_time(qa_q1b_game *game, double value);
void qa_q1b_game_sound(qa_q1b_game *game, qa_q1b_actor *entity, const char *path);
void qa_q1b_game_sound_ch(qa_q1b_game *game, qa_q1b_actor *entity, const char *path, qa_q1b_channel channel);
void qa_q1b_game_sound_ch_att(qa_q1b_game *game, qa_q1b_actor *entity, const char *path, qa_q1b_channel channel, double attenuation);
void qa_q1b_game_sound_owned(qa_q1b_game *game, qa_owned_actor actor, const char *path);
void qa_q1b_game_sound_owned_ch(qa_q1b_game *game, qa_owned_actor actor, const char *path, qa_q1b_channel channel);
void qa_q1b_game_sound_owned_ch_att(qa_q1b_game *game, qa_owned_actor actor, const char *path, qa_q1b_channel channel, double attenuation);
void qa_q1b_game_damage(qa_q1b_game *game, qa_actor_id target, qa_actor_id inflictor,
                        qa_q1b_opt_actor attacker, double amount);
void qa_q1b_game_message(qa_q1b_game *game, qa_actor_id player, const char *text);
void qa_q1b_game_effect(qa_q1b_game *game, const char *effect, qa_vec3 origin);
void qa_q1b_game_effect_actor(qa_q1b_game *game, const char *effect, qa_vec3 origin, qa_actor_id actor);
void qa_q1b_game_effect_full(qa_q1b_game *game, const char *effect, qa_vec3 origin, qa_q1b_opt_actor actor, double amount);

/* foundation/types.ts helpers used by this slice. */
extern const char *const QA_Q1B_WEAPONS[8]; /* TS WEAPONS */
const char *qa_q1b_weapon_item(const char *weapon, char *buf, size_t size); /* TS weaponItem */
qa_vec3 qa_q1b_normalize(qa_vec3 a);
double qa_q1b_yaw_for(qa_vec3 direction);
double qa_q1b_path_end_time(double time); /* foundation/monsters.ts */
void qa_q1b_door_down(qa_q1b_game *game, qa_q1b_actor *entity); /* foundation/movers.ts */
void qa_q1b_format_number(char *buf, size_t size, double value); /* TS String(number) site form */
void qa_q1b_format_fround(char *buf, size_t size, double value);

#endif


