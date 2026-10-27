/* Port of src/bots/behavior/prediction.ts. Movement contracts (owner:
 * contracts/movement.ts, travel/types.ts bots-b) mirrored as fn+ctx steps.
 * GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_PREDICTION_H
#define QA_BOTS_BEHAVIOR_PREDICTION_H
#include "qa/bots/behavior/q3/navigation_types.h"
#include "qa/contracts/math.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_bot_movement_stop {
  int64_t events;
  qa_vec3 origin;
  bool has_area;
  int64_t area;
} qa_bot_movement_stop;
/* One detached movement step: host advances origin/velocity by elapsed seconds. */
typedef struct qa_bot_movement_step_result {
  qa_vec3 origin, velocity;
  bool grounded;
  int64_t water_level;
  bool actor_removed;
  qa_bounds bounds;
  bool has_bounds;
} qa_bot_movement_step_result;
typedef struct qa_bot_movement_projection_bots_a {
  void *ctx;
  /* elapsed_out in seconds; must be >0 else RangeError. kind_ok false -> provider disagree error.
   */
  bool (*step)(void *ctx, int64_t frame, qa_vec3 command_move, double *elapsed_out,
               qa_bot_movement_step_result *out);
  qa_bot_movement_stop (*stop)(void *ctx, int64_t frame, const qa_bot_movement_step_result *result);
} qa_bot_movement_projection_bots_a;
typedef struct qa_bot_travel_prediction_result_bots_a {
  qa_vec3 end;
  bool has_end_area;
  int64_t end_area;
  qa_vec3 velocity;
  int64_t frames, stop_event;
  qa_vec3 *trajectory;
  size_t trajectory_count;
  double seconds;
  bool grounded;
  int64_t water_level;
  bool has_bounds;
  qa_bounds bounds;
} qa_bot_travel_prediction_result_bots_a;
qa_bot_travel_prediction_result_bots_a
qa_project_bot_movement(qa_bot_movement_prediction query,
                        qa_bot_movement_projection_bots_a projection);
void qa_bot_travel_prediction_result_bots_a_destroy(qa_bot_travel_prediction_result_bots_a *r);
#endif
