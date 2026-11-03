/* See header. Source area zero remains invalid even when a foreign navigation
 * graph uses node zero. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/navigation.h"
#include "qa/bots/behavior/q3/travel/routing.h"
#include "qa/bots/navigation/aas.h"
#include "qa/bots/navigation/estimate_aas.h"
#include "qa/bots/navigation/graph.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

typedef qa_source_bot_navigation_bots_a nav_t;
static double fr(double v) { return (double)(float)v; }
static void *xmalloc(size_t n) {
  void *p = malloc(n == 0 ? 1 : n);
  if (p == NULL)
    qa_range_error("Out of memory in bot navigation");
  return p;
}
static qa_vec3 with_z(qa_vec3 v, double z) { return qa_math_vec3(v.x, v.y, z); }
static bool clear(const qa_bot_server_trace_result *t) { return strcmp(t->solidity, "clear") == 0; }
static qa_navigation_runtime *runtime(nav_t *n) {
  return n->selected != NULL ? n->selected : n->host.runtime;
}
static int64_t source(const nav_t *n, int64_t node) { return node + n->offset; }
static int64_t node_of(const nav_t *n, int64_t area) { return area - n->offset; }

/* ---- travel host: navigation members of SourceBotTravelHost ---- */
static qa_bot_navigation_area th_area(void *ctx, int64_t number) {
  return qa_source_bot_navigation_area(ctx, number);
}
static int64_t th_point_area(void *ctx, qa_vec3 origin) {
  return qa_source_bot_navigation_point_area(ctx, origin);
}
static int64_t th_trace_areas(void *ctx, qa_vec3 start, qa_vec3 end, int64_t *areas, qa_vec3 *points,
                              int64_t maximum) {
  size_t count = 0;
  qa_bot_area_crossing *crossings = qa_source_bot_navigation_trace_areas(ctx, start, end, maximum, &count);
  for (size_t i = 0; i < count && (int64_t)i < maximum; i++) {
    areas[i] = crossings[i].area;
    if (points != NULL)
      points[i] = crossings[i].point;
  }
  free(crossings);
  return (int64_t)count;
}
static qa_bounds th_presence_bounds(void *ctx, int64_t presence) {
  return qa_source_bot_navigation_presence_bounds(ctx, presence);
}
static bool th_swimming(void *ctx, qa_vec3 origin) { return qa_source_bot_navigation_swimming(ctx, origin); }
static int64_t th_fuzzy(void *ctx, qa_vec3 origin) {
  return qa_source_bot_navigation_fuzzy_point_reachability_area(ctx, origin);
}
static int64_t th_reachability_area(void *ctx, qa_vec3 origin, int64_t client) {
  return qa_source_bot_navigation_reachability_area(ctx, origin, client);
}
static void *th_runtime(void *ctx, int64_t client) {
  nav_t *n = ctx;
  return n->host.for_client(n->host.ctx, client);
}
static double th_time(void *ctx) {
  nav_t *n = ctx;
  return n->host.time(n->host.ctx);
}
static int64_t th_point_contents(void *ctx, qa_vec3 point) {
  nav_t *n = ctx;
  return n->host.point_contents(n->host.ctx, point);
}
static qa_bot_server_trace_result th_trace(void *ctx, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds,
                                           int64_t pass, int64_t mask) {
  nav_t *n = ctx;
  return n->host.trace(n->host.ctx, start, end, bounds, pass, mask);
}
static int64_t th_model_index(void *ctx, int64_t entity) {
  nav_t *n = ctx;
  return n->host.entity_model_index(n->host.ctx, entity);
}
static bool th_model_info(void *ctx, int64_t model, qa_bot_travel_model *out) {
  nav_t *n = ctx;
  return n->host.model_info(n->host.ctx, model, out);
}
static int64_t th_next_entity(void *ctx, int64_t after) {
  nav_t *n = ctx;
  return n->host.next_entity(n->host.ctx, after);
}
static int64_t th_entity_type(void *ctx, int64_t entity) {
  nav_t *n = ctx;
  return n->host.entity_type(n->host.ctx, entity);
}
static int64_t th_entity_weapon(void *ctx, int64_t entity) {
  nav_t *n = ctx;
  return n->host.entity_weapon(n->host.ctx, entity);
}
static bool th_travel_weapon(void *ctx, int64_t client, const char *mode, int64_t *out) {
  nav_t *n = ctx;
  return n->host.travel_weapon(n->host.ctx, client, mode, out);
}
static qa_bot_travel_prediction_result th_predict(void *ctx, qa_bot_movement_prediction query) {
  nav_t *n = ctx;
  return n->host.predict_client_movement(n->host.ctx, query);
}

void qa_source_bot_navigation_bots_a_init(nav_t *n, const qa_source_bot_navigation_host_bots_a *host) {
  memset(n, 0, sizeof *n);
  n->host = *host;
  n->offset = host->runtime->graph->aas_asset != NULL ? 0 : 1;
  qa_source_bot_travel_host *t = &n->travel_host;
  t->actions = host->actions;
  t->move_states = host->move_states;
  t->random = host->random;
  t->ctx = n;
  t->area = th_area;
  t->point_area = th_point_area;
  t->trace_areas = th_trace_areas;
  t->presence_bounds = th_presence_bounds;
  t->swimming = th_swimming;
  t->fuzzy_point_reachability_area = th_fuzzy;
  t->reachability_area = th_reachability_area;
  t->runtime = th_runtime;
  t->time = th_time;
  t->point_contents = th_point_contents;
  t->trace = th_trace;
  t->entity_model_index = th_model_index;
  t->model_info = th_model_info;
  t->next_entity = th_next_entity;
  t->entity_type = th_entity_type;
  t->entity_weapon = th_entity_weapon;
  t->travel_weapon = th_travel_weapon;
  t->predict = th_predict;
  qa_source_bot_travel_init(&n->travel, &n->travel_host);
}
void qa_source_bot_navigation_with_client(nav_t *n, int64_t client, void (*think)(void *ctx), void *ctx) {
  qa_navigation_runtime *previous = n->selected;
  n->selected = n->host.for_client(n->host.ctx, client);
  think(ctx);
  n->selected = previous;
}
bool qa_source_bot_navigation_ready(nav_t *n) { return runtime(n)->graph->node_count > 0; }
int64_t qa_source_bot_navigation_point_area(nav_t *n, qa_vec3 origin) {
  int64_t node = 0;
  return qa_navigation_runtime_area_at(runtime(n), origin, &node) ? source(n, node) : 0;
}
qa_bot_navigation_area qa_source_bot_navigation_area(nav_t *n, int64_t number) {
  qa_navigation_runtime *rt = runtime(n);
  qa_bot_navigation_area out = {0, 0, 0, 0, 0};
  const qa_navigation_node *node = number == 0 ? NULL : qa_navigation_runtime_node(rt, node_of(n, number));
  if (node == NULL)
    return out;
  const qa_aas_asset *asset = rt->graph->aas_asset;
  const qa_aas_setting_rec *settings =
      asset != NULL && node->source.kind == QA_NS_AAS && node->source.primary >= 0 &&
              (size_t)node->source.primary < asset->setting_count
          ? &asset->settings[node->source.primary]
          : NULL;
  size_t outgoing = 0;
  (void)qa_navigation_runtime_outgoing(rt, node->id, &outgoing);
  out.contents = settings != NULL ? settings->contents
                                  : ((node->contents & 1) | ((node->contents & 4) >> 1) |
                                     ((node->contents & 2) << 1));
  out.flags = settings != NULL ? settings->flags : node->flags;
  out.presence_type = settings != NULL ? settings->presence : node->presence;
  out.cluster = settings != NULL ? settings->cluster
                : node->has_source_cluster ? node->source_cluster
                                           : 0;
  out.reachable_area_count = (int64_t)outgoing;
  return out;
}
int64_t *qa_source_bot_navigation_bbox_areas(nav_t *n, qa_bounds bounds, size_t *out_count) {
  int64_t *areas = qa_navigation_runtime_bbox_areas(runtime(n), bounds, out_count);
  for (size_t i = 0; i < *out_count; i++)
    areas[i] = source(n, areas[i]);
  return areas;
}
qa_bot_area_crossing *qa_source_bot_navigation_trace_areas(nav_t *n, qa_vec3 start, qa_vec3 end,
                                                           int64_t maximum, size_t *out_count) {
  size_t count = 0;
  qa_navigation_area_crossing *crossings =
      qa_navigation_runtime_trace_areas(runtime(n), start, end, maximum, &count);
  qa_bot_area_crossing *out = xmalloc(count * sizeof *out);
  for (size_t i = 0; i < count; i++) {
    out[i].area = source(n, crossings[i].area);
    out[i].point = crossings[i].point;
  }
  free(crossings);
  *out_count = count;
  return out;
}
void qa_source_bot_navigation_set_area_enabled(nav_t *n, int64_t area, bool enabled) {
  if (area != 0)
    (void)qa_navigation_runtime_enable_area(runtime(n), node_of(n, area), enabled);
}
static qa_navigation_estimate_result selected_estimate(nav_t *n, qa_area_travel_time_query q) {
  qa_navigation_estimate_result none;
  memset(&none, 0, sizeof none);
  /* AAS_AreaRouteToGoalArea answers 1 for equal areas before it range-tests either. */
  if (q.area == q.goal_area && runtime(n)->graph->aas_asset != NULL) {
    none.is_estimate = true;
    none.travel_time = 1;
    return none;
  }
  if (q.area == 0 || q.goal_area == 0)
    return none;
  qa_navigation_estimate_query query;
  memset(&query, 0, sizeof query);
  query.start_node = node_of(n, q.area);
  query.goal_node = node_of(n, q.goal_area);
  query.has_origin = q.has_origin;
  query.origin = q.origin;
  query.has_travel_flags = true;
  query.travel_flags = q.travel_flags;
  return qa_navigation_runtime_estimate(runtime(n), query);
}
static qa_area_travel_time_query time_query(int64_t area, const qa_vec3 *origin, int64_t goal_area,
                                            int64_t travel_flags) {
  qa_area_travel_time_query q;
  memset(&q, 0, sizeof q);
  q.area = area;
  q.has_origin = origin != NULL;
  if (origin != NULL)
    q.origin = *origin;
  q.goal_area = goal_area;
  q.travel_flags = travel_flags;
  return q;
}
qa_route_result qa_source_bot_navigation_route(nav_t *n, qa_route_query q) {
  qa_navigation_estimate_result result =
      selected_estimate(n, time_query(q.area, &q.origin, q.goal_area, q.travel_flags));
  qa_route_result out = {QA_ROUTE_UNREACHABLE, 0, 0};
  if (!result.is_estimate)
    return out;
  out.kind = QA_ROUTE_FOUND;
  out.travel_time = result.travel_time;
  out.next_reachability = result.has_first_edge ? result.first_edge.id + n->offset : 0;
  return out;
}
double qa_source_bot_navigation_area_travel_time_to_goal(nav_t *n, qa_area_travel_time_query q) {
  qa_navigation_estimate_result result = selected_estimate(n, q);
  return result.is_estimate ? result.travel_time : 0;
}
qa_predicted_route qa_source_bot_navigation_predict_route(nav_t *n, qa_predict_route_query query) {
  const qa_route_query *rq = &query.route;
  qa_navigation_runtime *rt = runtime(n);
  int64_t area = rq->area;
  qa_vec3 origin = rq->origin;
  qa_predicted_route out;
  memset(&out, 0, sizeof out);
  out.end_area = rq->goal_area;
  out.end_position = rq->origin;
  double time = 0;
  qa_bot_navigation_area initial = qa_source_bot_navigation_area(n, rq->area);
#define RESULT(ok, stop)                                                                           \
  do {                                                                                             \
    out.succeeded = (ok);                                                                          \
    out.stop_event = (stop);                                                                       \
    out.time = time;                                                                               \
    return out;                                                                                    \
  } while (0)
#define ADD_TIME(start, travel_time)                                                               \
  do {                                                                                             \
    time += (double)qa_aas_estimate_area_time(initial.presence_type, initial.flags, rq->origin,     \
                                              (start)) +                                           \
            (travel_time);                                                                         \
    if (time > 2147483647.0)                                                                       \
      qa_range_error("predicted route time exceeds source int range");                             \
  } while (0)
  for (int64_t count = 0; area != rq->goal_area &&
                          (query.maximum_areas == 0 || count < query.maximum_areas) &&
                          count < (int64_t)rt->graph->node_count;
       count++) {
    qa_navigation_estimate_result selected =
        selected_estimate(n, time_query(area, &origin, rq->goal_area, rq->travel_flags));
    if (!selected.is_estimate || !selected.has_first_edge)
      RESULT(false, QA_ROUTE_STOP_NO_ROUTE);
    qa_navigation_edge edge = selected.first_edge;
    int64_t flags = qa_navigation_edge_travel_flag(&edge), destination = source(n, edge.to);
    const qa_aas_asset *asset = rt->graph->aas_asset;
    const qa_aas_reach_rec *reach = asset != NULL && edge.source.kind == QA_NS_AAS && edge.id >= 0 &&
                                            (size_t)edge.id < asset->reachability_count
                                        ? &asset->reachability[edge.id]
                                        : NULL;
    double travel_time = reach != NULL ? (double)reach->travel_time
                                       : qa_js_max(1, trunc(fr(edge.travel_seconds * 100)));
    if ((query.stop_event & QA_ROUTE_STOP_USE_TRAVEL_TYPE) != 0) {
      if ((flags & query.stop_travel_flags) != 0) {
        out.end_area = area;
        out.end_contents = qa_source_bot_navigation_area(n, area).contents;
        out.end_travel_flags = flags;
        out.end_position = edge.start;
        RESULT(true, QA_ROUTE_STOP_USE_TRAVEL_TYPE);
      }
      qa_bot_navigation_area d = qa_source_bot_navigation_area(n, destination);
      int64_t contents_flags = qa_aas_area_travel_flags(d.contents, d.flags);
      if ((contents_flags & query.stop_travel_flags) != 0) {
        out.end_area = destination;
        out.end_contents = d.contents;
        out.end_travel_flags = contents_flags;
        out.end_position = edge.end;
        ADD_TIME(edge.start, travel_time);
        RESULT(true, QA_ROUTE_STOP_USE_TRAVEL_TYPE);
      }
    }
    qa_bot_area_crossing *crossed = NULL;
    size_t crossed_count = 0;
    if (reach == NULL)
      crossed = qa_source_bot_navigation_trace_areas(n, edge.start, edge.end, 32, &crossed_count);
    else
      switch (reach->travel_type & 0xffffff) {
      case QA_TRAVEL_BARRIERJUMP:
      case QA_TRAVEL_WATERJUMP:
        crossed = qa_source_bot_navigation_trace_areas(n, edge.start, with_z(edge.start, edge.end.z),
                                                       32, &crossed_count);
        break;
      case QA_TRAVEL_WALKOFFLEDGE:
        crossed = qa_source_bot_navigation_trace_areas(n, with_z(edge.end, edge.start.z), edge.end, 32,
                                                       &crossed_count);
        break;
      case QA_TRAVEL_GRAPPLEHOOK:
        crossed = qa_source_bot_navigation_trace_areas(n, edge.start, edge.end, 32, &crossed_count);
        break;
      default:
        break;
      }
    for (size_t i = 0; i <= crossed_count; i++) {
      int64_t entered = i < crossed_count ? crossed[i].area : destination;
      int64_t contents = qa_source_bot_navigation_area(n, entered).contents;
      if ((query.stop_event & QA_ROUTE_STOP_ENTER_CONTENTS) != 0 && (contents & query.stop_contents) != 0) {
        free(crossed);
        out.end_area = entered;
        out.end_contents = contents;
        out.end_position = edge.end;
        ADD_TIME(edge.start, travel_time);
        RESULT(true, QA_ROUTE_STOP_ENTER_CONTENTS);
      }
      if ((query.stop_event & QA_ROUTE_STOP_ENTER_AREA) != 0 && entered == query.stop_area) {
        free(crossed);
        out.end_area = entered;
        out.end_contents = contents;
        out.end_position = edge.start;
        RESULT(true, QA_ROUTE_STOP_ENTER_AREA);
      }
    }
    free(crossed);
    ADD_TIME(edge.start, travel_time);
    out.end_area = destination;
    out.end_contents = qa_source_bot_navigation_area(n, destination).contents;
    out.end_position = edge.end;
    out.end_travel_flags = flags;
    area = destination;
    origin = edge.end;
    if (query.maximum_time != 0 && time > (double)query.maximum_time)
      break;
  }
  RESULT(area == rq->goal_area, 0);
#undef RESULT
#undef ADD_TIME
}

static size_t neighbors(nav_t *n, int64_t area, int64_t **out) {
  qa_navigation_runtime *rt = runtime(n);
  const qa_aas_asset *asset = rt->graph->aas_asset;
  size_t count = 0;
  if (asset != NULL) {
    if (area < 0 || (size_t)area >= asset->area_count) {
      *out = NULL;
      return 0;
    }
    const qa_aas_area_rec *a = &asset->areas[area];
    int64_t *result = xmalloc((size_t)(a->face_count > 0 ? a->face_count : 0) * sizeof *result);
    for (int64_t i = 0; i < a->face_count; i++) {
      int64_t index = a->first_face + i;
      int64_t face_index = index >= 0 && (size_t)index < asset->face_index_count ? asset->face_indexes[index] : -1;
      int64_t absolute = face_index < 0 ? -face_index : face_index;
      if (index < 0 || (size_t)index >= asset->face_index_count || (size_t)absolute >= asset->face_count)
        qa_range_error("AAS alternative route references an absent face");
      const qa_aas_face_rec *face = &asset->faces[absolute];
      int64_t other = face->front_area == area ? face->back_area : face->front_area;
      if (other != 0)
        result[count++] = other;
    }
    *out = result;
    return count;
  }
  int64_t *result = xmalloc(rt->graph->edge_count * sizeof *result);
  for (size_t i = 0; i < rt->graph->edge_count; i++) {
    const qa_navigation_edge *edge = &rt->graph->edges[i];
    int64_t other;
    if (edge->from == node_of(n, area))
      other = source(n, edge->to);
    else if (edge->to == node_of(n, area))
      other = source(n, edge->from);
    else
      continue;
    bool seen = false;
    for (size_t j = 0; j < count && !seen; j++)
      seen = result[j] == other;
    if (!seen)
      result[count++] = other;
  }
  *out = result;
  return count;
}
typedef struct candidate {
  int64_t area;
  double start, goal;
} candidate;
/* TS centerOf: the AAS area center when the graph is an AAS asset, else the
 * graph node origin; false is TS null. */
static bool center_of(nav_t *n, int64_t area, qa_vec3 *out) {
  qa_navigation_runtime *rt = runtime(n);
  const qa_aas_asset *asset = rt->graph->aas_asset;
  const qa_navigation_node *node = qa_navigation_runtime_node(rt, node_of(n, area));
  if (asset != NULL) {
    if (area < 0 || (size_t)area >= asset->area_count)
      return false;
    *out = asset->areas[area].center;
    return true;
  }
  if (node == NULL)
    return false;
  *out = node->origin;
  return true;
}
static ptrdiff_t find_candidate(const candidate *list, size_t count, int64_t area) {
  for (size_t i = 0; i < count; i++)
    if (list[i].area == area)
      return (ptrdiff_t)i;
  return -1;
}
qa_alternative_goal *qa_source_bot_navigation_alternative_route_goals(nav_t *n,
                                                                      qa_alternative_route_query q,
                                                                      size_t *out_count) {
  *out_count = 0;
  qa_alternative_goal *goals = xmalloc(sizeof *goals);
  if (q.start_area == 0 || q.goal_area == 0)
    return goals;
  qa_navigation_runtime *rt = runtime(n);
  double direct = qa_source_bot_navigation_area_travel_time_to_goal(
      n, time_query(q.start_area, &q.start, q.goal_area, q.travel_flags));
  size_t node_count = rt->graph->node_count, candidate_count = 0;
  candidate *candidates = xmalloc(node_count * sizeof *candidates);
  for (size_t i = 0; i < node_count; i++) {
    int64_t number = source(n, rt->graph->nodes[i].id);
    qa_bot_navigation_area area = qa_source_bot_navigation_area(n, number);
    if ((q.type & QA_ALT_ROUTE_ALL) == 0 &&
        !((q.type & QA_ALT_ROUTE_CLUSTER_PORTALS) != 0 && (area.contents & 8) != 0) &&
        !((q.type & QA_ALT_ROUTE_VIEW_PORTALS) != 0 && (area.contents & 512) != 0))
      continue;
    if (area.reachable_area_count == 0)
      continue;
    double start = qa_source_bot_navigation_area_travel_time_to_goal(
        n, time_query(q.start_area, &q.start, number, q.travel_flags));
    if (start == 0 || start > fr(fr(1.1) * fr(direct)))
      continue;
    double goal = qa_source_bot_navigation_area_travel_time_to_goal(
        n, time_query(number, NULL, q.goal_area, q.travel_flags));
    if (goal == 0 || goal > fr(fr(0.8) * fr(direct)))
      continue;
    ptrdiff_t existing = find_candidate(candidates, candidate_count, number);
    if (existing >= 0) {
      candidates[existing].start = start;
      candidates[existing].goal = goal;
    } else
      candidates[candidate_count++] = (candidate){number, start, goal};
  }
  bool *visited = calloc(candidate_count == 0 ? 1 : candidate_count, sizeof *visited);
  int64_t *cluster = xmalloc(candidate_count * sizeof *cluster);
  if (visited == NULL)
    qa_range_error("Out of memory in bot navigation");
  size_t stack_capacity = 16, goal_count = 0;
  int64_t *stack = xmalloc(stack_capacity * sizeof *stack);
  goals = realloc(goals, (candidate_count == 0 ? 1 : candidate_count) * sizeof *goals);
  if (goals == NULL)
    qa_range_error("Out of memory in bot navigation");
  for (size_t first = 0; first < candidate_count; first++) {
    if (visited[first])
      continue;
    size_t cluster_count = 0, depth = 0;
    stack[depth++] = candidates[first].area;
    while (depth > 0) {
      int64_t current = stack[--depth];
      ptrdiff_t index = find_candidate(candidates, candidate_count, current);
      if (index < 0 || visited[index])
        continue;
      visited[index] = true;
      cluster[cluster_count++] = current;
      int64_t *around = NULL;
      size_t around_count = neighbors(n, current, &around);
      for (size_t k = around_count; k-- > 0;) {
        if (depth == stack_capacity) {
          stack_capacity *= 2;
          stack = realloc(stack, stack_capacity * sizeof *stack);
          if (stack == NULL)
            qa_range_error("Out of memory in bot navigation");
        }
        stack[depth++] = around[k];
      }
      free(around);
    }
    qa_vec3 center = {0, 0, 0}, origin;
    for (size_t k = 0; k < cluster_count; k++)
      if (center_of(n, cluster[k], &origin))
        center = qa_add3(center, origin);
    center = qa_scale3(center, 1.0 / (double)cluster_count);
    int64_t best_area = candidates[first].area;
    double best_distance = 999999;
    for (size_t k = 0; k < cluster_count; k++) {
      if (!center_of(n, cluster[k], &origin))
        continue;
      double distance = qa_length3(qa_sub3(origin, center));
      if (distance < best_distance) {
        best_area = cluster[k];
        best_distance = distance;
      }
    }
    bool has_origin = center_of(n, best_area, &origin);
    ptrdiff_t times = find_candidate(candidates, candidate_count, best_area);
    if (!has_origin || times < 0)
      qa_range_error("Alternative route candidate vanished");
    qa_alternative_goal *g = &goals[goal_count++];
    g->origin = origin;
    g->area = best_area;
    g->start_travel_time = candidates[times].start;
    g->goal_travel_time = candidates[times].goal;
    g->extra_travel_time =
        (double)((int64_t)(candidates[times].start + candidates[times].goal - direct) & 65535);
    if ((int64_t)goal_count >= q.maximum_goals)
      break;
  }
  free(stack);
  free(cluster);
  free(visited);
  free(candidates);
  *out_count = goal_count;
  return goals;
}
qa_bounds qa_source_bot_navigation_presence_bounds(nav_t *n, int64_t presence) {
  return presence == 4 ? n->host.crouched_bounds : runtime(n)->graph->profile.shape.bounds;
}
qa_goal_drop_result qa_source_bot_navigation_drop_to_floor(nav_t *n, qa_vec3 origin, qa_bounds bounds) {
  qa_bot_server_trace_result trace =
      n->host.trace(n->host.ctx, origin, with_z(origin, origin.z - 100), &bounds, 0, 1);
  qa_goal_drop_result out;
  out.success = clear(&trace);
  out.origin = out.success ? trace.end : origin;
  return out;
}
qa_goal_reachable_position qa_source_bot_navigation_best_reachable_area(nav_t *n, qa_vec3 origin,
                                                                       qa_bounds bounds) {
  qa_vec3 start = origin;
  int64_t area = qa_source_bot_navigation_point_area(n, start);
  qa_vec3 queried[5 * 5 * 9 + 1];
  size_t queried_count = 0;
  queried[queried_count++] = origin;
  for (int i = 0; i < 5 && area == 0; i++)
    for (int j = 0; j < 5 && area == 0; j++)
      for (int k = -1; k <= 1 && area == 0; k++)
        for (int l = -1; l <= 1 && area == 0; l++) {
          start = qa_math_vec3(origin.x + j * 4 * k, origin.y + j * 4 * l, origin.z + i * 4);
          bool seen = false;
          for (size_t q = 0; q < queried_count && !seen; q++)
            seen = queried[q].x == start.x && queried[q].y == start.y && queried[q].z == start.z;
          if (seen)
            continue;
          queried[queried_count++] = start;
          area = qa_source_bot_navigation_point_area(n, start);
        }
  qa_goal_reachable_position out;
  if (area != 0) {
    qa_vec3 end = with_z(start, start.z - 50);
    start = with_z(start, start.z + 0.25);
    /* AAS_TraceClientBBox walks the area tree and ignores entities, so a door or plat linked over the floor
       cannot change the answer. Other graphs drop the hull (TS floorBelow). */
    const qa_aas_asset *asset = runtime(n)->graph->aas_asset;
    bool has_floor;
    qa_vec3 floor;
    if (asset != NULL) {
      qa_aas_trace trace = qa_aas_trace_client_bbox(asset, start, end, 4, NULL, NULL);
      has_floor = !trace.start_solid;
      floor = trace.end;
    } else {
      qa_bounds crouched = qa_source_bot_navigation_presence_bounds(n, 4);
      qa_bot_server_trace_result trace = n->host.trace(n->host.ctx, start, end, &crouched, -1, 1 | 0x10000);
      has_floor = clear(&trace);
      floor = trace.end;
    }
    if (!has_floor) {
      out.area = area;
      out.origin = start;
      return out;
    }
    area = qa_source_bot_navigation_point_area(n, floor);
    if (area != 0) {
      out.area = area;
      out.origin = floor;
      return out;
    }
  }
  /* AAS_LinkEntityClientBBox grows the box by the crouch hull, so a box that touches no
   * area itself still finds one. */
  qa_bounds hull = qa_source_bot_navigation_presence_bounds(n, 4);
  qa_bounds box = {qa_sub3(qa_add3(origin, bounds.min), hull.max), qa_sub3(qa_add3(origin, bounds.max), hull.min)};
  size_t count = 0;
  int64_t *areas = qa_source_bot_navigation_bbox_areas(n, box, &count);
  out.origin = origin;
  out.area = count > 0 ? areas[0] : 0;
  for (size_t i = 0; i < count; i++)
    if ((qa_source_bot_navigation_area(n, areas[i]).flags & 5) != 0) {
      out.area = areas[i];
      break;
    }
  free(areas);
  return out;
}
int64_t qa_source_bot_navigation_fuzzy_point_reachability_area(nav_t *n, qa_vec3 origin) {
  int64_t first = qa_source_bot_navigation_point_area(n, origin);
  if (first != 0 && qa_source_bot_navigation_area(n, first).reachable_area_count != 0)
    return first;
  size_t count = 0;
  qa_bot_area_crossing *crossings =
      qa_source_bot_navigation_trace_areas(n, origin, with_z(origin, origin.z + 4), 10, &count);
  for (size_t i = 0; i < count; i++)
    if (qa_source_bot_navigation_area(n, crossings[i].area).reachable_area_count != 0) {
      int64_t area = crossings[i].area;
      free(crossings);
      return area;
    }
  free(crossings);
  int64_t best = 0;
  double distance = 999999;
  for (int z = 1; z >= -1; z--) {
    for (int x = 1; x >= -1; x--)
      for (int y = 1; y >= -1; y--) {
        qa_vec3 end = qa_math_vec3(origin.x + x * 8, origin.y + y * 8, origin.z + z * 12);
        crossings = qa_source_bot_navigation_trace_areas(n, origin, end, 10, &count);
        for (size_t i = 0; i < count; i++) {
          if (qa_source_bot_navigation_area(n, crossings[i].area).reachable_area_count != 0) {
            double candidate = qa_length3(qa_sub3(crossings[i].point, origin));
            if (candidate < distance) {
              distance = candidate;
              best = crossings[i].area;
            }
          }
          if (first == 0)
            first = crossings[i].area;
        }
        free(crossings);
      }
    if (best != 0)
      return best;
  }
  return first;
}
int64_t qa_source_bot_navigation_reachability_area(nav_t *n, qa_vec3 origin, int64_t client) {
  qa_bounds crouched = qa_source_bot_navigation_presence_bounds(n, 4);
  qa_bot_server_trace_result trace =
      n->host.trace(n->host.ctx, origin, with_z(origin, origin.z - 3), &crouched, client, 1 | 0x10000);
  if (clear(&trace) && trace.fraction < 1 && trace.entity_num != 1023) {
    if (trace.entity_num == 1022)
      return qa_source_bot_navigation_fuzzy_point_reachability_area(n, origin);
    int64_t model = n->host.entity_model_index(n->host.ctx, (int64_t)trace.entity_num);
    const qa_navigation_edge *mover = qa_navigation_runtime_mover_edge(runtime(n), model);
    if (mover != NULL)
      return source(n, mover->to);
    if (qa_source_bot_navigation_swimming(n, origin))
      return qa_source_bot_navigation_fuzzy_point_reachability_area(n, origin);
    int64_t area = qa_source_bot_navigation_fuzzy_point_reachability_area(n, origin);
    if (area != 0 && qa_source_bot_navigation_area(n, area).reachable_area_count != 0)
      return area;
    qa_bot_server_trace_result floor =
        n->host.trace(n->host.ctx, origin, with_z(origin, origin.z - 800), &crouched, -1, 1 | 0x10000);
    return qa_source_bot_navigation_fuzzy_point_reachability_area(n, !clear(&floor) ? origin : floor.end);
  }
  return qa_source_bot_navigation_fuzzy_point_reachability_area(n, origin);
}
/* TS text(bytes): the bytes before the first NUL, or "" when none is written. */
static const char *bsp_text(uint8_t *bytes, size_t length) {
  return memchr(bytes, 0, length) != NULL ? (const char *)bytes : "";
}
typedef struct jump_pad_info { qa_vec3 start; qa_bounds bounds; qa_vec3 velocity; } jump_pad_info;
/* AAS_GetJumpPadInfo for one trigger_push: the launch point on the floor of its
 * brush, the brush bounds and the launch velocity toward its target. */
static bool read_jump_pad(nav_t *n, int64_t entity, jump_pad_info *out) {
  qa_aas_bsp_entities *entities = n->host.bsp_entities;
  uint8_t model[128] = {0}, target[128] = {0}, name[128] = {0};
  (void)qa_aas_bsp_entities_value(entities, entity, "model", model, sizeof model);
  const char *model_text = bsp_text(model, sizeof model);
  qa_bounds bounds; /* text(model).slice(1) */
  if (!n->host.model_bounds(n->host.ctx, model[0] == 0 ? 0 : qa_native_atoi(model_text[0] != 0 ? model_text + 1 : model_text), &bounds))
    return false;
  qa_vec3 center = qa_scale3(qa_add3(bounds.min, bounds.max), 0.5);
  qa_bounds crouched = qa_source_bot_navigation_presence_bounds(n, 4);
  qa_bot_server_trace_result floor = n->host.trace(n->host.ctx, qa_math_vec3(center.x, center.y, center.z + 64), center,
                                                   &crouched, -1, 1 | 0x10000);
  qa_vec3 bottom = !clear(&floor) ? center : floor.end;
  qa_vec3 start = qa_math_vec3(bottom.x, bottom.y, bottom.z + 0.125);
  (void)qa_aas_bsp_entities_value(entities, entity, "target", target, sizeof target);
  const char *target_name = bsp_text(target, sizeof target);
  int64_t destination = qa_aas_bsp_entities_next_entity(entities, 0);
  for (; destination != 0; destination = qa_aas_bsp_entities_next_entity(entities, destination))
    if (qa_aas_bsp_entities_value(entities, destination, "targetname", name, sizeof name) &&
        strcmp(bsp_text(name, sizeof name), target_name) == 0)
      break;
  if (destination == 0)
    return false;
  double gravity = n->host.move_states->sv_gravity != NULL ? (double)qa_bot_lib_var_value(n->host.move_states->sv_gravity) : 800;
  qa_vec3 point = qa_aas_bsp_entities_vector(entities, destination, "origin").value;
  double time = fr(sqrt(fr(fr(point.z - center.z) / fr(0.5 * gravity))));
  if (!(time > 0))
    return false;
  qa_vec3 delta = qa_sub3(point, center);
  qa_vec3 push = qa_scale3(qa_normalize3(delta), fr(fr(qa_length3(delta) / time) * fr(1.1)));
  out->start = start;
  out->bounds = bounds;
  out->velocity = qa_math_vec3(push.x, push.y, fr(time * gravity));
  return true;
}
/* AAS_BestReachableFromJumpPadArea: the largest jump-pad area of the first
 * trigger_push whose launch flight hits the item box. */
int64_t qa_source_bot_navigation_best_reachable_from_jump_pad_area(nav_t *n, qa_vec3 origin,
                                                                   qa_bounds bounds) {
  qa_aas_bsp_entities *entities = n->host.bsp_entities;
  qa_bounds box = {qa_add3(origin, bounds.min), qa_add3(origin, bounds.max)};
  qa_bounds hull = qa_source_bot_navigation_presence_bounds(n, 4);
  uint8_t classname[128] = {0};
  for (int64_t entity = qa_aas_bsp_entities_next_entity(entities, 0); entity != 0;
       entity = qa_aas_bsp_entities_next_entity(entities, entity)) {
    if (!qa_aas_bsp_entities_value(entities, entity, "classname", classname, sizeof classname) ||
        strcmp(bsp_text(classname, sizeof classname), "trigger_push") != 0)
      continue;
    jump_pad_info pad;
    if (!read_jump_pad(n, entity, &pad))
      continue;
    size_t count = 0, kept = 0;
    int64_t *areas = qa_source_bot_navigation_bbox_areas(
        n, (qa_bounds){qa_sub3(pad.bounds.min, hull.max), qa_sub3(pad.bounds.max, hull.min)}, &count);
    for (size_t i = 0; i < count; i++)
      if ((qa_source_bot_navigation_area(n, areas[i]).contents & 128) != 0)
        areas[kept++] = areas[i];
    if (kept == 0) {
      free(areas);
      continue;
    }
    qa_bot_movement_prediction query;
    memset(&query, 0, sizeof query);
    query.entity_num = -1;
    query.origin = pad.start;
    query.presence = 2;
    query.on_ground = false;
    query.velocity = pad.velocity;
    query.command_move = qa_math_vec3(pad.velocity.x, pad.velocity.y, 0);
    query.command_frames = 0;
    query.max_frames = 30;
    query.frame_time = fr(0.1);
    query.stop_events = 2048;
    query.stop_area = 0;
    query.has_hit_box = true;
    query.hit_box = box;
    query.visualize = false;
    qa_bot_travel_prediction_result move = n->host.predict_client_movement(n->host.ctx, query);
    if ((qa_js_to_int32(move.stop_event) & 2048) == 0) {
      free(areas);
      continue;
    }
    /* The loaded graph keeps area boxes but not the faces AAS_AreaVolume sums, so
     * the box volume ranks the pad areas. */
    int64_t best = 0;
    double volume = 0;
    for (size_t i = 0; i < kept; i++) {
      const qa_navigation_node *node = qa_navigation_runtime_node(runtime(n), node_of(n, areas[i]));
      if (node == NULL)
        continue;
      qa_vec3 size = qa_sub3(node->bounds.max, node->bounds.min);
      double candidate = fr(fr(size.x * size.y) * size.z);
      if (candidate >= volume) {
        best = areas[i];
        volume = candidate;
      }
    }
    free(areas);
    return best;
  }
  return 0;
}
bool qa_source_bot_navigation_swimming(nav_t *n, qa_vec3 origin) {
  return (n->host.point_contents(n->host.ctx, qa_add3(origin, qa_math_vec3(0, 0, -2))) & (8 | 16 | 32)) != 0;
}
qa_vec3 qa_source_bot_navigation_predict_client_movement_end(nav_t *n, qa_bot_movement_prediction query) {
  return n->host.predict_client_movement(n->host.ctx, query).end;
}
typedef struct direction_think {
  nav_t *n;
  int64_t handle, type;
  qa_vec3 direction;
  double speed;
  bool result;
  qa_bot_move_result *move;
  qa_bot_goal goal;
} direction_think;
static void move_in_direction_think(void *ctx) {
  direction_think *t = ctx;
  t->result = qa_source_bot_travel_move_in_direction(&t->n->travel, t->handle, t->direction, t->speed, t->type);
}
bool qa_source_bot_navigation_move_in_direction(nav_t *n, int64_t handle, qa_vec3 direction,
                                                double speed, int64_t move_type) {
  qa_bot_move_state *state = qa_bot_move_state_store_from_handle(n->host.move_states, handle);
  if (state == NULL)
    return false;
  direction_think t;
  memset(&t, 0, sizeof t);
  t.n = n;
  t.handle = handle;
  t.type = move_type;
  t.direction = direction;
  t.speed = speed;
  qa_source_bot_navigation_with_client(n, state->src->client, move_in_direction_think, &t);
  return t.result;
}
static void move_to_goal_think(void *ctx) {
  direction_think *t = ctx;
  qa_source_bot_travel_move_to_goal(&t->n->travel, t->move, t->handle, &t->goal, t->type);
}
void qa_source_bot_navigation_move_to_goal(nav_t *n, qa_bot_move_result *result, int64_t handle,
                                           qa_bot_goal goal, int64_t travel_flags) {
  qa_bot_move_state *state = qa_bot_move_state_store_from_handle(n->host.move_states, handle);
  if (state == NULL) {
    result->failure = true;
    return;
  }
  direction_think t;
  memset(&t, 0, sizeof t);
  t.n = n;
  t.handle = handle;
  t.type = travel_flags;
  t.move = result;
  t.goal = goal;
  qa_source_bot_navigation_with_client(n, state->src->client, move_to_goal_think, &t);
}
/* BotAddToTarget: walks start to end while the look-ahead budget lasts and
 * reports whether the budget ran out. */
static bool add_to_target(qa_vec3 start, qa_vec3 end, double maximum, double *travelled, qa_vec3 *target) {
  qa_vec3 direction = qa_sub3(end, start), unit = qa_normalize3(direction);
  double length = qa_length3(direction);
  if (fr(*travelled + length) < maximum) {
    *target = end;
    *travelled = fr(*travelled + length);
    return false;
  }
  *target = qa_add3(start, qa_scale3(unit, fr(maximum - *travelled)));
  *travelled = maximum;
  return true;
}
bool qa_source_bot_navigation_movement_view_target(nav_t *n, int64_t handle, qa_bot_goal goal,
                                                   int64_t travel_flags, double look_ahead,
                                                   qa_vec3 *out) {
  qa_bot_move_state *state = qa_bot_move_state_store_from_handle(n->host.move_states, handle);
  if (state == NULL || state->src->last_reachability == 0)
    return false;
  qa_travel_graph graph;
  qa_travel_graph_init(&graph, runtime(n));
  graph.time = fr(n->host.time(n->host.ctx));
  double travelled = 0;
  qa_vec3 end = qa_bot_v3(state->src->origin);
  int64_t number = state->src->last_reachability;
  while (number != 0 && travelled < look_ahead) {
    qa_travel_reachability reach;
    if (!qa_travel_graph_reach(&graph, number, &reach))
      break;
    if (add_to_target(end, reach.reach.start, look_ahead, &travelled, out))
      return true;
    int64_t type = reach.reach.travel_type & QA_TRAVEL_MASK;
    /* Never look beyond a teleporter or a weapon jump. */
    if (type == QA_TRAVEL_TELEPORT || type == QA_TRAVEL_ROCKETJUMP || type == QA_TRAVEL_BFGJUMP)
      return true;
    if (type != QA_TRAVEL_JUMPPAD && type != QA_TRAVEL_ELEVATOR && type != QA_TRAVEL_FUNCBOB &&
        add_to_target(reach.reach.start, reach.reach.end, look_ahead, &travelled, out))
      return true;
    qa_travel_selection_start from = {reach.reach.area, reach.reach.end, state->src->last_goal_area,
                                      state->src->last_area, state};
    number = qa_travel_graph_select_from(&graph, &goal, travel_flags, travel_flags, &from).reachability;
    end = reach.reach.end;
  }
  return false;
}
bool qa_source_bot_navigation_predict_visible_position(nav_t *n, qa_vec3 origin, int64_t area,
                                                       qa_bot_goal goal, int64_t travel_flags,
                                                       qa_vec3 *out) {
  if (area == 0)
    return false;
  if (area == goal.area) {
    *out = goal.origin;
    return true;
  }
  qa_travel_graph graph;
  qa_travel_graph_init(&graph, runtime(n));
  graph.time = fr(n->host.time(n->host.ctx));
  qa_travel_selection_start from = {area, origin, -1, -1, NULL};
  int64_t number = qa_travel_graph_select_from(&graph, &goal, travel_flags, travel_flags, &from).reachability;
  for (int hop = 0; hop < 20 && number != 0; hop++) {
    qa_travel_reachability reach;
    if (!qa_travel_graph_reach(&graph, number, &reach))
      break;
    if (reach.reach.area == goal.area) {
      *out = reach.reach.end;
      return true;
    }
    qa_bot_server_trace_result trace = n->host.trace(n->host.ctx, reach.reach.end, goal.origin, NULL, -1, 1);
    if (trace.fraction == 1 && clear(&trace)) {
      *out = reach.reach.end;
      return true;
    }
    from = (qa_travel_selection_start){reach.reach.area, reach.reach.end, -1, -1, NULL};
    number = qa_travel_graph_select_from(&graph, &goal, travel_flags, travel_flags, &from).reachability;
  }
  return false;
}

/* ---- BotNavigation / GoalNavigation views ---- */
static bool i_ready(void *ctx) { return qa_source_bot_navigation_ready(ctx); }
static void i_with_client(void *ctx, int64_t client, void (*think)(void *), void *think_ctx) {
  qa_source_bot_navigation_with_client(ctx, client, think, think_ctx);
}
static qa_bot_area_crossing *i_trace_areas(void *ctx, qa_vec3 s, qa_vec3 e, int64_t max, size_t *c) {
  return qa_source_bot_navigation_trace_areas(ctx, s, e, max, c);
}
static int64_t *i_bbox(void *ctx, qa_bounds b, size_t *c) { return qa_source_bot_navigation_bbox_areas(ctx, b, c); }
static void i_set_enabled(void *ctx, int64_t a, bool e) { qa_source_bot_navigation_set_area_enabled(ctx, a, e); }
static double i_travel_time(void *ctx, qa_area_travel_time_query q) {
  return qa_source_bot_navigation_area_travel_time_to_goal(ctx, q);
}
static qa_route_result i_route(void *ctx, qa_route_query q) { return qa_source_bot_navigation_route(ctx, q); }
static qa_predicted_route i_predict_route(void *ctx, qa_predict_route_query q) {
  return qa_source_bot_navigation_predict_route(ctx, q);
}
static qa_alternative_goal *i_alternatives(void *ctx, qa_alternative_route_query q, size_t *c) {
  return qa_source_bot_navigation_alternative_route_goals(ctx, q, c);
}
static void i_move_to_goal(void *ctx, qa_bot_move_result *r, int64_t h, qa_bot_goal g, int64_t f) {
  qa_source_bot_navigation_move_to_goal(ctx, r, h, g, f);
}
static bool i_move_in_direction(void *ctx, int64_t h, qa_vec3 d, double s, int64_t t) {
  return qa_source_bot_navigation_move_in_direction(ctx, h, d, s, t);
}
static bool i_view_target(void *ctx, int64_t h, qa_bot_goal g, int64_t f, double l, qa_vec3 *o) {
  return qa_source_bot_navigation_movement_view_target(ctx, h, g, f, l, o);
}
static bool i_visible(void *ctx, qa_vec3 o, int64_t a, qa_bot_goal g, int64_t f, qa_vec3 *out) {
  return qa_source_bot_navigation_predict_visible_position(ctx, o, a, g, f, out);
}
static qa_vec3 i_predict_end(void *ctx, qa_bot_movement_prediction q) {
  return qa_source_bot_navigation_predict_client_movement_end(ctx, q);
}
qa_bot_navigation_bots_a qa_source_bot_navigation_interface(nav_t *n) {
  qa_bot_navigation_bots_a v;
  memset(&v, 0, sizeof v);
  v.ctx = n;
  v.ready = i_ready;
  v.with_client = i_with_client;
  v.point_area = th_point_area;
  v.reachability_area = th_reachability_area;
  v.fuzzy_point_reachability_area = th_fuzzy;
  v.get_area = th_area;
  v.trace_areas = i_trace_areas;
  v.bbox_areas = i_bbox;
  v.set_area_enabled = i_set_enabled;
  v.area_travel_time_to_goal = i_travel_time;
  v.route = i_route;
  v.predict_route = i_predict_route;
  v.alternative_route_goals = i_alternatives;
  v.move_to_goal = i_move_to_goal;
  v.move_in_direction = i_move_in_direction;
  v.movement_view_target = i_view_target;
  v.predict_visible_position = i_visible;
  v.swimming = th_swimming;
  v.predict_client_movement_end = i_predict_end;
  v.presence_bounds = th_presence_bounds;
  return v;
}
static qa_goal_area_info g_area(void *ctx, int64_t number) {
  qa_bot_navigation_area a = qa_source_bot_navigation_area(ctx, number);
  qa_goal_area_info out = {a.contents, a.reachable_area_count};
  return out;
}
static int64_t g_jump_pad(void *ctx, qa_vec3 o, qa_bounds b) {
  return qa_source_bot_navigation_best_reachable_from_jump_pad_area(ctx, o, b);
}
static qa_goal_drop_result g_drop(void *ctx, qa_vec3 o, qa_bounds b) {
  return qa_source_bot_navigation_drop_to_floor(ctx, o, b);
}
static qa_goal_reachable_position g_best(void *ctx, qa_vec3 o, qa_bounds b) {
  return qa_source_bot_navigation_best_reachable_area(ctx, o, b);
}
static qa_goal_route_result g_route(void *ctx, const qa_goal_route_request *r) {
  qa_route_query q = {r->area, r->origin, r->goal_area, r->travel_flags};
  qa_route_result result = qa_source_bot_navigation_route(ctx, q);
  qa_goal_route_result out = {result.kind == QA_ROUTE_FOUND, result.travel_time};
  return out;
}
qa_goal_navigation qa_source_bot_navigation_goal_interface(nav_t *n) {
  qa_goal_navigation v;
  v.ctx = n;
  v.area = g_area;
  v.best_reachable_from_jump_pad_area = g_jump_pad;
  v.drop_to_floor = g_drop;
  v.best_reachable_area = g_best;
  v.reachability_area = th_reachability_area;
  v.route = g_route;
  return v;
}
