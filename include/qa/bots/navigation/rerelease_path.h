/* Port of src/bots/navigation/rerelease-path.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_RERELEASE_PATH_H
#define QA_BOTS_NAVIGATION_RERELEASE_PATH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/bots/navigation/runtime.h"
#include "qa/bots/navigation/types.h"
typedef struct qa_rerelease_path_request {
  qa_vec3 start, goal; int64_t flags; double move_distance;
  bool ignore_node_flags; double min_height, max_height, radius, drop_height, jump_height;
} qa_rerelease_path_request;
typedef struct qa_rerelease_path_info {
  int64_t code; double distance_squared;
  qa_vec3 *points; size_t point_count; /* owned */
  qa_vec3 first, second; int64_t link_type;
} qa_rerelease_path_info;
void qa_rerelease_path_info_destroy(qa_rerelease_path_info *info);
/* runtime NULL = TS null (code 8). Invalid predecessor chains abort (TS Error). */
qa_rerelease_path_info qa_rerelease_path_to_goal(qa_navigation_runtime *runtime, qa_rerelease_path_request request);
#endif
