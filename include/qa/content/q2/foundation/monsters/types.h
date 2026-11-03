/* Port of src/content/q2/foundation/monsters/types.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_MONSTERS_TYPES_H
#define QA_CONTENT_Q2_FOUNDATION_MONSTERS_TYPES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q2/foundation/monsters/alternate_fly_state.h"
#include "qa/content/q2/foundation/weapons/ballistics.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/world.h"

/* MonsterFrame actions: string | {kind:"nextframe", frame:number|"next"}. */
typedef struct qa_q2m_action {
  bool is_nextframe;
  const char *name;      /* string form */
  double frame;          /* nextframe numeric form */
  bool frame_is_next;    /* frame === "next" */
} qa_q2m_action;
/* MonsterFrame ai: named union | {kind:"source", name}. */
typedef enum qa_q2m_ai_kind {
  QA_Q2M_AI_STAND, QA_Q2M_AI_WALK, QA_Q2M_AI_RUN, QA_Q2M_AI_CHARGE, QA_Q2M_AI_MOVE,
  QA_Q2M_AI_SOLDIER_MOVE, QA_Q2M_AI_TURN, QA_Q2M_AI_NONE, QA_Q2M_AI_SOURCE
} qa_q2m_ai_kind;
const char *qa_q2m_ai_name(qa_q2m_ai_kind kind);
typedef struct qa_q2m_frame {
  qa_q2m_ai_kind ai;
  const char *ai_source_name; /* QA_Q2M_AI_SOURCE */
  double distance;
  const qa_q2m_action *actions; size_t action_count;
  double lerp_frame;
} qa_q2m_frame;
typedef struct qa_q2m_move {
  const char *name;
  double first_frame, last_frame;
  const char *end; /* nullable */
  double sidestep_scale;
  const qa_q2m_frame *frames; size_t frame_count;
} qa_q2m_move;

typedef const char *qa_q2m_kind;
typedef enum qa_q2m_weapon_kind { QA_Q2M_WEAPON_BLASTER, QA_Q2M_WEAPON_SHOTGUN, QA_Q2M_WEAPON_MACHINEGUN } qa_q2m_weapon_kind;
typedef enum qa_q2m_locomotion { QA_Q2M_LOCO_WALK, QA_Q2M_LOCO_FLY, QA_Q2M_LOCO_SWIM, QA_Q2M_LOCO_STATIONARY } qa_q2m_locomotion;
typedef enum qa_q2m_spawned_by { QA_Q2M_SPAWNED_NONE, QA_Q2M_SPAWNED_CARRIER, QA_Q2M_SPAWNED_MEDIC, QA_Q2M_SPAWNED_WIDOW } qa_q2m_spawned_by;
typedef enum qa_q2m_power_armor { QA_Q2M_ARMOR_NONE, QA_Q2M_ARMOR_SCREEN, QA_Q2M_ARMOR_SHIELD } qa_q2m_power_armor;
typedef enum qa_q2m_attack_state {
  QA_Q2M_ATTACK_STRAIGHT, QA_Q2M_ATTACK_SLIDING, QA_Q2M_ATTACK_MELEE, QA_Q2M_ATTACK_MISSILE, QA_Q2M_ATTACK_BLIND
} qa_q2m_attack_state;
const char *qa_q2m_weapon_kind_name(qa_q2m_weapon_kind kind);
const char *qa_q2m_locomotion_name(qa_q2m_locomotion kind);
const char *qa_q2m_attack_state_name(qa_q2m_attack_state state);

typedef struct qa_q2m_sound_target { qa_actor_id actor, owner; qa_vec3 origin; double time; } qa_q2m_sound_target;

typedef struct qa_q2m_state {
  qa_q2m_alternate_fly_state alt;
  qa_q2m_kind kind;
  qa_q2m_weapon_kind weapon;
  qa_q2m_locomotion locomotion;
  bool has_melee, has_ranged_attack, has_idle, has_search, blind_fire;
  bool good_guy, target_anger, ignore_shots, do_not_count;
  qa_q2m_spawned_by spawned_by;
  bool has_commander; qa_actor_id commander;
  double monster_slots, monster_used;
  bool brutal, medic, resurrecting;
  const qa_q2m_move *move;
  const qa_q2m_move *next_move; /* nullable */
  double next_frame, next_move_time, scale, gib_health;
  qa_q2m_power_armor initial_power_armor_type;
  double max_power_armor_power, base_health, health_scaling;
  bool can_take_damage, dead, corpse, gibbed, stand_ground, temporary_stand_ground;
  bool hold_frame, ducked, dodging, charging, manual_steering, combat_point;
  qa_q2m_attack_state attack_state;
  bool lefty;
  double ideal_yaw, yaw_speed, pause_time, idle_time, pain_time, fire_wait, duck_wait;
  double next_duck_time, dodge_time, attack_finished, check_attack_time, strafe_time;
  bool had_visibility, close_sight_tripped;
  /* Rerelease movement memory: badMoveTime (no direct pursuit after a
   * blocked step), reachedHoldCombat/thirdEye (a hold-position combat point
   * was reached), bumpTime (slide-along cooldown), noStep (AI_NOSTEP: a
   * one-unit step), randomChangeTime (the next random heading change). */
  double bad_move_time;
  bool reached_hold_combat, third_eye;
  double bump_time;
  bool no_step;
  double random_change_time;
  double melee_time, search_time, trail_time, show_hostile;
  qa_vec3 last_sighting;
  bool has_saved_goal; qa_vec3 saved_goal;
  bool lost_sight, pursue_next, pursue_temporary, pursuit_last_seen;
  qa_vec3 blind_fire_target;
  double blind_fire_delay;
  bool has_sound_target; qa_q2m_sound_target sound_target;
  bool has_old_enemy; qa_actor_id old_enemy;
  bool has_move_target; qa_actor_id move_target;
  const char *combat_target;
  bool cocked, force_refire;
  double normal_height, water_level, water_type, last_link_count;
  double air_finished, environmental_damage_time, jump_time;
  bool has_flies_time; double flies_time;
} qa_q2m_state;

/* MonsterWeapons = Pick<Q2Ballistics, ...> as fn+ctx. */
typedef struct qa_q2m_weapons {
  void *ctx;
  void (*fire_bullet)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                      double damage, double kick, double hspread, double vspread, double mod);
  void (*fire_shotgun)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                       double damage, double kick, double hspread, double vspread, double count, double mod);
  qa_q2_entity *(*fire_blaster)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                                 double damage, double speed, double effects, bool hyper, bool has_mod, double mod);
  bool (*fire_hit)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 aim, double damage, double kick);
  qa_q2_entity *(*fire_rocket)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                                double damage, double speed, double radius, double radius_damage);
  qa_q2_entity *(*fire_grenade)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                                 double damage, double speed, double timer, double radius, bool hand, bool held, bool monster,
                                 const qa_q2w_grenade_adjustment *adjustment);
  void (*fire_rail)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction, double damage, double kick);
  qa_q2_entity *(*fire_bfg)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                             double damage, double speed, double radius);
} qa_q2m_weapons;
qa_q2m_weapons qa_q2m_weapons_from_ballistics(qa_q2w_ballistics *ballistics);

typedef enum qa_q2m_combat_rules { QA_Q2M_RULES_BASE, QA_Q2M_RULES_ROGUE } qa_q2m_combat_rules;
typedef struct qa_q2m_before_move_result { bool handled; qa_vec3 displacement; } qa_q2m_before_move_result;

struct qa_q2m_context;
typedef struct qa_q2m_context {
  qa_q2_entity *entity;
  qa_q2_game_services *game;
  qa_q2m_state *state;
  qa_q2m_weapons weapons;
  void *impl_ctx;
  qa_q2m_combat_rules (*source_combat_rules)(void *impl, struct qa_q2m_context *context);
  qa_q2m_before_move_result (*before_source_move)(void *impl, struct qa_q2m_context *context, qa_vec3 displacement);
  bool (*accepts_source_ground_move)(void *impl, struct qa_q2m_context *context, qa_vec3 origin);
  bool (*consume_source_blocked)(void *impl, struct qa_q2m_context *context);
  bool (*run_hint_path)(void *impl, struct qa_q2m_context *context, double distance);
  bool (*check_lost_hint_path)(void *impl, struct qa_q2m_context *context);
  /* callback: "monster_dead_think"|"M_FliesOn"|"M_FliesOff" */
  void (*schedule)(void *impl, struct qa_q2m_context *context, double delay_seconds, const char *callback);
  void (*set_move)(void *impl, struct qa_q2m_context *context, const char *name, bool has_immediate, bool immediate);
  void (*stand)(void *impl, struct qa_q2m_context *context);
  void (*walk)(void *impl, struct qa_q2m_context *context);
  void (*run)(void *impl, struct qa_q2m_context *context);
  void (*attack)(void *impl, struct qa_q2m_context *context);
  void (*dispatch)(void *impl, struct qa_q2m_context *context, const char *callback);
  bool (*find_target)(void *impl, struct qa_q2m_context *context);
  bool (*check_attack)(void *impl, struct qa_q2m_context *context, double distance);
  bool (*move_to_goal)(void *impl, struct qa_q2m_context *context, double distance);
  void (*dodge)(void *impl, struct qa_q2m_context *context, qa_actor_id attacker, double eta_seconds,
                const qa_trace_result *trace, bool has_gravity, bool gravity);
  void (*melee)(void *impl, struct qa_q2m_context *context);
  bool (*blocked)(void *impl, struct qa_q2m_context *context, double distance);
  const char *(*platform_state)(void *impl, struct qa_q2m_context *context, qa_actor_id actor); /* NULL = null */
  void (*idle)(void *impl, struct qa_q2m_context *context);
  void (*search)(void *impl, struct qa_q2m_context *context);
} qa_q2m_context;

typedef struct qa_q2m_handler {
  void *ctx;
  void (*fn)(void *ctx, qa_q2m_context *context);
} qa_q2m_handler;
typedef struct qa_q2m_reaction_handler {
  void *ctx;
  void (*pain)(void *ctx, qa_q2m_context *context, qa_pain_reaction reaction);
  void (*die)(void *ctx, qa_q2m_context *context, qa_death_reaction reaction);
} qa_q2m_reaction_handler;

typedef struct qa_q2m_source_combat_hooks {
  void *ctx;
  bool (*is_good_guy)(void *ctx, qa_q2_entity *entity);
  bool (*before_react)(void *ctx, qa_q2m_context *context, qa_actor_id attacker);
  void (*before_killed)(void *ctx, qa_q2m_context *context);
  bool (*recover_enemy)(void *ctx, qa_q2m_context *context, qa_actor_id *out); /* false = null */
  qa_q2m_before_move_result (*before_move)(void *ctx, qa_q2m_context *context, qa_vec3 displacement);
  bool (*accepts_ground_move)(void *ctx, qa_q2m_context *context, qa_vec3 origin);
  bool (*consume_blocked)(void *ctx, qa_q2m_context *context);
} qa_q2m_source_combat_hooks;
typedef struct qa_q2m_hint_hooks {
  void *ctx;
  bool (*run)(void *ctx, qa_q2m_context *context, double distance);
  bool (*check_lost)(void *ctx, qa_q2m_context *context);
  void (*stop)(void *ctx, qa_q2m_context *context);
} qa_q2m_hint_hooks;

typedef struct qa_q2m_named_handler { const char *name; qa_q2m_handler handler; } qa_q2m_named_handler;
typedef struct qa_q2m_named_ai {
  const char *name;
  void *ctx;
  void (*fn)(void *ctx, qa_q2m_context *context, double distance);
} qa_q2m_named_ai;
typedef enum qa_q2m_start_mode { QA_Q2M_START_AUTOMATIC, QA_Q2M_START_MANUAL } qa_q2m_start_mode;

typedef struct qa_q2m_definition {
  const char *classname, *kind, *model;
  double health, gib_health, mass;
  qa_bounds bounds;
  double scale;
  bool has_view_height; double view_height;
  bool has_yaw_speed; double yaw_speed;
  bool has_locomotion; qa_q2m_locomotion locomotion;
  /* TS soldier?: a soldier family member, the weapon its shared callbacks
   * fire, the model skin it spawns with and the entity count that selects them. */
  bool has_soldier; qa_q2m_weapon_kind soldier_weapon; double soldier_skin, soldier_count;
  const char *initial_move;
  const qa_q2m_move *moves; size_t move_count;
  const qa_q2m_named_handler *callbacks; size_t callback_count;
  qa_q2m_handler stand, walk, run, attack;
  qa_q2m_handler sight, idle, search, melee; /* fn NULL = absent */
  bool has_ranged_attack_flag; bool has_ranged_attack;
  bool has_blind_fire_flag; bool blind_fire;
  qa_q2m_reaction_handler reactions; /* pain may be NULL; die required */
  const qa_q2m_named_ai *ai; size_t ai_count;
  bool has_source_callbacks; qa_q2_callback_definitions source_callbacks;
  qa_q2m_handler initialize, after_spawn, restore; /* fn NULL = absent */
  void *start_mode_ctx;
  qa_q2m_start_mode (*start_mode)(void *ctx, qa_q2m_context *context); /* NULL = absent */
  void *duck_ctx;
  bool (*duck)(void *ctx, qa_q2m_context *context, double eta);
  void *sidestep_ctx;
  bool (*sidestep)(void *ctx, qa_q2m_context *context);
  void *dodge_ctx;
  void (*dodge)(void *ctx, qa_q2m_context *context, qa_actor_id attacker, double eta, const qa_trace_result *trace, bool gravity);
  void *blocked_ctx;
  bool (*blocked)(void *ctx, qa_q2m_context *context, double distance);
  void *check_attack_ctx;
  bool (*check_attack)(void *ctx, qa_q2m_context *context);
} qa_q2m_definition;

/* TS recordAt: validates index (RangeError text) and returns it. */
size_t qa_q2m_record_at(size_t count, double index);
#endif
