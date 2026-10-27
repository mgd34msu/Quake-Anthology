/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/travel/routing.h"
#include "qa/bots/behavior/q3/navigation_types.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include <math.h>
float qa_movement_angle_difference(double a, double b) {
  float fa = (float)a, fb = (float)b;
  float d = (float)((double)fa - (double)fb);
  if (fa > fb) {
    if (d > 180)
      d = (float)((double)d - 360);
  } else if (d < -180)
    d = (float)((double)d + 360);
  return d;
}
double qa_movement_distance_squared(qa_vec3 a, qa_vec3 b) {
  qa_vec3 d = qa_sub3(b, a);
  return qa_dot3(d, d);
}
static qa_vec3 ma_fn(qa_vec3 s, double dist, qa_vec3 dir) {
  return (qa_vec3){s.x + (float)(dist * dir.x), s.y + (float)(dist * dir.y),
                   s.z + (float)(dist * dir.z)};
}
double qa_distance_from_line_squared(qa_vec3 pt, qa_vec3 st, qa_vec3 en) {
  qa_vec3 dir = qa_normalize3(qa_sub3(en, st));
  qa_vec3 proj = ma_fn(st, qa_dot3(qa_sub3(pt, st), dir), dir);
  double pa[3] = {proj.x, proj.y, proj.z}, sa[3] = {st.x, st.y, st.z}, ea[3] = {en.x, en.y, en.z};
  for (int i = 0; i < 3; i++)
    if ((pa[i] > sa[i] && pa[i] > ea[i]) || (pa[i] < sa[i] && pa[i] < ea[i]))
      return qa_movement_distance_squared(
          pt,
          fabs((double)(float)(pa[i] - sa[i])) < fabs((double)(float)(pa[i] - ea[i])) ? st : en);
  return qa_movement_distance_squared(pt, proj);
}
int64_t qa_avoid_movement_spots(qa_vec3 origin, qa_travel_reachability_bots_a reach,
                                const qa_bot_avoid_spot *spots, int64_t count) {
  int64_t type = reach.travel_type & QA_TRAVEL_MASK;
  int disc = (type == QA_TRAVEL_WALKOFFLEDGE || type == QA_TRAVEL_JUMP ||
              type == QA_TRAVEL_TELEPORT || type == QA_TRAVEL_ELEVATOR ||
              type == QA_TRAVEL_GRAPPLEHOOK || type == QA_TRAVEL_ROCKETJUMP ||
              type == QA_TRAVEL_BFGJUMP || type == QA_TRAVEL_JUMPPAD || type == QA_TRAVEL_FUNCBOB);
  int64_t result = QA_AVOID_CLEAR;
  for (int64_t i = 0; i < count; i++) {
    if (i < 0)
      qa_range_error("Bot movement index outside entries");
    const qa_bot_avoid_spot *sp = &spots[i];
    float sr = (float)((double)sp->radius * (double)sp->radius);
    double sd = qa_distance_from_line_squared(sp->origin, origin, reach.start);
    if (sd < sr && qa_movement_distance_squared(sp->origin, origin) > sd)
      result = sp->type;
    else if (!disc) {
      sd = qa_distance_from_line_squared(sp->origin, reach.start, reach.end);
      if (sd < sr && qa_movement_distance_squared(sp->origin, reach.start) > sd)
        result = sp->type;
    } else {
      (void)qa_movement_distance_squared(sp->origin, reach.end);
      if (sd < sr && qa_movement_distance_squared(sp->origin, reach.start) > sd)
        result = sp->type;
    }
    if (result == QA_AVOID_ALWAYS)
      return result;
  }
  return result;
}
void qa_travel_graph_bots_a_init(qa_travel_graph_bots_a *g, int is_aas) {
  g->offset = is_aas ? 0 : 1;
}
int64_t qa_travel_graph_get_area(const qa_travel_graph_bots_a *g, int64_t n) {
  return n + g->offset;
}
int64_t qa_travel_graph_get_node(const qa_travel_graph_bots_a *g, int64_t a) {
  return a - g->offset;
}
int64_t qa_travel_graph_get_handle(const qa_travel_graph_bots_a *g, int64_t e) {
  return e + g->offset;
}
