/*
 * Port of src/content/q1/addons/monsters/ai/controller.ts (Mg3Monster).
 *
 * This header also carries the mirrored cross-module surface shared by
 * the whole q1/addons/monsters slice (qa_q1am_* names, collision-proof;
 * replace when the TS owners land):
 *  - qa_q1am_actor / qa_q1am_state: Q1Actor / Q1Monster,
 *    src/content/q1/foundation/entity.ts
 *  - qa_q1am_game: Q1EntityServices + Q1FoundationHost,
 *    src/content/q1/foundation/entity-services.ts and types.ts
 *    (methods/tables become fn+ctx hooks; field reads used by this
 *    slice are concrete fields)
 *  - qa_q1am_basis/body/trace/event/handlers/state_extension: Q1Basis,
 *    BodyState (src/contracts/world.ts), Q1Trace, Q1Event,
 *    Q1CallbackHandlers, Q1StateExtension
 *    (src/content/q1/foundation/types.ts, callbacks.ts)
 *  - qa_q1am_player: Q1PlayerState subset read/written by this slice
 *  - qa_q1am_species / qa_q1am_base_species: MonsterSpecies and
 *    baseSpecies, src/content/q1/base/species.ts
 *  - qa_q1am_frame/operation: MonsterFrame/MonsterOperation,
 *    src/content/q1/base/animation.ts
 *  - qa_q1am_monster (+ non-virtual helpers, ctor/capture/restore):
 *    BaseMonster, src/content/q1/base/monsters.ts. Virtual methods
 *    dispatch through ops; the provider supplies the base ops table
 *    (qa_q1am_base.base_ops) until the base port lands.
 *  - qa_q1am_base: Q1Base/MonsterServices subset,
 *    src/content/q1/base/provider.ts
 *  - qa_q1am_context: Q1AddonContext/Q1AddonServices,
 *    src/content/q1/addons/context.ts (methods as fn+ctx)
 *  - projectile/weapon helper hooks on qa_q1am_game: launchSpike,
 *    throwGib, throwHead, spawnMeatSpray, createMissile,
 *    spriteExplosion (src/content/q1/base/projectiles.ts),
 *    fireBullets (src/content/q1/foundation/weapons.ts),
 *    spawnTeleportFog (src/content/q1/foundation/spawns.ts),
 *    spawnBubble (src/content/q1/base/map-entities.ts),
 *    registerMonsterCallbacks (src/content/q1/base/monsters.ts),
 *    monsterFrames lookup (src/content/q1/base/frames.ts)
 *  - qa_q1am_velocity_angles: src/content/q1/missionpacks/types.ts
 *  - campaign flag constants: src/content/q1/addons/campaign.ts
 * Vector math reuses landed qa_add3/qa_sub3/qa_scale3/qa_dot3/
 * qa_length3/qa_cross3_double (src/core/math.ts); normalize/yawFor
 * mirror src/content/q1/foundation/types.ts.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_AI_CONTROLLER_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_AI_CONTROLLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/monsters/authored.h"
#include "qa/content/monsters/target.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/core/math.h"
#include "qa/persistence/value.h"

#define QA_Q1AM_BLOODY_NIGHTMARE_ACTIVE 64
#define QA_Q1AM_BLOODY_NIGHTMARE_DISCOVERED 128
#define QA_Q1AM_BLOODY_NIGHTMARE_NEWGAME 256

qa_vec3 qa_q1am_normalize(qa_vec3 a);
double qa_q1am_yaw_for(qa_vec3 direction);
qa_vec3 qa_q1am_velocity_angles(qa_vec3 velocity);
extern const qa_vec3 QA_Q1AM_ZERO;
extern const qa_bounds QA_Q1AM_POINT;

typedef struct qa_q1am_basis { qa_vec3 forward, right, up; } qa_q1am_basis;
typedef struct qa_q1am_body {
  qa_vec3 origin, angles, velocity;
  qa_bounds bounds;
  bool has_ground; qa_actor_id ground;
} qa_q1am_body;
typedef struct qa_q1am_body_patch {
  bool has_origin, has_angles, has_velocity, has_bounds, has_ground;
  qa_vec3 origin, angles, velocity;
  qa_bounds bounds;
  bool ground_null; qa_actor_id ground;
} qa_q1am_body_patch;
typedef struct qa_q1am_trace {
  double fraction;
  qa_vec3 end, normal;
  bool has_actor; qa_actor_id actor;
  bool start_solid, all_solid, sky, in_open, in_water;
} qa_q1am_trace;
typedef struct qa_q1am_trace_request {
  qa_vec3 start, end;
  qa_bounds bounds;
  bool has_ignore; qa_actor_id ignore;
  bool monsters;
} qa_q1am_trace_request;

/* TS Q1Event / Q1AddonEvent union, flattened; kind selects fields. */
typedef struct qa_q1am_event {
  const char *kind;
  bool has_actor; qa_actor_id actor;
  bool has_player; qa_actor_id player;
  const char *text, *pattern, *id, *style_text;
  double style, count, total, found, track, loop_track;
  double color_start, color_length, color;
  qa_vec3 origin, start, end, direction, angles;
} qa_q1am_event;

/* Named QC closure mirror: only the registered name is retained. */
typedef struct qa_q1am_callback { bool present; char name[160]; } qa_q1am_callback;

typedef struct qa_q1am_state {
  const char *species;
  char mode[16];
  double frame_index;
  const double *sequence; size_t sequence_count;
  double first_frame;
  bool has_enemy; qa_actor_id enemy;
  bool has_old_enemy; qa_actor_id old_enemy;
  char path[160];
  double pause_until, attack_finished, pain_finished, search_until;
  bool death_drop, refired;
} qa_q1am_state;

typedef struct qa_q1am_field { char key[64]; char value[192]; } qa_q1am_field;
typedef struct qa_q1am_ref { char key[64]; bool has; qa_actor_id id; } qa_q1am_ref;

typedef struct qa_q1am_actor {
  qa_owned_actor actor;
  char classname[128], model[192], original_model[192];
  double frame, skin, effects;
  const char *solid, *movement;
  qa_q1am_field *fields; size_t field_count, field_cap;
  qa_q1am_ref *refs; size_t ref_count, ref_cap;
  char target[128], targetname[128], killtarget[128], message[256];
  double delay, spawnflags, sounds, wait, speed, damage, max_health, count, attack_finished;
  bool aimed_damage, activated;
  double next_think;
  qa_q1am_callback think, use, touch, pain, die, blocked, path_end;
  bool has_owner; qa_actor_id owner;
  bool has_activator; qa_actor_id activator;
  qa_vec3 pos1, pos2, dest1, dest2, movedir, mangle, angular_velocity;
  bool has_monster; qa_q1am_state monster;
  const char *projectile;
  double water_level, water_type, movement_flags, ideal_yaw, yaw_speed;
  char attack_state[16];
} qa_q1am_actor;

/* Q1Actor helpers (entity.ts text/number/vector + field/reference maps). */
void qa_q1am_actor_init(qa_q1am_actor *actor, qa_owned_actor owned, const char *classname);
void qa_q1am_actor_destroy(qa_q1am_actor *actor);
const char *qa_q1am_actor_text(const qa_q1am_actor *actor, const char *key);
bool qa_q1am_actor_has_field(const qa_q1am_actor *actor, const char *key);
void qa_q1am_actor_set_field(qa_q1am_actor *actor, const char *key, const char *value);
double qa_q1am_actor_number(const qa_q1am_actor *actor, const char *key);
double qa_q1am_actor_number_or(const qa_q1am_actor *actor, const char *key, double fallback);
qa_vec3 qa_q1am_actor_vector(const qa_q1am_actor *actor, const char *key);
bool qa_q1am_actor_ref(const qa_q1am_actor *actor, const char *key, qa_actor_id *out);
void qa_q1am_actor_set_ref(qa_q1am_actor *actor, const char *key, bool has, qa_actor_id id);
/* String(Math.fround(value)) spelling used by TS field writes. */
void qa_q1am_format_fround(char *out, size_t cap, double value);
void qa_q1am_actor_set_field_number(qa_q1am_actor *actor, const char *key, double value);
bool qa_q1am_same_owned(qa_owned_actor a, qa_owned_actor b);
bool qa_q1am_same_opt(bool has_a, qa_actor_id a, bool has_b, qa_actor_id b);

typedef struct qa_q1am_player {
  qa_owned_actor actor;
  char weapon[64];
  double hostile_until;
  qa_vec3 view_angles;
  struct { char name[40]; double expires; } powerups[16];
  size_t powerup_count;
} qa_q1am_player;
double qa_q1am_player_powerup(const qa_q1am_player *player, const char *name);

typedef struct qa_q1am_species {
  const char *species, *kill_string, *model, *head, *stand, *walk, *run, *sight, *missile, *movement;
  const char *const *classnames; size_t classname_count;
  const char *const *gibs; size_t gib_count;
  double health, gib_health;
  qa_bounds bounds;
  bool melee;
} qa_q1am_species;
extern const qa_q1am_species qa_q1am_base_species[];
extern const size_t qa_q1am_base_species_count;

typedef enum qa_q1am_ai {
  QA_Q1AM_AI_STAND, QA_Q1AM_AI_WALK, QA_Q1AM_AI_RUN, QA_Q1AM_AI_CHARGE_SIDE,
  QA_Q1AM_AI_MELEE_SIDE, QA_Q1AM_AI_CHARGE, QA_Q1AM_AI_MELEE, QA_Q1AM_AI_PAINFORWARD,
  QA_Q1AM_AI_TURN, QA_Q1AM_AI_FACE, QA_Q1AM_AI_PAIN, QA_Q1AM_AI_FORWARD
} qa_q1am_ai;
const char *qa_q1am_ai_name(qa_q1am_ai mode);
typedef enum qa_q1am_op_kind {
  QA_Q1AM_OP_AI, QA_Q1AM_OP_SOUND, QA_Q1AM_OP_SOLID, QA_Q1AM_OP_LIGHTSTYLE, QA_Q1AM_OP_ACTION
} qa_q1am_op_kind;
typedef struct qa_q1am_operation {
  qa_q1am_op_kind kind;
  qa_q1am_ai ai_mode; double distance;
  const char *path, *channel, *name;
  double attenuation;
  bool comparison_greater, has_chance; double chance;
} qa_q1am_operation;
typedef struct qa_q1am_frame {
  const char *name;
  double frame;
  const char *next;
  const qa_q1am_operation *operations; size_t operation_count;
} qa_q1am_frame;
typedef struct qa_q1am_frame_table { const qa_q1am_frame *frames; size_t count; } qa_q1am_frame_table;
const qa_q1am_frame *qa_q1am_frame_table_get(const qa_q1am_frame_table *table, const char *name);

typedef struct qa_q1am_monster qa_q1am_monster;
typedef struct qa_q1am_game qa_q1am_game;
typedef struct qa_q1am_base qa_q1am_base;
typedef struct qa_q1am_context qa_q1am_context;

typedef struct qa_q1am_action_entry {
  const char *name;
  void *ctx;
  void (*fn)(void *ctx, qa_q1am_monster *monster);
} qa_q1am_action_entry;
typedef struct qa_q1am_source {
  const char *callback_prefix;
  const qa_q1am_frame_table *frames;
  const qa_q1am_action_entry *actions; size_t action_count;
} qa_q1am_source;

typedef struct qa_q1am_monster_ops {
  void (*play)(qa_q1am_monster *m, const char *name);
  void (*spawn)(qa_q1am_monster *m);
  void (*start)(qa_q1am_monster *m);
  void (*use)(qa_q1am_monster *m, bool has, qa_actor_id activator);
  void (*pain)(qa_q1am_monster *m, bool has, qa_actor_id attacker, double damage);
  void (*die)(qa_q1am_monster *m, bool has, qa_actor_id attacker);
  void (*awake)(qa_q1am_monster *m, bool has, qa_actor_id activator);
  void (*ai)(qa_q1am_monster *m, qa_q1am_ai mode, double distance);
  void (*run_ai)(qa_q1am_monster *m, double distance);
  void (*found)(qa_q1am_monster *m, qa_actor_id target);
  bool (*find_target)(qa_q1am_monster *m);
  bool (*try_attack)(qa_q1am_monster *m);
  void (*melee_attack)(qa_q1am_monster *m);
  void (*attack_finished)(qa_q1am_monster *m, double seconds);
  void (*sight_sound)(qa_q1am_monster *m);
  qa_q1am_basis (*make_vectors)(qa_q1am_monster *m);
  void (*update_run_knowledge)(qa_q1am_monster *m);
  bool (*check_attack)(qa_q1am_monster *m);
} qa_q1am_monster_ops;

/* instanceof mirror for the TS class checks in action trampolines. */
typedef enum qa_q1am_kind {
  QA_Q1AM_KIND_BASE, QA_Q1AM_KIND_MG3, QA_Q1AM_KIND_FINAL, QA_Q1AM_KIND_GHOST,
  QA_Q1AM_KIND_OLDNEW, QA_Q1AM_KIND_ORB, QA_Q1AM_KIND_SZOMBIE, QA_Q1AM_KIND_DEMODOG,
  QA_Q1AM_KIND_HEAVY, QA_Q1AM_KIND_INFECTED, QA_Q1AM_KIND_ORDINARY, QA_Q1AM_KIND_MG3_ORDINARY
} qa_q1am_kind;

struct qa_q1am_monster {
  qa_q1am_game *game;
  qa_q1am_actor *entity;
  qa_q1am_species spec;
  qa_q1am_base *services;
  const qa_q1am_source *source;
  qa_q1am_state *state;
  char current_frame[80], next_frame[80];
  double in_pain, counter, idle_until, lightning_count;
  bool lefty, sliding, counted_death;
  qa_q1am_monster_ops ops;
  qa_q1am_monster_ops mg3_super; /* ops snapshot before Mg3Monster install */
  qa_q1am_kind kind;
  void *owner; /* outermost derived struct */
};

/* BaseMonster mirror: ctor, checkpoint, non-virtual methods/getters. */
void qa_q1am_monster_init(qa_q1am_monster *m, qa_q1am_game *game, qa_q1am_actor *entity,
                          const qa_q1am_species *spec, qa_q1am_base *services,
                          const qa_q1am_source *source);
qa_save_value *qa_q1am_monster_capture(const qa_q1am_monster *m);
void qa_q1am_monster_restore(qa_q1am_monster *m, const qa_save_reader *reader);
qa_vec3 qa_q1am_monster_origin(const qa_q1am_monster *m);
bool qa_q1am_monster_target(const qa_q1am_monster *m, qa_vec3 *out);
double qa_q1am_monster_distance(const qa_q1am_monster *m);
bool qa_q1am_monster_eye(const qa_q1am_monster *m, qa_actor_id target, qa_vec3 *out);
double qa_q1am_monster_range_distance(const qa_q1am_monster *m, bool has, qa_actor_id target);
bool qa_q1am_monster_visible(const qa_q1am_monster *m, bool has, qa_actor_id target);
void qa_q1am_monster_delay(qa_q1am_monster *m, double seconds);
void qa_q1am_monster_change_yaw(qa_q1am_monster *m);
void qa_q1am_monster_face(qa_q1am_monster *m);
double qa_q1am_monster_melee(qa_q1am_monster *m, double range, double scale, double rolls, bool sight);
void qa_q1am_monster_retaliate(qa_q1am_monster *m, bool has, qa_actor_id attacker);
void qa_q1am_monster_count_kill(qa_q1am_monster *m);
bool qa_q1am_monster_route(const qa_q1am_monster *m, qa_actor_id *out);
void qa_q1am_monster_update_route(qa_q1am_monster *m);
void qa_q1am_monster_move_to_enemy(qa_q1am_monster *m, double distance);
bool qa_q1am_monster_search_for_coop_target(qa_q1am_monster *m);
void qa_q1am_monster_set_frame(char *dst, size_t cap, const char *name);

struct qa_q1am_base {
  void *ctx;
  qa_q1am_game *game;
  qa_q1am_monster_ops base_ops;
  bool (*count_monster_kill)(void *ctx, qa_q1am_monster *monster);
  const char *(*next_hell_knight_melee)(void *ctx);
  void (*finale)(void *ctx, qa_q1am_monster *monster);
  void (*finish_finale)(void *ctx, qa_q1am_monster *monster);
  double (*campaign_read_flags)(void *ctx);
  void (*campaign_write_flags)(void *ctx, double flags);
  void (*level_begin)(void *ctx, const char *map, bool has_cause, qa_actor_id cause);
  qa_q1am_monster *(*monsters_get)(void *ctx, qa_owned_actor actor);
  void (*register_kill_count_rule)(void *ctx, const char *id,
                                   bool (*fn)(void *fn_ctx, qa_q1am_monster *monster), void *fn_ctx);
};

struct qa_q1am_context {
  void *ctx;
  qa_q1am_base *base;
  qa_q1am_game *game;
  const char *program;
  void *services_ctx;
  void (*services_emit)(void *ctx, const qa_q1am_event *event);
  double (*services_cvar)(void *ctx, const char *name);
  void (*set_number)(void *ctx, qa_q1am_actor *entity, const char *name, double value);
  void (*set_vector)(void *ctx, qa_q1am_actor *entity, const char *name, qa_vec3 value);
  void (*set_player_number)(void *ctx, qa_actor_id actor, const char *name, double value);
  bool (*removed_for_runes)(void *ctx, qa_q1am_actor *entity);
  bool (*removed_outside_coop)(void *ctx, qa_q1am_actor *entity);
};

typedef struct qa_q1am_handlers {
  void *ctx;
  void (*action)(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity);
  void (*use)(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, bool has_other, qa_actor_id other, bool has_activator, qa_actor_id activator);
  void (*touch)(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, qa_actor_id other, bool has_normal, qa_vec3 normal);
  void (*pain)(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, bool has_attacker, qa_actor_id attacker, double damage);
  void (*die)(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, bool has_attacker, qa_actor_id attacker);
} qa_q1am_handlers;
typedef struct qa_q1am_state_extension {
  const char *id;
  void *ctx;
  uint8_t *(*capture)(void *ctx, size_t *out_length);
  void (*restore)(void *ctx, const uint8_t *bytes, size_t length);
  void (*clone)(void *ctx, qa_q1am_actor *source, qa_q1am_actor *target);
} qa_q1am_state_extension;
typedef struct qa_q1am_damage_request {
  qa_actor_id target;
  bool has_attacker; qa_actor_id attacker;
} qa_q1am_damage_request;
typedef struct qa_q1am_damage_effects {
  void *ctx;
  bool (*before_health)(void *ctx, const qa_q1am_damage_request *request);
  double (*after_armor)(void *ctx, const qa_q1am_damage_request *request, double take);
} qa_q1am_damage_effects;
typedef struct qa_q1am_monster_lookup {
  void *ctx;
  qa_q1am_monster *(*fn)(void *ctx, qa_q1am_actor *entity);
} qa_q1am_monster_lookup;

struct qa_q1am_game {
  void *ctx;
  double time, total_monsters, killed_monsters, sight_time, force_retouch;
  qa_q1am_actor *world, *sight_entity;
  qa_q1am_basis basis;
  struct { double deathmatch, skill; bool coop; const char *edition; } options;
  size_t (*entity_count)(void *ctx);
  qa_q1am_actor *(*entity_at)(void *ctx, size_t index);
  const qa_monster_mission *(*mission)(void *ctx, qa_actor_id actor);
  void (*named_register)(void *ctx, const char *name, qa_q1am_handlers handlers);
  qa_q1am_callback (*named_action)(void *ctx, qa_q1am_actor *entity, const char *name);
  qa_q1am_callback (*named_use)(void *ctx, qa_q1am_actor *entity, const char *name);
  qa_q1am_callback (*named_touch)(void *ctx, qa_q1am_actor *entity, const char *name);
  qa_q1am_callback (*named_pain)(void *ctx, qa_q1am_actor *entity, const char *name);
  qa_q1am_callback (*named_die)(void *ctx, qa_q1am_actor *entity, const char *name);
  void (*call_action)(void *ctx, qa_q1am_actor *entity, qa_q1am_callback callback);
  const char *(*callback_name)(void *ctx, bool present, const qa_q1am_callback *callback);
  void (*register_spawn)(void *ctx, const char *classname, void (*fn)(void *fn_ctx, qa_q1am_game *game, qa_q1am_actor *entity), void *fn_ctx);
  void (*replace_spawn)(void *ctx, const char *classname, void (*fn)(void *fn_ctx, qa_q1am_game *game, qa_q1am_actor *entity), void *fn_ctx);
  void (*register_state_extension)(void *ctx, qa_q1am_state_extension extension);
  void (*register_damage_source_effects)(void *ctx, const char *id, qa_q1am_damage_effects effects);
  qa_q1am_actor *(*create)(void *ctx, const char *classname);
  void (*spawn_entity)(void *ctx, qa_q1am_actor *entity);
  void (*remove)(void *ctx, qa_q1am_actor *entity);
  bool (*live)(void *ctx, qa_q1am_actor *entity);
  qa_q1am_actor *(*entity)(void *ctx, bool has, qa_actor_id actor);
  bool (*is_player)(void *ctx, bool has, qa_actor_id actor);
  double (*health)(void *ctx, qa_actor_id actor);
  qa_q1am_body (*body)(void *ctx, qa_q1am_actor *entity);
  void (*set_body)(void *ctx, qa_q1am_actor *entity, const qa_q1am_body_patch *patch);
  void (*set_origin)(void *ctx, qa_q1am_actor *entity, qa_vec3 origin);
  void (*set_bounds)(void *ctx, qa_q1am_actor *entity, qa_bounds bounds);
  void (*link)(void *ctx, qa_q1am_actor *entity);
  void (*schedule)(void *ctx, qa_q1am_actor *entity, double delay, qa_q1am_callback callback);
  void (*sound)(void *ctx, qa_q1am_actor *entity, const char *path, const char *channel, double attenuation);
  void (*effect)(void *ctx, const char *effect, qa_vec3 origin, bool has_actor, qa_actor_id actor, double amount);
  qa_q1am_actor **(*find)(void *ctx, const char *targetname, size_t *out_count);
  void (*use_targets)(void *ctx, qa_q1am_actor *entity, bool has_activator, qa_actor_id activator);
  void (*damage)(void *ctx, qa_actor_id target, qa_actor_id inflictor, bool has_attacker, qa_actor_id attacker, double amount);
  bool (*can_damage)(void *ctx, qa_actor_id target, qa_actor_id inflictor);
  void (*radius_damage)(void *ctx, qa_actor_id inflictor, bool has_attacker, qa_actor_id attacker, double amount, bool has_ignore, qa_actor_id ignore, const char *weapon);
  qa_q1am_basis (*make_vectors_raw)(void *ctx, qa_vec3 angles);
  qa_q1am_player *(*player)(void *ctx, qa_actor_id actor);
  bool (*monster_target)(void *ctx, qa_actor_id actor, qa_monster_target_observation *out);
  const qa_q1am_frame *(*base_frame)(void *ctx, const char *name);
  double (*random)(void *ctx);
  qa_q1am_trace (*trace)(void *ctx, const qa_q1am_trace_request *request);
  const char *(*contents)(void *ctx, qa_vec3 point);
  const char *(*classname_of)(void *ctx, qa_actor_id actor);
  bool (*walk_move)(void *ctx, qa_owned_actor actor, double yaw, double distance);
  void (*change_yaw)(void *ctx, qa_owned_actor actor);
  void (*move_to_goal)(void *ctx, qa_owned_actor actor, qa_actor_id goal, double distance);
  bool (*check_bottom)(void *ctx, qa_actor_id actor);
  bool (*check_client)(void *ctx, qa_owned_actor observer, qa_actor_id *out);
  qa_actor_id *(*players)(void *ctx, size_t *out_count);
  void (*emit)(void *ctx, const qa_q1am_event *event);
  bool (*body_read)(void *ctx, qa_actor_id actor, qa_q1am_body *out);
  void (*body_write)(void *ctx, qa_owned_actor actor, const qa_q1am_body *body);
  void (*body_link)(void *ctx, qa_owned_actor actor);
  void (*actors_on_release)(void *ctx, void (*fn)(void *fn_ctx, qa_owned_actor actor), void *fn_ctx);
  bool (*actors_resolve_owned)(void *ctx, qa_actor_id actor, qa_owned_actor *out);
  bool (*actors_resolve_saved)(void *ctx, int64_t slot, int64_t generation, qa_owned_actor *out);
  qa_actor_id *(*observations)(void *ctx, size_t *out_count);
  void (*combat_set_health)(void *ctx, qa_owned_actor actor, double health);
  void (*combat_set_team)(void *ctx, qa_owned_actor actor, const char *team);
  void (*combat_set_can_take_damage)(void *ctx, qa_owned_actor actor, bool can_take_damage);
  bool (*can_take_damage)(void *ctx, qa_actor_id actor);
  void (*combat_set_armor_none)(void *ctx, qa_owned_actor actor);
  size_t (*inventory_count)(void *ctx, qa_actor_id actor);
  const char *(*inventory_item)(void *ctx, qa_actor_id actor, size_t index);
  void (*inventory_set_count)(void *ctx, qa_owned_actor actor, size_t index, double count);
  qa_q1am_actor *(*launch_spike)(void *ctx, qa_q1am_game *game, bool has_owner, qa_actor_id owner, qa_vec3 origin, qa_vec3 velocity);
  qa_q1am_actor *(*throw_gib)(void *ctx, qa_q1am_game *game, qa_vec3 origin, const char *model, double damage);
  void (*throw_head)(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, const char *model, bool has_damage, double damage);
  qa_q1am_actor *(*spawn_meat_spray)(void *ctx, qa_q1am_game *game, qa_q1am_actor *owner, qa_vec3 origin, qa_vec3 velocity);
  qa_q1am_actor *(*create_missile)(void *ctx, qa_q1am_game *game, bool has_owner, qa_actor_id owner, const char *classname, const char *model, qa_vec3 origin, qa_vec3 velocity);
  void (*sprite_explosion)(void *ctx, qa_q1am_game *game, qa_q1am_actor *missile);
  qa_q1am_actor *(*spawn_teleport_fog)(void *ctx, qa_q1am_game *game, qa_vec3 origin);
  qa_q1am_actor *(*spawn_bubble)(void *ctx, qa_q1am_game *game, qa_vec3 origin);
  void (*fire_bullets)(void *ctx, qa_q1am_game *game, qa_owned_actor shooter, qa_vec3 direction, qa_vec3 view_angles, double count, double spread_x, double spread_y, const char *weapon);
  void (*register_monster_callbacks)(void *ctx, qa_q1am_game *game, const char *prefix, qa_q1am_monster_lookup lookup);
};
/* game.makeVectors: stores into basis like the TS. */
qa_q1am_basis qa_q1am_game_make_vectors(qa_q1am_game *game, qa_vec3 angles);
/* entity.damageable get/set (combat-backed in TS). */
bool qa_q1am_actor_damageable(const qa_q1am_game *game, const qa_q1am_actor *entity);
void qa_q1am_actor_set_damageable(qa_q1am_game *game, qa_q1am_actor *entity, bool value);

/* Mg3Monster (this module). Derived structs embed qa_q1am_monster and
 * call install during construction. */
void qa_q1am_mg3_install(qa_q1am_monster *m);
double qa_q1am_mg3_range_category(qa_q1am_monster *m, bool has, qa_actor_id target);
bool qa_q1am_mg3_in_front(qa_q1am_monster *m, qa_actor_id target);
bool qa_q1am_mg3_check_contents_damage(qa_q1am_monster *m);
void qa_q1am_mg3_hunt_target(qa_q1am_monster *m);
void qa_q1am_mg3_path_to_goal(qa_q1am_monster *m, double distance);
void qa_q1am_mg3_source_word(qa_q1am_monster *m, const char *name, double value);

#endif

