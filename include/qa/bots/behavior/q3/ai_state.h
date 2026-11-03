/*
 * Port of src/bots/behavior/q3/ai-state.ts (game/ai_main.h bot_state_t).
 *
 * TS binds every BotState field to source bytes through DataView accessors. C
 * overlays the same 9088-byte release32 layout directly:
 *  - qa_bot_state_source is bot_state_t. Offsets are _Static_assert'ed in
 *    ai_state.c. Fields hold exactly what TS DataView stores (int32/float), and
 *    raw overflow writes such as copy_team_leader_with_overflow hit the same
 *    neighbouring cells. Host is little-endian, as the DataView calls assume.
 *  - qa_bot_state_bots_a is the TS object: `src` is the borrowed record in the
 *    game memory pool (or owned for a diagnostic state), and the typed
 *    pointers TS keeps outside the bytes are direct members. Read source fields
 *    as state->src->ltg_type; read typed ones as state->ai_node.
 *  - Vector cells are qa_bot_fvec3: qa_bot_v3()/qa_bot_fv3() convert to qa_vec3.
 *    Storing through qa_bot_fv3 is the TS float32 store.
 *  - TS enum-checked getters are the *_checked functions below; a plain field
 *    read is the raw cell.
 *  - Text getters: qa_bot_state_text bounds-checks like TS bindSourceText.
 * GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_STATE_H
#define QA_BOTS_BEHAVIOR_Q3_AI_STATE_H
#include "qa/bots/behavior/library/goals.h"
#include "qa/bots/behavior/orders.h"
#include "qa/bots/behavior/q3/ai_definitions.h"
#include "qa/bots/behavior/q3/movement_state.h"
#include "qa/content/q3/base/game/memory.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/contracts/math.h"
#include "qa/persistence/value.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define QA_BOT_STATE_SOURCE_BYTES                                                                  \
  (57 * 4 + 56 * 4 + 12 * 12 + 5 * 4 + (1024 + 256 + 64) * 4 + 5 * 56 + 8 * 244 + 80 + 468 + 24 +  \
   292)

typedef struct qa_bot_ivec3 {
  int32_t x, y, z;
} qa_bot_ivec3;

/* TS BotSettings value, as passed to setup. */
typedef struct qa_bot_settings_bots_a {
  char characterfile[QA_BOT_SETTINGS_PATH_LENGTH];
  double skill;
  char team[QA_BOT_SETTINGS_PATH_LENGTH];
} qa_bot_settings_bots_a;

/* The eleven implemented AINode functions; NONE is the distinct source NULL. */
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
/* TS AiNode string ("seek-ltg", ...); NULL for NONE. */
const char *qa_ai_node_name_bots_a(qa_ai_node_bots_a node);

typedef enum qa_bot_setup_stage_bots_a {
  QA_BOT_SETUP_ALLOCATED,
  QA_BOT_SETUP_CHARACTER,
  QA_BOT_SETUP_SETTINGS,
  QA_BOT_SETUP_GOAL_STATE,
  QA_BOT_SETUP_ITEM_WEIGHTS,
  QA_BOT_SETUP_WEAPON_STATE,
  QA_BOT_SETUP_WEAPON_WEIGHTS,
  QA_BOT_SETUP_CHAT_STATE,
  QA_BOT_SETUP_CHAT_FILE,
  QA_BOT_SETUP_CHAT_GENDER,
  QA_BOT_SETUP_PUBLISHED,
  QA_BOT_SETUP_MOVE_STATE,
  QA_BOT_SETUP_WALKER,
  QA_BOT_SETUP_COUNTED,
  QA_BOT_SETUP_SCHEDULED,
  QA_BOT_SETUP_INTERBRED,
  QA_BOT_SETUP_SESSION,
  QA_BOT_SETUP_AAS /* failed only */
} qa_bot_setup_stage_bots_a;
typedef enum qa_bot_setup_kind_bots_a {
  QA_BOT_SETUP_EMPTY,
  QA_BOT_SETUP_SETTING_UP,
  QA_BOT_SETUP_FAILED,
  QA_BOT_SETUP_COMPLETE
} qa_bot_setup_kind_bots_a;
/* TS BotSetupProgress. error_code holds for failed item-weights/weapon-weights/chat-file. */
typedef struct qa_bot_setup_progress_bots_a {
  qa_bot_setup_kind_bots_a kind;
  qa_bot_setup_stage_bots_a stage;
  int64_t error_code;
} qa_bot_setup_progress_bots_a;

/* Inline bot_goal_t cell (TS BotGoalState): 56 bytes. */
typedef struct qa_bot_goal_state_bots_a {
  qa_bot_fvec3 origin;
  int32_t area;
  qa_bot_fvec3 mins, maxs;
  int32_t entity, number, flags, item_info;
} qa_bot_goal_state_bots_a;
/* TS copyFrom: float32/int32 stores from a canonical goal. */
void qa_bot_goal_state_copy_from(qa_bot_goal_state_bots_a *dst, qa_bot_goal src);
void qa_bot_goal_state_clear(qa_bot_goal_state_bots_a *goal);
/* The BotGoal contract view of a cell. */
qa_bot_goal qa_bot_goal_state_get(const qa_bot_goal_state_bots_a *goal);

/* TS BotWaypoint. */
typedef struct qa_bot_waypoint_bots_a {
  bool inuse;
  char name[64];
  qa_bot_goal_state_bots_a goal;
  struct qa_bot_waypoint_bots_a *next, *prev;
} qa_bot_waypoint_bots_a;

/* bot_activategoal_t: 244 bytes; the source next pointer cell stays zero. */
typedef struct qa_bot_activate_goal_source {
  int32_t inuse;
  qa_bot_goal_state_bots_a goal;
  float time, start_time, just_used_time;
  int32_t shoot, weapon;
  qa_bot_fvec3 target, origin;
  int32_t areas[QA_MAX_ACTIVATEAREAS];
  int32_t num_areas, areas_disabled;
  int32_t next_cell;
} qa_bot_activate_goal_source;
/* TS BotActivateGoal: source cell plus typed next. A standalone goal from
 * qa_bot_activate_goal_init owns its cell, so do not copy this struct by value. */
typedef struct qa_bot_activate_goal_bots_a {
  qa_bot_activate_goal_source *src;
  struct qa_bot_activate_goal_bots_a *next;
  qa_bot_activate_goal_source own;
} qa_bot_activate_goal_bots_a;
void qa_bot_activate_goal_init(qa_bot_activate_goal_bots_a *goal);
void qa_bot_activate_goal_copy_from(qa_bot_activate_goal_bots_a *dst,
                                    const qa_bot_activate_goal_bots_a *src);
void qa_bot_activate_goal_clear(qa_bot_activate_goal_bots_a *goal);

/* q_shared.h playerState_t: 468 bytes. */
typedef struct qa_bot_player_state_source {
  int32_t command_time, pm_type, bob_cycle, pm_flags, pm_time;
  qa_bot_fvec3 origin, velocity;
  int32_t weapon_time, gravity, speed;
  qa_bot_ivec3 delta_angles;
  int32_t ground_entity_num, legs_timer, legs_anim, torso_timer, torso_anim, movement_dir;
  qa_bot_fvec3 grapple_point;
  int32_t e_flags, event_sequence;
  int32_t events[2], event_parms[2];
  int32_t external_event, external_event_parm, external_event_time, client_num;
  int32_t weapon, weapon_state;
  qa_bot_fvec3 viewangles;
  int32_t viewheight, damage_event, damage_yaw, damage_pitch, damage_count;
  int32_t stats[16], persistant[16], powerups[16], ammo[16];
  int32_t generic1, loop_sound, jumppad_ent, ping, pmove_framecount, jumppad_frame,
      entity_event_sequence;
} qa_bot_player_state_source;

/* q_shared.h usercmd_t: 24 bytes. */
typedef struct qa_bot_user_command_source {
  int32_t server_time;
  qa_bot_ivec3 angles;
  int32_t buttons;
  uint8_t weapon;
  int8_t forwardmove, rightmove, upmove;
} qa_bot_user_command_source;

/* ai_main.h bot_state_t, release32. Pointer cells remain zero source bytes. */
typedef struct qa_bot_state_source {
  int32_t inuse, bot_think_residual, client, entity_num;
  qa_bot_player_state_source cur_ps;
  int32_t last_eflags;
  qa_bot_user_command_source last_ucmd;
  int32_t entity_event_time[QA_Q3_MAX_GENTITIES];
  char characterfile[QA_BOT_SETTINGS_PATH_LENGTH];
  float skill;
  char team[QA_BOT_SETTINGS_PATH_LENGTH];
  int32_t ai_node_cell;
  float think_time;
  qa_bot_fvec3 origin, velocity;
  int32_t presence_type;
  qa_bot_fvec3 eye;
  int32_t area_num;
  int32_t inventory[QA_Q3_BASE_MAX_ITEMS];
  int32_t tfl, flags, respawn_wait, last_health, last_killed_player, last_killed_by, bot_death_type,
      enemy_death_type, bot_suicide, enemy_suicide, setup_count, map_restart, enter_game_chat,
      num_deaths, num_kills, revenge_enemy, revenge_kills, last_frame_health, last_hit_count, chat_to;
  float walker, ltime, enter_game_time, ltg_time, nbg_time, respawn_time, respawn_chat_time,
      chase_time, enemy_visible_time, check_time, stand_time, last_chat_time, kamikaze_time,
      invulnerability_time, stand_find_enemy_time, attack_strafe_time, attack_crouch_time,
      attack_chase_time, attack_jump_time, enemy_sight_time, enemy_death_time, enemy_position_time,
      defend_away_time, defend_away_range, rush_base_away_time, attack_away_time, harvest_away_time,
      ctf_roam_time, killed_enemy_time, arrive_time, last_air_time, teleport_time, camp_time,
      camp_range, weapon_change_time, fire_throttle_wait_time, fire_throttle_shoot_time,
      not_blocked_time, blocked_by_avoid_spot_time, predict_obstacles_time;
  int32_t predict_obstacles_goal_area_num;
  qa_bot_fvec3 aim_target, enemy_velocity, enemy_origin;
  int32_t kamikaze_body;
  int32_t prox_mines[QA_MAX_PROXMINES_BOTS_A];
  int32_t num_prox_mines, character, ms, gs, cs, ws, enemy, last_enemy_area_num;
  qa_bot_fvec3 last_enemy_origin;
  int32_t weapon_num;
  qa_bot_fvec3 viewangles, ideal_viewangles, viewangle_speed;
  int32_t ltg_type, teammate, decisionmaker, ordered;
  float order_time;
  int32_t own_decision_time;
  qa_bot_goal_state_bots_a team_goal, alt_route_goal;
  float reached_alt_route_goal_time, team_message_time, team_goal_time, teammate_visible_time;
  int32_t team_task_preference, last_goal_decisionmaker, last_goal_ltg_type, last_goal_teammate;
  qa_bot_goal_state_bots_a last_goal_team_goal;
  int32_t lead_teammate;
  qa_bot_goal_state_bots_a lead_team_goal;
  float lead_time, lead_visible_time, lead_message_time, lead_backup_time;
  char team_leader[32];
  float ask_team_leader_time, become_team_leader_time, team_give_orders_time,
      last_flag_capture_time;
  int32_t num_teammates, red_flag_status, blue_flag_status, neutral_flag_status,
      flag_status_changed, force_orders, flag_carrier, ctf_strategy;
  char subteam[32];
  float formation_dist;
  char formation_teammate[16];
  float formation_angle;
  qa_bot_fvec3 formation_dir, formation_origin;
  qa_bot_goal_state_bots_a formation_goal;
  int32_t activate_stack_cell;
  qa_bot_activate_goal_source activate_goal_heap[QA_MAX_ACTIVATESTACK];
  int32_t checkpoints_cell, patrol_points_cell, current_patrol_point_cell;
  int32_t patrol_flags;
} qa_bot_state_source;

/* TS BotState. */
typedef struct qa_bot_state_bots_a {
  qa_bot_state_source *src;
  qa_q3_base_product product;
  bool has_source_allocation;
  qa_q3_game_memory_allocation source_allocation;
  bool has_scripted_order;
  qa_bot_order_state scripted_order;
  qa_ai_node_bots_a ai_node;
  qa_bot_activate_goal_bots_a *activate_stack;
  qa_bot_activate_goal_bots_a activate_goal_heap[QA_MAX_ACTIVATESTACK];
  qa_bot_waypoint_bots_a *checkpoints, *patrol_points, *current_patrol_point;
  qa_bot_setup_progress_bots_a setup;
  qa_bot_state_source *owned;
} qa_bot_state_bots_a;

/* Saved-pointer callbacks. id < 0 is TS null. */
typedef int64_t (*qa_bot_waypoint_id_fn)(void *ctx, const qa_bot_waypoint_bots_a *waypoint);
typedef qa_bot_waypoint_bots_a *(*qa_bot_waypoint_at_fn)(void *ctx, int64_t id);
typedef void (*qa_bot_remap_observation_fn)(void *ctx, int64_t number, int64_t generation,
                                            int64_t *out_number, int64_t *out_generation);

/* allocation NULL is a diagnostic state owning one bounded record. A smaller
 * allocation aborts. False only on OOM. */
bool qa_bot_state_bots_a_init(qa_bot_state_bots_a *state, qa_q3_base_product product,
                              const qa_q3_game_memory_allocation *allocation);
void qa_bot_state_bots_a_destroy(qa_bot_state_bots_a *state);
/* TS presenceType getter; aborts on values other than 0/1/2/4. */
int32_t qa_bot_state_presence_type_checked(const qa_bot_state_bots_a *state);
/* TS curPs pmType/weapon/weaponState and lastUcmd.weapon getters. */
int32_t qa_bot_state_pm_type_checked(const qa_bot_state_bots_a *state);
int32_t qa_bot_state_weapon_checked(const qa_bot_state_bots_a *state);
int32_t qa_bot_state_weapon_state_checked(const qa_bot_state_bots_a *state);
int32_t qa_bot_state_command_weapon_checked(const qa_bot_state_bots_a *state);
/* TS string getter of a text cell inside src: aborts unless a NUL precedes the
 * end of source storage. name is the TS field name, for the diagnostic. */
const char *qa_bot_state_text(const qa_bot_state_bots_a *state, const char *cell, const char *name);
/* TS text setter: Q_strncpyz of a cell of `size` bytes. */
void qa_bot_state_set_text(char *cell, size_t size, const char *value);
void qa_bot_state_copy_team_leader_client_name(qa_bot_state_bots_a *state, const char *raw_name);
void qa_bot_state_copy_team_leader_with_overflow(qa_bot_state_bots_a *state, const char *name);
void qa_bot_state_copy_team_leader(qa_bot_state_bots_a *state, const char *name);
void qa_bot_state_clear_team_leader(qa_bot_state_bots_a *state);
void qa_bot_state_copy_subteam(qa_bot_state_bots_a *state, const char *name);
void qa_bot_state_clear_subteam(qa_bot_state_bots_a *state);
void qa_bot_state_reset_decision_state(qa_bot_state_bots_a *state);
void qa_bot_state_clear(qa_bot_state_bots_a *state);
/* Owned checkpoint tree. */
qa_save_value *qa_bot_state_capture_save_state(const qa_bot_state_bots_a *state,
                                               qa_bot_waypoint_id_fn waypoint_id, void *ctx);
void qa_bot_state_restore_save_state(qa_bot_state_bots_a *state, const qa_save_value *value,
                                     qa_bot_waypoint_at_fn waypoint, void *waypoint_ctx,
                                     qa_bot_remap_observation_fn remap, void *remap_ctx);

/* TS BotStateStore: botstates[MAX_CLIENTS], lazily allocated. Records replaced by
 * clear/restore stay allocated until destroy (TS GC keeps borrowers valid). */
typedef struct qa_bot_state_store_bots_a {
  qa_q3_base_product product;
  qa_bot_state_bots_a *cells[QA_Q3_MAX_CLIENTS];
  qa_bot_state_bots_a **retired;
  size_t retired_count, retired_capacity;
} qa_bot_state_store_bots_a;
void qa_bot_state_store_bots_a_init(qa_bot_state_store_bots_a *store, qa_q3_base_product product);
void qa_bot_state_store_bots_a_destroy(qa_bot_state_store_bots_a *store);
/* NULL when unallocated; a client outside 64 slots aborts as TS. */
qa_bot_state_bots_a *qa_bot_state_store_get(qa_bot_state_store_bots_a *store, int64_t client);
qa_bot_state_bots_a *qa_bot_state_store_acquire(qa_bot_state_store_bots_a *store, int64_t client,
                                                qa_q3_game_memory *memory);
qa_save_value *qa_bot_state_store_capture_save_state(qa_bot_state_store_bots_a *store,
                                                     qa_q3_game_memory *memory,
                                                     qa_bot_waypoint_id_fn waypoint_id, void *ctx);
void qa_bot_state_store_restore_save_state(qa_bot_state_store_bots_a *store,
                                           const qa_save_value *value, qa_q3_game_memory *memory,
                                           qa_bot_waypoint_at_fn waypoint, void *waypoint_ctx,
                                           qa_bot_remap_observation_fn remap, void *remap_ctx);
void qa_bot_state_store_clear(qa_bot_state_store_bots_a *store);

/* TS copyBotPlayerState: source memcpy into the state's cur_ps, keeping the
 * destination's embedded storage. Aborts when products differ. */
void qa_copy_bot_player_state_bots_a(qa_bot_state_bots_a *destination,
                                     const qa_q3_base_player_state *source);
#endif
