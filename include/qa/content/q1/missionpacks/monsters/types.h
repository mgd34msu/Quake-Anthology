/*
 * Port of src/content/q1/missionpacks/monsters/types.ts.
 * Cross-module types whose C owners have not landed are mirrored here
 * with collision-proof qa_q1mpm_ names (replace when owners land):
 *  - qa_q1mpm_actor: Q1Actor, src/content/q1/foundation/entity.ts
 *  - qa_q1mpm_monster_state: Q1Monster, same owner
 *  - qa_q1mpm_game: Q1EntityServices,
 *    src/content/q1/foundation/entity-services.ts (only the surface
 *    this slice uses; methods are fn pointers on the struct)
 *  - qa_q1mpm_host: Q1FoundationHost, src/content/q1/foundation/types.ts
 *    (plus its Q1Trace/Q1TraceRequest/Q1Basis/Q1Event/Q1PlayerState/
 *    Q1SoundChannel shapes and BodyState from src/world/actors)
 *  - qa_q1mpm_species_spec / qa_q1mpm_frame_* / qa_q1mpm_ai:
 *    MonsterSpecies (src/content/q1/base/species.ts) and MonsterFrame/
 *    MonsterOperation/MonsterAi (src/content/q1/base/animation.ts)
 *  - qa_q1mpm_base_ops: BaseMonster, src/content/q1/base/monsters.ts
 *    (per-monster fn+ctx; created by qa_q1mpm_base_factory)
 *  - qa_q1mpm_q1base: Q1Base, src/content/q1/base/provider.ts
 *  - qa_q1mpm_handlers: Q1CallbackHandlers,
 *    src/content/q1/foundation/callbacks.ts
 * Strings for TS string unions (solid, movement, edition, weapon...)
 * stay C strings. TS ActorId|null becomes qa_q1mpm_opt_actor.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_TYPES_H
#define QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/persistence/value.h"

typedef struct qa_q1mpm_opt_actor { bool has; qa_actor_id id; } qa_q1mpm_opt_actor;
typedef struct qa_q1mpm_opt_owned { bool has; qa_owned_actor actor; } qa_q1mpm_opt_owned;

/* TS Q1SoundChannel: a channel name, or NULL name + numeric (-1|5|6|7). */
typedef struct qa_q1mpm_channel { const char *name; double numeric; } qa_q1mpm_channel;

/* TS BodyState (src/world/actors), fields this slice reads/writes. */
typedef struct qa_q1mpm_body {
  qa_vec3 origin; qa_vec3 angles; qa_vec3 velocity; qa_bounds bounds;
  qa_q1mpm_opt_actor ground;
} qa_q1mpm_body;
typedef struct qa_q1mpm_body_patch {
  bool has_origin; qa_vec3 origin;
  bool has_angles; qa_vec3 angles;
  bool has_velocity; qa_vec3 velocity;
  bool has_bounds; qa_bounds bounds;
  bool has_ground; qa_q1mpm_opt_actor ground;
} qa_q1mpm_body_patch;

typedef struct qa_q1mpm_trace {
  double fraction; qa_vec3 end; qa_vec3 normal; qa_q1mpm_opt_actor actor;
  bool start_solid; bool all_solid; bool sky; bool in_open; bool in_water;
} qa_q1mpm_trace;
typedef struct qa_q1mpm_trace_request {
  qa_vec3 start; qa_vec3 end; qa_bounds bounds; qa_q1mpm_opt_actor ignore;
  bool monsters; bool missile;
} qa_q1mpm_trace_request;
typedef qa_angle_vectors qa_q1mpm_basis;

/* TS Q1Event subset emitted directly through host.emit in this slice. */
typedef struct qa_q1mpm_event {
  const char *kind; /* "sound" | "message" | "effect" | ... */
  qa_q1mpm_opt_actor actor;
  const char *path; qa_q1mpm_channel channel; double volume; double attenuation;
  bool has_origin; qa_vec3 origin;
  qa_q1mpm_opt_actor player; const char *text; bool center;
  const char *effect; double amount; double total; double found;
  const char *id;
  const char *style; qa_vec3 start; qa_vec3 end; qa_vec3 direction;
  double color_start; double color_length; double color; double count_value;
} qa_q1mpm_event;

/* TS Q1Monster (entity.monster state). mode is the TS string union. */
typedef struct qa_q1mpm_monster_state {
  const char *species; const char *mode;
  double frame_index; const double *sequence; size_t sequence_count; double first_frame;
  qa_q1mpm_opt_actor enemy; qa_q1mpm_opt_actor old_enemy;
  char path[128];
  double pause_until; double attack_finished; double pain_finished; double search_until;
  bool death_drop; bool refired;
} qa_q1mpm_monster_state;

typedef struct qa_q1mpm_game qa_q1mpm_game;
typedef struct qa_q1mpm_actor qa_q1mpm_actor;
typedef struct qa_q1mpm_mission_monster qa_q1mpm_mission_monster;
typedef struct qa_q1mpm_runtime qa_q1mpm_runtime;

/* TS Q1CallbackHandlers as fn+ctx; NULL fn means the handler is absent. */
typedef void (*qa_q1mpm_cb_action_fn)(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity);
typedef void (*qa_q1mpm_cb_touch_fn)(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity,
                                     qa_actor_id other, bool has_normal, qa_vec3 normal, const void *surface);
typedef void (*qa_q1mpm_cb_pain_fn)(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity,
                                    qa_q1mpm_opt_actor attacker, double damage);
typedef void (*qa_q1mpm_cb_die_fn)(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity,
                                   qa_q1mpm_opt_actor attacker);
typedef void (*qa_q1mpm_cb_use_fn)(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity,
                                   qa_q1mpm_opt_actor other, qa_q1mpm_opt_actor activator);
typedef struct qa_q1mpm_handlers {
  void *ctx;
  qa_q1mpm_cb_action_fn action;
  qa_q1mpm_cb_touch_fn touch;
  qa_q1mpm_cb_pain_fn pain;
  qa_q1mpm_cb_die_fn die;
  qa_q1mpm_cb_use_fn use;
} qa_q1mpm_handlers;

/* A bound TS callback closure from game.named.* (name NULL = TS null). */
typedef enum qa_q1mpm_callback_kind {
  QA_Q1MPM_CB_ACTION, QA_Q1MPM_CB_USE, QA_Q1MPM_CB_TOUCH, QA_Q1MPM_CB_PAIN, QA_Q1MPM_CB_DIE
} qa_q1mpm_callback_kind;
typedef struct qa_q1mpm_callback {
  const char *name; qa_q1mpm_actor *entity; qa_q1mpm_callback_kind kind;
} qa_q1mpm_callback;

/* TS Q1Actor mirror. String fields are fixed buffers; fields/references
 * are the TS Maps as owned ordered lists. */
typedef struct qa_q1mpm_field_entry { char *key; char *value; } qa_q1mpm_field_entry;
typedef struct qa_q1mpm_ref_entry { char *key; qa_q1mpm_opt_actor value; } qa_q1mpm_ref_entry;
struct qa_q1mpm_actor {
  qa_owned_actor actor;
  char classname[64];
  const void *source_ordinal_unused; /* parity placeholder, unused */
  bool has_source_ordinal; double source_ordinal;
  char model[128]; double frame; double skin; int32_t effects;
  const char *solid; const char *movement;
  qa_q1mpm_field_entry *fields; size_t field_count; size_t field_capacity;
  qa_q1mpm_ref_entry *references; size_t reference_count; size_t reference_capacity;
  char target[128]; char targetname[128]; char killtarget[128]; char message[256];
  double delay; int32_t spawnflags; double sounds; double wait; double speed;
  double damage; double max_health; bool aimed_damage; double next_think;
  qa_q1mpm_callback think; qa_q1mpm_callback use; qa_q1mpm_callback touch;
  qa_q1mpm_callback pain; qa_q1mpm_callback die; qa_q1mpm_callback blocked;
  qa_q1mpm_callback path_end;
  bool has_move; qa_vec3 move_destination; qa_q1mpm_callback move_done;
  qa_q1mpm_opt_actor owner; qa_q1mpm_opt_actor activator;
  char original_model[128];
  qa_vec3 pos1; qa_vec3 pos2; qa_vec3 dest1; qa_vec3 dest2; qa_vec3 movedir; qa_vec3 mangle;
  const char *door_state;
  double attack_finished; double count; bool activated;
  qa_q1mpm_monster_state *monster; /* borrowed; owned by the controller */
  const char *projectile; const char *projectile_weapon;
  qa_vec3 angular_velocity; double water_level; int water_type;
  int32_t movement_flags; double ideal_yaw; double yaw_speed;
  const char *attack_state;
};

/* Q1Actor Map/text/number/vector accessors + damageable (runtime.c). */
void qa_q1mpm_actor_init(qa_q1mpm_actor *entity, qa_owned_actor actor, const char *classname);
void qa_q1mpm_actor_destroy(qa_q1mpm_actor *entity);
void qa_q1mpm_fields_set(qa_q1mpm_actor *entity, const char *key, const char *value);
const char *qa_q1mpm_fields_get(const qa_q1mpm_actor *entity, const char *key); /* NULL absent */
bool qa_q1mpm_fields_has(const qa_q1mpm_actor *entity, const char *key);
void qa_q1mpm_references_set(qa_q1mpm_actor *entity, const char *key, qa_q1mpm_opt_actor value);
qa_q1mpm_opt_actor qa_q1mpm_references_get(const qa_q1mpm_actor *entity, const char *key);
const char *qa_q1mpm_actor_text(const qa_q1mpm_actor *entity, const char *key);
double qa_q1mpm_actor_number(const qa_q1mpm_actor *entity, const char *key);
double qa_q1mpm_actor_number_or(const qa_q1mpm_actor *entity, const char *key, double fallback);
qa_vec3 qa_q1mpm_actor_vector(const qa_q1mpm_actor *entity, const char *key);
qa_vec3 qa_q1mpm_parse_vector(const char *text);
bool qa_q1mpm_actor_damageable(qa_q1mpm_game *game, const qa_q1mpm_actor *entity);
void qa_q1mpm_actor_set_damageable(qa_q1mpm_game *game, qa_q1mpm_actor *entity, bool value);
/* TS String(Math.fround(x)) via the shortest-round-trip style used for
 * field writes in this slice; buf must hold 32 bytes. */
void qa_q1mpm_format_fround(char *buf, size_t size, double value);
void qa_q1mpm_format_number(char *buf, size_t size, double value);

/* TS Q1PlayerState subset read by this slice. */
typedef struct qa_q1mpm_powerup_entry { const char *name; double expires; } qa_q1mpm_powerup_entry;
typedef struct qa_q1mpm_player_state {
  qa_owned_actor actor;
  const char *weapon; /* TS Q1Weapon */
  double hostile_until;
  const qa_q1mpm_powerup_entry *powerups; size_t powerup_count;
} qa_q1mpm_player_state;
double qa_q1mpm_player_powerup(const qa_q1mpm_player_state *player, const char *name); /* 0 absent */

/* TS Q1FoundationOptions subset read by this slice. */
typedef struct qa_q1mpm_options {
  const char *edition; /* "classic" | "rerelease" */
  int skill; double deathmatch; bool coop;
  bool has_teamplay; double teamplay;
} qa_q1mpm_options;

/* TS Q1FoundationHost mirror (fn+ctx). List fns fill caller buffers and
 * return the count written. set_gravity may be NULL (TS optional). */
typedef struct qa_q1mpm_combat_read { double health; bool can_take_damage; const char *team; } qa_q1mpm_combat_read;
/* TS SharedInventoryTable entry subset (src/world/gameplay/inventory). */
typedef struct qa_q1mpm_inventory_entry {
  qa_item_id item; double count; double capacity;
  const char *count_policy_kind; const char *count_policy_arithmetic; /* NULL absent */
} qa_q1mpm_inventory_entry;
typedef void (*qa_q1mpm_on_release_fn)(void *ctx, qa_owned_actor actor);
typedef struct qa_q1mpm_host {
  void *ctx;
  double (*random)(void *ctx);
  qa_q1mpm_trace (*trace)(void *ctx, const qa_q1mpm_trace_request *request);
  const char *(*contents)(void *ctx, qa_vec3 point);
  bool (*walk_move)(void *ctx, qa_owned_actor actor, double yaw, double distance);
  void (*change_yaw)(void *ctx, qa_owned_actor actor);
  void (*move_to_goal)(void *ctx, qa_owned_actor actor, qa_actor_id goal, double distance, const char *mode);
  bool (*check_bottom)(void *ctx, qa_actor_id actor);
  void (*schedule_think)(void *ctx, qa_owned_actor actor, double due_seconds);
  void (*cancel_think)(void *ctx, qa_owned_actor actor);
  void (*emit)(void *ctx, const qa_q1mpm_event *event);
  size_t (*players)(void *ctx, qa_actor_id *out, size_t capacity);
  qa_q1mpm_opt_actor (*check_client)(void *ctx, qa_owned_actor observer);
  const char *(*classname)(void *ctx, qa_actor_id actor);
  void (*set_gravity)(void *ctx, qa_actor_id actor, double scale);
  bool (*bodies_read)(void *ctx, qa_actor_id actor, qa_q1mpm_body *out);
  void (*bodies_write)(void *ctx, qa_owned_actor actor, const qa_q1mpm_body *body);
  void (*bodies_link)(void *ctx, qa_owned_actor actor);
  bool (*combat_read)(void *ctx, qa_actor_id actor, qa_q1mpm_combat_read *out);
  void (*combat_set_health)(void *ctx, qa_owned_actor actor, double health);
  void (*combat_set_can_take_damage)(void *ctx, qa_owned_actor actor, bool value);
  bool (*actors_resolve_owned)(void *ctx, qa_actor_id actor, qa_owned_actor *out);
  bool (*actors_resolve_saved)(void *ctx, int64_t slot, int64_t generation, qa_owned_actor *out);
  bool (*actors_is_live)(void *ctx, qa_actor_id actor);
  void (*actors_release)(void *ctx, qa_owned_actor actor);
  size_t (*actors_observations)(void *ctx, qa_owned_actor *out, size_t capacity);
  void (*actors_on_release)(void *ctx, qa_q1mpm_on_release_fn fn, void *fn_ctx);
  double (*inventory_count)(void *ctx, qa_actor_id actor, qa_item_id item);
  void (*inventory_configure)(void *ctx, qa_owned_actor actor, const qa_q1mpm_inventory_entry *entry);
  size_t (*inventory_entries)(void *ctx, qa_actor_id actor, qa_q1mpm_inventory_entry *out, size_t capacity);
  bool (*inventory_has)(void *ctx, qa_actor_id actor);
  void (*inventory_create)(void *ctx, qa_owned_actor actor, const qa_q1mpm_inventory_entry *entries, size_t count);
  void (*inventory_consume)(void *ctx, qa_owned_actor actor, qa_item_id item, double amount);
  double (*inventory_adjust_source_counter)(void *ctx, qa_owned_actor actor, qa_item_id item, double delta);
} qa_q1mpm_host;

/* TS game.named registry subset. */
typedef struct qa_q1mpm_named {
  void *ctx;
  void (*register_named)(void *ctx, const char *name, const qa_q1mpm_handlers *handlers);
  qa_q1mpm_callback (*action)(void *ctx, qa_q1mpm_actor *entity, const char *name);
  qa_q1mpm_callback (*use)(void *ctx, qa_q1mpm_actor *entity, const char *name);
  qa_q1mpm_callback (*touch)(void *ctx, qa_q1mpm_actor *entity, const char *name);
  qa_q1mpm_callback (*pain)(void *ctx, qa_q1mpm_actor *entity, const char *name);
  qa_q1mpm_callback (*die)(void *ctx, qa_q1mpm_actor *entity, const char *name);
} qa_q1mpm_named;

/* TS spawn handler and state extension shapes. */
typedef void (*qa_q1mpm_spawn_fn)(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity);
typedef struct qa_q1mpm_state_extension {
  const char *id; void *ctx;
  uint8_t *(*capture)(void *ctx, size_t *out_length);
  void (*restore)(void *ctx, const uint8_t *bytes, size_t length);
  void (*clone)(void *ctx, qa_q1mpm_actor *source, qa_q1mpm_actor *target);
} qa_q1mpm_state_extension;
typedef bool (*qa_q1mpm_path_touch_fn)(void *ctx, qa_q1mpm_actor *corner, qa_q1mpm_actor *mover);

/* TS Q1EntityServices mirror: data fields + method fn pointers taking
 * the game struct (implementor state in ctx). */
struct qa_q1mpm_game {
  void *ctx;
  const qa_q1mpm_host *host;
  qa_q1mpm_named named;
  qa_q1mpm_options options;
  double time;
  qa_q1mpm_actor *world;
  qa_q1mpm_actor *sight_entity;
  double sight_time;
  double total_monsters;
  double killed_monsters;
  qa_q1mpm_basis basis;
  bool uses_id1_precaches;
  qa_q1mpm_body (*body)(qa_q1mpm_game *game, qa_q1mpm_actor *entity);
  void (*set_body)(qa_q1mpm_game *game, qa_q1mpm_actor *entity, const qa_q1mpm_body_patch *patch);
  void (*link)(qa_q1mpm_game *game, qa_q1mpm_actor *entity);
  void (*set_origin)(qa_q1mpm_game *game, qa_q1mpm_actor *entity, qa_vec3 origin);
  void (*set_bounds)(qa_q1mpm_game *game, qa_q1mpm_actor *entity, qa_bounds bounds);
  void (*remove)(qa_q1mpm_game *game, qa_q1mpm_actor *entity);
  qa_q1mpm_actor *(*create)(qa_q1mpm_game *game, const char *classname);
  qa_q1mpm_actor *(*entity)(qa_q1mpm_game *game, qa_q1mpm_opt_actor actor);
  size_t (*find)(qa_q1mpm_game *game, const char *targetname, qa_q1mpm_actor **out, size_t capacity);
  double (*health)(qa_q1mpm_game *game, qa_actor_id actor);
  bool (*live)(qa_q1mpm_game *game, qa_q1mpm_actor *entity);
  bool (*is_player)(qa_q1mpm_game *game, qa_q1mpm_opt_actor actor);
  qa_q1mpm_player_state *(*player)(qa_q1mpm_game *game, qa_actor_id actor);
  void (*sound)(qa_q1mpm_game *game, qa_q1mpm_actor *entity, const char *path,
                qa_q1mpm_channel channel, double attenuation, double volume);
  void (*message)(qa_q1mpm_game *game, qa_q1mpm_opt_actor player, const char *text, bool center);
  void (*effect)(qa_q1mpm_game *game, const char *effect, qa_vec3 origin,
                 qa_q1mpm_opt_actor actor, double amount);
  void (*schedule)(qa_q1mpm_game *game, qa_q1mpm_actor *entity, double delay, qa_q1mpm_callback callback);
  void (*cancel)(qa_q1mpm_game *game, qa_q1mpm_actor *entity);
  void (*damage)(qa_q1mpm_game *game, qa_actor_id target, qa_actor_id inflictor,
                 qa_q1mpm_opt_actor attacker, double amount, const char *weapon,
                 const char *delivery, const char *death_type);
  bool (*can_damage)(qa_q1mpm_game *game, qa_actor_id target, qa_actor_id inflictor);
  void (*radius_damage)(qa_q1mpm_game *game, qa_actor_id inflictor, qa_q1mpm_opt_actor attacker,
                        double amount, qa_q1mpm_opt_actor ignore, const char *weapon, const char *death_type);
  qa_q1mpm_basis (*make_vectors)(qa_q1mpm_game *game, qa_vec3 angles);
  void (*register_spawn)(qa_q1mpm_game *game, const char *classname, qa_q1mpm_spawn_fn fn, void *fn_ctx);
  void (*replace_spawn)(qa_q1mpm_game *game, const char *classname, qa_q1mpm_spawn_fn fn, void *fn_ctx);
  void (*register_state_extension)(qa_q1mpm_game *game, const qa_q1mpm_state_extension *extension);
  void (*register_path_touch)(qa_q1mpm_game *game, const char *id, qa_q1mpm_path_touch_fn fn, void *fn_ctx);
  void (*use_targets)(qa_q1mpm_game *game, qa_q1mpm_actor *entity, qa_q1mpm_opt_actor activator);
  qa_item_id (*weapon_item)(qa_q1mpm_game *game, const char *weapon);
  const char *(*weapon_ammo)(qa_q1mpm_game *game, const char *weapon);
  bool (*select_weapon)(qa_q1mpm_game *game, qa_owned_actor actor, const char *weapon);
  const char *(*choose_best)(qa_q1mpm_game *game, qa_owned_actor actor);
  void (*spawn_entity)(qa_q1mpm_game *game, qa_q1mpm_actor *entity);
  qa_q1mpm_actor *(*clone_entity)(qa_q1mpm_game *game, qa_q1mpm_actor *source);
  size_t (*entities)(qa_q1mpm_game *game, qa_q1mpm_actor **out, size_t capacity);
  void (*sound_owned)(qa_q1mpm_game *game, qa_owned_actor actor, const char *path,
                      qa_q1mpm_channel channel, double attenuation, double volume);
  bool (*monster_mission_spawned_started)(qa_q1mpm_game *game, qa_actor_id actor, int which);
};

/* TS default-argument wrappers for game.sound/message/damage (runtime.c). */
void qa_q1mpm_game_sound(qa_q1mpm_game *game, qa_q1mpm_actor *entity, const char *path);
void qa_q1mpm_game_sound_ch(qa_q1mpm_game *game, qa_q1mpm_actor *entity, const char *path,
                            qa_q1mpm_channel channel);
void qa_q1mpm_game_damage(qa_q1mpm_game *game, qa_actor_id target, qa_actor_id inflictor,
                          qa_q1mpm_opt_actor attacker, double amount);

/* TS MonsterAi / MonsterOperation / MonsterFrame / MonsterSpecies. */
typedef enum qa_q1mpm_ai {
  QA_Q1MPM_AI_STAND, QA_Q1MPM_AI_WALK, QA_Q1MPM_AI_RUN, QA_Q1MPM_AI_CHARGE_SIDE,
  QA_Q1MPM_AI_MELEE_SIDE, QA_Q1MPM_AI_CHARGE, QA_Q1MPM_AI_MELEE, QA_Q1MPM_AI_PAINFORWARD,
  QA_Q1MPM_AI_TURN, QA_Q1MPM_AI_FACE, QA_Q1MPM_AI_PAIN, QA_Q1MPM_AI_FORWARD
} qa_q1mpm_ai;
const char *qa_q1mpm_ai_name(qa_q1mpm_ai mode);
typedef enum qa_q1mpm_operation_kind {
  QA_Q1MPM_OP_AI, QA_Q1MPM_OP_SOUND, QA_Q1MPM_OP_SOLID, QA_Q1MPM_OP_LIGHTSTYLE, QA_Q1MPM_OP_ACTION
} qa_q1mpm_operation_kind;
typedef enum qa_q1mpm_comparison { QA_Q1MPM_CMP_GREATER, QA_Q1MPM_CMP_LESS } qa_q1mpm_comparison;
typedef struct qa_q1mpm_operation {
  qa_q1mpm_operation_kind kind;
  qa_q1mpm_ai mode; double distance;
  const char *path; qa_q1mpm_channel channel; double attenuation;
  qa_q1mpm_comparison comparison; bool has_chance; double chance;
  const char *solid; const char *pattern; const char *name;
} qa_q1mpm_operation;
typedef struct qa_q1mpm_frame_entry {
  const char *name; double frame; const char *next;
  const qa_q1mpm_operation *operations; size_t operation_count;
} qa_q1mpm_frame_entry;
typedef struct qa_q1mpm_frame_table {
  const qa_q1mpm_frame_entry *entries; size_t count;
} qa_q1mpm_frame_table;
const qa_q1mpm_frame_entry *qa_q1mpm_frame_table_get(const qa_q1mpm_frame_table *table, const char *name);
bool qa_q1mpm_frame_table_has(const qa_q1mpm_frame_table *table, const char *name);

typedef struct qa_q1mpm_species_spec {
  const char *species; const char *kill_string; /* NULL = absent */
  const char *const *classnames; size_t classname_count;
  const char *model; const char *head; /* head NULL = TS null */
  double health; double gib_health;
  const char *const *gibs; size_t gib_count;
  qa_bounds bounds;
  const char *stand; const char *walk; const char *run; const char *sight;
  const char *missile; /* NULL = TS null */
  bool melee; const char *movement; /* "walk"|"fly"|"swim"|"boss" */
} qa_q1mpm_species_spec;

/* Mirrored baseSpecies (owner: src/content/q1/base/species.ts). */
extern const qa_q1mpm_species_spec *const qa_q1mpm_base_species;
extern const size_t qa_q1mpm_base_species_count;

/* TS MissionAction and friends. Closures are fn+ctx. */
typedef void (*qa_q1mpm_action_fn)(void *ctx, qa_q1mpm_mission_monster *monster);
typedef struct qa_q1mpm_action { qa_q1mpm_action_fn fn; void *ctx; } qa_q1mpm_action;
typedef struct qa_q1mpm_named_action { const char *name; qa_q1mpm_action action; } qa_q1mpm_named_action;
typedef struct qa_q1mpm_named_handlers { const char *name; qa_q1mpm_handlers handlers; } qa_q1mpm_named_handlers;
typedef void (*qa_q1mpm_pain_hook_fn)(void *ctx, qa_q1mpm_mission_monster *monster,
                                      qa_q1mpm_opt_actor attacker, double damage);
typedef void (*qa_q1mpm_die_hook_fn)(void *ctx, qa_q1mpm_mission_monster *monster,
                                     qa_q1mpm_opt_actor attacker);
typedef bool (*qa_q1mpm_check_attack_fn)(void *ctx, qa_q1mpm_mission_monster *monster);
typedef void (*qa_q1mpm_found_fn)(void *ctx, qa_q1mpm_mission_monster *monster, qa_actor_id target);
typedef void (*qa_q1mpm_ai_hook_fn)(void *ctx, qa_q1mpm_mission_monster *monster,
                                    qa_q1mpm_ai mode, double distance);
typedef void (*qa_q1mpm_use_hook_fn)(void *ctx, qa_q1mpm_mission_monster *monster,
                                     qa_q1mpm_opt_actor activator);

/* TS PackMonsterDefinition. Optional hooks have NULL fn. */
typedef struct qa_q1mpm_pack_monster_definition {
  qa_q1mpm_species_spec spec;
  bool has_base_behavior; bool base_behavior;
  qa_q1mpm_frame_table frames;
  const qa_q1mpm_named_action *actions; size_t action_count;
  const qa_q1mpm_named_handlers *callbacks; size_t callback_count;
  qa_q1mpm_action spawn; qa_q1mpm_action start;
  qa_q1mpm_pain_hook_fn pain; void *pain_ctx;
  qa_q1mpm_die_hook_fn die; void *die_ctx;
  qa_q1mpm_action melee;
  qa_q1mpm_check_attack_fn check_attack; void *check_attack_ctx;
  qa_q1mpm_found_fn found; void *found_ctx;
  qa_q1mpm_ai_hook_fn ai; void *ai_ctx;
  qa_q1mpm_use_hook_fn use; void *use_ctx;
} qa_q1mpm_pack_monster_definition;

/* TS PackMonsterFactory: fills caller array, returns count written. */
typedef size_t (*qa_q1mpm_pack_monster_factory_fn)(
    void *ctx, qa_q1mpm_runtime *runtime,
    const qa_q1mpm_pack_monster_definition **out, size_t capacity);
/* TS MissionSpawn. */
typedef void (*qa_q1mpm_mission_spawn_fn)(void *ctx, qa_q1mpm_actor *entity);

/* TS MissionMonsterHooks. charmer NULL fn = absent. */
typedef struct qa_q1mpm_mission_monster_hooks {
  void *ctx;
  qa_q1mpm_opt_actor (*charmer)(void *ctx);
} qa_q1mpm_mission_monster_hooks;

/* TS BaseMonster mirror: per-monster super operations (fn+ctx). */
typedef struct qa_q1mpm_base_ops {
  void *ctx;
  void (*spawn)(void *ctx, qa_q1mpm_mission_monster *m);
  void (*start)(void *ctx, qa_q1mpm_mission_monster *m);
  void (*play)(void *ctx, qa_q1mpm_mission_monster *m, const char *name);
  void (*pain)(void *ctx, qa_q1mpm_mission_monster *m, qa_q1mpm_opt_actor attacker, double damage);
  void (*die)(void *ctx, qa_q1mpm_mission_monster *m, qa_q1mpm_opt_actor attacker);
  void (*melee_attack)(void *ctx, qa_q1mpm_mission_monster *m);
  bool (*try_attack)(void *ctx, qa_q1mpm_mission_monster *m);
  void (*found)(void *ctx, qa_q1mpm_mission_monster *m, qa_actor_id target);
  void (*ai)(void *ctx, qa_q1mpm_mission_monster *m, qa_q1mpm_ai mode, double distance);
  void (*run)(void *ctx, qa_q1mpm_mission_monster *m, double distance);
  void (*move_to_enemy)(void *ctx, qa_q1mpm_mission_monster *m, double distance);
  bool (*search_for_coop_target)(void *ctx, qa_q1mpm_mission_monster *m);
  bool (*find_target)(void *ctx, qa_q1mpm_mission_monster *m);
  void (*retaliate)(void *ctx, qa_q1mpm_mission_monster *m, qa_q1mpm_opt_actor attacker);
  void (*use)(void *ctx, qa_q1mpm_mission_monster *m, qa_q1mpm_opt_actor activator);
  void (*count_kill)(void *ctx, qa_q1mpm_mission_monster *m);
  void (*delay)(void *ctx, qa_q1mpm_mission_monster *m, double seconds);
  void (*face)(void *ctx, qa_q1mpm_mission_monster *m);
  bool (*visible)(void *ctx, qa_q1mpm_mission_monster *m, qa_q1mpm_opt_actor target, bool has_target);
  bool (*eye)(void *ctx, qa_q1mpm_mission_monster *m, qa_actor_id target, qa_vec3 *out);
  double (*range_distance)(void *ctx, qa_q1mpm_mission_monster *m, qa_q1mpm_opt_actor target, bool has_target);
  void (*change_yaw)(void *ctx, qa_q1mpm_mission_monster *m);
  void (*attack_finished)(void *ctx, qa_q1mpm_mission_monster *m, double seconds);
  qa_q1mpm_basis (*make_vectors)(void *ctx, qa_q1mpm_mission_monster *m);
  double (*melee)(void *ctx, qa_q1mpm_mission_monster *m, double range, double scale, double rolls, bool sight);
  qa_save_value *(*capture)(void *ctx, qa_q1mpm_mission_monster *m);
  void (*restore)(void *ctx, qa_q1mpm_mission_monster *m, const qa_save_reader *reader);
} qa_q1mpm_base_ops;

/* TS BaseMonsterSource handed to the base factory. */
typedef struct qa_q1mpm_base_source {
  const char *callback_prefix;
  qa_q1mpm_frame_table frames;
  const qa_q1mpm_named_action *actions; size_t action_count;
} qa_q1mpm_base_source;
typedef struct qa_q1mpm_q1base qa_q1mpm_q1base;
typedef struct qa_q1mpm_base_factory {
  void *ctx;
  /* Mirrors `new BaseMonster(game, entity, spec, base, source)`: fills
   * out_ops and the monster's base-owned scalar state. */
  void (*create)(void *ctx, qa_q1mpm_mission_monster *m, const qa_q1mpm_base_source *source);
} qa_q1mpm_base_factory;

/* TS Q1Base mirror: MonsterServices + the base monsters map surface. */
struct qa_q1mpm_q1base {
  void *ctx;
  qa_q1mpm_base_factory base_factory;
  bool (*count_monster_kill)(void *ctx, qa_q1mpm_mission_monster *m);
  const char *(*next_hell_knight_melee)(void *ctx);
  void (*finale)(void *ctx, qa_q1mpm_mission_monster *m);
  void (*finish_finale)(void *ctx, qa_q1mpm_mission_monster *m);
  /* base.monsters.get(actor)?.capture(): owned tree or NULL. */
  qa_save_value *(*monsters_capture)(void *ctx, qa_owned_actor actor);
  bool (*monsters_has)(void *ctx, qa_owned_actor actor);
  void (*monsters_delete)(void *ctx, qa_owned_actor actor);
  /* base.monsters.get(actor) as an owner-wrapped controller (paths.ts). */
  qa_q1mpm_mission_monster *(*monsters_get)(void *ctx, qa_owned_actor actor);
  /* base.backpacks.set(actor, contents). */
  void (*backpacks_set)(void *ctx, qa_owned_actor actor, const char *weapon,
                        const qa_item_id *extra, size_t extra_count,
                        double shells, double nails, double rockets, double cells);
};

#endif
