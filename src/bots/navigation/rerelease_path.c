/* Port of src/bots/navigation/rerelease-path.ts. GPL-2.0-or-later. */
#include "qa/bots/navigation/rerelease_path.h"
#include "qa/bots/navigation/helpers.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void qa_rerelease_path_info_destroy(qa_rerelease_path_info *info) {
  if (!info) return;
  free(info->points);
  memset(info, 0, sizeof *info);
}
static qa_rerelease_path_info failure(int64_t code) {
  qa_rerelease_path_info info; memset(&info, 0, sizeof info);
  info.code = code;
  return info;
}
static bool node_allowed(const qa_navigation_node *node, qa_rerelease_path_request request) {
  if (node->source.kind != QA_NS_NAV3) return true;
  if ((node->flags & 8192) != 0) return false;
  if (request.ignore_node_flags) return (node->flags & 1024) == 0;
  if ((node->flags & (1 | 2 | 8 | 256 | 512)) != 0) return false;
  int64_t medium = request.flags & 3;
  return !((medium == 2 && (node->flags & 16) != 0) || (medium == 1 && (node->flags & 16) == 0) ||
           ((request.flags & 32) == 0 && (node->flags & 4) != 0));
}
static int64_t link_type(const qa_navigation_edge *edge) {
  if (edge->mode == QA_TM_DROP) return 1;
  if (edge->mode == QA_TM_JUMP) return edge->source_travel_type == 5 ? 3 : 2;
  return edge->mode == QA_TM_MOVER ? 4 : 0;
}
static bool link_allowed(const qa_navigation_edge *edge, qa_rerelease_path_request request) {
  if (request.ignore_node_flags) return true;
  if (request.flags == 1 && edge->mode != QA_TM_SWIM && edge->mode != QA_TM_WALK) return false;
  switch (edge->mode) {
    case QA_TM_WALK: case QA_TM_SWIM: return !edge->has_entity;
    case QA_TM_DROP: return (request.flags & 4) != 0 && (request.drop_height <= 0 || edge->start.z - edge->end.z <= request.drop_height);
    case QA_TM_JUMP: return (request.flags & (edge->source_travel_type == 5 ? 16 : 8)) != 0 &&
                            (edge->source_travel_type != 5 || request.jump_height <= 0 || edge->end.z - edge->start.z <= request.jump_height);
    case QA_TM_MOVER: return (request.flags & 32) != 0;
    default: return false;
  }
}
static qa_trace_result do_trace(qa_navigation_runtime *runtime, qa_vec3 start, qa_vec3 end) {
  const qa_navigation_world *world = runtime->world;
  qa_trace_shape shape; memset(&shape, 0, sizeof shape); shape.kind = "point";
  qa_query_target target; memset(&target, 0, sizeof target);
  qa_trace_policy policy; memset(&policy, 0, sizeof policy);
  policy.kind = "q2"; policy.contents_mask = 0x30003; policy.leaf_contents = "merged";
  qa_trace_query q = {start, end, shape, target, policy, runtime->graph->profile.movement.numeric, world->pass_actor};
  return world->scene.trace(world->scene.ctx, q);
}
static const qa_navigation_node *nearest_node(qa_navigation_runtime *runtime, qa_vec3 point, qa_rerelease_path_request request) {
  const qa_navigation_node *selected = NULL;
  double best = request.radius > 0 ? request.radius : 512;
  for (size_t i = 0; i < runtime->graph->node_count; i++) {
    const qa_navigation_node *node = &runtime->graph->nodes[i];
    if (!node_allowed(node, request) ||
        node->origin.z < point.z - (request.min_height > 0 ? request.min_height : 64) ||
        node->origin.z > point.z + (request.max_height > 0 ? request.max_height : 64)) continue;
    double d = hypot(node->origin.x - point.x, node->origin.y - point.y);
    if (d > best) continue;
    qa_vec3 dest = node->origin; dest.z += 32;
    qa_trace_result result = do_trace(runtime, point, dest);
    if (result.fraction < 1 || result.start_solid || result.all_solid) continue;
    selected = node; best = d;
  }
  return selected;
}
typedef struct { int64_t node; double cost; const qa_navigation_edge *previous; bool has_cost; } Cost;
qa_rerelease_path_info qa_rerelease_path_to_goal(qa_navigation_runtime *runtime, qa_rerelease_path_request request) {
  if (!runtime || runtime->graph->node_count == 0) return failure(8);
  if ((request.flags & 3) == 0) return failure(12);
  const qa_navigation_node *start = nearest_node(runtime, request.start, request);
  if (!start) return failure(9);
  const qa_navigation_node *goal = nearest_node(runtime, request.goal, request);
  if (!goal) return failure(10);
  if (start->id == goal->id || qa_nav_distance(request.start, goal->origin) <= request.move_distance) return failure(0);
  if (!request.ignore_node_flags) {
    if (do_trace(runtime, request.start, request.start).start_solid) return failure(6);
    if (do_trace(runtime, request.goal, request.goal).start_solid) return failure(7);
  }
  Cost *costs = NULL; size_t cn = 0, ccap = 0;
  typedef struct { int64_t node; double cost; } Item;
  Item *queue = NULL; size_t qn = 0, qcap = 0;
#define PUSH_COST(id_, cost_, prev_) do { \
    Cost *found_ = NULL; \
    for (size_t i_ = 0; i_ < cn; i_++) if (costs[i_].node == (id_)) found_ = &costs[i_]; \
    if (!found_) { if (cn == ccap) { ccap = ccap ? ccap * 2 : 16; costs = realloc(costs, ccap * sizeof *costs); if (!costs) qa_range_error("Out of memory"); } costs[cn++] = (Cost){(id_), (cost_), (prev_), true}; } \
    else { found_->cost = (cost_); found_->previous = (prev_); found_->has_cost = true; } \
  } while (0)
  PUSH_COST(start->id, 0, NULL);
  queue = malloc(16 * sizeof *queue); if (!queue) qa_range_error("Out of memory");
  qcap = 16; queue[qn++] = (Item){start->id, 0};
  while (qn > 0) {
    size_t best = 0;
    for (size_t i = 1; i < qn; i++) if (queue[i].cost < queue[best].cost) best = i;
    Item current = queue[best];
    memmove(&queue[best], &queue[best + 1], (qn - best - 1) * sizeof *queue);
    qn--;
    Cost *known = NULL;
    for (size_t i = 0; i < cn; i++) if (costs[i].node == current.node) known = &costs[i];
    if (!known || known->cost != current.cost) continue;
    if (current.node == goal->id) break;
    size_t oc = 0;
    const qa_navigation_edge *const *edges = qa_navigation_runtime_outgoing(runtime, current.node, &oc);
    for (size_t i = 0; i < oc; i++) {
      const qa_navigation_edge *edge = edges[i];
      const qa_navigation_node *node = qa_navigation_runtime_node(runtime, edge->to);
      if (!node || !node_allowed(node, request) || !link_allowed(edge, request) ||
          !qa_navigation_runtime_edge_allowed(runtime, edge, NULL)) continue;
      const qa_navigation_node *from_node = qa_navigation_runtime_node(runtime, edge->from);
      qa_vec3 from_origin = from_node ? from_node->origin : edge->start;
      double cost = current.cost + (edge->mode == QA_TM_TELEPORT ? 1 : qa_nav_distance(from_origin, node->origin));
      Cost *existing = NULL;
      for (size_t j = 0; j < cn; j++) if (costs[j].node == edge->to) existing = &costs[j];
      if (cost >= (existing && existing->has_cost ? existing->cost : INFINITY)) continue;
      PUSH_COST(edge->to, cost, edge);
      if (qn == qcap) { qcap *= 2; queue = realloc(queue, qcap * sizeof *queue); if (!queue) qa_range_error("Out of memory"); }
      queue[qn++] = (Item){edge->to, cost};
    }
  }
  free(queue);
  Cost *goal_cost = NULL;
  for (size_t i = 0; i < cn; i++) if (costs[i].node == goal->id) goal_cost = &costs[i];
  if (!goal_cost || !goal_cost->previous) { free(costs); return failure(11); }
  /* rebuild chain: edges/nodes reversed */
  const qa_navigation_edge **edges = NULL; size_t en = 0, ecap = 0;
  const qa_navigation_node **nodes = NULL; size_t nn = 0, ncap = 0;
#define PUSH_PTR(arr, n, cap, v) do { if ((n) == (cap)) { (cap) = (cap) ? (cap) * 2 : 8; (arr) = realloc((arr), (cap) * sizeof *(arr)); if (!(arr)) qa_range_error("Out of memory"); } (arr)[(n)++] = (v); } while (0)
  PUSH_PTR(nodes, nn, ncap, goal);
  int64_t cursor = goal->id;
  while (cursor != start->id) {
    Cost *entry = NULL;
    for (size_t i = 0; i < cn; i++) if (costs[i].node == cursor) entry = &costs[i];
    const qa_navigation_edge *edge = entry ? entry->previous : NULL;
    if (!edge || en >= runtime->graph->node_count) qa_range_error("Invalid shared navigation predecessor chain");
    const qa_navigation_node *node = qa_navigation_runtime_node(runtime, edge->from);
    if (!node) qa_range_error("Missing shared navigation predecessor");
    PUSH_PTR(edges, en, ecap, edge);
    PUSH_PTR(nodes, nn, ncap, node);
    cursor = edge->from;
  }
  free(costs);
  for (size_t i = 0, j = nn ? nn - 1 : 0; i < j; i++, j--) { const qa_navigation_node *t = nodes[i]; nodes[i] = nodes[j]; nodes[j] = t; }
  for (size_t i = 0, j = en ? en - 1 : 0; i < j; i++, j--) { const qa_navigation_edge *t = edges[i]; edges[i] = edges[j]; edges[j] = t; }
  const qa_navigation_edge *edge = en ? edges[0] : NULL;
  size_t first_index = 0;
  if (!request.ignore_node_flags && edge && (edge->mode == QA_TM_WALK || edge->mode == QA_TM_CROUCH)) {
    double dx = request.start.x - start->origin.x, dy = request.start.y - start->origin.y;
    double length = hypot(dx, dy);
    const qa_navigation_node *next = nn > 1 ? nodes[1] : NULL;
    if ((length <= start->radius && fabs(request.start.z - start->origin.z) <= 64) ||
        (length > 0 && next && dx * (next->origin.x - start->origin.x) + dy * (next->origin.y - start->origin.y) > start->radius * length))
      first_index = 1;
  }
  if (first_index >= nn) qa_range_error("Empty shared navigation result");
  const qa_navigation_node *first_node = nodes[first_index];
  double walked = 0;
  qa_vec3 at = request.start;
  for (size_t i = first_index; i < nn; i++) { walked += qa_nav_distance(at, nodes[i]->origin); at = nodes[i]->origin; }
  walked += qa_nav_distance(at, request.goal);
  size_t pn = 0, pcap = 0;
  qa_vec3 *points = NULL;
  for (size_t i = first_index; i < nn; i++) PUSH_PTR(points, pn, pcap, nodes[i]->origin);
  if (qa_nav_distance(request.start, first_node->origin) > 64) {
    if (pn == pcap) { pcap = pcap ? pcap * 2 : 8; points = realloc(points, pcap * sizeof *points); if (!points) qa_range_error("Out of memory"); }
    memmove(&points[1], &points[0], pn * sizeof *points);
    points[0] = request.start; pn++;
  }
  if (qa_nav_distance(goal->origin, request.goal) > 64) PUSH_PTR(points, pn, pcap, request.goal);
  bool traversal = !request.ignore_node_flags && edge && edge->has_hint;
  qa_rerelease_path_info info; memset(&info, 0, sizeof info);
  info.code = request.ignore_node_flags ? 3 : traversal ? 2 : 4;
  info.distance_squared = walked * walked;
  info.points = points; info.point_count = pn;
  qa_vec3 zero = {0, 0, 0};
  info.first = request.ignore_node_flags ? zero : traversal ? edge->start : first_node->origin;
  info.second = request.ignore_node_flags ? zero : traversal ? edge->end
                : (first_index + 1 < nn ? nodes[first_index + 1]->origin : request.goal);
  info.link_type = edge ? link_type(edge) : 0;
  free(edges); free(nodes);
  return info;
#undef PUSH_COST
#undef PUSH_PTR
}
