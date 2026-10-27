/*
 * Port of src/content/q2/foundation/weapons/types.ts.
 * Also carries collision-proof mirrors (qa_q2f_*) of the unported
 * src/content/q2/foundation/host.ts / callbacks.ts / checkpoint.ts /
 * fields.ts surfaces used by the weapons and monsters slices (TS
 * owners noted per block). Plain TS Errors abort via qa_q2f_error
 * printing "Error: <msg>"; RangeErrors use qa_range_error.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q2_FOUNDATION_WEAPONS_TYPES_H
#define QA_CONTENT_Q2_FOUNDATION_WEAPONS_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

#include "qa/content/monsters/target.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/session.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/contracts/world.h"
#include "qa/core/random/q2_rerelease.h"

noreturn void qa_q2f_error(const char *format, ...);

/* ---- host.ts mirrors ---- */
typedef enum qa_q2f_edition { QA_Q2F_CLASSIC, QA_Q2F_RERELEASE } qa_q2f_edition;
typedef struct qa_q2f_game_options {
  qa_q2f_edition edition;
  const char *map_name;
  double skill; /* 0|1|2|3 */
  const char *mode; /* "singleplayer"|"coop"|"deathmatch" */
  double deathmatch_flags;
  double max_clients;
  qa_provider_id provider, campaign, combat_provider, inventory_provider, movement_provider;
  bool has_damage_powerup_owner;
  qa_provider_id damage_powerup_owner;
  const qa_source_damage_modifier *source_damage_modifier; /* optional */
} qa_q2f_game_options;

typedef struct qa_q2f_spawn_fields {
  double ordinal;
  const char *classname;
  const char *const *keys;
  const char *const *values;
  size_t count;
} qa_q2f_spawn_fields;
const char *qa_q2f_spawn_value(const qa_q2f_spawn_fields *fields, const char *key); /* NULL = absent */
/* fields.ts numberField (fallback when absent). */
double qa_q2f_number_field(const qa_q2f_spawn_fields *fields, const char *key, double fallback);

typedef enum qa_q2f_solid { QA_Q2F_SOLID_NONE, QA_Q2F_SOLID_TRIGGER, QA_Q2F_SOLID_BOX, QA_Q2F_SOLID_BRUSH } qa_q2f_solid;
typedef enum qa_q2f_motion {
  QA_Q2F_MOTION_STATIONARY, QA_Q2F_MOTION_PUSH, QA_Q2F_MOTION_STOP, QA_Q2F_MOTION_TOSS,
  QA_Q2F_MOTION_NEW_TOSS, QA_Q2F_MOTION_BOUNCE, QA_Q2F_MOTION_WALL_BOUNCE, QA_Q2F_MOTION_FLY,
  QA_Q2F_MOTION_FLY_MISSILE, QA_Q2F_MOTION_STEP
} qa_q2f_motion_kind;

/* Q2PresentationEvent: only kinds emitted by this slice carry fields. */
typedef enum qa_q2f_event_kind {
  QA_Q2F_EVENT_EFFECT, QA_Q2F_EVENT_SOUND, QA_Q2F_EVENT_MONSTER_MUZZLEFLASH, QA_Q2F_EVENT_ENTITY_EVENT
} qa_q2f_event_kind;
typedef struct qa_q2f_presentation_event {
  qa_q2f_event_kind kind;
  const qa_actor_id *actor; /* nullable */
  qa_vec3 origin, direction;
  const char *effect; /* effect */
  double count, color;
  const char *path; /* sound */
  double channel, volume, attenuation;
  bool reliable;
  const char *loop; /* "start"|"stop"|"once" */
  double flash;     /* monster-muzzleflash */
  double event;     /* entity-event */
} qa_q2f_presentation_event;

typedef struct qa_q2f_trace_request {
  qa_vec3 start, end;
  const qa_bounds *bounds; /* nullable */
  const qa_actor_id *ignore; /* nullable */
  double mask;
  const qa_actor_id *exclude;
  size_t exclude_count;
} qa_q2f_trace_request;

typedef struct qa_q2f_weapon_target {
  qa_q2f_solid solid;
  bool laser_immune, damageable_target, bfg_explobox;
} qa_q2f_weapon_target;
typedef struct qa_q2f_weapon_target_result { bool has_value; qa_q2f_weapon_target value; } qa_q2f_weapon_target_result;
typedef struct qa_q2f_target_obs_result { bool has_value; qa_monster_target_observation value; } qa_q2f_target_obs_result;
typedef struct qa_q2f_owned_result { bool has_value; qa_owned_actor value; } qa_q2f_owned_result;
typedef struct qa_q2f_saved_result { bool has_value; qa_saved_actor_id value; } qa_q2f_saved_result;

struct qa_q2f_entity;
struct qa_q2f_game;
typedef struct qa_q2f_think { void *ctx; void (*fn)(void *ctx, struct qa_q2f_entity *entity, struct qa_q2f_game *game); } qa_q2f_think;
typedef struct qa_q2f_use { void *ctx; void (*fn)(void *ctx, struct qa_q2f_entity *entity, struct qa_q2f_game *game, const qa_actor_id *other, const qa_actor_id *activator); } qa_q2f_use;
typedef struct qa_q2f_touch { void *ctx; void (*fn)(void *ctx, struct qa_q2f_entity *entity, struct qa_q2f_game *game, qa_touch_contact contact); } qa_q2f_touch;
typedef struct qa_q2f_pain { void *ctx; void (*fn)(void *ctx, struct qa_q2f_entity *entity, struct qa_q2f_game *game, qa_pain_reaction reaction); } qa_q2f_pain;
typedef struct qa_q2f_die { void *ctx; void (*fn)(void *ctx, struct qa_q2f_entity *entity, struct qa_q2f_game *game, qa_death_reaction reaction); } qa_q2f_die;
typedef struct qa_q2f_blocked { void *ctx; void (*fn)(void *ctx, struct qa_q2f_entity *entity, struct qa_q2f_game *game, qa_actor_id other); } qa_q2f_blocked;
bool qa_q2f_think_eq(qa_q2f_think a, qa_q2f_think b);

/* callbacks.ts Q2CallbackDefinitions (records as name arrays). */
typedef struct qa_q2f_named_think { const char *name; qa_q2f_think cb; } qa_q2f_named_think;
typedef struct qa_q2f_named_use { const char *name; qa_q2f_use cb; } qa_q2f_named_use;
typedef struct qa_q2f_named_touch { const char *name; qa_q2f_touch cb; } qa_q2f_named_touch;
typedef struct qa_q2f_named_pain { const char *name; qa_q2f_pain cb; } qa_q2f_named_pain;
typedef struct qa_q2f_named_die { const char *name; qa_q2f_die cb; } qa_q2f_named_die;
typedef struct qa_q2f_named_blocked { const char *name; qa_q2f_blocked cb; } qa_q2f_named_blocked;
typedef struct qa_q2f_trajectory_def {
  qa_q2f_touch touch;
  void *ctx;
  void (*project)(void *ctx, struct qa_q2f_entity *entity, qa_weapon_trajectory_update update);
} qa_q2f_trajectory_def;
typedef struct qa_q2f_callback_definitions {
  const qa_q2f_trajectory_def *trajectory; size_t trajectory_count;
  const qa_q2f_named_think *think; size_t think_count;
  const qa_q2f_named_use *use; size_t use_count;
  const qa_q2f_named_touch *touch; size_t touch_count;
  const qa_q2f_named_pain *pain; size_t pain_count;
  const qa_q2f_named_die *die; size_t die_count;
  const qa_q2f_named_blocked *blocked; size_t blocked_count;
} qa_q2f_callback_definitions;
/* callbacks.ts freeQ2Entity. */
qa_q2f_think qa_q2f_free_entity_think(void);
qa_q2f_die qa_q2f_free_entity_die(void);

/* Q2SourceCallbacks surface used here. think_resolve: NULL name -> no
 * value; unknown non-NULL name aborts in TS. */
typedef struct qa_q2f_think_result { bool has_value; qa_q2f_think value; } qa_q2f_think_result;
typedef struct qa_q2f_source_callbacks {
  void *ctx;
  void (*register_defs)(void *ctx, qa_q2f_callback_definitions definitions);
  qa_q2f_think_result (*think_resolve)(void *ctx, const char *name);
} qa_q2f_source_callbacks;

/* SessionActorRegistry surface (world/actors). */
typedef struct qa_q2f_actors {
  void *ctx;
  bool (*is_live)(void *ctx, qa_actor_id actor);
  qa_q2f_owned_result (*resolve_saved)(void *ctx, qa_saved_actor_id actor);
  qa_actor_id (*reference_saved)(void *ctx, qa_saved_actor_id actor);
  qa_q2f_owned_result (*resolve_owned)(void *ctx, qa_actor_id actor);
  void (*assert_owned)(void *ctx, qa_owned_actor actor);
  qa_q2f_saved_result (*source_of)(void *ctx, qa_actor_id actor);
  void (*on_release)(void *ctx, void *cb_ctx, void (*cb)(void *cb_ctx, qa_owned_actor actor));
} qa_q2f_actors;
/* SharedBodyTable surface (world/actors). */
typedef struct qa_q2f_bodies {
  void *ctx;
  qa_body_state_result (*read)(void *ctx, qa_actor_id actor);
  void (*write)(void *ctx, qa_owned_actor actor, qa_body_state state);
  qa_linked_body_result (*linked)(void *ctx, qa_actor_id actor);
  void (*link)(void *ctx, qa_owned_actor actor);
} qa_q2f_bodies;
/* GameplayAuthority surface (world/gameplay/authority.ts). */
typedef struct qa_q2f_combat_traits {
  bool has_mass; double mass;
  bool has_can_take_damage; bool can_take_damage;
  bool has_invulnerable; bool invulnerable;
} qa_q2f_combat_traits;
typedef struct qa_q2f_combat {
  void *ctx;
  qa_combat_state_result (*read)(void *ctx, qa_actor_id actor);
  void (*create)(void *ctx, qa_owned_actor actor, qa_combat_state state);
  void (*set_health)(void *ctx, qa_owned_actor actor, double health);
  void (*set_armor)(void *ctx, qa_owned_actor actor, qa_armor_state armor);
  void (*set_traits)(void *ctx, qa_owned_actor actor, qa_q2f_combat_traits traits);
} qa_q2f_combat;
/* SharedInventoryTable surface (world/gameplay/inventory.ts). */
typedef struct qa_q2f_inventory {
  void *ctx;
  const qa_inventory_entry *(*entries)(void *ctx, qa_actor_id actor, size_t *out_count);
  double (*count)(void *ctx, qa_actor_id actor, qa_item_id item);
  bool (*consume)(void *ctx, qa_owned_actor actor, qa_item_id item, double count);
  void (*configure)(void *ctx, qa_owned_actor actor, qa_inventory_entry entry);
} qa_q2f_inventory;
/* WeaponBehaviorProjectilePort.launch surface (contracts). */
typedef struct qa_q2f_weapon_behavior {
  void *ctx;
  qa_weapon_trajectory_update_result (*launch)(void *ctx, qa_weapon_behavior_launch input);
} qa_q2f_weapon_behavior;

typedef struct qa_q2f_host {
  void *ctx;
  qa_q2f_actors actors;
  qa_q2f_bodies bodies;
  qa_q2f_combat combat;
  qa_q2f_inventory inventory;
  bool has_weapon_behavior;
  qa_q2f_weapon_behavior weapon_behavior;
  qa_q2f_weapon_target_result (*weapon_target)(void *ctx, qa_actor_id actor); /* optional, may be NULL */
  qa_q2_rerelease_random *rerelease_random; /* NULL = absent */
  double (*now)(void *ctx);
  double (*frame_seconds)(void *ctx);
  double (*random)(void *ctx);
  void (*touch_triggers)(void *ctx, qa_owned_actor actor);
  qa_trace_result (*trace)(void *ctx, qa_q2f_trace_request request);
  double (*point_contents)(void *ctx, qa_vec3 point);
  bool (*in_phs)(void *ctx, qa_vec3 first, qa_vec3 second);
  bool (*areas_connected)(void *ctx, qa_vec3 first, qa_vec3 second);
  const qa_actor_id *(*nearby)(void *ctx, qa_vec3 origin, double radius, size_t *out_count);
  const qa_actor_id *(*players)(void *ctx, size_t *out_count);
  qa_actor_id (*world_actor)(void *ctx);
  bool (*is_player)(void *ctx, qa_actor_id actor);
  bool (*is_monster)(void *ctx, qa_actor_id actor);
  void (*emit)(void *ctx, qa_q2f_presentation_event event);
  void (*diagnostic)(void *ctx, const char *message);
} qa_q2f_host;

/* Q2Entity (host.ts). Strings borrowed; spawn by value. */
typedef struct qa_q2f_entity {
  qa_owned_actor actor;
  qa_q2f_spawn_fields spawn;
  const char *classname, *target, *targetname, *killtarget, *combat_target, *death_target;
  const char *health_target, *item_target, *message, *model, *model2, *model3, *model4;
  const char *noise, *sound_text, *map;
  double spawnflags, delay, wait, speed, accel, decel, damage, damage_radius, radius_damage;
  double count, max_health, view_height, frame, old_frame, scale, alpha, skin, effects;
  double render_flags, flags, server_flags, light_level, power_cubes, timestamp, volume;
  double attenuation, random_value, style, gravity;
  bool transition_started, projectile, dodgeable, laser_immune, damageable_target, visible;
  double clip_mask;
  const qa_attack_provenance *last_attack; /* nullable, borrowed */
  qa_q2f_solid solid;
  qa_q2f_motion_kind motion;
  qa_vec3 gravity_vector, angular_velocity, movedir, pos1, pos2;
  bool has_activator, has_enemy, has_owner, has_goal, has_team_master, has_team_chain, has_chain, has_beam, has_beam2, has_proboscus;
  qa_actor_id activator, enemy, owner, goal, team_master, team_chain, chain, beam, beam2, proboscus;
  bool has_next_think;
  double next_think;
  qa_q2f_think think, prethink, postthink;
  qa_q2f_use use;
  qa_q2f_touch touch;
  qa_q2f_pain pain;
  qa_q2f_die die;
  qa_q2f_blocked blocked;
} qa_q2f_entity;
/* TS Q2Entity constructor defaults. */
void qa_q2f_entity_init(qa_q2f_entity *entity, qa_owned_actor actor, qa_q2f_spawn_fields spawn);

/* Partial<BodyState> for game.move. */
typedef struct qa_q2f_body_changes {
  bool has_origin; qa_vec3 origin;
  bool has_angles; qa_vec3 angles;
  bool has_velocity; qa_vec3 velocity;
  bool has_bounds; qa_bounds bounds;
  bool has_ground; const qa_actor_id *ground; /* NULL ground = null */
} qa_q2f_body_changes;

typedef struct qa_q2f_damage_args {
  qa_actor_id target;
  qa_q2f_entity *inflictor_entity; /* non-NULL = Q2Entity inflictor */
  qa_actor_id inflictor_actor;
  const qa_actor_id *attacker; /* nullable */
  double amount, knockback;
  qa_vec3 direction, point, normal;
  double means_of_death;
  bool has_flags; double flags;
  qa_item_id weapon; /* nullable */
} qa_q2f_damage_args;
typedef struct qa_q2f_radius_damage_args {
  qa_q2f_entity *inflictor;
  const qa_actor_id *attacker;
  double damage;
  const qa_actor_id *ignore;
  double radius, means_of_death;
  bool has_damage_flags; double damage_flags;
  qa_item_id weapon;
} qa_q2f_radius_damage_args;

typedef struct qa_q2f_counters {
  double total_secrets, found_secrets, total_goals, found_goals, total_monsters, killed_monsters, server_flags;
} qa_q2f_counters;

typedef struct qa_q2f_game {
  void *ctx;
  qa_q2f_host *host;
  qa_q2f_source_callbacks source_callbacks;
  qa_q2f_game_options options;
  qa_q2f_entity *const *entities;
  size_t entity_count;
  bool has_current_actor;
  qa_actor_id current_actor;
  qa_q2f_counters counters;
  qa_q2f_entity *(*spawn)(void *ctx, qa_q2f_spawn_fields fields);
  qa_q2f_entity *(*create)(void *ctx, const char *classname, const qa_q2f_spawn_fields *values);
  void (*remove)(void *ctx, qa_q2f_entity *entity);
  qa_q2f_entity *(*entity)(void *ctx, const qa_actor_id *actor);
  qa_q2f_weapon_target_result (*weapon_target)(void *ctx, qa_actor_id actor);
  qa_body_state (*body)(void *ctx, qa_owned_actor actor);
  void (*move)(void *ctx, qa_q2f_entity *entity, qa_q2f_body_changes changes, bool has_link, bool link);
  void (*project_trajectory)(void *ctx, qa_q2f_entity *entity, qa_weapon_trajectory_update update);
  void (*link)(void *ctx, qa_q2f_entity *entity);
  void (*solid)(void *ctx, qa_q2f_entity *entity, qa_q2f_solid solid);
  void (*motion)(void *ctx, qa_q2f_entity *entity, qa_q2f_motion_kind kind);
  void (*schedule)(void *ctx, qa_q2f_entity *entity, double delay_seconds, qa_q2f_think think);
  void (*cancel)(void *ctx, qa_q2f_entity *entity);
  void (*use_targets)(void *ctx, qa_q2f_entity *entity, const qa_actor_id *activator, bool has_ignore_delay, bool ignore_delay);
  qa_q2f_target_obs_result (*monster_target)(void *ctx, const qa_actor_id *actor);
  qa_q2f_entity *const *(*targets)(void *ctx, const char *name, size_t *out_count);
  qa_q2f_entity *(*pick_target)(void *ctx, const char *name);
  void (*show)(void *ctx, qa_q2f_entity *entity);
  void (*sound)(void *ctx, qa_owned_actor actor, const char *path, bool has_channel, double channel,
                bool has_volume, double volume, bool has_attenuation, double attenuation);
  qa_damage_outcome (*damage)(void *ctx, qa_q2f_damage_args args);
  void (*radius_damage)(void *ctx, qa_q2f_radius_damage_args args);
  bool (*can_damage)(void *ctx, qa_actor_id target, qa_owned_actor inflictor);
} qa_q2f_game;

/* checkpoint.ts saveQ2Actor / restoreQ2Actor mirrors. */
bool qa_q2f_save_actor(const qa_actor_id *actor, qa_saved_actor_id *out); /* false = null */
qa_owned_actor qa_q2f_restore_actor(qa_q2f_game *game, qa_saved_actor_id actor);

/* ---- weapons/types.ts ---- */
typedef struct qa_q2w_owner { qa_owned_actor actor; double view_height; } qa_q2w_owner;
typedef const char *qa_q2w_name; /* Q2WeaponName / Q2BaseWeaponName */
typedef enum qa_q2w_phase { QA_Q2W_ACTIVATING, QA_Q2W_READY, QA_Q2W_FIRING, QA_Q2W_DROPPING } qa_q2w_phase;
const char *qa_q2w_phase_name(qa_q2w_phase phase);

typedef struct qa_q2w_definition {
  qa_q2w_name name;
  qa_item_id item;
  const char *classname;
  qa_item_id ammo; /* nullable */
  double quantity, warning;
  const char *view_model, *world_model;
  double player_model, activate_last, fire_last, idle_last, deactivate_last;
  const double *pauses; size_t pause_count;
  const double *fires; size_t fire_count;
  bool repeating;
} qa_q2w_definition;
typedef qa_q2w_definition qa_q2w_base_definition;

typedef enum qa_q2w_event_kind {
  QA_Q2W_EVENT_MUZZLEFLASH, QA_Q2W_EVENT_BEAM, QA_Q2W_EVENT_VIEW_WEAPON,
  QA_Q2W_EVENT_PLAYER_ANIMATION, QA_Q2W_EVENT_INVISIBILITY_REVEAL
} qa_q2w_event_kind;
typedef struct qa_q2w_event {
  qa_q2w_event_kind kind;
  const qa_actor_id *actor; /* nullable (beam) */
  double flash; bool silenced;                 /* muzzleflash */
  const char *effect; qa_vec3 start, end; double duration; /* beam */
  qa_q2w_name weapon; const char *model; double player_model, frame, skin, rate; /* view-weapon */
  qa_vec3 kick_origin, kick_angles;
  const char *priority; double first, last; bool reset_time; /* player-animation */
  double until;                                 /* invisibility-reveal */
} qa_q2w_event;

typedef struct qa_q2w_hooks {
  void *ctx;
  double (*quad_multiplier)(void *ctx, qa_actor_id actor);          /* optional */
  double (*source_damage_multiplier)(void *ctx, qa_actor_id actor); /* optional */
  double (*firing_interval)(void *ctx, qa_actor_id actor, double seconds); /* optional */
  void (*emit)(void *ctx, qa_q2w_event event);
  void (*noise)(void *ctx, qa_actor_id actor, qa_vec3 origin, bool secondary);
  void (*dodge)(void *ctx, qa_actor_id monster, qa_actor_id attacker, double eta_seconds, qa_trace_result trace);
  bool lag_is_history;
  /* history begin(); returned token passed to lag_end (TS closure). */
  void *(*lag_begin)(void *ctx, qa_actor_id actor, qa_vec3 start, qa_vec3 direction);
  void (*lag_end)(void *ctx, void *token);
  void (*ammo_changed)(void *ctx, qa_actor_id actor, qa_item_id ammo);
  bool (*can_target)(void *ctx, const qa_actor_id *attacker, qa_actor_id target);
} qa_q2w_hooks;

typedef enum qa_q2w_hand { QA_Q2W_HAND_RIGHT, QA_Q2W_HAND_LEFT, QA_Q2W_HAND_CENTER } qa_q2w_hand;
typedef struct qa_q2w_input {
  bool attack, latched_attack, holster;
  qa_vec3 angles;
  bool ducked, spectator, notarget;
  qa_q2w_hand hand;
  bool animate_player;
  double quad_until, double_until, quad_fire_until;
  bool haste, no_stack_double, instant_switch, quick_switch, infinite_ammo, players_collide;
  double gravity;
  bool weapon_thunk;
} qa_q2w_input;

typedef enum qa_q2w_hand_reservation { QA_Q2W_RES_NONE, QA_Q2W_RES_FINITE, QA_Q2W_RES_INFINITE } qa_q2w_hand_reservation;
typedef enum qa_q2w_handoff { QA_Q2W_HANDOFF_ACTIVE, QA_Q2W_HANDOFF_HOLSTERING, QA_Q2W_HANDOFF_HOLSTERED } qa_q2w_handoff;

typedef struct qa_q2w_state {
  qa_q2w_handoff primary_handoff;
  qa_q2w_name weapon, last_weapon, pending; /* nullable */
  qa_q2w_phase phase;
  double frame, think_time, fire_finished;
  bool fire_buffered, latched_attack;
  double machinegun_shots, empty_sound_time;
  qa_q2w_hand_reservation hand_reservation;
  double grenade_time, grenade_finished;
  bool grenade_blew_up;
  qa_vec3 kick_origin, kick_angles;
  double kick_time, kick_until, kick_duration;
  const char *loop_sound;
  const char *view_model; /* nullable */
  double view_skin, last_firing_time;
  bool source_firing;
  double gun_rate;
} qa_q2w_state;
/* TS constructor(weapon = "blaster"); pass NULL name for null weapon
 * via qa_q2w_state_init_null. Default-name form: qa_q2w_state_init. */
void qa_q2w_state_init(qa_q2w_state *state, qa_q2w_name weapon);
void qa_q2w_state_init_default(qa_q2w_state *state);

typedef struct qa_q2w_noise_record { qa_actor_id actor; qa_vec3 origin; double time; bool secondary; } qa_q2w_noise_record;
typedef struct qa_q2w_grenade_adjustment { double right, up, gravity; } qa_q2w_grenade_adjustment;

/* TS MOD record as constants. */
enum {
  QA_Q2W_MOD_BLASTER = 1, QA_Q2W_MOD_SHOTGUN = 2, QA_Q2W_MOD_SUPERSHOTGUN = 3,
  QA_Q2W_MOD_MACHINEGUN = 4, QA_Q2W_MOD_CHAINGUN = 5, QA_Q2W_MOD_GRENADE = 6,
  QA_Q2W_MOD_GRENADE_SPLASH = 7, QA_Q2W_MOD_ROCKET = 8, QA_Q2W_MOD_ROCKET_SPLASH = 9,
  QA_Q2W_MOD_HYPERBLASTER = 10, QA_Q2W_MOD_RAILGUN = 11, QA_Q2W_MOD_BFG_LASER = 12,
  QA_Q2W_MOD_BFG_BLAST = 13, QA_Q2W_MOD_BFG_EFFECT = 14, QA_Q2W_MOD_HAND_GRENADE = 15,
  QA_Q2W_MOD_HAND_GRENADE_SPLASH = 16, QA_Q2W_MOD_HELD_GRENADE = 24, QA_Q2W_MOD_HIT = 32
};
#define QA_Q2W_SHOT_MASK ((double)(1 | 2 | 0x2000000 | 0x4000000))
#define QA_Q2W_PLAYER_CONTENTS ((double)0x40000000)
#define QA_Q2W_PROJECTILE_MASK ((double)((1 | 2 | 0x2000000 | 0x4000000) | 0x40000000 | 0x4000))
#define QA_Q2W_WATER_MASK ((double)(8 | 16 | 32))

#endif
