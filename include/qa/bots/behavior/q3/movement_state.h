/* Port of src/bots/behavior/q3/movement-state.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_MOVEMENT_STATE_H
#define QA_BOTS_BEHAVIOR_Q3_MOVEMENT_STATE_H
#include "qa/contracts/math.h"
#include <stdbool.h>
#include <stdint.h>
#define QA_MAX_MOVE_STATES 64
#define QA_MAX_AVOID_REACH 1
#define QA_MAX_AVOID_SPOTS 32
typedef enum qa_bot_move_type {
  QA_BOT_MOVE_WALK = 1,
  QA_BOT_MOVE_CROUCH = 2,
  QA_BOT_MOVE_JUMP = 4,
  QA_BOT_MOVE_GRAPPLE = 8,
  QA_BOT_MOVE_ROCKETJUMP = 16,
  QA_BOT_MOVE_BFGJUMP = 32
} qa_bot_move_type;
typedef enum qa_bot_move_flag {
  QA_BOT_MF_BARRIERJUMP = 1,
  QA_BOT_MF_ONGROUND = 2,
  QA_BOT_MF_SWIMMING = 4,
  QA_BOT_MF_AGAINSTLADDER = 8,
  QA_BOT_MF_WATERJUMP = 16,
  QA_BOT_MF_TELEPORTED = 32,
  QA_BOT_MF_GRAPPLEPULL = 64,
  QA_BOT_MF_ACTIVEGRAPPLE = 128,
  QA_BOT_MF_GRAPPLERESET = 256,
  QA_BOT_MF_WALK = 512
} qa_bot_move_flag;
typedef enum qa_bot_move_result_flag {
  QA_BOT_MRF_MOVEMENTVIEW = 1,
  QA_BOT_MRF_SWIMVIEW = 2,
  QA_BOT_MRF_WAITING = 4,
  QA_BOT_MRF_MOVEMENTVIEWSET = 8,
  QA_BOT_MRF_MOVEMENTWEAPON = 16,
  QA_BOT_MRF_ONTOPOFOBSTACLE = 32,
  QA_BOT_MRF_ONTOPOF_FUNCBOB = 64,
  QA_BOT_MRF_ONTOPOF_ELEVATOR = 128,
  QA_BOT_MRF_BLOCKEDBYAVOIDSPOT = 256
} qa_bot_move_result_flag;
typedef enum qa_bot_move_result_type {
  QA_BOT_MRT_ELEVATORUP = 1,
  QA_BOT_MRT_WAITFORFUNCBOBBING = 2,
  QA_BOT_MRT_BADGRAPPLEPATH = 4,
  QA_BOT_MRT_INSOLIDAREA = 8
} qa_bot_move_result_type;
typedef enum qa_bot_avoid_spot_type {
  QA_AVOID_CLEAR = 0,
  QA_AVOID_ALWAYS = 1,
  QA_AVOID_DONTBLOCK = 2
} qa_bot_avoid_spot_type;
typedef struct qa_bot_init_move {
  qa_vec3 origin, velocity, view_offset;
  int64_t entity_num, client;
  double think_time;
  int64_t presence_type;
  qa_vec3 view_angles;
  int64_t or_move_flags;
} qa_bot_init_move;
typedef struct qa_bot_move_result {
  bool failure;
  int64_t type;
  bool blocked;
  int64_t block_entity, travel_type, flags, weapon;
  qa_vec3 move_direction, ideal_view_angles;
} qa_bot_move_result;
typedef struct qa_bot_avoid_spot {
  qa_vec3 origin;
  float radius;
  int64_t type;
} qa_bot_avoid_spot;
typedef struct qa_bot_move_state {
  qa_vec3 origin, velocity, view_offset, view_angles, last_origin;
  int64_t entity_num, client;
  float think_time;
  int64_t presence_type;
  int64_t area, last_area, last_goal_area, last_reachability, reach_area, move_flags, jump_reach;
  float grapple_visible_time, last_grapple_distance, reachability_time;
  int64_t avoid_reach;
  float avoid_reach_time;
  int64_t avoid_reach_tries;
  qa_bot_avoid_spot avoid_spots[QA_MAX_AVOID_SPOTS];
  int64_t num_avoid_spots;
  bool has_walk_progress;
  int64_t walk_edge_id;
} qa_bot_move_state;
typedef struct qa_bot_move_state_store {
  qa_bot_move_state states[QA_MAX_MOVE_STATES + 1];
  bool allocated[QA_MAX_MOVE_STATES + 1];
  double (*time_fn)(void *ctx);
  void *time_ctx;
  void (*print)(void *ctx, int severity, const char *text);
  void *print_ctx;
  float (*lib_var_value)(void *ctx, const char *name, const char *def);
  void *libvar_ctx;
  float sv_max_step, sv_max_barrier, sv_gravity, rocket_index, bfg_index, grapple_index,
      missile_entity_type, offhand_grapple;
} qa_bot_move_state_store;
void qa_bot_move_state_store_init(qa_bot_move_state_store *s);
int64_t qa_bot_move_state_store_allocate(qa_bot_move_state_store *s);
qa_bot_move_state *qa_bot_move_state_store_from_handle(qa_bot_move_state_store *s, int64_t handle);
void qa_bot_move_state_store_free(qa_bot_move_state_store *s, int64_t handle);
void qa_bot_move_state_store_initialize(qa_bot_move_state_store *s, int64_t handle,
                                        qa_bot_init_move input);
void qa_bot_move_state_store_reset(qa_bot_move_state_store *s, int64_t handle);
void qa_bot_move_state_store_reset_avoid_reach(qa_bot_move_state_store *s, int64_t handle);
void qa_bot_move_state_store_reset_last_avoid_reach(qa_bot_move_state_store *s, int64_t handle);
void qa_bot_move_state_store_add_to_avoid_reach(qa_bot_move_state_store *s,
                                                qa_bot_move_state *state, int64_t number,
                                                double avoid_time);
void qa_bot_move_state_store_add_avoid_spot(qa_bot_move_state_store *s, int64_t handle,
                                            qa_vec3 origin, double radius, int64_t type);
int64_t qa_bot_move_state_store_setup(qa_bot_move_state_store *s);
void qa_bot_move_state_store_shutdown(qa_bot_move_state_store *s);
void qa_bot_move_result_init(qa_bot_move_result *r);
#endif
