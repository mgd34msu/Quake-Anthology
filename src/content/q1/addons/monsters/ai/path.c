/* Port of src/content/q1/addons/monsters/ai/path.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ai/path.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

#define FR(x) ((double)(float)(x))

typedef struct nav_path {
  qa_owned_actor actor;
  qa_vec3 goal;
  qa_q1am_nav_route route;
  double cursor;
} nav_path;
typedef struct navigation {
  qa_q1am_game *game;
  qa_q1am_nav_host host;
  nav_path *paths;
  size_t count, cap;
  struct navigation *next;
} navigation;
static navigation *registrations;

static void route_clear(qa_q1am_nav_route *route) {
  if (route->destroy) route->destroy(route->raw);
  free(route->nodes);
  free(route->edges);
  free(route->points);
  memset(route, 0, sizeof *route);
}
static nav_path *path_find(navigation *nav, qa_owned_actor actor) {
  for (size_t i = 0; i < nav->count; i++)
    if (qa_q1am_same_owned(nav->paths[i].actor, actor)) return &nav->paths[i];
  return NULL;
}
static void path_delete(navigation *nav, qa_owned_actor actor) {
  for (size_t i = 0; i < nav->count; i++)
    if (qa_q1am_same_owned(nav->paths[i].actor, actor)) {
      route_clear(&nav->paths[i].route);
      memmove(&nav->paths[i], &nav->paths[i + 1], (nav->count - i - 1) * sizeof *nav->paths);
      nav->count--;
      return;
    }
}
static nav_path *path_add(navigation *nav, qa_owned_actor actor) {
  nav_path *existing = path_find(nav, actor);
  if (existing) {
    route_clear(&existing->route);
    return existing;
  }
  if (nav->count == nav->cap) {
    size_t cap = nav->cap ? nav->cap * 2 : 8;
    nav_path *next = realloc(nav->paths, cap * sizeof *next);
    if (!next) qa_range_error("MG3 navigation allocation failed");
    nav->paths = next;
    nav->cap = cap;
  }
  nav_path *path = &nav->paths[nav->count++];
  memset(path, 0, sizeof *path);
  path->actor = actor;
  return path;
}
static void paths_clear(navigation *nav) {
  for (size_t i = 0; i < nav->count; i++) route_clear(&nav->paths[i].route);
  nav->count = 0;
}
static void on_release(void *ctx, qa_owned_actor actor) { path_delete(ctx, actor); }

static qa_q1am_nav_runtime *nav_runtime(navigation *nav, qa_owned_actor actor) {
  qa_q1am_nav_runtime *runtime = nav->host.for_actor(nav->host.ctx, actor);
  if (runtime == NULL) return NULL;
  if (!runtime->profile_monster || !runtime->has_pass_actor || !qa_same_actor(runtime->pass_actor, actor.id))
    qa_range_error("MG3 pathfinding requires the canonical monster profile and self collision exclusion");
  return runtime;
}
static qa_vec3 reader_point(const qa_save_reader *reader) {
  qa_save_reader x, y, z;
  qa_save_reader_field(reader, "x", &x);
  qa_save_reader_field(reader, "y", &y);
  qa_save_reader_field(reader, "z", &z);
  qa_vec3 point = {qa_save_reader_number(&x), qa_save_reader_number(&y), qa_save_reader_number(&z)};
  qa_save_reader_destroy(&x);
  qa_save_reader_destroy(&y);
  qa_save_reader_destroy(&z);
  return point;
}
static double reader_int_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  qa_save_reader_field(reader, name, &field);
  double value = qa_save_reader_integer_min(&field, 0);
  qa_save_reader_destroy(&field);
  return value;
}
static double reader_num_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  qa_save_reader_field(reader, name, &field);
  double value = qa_save_reader_number(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static qa_save_value *point_value(qa_vec3 point) {
  qa_save_value *object = qa_save_value_object();
  qa_save_value_set(object, "x", qa_save_value_number(point.x));
  qa_save_value_set(object, "y", qa_save_value_number(point.y));
  qa_save_value_set(object, "z", qa_save_value_number(point.z));
  return object;
}
static uint8_t *nav_capture(void *ctx, size_t *out_length) {
  navigation *nav = ctx;
  qa_save_value *list = qa_save_value_array();
  for (size_t i = 0; i < nav->count; i++) {
    nav_path *path = &nav->paths[i];
    qa_save_value *object = qa_save_value_object();
    qa_save_value *actor = qa_save_value_object();
    qa_save_value_set(actor, "slot", qa_save_value_number((double)path->actor.id.slot));
    qa_save_value_set(actor, "generation", qa_save_value_number((double)path->actor.id.generation));
    qa_save_value_set(object, "actor", actor);
    qa_save_value_set(object, "goal", point_value(path->goal));
    qa_save_value_set(object, "cursor", qa_save_value_number(path->cursor));
    qa_save_value_set(object, "map", qa_save_value_string(path->route.map_digest));
    qa_save_value *nodes = qa_save_value_array();
    for (size_t n = 0; n < path->route.node_count; n++)
      qa_save_value_push(nodes, qa_save_value_number(path->route.nodes[n]));
    qa_save_value_set(object, "nodes", nodes);
    qa_save_value *edges = qa_save_value_array();
    for (size_t e = 0; e < path->route.edge_count; e++)
      qa_save_value_push(edges, qa_save_value_number(path->route.edges[e]->id));
    qa_save_value_set(object, "edges", edges);
    qa_save_value *points = qa_save_value_array();
    for (size_t p = 0; p < path->route.point_count; p++)
      qa_save_value_push(points, point_value(path->route.points[p]));
    qa_save_value_set(object, "points", points);
    qa_save_value_set(object, "seconds", qa_save_value_number(path->route.travel_seconds));
    qa_save_value_set(object, "generation", qa_save_value_number(path->route.generation));
    qa_save_value_push(list, object);
  }
  uint8_t *bytes = qa_encode_checkpoint_value(list, out_length);
  qa_save_value_free(list);
  return bytes;
}
static void nav_restore(void *ctx, const uint8_t *bytes, size_t length) {
  navigation *nav = ctx;
  qa_q1am_game *game = nav->game;
  paths_clear(nav);
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_save_reader root;
  qa_save_reader_init(&root, decoded, "mg3:monster-navigation");
  size_t count = qa_save_reader_list_count(&root);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader reader;
    qa_save_reader_item(&root, i, &reader);
    qa_save_reader saved;
    qa_save_reader_field(&reader, "actor", &saved);
    int64_t slot = (int64_t)reader_int_field(&saved, "slot");
    int64_t generation = (int64_t)reader_int_field(&saved, "generation");
    qa_save_reader_destroy(&saved);
    qa_owned_actor actor;
    if (!game->actors_resolve_saved(game->ctx, slot, generation, &actor))
      qa_save_reader_fail(&reader, "missing navigation actor");
    qa_q1am_nav_runtime *runtime = nav_runtime(nav, actor);
    qa_save_reader map;
    qa_save_reader_field(&reader, "map", &map);
    const char *digest = qa_save_reader_string(&map);
    if (runtime == NULL || strcmp(runtime->map_digest, digest) != 0) {
      qa_save_reader_destroy(&map);
      qa_save_reader_fail(&reader, "saved navigation map is unavailable");
    }
    qa_save_reader_destroy(&map);
    qa_q1am_nav_route route = {0};
    route.map_digest = runtime->map_digest;
    qa_save_reader list;
    qa_save_reader_field(&reader, "nodes", &list);
    route.node_count = qa_save_reader_list_count(&list);
    route.nodes = calloc(route.node_count ? route.node_count : 1, sizeof *route.nodes);
    for (size_t n = 0; n < route.node_count; n++) {
      qa_save_reader item;
      qa_save_reader_item(&list, n, &item);
      double id = qa_save_reader_integer_min(&item, 0);
      if (!runtime->node_exists(runtime->ctx, id)) qa_save_reader_fail(&item, "missing navigation node");
      route.nodes[n] = id;
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&list);
    qa_save_reader_field(&reader, "edges", &list);
    route.edge_count = qa_save_reader_list_count(&list);
    route.edges = calloc(route.edge_count ? route.edge_count : 1, sizeof *route.edges);
    for (size_t e = 0; e < route.edge_count; e++) {
      qa_save_reader item;
      qa_save_reader_item(&list, e, &item);
      double id = qa_save_reader_integer_min(&item, 0);
      const qa_q1am_nav_edge *edge = NULL;
      for (size_t g = 0; g < runtime->graph_edge_count; g++)
        if (runtime->graph_edges[g].id == id) edge = &runtime->graph_edges[g];
      if (edge == NULL) qa_save_reader_fail(&item, "missing navigation edge");
      route.edges[e] = edge;
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&list);
    qa_save_reader_field(&reader, "points", &list);
    route.point_count = qa_save_reader_list_count(&list);
    route.points = calloc(route.point_count ? route.point_count : 1, sizeof *route.points);
    for (size_t p = 0; p < route.point_count; p++) {
      qa_save_reader item;
      qa_save_reader_item(&list, p, &item);
      route.points[p] = reader_point(&item);
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&list);
    route.travel_seconds = reader_num_field(&reader, "seconds");
    route.generation = reader_int_field(&reader, "generation");
    double cursor = reader_int_field(&reader, "cursor");
    if (cursor > (double)route.point_count) qa_save_reader_fail(&reader, "navigation cursor exceeds route");
    qa_save_reader goal;
    qa_save_reader_field(&reader, "goal", &goal);
    qa_vec3 goal_point = reader_point(&goal);
    qa_save_reader_destroy(&goal);
    nav_path *path = path_add(nav, actor);
    path->goal = goal_point;
    path->route = route;
    path->cursor = cursor;
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&root);
  qa_save_value_free(decoded);
}
static void nav_clone(void *ctx, qa_q1am_actor *source, qa_q1am_actor *target) {
  navigation *nav = ctx;
  nav_path *path = path_find(nav, source->actor);
  if (path == NULL) return;
  qa_q1am_nav_route route = path->route;
  qa_vec3 goal = path->goal;
  double cursor = path->cursor;
  route.nodes = calloc(route.node_count ? route.node_count : 1, sizeof *route.nodes);
  memcpy(route.nodes, path->route.nodes, route.node_count * sizeof *route.nodes);
  route.edges = calloc(route.edge_count ? route.edge_count : 1, sizeof *route.edges);
  memcpy(route.edges, path->route.edges, route.edge_count * sizeof *route.edges);
  route.points = calloc(route.point_count ? route.point_count : 1, sizeof *route.points);
  memcpy(route.points, path->route.points, route.point_count * sizeof *route.points);
  route.raw = NULL;
  route.destroy = NULL;
  nav_path *copy = path_add(nav, target->actor);
  copy->goal = goal;
  copy->route = route;
  copy->cursor = cursor;
}
static bool walk_edge_filter(void *ctx, const qa_q1am_nav_edge *edge) {
  (void)ctx;
  return strcmp(edge->mode, "walk") == 0 || strcmp(edge->mode, "drop") == 0 || strcmp(edge->mode, "swim") == 0;
}
static double nav_walk(navigation *nav, qa_q1am_monster *monster, double distance, qa_vec3 goal) {
  qa_owned_actor actor = monster->entity->actor;
  qa_q1am_nav_runtime *runtime = nav_runtime(nav, actor);
  if (runtime == NULL) {
    path_delete(nav, actor);
    return QA_Q1AM_PATH_ERROR;
  }
  qa_vec3 origin = qa_q1am_monster_origin(monster);
  if (qa_length3(qa_sub3(goal, origin)) <= 1) {
    path_delete(nav, actor);
    return QA_Q1AM_PATH_REACHED_GOAL;
  }
  nav_path *path = path_find(nav, actor);
  if (path == NULL || qa_length3(qa_sub3(goal, path->goal)) > 1 ||
      !runtime->route_still_valid(runtime->ctx, &path->route)) {
    qa_q1am_nav_query query = {origin, goal, NULL, walk_edge_filter};
    qa_q1am_nav_route route = {0};
    if (!runtime->route(runtime->ctx, &query, &route)) {
      path_delete(nav, actor);
      return QA_Q1AM_PATH_ERROR;
    }
    path = path_add(nav, actor);
    path->goal = goal;
    path->route = route;
    path->cursor = 0;
  }
  bool has_next = path->cursor < (double)path->route.point_count;
  qa_vec3 next = has_next ? path->route.points[(size_t)path->cursor] : QA_Q1AM_ZERO;
  while (has_next && qa_length3(qa_sub3(next, origin)) <= 1) {
    path->cursor++;
    has_next = path->cursor < (double)path->route.point_count;
    if (has_next) next = path->route.points[(size_t)path->cursor];
  }
  if (!has_next) {
    path_delete(nav, actor);
    return QA_Q1AM_PATH_REACHED_PATH_END;
  }
  qa_vec3 offset = qa_sub3(next, origin);
  double horizontal = hypot(offset.x, offset.y);
  double step = fmin(fabs(distance), horizontal);
  double signed_step = (distance > 0 ? 1 : distance < 0 ? -1 : 0) * step;
  if (step == 0 || !monster->game->walk_move(monster->game->ctx, actor, qa_q1am_yaw_for(offset), signed_step)) {
    path_delete(nav, actor);
    return QA_Q1AM_PATH_MOVE_BLOCKED;
  }
  return QA_Q1AM_PATH_IN_PROGRESS;
}
void qa_q1am_register_mg3_monster_navigation(qa_q1am_game *game, qa_q1am_nav_host host) {
  for (navigation *entry = registrations; entry; entry = entry->next)
    if (entry->game == game) qa_range_error("MG3 monster navigation is already registered");
  navigation *nav = calloc(1, sizeof *nav);
  if (!nav) qa_range_error("MG3 navigation allocation failed");
  nav->game = game;
  nav->host = host;
  nav->next = registrations;
  registrations = nav;
  game->actors_on_release(game->ctx, on_release, nav);
  qa_q1am_state_extension extension = {"mg3:monster-navigation", nav, nav_capture, nav_restore, nav_clone};
  game->register_state_extension(game->ctx, extension);
}
double qa_q1am_walk_mg3_path_to_goal(qa_q1am_monster *monster, double distance, qa_vec3 goal) {
  navigation *nav = NULL;
  for (navigation *entry = registrations; entry; entry = entry->next)
    if (entry->game == monster->game) nav = entry;
  if (nav == NULL) qa_range_error("MG3 walkpathtogoal requires the selected shared navigation host");
  return nav_walk(nav, monster, FR(distance), goal);
}
