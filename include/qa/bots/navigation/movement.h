/* Port of src/bots/navigation/movement.ts. Prediction driver is fn+ctx.
 * GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_MOVEMENT_H
#define QA_BOTS_NAVIGATION_MOVEMENT_H
#include <stdbool.h>
#include "qa/bots/navigation/types.h"
#include "qa/contracts/movement.h"
typedef struct qa_navigation_prediction {
  qa_movement_provider provider; qa_movement_services services; void *ctx;
  /* previous NULL on first command. */
  qa_movement_input (*input)(void *ctx, const qa_movement_result *previous, int64_t command_index, qa_traversal_request request);
} qa_navigation_prediction;
typedef struct qa_navigation_prediction_driver {
  void *ctx;
  bool (*begin)(void *ctx, qa_traversal_request request, qa_navigation_profile profile, qa_navigation_prediction *out);
} qa_navigation_prediction_driver;
typedef struct qa_navigation_prediction_limits { bool has_max_seconds; double max_seconds; bool has_max_commands; int64_t max_commands; bool has_tolerance; double tolerance; } qa_navigation_prediction_limits;
typedef struct qa_movement_route_admission {
  qa_navigation_prediction_driver driver; qa_navigation_profile profile; double max_seconds, tolerance; int64_t max_commands;
  bool has_prediction; qa_navigation_prediction prediction; bool has_previous; qa_movement_result previous; int64_t sequence; bool failed;
} qa_movement_route_admission;
void qa_movement_route_admission_init(qa_movement_route_admission *a, qa_navigation_prediction_driver driver, qa_navigation_profile profile, qa_navigation_prediction_limits options);
qa_traversal_admission qa_movement_route_admission_admit(qa_movement_route_admission *a, qa_traversal_request request);
qa_navigation_route_prediction qa_movement_route_admission_as_prediction(qa_movement_route_admission *a);
qa_traversal_admission qa_create_movement_admission(qa_navigation_prediction_driver driver, qa_navigation_prediction_limits options, qa_traversal_request request, qa_navigation_profile profile);
#endif
