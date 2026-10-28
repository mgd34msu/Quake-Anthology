/* Port of src/bots/behavior/rerelease/nav.ts. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/nav.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void qa_nav_path_destroy(qa_nav_path *path) {
  if (!path) return;
  free(path->nodes); free(path->points); free(path->links); free(path->has_link);
  memset(path, 0, sizeof *path);
}
qa_nav_traverse_caps qa_default_traverse_caps(void) {
  qa_nav_traverse_caps caps; memset(&caps, 0, sizeof caps);
  caps.jump = caps.walk_off_ledge = caps.entity_traversal = caps.swim = true;
  return caps;
}
bool qa_nav_link_is_jump(qa_nav_link_type type) {
  return type == QA_NAV_LINK_LONG_JUMP || type == QA_NAV_LINK_BARRIER_JUMP || type == QA_NAV_LINK_MANUAL_LONG_JUMP;
}
bool qa_nav_link_is_entity(qa_nav_link_type type) {
  return type == QA_NAV_LINK_TELEPORT || type == QA_NAV_LINK_PUSHER || type == QA_NAV_LINK_ELEVATOR || type == QA_NAV_LINK_TRAIN;
}
qa_bot_vec3 qa_steer_direction(qa_bot_vec3 from, qa_bot_vec3 to) {
  qa_bot_vec3 direction = qa_bvec_sub(to, from);
  if (fabs(direction.x) < 0.001 && fabs(direction.y) < 0.001) return qa_bvec(0, 0, 0);
  return qa_bvec_normalized(qa_bvec(direction.x, direction.y, 0));
}
static qa_nav_graph_node node_view(const qa_navigation_node *node) {
  return (qa_nav_graph_node){node->id, node->origin, node->radius, node->flags};
}
static bool link_type_of(const qa_navigation_edge *edge, qa_nav_link_type *out) {
  switch (edge->mode) {
    case QA_TM_WALK: case QA_TM_SWIM: *out = QA_NAV_LINK_WALK; return true;
    case QA_TM_CROUCH: *out = QA_NAV_LINK_CROUCH; return true;
    case QA_TM_LADDER: *out = QA_NAV_LINK_LADDER; return true;
    case QA_TM_JUMP: case QA_TM_WATER_JUMP:
      *out = edge->source_travel_type == 5 ? QA_NAV_LINK_BARRIER_JUMP : QA_NAV_LINK_LONG_JUMP; return true;
    case QA_TM_DROP: *out = QA_NAV_LINK_WALK_OFF_LEDGE; return true;
    case QA_TM_TELEPORT: *out = QA_NAV_LINK_TELEPORT; return true;
    case QA_TM_JUMP_PAD: *out = QA_NAV_LINK_PUSHER; return true;
    case QA_TM_MOVER: *out = edge->source_travel_type == 7 ? QA_NAV_LINK_TRAIN : QA_NAV_LINK_ELEVATOR; return true;
    default: return false;
  }
}
static qa_nav_graph_link link_view(const qa_navigation_edge *edge, qa_nav_link_type type) {
  qa_nav_graph_link link; memset(&link, 0, sizeof link);
  link.from = edge->from; link.to = edge->to; link.type = type;
  if (edge->has_hint) {
    link.has_traversal = true;
    link.traversal = (qa_nav_traversal){edge->hint.funnel, edge->start, edge->end};
  }
  if (edge->has_entity) {
    link.has_entity_bounds = true;
    link.entity_mins = edge->entity.bounds.min;
    link.entity_maxs = edge->entity.bounds.max;
  }
  return link;
}
void qa_source_rerelease_navigation_init(qa_source_rerelease_navigation *nav, qa_navigation_runtime *runtime,
                                         void *ground_ctx, qa_nav_ground_fn ground) {
  nav->runtime = runtime; nav->ground_ctx = ground_ctx; nav->ground = ground;
}
qa_bot_transport_step qa_source_rerelease_navigation_transport(qa_source_rerelease_navigation *nav,
                                                               qa_nav_graph_link link, qa_bot_vec3 origin) {
  qa_bot_transport_step none; memset(&none, 0, sizeof none);
  if (link.type != QA_NAV_LINK_TRAIN) return none;
  size_t count = 0;
  const qa_navigation_edge *const *edges = qa_navigation_runtime_outgoing(nav->runtime, link.from, &count);
  const qa_navigation_edge *found = NULL;
  for (size_t i = 0; i < count; i++) {
    const qa_navigation_edge *candidate = edges[i];
    qa_nav_link_type type;
    if (candidate->to != link.to || !link_type_of(candidate, &type) || type != QA_NAV_LINK_TRAIN) continue;
    if (link.has_traversal &&
        !(qa_bvec_distance(candidate->start, link.traversal.start) == 0 &&
          qa_bvec_distance(candidate->end, link.traversal.end) == 0)) continue;
    found = candidate; break;
  }
  qa_bot_transport_step unavailable = {true, QA_TRANSPORT_UNAVAILABLE, false, {0, 0, 0}};
  if (!found) return unavailable;
  qa_actor_id ground_id; bool has_ground = nav->ground && nav->ground(nav->ground_ctx, &ground_id);
  qa_bot_transport_step step = qa_navigation_runtime_train_step(nav->runtime, found, origin, has_ground ? &ground_id : NULL);
  return step.has ? step : unavailable;
}
size_t qa_source_rerelease_navigation_node_count(qa_source_rerelease_navigation *nav) {
  return nav->runtime->graph->node_count;
}
size_t qa_source_rerelease_navigation_nodes(qa_source_rerelease_navigation *nav, qa_nav_graph_node *out, size_t cap) {
  size_t n = nav->runtime->graph->node_count < cap ? nav->runtime->graph->node_count : cap;
  for (size_t i = 0; i < n; i++) out[i] = node_view(&nav->runtime->graph->nodes[i]);
  return n;
}
bool qa_source_rerelease_navigation_path_valid(qa_source_rerelease_navigation *nav, const qa_nav_path *path) {
  const char *digest = nav->runtime->graph->map.digest;
  return path->generation == qa_navigation_runtime_generation(nav->runtime) &&
         path->map_digest && digest && !strcmp(path->map_digest, digest);
}
typedef struct { qa_nav_traverse_caps caps; } EligibleCtx;
static bool eligible_filter(void *ctx, const qa_navigation_edge *edge) {
  const qa_nav_traverse_caps *caps = ctx;
  qa_nav_link_type type;
  if (!link_type_of(edge, &type)) return false;
  if ((!caps->jump && qa_nav_link_is_jump(type)) || (!caps->entity_traversal && qa_nav_link_is_entity(type)) ||
      (!caps->walk_off_ledge && type == QA_NAV_LINK_WALK_OFF_LEDGE) || (!caps->swim && edge->mode == QA_TM_SWIM)) return false;
  if (caps->max_drop > 0 && edge->mode != QA_TM_MOVER && edge->start.z - edge->end.z >= caps->max_drop) return false;
  if (caps->max_jump_height > 0 && qa_nav_link_is_jump(type) && edge->end.z - edge->start.z >= caps->max_jump_height) return false;
  return true;
}
static bool nearest_node(qa_source_rerelease_navigation *nav, qa_bot_vec3 point, const qa_nav_plan_options *options,
                         const int64_t *disabled, size_t disabled_count, bool has_above, double above, int64_t *out) {
  bool has = false;
  int64_t selected = 0;
  double best = options && options->has_max_radius ? options->max_radius : 512;
  for (size_t i = 0; i < nav->runtime->graph->node_count; i++) {
    const qa_navigation_node *node = &nav->runtime->graph->nodes[i];
    bool dis = false;
    for (size_t j = 0; j < disabled_count; j++) if (disabled[j] == node->id) dis = true;
    if (dis || (has_above && node->origin.z > point.z + above)) continue;
    double distance = qa_bvec_distance(point, node->origin);
    if (distance > best) continue;
    if (options && options->visible && !options->visible(options->visible_ctx, point, node->origin)) continue;
    selected = node->id; best = distance; has = true;
  }
  if (has) *out = selected;
  return has;
}
typedef struct {
  qa_bot_vec3 *points; size_t point_count, point_cap;
  qa_nav_graph_link *links; bool *has_link;
} AppendState;
static void append_point(AppendState *st, qa_bot_vec3 point, bool has_link, qa_nav_graph_link link) {
  if (st->point_count > 0) {
    qa_bot_vec3 previous = st->points[st->point_count - 1];
    if (qa_bvec_distance(previous, point) < 1) {
      if (has_link) { st->links[st->point_count - 1] = link; st->has_link[st->point_count - 1] = true; }
      return;
    }
  }
  if (st->point_count == st->point_cap) {
    size_t c = st->point_cap ? st->point_cap * 2 : 16;
    st->points = realloc(st->points, c * sizeof *st->points);
    st->links = realloc(st->links, c * sizeof *st->links);
    st->has_link = realloc(st->has_link, c * sizeof *st->has_link);
    if (!st->points || !st->links || !st->has_link) qa_range_error("Out of memory");
    st->point_cap = c;
  }
  st->points[st->point_count] = point;
  st->links[st->point_count] = link;
  st->has_link[st->point_count] = has_link;
  st->point_count++;
}
bool qa_source_rerelease_navigation_plan_path(qa_source_rerelease_navigation *nav, qa_bot_vec3 start, qa_bot_vec3 goal,
                                              const qa_nav_plan_options *options, qa_nav_path *out) {
  memset(out, 0, sizeof *out);
  qa_nav_plan_options empty; memset(&empty, 0, sizeof empty);
  if (!options) options = &empty;
  qa_nav_traverse_caps caps = options->has_caps ? options->caps : qa_default_traverse_caps();
  int64_t *disabled = NULL; size_t disabled_count = 0, disabled_cap = 0;
  if (caps.avoid) {
    for (size_t i = 0; i < nav->runtime->graph->node_count; i++) {
      const qa_navigation_node *node = &nav->runtime->graph->nodes[i];
      if (caps.avoid(caps.avoid_ctx, node_view(node))) {
        if (disabled_count == disabled_cap) { disabled_cap = disabled_cap ? disabled_cap * 2 : 8; disabled = realloc(disabled, disabled_cap * sizeof *disabled); if (!disabled) qa_range_error("Out of memory"); }
        disabled[disabled_count++] = node->id;
      }
    }
  }
  int64_t start_node = 0, goal_node = 0;
  bool has_start = nearest_node(nav, start, options, disabled, disabled_count, options->has_start_above, options->start_above, &start_node);
  bool has_goal = nearest_node(nav, goal, options, disabled, disabled_count, false, 0, &goal_node);
  if (!has_start || !has_goal) { free(disabled); return false; }
  qa_navigation_route_query query; memset(&query, 0, sizeof query);
  query.start = start; query.goal = goal;
  query.has_start_node = true; query.start_node = start_node;
  query.has_goal_node = true; query.goal_node = goal_node;
  query.disabled_areas = disabled; query.disabled_area_count = disabled_count;
  query.edge_filter_ctx = &caps; query.edge_filter = eligible_filter;
  qa_navigation_route_result result = qa_navigation_runtime_route(nav->runtime, query);
  free(disabled);
  if (!result.is_route) return false;
  qa_navigation_route *route = &result.route;
  AppendState st; memset(&st, 0, sizeof st);
  size_t cursor = 0;
  for (size_t e = 0; e < route->edge_count; e++) {
    const qa_navigation_edge *edge = &route->edges[e];
    qa_nav_link_type type;
    if (!link_type_of(edge, &type)) qa_range_error("Navigation admitted an unsupported rerelease command traversal");
    qa_nav_graph_link link = link_view(edge, type);
    /* nearestTrajectoryIndex(edge.start) */
    size_t begin = cursor; double begin_distance = INFINITY;
    for (size_t i = cursor; i < route->point_count; i++) {
      double d = qa_bvec_distance(route->points[i], edge->start);
      if (d < begin_distance) { begin = i; begin_distance = d; }
    }
    if (route->point_count == 0 && cursor == 0) { /* TS would throw on absent point below when edges exist */ }
    for (; cursor < begin; cursor++) append_point(&st, route->points[cursor], false, link);
    size_t end = cursor; double end_distance = INFINITY;
    for (size_t i = cursor; i < route->point_count; i++) {
      double d = qa_bvec_distance(route->points[i], edge->end);
      if (d < end_distance) { end = i; end_distance = d; }
    }
    if (type != QA_NAV_LINK_WALK || edge->has_entity) {
      if (link.has_traversal) { append_point(&st, edge->start, true, link); append_point(&st, edge->end, false, link); }
      else append_point(&st, edge->end, true, link);
      cursor = end + 1;
    } else {
      for (; cursor <= end && cursor < route->point_count; cursor++) append_point(&st, route->points[cursor], false, link);
    }
  }
  for (; cursor < route->point_count; cursor++) append_point(&st, route->points[cursor], false, (qa_nav_graph_link){0});
  out->nodes = route->nodes; out->node_count = route->node_count;
  route->nodes = NULL; route->node_count = 0;
  out->points = st.points; out->point_count = st.point_count;
  out->links = st.links; out->has_link = st.has_link; out->link_count = st.point_count;
  out->cost = route->travel_seconds;
  out->generation = route->generation;
  out->map_digest = route->map.digest;
  qa_navigation_route_destroy(route);
  return true;
}
static size_t iface_node_count(void *ctx) { return qa_source_rerelease_navigation_node_count(ctx); }
static size_t iface_nodes(void *ctx, qa_nav_graph_node *out, size_t cap) { return qa_source_rerelease_navigation_nodes(ctx, out, cap); }
static bool iface_plan(void *ctx, qa_bot_vec3 s, qa_bot_vec3 g, const qa_nav_plan_options *o, qa_nav_path *out) {
  return qa_source_rerelease_navigation_plan_path(ctx, s, g, o, out);
}
static bool iface_valid(void *ctx, const qa_nav_path *p) { return qa_source_rerelease_navigation_path_valid(ctx, p); }
static qa_bot_transport_step iface_transport(void *ctx, qa_nav_graph_link link, qa_bot_vec3 origin) {
  return qa_source_rerelease_navigation_transport(ctx, link, origin);
}
qa_bot_navigation_iface qa_source_rerelease_navigation_as_iface(qa_source_rerelease_navigation *nav) {
  qa_bot_navigation_iface iface; memset(&iface, 0, sizeof iface);
  iface.ctx = nav;
  iface.node_count = iface_node_count;
  iface.nodes = iface_nodes;
  iface.plan_path = iface_plan;
  iface.path_valid = iface_valid;
  iface.has_transport = true;
  iface.transport = iface_transport;
  return iface;
}
