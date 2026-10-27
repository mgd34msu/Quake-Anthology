/*
 * Port of src/content/q1/addons/context.ts.
 * Mirrored types from unported modules (collision-proof qa_q1ad_ names;
 * replace when owners land):
 *  - qa_q1ad_actor: Q1Actor, src/content/q1/foundation/entity.ts
 *    (fields the addon slice reads/writes; `fields` map as a fixed
 *    key/value list; named callbacks stored by registered name).
 *  - qa_q1ad_player: Q1PlayerState, src/content/q1/foundation/types.ts.
 *  - qa_q1ad_game: Q1EntityServices/Q1Base surface used by this slice
 *    (src/content/q1/foundation/entity-services.ts, base/provider.ts),
 *    as fn+ctx callbacks. NULL callbacks are no-ops/zero, matching the
 *    optional host services the TS guards.
 *  - qa_q1ad_travel: Q1TravelState, src/content/q1/base/travel.ts.
 * Checkpoint capture/restore uses landed qa/persistence/value.h.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q1_ADDONS_CONTEXT_H
#define QA_CONTENT_Q1_ADDONS_CONTEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/core/math.h"

typedef enum qa_q1ad_program {
  QA_Q1AD_DOPA, QA_Q1AD_MG1, QA_Q1AD_MG3, QA_Q1AD_CTF
} qa_q1ad_program;
const char *qa_q1ad_program_name(qa_q1ad_program program);

typedef enum qa_q1ad_event_kind {
  QA_Q1AD_EVENT_MUSIC, QA_Q1AD_EVENT_SELL_SCREEN, QA_Q1AD_EVENT_ALPHA,
  QA_Q1AD_EVENT_RUNE_COLLECTED, QA_Q1AD_EVENT_CUTSCENE, QA_Q1AD_EVENT_FOG,
  QA_Q1AD_EVENT_PUNCH_ANGLE, QA_Q1AD_EVENT_VIEW_ROLL, QA_Q1AD_EVENT_LIGHTNING,
  QA_Q1AD_EVENT_COLORED_EXPLOSION, QA_Q1AD_EVENT_MONSTER_COUNT,
  QA_Q1AD_EVENT_DEVELOPER_MESSAGE, QA_Q1AD_EVENT_ACTOR_EFFECTS,
  QA_Q1AD_EVENT_DEBUG_BOUNDS
} qa_q1ad_event_kind;

typedef struct qa_q1ad_event {
  qa_q1ad_event_kind kind;
  double track, loop_track, alpha, bits, density, sky_factor, duration, roll;
  double color_start, color_length, count, effects, color, lifetime;
  qa_actor_id actor;
  bool has_actor, has_player, depth_test;
  qa_actor_id player;
  qa_q1ad_program program;
  int style;
  qa_vec3 camera, angles, color_vec, start, end, origin, min, max;
  const char *text;
} qa_q1ad_event;

typedef struct qa_q1ad_services {
  void *ctx;
  bool (*cheat_arsenal)(void *ctx, qa_actor_id actor, const char *category);
  bool has_cheat_arsenal;
  void (*emit)(void *ctx, const qa_q1ad_event *event);
  bool (*is_monster)(void *ctx, qa_actor_id actor);
  double (*cvar)(void *ctx, const char *name);
  void (*set_cvar)(void *ctx, const char *name, const char *value);
} qa_q1ad_services;

/* TS BodyState subset (contracts/world.ts). */
typedef struct qa_q1ad_body {
  qa_vec3 origin, angles, velocity;
  qa_bounds bounds;
  qa_actor_id ground;
  bool has_ground;
} qa_q1ad_body;

/* Partial<BodyState> patch for setBody. */
typedef struct qa_q1ad_body_patch {
  bool has_origin, has_angles, has_velocity, has_bounds, has_ground;
  qa_vec3 origin, angles, velocity;
  qa_bounds bounds;
  qa_actor_id ground;
  bool ground_null;
} qa_q1ad_body_patch;

typedef struct qa_q1ad_trace {
  double fraction;
  qa_vec3 end;
  qa_actor_id actor;
  bool has_actor, start_solid, all_solid;
} qa_q1ad_trace;

/* Mirrored Q1PlayerState (foundation/types.ts), fields used here. */
#define QA_Q1AD_POWERUPS 8
typedef struct qa_q1ad_player {
  qa_owned_actor actor;
  const char *weapon; /* exact TS Q1Weapon string */
  const char *auto_switch; /* "always"|"new"|"never"|... */
  double max_health, attack_finished, weapon_frame, weapon_animation_at;
  double next_weapon_frame, hostile_until, water_level;
  int weapon_animation_base;
  bool continuous_firing, attack_held;
  qa_vec3 view_angles;
  const char *powerup_names[QA_Q1AD_POWERUPS];
  double powerup_expires[QA_Q1AD_POWERUPS];
  size_t powerup_count;
} qa_q1ad_player;
double qa_q1ad_player_powerup(const qa_q1ad_player *player, const char *kind);
void qa_q1ad_player_set_powerup(qa_q1ad_player *player, const char *kind, double expires);

#define QA_Q1AD_ACTOR_FIELDS 24
#define QA_Q1AD_ACTOR_REFS 8
#define QA_Q1AD_NAME 96
typedef struct qa_q1ad_actor {
  qa_owned_actor actor;
  const char *classname, *target, *targetname, *killtarget, *model;
  const char *original_model, *message, *solid, *movement, *state;
  double spawnflags, wait, delay, damage, speed, count, frame, max_health;
  double attack_finished, sounds, skin, effects, next_think, yaw_speed;
  int64_t movement_flags;
  bool activated, damageable, aimed_damage;
  bool has_monster;              /* entity.monster !== null */
  const char *monster_species;   /* entity.monster.species or NULL */
  qa_actor_id monster_enemy;
  bool monster_has_enemy;
  qa_vec3 movedir, pos1, pos2, dest, dest2, mangle, angular_velocity;
  qa_bounds trigger_bounds;
  bool has_trigger_bounds;
  qa_actor_id owner, activator;
  bool has_owner, has_activator;
  /* Registered named callbacks; empty string means TS null. */
  char touch[QA_Q1AD_NAME], use[QA_Q1AD_NAME], think[QA_Q1AD_NAME];
  char die[QA_Q1AD_NAME], pain[QA_Q1AD_NAME], blocked[QA_Q1AD_NAME];
  struct qa_q1ad_actor **door_group;
  size_t door_group_count;
  const char *field_keys[QA_Q1AD_ACTOR_FIELDS];
  char field_values[QA_Q1AD_ACTOR_FIELDS][160];
  size_t field_count;
  const char *ref_keys[QA_Q1AD_ACTOR_REFS];
  qa_actor_id ref_values[QA_Q1AD_ACTOR_REFS];
  bool ref_has[QA_Q1AD_ACTOR_REFS];
  size_t ref_count;
} qa_q1ad_actor;

const char *qa_q1ad_actor_text(const qa_q1ad_actor *entity, const char *name);
bool qa_q1ad_actor_has(const qa_q1ad_actor *entity, const char *name);
void qa_q1ad_actor_set(qa_q1ad_actor *entity, const char *name, const char *value);
double qa_q1ad_actor_number(const qa_q1ad_actor *entity, const char *name);
double qa_q1ad_actor_number_or(const qa_q1ad_actor *entity, const char *name, double fallback);
qa_vec3 qa_q1ad_actor_vector(const qa_q1ad_actor *entity, const char *name);
bool qa_q1ad_actor_ref(const qa_q1ad_actor *entity, const char *name, qa_actor_id *out);
void qa_q1ad_actor_set_ref(qa_q1ad_actor *entity, const char *name, const qa_actor_id *target);

/* game.host.emit event kinds used by this slice (Q1Event subset). */
typedef struct qa_q1ad_host_event {
  const char *kind; /* exact TS kind string */
  qa_actor_id actor, player;
  bool has_actor, has_player, player_null;
  double style, volume, attenuation, color, count, frame, punch;
  const char *text, *path, *pattern, *channel, *weapon, *view_model;
  qa_vec3 origin, direction;
} qa_q1ad_host_event;

typedef struct qa_q1ad_msg_arg {
  bool is_number;
  double number;
  const char *text;
} qa_q1ad_msg_arg;

typedef struct qa_q1ad_inventory_entry {
  qa_item_id item;
  double count, capacity;
} qa_q1ad_inventory_entry;

/* Combat armor regular slot (contracts/gameplay.ts subset).
 * kind: "none"|"q1"|"q2"|"q3"|"source". */
typedef struct qa_q1ad_armor {
  const char *kind;
  double points, absorption, normal_protection, protection;
  qa_item_id item;
} qa_q1ad_armor;

/* touchQ1Pickup closures (foundation/pickups.ts). */
typedef struct qa_q1ad_pickup_ops {
  void *ctx;
  bool (*original)(void *ctx);
  void (*complete)(void *ctx, bool accepted);
} qa_q1ad_pickup_ops;

/* Level finale result (base levelRules.advanceFinale). */
typedef struct qa_q1ad_finale {
  const char *kind; /* "finale"|"sell-screen"|other */
  const char *text;
  double track;
} qa_q1ad_finale;

/* ordinaryAddonMonster controller (addons/monsters/, not this slice). */
typedef struct qa_q1ad_monster_info {
  bool present;
  const char *model;
  qa_bounds bounds;
  bool has_enemy;
  qa_actor_id enemy;
} qa_q1ad_monster_info;

typedef struct qa_q1ad_context qa_q1ad_context;

typedef struct qa_q1ad_game {
  void *ctx;
  /* Scalar host state mirroring game fields/options. */
  double time, world_type;
  const char *map_name;
  bool coop, intermission, registered;
  bool has_official_campaign, official_campaign;
  double deathmatch, skill, no_exit;
  double total_monsters, killed_monsters, total_secrets, force_retouch;
  /* Entity lifecycle / lookup. */
  void (*remove)(void *ctx, qa_q1ad_actor *entity);
  bool (*live)(void *ctx, qa_q1ad_actor *entity);
  qa_q1ad_actor *(*create)(void *ctx, const char *classname);
  void (*spawn_entity)(void *ctx, qa_q1ad_actor *entity);
  qa_q1ad_actor *(*entity_by_id)(void *ctx, qa_actor_id actor);
  qa_q1ad_actor *(*world)(void *ctx);
  size_t (*entities)(void *ctx, qa_q1ad_actor **out, size_t capacity);
  size_t (*find)(void *ctx, const char *targetname, qa_q1ad_actor **out, size_t capacity);
  size_t (*players)(void *ctx, qa_actor_id *out, size_t capacity);
  size_t (*observations)(void *ctx, qa_actor_id *out, size_t capacity);
  bool (*resolve_owned)(void *ctx, qa_actor_id actor, qa_owned_actor *out);
  bool (*resolve_saved)(void *ctx, int64_t slot, int64_t generation, qa_owned_actor *out);
  bool (*reference_saved)(void *ctx, int64_t slot, int64_t generation, qa_actor_id *out);
  bool (*is_live)(void *ctx, qa_actor_id actor);
  bool (*is_player)(void *ctx, qa_actor_id actor);
  qa_q1ad_player *(*player)(void *ctx, qa_actor_id actor);
  double (*health)(void *ctx, qa_actor_id actor);
  const char *(*classname_of)(void *ctx, qa_actor_id actor);
  /* Bodies. */
  bool (*body)(void *ctx, qa_q1ad_actor *entity, qa_q1ad_body *out);
  bool (*body_of)(void *ctx, qa_actor_id actor, qa_q1ad_body *out);
  void (*set_body)(void *ctx, qa_q1ad_actor *entity, const qa_q1ad_body_patch *patch);
  void (*write_body)(void *ctx, qa_owned_actor actor, const qa_q1ad_body *body);
  void (*link_body)(void *ctx, qa_owned_actor actor);
  void (*set_origin)(void *ctx, qa_q1ad_actor *entity, qa_vec3 origin);
  void (*set_bounds)(void *ctx, qa_q1ad_actor *entity, qa_bounds bounds);
  void (*link)(void *ctx, qa_q1ad_actor *entity);
  bool (*trace)(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds,
                const qa_actor_id *ignore, bool monsters, qa_q1ad_trace *out);
  double (*random)(void *ctx);
  void (*walk_move)(void *ctx, qa_owned_actor actor, double yaw, double distance);
  /* Effects / messaging / damage. */
  void (*sound)(void *ctx, qa_actor_id actor, const char *path, const char *channel);
  void (*effect)(void *ctx, const char *kind, qa_vec3 origin, const qa_actor_id *actor, double count);
  void (*message)(void *ctx, qa_actor_id player, const char *key, bool localized,
                  const qa_q1ad_msg_arg *args, size_t arg_count);
  void (*host_emit)(void *ctx, const qa_q1ad_host_event *event);
  void (*damage)(void *ctx, qa_actor_id target, const qa_actor_id *inflictor,
                 const qa_actor_id *attacker, double amount, const char *weapon,
                 const char *mode, const char *cause);
  void (*radius_damage)(void *ctx, qa_actor_id inflictor, const qa_actor_id *attacker,
                        double amount, const qa_actor_id *ignore, const char *weapon);
  bool (*can_damage)(void *ctx, qa_actor_id target, qa_actor_id owner);
  void (*use_targets)(void *ctx, qa_q1ad_actor *entity, const qa_actor_id *activator);
  void (*host_touch)(void *ctx, qa_owned_actor self, qa_actor_id other);
  /* Scheduling by registered name. */
  void (*schedule)(void *ctx, qa_q1ad_actor *entity, double delay, const char *name);
  void (*schedule_at)(void *ctx, qa_q1ad_actor *entity, double time, const char *name);
  void (*cancel)(void *ctx, qa_q1ad_actor *entity);
  /* Foreign (foundation-owned) named callbacks + registration notices. */
  void (*register_named)(void *ctx, const char *name);
  void (*register_spawn)(void *ctx, const char *classname, bool replace);
  void (*register_rule)(void *ctx, const char *id);
  void (*call_action)(void *ctx, qa_q1ad_actor *entity, const char *name);
  void (*call_touch)(void *ctx, qa_q1ad_actor *entity, const char *name,
                     const qa_actor_id *other, const qa_vec3 *normal);
  void (*call_use)(void *ctx, qa_q1ad_actor *entity, const char *name,
                   const qa_actor_id *other, const qa_actor_id *activator);
  void (*call_die)(void *ctx, qa_q1ad_actor *entity, const char *name, const qa_actor_id *attacker);
  /* Inventory / combat hosts. */
  double (*inventory_count)(void *ctx, qa_actor_id actor, qa_item_id item);
  void (*inventory_give)(void *ctx, qa_owned_actor actor, qa_item_id item, double amount);
  bool (*inventory_consume)(void *ctx, qa_owned_actor actor, qa_item_id item, double amount);
  void (*inventory_configure)(void *ctx, qa_owned_actor actor, qa_item_id item, double count, double capacity);
  size_t (*inventory_entries)(void *ctx, qa_actor_id actor, qa_q1ad_inventory_entry *out, size_t capacity);
  bool (*combat_can_take_damage)(void *ctx, qa_actor_id actor, bool *out);
  void (*combat_set_health)(void *ctx, qa_owned_actor actor, double health);
  const char *(*combat_team)(void *ctx, qa_actor_id actor);
  void (*combat_set_team)(void *ctx, qa_owned_actor actor, const char *team);
  void (*combat_set_can_take_damage)(void *ctx, qa_owned_actor actor, bool value);
  void (*combat_set_invulnerable)(void *ctx, qa_owned_actor actor, bool value);
  bool (*combat_armor)(void *ctx, qa_actor_id actor, qa_q1ad_armor *out);
  void (*combat_set_regular_armor)(void *ctx, qa_owned_actor actor, const qa_q1ad_armor *armor);
  void (*combat_set_armor)(void *ctx, qa_owned_actor actor, const qa_q1ad_armor *regular);
  void (*combat_set_regular_points)(void *ctx, qa_owned_actor actor, double points);
  void (*combat_set_team_q1monsters)(void *ctx, qa_owned_actor actor);
  void (*powerup)(void *ctx, qa_owned_actor actor, const char *kind, double expires);
  void (*give_powerup)(void *ctx, qa_q1ad_player *player, const char *kind, double seconds);
  /* Weapons / precache / aim. */
  const char *(*precache_model)(void *ctx, const char *path);
  const char *(*precache_sound)(void *ctx, const char *path);
  bool (*select_weapon)(void *ctx, qa_owned_actor actor, const char *weapon);
  const char *(*choose_best)(void *ctx, qa_owned_actor actor);
  const char *(*weapon_ammo)(void *ctx, const char *weapon);
  const char *(*weapon_model)(void *ctx, const char *weapon, qa_q1ad_player *player);
  double (*weapon_frame_delay)(void *ctx, qa_q1ad_player *player, double delay);
  double (*weapon_attack_delay)(void *ctx, qa_q1ad_player *player, double delay);
  qa_vec3 (*aim)(void *ctx, qa_owned_actor actor, qa_vec3 forward);
  bool (*make_vectors)(void *ctx, qa_vec3 angles, qa_angle_vectors *out);
  bool (*fire_base_weapon)(void *ctx, qa_q1ad_player *player);
  void (*fire_bullets)(void *ctx, qa_owned_actor actor, qa_vec3 direction, qa_vec3 angles,
                       double count, double spread_x, double spread_y, const char *weapon);
  void (*launch_laser)(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 direction, bool alternate);
  void (*spawn_hammer_base)(void *ctx, qa_q1ad_player *player, qa_vec3 origin, const char *weapon);
  /* Campaign / level rules / travel (Q1Base). */
  double (*campaign_read_flags)(void *ctx);
  void (*campaign_write_flags)(void *ctx, double flags);
  void (*campaign_set_skill)(void *ctx, double skill);
  void (*travel)(void *ctx, const char *map, const qa_actor_id *cause);
  void (*level_begin)(void *ctx, const char *map, const qa_actor_id *cause);
  void (*level_changelevel_touched)(void *ctx, qa_q1ad_actor *entity, qa_actor_id player);
  void (*level_travel_to)(void *ctx, const char *map, qa_actor_id player);
  bool (*level_advance_finale)(void *ctx, double time, qa_q1ad_finale *out);
  void (*player_exited)(void *ctx, qa_actor_id player);
  bool has_player_exited, has_same_level;
  bool (*same_level)(void *ctx);
  qa_q1ad_actor *(*spawn_select)(void *ctx, bool coop);
  /* Foundation/base spawn helpers. */
  void (*make_static)(void *ctx, qa_q1ad_actor *entity);
  void (*spawn_map_actor)(void *ctx, qa_q1ad_actor *entity);
  void (*spawn_remaining_map_actor)(void *ctx, qa_q1ad_actor *entity);
  void (*spawn_button)(void *ctx, qa_q1ad_actor *entity);
  void (*spawn_door)(void *ctx, qa_q1ad_actor *entity);
  void (*door_up)(void *ctx, qa_q1ad_actor *entity);
  void (*door_down)(void *ctx, qa_q1ad_actor *entity);
  void (*spawn_pickup)(void *ctx, qa_q1ad_actor *entity);
  void (*spawn_teleport_fog)(void *ctx, qa_vec3 origin);
  void (*spawn_teledeath)(void *ctx, qa_vec3 origin, qa_actor_id owner);
  void (*touch_pickup)(void *ctx, qa_q1ad_actor *entity, qa_actor_id other,
                        qa_item_id item, const qa_q1ad_pickup_ops *ops);
  /* Pickup admission (game.pickupAdmission, optional). */
  bool has_pickup_admission;
  bool (*admission_maps)(void *ctx, const char *category, qa_item_id item);
  bool (*admission_owns)(void *ctx, qa_actor_id actor, qa_item_id item);
  bool (*admission_weapon)(void *ctx, qa_owned_actor actor, qa_item_id item, const char *selection);
  /* Cross-slice monster hooks (addons/monsters/, not this slice). */
  bool (*waiting_monster)(void *ctx, qa_q1ad_actor *entity);
  bool (*monster_controller)(void *ctx, qa_q1ad_actor *entity, qa_q1ad_monster_info *out);
  void (*monster_play_stand)(void *ctx, qa_q1ad_actor *entity);
  void (*monster_found)(void *ctx, qa_q1ad_actor *entity, qa_actor_id enemy);
  void (*monster_set_enemy)(void *ctx, qa_q1ad_actor *entity, const qa_actor_id *enemy);
} qa_q1ad_game;

/* Named handler registry mirroring game.named.register. Each slot may
 * be NULL (TS registers only some of action/touch/use/die/pain/blocked). */
typedef void (*qa_q1ad_action_fn)(qa_q1ad_context *context, qa_q1ad_actor *entity);
typedef void (*qa_q1ad_touch_fn)(qa_q1ad_context *context, qa_q1ad_actor *entity,
                                 const qa_actor_id *other, const qa_vec3 *normal);
typedef void (*qa_q1ad_use_fn)(qa_q1ad_context *context, qa_q1ad_actor *entity,
                               const qa_actor_id *other, const qa_actor_id *activator);
typedef void (*qa_q1ad_die_fn)(qa_q1ad_context *context, qa_q1ad_actor *entity,
                               const qa_actor_id *attacker);
typedef void (*qa_q1ad_pain_fn)(qa_q1ad_context *context, qa_q1ad_actor *entity);
typedef void (*qa_q1ad_blocked_fn)(qa_q1ad_context *context, qa_q1ad_actor *entity,
                                   qa_actor_id other);
typedef void (*qa_q1ad_spawn_fn)(qa_q1ad_context *context, qa_q1ad_actor *entity);

typedef struct qa_q1ad_handlers {
  qa_q1ad_action_fn action;
  qa_q1ad_touch_fn touch;
  qa_q1ad_use_fn use;
  qa_q1ad_die_fn die;
  qa_q1ad_pain_fn pain;
  qa_q1ad_blocked_fn blocked;
} qa_q1ad_handlers;

struct qa_q1ad_context {
  qa_q1ad_program program;
  qa_q1ad_services services;
  qa_q1ad_game game;
  double frame_time;
  struct qa_q1ad_word { qa_owned_actor owner; char *name; double value; } *words;
  size_t word_count, word_capacity;
  struct qa_q1ad_ref { qa_owned_actor owner; char *name; qa_actor_id target; bool has_target; } *refs;
  size_t ref_count, ref_capacity;
  qa_q1ad_actor **ticks;
  size_t tick_count, tick_capacity;
  struct qa_q1ad_named { char *name; qa_q1ad_handlers handlers; } *named;
  size_t named_count, named_capacity;
  struct qa_q1ad_spawn_reg { char *classname; bool replace; qa_q1ad_spawn_fn fn; } *spawns;
  size_t spawn_count, spawn_capacity;
  void *slice; /* CtfState/Q1Horde back-pointer for registry handlers. */
};

bool qa_q1ad_context_init(qa_q1ad_context *context, qa_q1ad_program program,
                          qa_q1ad_services services, qa_q1ad_game game);
void qa_q1ad_context_destroy(qa_q1ad_context *context);
/* TS host onRelease handler. */
void qa_q1ad_context_release(qa_q1ad_context *context, qa_owned_actor actor);
/* Checkpoint extension capture/restore (bytes malloc'd; NULL on OOM). */
uint8_t *qa_q1ad_context_capture(const qa_q1ad_context *context, size_t *out_length);
void qa_q1ad_context_restore(qa_q1ad_context *context, const uint8_t *bytes, size_t length);

double qa_q1ad_player_number(qa_q1ad_context *context, qa_actor_id actor, const char *name);
bool qa_q1ad_player_word(qa_q1ad_context *context, qa_actor_id actor, const char *name, double *out);
void qa_q1ad_set_player_number(qa_q1ad_context *context, qa_actor_id actor, const char *name, double value);
bool qa_q1ad_player_reference(qa_q1ad_context *context, qa_actor_id actor, const char *name, qa_actor_id *out);
void qa_q1ad_set_player_reference(qa_q1ad_context *context, qa_actor_id actor, const char *name,
                                  const qa_actor_id *target);
void qa_q1ad_set_number(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name, double value);
void qa_q1ad_set_vector(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name, qa_vec3 value);
void qa_q1ad_alpha(qa_q1ad_context *context, qa_q1ad_actor *entity, double alpha);
void qa_q1ad_broadcast(qa_q1ad_context *context, const char *text);
void qa_q1ad_init_trigger(qa_q1ad_context *context, qa_q1ad_actor *entity);
bool qa_q1ad_removed_outside_coop(qa_q1ad_context *context, qa_q1ad_actor *entity, bool inhibit_coop);
bool qa_q1ad_removed_for_runes(qa_q1ad_context *context, qa_q1ad_actor *entity);
void qa_q1ad_add_frame_tick(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name);
void qa_q1ad_remove_frame_tick(qa_q1ad_context *context, qa_q1ad_actor *entity);
void qa_q1ad_frame(qa_q1ad_context *context, double seconds);

/* Registry + dispatch. Local names run local handlers; unknown names
 * delegate to the host call_* callbacks (foundation-owned callbacks). */
void qa_q1ad_register_named(qa_q1ad_context *context, const char *name, qa_q1ad_handlers handlers);
void qa_q1ad_register_spawn(qa_q1ad_context *context, const char *classname, bool replace, qa_q1ad_spawn_fn fn);
bool qa_q1ad_spawn_dispatch(qa_q1ad_context *context, qa_q1ad_actor *entity);
void qa_q1ad_do_action(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name);
void qa_q1ad_do_touch(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name,
                      const qa_actor_id *other, const qa_vec3 *normal);
void qa_q1ad_do_use(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name,
                    const qa_actor_id *other, const qa_actor_id *activator);
void qa_q1ad_do_die(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name,
                    const qa_actor_id *attacker);
/* Store a registered name into an actor callback slot (game.named.*). */
void qa_q1ad_bind(char slot[QA_Q1AD_NAME], const char *name);
void qa_q1ad_bind_prefix(char slot[QA_Q1AD_NAME], const char *prefix, const char *name);
void qa_q1ad_named_name(char out[QA_Q1AD_NAME], const char *prefix, const char *name);

/* Thin host wrappers with TS-matching NULL defaults. */
double qa_q1ad_cvar(qa_q1ad_context *context, const char *name);
void qa_q1ad_emit(qa_q1ad_context *context, const qa_q1ad_event *event);
void qa_q1ad_host_emit(qa_q1ad_context *context, const qa_q1ad_host_event *event);
bool qa_q1ad_live(qa_q1ad_context *context, qa_q1ad_actor *entity);
void qa_q1ad_remove(qa_q1ad_context *context, qa_q1ad_actor *entity);
void qa_q1ad_link(qa_q1ad_context *context, qa_q1ad_actor *entity);
void qa_q1ad_cancel(qa_q1ad_context *context, qa_q1ad_actor *entity);
void qa_q1ad_schedule(qa_q1ad_context *context, qa_q1ad_actor *entity, double delay, const char *name);
void qa_q1ad_schedule_at(qa_q1ad_context *context, qa_q1ad_actor *entity, double time, const char *name);
void qa_q1ad_schedule_named(qa_q1ad_context *context, qa_q1ad_actor *entity, double delay,
                            const char *prefix, const char *name);
qa_q1ad_body qa_q1ad_body_of(qa_q1ad_context *context, qa_q1ad_actor *entity);
bool qa_q1ad_body_by_id(qa_q1ad_context *context, qa_actor_id actor, qa_q1ad_body *out);
void qa_q1ad_set_body(qa_q1ad_context *context, qa_q1ad_actor *entity, const qa_q1ad_body_patch *patch);
void qa_q1ad_set_origin(qa_q1ad_context *context, qa_q1ad_actor *entity, qa_vec3 origin);
void qa_q1ad_set_bounds(qa_q1ad_context *context, qa_q1ad_actor *entity, qa_bounds bounds);
double qa_q1ad_random(qa_q1ad_context *context);
double qa_q1ad_health(qa_q1ad_context *context, qa_actor_id actor);
bool qa_q1ad_is_player(qa_q1ad_context *context, const qa_actor_id *actor);
qa_q1ad_player *qa_q1ad_player_state(qa_q1ad_context *context, qa_actor_id actor);
qa_q1ad_actor *qa_q1ad_entity(qa_q1ad_context *context, const qa_actor_id *actor);
qa_q1ad_actor *qa_q1ad_world(qa_q1ad_context *context);
bool qa_q1ad_resolve_owned(qa_q1ad_context *context, qa_actor_id actor, qa_owned_actor *out);
void qa_q1ad_sound_actor(qa_q1ad_context *context, qa_actor_id actor, const char *path, const char *channel);
void qa_q1ad_sound_entity(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *path, const char *channel);
void qa_q1ad_effect(qa_q1ad_context *context, const char *kind, qa_vec3 origin, const qa_actor_id *actor, double count);
void qa_q1ad_message(qa_q1ad_context *context, qa_actor_id player, const char *key, bool localized,
                     const qa_q1ad_msg_arg *args, size_t arg_count);
void qa_q1ad_damage(qa_q1ad_context *context, qa_actor_id target, const qa_actor_id *inflictor,
                    const qa_actor_id *attacker, double amount, const char *weapon,
                    const char *mode, const char *cause);
void qa_q1ad_use_targets(qa_q1ad_context *context, qa_q1ad_actor *entity, const qa_actor_id *activator);
size_t qa_q1ad_entities(qa_q1ad_context *context, qa_q1ad_actor **out, size_t capacity);
size_t qa_q1ad_find(qa_q1ad_context *context, const char *targetname, qa_q1ad_actor **out, size_t capacity);
size_t qa_q1ad_players(qa_q1ad_context *context, qa_actor_id *out, size_t capacity);
const char *qa_q1ad_weapon_item(const char *weapon, char buffer[64]);

/* Mirrored Q1TravelState (src/content/q1/base/travel.ts). */
#define QA_Q1AD_TRAVEL_ITEMS 24
typedef struct qa_q1ad_travel {
  double health, max_health;
  qa_q1ad_inventory_entry inventory[QA_Q1AD_TRAVEL_ITEMS];
  size_t inventory_count;
  const char *weapon;
  qa_q1ad_armor armor_regular;
  const uint8_t *extension_bytes;
  size_t extension_count;
} qa_q1ad_travel;

/* Shared constants from foundation/types.ts. */
#define QA_Q1AD_WEAPONS_COUNT 8
extern const char *const QA_Q1AD_WEAPONS[QA_Q1AD_WEAPONS_COUNT];

#endif

