/* Port of src/bots/behavior/rerelease/path-follow.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_PATH_FOLLOW_H
#define QA_BOTS_BEHAVIOR_RERELEASE_PATH_FOLLOW_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/bots/behavior/rerelease/data/botdata.h"
#include "qa/bots/behavior/rerelease/math.h"
#include "qa/bots/behavior/rerelease/nav.h"
#include "qa/bots/behavior/rerelease/rng.h"
#define QA_BOT_RUN_SPEED 320
#define QA_BOT_WALK_SPEED 160
#define QA_BOT_POINT_REACHED 32
#define QA_BOT_TRAVERSAL_REACHED 20
#define QA_CORNER_TURN_DEGREES 60
#define QA_CORNER_SLOW_DISTANCE 96
#define QA_LIFT_BOARD_RADIUS 48
#define QA_LIFT_WAIT_SECONDS 4
#define QA_LEDGE_OVERSHOOT 128
#define QA_SWIM_AIR_RESERVE 5
#define QA_SWIM_SURFACE_PUSH 0.7
typedef struct qa_bot_path_state {
  const qa_nav_path *path; /* borrowed, NULL = TS null */
  int64_t index;
  qa_bot_vec3 stuck_origin;
  double stuck_since; int64_t stuck_count;
  double lift_wait_since, lift_wait_z, jump_ready_at, planned_at;
} qa_bot_path_state;
qa_bot_path_state qa_new_path_state(void);
void qa_bot_set_path(qa_bot_path_state *state, const qa_nav_path *path, qa_bot_vec3 origin, double now);
void qa_bot_clear_path(qa_bot_path_state *state);
enum { QA_BOT_PATH_NO_PATH = 0, QA_BOT_PATH_MOVING = 1, QA_BOT_PATH_ARRIVED = 2, QA_BOT_PATH_STUCK = 3 };
typedef struct qa_bot_move_output {
  int64_t status; double forwardmove, sidemove, upmove; bool jump;
  bool has_riding, riding;
  bool has_target; qa_bot_vec3 target;
  bool has_link; qa_nav_graph_link link;
} qa_bot_move_output;
typedef qa_bot_transport_step (*qa_bot_transport_fn)(void *ctx, qa_nav_graph_link link, qa_bot_vec3 origin);
typedef struct qa_bot_follow_input {
  void *transport_ctx; qa_bot_transport_fn transport; /* NULL = TS undefined */
  qa_bot_vec3 origin; double pitch, yaw; bool on_ground;
  bool has_water_level; double water_level;
  bool has_air_seconds; double air_seconds;
  bool has_air_above; bool air_above;
  bool has_velocity; qa_bot_vec3 velocity;
  double now, stuck_time;
  bool has_run_speed; double run_speed;
  bool has_walk_speed; double walk_speed;
} qa_bot_follow_input;
/* rng is unused by followPath in TS (kept for signature parity via roll). */
qa_bot_move_output qa_follow_path(qa_bot_path_state *state, qa_bot_follow_input input, qa_bot_movement_settings movement);
bool qa_roll_combat_jump(qa_bot_path_state *state, qa_bot_movement_settings movement, qa_bot_random_t rng, double now, bool on_ground);
typedef struct qa_bot_steer { double forwardmove, sidemove; } qa_bot_steer;
qa_bot_steer qa_steer_direct(qa_bot_vec3 origin, double yaw, qa_bot_vec3 target, bool walk_only, double run_speed, double walk_speed);
#endif
