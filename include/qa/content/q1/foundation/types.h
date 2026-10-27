/*
 * Port of src/content/q1/foundation/types.ts. Q1 gameplay adapted from
 * id Software Quake / rerelease QuakeC. GPL-2.0-or-later.
 * C accommodations: string unions stay exact const char * values
 * (Q1SoundChannel numerics use "-1"/"5"/"6"/"7"); TS Maps become
 * insertion-ordered arrays; Q1FoundationHost's world tables
 * (SessionActorRegistry/SharedBodyTable/GameplayAuthority/
 * SharedInventoryTable, TS world tables) are mirrored as a fn+ctx
 * table qa_q1_host (collision-proof; rebind to landed world
 * concrete tables at integration). Entity bodies/health therefore
 * live on qa_q1_actor until that rebinding.
 */
#ifndef QA_CONTENT_Q1_FOUNDATION_TYPES_H
#define QA_CONTENT_Q1_FOUNDATION_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/contracts/world.h"
#include "qa/core/math.h"

typedef struct qa_q1_actor qa_q1_actor;
typedef struct qa_q1_services qa_q1_services;

typedef const char *qa_q1_weapon;      /* TS Q1Weapon */
typedef const char *qa_q1_base_weapon; /* TS Q1BaseWeapon */
typedef const char *qa_q1_powerup;     /* TS Q1Powerup */
typedef const char *qa_q1_sound_channel;
typedef const char *qa_q1_solid;
typedef const char *qa_q1_move_type;
typedef const char *qa_q1_contents; /* empty|solid|water|slime|lava|sky */

double qa_q1_weapon_bit(qa_q1_base_weapon weapon);
extern const qa_q1_powerup QA_Q1_POWERUP_IDS[12];
extern const qa_q1_base_weapon QA_Q1_WEAPONS[8]; /* TS WEAPONS */
extern const qa_q1_weapon QA_Q1_WEAPON_IDS[20];
extern const qa_provider_id QA_Q1_PROVIDER; /* "q1:official" */
extern const qa_vec3 QA_Q1_ZERO;
extern const qa_bounds QA_Q1_POINT;
extern const qa_bounds QA_Q1_PLAYER_BOUNDS;

bool qa_q1_is_base_weapon(qa_q1_weapon weapon);
/* TS weaponItem: `q1:weapon/${weapon}`; static storage for known ids,
 * caller buffer fallback for foreign ids. */
qa_item_id qa_q1_weapon_item(qa_q1_weapon weapon);
qa_vec3 qa_q1_vadd(qa_vec3 a, qa_vec3 b);
qa_vec3 qa_q1_vsub(qa_vec3 a, qa_vec3 b);
qa_vec3 qa_q1_vscale(qa_vec3 a, double s);
double qa_q1_dot(qa_vec3 a, qa_vec3 b);
double qa_q1_length(qa_vec3 a);
qa_vec3 qa_q1_normalize(qa_vec3 a);
bool qa_q1_overlaps(qa_bounds a, qa_bounds b);
double qa_q1_yaw_for(qa_vec3 direction);

typedef struct qa_q1_basis { qa_vec3 forward, right, up; } qa_q1_basis;
qa_q1_basis qa_q1_vectors(qa_vec3 angles);

typedef struct qa_q1_trace {
  double fraction;
  qa_vec3 end, normal;
  bool has_actor; qa_actor_id actor;
  bool start_solid, all_solid, sky, in_open, in_water;
} qa_q1_trace;
typedef struct qa_q1_trace_request {
  qa_vec3 start, end;
  qa_bounds bounds;
  bool has_ignore; qa_actor_id ignore;
  bool monsters, missile, has_missile;
} qa_q1_trace_request;

/* TS Q1CharacterAttack. variant valid when kind=="axe". */
typedef struct qa_q1_character_attack { const char *kind; double variant; bool has; } qa_q1_character_attack;
/* TS Q1MessagePart arg: string|number. */
typedef struct qa_q1_msg_arg { bool is_number; double number; const char *text; } qa_q1_msg_arg;
typedef struct qa_q1_message_part { const char *text; const qa_q1_msg_arg *args; size_t arg_count; } qa_q1_message_part;

/* TS Q1Event union on kind (exact strings); unused fields zero. */
typedef struct qa_q1_event {
  const char *kind;
  bool has_actor; qa_actor_id actor;
  bool has_player; qa_actor_id player;
  double channel; /* stop-sound numeric channel */
  qa_q1_sound_channel sound_channel;
  const char *path, *text, *effect, *view_model, *pattern, *map, *id;
  bool has_origin; qa_vec3 origin, angles, start, end, direction;
  bool has_view_offset; qa_vec3 view_offset;
  double attenuation, volume, amount, color_start, color_length, frame;
  double color_map, skin, color, count, style, total, found, punch;
  double lock_until, expires, exit_after, track, stage;
  bool center;
  const qa_q1_msg_arg *args; size_t arg_count;
  const qa_q1_message_part *parts; size_t part_count;
  bool has_muzzle; qa_vec3 muzzle_origin, muzzle_angles;
  const char *beam_style;
  qa_q1_weapon weapon;
  qa_q1_powerup powerup;
  qa_q1_character_attack attack;
} qa_q1_event;

typedef struct qa_q1_precache_tables {
  const char *phase; /* loading|frozen */
  const char *const *models; size_t model_count;
  const char *const *sounds; size_t sound_count;
} qa_q1_precache_tables;

/* Mirrored monster target observation (TS src/content/monsters/target.ts). */
typedef struct qa_q1_monster_target_obs {
  double view_height; bool notarget, invisible;
  const double *light_level; const double *hostile_until;
} qa_q1_monster_target_obs;
typedef struct qa_q1_monster_target_result { bool has_value; qa_q1_monster_target_obs value; } qa_q1_monster_target_result;
typedef struct qa_q1_source_target { bool aimed_damage, push, player, slidebox; } qa_q1_source_target;

/* Mirrored Q1FoundationHost (see header note). NULL fn = TS optional absent;
 * required fns may be NULL only for pure-logic tests (services then use
 * actor-local body/health/inventory-lite fallbacks documented in .c). */
typedef struct qa_q1_host {
  void *ctx;
  double (*random)(void *ctx);
  qa_q1_trace (*trace)(void *ctx, qa_q1_trace_request request);
  qa_q1_contents (*contents)(void *ctx, qa_vec3 point);
  bool (*walk_move)(void *ctx, qa_owned_actor actor, double yaw, double distance);
  void (*change_yaw)(void *ctx, qa_owned_actor actor);
  void (*move_to_goal)(void *ctx, qa_owned_actor actor, qa_actor_id goal, double distance, const char *mode);
  bool (*check_bottom)(void *ctx, qa_actor_id actor);
  void (*schedule_think)(void *ctx, qa_owned_actor actor, double due_seconds);
  void (*cancel_think)(void *ctx, qa_owned_actor actor);
  void (*emit)(void *ctx, qa_q1_event event);
  void (*transition)(void *ctx, const char *map, const qa_actor_id *cause);
  const qa_actor_id *(*players)(void *ctx, size_t *out_count);
  qa_q1_monster_target_result (*check_client)(void *ctx, qa_owned_actor observer, qa_actor_id *out);
  const char *(*classname)(void *ctx, qa_actor_id actor);
  void (*powerup)(void *ctx, qa_owned_actor actor, qa_q1_powerup powerup, double expires_seconds);
  /* Optional */
  void (*weapon_impact)(void *ctx, qa_actor_id owner, qa_vec3 origin);
  double (*weapon_volume)(void *ctx, qa_actor_id actor);
  qa_q1_monster_target_result (*monster_target)(void *ctx, qa_actor_id actor);
  void (*set_gravity)(void *ctx, qa_actor_id actor, double scale);
  void (*control_player)(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles, qa_vec3 view_offset);
  bool (*source_target)(void *ctx, qa_actor_id actor, qa_q1_source_target *out);
  double (*powerup_expires)(void *ctx, qa_actor_id actor, qa_q1_powerup powerup);
  double (*source_damage_multiplier)(void *ctx, qa_actor_id attacker);
  bool (*pusher_step)(void *ctx, qa_q1_services *game, qa_q1_actor *entity, double elapsed);
  /* Inventory-lite mirror of SharedInventoryTable subset used here. */
  double (*inventory_count)(void *ctx, qa_actor_id actor, qa_item_id item);
  double (*inventory_give)(void *ctx, qa_owned_actor actor, qa_item_id item, double count);
  bool (*inventory_consume)(void *ctx, qa_owned_actor actor, qa_item_id item, double count);
  bool (*inventory_has)(void *ctx, qa_actor_id actor);
  void (*inventory_configure)(void *ctx, qa_owned_actor actor, qa_item_id item, double count, double capacity);
  /* Combat-lite mirror: damage application; NULL = direct health adjust. */
  double (*apply_damage)(void *ctx, qa_actor_id target, qa_actor_id inflictor,
                         const qa_actor_id *attacker, double amount,
                         qa_q1_weapon weapon, const char *delivery, const char *death_type);
  const qa_source_damage_modifier *source_damage_modifier;
  qa_provider_id source_damage_powerup_owner; /* NULL = absent */
  bool has_punch_angles;
  qa_vec3 (*punch_read)(void *ctx, qa_actor_id actor);
  void (*punch_write)(void *ctx, qa_actor_id actor, qa_vec3 angles);
} qa_q1_host;

typedef struct qa_q1_options {
  qa_provider_id provider; /* NULL -> QA_Q1_PROVIDER */
  bool precache_id1;        /* TS precacheProgram=="id1" */
  const char *edition;      /* classic|rerelease */
  const char *physics_edition; /* NULL -> edition */
  double skill, deathmatch;
  bool coop;
  qa_provider_id campaign, combat_provider, movement_provider, inventory_provider;
  double gravity;
  double max_clients; bool has_max_clients;
  bool foreign_world;
  double no_exit; bool has_no_exit;
  double teamplay; bool has_teamplay;
  double aim_threshold; bool has_aim_threshold;
} qa_q1_options;

typedef struct qa_q1_presentation {
  qa_actor_id actor;
  const char *classname, *model, *targetname;
  double frame, skin, effects;
  qa_q1_solid solid; qa_q1_move_type movement;
  bool has_source_ordinal; double source_ordinal;
} qa_q1_presentation;

typedef struct qa_q1_powerup_entry { qa_q1_powerup kind; double expires; } qa_q1_powerup_entry;
typedef struct qa_q1_player_state {
  double alpha, scale;
  qa_owned_actor actor;
  qa_q1_weapon weapon;
  bool primary_holstered, attack_held, jump_held, continuous_firing;
  double attack_finished, teleport_until, weapon_frame, weapon_animation_at;
  double weapon_animation_base, next_weapon_frame, lightning_sound_at;
  qa_vec3 punch_angles, view_angles;
  double nail_side, max_health, mega_rot_at, hostile_until, water_level;
  double air_finished, drown_damage, drown_at, hazard_at;
  const char *auto_switch; /* always|new|never */
  qa_q1_powerup_entry powerups[16]; size_t powerup_count;
} qa_q1_player_state;

double qa_q1_player_powerup_get(const qa_q1_player_state *p, qa_q1_powerup kind); /* 0 absent, TS Map get ?? 0 sites */
bool qa_q1_player_powerup_has(const qa_q1_player_state *p, qa_q1_powerup kind, double *out);
void qa_q1_player_powerup_set(qa_q1_player_state *p, qa_q1_powerup kind, double expires);
void qa_q1_player_powerup_delete(qa_q1_player_state *p, qa_q1_powerup kind);

#endif
