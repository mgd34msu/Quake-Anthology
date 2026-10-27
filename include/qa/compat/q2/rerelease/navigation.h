/* Port of src/compat/q2/rerelease/navigation.ts. GPL-2.0-or-later.
 * NavigationRuntime (bots/navigation) is not landed; the runtime
 * handle is opaque and path_to_goal is a fn+ctx mirror of
 * rereleasePathToGoal (bots/navigation/rerelease-path.ts). */
#ifndef QA_COMPAT_Q2_RERELEASE_NAVIGATION_H
#define QA_COMPAT_Q2_RERELEASE_NAVIGATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/q2/rerelease/layouts.h"
#include "qa/contracts/identity.h"

/* TS RereleaseGoalStatus 0|1|2|3. */
typedef int qa_compat_q2_rerelease_goal_status;
typedef struct qa_compat_q2_rerelease_path_request {
  qa_vec3 start, goal; double flags, move_distance; bool ignore_node_flags;
  double min_height, max_height, radius, drop_height, jump_height;
} qa_compat_q2_rerelease_path_request;
typedef struct qa_compat_q2_rerelease_path_result {
  const qa_vec3 *points; size_t point_count; double distance_squared;
  qa_vec3 first, second; double link_type, code;
} qa_compat_q2_rerelease_path_result;
typedef struct qa_compat_q2_rerelease_navigation_services {
  void *ctx;
  void *(*runtime)(void *ctx); /* NULL is TS null */
  qa_compat_q2_rerelease_path_result (*path_to_goal)(void *ctx, void *runtime, qa_compat_q2_rerelease_path_request request);
  qa_compat_q2_rerelease_goal_status (*move_to_point)(void *ctx, qa_actor_id actor, qa_vec3 point, double tolerance);
  qa_compat_q2_rerelease_goal_status (*follow_actor)(void *ctx, qa_actor_id actor, qa_actor_id target);
} qa_compat_q2_rerelease_navigation_services;

typedef struct qa_compat_q2_rerelease_navigation_imports qa_compat_q2_rerelease_navigation_imports;
/* actor false return is the TS null. NULL only on OOM. */
qa_compat_q2_rerelease_navigation_imports *qa_compat_q2_rerelease_navigation_imports_create(
    qa_compat_q2_memory *memory, qa_compat_q2_rerelease_navigation_services services,
    void *actor_ctx, bool (*actor)(void *ctx, qa_guest_address address, qa_actor_id *out));
void qa_compat_q2_rerelease_navigation_imports_destroy(qa_compat_q2_rerelease_navigation_imports *n);
qa_compat_q2_memory *qa_compat_q2_rerelease_navigation_imports_get_memory(const qa_compat_q2_rerelease_navigation_imports *n);
qa_guest_call_result qa_compat_q2_rerelease_navigation_imports_invoke(qa_compat_q2_rerelease_navigation_imports *n, const char *name,
                                                                      const qa_guest_call_value *args, size_t count);
#endif
