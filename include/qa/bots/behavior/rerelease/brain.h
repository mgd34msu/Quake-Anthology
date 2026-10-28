/* Port of src/bots/behavior/rerelease/brain.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_BRAIN_H
#define QA_BOTS_BEHAVIOR_RERELEASE_BRAIN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/bots/behavior/rerelease/aim.h"
#include "qa/bots/behavior/rerelease/data/botdata.h"
#include "qa/bots/behavior/rerelease/data/knowledge.h"
#include "qa/bots/behavior/rerelease/math.h"
#include "qa/bots/behavior/rerelease/nav.h"
#include "qa/bots/behavior/rerelease/path_follow.h"
#include "qa/bots/behavior/rerelease/rng.h"
#include "qa/bots/behavior/rerelease/senses.h"
#include "qa/bots/behavior/rerelease/world.h"

enum { QA_BOT_GOAL_ERROR = 0, QA_BOT_GOAL_SUCCESS = 1, QA_BOT_GOAL_IN_PROGRESS = 2 };
typedef struct qa_bot_chat_event {
  const char *locstring, *type; double delay_ms; bool team_only;
} qa_bot_chat_event;
typedef struct qa_bot_movement_projection {
  double gravity, jump_velocity, jump_air_seconds, maximum_landing_rise, start_above;
  qa_bot_vec3 body_mins, body_maxs;
} qa_bot_movement_projection;
typedef struct qa_bot_brain_config {
  qa_bot_knowledge *knowledge;
  const char *skill;
  qa_bot_random_t rng;
  qa_bot_game_mode game_mode; /* owned game_type string */
  const qa_character_entry *character; /* NULL = TS undefined */
  bool has_max_health; double max_health;
  void *on_chat_ctx; void (*on_chat)(void *ctx, qa_bot_chat_event event);
  void *weapon_impulse_ctx; double (*weapon_impulse)(void *ctx, double weapon_number); /* NULL = undefined */
  void *on_weapon_select_ctx; void (*on_weapon_select)(void *ctx, double weapon_number);
  bool has_run_speed; double run_speed;
  bool has_walk_speed; double walk_speed;
  bool has_movement; qa_bot_movement_projection movement;
  void *human_teammate_near_ctx; bool (*human_teammate_near)(void *ctx);
} qa_bot_brain_config;

typedef struct qa_bot_explicit_goal {
  bool objective_owner; /* false external, true objective */
  bool is_entity;
  qa_bot_vec3 point;
  int64_t entity_id;
} qa_bot_explicit_goal;
typedef struct qa_bot_id_time { int64_t entity; double time; } qa_bot_id_time;
typedef struct qa_bot_id_origin { int64_t entity; qa_bot_vec3 origin; } qa_bot_id_origin;
typedef struct qa_bot_awareness_entry { int64_t entity; qa_bot_awareness value; } qa_bot_awareness_entry;

typedef struct qa_bot_brain_memory {
  double trigger_weapon, trigger_held_since, trigger_ready_at;
  qa_bot_aim_state aim;
  qa_bot_path_state path_state;
  qa_nav_path owned_path; bool has_owned_path; /* backing for path_state.path */
  qa_bot_awareness *awareness; size_t awareness_count, awareness_cap; /* keyed by value.id */
  double target_id;
  bool has_goal_point; qa_bot_vec3 goal_point;
  double goal_entity_id;
  qa_bot_id_time *unreachable_until; size_t unreachable_count, unreachable_cap;
  double stuck_trips; bool goal_is_live;
  double unstick_until, press_until, unstick_side;
  bool has_explicit_goal; qa_bot_explicit_goal explicit_goal;
  bool explicit_goal_done, explicit_goal_failed;
  bool has_wedge_origin; qa_bot_vec3 wedge_origin; double wedge_since;
  bool has_last_safe_origin; qa_bot_vec3 last_safe_origin;
  bool has_rest_point; qa_bot_vec3 rest_point; double rest_until;
  double guard_refusals; bool last_guard_refused; double gap_jumps, hazard_frames;
  qa_bot_id_origin *objective_home; size_t objective_home_count, objective_home_cap;
  bool has_own_objective_home; qa_bot_vec3 own_objective_home;
  bool has_enemy_objective_home; qa_bot_vec3 enemy_objective_home;
  char *objective_role; /* owned */
  bool touch_goal, hold_position;
  bool has_gate_shoot_at; qa_bot_vec3 gate_shoot_at; double gate_fired_at;
  bool coop_regrouping; double coop_regroup_at, coop_regroup_until;
  char **said_this_level; size_t said_count, said_cap;
  bool level_started; double check_six_until, check_six_next_at;
  bool has_roam_point; qa_bot_vec3 roam_point; double roam_until;
  qa_bot_usercmd last_cmd;
  bool spawned_once; double last_weapon_number;
  double dead_since, respawn_wait; bool respawn_press;
} qa_bot_brain_memory;

/* Checkpoint form: deep copies; strings/paths owned. */
typedef struct qa_bot_brain_checkpoint_memory {
  double trigger_weapon, trigger_held_since, trigger_ready_at, target_id;
  bool has_goal_point; qa_bot_vec3 goal_point;
  double goal_entity_id, stuck_trips; bool goal_is_live;
  double unstick_until, press_until, unstick_side;
  bool explicit_goal_done, explicit_goal_failed;
  bool has_wedge_origin; qa_bot_vec3 wedge_origin; double wedge_since;
  bool has_last_safe_origin; qa_bot_vec3 last_safe_origin;
  bool has_rest_point; qa_bot_vec3 rest_point; double rest_until;
  double guard_refusals; bool last_guard_refused; double gap_jumps, hazard_frames;
  bool has_own_objective_home; qa_bot_vec3 own_objective_home;
  bool has_enemy_objective_home; qa_bot_vec3 enemy_objective_home;
  char *objective_role;
  bool touch_goal, hold_position;
  bool has_gate_shoot_at; qa_bot_vec3 gate_shoot_at; double gate_fired_at;
  bool coop_regrouping; double coop_regroup_at, coop_regroup_until;
  bool level_started; double check_six_until, check_six_next_at;
  bool has_roam_point; qa_bot_vec3 roam_point; double roam_until;
  bool spawned_once; double last_weapon_number, dead_since, respawn_wait; bool respawn_press;
  qa_bot_aim_state aim;
  bool has_path; qa_nav_path path;
  int64_t path_index; qa_bot_vec3 stuck_origin; double stuck_since; int64_t stuck_count;
  double lift_wait_since, lift_wait_z, jump_ready_at, planned_at;
  qa_bot_awareness_entry *awareness; size_t awareness_count;
  qa_bot_id_time *unreachable_until; size_t unreachable_count;
  qa_bot_id_origin *objective_home; size_t objective_home_count;
  char **said_this_level; size_t said_count;
  bool has_explicit_goal; qa_bot_explicit_goal explicit_goal;
  qa_bot_usercmd last_cmd;
} qa_bot_brain_checkpoint_memory;
typedef struct qa_bot_brain_checkpoint {
  int64_t version;
  char *skill;
  qa_bot_game_mode game_mode;
  qa_bot_brain_checkpoint_memory memory;
} qa_bot_brain_checkpoint;
void qa_bot_brain_checkpoint_destroy(qa_bot_brain_checkpoint *checkpoint);

typedef struct qa_bot_brain {
  qa_bot_brain_config config;
  const qa_bot_skill_settings *settings;
  qa_bot_brain_memory state;
} qa_bot_brain;
/* Config is copied (game_mode string duplicated). False only on OOM;
 * no available skill aborts with the TS Error text. */
bool qa_bot_brain_init(qa_bot_brain *brain, qa_bot_brain_config config);
void qa_bot_brain_destroy(qa_bot_brain *brain);
bool qa_bot_brain_checkpoint_make(qa_bot_brain *brain, qa_bot_brain_checkpoint *out);
/* Wrong version/skill aborts with the TS Error text. False only on OOM. */
bool qa_bot_brain_restore(qa_bot_brain *brain, const qa_bot_brain_checkpoint *checkpoint);
void qa_bot_brain_request_move_to_point(qa_bot_brain *brain, qa_bot_vec3 point);
void qa_bot_brain_set_objective_goal(qa_bot_brain *brain, const qa_bot_vec3 *point);
void qa_bot_brain_request_follow_entity(qa_bot_brain *brain, double entity_id, qa_bot_vec3 origin);
int64_t qa_bot_brain_goal_status(qa_bot_brain *brain);
void qa_bot_brain_clear_explicit_goal(qa_bot_brain *brain);
void qa_bot_brain_reset_for_level(qa_bot_brain *brain);
/* Takes ownership of mode's owned string copy semantics: duplicates. False on OOM. */
bool qa_bot_brain_set_game_mode(qa_bot_brain *brain, qa_bot_game_mode mode);
const qa_bot_awareness *qa_bot_brain_awareness_of(qa_bot_brain *brain, int64_t id);
double qa_bot_brain_current_target(qa_bot_brain *brain);
const qa_nav_path *qa_bot_brain_current_path(qa_bot_brain *brain);
int64_t qa_bot_brain_current_path_index(qa_bot_brain *brain);
void qa_bot_brain_emit_chat(qa_bot_brain *brain, const char *type);
qa_bot_usercmd qa_bot_brain_think(qa_bot_brain *brain, qa_bot_world *world);
qa_bot_usercmd qa_bot_brain_last_usercmd(qa_bot_brain *brain);
#endif
