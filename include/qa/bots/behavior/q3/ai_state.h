/* Port of src/bots/behavior/q3/ai-state.ts. PlayerState/UserCommand (owner:
 * content/q3/base/shared, other slices) mirrored as scalar fields needed by
 * bots-a AI logic. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_STATE_H
#define QA_BOTS_BEHAVIOR_Q3_AI_STATE_H
#include "qa/bots/behavior/library/goals.h"
#include "qa/bots/behavior/orders.h"
#include "qa/contracts/math.h"
#include <stdbool.h>
#include <stdint.h>
#define QA_BOT_STATE_SOURCE_BYTES                                                                  \
  (57 * 4 + 56 * 4 + 12 * 12 + 5 * 4 + 128 * (56 + 512 + 20 + 4) + 6 * 4 + 2 * 256 * 4 + 14 * 4 +  \
   64 * (4 + 56 + 4) + 4)
typedef struct qa_bot_settings_bots_a {
  char characterfile[144];
  double skill;
  char team[32];
} qa_bot_settings_bots_a;
typedef enum qa_ai_node_bots_a {
  QA_AI_NODE_NONE,
  QA_AI_NODE_INTERMISSION,
  QA_AI_NODE_OBSERVER,
  QA_AI_NODE_RESPAWN,
  QA_AI_NODE_STAND,
  QA_AI_NODE_SEEK_NBG,
  QA_AI_NODE_SEEK_LTG,
  QA_AI_NODE_BATTLE_FIGHT,
  QA_AI_NODE_BATTLE_CHASE,
  QA_AI_NODE_BATTLE_RETREAT,
  QA_AI_NODE_BATTLE_NBG,
  QA_AI_NODE_SEEK_ACTIVATE
} qa_ai_node_bots_a;
typedef qa_bot_goal qa_bot_goal_state_bots_a;
typedef struct qa_bot_waypoint_bots_a {
  bool inuse;
  char name[64];
  qa_bot_goal goal;
  struct qa_bot_waypoint_bots_a *next, *prev;
} qa_bot_waypoint_bots_a;
typedef struct qa_bot_activate_goal_bots_a {
  bool inuse;
  qa_bot_goal goal;
  double time, start_time, just_used_time;
  bool shoot;
  int64_t weapon;
  qa_vec3 target, origin;
  int64_t areas[32];
  int64_t num_areas;
  bool areas_disabled;
  struct qa_bot_activate_goal_bots_a *next;
} qa_bot_activate_goal_bots_a;
void qa_bot_goal_state_copy_from(qa_bot_goal *dst, qa_bot_goal src);
void qa_bot_goal_state_clear(qa_bot_goal *g);
typedef struct qa_bot_state_bots_a {
  bool has_scripted_order;
  qa_bot_order_state scripted_order;
  bool inuse;
  double bot_think_residual;
  int64_t client, entity_num;
  int64_t pm_type, pm_flags;
  qa_vec3 ps_origin, ps_velocity, view_angles_ps;
  int64_t weapon_ps, health_stat, armor_stat, team_ps;
  int64_t last_eflags;
  qa_bot_settings_bots_a settings;
  qa_ai_node_bots_a ai_node;
  double think_time;
  qa_vec3 origin, velocity;
  int64_t presence_type;
  qa_vec3 eye;
  int64_t area_num;
  double inventory[204];
  int64_t tfl, flags;
  bool respawn_wait;
  int64_t last_health, last_killed_player, last_killed_by, bot_death_type, enemy_death_type;
  bool bot_suicide, enemy_suicide;
  int64_t setup_count;
  bool map_restart, enter_game_chat;
  int64_t num_deaths, num_kills, revenge_enemy, revenge_kills;
  int64_t last_frame_health, last_hit_count, chat_to, walker;
  double ltime, enter_game_time, ltg_time, nbg_time, respawn_time, respawn_chat_time, chase_time,
      enemy_visible_time, check_time, stand_time;
  double last_chat_time, kamikaze_time, invulnerability_time, stand_find_enemy_time,
      attack_strafe_time, attack_crouch_time, attack_chase_time, attack_jump_time;
  double enemy_sight_time, enemy_death_time, enemy_position_time, defend_away_time,
      defend_away_range, rush_base_away_time, attack_away_time, harvest_away_time;
  double ctf_roam_time, killed_enemy_time, arrive_time, last_air_time, teleport_time, camp_time,
      camp_range, weapon_change_time;
  double fire_throttle_wait_time, fire_throttle_shoot_time, not_blocked_time,
      blocked_by_avoid_spot_time, predict_obstacles_time;
  int64_t predict_obstacles_goal_area;
  qa_vec3 aim_target, enemy_velocity, enemy_origin;
  int64_t kamikaze_body;
  int64_t prox_mines[64];
  int64_t num_prox_mines;
  int64_t enemy, character, goal_handle, move_handle, weapon_handle, chat_handle;
  qa_vec3 viewangles, ideal_viewangles, viewangle_speed;
  qa_bot_goal ltg, nbg, camp_goal;
  int64_t ltg_type, team_task_preference, formation_space;
  char subteam[64], team_leader[64], last_ordered_task[128];
  double team_task_time, order_time;
  int64_t desired_weapon, current_weapon_mode;
  double reached_goal_time, blocked_time;
} qa_bot_state_bots_a;
typedef struct qa_bot_state_store_bots_a {
  qa_bot_state_bots_a *states;
  int64_t capacity;
} qa_bot_state_store_bots_a;
bool qa_bot_state_store_bots_a_init(qa_bot_state_store_bots_a *s, int64_t capacity);
void qa_bot_state_store_bots_a_destroy(qa_bot_state_store_bots_a *s);
qa_bot_state_bots_a *qa_bot_state_store_get(qa_bot_state_store_bots_a *s, int64_t client);
void qa_copy_bot_player_state_bots_a(qa_bot_state_bots_a *dst, const qa_bot_state_bots_a *src);
#endif
