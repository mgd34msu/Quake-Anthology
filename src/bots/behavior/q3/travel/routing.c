/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/travel/routing.h"
#include "qa/bots/behavior/q3/navigation_types.h"
#include "qa/bots/navigation/aas.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/bots/navigation/graph.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
static double fr(double v) { return (double)(float)v; }
float qa_movement_angle_difference(double a, double b) {
  double fa = fr(a), fb = fr(b), d = fr(fa - fb);
  if (fa > fb) {
    if (d > 180)
      d = fr(d - 360);
  } else if (d < -180)
    d = fr(d + 360);
  return (float)d;
}
double qa_movement_distance_squared(qa_vec3 a, qa_vec3 b) {
  qa_vec3 d = qa_sub3(b, a);
  return qa_dot3(d, d);
}
static qa_vec3 ma(qa_vec3 s, double distance, qa_vec3 d) {
  return qa_math_vec3(s.x + fr(distance * d.x), s.y + fr(distance * d.y), s.z + fr(distance * d.z));
}
/* AAS_ProjectPointOntoVector, followed by the source's first-outside-axis endpoint choice. */
double qa_distance_from_line_squared(qa_vec3 pt, qa_vec3 st, qa_vec3 en) {
  qa_vec3 dir = qa_normalize3(qa_sub3(en, st));
  qa_vec3 proj = ma(st, qa_dot3(qa_sub3(pt, st), dir), dir);
  double pa[3] = {proj.x, proj.y, proj.z}, sa[3] = {st.x, st.y, st.z}, ea[3] = {en.x, en.y, en.z};
  for (int i = 0; i < 3; i++)
    if ((pa[i] > sa[i] && pa[i] > ea[i]) || (pa[i] < sa[i] && pa[i] < ea[i]))
      return qa_movement_distance_squared(pt, fabs(fr(pa[i] - sa[i])) < fabs(fr(pa[i] - ea[i])) ? st : en);
  return qa_movement_distance_squared(pt, proj);
}
int64_t qa_avoid_movement_spots(qa_vec3 origin, qa_travel_reachability reach,
                                const qa_bot_avoid_spot *spots, int64_t count) {
  int64_t type = reach.reach.travel_type & QA_TRAVEL_MASK;
  bool check_between = !(type == QA_TRAVEL_WALKOFFLEDGE || type == QA_TRAVEL_JUMP ||
                         type == QA_TRAVEL_TELEPORT || type == QA_TRAVEL_ELEVATOR ||
                         type == QA_TRAVEL_GRAPPLEHOOK || type == QA_TRAVEL_ROCKETJUMP ||
                         type == QA_TRAVEL_BFGJUMP || type == QA_TRAVEL_JUMPPAD ||
                         type == QA_TRAVEL_FUNCBOB);
  qa_vec3 start = reach.reach.start, end = reach.reach.end;
  int64_t result = QA_AVOID_CLEAR;
  for (int64_t i = 0; i < count; i++) {
    if (i >= QA_MAX_AVOID_SPOTS)
      qa_range_error("Bot movement index %lld outside %d entries", (long long)i, QA_MAX_AVOID_SPOTS);
    const qa_bot_avoid_spot *spot = &spots[i];
    qa_vec3 at = qa_bot_v3(spot->origin);
    double squared_radius = fr((double)spot->radius * (double)spot->radius);
    double squared_distance = qa_distance_from_line_squared(at, origin, start);
    if (squared_distance < squared_radius &&
        qa_movement_distance_squared(at, origin) > squared_distance)
      result = spot->type;
    else if (check_between) {
      squared_distance = qa_distance_from_line_squared(at, start, end);
      if (squared_distance < squared_radius &&
          qa_movement_distance_squared(at, start) > squared_distance)
        result = spot->type;
    } else if (squared_distance < squared_radius &&
               qa_movement_distance_squared(at, start) > squared_distance)
      /* Source discards the end distance; the comparison still uses origin->start. */
      result = spot->type;
    if (result == QA_AVOID_ALWAYS)
      return result;
  }
  return result;
}
static int64_t source_type(const qa_navigation_edge *edge) {
  if (edge->source.kind == QA_NS_AAS)
    return edge->source_travel_type;
  switch (edge->mode) {
  case QA_TM_WALK: return QA_TRAVEL_WALK;
  case QA_TM_CROUCH: return QA_TRAVEL_CROUCH;
  case QA_TM_JUMP:
    return edge->source_travel_type == QA_KEX_TRAVEL_BARRIER_JUMP || edge->source_travel_type == 11 ? QA_TRAVEL_BARRIERJUMP
                                                                           : QA_TRAVEL_JUMP;
  case QA_TM_DROP: return QA_TRAVEL_WALKOFFLEDGE;
  case QA_TM_SWIM: return QA_TRAVEL_SWIM;
  case QA_TM_WATER_JUMP: return QA_TRAVEL_WATERJUMP;
  case QA_TM_LADDER: return QA_TRAVEL_LADDER;
  case QA_TM_TELEPORT: return QA_TRAVEL_TELEPORT;
  case QA_TM_MOVER: return QA_TRAVEL_ELEVATOR;
  case QA_TM_JUMP_PAD: return QA_TRAVEL_JUMPPAD;
  case QA_TM_GRAPPLE: return QA_TRAVEL_GRAPPLEHOOK;
  case QA_TM_ROCKET_JUMP: return QA_TRAVEL_ROCKETJUMP;
  case QA_TM_BFG_JUMP: return QA_TRAVEL_BFGJUMP;
  case QA_TM_DOUBLE_JUMP: return QA_TRAVEL_DOUBLEJUMP;
  case QA_TM_RAMP_JUMP: return QA_TRAVEL_RAMPJUMP;
  case QA_TM_STRAFE_JUMP: return QA_TRAVEL_STRAFEJUMP;
  case QA_TM_UNKNOWN:
  case QA_TM_COUNT: break;
  }
  return QA_TRAVEL_INVALID;
}
void qa_travel_graph_init(qa_travel_graph *g, qa_navigation_runtime *runtime) {
  g->runtime = runtime;
  g->offset = runtime->graph->aas_asset != NULL ? 0 : 1;
  g->time = 0;
}
int64_t qa_travel_graph_area(const qa_travel_graph *g, int64_t n) { return n + g->offset; }
int64_t qa_travel_graph_node(const qa_travel_graph *g, int64_t a) { return a - g->offset; }
int64_t qa_travel_graph_handle(const qa_travel_graph *g, const qa_navigation_edge *e) {
  return e->id + g->offset;
}
qa_travel_reachability qa_travel_graph_describe(const qa_travel_graph *g,
                                                const qa_navigation_edge *edge) {
  const qa_aas_asset *asset = g->runtime->graph->aas_asset;
  const qa_aas_reach_rec *record =
      asset != NULL && edge->id >= 0 && (size_t)edge->id < asset->reachability_count
          ? &asset->reachability[edge->id]
          : NULL;
  qa_travel_reachability r;
  r.reach.area = qa_travel_graph_area(g, edge->to);
  r.reach.face = record != NULL                          ? record->face
                 : edge->has_entity && edge->entity.has_model ? edge->entity.model
                                                              : 0;
  r.reach.edge = record != NULL ? record->edge : (int64_t)trunc(edge->end.z - edge->start.z);
  r.reach.start = edge->start;
  r.reach.end = edge->end;
  r.reach.travel_type = source_type(edge);
  double seconds = trunc(edge->travel_seconds * 100);
  r.reach.travel_time = record != NULL ? record->travel_time : (int64_t)qa_js_max(1, seconds);
  r.reach.padding = record != NULL ? record->padding : 0;
  r.graph_edge = *edge;
  return r;
}
bool qa_travel_graph_reach(const qa_travel_graph *g, int64_t number, qa_travel_reachability *out) {
  if (number == 0)
    return false;
  const qa_navigation_edge *edge = qa_navigation_runtime_edge(g->runtime, number - g->offset);
  if (edge == NULL)
    return false;
  *out = qa_travel_graph_describe(g, edge);
  return true;
}
typedef struct candidate {
  const qa_navigation_edge *edge;
  int32_t time;
} candidate;
static int64_t setting_contents(const qa_aas_asset *asset, int64_t area) {
  return area >= 0 && (size_t)area < asset->setting_count ? asset->settings[area].contents : 0;
}
qa_travel_selection qa_travel_graph_select(qa_travel_graph *g, const qa_bot_move_state *state,
                                           const qa_bot_goal *goal, int64_t travel_flags,
                                           int64_t move_travel_flags) {
  qa_travel_selection_start from = {state->src->area, qa_bot_v3(state->src->origin), state->src->last_goal_area,
                                    state->src->last_area, state};
  return qa_travel_graph_select_from(g, goal, travel_flags, move_travel_flags, &from);
}
qa_travel_selection qa_travel_graph_select_from(qa_travel_graph *g, const qa_bot_goal *goal, int64_t travel_flags,
                                                int64_t move_travel_flags, const qa_travel_selection_start *from) {
  qa_travel_selection result = {0, 0};
  const qa_bot_move_record *avoid = from->avoid == NULL ? NULL : from->avoid->src;
  const qa_aas_asset *asset = g->runtime->graph->aas_asset;
  if (asset != NULL &&
      ((setting_contents(asset, from->area) | setting_contents(asset, goal->area)) & 256) != 0) {
    travel_flags |= QA_TFL_DONOTENTER;
    move_travel_flags |= QA_TFL_DONOTENTER;
  }
  size_t edge_count = 0;
  const qa_navigation_edge *const *edges =
      qa_navigation_runtime_outgoing(g->runtime, qa_travel_graph_node(g, from->area), &edge_count);
  candidate *candidates = malloc((edge_count == 0 ? 1 : edge_count) * sizeof *candidates);
  if (candidates == NULL)
    qa_range_error("Out of memory selecting a bot reachability");
  size_t count = 0;
  for (size_t i = 0; i < edge_count; i++) {
    const qa_navigation_edge *edge = edges[i];
    if (asset != NULL && !qa_navigation_runtime_edge_selectable(g->runtime, edge, true, move_travel_flags))
      continue;
    qa_travel_reachability reach = qa_travel_graph_describe(g, edge);
    int64_t number = qa_travel_graph_handle(g, edge);
    int64_t flag = qa_travel_flag_for_type(reach.reach.travel_type);
    if ((flag & travel_flags) == 0 || (flag & move_travel_flags) == 0)
      continue;
    if (avoid != NULL && avoid->avoid_reach == number && avoid->avoid_reach_time >= g->time &&
        avoid->avoid_reach_tries > 4)
      continue;
    if (from->last_goal_area == goal->area && reach.reach.area == from->last_area)
      continue;
    qa_navigation_estimate_query query;
    memset(&query, 0, sizeof query);
    query.start_node = edge->to;
    query.goal_node = qa_travel_graph_node(g, goal->area);
    query.has_origin = true;
    query.origin = reach.reach.end;
    query.has_travel_flags = true;
    query.travel_flags = travel_flags;
    qa_navigation_estimate_result tail = qa_navigation_runtime_estimate(g->runtime, query);
    if (!tail.is_estimate || tail.travel_time == 0)
      continue;
    if (avoid != NULL &&
        qa_avoid_movement_spots(from->origin, reach, avoid->avoid_spots, avoid->num_avoid_spots) != 0) {
      result.flags |= QA_BOT_MRF_BLOCKEDBYAVOIDSPOT;
      continue;
    }
    /* TS (a + b) | 0, then a stable sort by time. */
    int32_t time = qa_js_to_int32(tail.travel_time + (double)reach.reach.travel_time);
    size_t at = count++;
    while (at > 0 && candidates[at - 1].time > time) {
      candidates[at] = candidates[at - 1];
      at--;
    }
    candidates[at].edge = edge;
    candidates[at].time = time;
  }
  /* AAS reachabilities are taken as stored (BotGetReachabilityToGoal has no movement
   * test); only foreign graphs need admission. */
  if (count > 0 && asset != NULL) {
    result.reachability = qa_travel_graph_handle(g, candidates[0].edge);
    free(candidates);
    return result;
  }
  for (size_t i = 0; i < count; i++) {
    qa_traversal_admission admission =
        qa_navigation_runtime_admit_edge(g->runtime, candidates[i].edge, from->origin);
    bool admitted = admission.admitted;
    qa_traversal_admission_destroy(&admission);
    if (admitted) {
      result.reachability = qa_travel_graph_handle(g, candidates[i].edge);
      break;
    }
  }
  free(candidates);
  return result;
}
