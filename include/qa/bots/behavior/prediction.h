/* Port of src/bots/behavior/prediction.ts: source prediction requests replay the
 * selected movement over detached input; no actor writes or source frame loop.
 * GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_PREDICTION_H
#define QA_BOTS_BEHAVIOR_PREDICTION_H
#include "qa/bots/behavior/q3/navigation_types.h"
#include "qa/bots/behavior/q3/travel/types.h"
#include "qa/contracts/math.h"
#include "qa/contracts/movement.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* TS BotMovementStop; has_area false is TS null. */
typedef struct qa_bot_movement_stop {
  int64_t events;
  qa_vec3 origin;
  bool has_area;
  int64_t area;
} qa_bot_movement_stop;
/* TS BotMovementProjection. previous NULL is TS null. Every member receives ctx. */
typedef struct qa_bot_movement_projection_bots_a {
  void *ctx;
  qa_movement_provider provider;
  qa_movement_services services;
  qa_movement_input (*input)(void *ctx, const qa_movement_result *previous, int64_t frame,
                             qa_vec3 command_move);
  /* Selected character/trigger semantics classify source stop requests from
   * actual movement output. */
  qa_bot_movement_stop (*stop)(void *ctx, const qa_movement_result *previous,
                               const qa_movement_result *result, int64_t frame);
} qa_bot_movement_projection_bots_a;
/* The result's trajectory is malloc'd: release it with
 * qa_bot_travel_prediction_result_destroy. */
qa_bot_travel_prediction_result qa_project_bot_movement(qa_bot_movement_prediction query,
                                                        qa_bot_movement_projection_bots_a projection);
void qa_bot_travel_prediction_result_destroy(qa_bot_travel_prediction_result *result);
#endif
