/* Port of src/bots/navigation/helpers.ts. Generic at<T> becomes checked
 * accessors per array type. GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_HELPERS_H
#define QA_BOTS_NAVIGATION_HELPERS_H
#include <stdbool.h>
#include "qa/bots/navigation/types.h"
enum { QA_NAV_CONTENTS_WATER = 1, QA_NAV_CONTENTS_SLIME = 2, QA_NAV_CONTENTS_LAVA = 4, QA_NAV_CONTENTS_LADDER = 8 };
const qa_navigation_node *qa_nav_at_node(const qa_navigation_node *values, size_t count, int64_t index);
const qa_navigation_edge *qa_nav_at_edge(const qa_navigation_edge *values, size_t count, int64_t index);
double qa_nav_distance(qa_vec3 a, qa_vec3 b);
qa_vec3 qa_nav_midpoint(qa_vec3 a, qa_vec3 b);
qa_bounds qa_nav_translated(qa_vec3 point, qa_bounds bounds);
/* False = TS null; out filled otherwise. */
bool qa_nav_crouched_profile(const qa_navigation_profile *profile, qa_navigation_profile *out);
bool qa_nav_node_profile(const qa_navigation_profile *profile, const qa_navigation_node *node, qa_navigation_profile *out);
int64_t qa_nav_contents(const qa_navigation_world *world, const qa_navigation_profile *profile, qa_vec3 point, qa_query_target target);
qa_trace_result qa_nav_trace(const qa_navigation_world *world, const qa_navigation_profile *profile, qa_vec3 start, qa_vec3 end, qa_query_target target);
bool qa_nav_clear(const qa_navigation_world *world, const qa_navigation_profile *profile, qa_vec3 start, qa_vec3 end, qa_query_target target);
/* Default target {kind:"world"} variants. */
int64_t qa_nav_contents_world(const qa_navigation_world *world, const qa_navigation_profile *profile, qa_vec3 point);
bool qa_nav_clear_world(const qa_navigation_world *world, const qa_navigation_profile *profile, qa_vec3 start, qa_vec3 end);
qa_trace_result qa_nav_trace_world(const qa_navigation_world *world, const qa_navigation_profile *profile, qa_vec3 start, qa_vec3 end);
void qa_nav_validate_profile(const qa_navigation_profile *profile);
#endif
