/* Port of src/content/q1/addons/monsters/ai/path.ts.
 * quakec_mg3/defs.qc return codes and ai.qc call contract. GPL-2.0-or-later.
 * The closed rerelease engine's path algorithm is unavailable. This implementation
 * follows shared, movement-admitted navigation, then commits only source walkMove. */
#include "qa/content/q1/addons/monsters/ai/path.h"
#include "qa/foundation/support.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../support.h"

/* TS interface Path. */
typedef struct nav_path {
  qa_owned_actor actor;
  qa_vec3 goal;
  qa_navigation_route route;
  double cursor;
} nav_path;
/* TS class Mg3MonsterNavigation. */
typedef struct navigation {
  qa_q1_services *game;
  qa_q1am_nav_host host;
  nav_path *paths; size_t count, cap; /* TS Map<OwnedActor, Path>, insertion order */
  int64_t release_subscription;
  qa_q1_state_extension extension;
  struct navigation *next;
} navigation;
/* TS WeakMap<Q1EntityServices, Mg3MonsterNavigation>. */
static navigation *registrations;

static navigation *registration_of(qa_q1_services *game) {
  for (navigation *entry = registrations; entry != NULL; entry = entry->next)
    if (entry->game == game) return entry;
  return NULL;
}
static nav_path *path_find(navigation *nav, qa_owned_actor actor) {
  for (size_t i = 0; i < nav->count; i++)
    if (qa_same_owned_actor(nav->paths[i].actor, actor)) return &nav->paths[i];
  return NULL;
}
static void path_delete(navigation *nav, qa_owned_actor actor) {
  for (size_t i = 0; i < nav->count; i++)
    if (qa_same_owned_actor(nav->paths[i].actor, actor)) {
      qa_navigation_route_destroy(&nav->paths[i].route);
      memmove(&nav->paths[i], &nav->paths[i + 1], (nav->count - i - 1) * sizeof *nav->paths);
      nav->count--;
      return;
    }
}
/* TS paths.set(actor, path): replaces in place or appends. Takes the route. */
static nav_path *path_set(navigation *nav, qa_owned_actor actor, qa_vec3 goal, qa_navigation_route route, double cursor) {
  nav_path *path = path_find(nav, actor);
  if (path != NULL) {
    qa_navigation_route_destroy(&path->route);
  } else {
    if (nav->count == nav->cap) {
      const size_t cap = nav->cap != 0 ? nav->cap * 2 : 8;
      nav_path *grown = realloc(nav->paths, cap * sizeof *grown);
      if (grown == NULL) qa_range_error("out of memory");
      nav->paths = grown;
      nav->cap = cap;
    }
    path = &nav->paths[nav->count++];
    path->actor = actor;
  }
  path->goal = goal;
  path->route = route;
  path->cursor = cursor;
  return path;
}
static void paths_clear(navigation *nav) {
  for (size_t i = 0; i < nav->count; i++) qa_navigation_route_destroy(&nav->paths[i].route);
  nav->count = 0;
}
static void *copy_array(const void *source, size_t count, size_t size) {
  if (count == 0) return NULL;
  void *copy = malloc(count * size);
  if (copy == NULL) qa_range_error("out of memory");
  memcpy(copy, source, count * size);
  return copy;
}
static qa_navigation_route route_copy(const qa_navigation_route *route) {
  qa_navigation_route copy = *route;
  copy.nodes = copy_array(route->nodes, route->node_count, sizeof *route->nodes);
  copy.edges = copy_array(route->edges, route->edge_count, sizeof *route->edges);
  copy.points = copy_array(route->points, route->point_count, sizeof *route->points);
  return copy;
}
static void on_release(void *ctx, qa_owned_actor actor) { path_delete(ctx, actor); }

static qa_navigation_runtime *nav_runtime(navigation *nav, qa_owned_actor actor) {
  qa_navigation_runtime *runtime = nav->host.for_actor(nav->host.ctx, actor);
  if (runtime == NULL) return NULL;
  if (!runtime->graph->profile.monster || runtime->world->pass_actor == NULL ||
      !qa_same_actor(*runtime->world->pass_actor, actor.id))
    qa_range_error("MG3 pathfinding requires the canonical monster profile and self collision exclusion");
  return runtime;
}

static void must(bool ok) { if (!ok) qa_range_error("out of memory"); }
static void set_child(qa_save_value *object, const char *key, qa_save_value *child) {
  must(child != NULL && qa_save_value_set(object, key, child));
}
static void push_child(qa_save_value *array, qa_save_value *child) {
  must(child != NULL && qa_save_value_push(array, child));
}
static qa_save_value *point_value(qa_vec3 point) {
  qa_save_value *object = qa_save_value_object();
  must(object != NULL);
  set_child(object, "x", qa_save_value_number(point.x));
  set_child(object, "y", qa_save_value_number(point.y));
  set_child(object, "z", qa_save_value_number(point.z));
  return object;
}
static bool nav_capture(void *ctx, uint8_t **out, size_t *out_length) {
  navigation *nav = ctx;
  qa_save_value *list = qa_save_value_array();
  if (list == NULL) return false;
  for (size_t i = 0; i < nav->count; i++) {
    const nav_path *path = &nav->paths[i];
    qa_save_value *object = qa_save_value_object(), *actor = qa_save_value_object();
    must(object != NULL && actor != NULL);
    set_child(actor, "slot", qa_save_value_number((double)path->actor.id.slot));
    set_child(actor, "generation", qa_save_value_number((double)path->actor.id.generation));
    set_child(object, "actor", actor);
    set_child(object, "goal", point_value(path->goal));
    set_child(object, "cursor", qa_save_value_number(path->cursor));
    set_child(object, "map", qa_save_value_string(path->route.map.digest));
    qa_save_value *nodes = qa_save_value_array(), *edges = qa_save_value_array(), *points = qa_save_value_array();
    must(nodes != NULL && edges != NULL && points != NULL);
    for (size_t n = 0; n < path->route.node_count; n++) push_child(nodes, qa_save_value_number((double)path->route.nodes[n]));
    for (size_t e = 0; e < path->route.edge_count; e++) push_child(edges, qa_save_value_number((double)path->route.edges[e].id));
    for (size_t p = 0; p < path->route.point_count; p++) push_child(points, point_value(path->route.points[p]));
    set_child(object, "nodes", nodes);
    set_child(object, "edges", edges);
    set_child(object, "points", points);
    set_child(object, "seconds", qa_save_value_number(path->route.travel_seconds));
    set_child(object, "generation", qa_save_value_number((double)path->route.generation));
    push_child(list, object);
  }
  *out = qa_encode_checkpoint_value(list, out_length);
  qa_save_value_free(list);
  return *out != NULL;
}
static void field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  must(qa_save_reader_field(reader, name, out));
}
static void item(const qa_save_reader *reader, size_t index, qa_save_reader *out) {
  must(qa_save_reader_item(reader, index, out));
}
static double field_integer(const qa_save_reader *reader, const char *name) {
  qa_save_reader value;
  field(reader, name, &value);
  const double result = qa_save_reader_integer_min(&value, 0);
  qa_save_reader_destroy(&value);
  return result;
}
static double field_number(const qa_save_reader *reader, const char *name) {
  qa_save_reader value;
  field(reader, name, &value);
  const double result = qa_save_reader_number(&value);
  qa_save_reader_destroy(&value);
  return result;
}
static qa_vec3 reader_point(const qa_save_reader *reader) {
  qa_vec3 point;
  point.x = field_number(reader, "x");
  point.y = field_number(reader, "y");
  point.z = field_number(reader, "z");
  return point;
}
static void q1_free_decoded_save(void *value) { qa_save_value_free(value); }
static void nav_restore(void *ctx, const uint8_t *bytes, size_t length) {
  navigation *nav = ctx;
  qa_q1_services *game = nav->game;
  paths_clear(nav);
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_cleanup decoded_cleanup; /* a malformed save raises from the readers */
  qa_cleanup_push(&decoded_cleanup, q1_free_decoded_save, decoded);
  qa_save_reader root;
  must(qa_save_reader_init(&root, decoded, "mg3:monster-navigation"));
  const size_t count = qa_save_reader_list_count(&root);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader reader, saved;
    item(&root, i, &reader);
    field(&reader, "actor", &saved);
    qa_saved_actor_id id;
    id.slot = field_integer(&saved, "slot");
    id.generation = field_integer(&saved, "generation");
    qa_save_reader_destroy(&saved);
    qa_owned_actor actor;
    if (!qa_session_actor_registry_resolve_saved(game->host.actors, id, &actor))
      qa_save_reader_fail(&reader, "missing navigation actor");
    qa_navigation_runtime *runtime = nav_runtime(nav, actor);
    qa_save_reader map;
    field(&reader, "map", &map);
    const char *digest = qa_save_reader_string(&map);
    const bool same_map = runtime != NULL && runtime->graph->map.digest != NULL &&
                          strcmp(runtime->graph->map.digest, digest) == 0;
    qa_save_reader_destroy(&map);
    if (!same_map) qa_save_reader_fail(&reader, "saved navigation map is unavailable");
    qa_navigation_route route;
    memset(&route, 0, sizeof route);
    route.map = runtime->graph->map;
    qa_save_reader list;
    field(&reader, "nodes", &list);
    route.node_count = qa_save_reader_list_count(&list);
    route.nodes = route.node_count == 0 ? NULL : calloc(route.node_count, sizeof *route.nodes);
    must(route.node_count == 0 || route.nodes != NULL);
    for (size_t n = 0; n < route.node_count; n++) {
      qa_save_reader value;
      item(&list, n, &value);
      const int64_t node = (int64_t)qa_save_reader_integer_min(&value, 0);
      if (qa_navigation_runtime_node(runtime, node) == NULL) qa_save_reader_fail(&value, "missing navigation node");
      route.nodes[n] = node;
      qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&list);
    field(&reader, "edges", &list);
    route.edge_count = qa_save_reader_list_count(&list);
    route.edges = route.edge_count == 0 ? NULL : calloc(route.edge_count, sizeof *route.edges);
    must(route.edge_count == 0 || route.edges != NULL);
    for (size_t e = 0; e < route.edge_count; e++) {
      qa_save_reader value;
      item(&list, e, &value);
      const int64_t edge_id = (int64_t)qa_save_reader_integer_min(&value, 0);
      const qa_navigation_edge *edge = NULL;
      for (size_t g = 0; g < runtime->graph->edge_count && edge == NULL; g++)
        if (runtime->graph->edges[g].id == edge_id) edge = &runtime->graph->edges[g];
      if (edge == NULL) qa_save_reader_fail(&value, "missing navigation edge");
      route.edges[e] = *edge;
      qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&list);
    field(&reader, "points", &list);
    route.point_count = qa_save_reader_list_count(&list);
    route.points = route.point_count == 0 ? NULL : calloc(route.point_count, sizeof *route.points);
    must(route.point_count == 0 || route.points != NULL);
    for (size_t p = 0; p < route.point_count; p++) {
      qa_save_reader value;
      item(&list, p, &value);
      route.points[p] = reader_point(&value);
      qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&list);
    route.travel_seconds = field_number(&reader, "seconds");
    route.generation = (int64_t)field_integer(&reader, "generation");
    const double cursor = field_integer(&reader, "cursor");
    if (cursor > (double)route.point_count) qa_save_reader_fail(&reader, "navigation cursor exceeds route");
    qa_save_reader goal;
    field(&reader, "goal", &goal);
    const qa_vec3 goal_point = reader_point(&goal);
    qa_save_reader_destroy(&goal);
    (void)path_set(nav, actor, goal_point, route, cursor);
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&root);
  qa_cleanup_pop(&decoded_cleanup);
  qa_save_value_free(decoded);
}
static void nav_clone(void *ctx, qa_q1_actor *source, qa_q1_actor *target) {
  navigation *nav = ctx;
  const nav_path *path = path_find(nav, source->actor);
  if (path == NULL) return;
  const qa_vec3 goal = path->goal;
  const double cursor = path->cursor;
  (void)path_set(nav, target->actor, goal, route_copy(&path->route), cursor);
}

/* walkpathtogoal commits walking only; jumps and source triggers require their own actions. */
static bool walk_edge_filter(void *ctx, const qa_navigation_edge *edge) {
  (void)ctx; /* TS closure captures nothing */
  return edge->mode == QA_TM_WALK || edge->mode == QA_TM_DROP || edge->mode == QA_TM_SWIM;
}
static bool close_to(qa_q1_base_monster *monster, qa_vec3 point) {
  return qa_length3(qa_sub3(point, qa_q1_base_monster_origin(monster))) <= 1;
}
static double nav_walk(navigation *nav, qa_q1_base_monster *monster, double distance, qa_vec3 goal) {
  const qa_owned_actor actor = monster->entity->actor;
  qa_navigation_runtime *runtime = nav_runtime(nav, actor);
  if (runtime == NULL) { path_delete(nav, actor); return QA_Q1AM_PATH_ERROR; }
  if (close_to(monster, goal)) { path_delete(nav, actor); return QA_Q1AM_PATH_REACHED_GOAL; }
  nav_path *path = path_find(nav, actor);
  if (path == NULL || qa_length3(qa_sub3(goal, path->goal)) > 1 ||
      !qa_navigation_runtime_route_still_valid(runtime, &path->route)) {
    qa_navigation_route_query query;
    memset(&query, 0, sizeof query);
    query.start = qa_q1_base_monster_origin(monster);
    query.goal = goal;
    query.edge_filter = walk_edge_filter;
    qa_navigation_route_result result = qa_navigation_runtime_route(runtime, query);
    if (!result.is_route) { path_delete(nav, actor); return QA_Q1AM_PATH_ERROR; }
    path = path_set(nav, actor, goal, result.route, 0);
  }
  while (path->cursor < (double)path->route.point_count && close_to(monster, path->route.points[(size_t)path->cursor]))
    path->cursor++;
  if (!(path->cursor < (double)path->route.point_count)) {
    path_delete(nav, actor);
    return QA_Q1AM_PATH_REACHED_PATH_END;
  }
  const qa_vec3 next = path->route.points[(size_t)path->cursor];
  const qa_vec3 offset = qa_sub3(next, qa_q1_base_monster_origin(monster));
  const double horizontal = qa_js_hypot2(offset.x, offset.y);
  const double step = qa_js_min(fabs(distance), horizontal);
  if (step == 0 || !qa_q1am_walk_move(nav->game, actor, qa_q1_yaw_for(offset), qa_js_sign(distance) * step)) {
    path_delete(nav, actor);
    return QA_Q1AM_PATH_MOVE_BLOCKED;
  }
  return QA_Q1AM_PATH_IN_PROGRESS;
}

void qa_q1am_register_mg3_monster_navigation(qa_q1_services *game, qa_q1am_nav_host host) {
  if (registration_of(game) != NULL) qa_range_error("MG3 monster navigation is already registered");
  navigation *nav = calloc(1, sizeof *nav);
  if (nav == NULL) qa_range_error("out of memory");
  nav->game = game;
  nav->host = host;
  nav->release_subscription = qa_session_actor_registry_on_release(game->host.actors, on_release, nav);
  nav->extension.ctx = nav;
  nav->extension.id = "mg3:monster-navigation";
  nav->extension.capture = nav_capture;
  nav->extension.restore = nav_restore;
  nav->extension.clone = nav_clone;
  qa_q1_register_state_extension(game, &nav->extension);
  nav->next = registrations;
  registrations = nav;
}
double qa_q1am_walk_mg3_path_to_goal(qa_q1_base_monster *monster, double distance, qa_vec3 goal) {
  navigation *nav = registration_of(monster->game);
  if (nav == NULL) qa_range_error("MG3 walkpathtogoal requires the selected shared navigation host");
  return nav_walk(nav, monster, QA_Q1AM_FR(distance), goal);
}
void qa_q1am_release_mg3_monster_navigation(qa_q1_services *game) {
  for (navigation **link = &registrations; *link != NULL; link = &(*link)->next) {
    navigation *nav = *link;
    if (nav->game != game) continue;
    *link = nav->next;
    qa_session_actor_registry_off_release(game->host.actors, nav->release_subscription);
    paths_clear(nav);
    free(nav->paths);
    free(nav);
    return;
  }
}
