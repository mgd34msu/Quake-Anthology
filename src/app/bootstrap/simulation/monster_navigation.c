/* Port of src/app/bootstrap/simulation/monster-navigation.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/monster_navigation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/players.h"
#include "qa/content/q1/addons/monsters/ai/path.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static double distance(qa_vec3 a, qa_vec3 b) { return qa_js_hypot3(a.x - b.x, a.y - b.y, a.z - b.z); }
static void *grow(void *array, size_t *capacity, size_t count, size_t element) {
  if (count < *capacity) return array;
  size_t next = *capacity ? *capacity * 2 : 8;
  void *grown = realloc(array, next * element);
  if (grown == NULL) qa_range_error("Out of memory");
  *capacity = next; return grown;
}

typedef struct mnav_install mnav_install;
typedef struct mnav_actor mnav_actor;
/* TS JSON.stringify([state.bounds, state.flags & (1 | 2)]) key. */
typedef struct mnav_key { qa_bounds bounds; int32_t flags; } mnav_key;
static bool same_key(mnav_key a, mnav_key b) {
  return a.flags == b.flags && a.bounds.min.x == b.bounds.min.x && a.bounds.min.y == b.bounds.min.y && a.bounds.min.z == b.bounds.min.z
      && a.bounds.max.x == b.bounds.max.x && a.bounds.max.y == b.bounds.max.y && a.bounds.max.z == b.bounds.max.z;
}
/* One begin(): a predictor whose actor reads/writes a detached state. */
typedef struct mnav_route {
  mnav_actor *owner;
  bool has_detached; qa_q1_monster_move_state detached;
  qa_q1_monster_movement predictor;
} mnav_route;
/* The topology scene: the same scene traced against model zero. */
typedef struct mnav_topology {
  qa_scene_queries base;
  qa_navigation_world world;
} mnav_topology;
typedef struct mnav_graph {
  mnav_key key;
  mnav_topology *topology;
  qa_loaded_navigation loaded;
} mnav_graph;
struct mnav_actor {
  mnav_install *install;
  qa_owned_actor actor;
  mnav_key key;
  qa_actor_id pass_actor;
  qa_navigation_world world;
  qa_navigation_runtime runtime;
  mnav_route *route; /* latest beginRoute */
};
struct mnav_install {
  qa_app_sim_application_monster_navigation *navigation;
  qa_app_sim_monster_navigation_simulation simulation;
  qa_q1_monster_movement *movement;
  qa_movement_profile selected;
  mnav_graph **graphs; size_t graph_count, graph_cap;
  mnav_actor **actors; size_t actor_count, actor_cap;
};
struct qa_app_sim_application_monster_navigation {
  qa_app_sim_monster_navigation_content content;
  uint8_t *map_bytes;
  qa_prepared_navigation prepared;
  mnav_install **installs; size_t install_count, install_cap;
};

/* ---- detached predictor services ---- */
static bool route_read(void *ctx, qa_actor_id actor, qa_q1_monster_move_state *out) {
  mnav_route *route = ctx;
  if (qa_same_actor(actor, route->owner->actor.id)) {
    if (!route->has_detached) return false;
    *out = route->detached; return true;
  }
  const qa_q1_monster_move_services *services = &route->owner->install->movement->services;
  return services->read(services->ctx, actor, out);
}
static bool route_read_target(void *ctx, qa_actor_id actor, qa_q1_monster_move_target *out) {
  mnav_route *route = ctx;
  const qa_q1_monster_move_services *services = &route->owner->install->movement->services;
  return services->read_target(services->ctx, actor, out);
}
static void route_write(void *ctx, qa_owned_actor actor, qa_q1_monster_move_state next) {
  (void)actor; /* TS write: (_actor, next) => detached = next */
  mnav_route *route = ctx;
  route->detached = next; route->has_detached = true;
}
static void route_link(void *ctx, qa_owned_actor actor, bool touch_triggers) { (void)ctx; (void)actor; (void)touch_triggers; }
static void route_init(mnav_route *route, mnav_actor *owner) {
  memset(route, 0, sizeof *route);
  route->owner = owner;
  qa_q1_monster_move_services services = owner->install->movement->services;
  services.ctx = route; services.read = route_read; services.read_target = route_read_target;
  services.write = route_write; services.link = route_link;
  qa_q1_monster_movement_init(&route->predictor, services);
}
static qa_traversal_admission rejected(const char *reason) {
  qa_traversal_admission out; memset(&out, 0, sizeof out);
  out.admitted = false; out.reason = reason; return out;
}
static qa_traversal_admission route_admit(void *ctx, qa_traversal_request request) {
  mnav_route *route = ctx;
  mnav_actor *owner = route->owner;
  if (request.mode != QA_TM_WALK && request.mode != QA_TM_DROP && request.mode != QA_TM_SWIM)
    return rejected("MG3 path walking cannot execute this traversal");
  const qa_q1_monster_move_services *services = &owner->install->movement->services;
  qa_q1_monster_move_state actual;
  if (!services->read(services->ctx, owner->actor.id, &actual)) return rejected("Monster was removed");
  if (!route->has_detached) { route->detached = actual; route->detached.origin = request.from; route->has_detached = true; }
  if (distance(route->detached.origin, request.from) > 2) return rejected("Monster route segments are disconnected");
  size_t capacity = 0, count = 0;
  qa_vec3 *trajectory = NULL;
  trajectory = grow(trajectory, &capacity, count, sizeof *trajectory);
  trajectory[count++] = route->detached.origin;
  double steps = ceil(distance(request.from, request.to) / 8) + 2;
  for (double index = 0; index < steps; index++) {
    double x = request.to.x - route->detached.origin.x, y = request.to.y - route->detached.origin.y, length = qa_js_hypot2(x, y);
    if (length <= 1) break;
    if (!qa_q1_monster_walk_move(&route->predictor, owner->actor, atan2(y, x) * 180 / 3.141592653589793, qa_js_min(8, length))) {
      free(trajectory); return rejected("Source monster walkMove blocked");
    }
    trajectory = grow(trajectory, &capacity, count, sizeof *trajectory);
    trajectory[count++] = route->detached.origin;
  }
  if (distance(route->detached.origin, request.to) <= 2) {
    qa_traversal_admission out; memset(&out, 0, sizeof out);
    out.admitted = true; out.seconds = distance(request.from, request.to) / 100;
    out.trajectory = trajectory; out.trajectory_count = count;
    return out;
  }
  free(trajectory);
  return rejected("Source monster walkMove did not reach the endpoint");
}

/* ---- per-actor world ---- */
static qa_traversal_admission world_admit(void *ctx, qa_traversal_request request, qa_navigation_profile profile) {
  (void)profile; /* TS admit: request => begin().admit(request) */
  mnav_route route; route_init(&route, ctx);
  return route_admit(&route, request);
}
static qa_navigation_route_prediction world_begin_route(void *ctx, qa_navigation_profile profile) {
  (void)profile; /* TS beginRoute: begin (takes no profile) */
  mnav_actor *owner = ctx;
  mnav_route *route = malloc(sizeof *route);
  if (route == NULL) qa_range_error("Out of memory");
  route_init(route, owner);
  free(owner->route); owner->route = route;
  return (qa_navigation_route_prediction){route, route_admit};
}
static bool world_entity(void *ctx, qa_navigation_entity_binding binding, qa_navigation_entity_state *out) {
  (void)ctx; (void)binding; (void)out; /* TS entity: () => null */
  return false;
}
static bool world_hazard(void *ctx, qa_bounds bounds) { (void)ctx; (void)bounds; return false; } /* TS hazard: () => false */
static double world_revision(void *ctx) {
  mnav_actor *owner = ctx;
  return owner->install->simulation.time_seconds(owner->install->simulation.ctx) * 1000;
}

/* ---- topology scene (model zero of the same scene) ---- */
static qa_query_target model_zero(void) {
  qa_query_target target; memset(&target, 0, sizeof target);
  target.is_model = true; target.model = 0;
  return target;
}
static qa_trace_result topology_trace(void *ctx, qa_trace_query query) {
  mnav_topology *t = ctx; query.target = model_zero();
  return t->base.trace(t->base.ctx, query);
}
static qa_point_contents_result topology_contents(void *ctx, qa_point_contents_query query) {
  mnav_topology *t = ctx; query.target = model_zero();
  return t->base.point_contents(t->base.ctx, query);
}
static qa_leaf_query_result topology_leaves(void *ctx, qa_bounds bounds, double limit) {
  mnav_topology *t = ctx; return t->base.box_leaves(t->base.ctx, bounds, limit);
}
static bool topology_areas(void *ctx, double a, double b) { mnav_topology *t = ctx; return t->base.areas_connected(t->base.ctx, a, b); }
static bool topology_visible(void *ctx, double a, double b, const char *kind) {
  mnav_topology *t = ctx; return t->base.cluster_visible(t->base.ctx, a, b, kind);
}

/* TS forActor(actor). */
static qa_navigation_runtime *for_actor(void *ctx, qa_owned_actor actor) {
  mnav_install *install = ctx;
  qa_q1_monster_move_services *services = &install->movement->services;
  qa_q1_monster_move_state state;
  if (!services->read(services->ctx, actor.id, &state)) return NULL;
  mnav_key key = {state.bounds, qa_js_to_int32(state.flags) & (1 | 2)};
  for (size_t i = 0; i < install->actor_count; i++) {
    mnav_actor *cached = install->actors[i];
    if (qa_same_actor(cached->actor.id, actor.id) && is(cached->actor.owner, actor.owner))
      { if (same_key(cached->key, key)) return &cached->runtime; break; }
  }
  qa_navigation_profile profile; memset(&profile, 0, sizeof profile);
  profile.movement = install->selected;
  profile.shape.kind = "box"; profile.shape.bounds = state.bounds;
  profile.policy.kind = "q1"; profile.policy.move = "normal"; profile.policy.has_hull = false;
  profile.capabilities = 1u << QA_TM_WALK | 1u << QA_TM_DROP | 1u << QA_TM_SWIM;
  profile.maximum_step = 18; profile.minimum_floor_normal = 0.7; profile.maximum_drop = 18; profile.team = -1; profile.monster = true;
  mnav_actor *entry = calloc(1, sizeof *entry);
  if (entry == NULL) qa_range_error("Out of memory");
  entry->install = install; entry->actor = actor; entry->key = key; entry->pass_actor = actor.id;
  entry->world.scene = qa_shared_as_scene_queries(install->simulation.scene);
  entry->world.pass_actor = &entry->pass_actor;
  entry->world.ctx = entry;
  entry->world.admit = world_admit; entry->world.begin_route = world_begin_route;
  entry->world.get_entity = world_entity; entry->world.hazard = world_hazard; entry->world.revision_of = world_revision;
  mnav_graph *graph = NULL;
  for (size_t i = 0; i < install->graph_count; i++) if (same_key(install->graphs[i]->key, key)) graph = install->graphs[i];
  if (graph == NULL) {
    /* Stable topology uses model zero of the same scene; live movement checks actors and movers. */
    graph = calloc(1, sizeof *graph);
    mnav_topology *topology = calloc(1, sizeof *topology);
    if (graph == NULL || topology == NULL) qa_range_error("Out of memory");
    graph->key = key; graph->topology = topology;
    topology->base = entry->world.scene;
    topology->world = entry->world;
    topology->world.scene = (qa_scene_queries){topology, topology_trace, topology_contents, topology_leaves, topology_areas, topology_visible};
    qa_navigation_construction options; memset(&options, 0, sizeof options);
    qa_app_sim_application_monster_navigation *navigation = install->navigation;
    options.geometry = navigation->content.world; options.map = navigation->prepared.map;
    options.profile = profile; options.world = &topology->world;
    if (!qa_load_prepared_navigation(&graph->loaded, options, &navigation->prepared)) qa_range_error("Out of memory");
    install->graphs = grow(install->graphs, &install->graph_cap, install->graph_count, sizeof *install->graphs);
    install->graphs[install->graph_count++] = graph;
  }
  if (!qa_navigation_runtime_init(&entry->runtime, &graph->loaded.graph, &entry->world)) qa_range_error("Out of memory");
  /* actors.set(actor, ...): replaces the actor's previous entry. */
  bool replaced = false;
  for (size_t i = 0; i < install->actor_count && !replaced; i++) {
    mnav_actor *cached = install->actors[i];
    if (!qa_same_actor(cached->actor.id, actor.id) || !is(cached->actor.owner, actor.owner)) continue;
    /* The previous runtime may still be held by the caller for this
     * think; keep it alive with the install (TS GC keeps it while used). */
    install->actors = grow(install->actors, &install->actor_cap, install->actor_count, sizeof *install->actors);
    install->actors[install->actor_count++] = cached;
    install->actors[i] = entry; replaced = true;
  }
  if (!replaced) {
    install->actors = grow(install->actors, &install->actor_cap, install->actor_count, sizeof *install->actors);
    install->actors[install->actor_count++] = entry;
  }
  return &entry->runtime;
}

static qa_content_id provenance_content(const qa_resolved_resource_reference *reference) {
  return reference->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE
      ? reference->provenance.archive_mount->identity.content : reference->provenance.loose_mount->identity.content;
}
/* TS content.split(":")[2] === "mg3". */
static bool mg3_content(const char *content) {
  if (content == NULL) return false;
  const char *part = content;
  for (int index = 0; index < 2; index++) {
    part = strchr(part, ':');
    if (part == NULL) return false;
    part++;
  }
  size_t length = strcspn(part, ":");
  return length == 3 && strncmp(part, "mg3", 3) == 0;
}

qa_app_sim_application_monster_navigation *qa_app_sim_preload_application_monster_navigation(
    qa_app_sim_monster_navigation_content content) {
  if (!mg3_content(content.recipe->map.entities.content)) return NULL;
  qa_app_sim_application_monster_navigation *navigation = calloc(1, sizeof *navigation);
  if (navigation == NULL) qa_range_error("Out of memory");
  navigation->content = content;
  const qa_resolved_resource_reference *geometry = &content.recipe->map.geometry;
  qa_content_mounted *resources = content.for_content(content.ctx, provenance_content(geometry));
  size_t length = 0;
  navigation->map_bytes = qa_content_mounted_read_reference(content.mounts, geometry, &length);
  qa_navigation_map_identity map = {geometry->requested_path, content.world.kind, geometry->digest};
  if (!qa_preload_navigation(&navigation->prepared, map, resources, navigation->map_bytes, length, true, provenance_content(geometry)))
    qa_range_error("Out of memory");
  return navigation;
}

void qa_app_sim_application_monster_navigation_install(qa_app_sim_application_monster_navigation *navigation,
    qa_app_sim_monster_navigation_simulation simulation, qa_q1_monster_movement *movement) {
  qa_q1_services *game = simulation.q1_game(simulation.ctx);
  /* MG3 monster classes exist only in the Q1 source program; other hosts (e.g. QuakeWorld
   * deathmatch on mg3 maps) spawn no MG3 monsters, so there is nothing to navigate. */
  if (game == NULL) return;
  qa_executable_recipe recipe = *simulation.recipe;
  recipe.movement = recipe.map.entities;
  qa_movement_profile selected = qa_app_sim_movement_profile(recipe);
  if (!is(selected.kind, "q1-netquake") && !is(selected.kind, "q1-quakeworld"))
    qa_raise("Error", 0, 0, "%s", "MG3 monsters require Q1 source movement");
  mnav_install *install = calloc(1, sizeof *install);
  if (install == NULL) qa_range_error("Out of memory");
  install->navigation = navigation; install->simulation = simulation; install->movement = movement; install->selected = selected;
  navigation->installs = grow(navigation->installs, &navigation->install_cap, navigation->install_count, sizeof *navigation->installs);
  navigation->installs[navigation->install_count++] = install;
  qa_q1am_register_mg3_monster_navigation(game, (qa_q1am_nav_host){install, for_actor});
}

void qa_app_sim_application_monster_navigation_destroy(qa_app_sim_application_monster_navigation *navigation) {
  if (navigation == NULL) return;
  for (size_t i = 0; i < navigation->install_count; i++) {
    mnav_install *install = navigation->installs[i];
    for (size_t j = 0; j < install->actor_count; j++) {
      mnav_actor *actor = install->actors[j];
      qa_navigation_runtime_destroy(&actor->runtime);
      free(actor->route); free(actor);
    }
    for (size_t j = 0; j < install->graph_count; j++) {
      qa_loaded_navigation_destroy(&install->graphs[j]->loaded);
      free(install->graphs[j]->topology); free(install->graphs[j]);
    }
    free(install->actors); free(install->graphs); free(install);
  }
  free(navigation->installs);
  qa_prepared_navigation_destroy(&navigation->prepared);
  free(navigation->map_bytes);
  free(navigation);
}
