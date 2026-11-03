/*
 * Port of src/content/q2/foundation/host.ts: Q2 gameplay provider
 * boundary. Adapted from id Software's Quake II game and rerelease
 * game DLL. GPL-2.0-or-later.
 * C accommodations: TS interfaces become fn tables + ctx; TS string
 * fields on Q2Entity are borrowed const char * (spawn values are owned
 * by the embedded spawn fields; literals are static); nullable ActorId
 * fields use has_* flags; callbacks are fn ptr + ctx pairs (NULL fn is
 * TS null); Map iteration becomes the services entities() snapshot.
 */
#ifndef QA_CONTENT_Q2_FOUNDATION_HOST_H
#define QA_CONTENT_Q2_FOUNDATION_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/monsters/authored.h"
#include "qa/content/monsters/target.h"
#include "qa/contracts/flare.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/session.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/contracts/world.h"
#include "qa/core/random/q2_rerelease.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"

typedef struct qa_q2_entity qa_q2_entity;
typedef struct qa_q2_game_services qa_q2_game_services;
typedef struct qa_q2_source_callbacks qa_q2_source_callbacks;
typedef struct qa_q2_entity_services qa_q2_entity_services;

/* TS Q2Edition: "classic" | "rerelease". */
typedef enum qa_q2_edition { QA_Q2_EDITION_CLASSIC, QA_Q2_EDITION_RERELEASE } qa_q2_edition;
/* TS mode: "singleplayer" | "coop" | "deathmatch". */
typedef enum qa_q2_mode { QA_Q2_MODE_SINGLEPLAYER, QA_Q2_MODE_COOP, QA_Q2_MODE_DEATHMATCH } qa_q2_mode;

typedef struct qa_q2_game_options {
  qa_q2_edition edition;
  const char *map_name;
  double skill; /* 0|1|2|3 */
  qa_q2_mode mode;
  double deathmatch_flags;
  /* TS instagib?: rerelease g_instagib, every pickup is removed and players
   * start with a railgun and 99 slugs instead of the blaster (false = TS undefined). */
  bool instagib;
  double max_clients;
  qa_provider_id provider;
  bool has_damage_powerup_owner;
  qa_provider_id damage_powerup_owner;
  const qa_source_damage_modifier *source_damage_modifier; /* optional */
  qa_provider_id campaign;
  qa_provider_id combat_provider;
  qa_provider_id inventory_provider;
  qa_provider_id movement_provider;
  /* TS playerName?(actor): NULL fn = TS undefined; a NULL return is TS null. */
  void *player_name_ctx;
  const char *(*player_name)(void *ctx, qa_actor_id actor);
  /* TS options objects may define deathmatchFlags as a getter
   * (composition/q2/index.ts). When set, every read goes through it;
   * read with qa_q2_options_deathmatch_flags. */
  void *deathmatch_flags_ctx;
  double (*deathmatch_flags_read)(void *ctx);
} qa_q2_game_options;
/* TS options.deathmatchFlags (the getter when one is installed). */
static inline double qa_q2_options_deathmatch_flags(const qa_q2_game_options *options) {
  return options->deathmatch_flags_read != NULL ? options->deathmatch_flags_read(options->deathmatch_flags_ctx)
                                                : options->deathmatch_flags;
}

/** TS Q2SpawnFields. values are owned pairs (insertion order). */
typedef struct qa_q2_field_pair { char *key; char *value; } qa_q2_field_pair;
typedef struct qa_q2_spawn_fields {
  double ordinal;
  const char *classname; /* borrowed from values or static */
  qa_q2_field_pair *values;
  size_t value_count;
  size_t value_capacity;
} qa_q2_spawn_fields;

const char *qa_q2_spawn_get(const qa_q2_spawn_fields *fields, const char *key);
bool qa_q2_spawn_has(const qa_q2_spawn_fields *fields, const char *key);
/* False only on OOM. Copies key/value; later keys replace. */
bool qa_q2_spawn_set(qa_q2_spawn_fields *fields, const char *key, const char *value);
void qa_q2_spawn_fields_free(qa_q2_spawn_fields *fields);
bool qa_q2_spawn_fields_clone(qa_q2_spawn_fields *out, const qa_q2_spawn_fields *in);

/* TS Q2TraceRequest. bounds NULL = TS null; ignore NULL = TS null. */
typedef struct qa_q2_trace_request {
  qa_vec3 start, end;
  const qa_bounds *bounds;
  const qa_actor_id *ignore;
  double mask;
  const qa_actor_id *exclude;
  size_t exclude_count;
} qa_q2_trace_request;

/* TS Q2Motion kind strings kept exact. */
typedef struct qa_q2_motion {
  qa_owned_actor actor;
  qa_vec3 velocity, angular_velocity;
  const char *kind;
  double gravity;
  qa_vec3 gravity_vector;
  double clip_mask;
  bool has_owner;
  qa_actor_id owner;
  /* Key order of the TS literal that produced it, kept by the physics
   * motion table's `{ ...motion }` copies: false {actor, velocity,
   * angularVelocity, kind, ...} (actorMotion, runtime), true {actor, kind,
   * velocity, angularVelocity, ...} (Q2 entity-services motion). */
  bool kind_first;
} qa_q2_motion;

typedef struct qa_q2_landmark_carry {
  qa_actor_id player;
  const char *name;
  qa_vec3 relative_origin, relative_velocity, relative_view_angles;
} qa_q2_landmark_carry;

/** TS Q2WeaponTarget. solid: "none"|"trigger"|"box"|"brush". */
typedef struct qa_q2_weapon_target {
  const char *solid;
  bool laser_immune, damageable_target, bfg_explobox;
} qa_q2_weapon_target;
typedef struct qa_q2_weapon_target_result { bool has_value; qa_q2_weapon_target value; } qa_q2_weapon_target_result;
typedef struct qa_q2_monster_target_result { bool has_value; qa_monster_target_observation value; } qa_q2_monster_target_result;

/** TS Q2PresentationEvent union on kind (exact strings). Unused fields zero. */
typedef struct qa_q2_presentation_event {
  const char *kind;
  bool has_actor; qa_actor_id actor;
  const char *path;               /* model/sound */
  const char *attached_models[3]; /* model */
  double frame, old_frame, scale, alpha, skin, effects, render_flags;
  bool visible;                   /* visibility/dynamic-light/beam */
  qa_vec3 origin;                 /* sound/effect/poi/damage-indicator/dynamic-light */
  double channel, volume, attenuation;
  bool reliable;
  const char *loop;               /* "start"|"stop"|"once" */
  bool has_loop_owner; qa_provider_id loop_owner;
  bool local;                     /* sound: gi.local_sound, heard only by its actor (TS local?: true) */
  const char *text;               /* centerprint/print/help */
  bool has_instant; bool instant;
  bool has_duration_seconds; double duration_seconds;
  const char *level;              /* print: "low"|"medium"|"high"|"chat" */
  double slot;                    /* help: 1|2 */
  double style; const char *pattern; /* lightstyle */
  const char *track;              /* music */
  const char *effect;             /* effect/monster-beam */
  qa_vec3 direction; double count, color;
  double amount;                  /* damage-indicator */
  qa_actor_id player; qa_item_id item; const char *icon, *name; /* pickup */
  const char *message;            /* poi */
  const qa_q2_spawn_fields *fields; /* poi */
  qa_vec3 light_color; double radius, intensity, resolution, fade_start, fade_end, lightstyle; /* dynamic-light */
  bool has_cone; qa_vec3 cone_direction; double cone_cos_half_angle;
  qa_vec3 start, end; double width; /* beam/monster-beam (color shared) */
  double flash;                   /* monster-muzzleflash */
  double event;                   /* entity-event */
} qa_q2_presentation_event;

typedef struct qa_q2_player_view_state { qa_vec3 view_angles, old_velocity; } qa_q2_player_view_state;
typedef struct qa_q2_player_view_result { bool has_value; qa_q2_player_view_state value; } qa_q2_player_view_result;

/** TS Q2FoundationHost. Optional TS members may be NULL. */
typedef struct qa_q2_foundation_host {
  void *ctx;
  const qa_original_pickup_admission *original_pickups;
  const qa_weapon_behavior_projectile_port *weapon_behavior;
  qa_q2_weapon_target_result (*weapon_target)(void *ctx, qa_actor_id actor);
  qa_q2_monster_target_result (*monster_target)(void *ctx, qa_actor_id actor);
  void (*register_entity)(void *ctx, qa_q2_entity *entity, qa_q2_entity_services *services);
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_actor_callback_table *callbacks;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  double (*now)(void *ctx);
  double (*frame_seconds)(void *ctx);
  double (*gravity)(void *ctx);
  double (*random)(void *ctx);
  qa_q2_rerelease_random *rerelease_random; /* optional */
  void (*schedule)(void *ctx, qa_owned_actor actor, const double *due_seconds);
  void (*touch_triggers)(void *ctx, qa_owned_actor actor);
  qa_trace_result (*trace)(void *ctx, qa_q2_trace_request request);
  double (*point_contents)(void *ctx, qa_vec3 point);
  bool (*in_pvs)(void *ctx, qa_vec3 first, qa_vec3 second);
  bool (*in_phs)(void *ctx, qa_vec3 first, qa_vec3 second);
  bool (*areas_connected)(void *ctx, qa_vec3 first, qa_vec3 second);
  const qa_actor_id *(*nearby)(void *ctx, qa_vec3 origin, double radius, size_t *out_count);
  const qa_actor_id *(*players)(void *ctx, size_t *out_count);
  qa_actor_id (*world_actor)(void *ctx);
  bool (*is_player)(void *ctx, qa_actor_id actor);
  bool (*is_monster)(void *ctx, qa_actor_id actor);
  qa_bounds (*inline_model_bounds)(void *ctx, double model);
  void (*set_solid)(void *ctx, qa_owned_actor actor, const char *solid, const double *model);
  void (*set_motion)(void *ctx, qa_q2_motion motion);
  void (*set_area_portal)(void *ctx, double portal, bool open);
  void (*emit)(void *ctx, qa_q2_presentation_event event);
  qa_q2_player_view_result (*player_view_state)(void *ctx, qa_actor_id player);
  void (*key_consumed)(void *ctx, qa_actor_id player);
  void (*prepare_level_change)(void *ctx, const char *map, const qa_q2_landmark_carry *landmark, double server_flags);
  void (*transition)(void *ctx, qa_transition_intent intent);
  void (*diagnostic)(void *ctx, const char *message);
} qa_q2_foundation_host;

/* TS callbacks: fn ptr + ctx. NULL fn is TS null. */
typedef struct qa_q2_think { void *ctx; void (*fn)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g); } qa_q2_think;
typedef struct qa_q2_use { void *ctx; void (*fn)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, const qa_actor_id *other, const qa_actor_id *activator); } qa_q2_use;
typedef struct qa_q2_touch { void *ctx; void (*fn)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact); } qa_q2_touch;
typedef struct qa_q2_pain { void *ctx; void (*fn)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_pain_reaction reaction); } qa_q2_pain;
typedef struct qa_q2_die { void *ctx; void (*fn)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction reaction); } qa_q2_die;
typedef struct qa_q2_blocked { void *ctx; void (*fn)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id other); } qa_q2_blocked;

bool qa_q2_think_eq(qa_q2_think a, qa_q2_think b);
bool qa_q2_use_eq(qa_q2_use a, qa_q2_use b);
bool qa_q2_touch_eq(qa_q2_touch a, qa_q2_touch b);
bool qa_q2_pain_eq(qa_q2_pain a, qa_q2_pain b);
bool qa_q2_die_eq(qa_q2_die a, qa_q2_die b);
bool qa_q2_blocked_eq(qa_q2_blocked a, qa_q2_blocked b);

/** TS Q2Entity. String fields borrowed; spawn owned. */
struct qa_q2_entity {
  qa_owned_actor actor;
  qa_q2_spawn_fields spawn;
  const char *classname, *target, *targetname, *killtarget, *combat_target;
  const char *death_target, *health_target, *item_target, *message, *model;
  const char *model2, *model3, *model4;
  double spawnflags, delay, wait, speed, accel, decel, damage, damage_radius;
  double radius_damage, count, max_health, view_height, frame, old_frame;
  double scale, alpha, skin, effects, render_flags, flags, server_flags;
  double light_level, power_cubes, timestamp;
  const char *noise, *sound;
  double volume, attenuation, random;
  const char *map;
  double style;
  bool transition_started;
  bool item_dropped; /* TS itemDropped */
  double clip_mask;
  bool projectile, dodgeable, laser_immune, damageable_target;
  const qa_attack_provenance *last_attack; /* nullable, borrowed */
  bool visible;
  const char *solid;  /* "none"|"trigger"|"box"|"brush" */
  const char *motion; /* Q2Motion kind */
  double gravity;
  qa_vec3 gravity_vector, angular_velocity, movedir, pos1, pos2;
  bool has_activator, has_enemy, has_owner, has_goal, has_team_master;
  bool has_team_chain, has_chain, has_beam, has_beam2, has_proboscus;
  qa_actor_id activator, enemy, owner, goal, team_master, team_chain, chain, beam, beam2, proboscus;
  bool has_next_think; double next_think;
  qa_q2_think think, prethink, postthink;
  qa_q2_use use;
  qa_q2_touch touch;
  qa_q2_pain pain;
  qa_q2_die die;
  qa_q2_blocked blocked;
};

/* TS Q2Entity constructor. Takes ownership of spawn's pairs. */
void qa_q2_entity_init(qa_q2_entity *entity, qa_owned_actor actor, qa_q2_spawn_fields spawn);
void qa_q2_entity_destroy(qa_q2_entity *entity);
/* TS flare getter. False is TS null; out->rim_color points to scratch. */
bool qa_q2_entity_flare(const qa_q2_entity *entity, qa_scene_flare *out, qa_vec3 *rim_scratch);

/** TS Partial<BodyState> for game.move(). */
typedef struct qa_q2_body_changes {
  bool has_origin, has_angles, has_velocity, has_bounds, has_ground;
  qa_vec3 origin, angles, velocity;
  qa_bounds bounds;
  bool ground_null; qa_actor_id ground;
} qa_q2_body_changes;

typedef struct qa_q2_counters {
  double total_secrets, found_secrets, total_goals, found_goals;
  double total_monsters, killed_monsters, server_flags;
} qa_q2_counters;

/* TS AuthoredTarget view of an entity for useTargets (AuthoredTarget is
 * structurally shared; C passes the landed qa_authored_target built by
 * the caller or converted by qa_q2_entity_authored_target). */
qa_authored_target qa_q2_entity_authored_target(qa_q2_entity *entity);

/** TS Q2GameServices. */
struct qa_q2_game_services {
  void *ctx;
  qa_q2_foundation_host *host;
  qa_q2_source_callbacks *source_callbacks;
  qa_q2_game_options options;
  qa_q2_counters *counters;
  bool has_current_actor;
  qa_actor_id current_actor;
  /* TS helpChanged: game.helpchanged counts use_target_help firings. */
  double help_changed;
  /* TS disguiseViolation: level.disguise_violator and its expiry, a
   * disguised player who fired is the next target of every monster.
   * has_disguise_violation false = TS null. */
  bool has_disguise_violation;
  qa_actor_id disguise_violation_actor;
  double disguise_violation_until;
  qa_q2_entity *const *(*entities)(void *ctx, size_t *out_count);
  /* TS markDodgeable: registers a projectile whose projectile and dodgeable
   * flags are set, for monster dodge checks. */
  void (*mark_dodgeable)(void *ctx, qa_q2_entity *entity);
  /* TS dodgeables(): live dodgeable projectiles in registration order; stale
   * members are dropped as they are found. The array is borrowed until the
   * next mark or entity release. */
  qa_q2_entity *const *(*dodgeables)(void *ctx, size_t *out_count);
  qa_q2_entity *(*spawn)(void *ctx, qa_q2_spawn_fields fields);
  qa_q2_entity *(*create)(void *ctx, const char *classname, const qa_q2_field_pair *values, size_t value_count);
  void (*remove)(void *ctx, qa_q2_entity *entity);
  qa_q2_entity *(*entity)(void *ctx, const qa_actor_id *actor);
  qa_q2_weapon_target_result (*weapon_target)(void *ctx, qa_actor_id actor);
  const char *(*item_name)(void *ctx, const char *classname);
  const qa_owned_actor *(*push_team)(void *ctx, qa_actor_id actor, size_t *out_count);
  qa_body_state (*body)(void *ctx, qa_owned_actor actor);
  void (*move)(void *ctx, qa_q2_entity *entity, qa_q2_body_changes changes, bool link);
  void (*project_trajectory)(void *ctx, qa_q2_entity *entity, qa_weapon_trajectory_update update);
  void (*link)(void *ctx, qa_q2_entity *entity);
  void (*solid)(void *ctx, qa_q2_entity *entity, const char *solid);
  void (*motion)(void *ctx, qa_q2_entity *entity, const char *kind);
  void (*schedule)(void *ctx, qa_q2_entity *entity, double delay_seconds, qa_q2_think think);
  void (*cancel)(void *ctx, qa_q2_entity *entity);
  void (*use_targets)(void *ctx, qa_authored_target entity, const qa_actor_id *activator, bool ignore_delay);
  qa_q2_monster_target_result (*monster_target)(void *ctx, const qa_actor_id *actor);
  qa_q2_entity *const *(*targets)(void *ctx, const char *name, size_t *out_count);
  qa_q2_entity *(*pick_target)(void *ctx, const char *name);
  void (*show)(void *ctx, qa_q2_entity *entity);
  void (*sound)(void *ctx, qa_owned_actor actor, const char *path, double channel, double volume, double attenuation);
  /* inflictor: entity when inflictor_entity non-NULL else inflictor_id. */
  qa_damage_outcome (*damage)(void *ctx, qa_actor_id target, qa_q2_entity *inflictor_entity,
      qa_actor_id inflictor_id, const qa_actor_id *attacker, double amount, double knockback,
      qa_vec3 direction, qa_vec3 point, qa_vec3 normal, double means_of_death, double flags, qa_item_id weapon);
  void (*radius_damage)(void *ctx, qa_q2_entity *inflictor, const qa_actor_id *attacker, double damage,
      const qa_actor_id *ignore, double radius, double means_of_death, double damage_flags, qa_item_id weapon);
  bool (*can_damage)(void *ctx, qa_actor_id target, qa_owned_actor inflictor);
  qa_attack_provenance (*attack)(void *ctx, qa_q2_entity *inflictor_entity, qa_actor_id inflictor_id,
      const qa_actor_id *attacker, double means_of_death, double flags, qa_item_id weapon);
};

/** TS Q2SpawnModule. callbacks may be NULL; item_name may be NULL. */
typedef struct qa_q2_spawn_module {
  void *ctx;
  const struct qa_q2_callback_definitions *callbacks;
  const char *(*item_name)(void *ctx, const char *classname);
  bool (*spawn)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
} qa_q2_spawn_module;

/* Convenience wrappers mirroring the TS method call shapes. */
qa_body_state qa_q2_game_body(qa_q2_game_services *g, qa_q2_entity *e);
void qa_q2_game_move(qa_q2_game_services *g, qa_q2_entity *e, qa_q2_body_changes c, bool link);
qa_q2_entity *qa_q2_game_entity_of(qa_q2_game_services *g, const qa_actor_id *a);
void qa_q2_game_use_targets_entity(qa_q2_game_services *g, qa_q2_entity *e, const qa_actor_id *activator, bool ignore_delay);
/* TS game.markDodgeable(entity). Test doubles that leave mark_dodgeable NULL skip it. */
void qa_q2_game_mark_dodgeable(qa_q2_game_services *g, qa_q2_entity *e);
/* TS game.entities is a live map: a loop that spawns or removes reads the
 * current size and slot on every step, as these do. */
static inline size_t qa_q2_game_entity_count(qa_q2_game_services *g) {
  size_t n = 0;
  (void)g->entities(g->ctx, &n);
  return n;
}
static inline qa_q2_entity *qa_q2_game_entity_at(qa_q2_game_services *g, size_t index) {
  size_t n = 0;
  return g->entities(g->ctx, &n)[index];
}

#endif
