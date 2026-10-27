/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/navigation.h"
#include "qa/bots/behavior/library/goals.h"
#include <string.h>
void qa_source_bot_navigation_bots_a_init(qa_source_bot_navigation_bots_a *n) {
  memset(n, 0, sizeof *n);
}
bool qa_source_bot_navigation_get_ready(const qa_source_bot_navigation_bots_a *n) {
  return n->ready;
}
int64_t qa_source_bot_navigation_point_area(qa_source_bot_navigation_bots_a *n, qa_vec3 o) {
  return n->point_area ? n->point_area(n->ctx, o) : 0;
}
qa_route_result qa_source_bot_navigation_route(qa_source_bot_navigation_bots_a *n,
                                               qa_route_query q) {
  return n->route ? n->route(n->ctx, q) : (qa_route_result){QA_ROUTE_UNREACHABLE, 0, 0};
}
