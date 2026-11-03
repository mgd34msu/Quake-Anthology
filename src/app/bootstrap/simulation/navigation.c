/* Port of src/app/bootstrap/simulation/navigation.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/navigation.h"
#include <math.h>
#include <stdnoreturn.h>
#include <stdlib.h>
#include <string.h>
#include "qa/bots/navigation/entity_binding.h"
#include "qa/bots/navigation/movement.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static void *grow(void *array, size_t *capacity, size_t count, size_t element) {
  if (count < *capacity) return array;
  size_t next = *capacity ? *capacity * 2 : 8;
  void *grown = realloc(array, next * element);
  if (grown == NULL) qa_range_error("Out of memory");
  *capacity = next; return grown;
}
static noreturn void fail(const char *message) { qa_raise("Error", 0, 0, "%s", message); }

qa_app_sim_locomotion_player qa_app_sim_locomotion_of(const qa_app_sim_movement_prediction_player *player) {
  qa_app_sim_locomotion_player out; memset(&out, 0, sizeof out);
  out.profile = player->profile; out.standing_bounds = player->standing_bounds;
  out.has_source_movement = player->has_source_movement; out.source_movement = player->source_movement;
  out.character = player->character; out.world_gravity = player->world_gravity;
  out.has_q2_movement_config = player->has_q2_movement_config; out.q2_movement_config = player->q2_movement_config;
  out.flight = player->flight;
  return out;
}

qa_navigation_profile qa_app_sim_bot_navigation_profile(qa_app_sim_locomotion_player player) {
  qa_app_sim_selected_profile_player selected; memset(&selected, 0, sizeof selected);
  selected.profile = player.profile; selected.world_gravity = player.world_gravity;
  selected.has_source_movement = player.has_source_movement; selected.source_movement = player.source_movement;
  selected.has_q2_movement_config = player.has_q2_movement_config; selected.q2_movement_config = player.q2_movement_config;
  qa_movement_profile movement = qa_app_sim_selected_movement_profile(selected, NULL);
  bool crouches = is(movement.kind, "q2-classic") || is(movement.kind, "q2-rerelease") || is(movement.kind, "q3");
  qa_navigation_profile out; memset(&out, 0, sizeof out);
  out.movement = movement;
  out.shape.kind = "box"; out.shape.bounds = player.standing_bounds;
  if (crouches) { out.has_crouched_shape = true; out.crouched_shape.kind = "box"; out.crouched_shape.bounds = qa_app_sim_player_crouched_bounds(player); }
  out.policy = qa_app_sim_player_trace_policy(player);
  uint32_t modes = 1u << QA_TM_WALK | 1u << QA_TM_JUMP | 1u << QA_TM_DROP | 1u << QA_TM_SWIM
      | 1u << QA_TM_WATER_JUMP | 1u << QA_TM_LADDER | 1u << QA_TM_MOVER;
  if (crouches) modes |= 1u << QA_TM_CROUCH;
  /* TFL_DEFAULT includes TFL_TELEPORT and TFL_JUMPPAD; routing leaves selection to the travel flags. */
  if (is(movement.kind, "q3")) modes |= 1u << QA_TM_TELEPORT | 1u << QA_TM_JUMP_PAD;
  out.capabilities = modes;
  out.maximum_step = 18; out.minimum_floor_normal = 0.7; out.maximum_drop = 128; out.team = -1; out.monster = false;
  return out;
}

/* ---- checkpoint values ---- */
static bool set_number(qa_save_value *object, const char *key, double value) {
  qa_save_value *child = qa_save_value_number(value);
  return child != NULL && qa_save_value_set(object, key, child);
}
static bool set_string(qa_save_value *object, const char *key, const char *value) {
  qa_save_value *child = qa_save_value_string(value != NULL ? value : "");
  return child != NULL && qa_save_value_set(object, key, child);
}
static bool set_boolean(qa_save_value *object, const char *key, bool value) {
  qa_save_value *child = qa_save_value_boolean(value);
  return child != NULL && qa_save_value_set(object, key, child);
}
qa_save_value *qa_app_sim_navigation_runtime_checkpoint_value(const qa_navigation_runtime_checkpoint *c) {
  qa_save_value *out = qa_save_value_object(), *map = qa_save_value_object();
  qa_save_value *enabled = qa_save_value_array(), *blocked = qa_save_value_array(), *admissions = qa_save_value_array();
  bool ok = out && map && enabled && blocked && admissions;
  ok = ok && set_number(out, "version", 1);
  ok = ok && set_string(map, "name", c->map_name) && set_string(map, "format", c->map_format) && set_string(map, "digest", c->map_digest);
  for (size_t i = 0; ok && i < c->enabled_count; i++) {
    qa_save_value *entry = qa_save_value_object();
    ok = entry && set_number(entry, "id", (double)c->enabled[i].id) && set_boolean(entry, "enabled", c->enabled[i].enabled);
    if (entry && !(ok && qa_save_value_push(enabled, entry))) { if (ok) qa_save_value_free(entry); ok = false; }
  }
  for (size_t i = 0; ok && i < c->blocked_count; i++) {
    qa_save_value *entry = qa_save_value_object();
    ok = entry && set_number(entry, "id", (double)c->blocked[i].id) && set_string(entry, "reason", c->blocked[i].reason);
    if (entry && !(ok && qa_save_value_push(blocked, entry))) { if (ok) qa_save_value_free(entry); ok = false; }
  }
  for (size_t i = 0; ok && i < c->admission_count; i++) {
    qa_save_value *entry = qa_save_value_object();
    ok = entry && set_number(entry, "id", (double)c->admission_seconds[i].id) && set_number(entry, "seconds", c->admission_seconds[i].seconds);
    if (entry && !(ok && qa_save_value_push(admissions, entry))) { if (ok) qa_save_value_free(entry); ok = false; }
  }
  if (ok && qa_save_value_set(out, "map", map)) map = NULL; else ok = false;
  if (ok && qa_save_value_set(out, "enabled", enabled)) enabled = NULL; else ok = false;
  if (ok && qa_save_value_set(out, "blocked", blocked)) blocked = NULL; else ok = false;
  if (ok && qa_save_value_set(out, "admissionSeconds", admissions)) admissions = NULL; else ok = false;
  ok = ok && set_number(out, "worldRevision", c->world_revision) && set_number(out, "generation", (double)c->generation);
  if (map) qa_save_value_free(map);
  if (enabled) qa_save_value_free(enabled);
  if (blocked) qa_save_value_free(blocked);
  if (admissions) qa_save_value_free(admissions);
  if (!ok) { if (out) qa_save_value_free(out); return NULL; }
  return out;
}
qa_save_value *qa_app_sim_bot_navigation_checkpoint_value(const qa_app_sim_bot_navigation_checkpoint *c) {
  qa_save_value *out = qa_save_value_object(), *clients = qa_save_value_array();
  qa_save_value *base = qa_app_sim_navigation_runtime_checkpoint_value(&c->base);
  bool ok = out && clients && base && set_number(out, "version", c->version);
  if (ok && qa_save_value_set(out, "base", base)) base = NULL; else ok = false;
  for (size_t i = 0; ok && i < c->client_count; i++) {
    qa_save_value *entry = qa_save_value_object(), *runtime = qa_app_sim_navigation_runtime_checkpoint_value(&c->clients[i].runtime);
    ok = entry && runtime && set_number(entry, "client", (double)c->clients[i].client) && set_boolean(entry, "reusable", c->clients[i].reusable);
    if (ok && qa_save_value_set(entry, "runtime", runtime)) runtime = NULL; else ok = false;
    if (ok && qa_save_value_push(clients, entry)) entry = NULL; else ok = false;
    if (runtime) qa_save_value_free(runtime);
    if (entry) qa_save_value_free(entry);
  }
  if (ok && qa_save_value_set(out, "clients", clients)) clients = NULL; else ok = false;
  if (base) qa_save_value_free(base);
  if (clients) qa_save_value_free(clients);
  if (!ok) { if (out) qa_save_value_free(out); return NULL; }
  return out;
}
void qa_app_sim_bot_navigation_checkpoint_destroy(qa_app_sim_bot_navigation_checkpoint *c) {
  qa_navigation_runtime_checkpoint_destroy(&c->base);
  for (size_t i = 0; i < c->client_count; i++) qa_navigation_runtime_checkpoint_destroy(&c->clients[i].runtime);
  free(c->clients); c->clients = NULL; c->client_count = 0;
}

/* ---- navigation state ---- */
typedef struct nav_world nav_world;
typedef struct nav_route {
  nav_world *world;
  qa_movement_route_admission admission;
  qa_app_sim_player_movement_prediction *prediction;
  qa_traversal_request request; /* the request the prediction began with */
} nav_route;
typedef struct nav_bound_model { qa_navigation_entity_binding binding; double model; } nav_bound_model;
struct nav_world {
  qa_app_sim_application_bot_navigation *nav;
  bool has_player; qa_app_sim_nav_player player; /* TS selectedPlayer */
  qa_actor_id pass_actor;
  qa_navigation_world world;
  nav_bound_model *bound; size_t bound_count, bound_cap; /* TS boundModels */
  nav_route *route; /* latest begin_route */
  qa_spatial_actor *linked; size_t linked_cap;
  qa_spatial_actor *hazards; size_t hazard_cap;
  qa_navigation_train_stop *stops; size_t stop_cap;
};
typedef struct nav_slot { /* one NavigationRuntime over {...graph, profile} */
  qa_navigation_graph graph;
  qa_navigation_runtime runtime;
  nav_world *world;
} nav_slot;
typedef struct nav_client {
  int64_t client;
  bool has_player; const void *player; /* TS cached.player (identity) */
  qa_app_sim_locomotion_player locomotion;
  nav_slot *slot;
} nav_client;
typedef struct nav_projection { qa_client_id client; qa_app_q3_guest_movement_projection *projection; } nav_projection;
struct qa_app_sim_application_bot_navigation {
  qa_app_sim_navigation_content content;
  qa_app_sim_navigation_simulation simulation;
  nav_projection *guest; size_t guest_count, guest_cap; /* TS guestPlayers */
  qa_app_sim_nav_player *players; size_t player_count, player_cap;
  bool first_is_player; const void *first_identity; qa_app_sim_locomotion_player first; /* TS first */
  uint8_t *map_bytes;
  nav_world *loaded_world;
  qa_loaded_navigation loaded;
  nav_slot *base; /* NULL: baseRuntime is loaded.runtime */
  nav_client *clients; size_t client_count, client_cap;
  nav_slot **retired; size_t retired_count, retired_cap; /* runtimes TS lets the GC keep while referenced */
  qa_selected_bot_navigation_bots_a selected;
  qa_vec3 probe_trajectory[1];
};

static void route_free(nav_route *route) {
  if (route == NULL) return;
  if (route->prediction) qa_app_sim_player_movement_prediction_destroy(route->prediction);
  free(route);
}
static void world_free(nav_world *world) {
  if (world == NULL) return;
  route_free(world->route);
  free(world->bound); free(world->linked); free(world->hazards); free(world->stops);
  free(world);
}
static void slot_free(nav_slot *slot) {
  if (slot == NULL) return;
  qa_navigation_runtime_destroy(&slot->runtime);
  world_free(slot->world);
  free(slot);
}
static void retire(qa_app_sim_application_bot_navigation *nav, nav_slot *slot) {
  if (slot == NULL) return;
  nav->retired = grow(nav->retired, &nav->retired_cap, nav->retired_count, sizeof *nav->retired);
  nav->retired[nav->retired_count++] = slot;
}

/* TS players(). */
static size_t players(qa_app_sim_application_bot_navigation *nav, const qa_app_sim_nav_player **out) {
  const qa_app_sim_navigation_simulation *sim = &nav->simulation;
  nav->player_count = 0;
  qa_app_q3_guest_movement_host host;
  if (!sim->q3_guest(sim->ctx, &host)) {
    size_t count = 0;
    const qa_owned_actor *actors = sim->players(sim->ctx, &count);
    for (size_t i = 0; i < count; i++) {
      qa_app_sim_nav_player player;
      if (!sim->movement_player(sim->ctx, actors[i], &player)) continue;
      nav->players = grow(nav->players, &nav->player_cap, nav->player_count, sizeof *nav->players);
      nav->players[nav->player_count++] = player;
    }
    *out = nav->players; return nav->player_count;
  }
  size_t count = 0;
  const qa_app_q3_player *current = sim->q3_guest_players(sim->ctx, &count);
  for (size_t i = 0; i < nav->guest_count;) {
    bool present = false;
    for (size_t j = 0; j < count; j++) if (qa_same_client(current[j].client, nav->guest[i].client)) present = true;
    if (present) { i++; continue; }
    qa_app_q3_guest_movement_projection_destroy(nav->guest[i].projection);
    memmove(&nav->guest[i], &nav->guest[i + 1], (nav->guest_count - i - 1) * sizeof *nav->guest);
    nav->guest_count--;
  }
  for (size_t j = 0; j < count; j++) {
    qa_app_q3_guest_movement_projection *projection = NULL;
    for (size_t i = 0; i < nav->guest_count; i++) if (qa_same_client(nav->guest[i].client, current[j].client)) projection = nav->guest[i].projection;
    if (projection == NULL) {
      projection = qa_app_guest_movement_projection_create(host, current[j], *nav->content.recipe);
      nav->guest = grow(nav->guest, &nav->guest_cap, nav->guest_count, sizeof *nav->guest);
      nav->guest[nav->guest_count++] = (nav_projection){current[j].client, projection};
    }
    qa_app_sim_nav_player player = {projection, qa_app_q3_guest_movement_projection_refresh(projection)};
    nav->players = grow(nav->players, &nav->player_cap, nav->player_count, sizeof *nav->players);
    nav->players[nav->player_count++] = player;
  }
  *out = nav->players; return nav->player_count;
}
static bool first_player(qa_app_sim_application_bot_navigation *nav, qa_app_sim_nav_player *out) {
  const qa_app_sim_nav_player *list; size_t count = players(nav, &list);
  if (count == 0) return false;
  *out = list[0]; return true;
}
static bool find_player(qa_app_sim_application_bot_navigation *nav, int64_t client, qa_app_sim_nav_player *out) {
  const qa_app_sim_nav_player *list; size_t count = players(nav, &list);
  for (size_t i = 0; i < count; i++) if (list[i].value.client.slot == client) { *out = list[i]; return true; }
  return false;
}
/* TS playerFor(client). */
static qa_app_sim_nav_player player_for(qa_app_sim_application_bot_navigation *nav, int64_t client) {
  qa_app_sim_nav_player player;
  if (!find_player(nav, client, &player)) qa_raise("Error", 0, 0, "Navigation client %lld is not admitted", (long long)client);
  return player;
}
/* The live fields of a closed-over player object (TS reads its getters). */
static qa_app_sim_nav_player live(qa_app_sim_application_bot_navigation *nav, const qa_app_sim_nav_player *stored) {
  const qa_app_sim_nav_player *list; size_t count = players(nav, &list);
  for (size_t i = 0; i < count; i++) if (list[i].identity == stored->identity) return list[i];
  return *stored;
}
/* selectedPlayer ?? firstPlayer() */
static bool world_player(nav_world *w, qa_app_sim_nav_player *out) {
  if (w->has_player) { *out = live(w->nav, &w->player); return true; }
  return first_player(w->nav, out);
}

/* ---- prediction driver ---- */
static qa_movement_input route_input(void *ctx, const qa_movement_result *previous, int64_t index, qa_traversal_request action) {
  nav_route *route = ctx;
  qa_vec3 at = previous != NULL && is(previous->status, "active") ? qa_app_sim_movement_origin(previous->state) : route->request.from;
  double x = action.to.x - at.x, y = action.to.y - at.y, distance = qa_js_hypot2(x, y);
  double scale = distance == 0 ? 0 : qa_js_min(400, distance * 20) / distance;
  qa_vec3 move = {x * scale, y * scale,
      action.mode == QA_TM_JUMP || action.mode == QA_TM_WATER_JUMP || action.mode == QA_TM_LADDER ? 400 : action.mode == QA_TM_CROUCH ? -400 : 0};
  return qa_app_sim_player_movement_prediction_input(route->prediction, previous, (double)index, move);
}
static bool route_begin(void *ctx, qa_traversal_request request, qa_navigation_profile selected, qa_navigation_prediction *out) {
  nav_route *route = ctx;
  qa_app_sim_nav_player player;
  if (!world_player(route->world, &player)) return false;
  const qa_app_sim_navigation_simulation *sim = &route->world->nav->simulation;
  qa_app_sim_prediction_simulation view = sim->prediction(sim->ctx);
  qa_vec3 zero = {0, 0, 0};
  qa_app_sim_player_movement_prediction *projection = qa_app_sim_create_player_movement_prediction(view, player.value, request.from, zero, 16, false, &selected.movement);
  if (route->prediction) qa_app_sim_player_movement_prediction_destroy(route->prediction);
  route->prediction = projection; route->request = request;
  memset(out, 0, sizeof *out);
  out->provider = qa_app_sim_player_movement_prediction_provider(projection);
  out->services = qa_app_sim_player_movement_prediction_services(projection);
  out->ctx = route; out->input = route_input;
  return true;
}
static void route_init(nav_route *route, nav_world *world, qa_navigation_profile profile) {
  memset(route, 0, sizeof *route);
  route->world = world;
  qa_navigation_prediction_limits limits; memset(&limits, 0, sizeof limits);
  qa_movement_route_admission_init(&route->admission, (qa_navigation_prediction_driver){route, route_begin}, profile, limits);
}
static qa_traversal_admission world_admit(void *ctx, qa_traversal_request request, qa_navigation_profile profile) {
  nav_route route; route_init(&route, ctx, profile);
  qa_traversal_admission result = qa_movement_route_admission_admit(&route.admission, request);
  if (route.prediction) qa_app_sim_player_movement_prediction_destroy(route.prediction);
  return result;
}
static qa_navigation_route_prediction world_begin_route(void *ctx, qa_navigation_profile profile) {
  nav_world *w = ctx;
  nav_route *route = malloc(sizeof *route);
  if (route == NULL) qa_range_error("Out of memory");
  route_init(route, w, profile);
  route_free(w->route); w->route = route;
  return qa_movement_route_admission_as_prediction(&route->admission);
}
static double world_revision(void *ctx) {
  nav_world *w = ctx;
  return w->nav->simulation.time_seconds(w->nav->simulation.ctx) * 1000;
}

/* ---- entity and hazard ---- */
static const qa_spatial_actor *copy_actors(qa_spatial_actor **buffer, size_t *capacity, const qa_spatial_actor *found, size_t count) {
  if (count > *capacity) {
    qa_spatial_actor *grown = realloc(*buffer, count * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    *buffer = grown; *capacity = count;
  }
  if (count) memcpy(*buffer, found, count * sizeof *found);
  return *buffer;
}
static bool same_binding(qa_navigation_entity_binding a, qa_navigation_entity_binding b) {
  return a.has_model == b.has_model && (!a.has_model || a.model == b.model) && a.raw == b.raw && a.raw_count == b.raw_count
      && memcmp(&a.bounds, &b.bounds, sizeof a.bounds) == 0;
}
static int phase_index(const char *phase) {
  return is(phase, "up") ? 1 : is(phase, "top") ? 2 : is(phase, "down") ? 3 : 0;
}
static qa_q1_actor *q1_entity(const qa_app_sim_navigation_simulation *sim, qa_actor_id actor) {
  qa_q1_services *game = sim->q1_game(sim->ctx);
  return game == NULL ? NULL : qa_q1_get_entity(game, &actor);
}
static bool world_entity(void *ctx, qa_navigation_entity_binding binding, qa_navigation_entity_state *out) {
  nav_world *w = ctx;
  const qa_app_sim_navigation_simulation *sim = &w->nav->simulation;
  size_t count = 0;
  const qa_spatial_actor *found = qa_shared_query_actors(sim->scene, qa_shared_model_bounds(sim->scene, 0), "both", &count);
  const qa_spatial_actor *linked = copy_actors(&w->linked, &w->linked_cap, found, count);
  bool has_model = binding.has_model; double model = (double)binding.model;
  if (!has_model) for (size_t i = 0; i < w->bound_count; i++)
    if (same_binding(w->bound[i].binding, binding)) { has_model = true; model = w->bound[i].model; break; }
  if (!has_model) {
    for (size_t i = 0; i < count; i++) {
      const qa_spatial_actor *entry = &linked[i];
      if (!is(entry->collision.shape_kind, "model")) continue;
      qa_q1_actor *entity = q1_entity(sim, entry->body.actor);
      if (entity == NULL) continue;
      const qa_vec3 endpoints[3] = {entry->body.state.origin, entity->pos1, entity->pos2};
      bool matched = false;
      for (int e = 0; e < 3 && !matched; e++)
        matched = qa_source_mover_bounds_match(binding.bounds, entry->body.absolute_bounds, entry->body.state.origin, endpoints[e]);
      if (!matched) continue;
      if (has_model) return false;
      has_model = true; model = entry->collision.shape_model;
    }
    if (has_model) {
      w->bound = grow(w->bound, &w->bound_cap, w->bound_count, sizeof *w->bound);
      w->bound[w->bound_count++] = (nav_bound_model){binding, model};
    }
  }
  if (!has_model) return false;
  for (size_t i = 0; i < count; i++) {
    const qa_spatial_actor *entry = &linked[i];
    if (!is(entry->collision.shape_kind, "model") || entry->collision.shape_model != model) continue;
    memset(out, 0, sizeof *out);
    out->actor = entry->body.actor; out->bounds = entry->body.absolute_bounds; out->velocity = entry->body.state.velocity;
    qa_q1_actor *q1 = q1_entity(sim, entry->body.actor);
    if (q1 != NULL) {
      qa_q1_actor *master = q1->door_group_count > 0 && q1->door_group[0] != NULL ? q1->door_group[0] : q1;
      int32_t flags = qa_js_to_int32(master->spawnflags);
      const char *key = (flags & 8) != 0 ? "q1:key/gold" : (flags & 16) != 0 ? "q1:key/silver" : NULL;
      qa_app_sim_nav_player player; bool has_player = world_player(w, &player);
      bool needs_key = key != NULL && master->touch.fn != NULL
          && (!has_player || qa_shared_inventory_table_count(sim->inventory, player.value.actor.id, key) == 0);
      out->enabled = is(q1->solid, "bsp");
      if (q1->has_move) { out->has_destination = true; out->destination = q1->move.destination; }
      if (is(q1->classname, "func_plat")) {
        out->has_elevator = true; out->elevator_origin = entry->body.state.origin;
        out->elevator_bottom = q1->pos2; out->elevator_top = q1->pos1; out->elevator_phase = phase_index(q1->state);
      }
      out->locked = is(q1->classname, "func_plat") ? !q1->activated : is(q1->classname, "func_door")
          && (is(master->state, "bottom") || is(master->state, "down"))
          && ((master->targetname != NULL && master->targetname[0] != '\0') || master->max_health > 0
              || (flags & 4) != 0 || needs_key);
      return true;
    }
    qa_app_sim_nav_q2_source q2; bool has_q2 = sim->q2_source(sim->ctx, &q2);
    qa_q2_entity *entity = has_q2 ? q2.game->entity(q2.game->ctx, &entry->body.actor) : NULL;
    if (has_q2 && entity != NULL) {
      qa_q2_base_platform_state *base_platform = qa_q2_base_entity_module_platform_state(q2.base_entities, entity);
      bool has_platform = base_platform != NULL; qa_vec3 top = {0, 0, 0}, bottom = {0, 0, 0}; int phase = 0;
      if (has_platform) { top = base_platform->top; bottom = base_platform->bottom; phase = phase_index(base_platform->phase); }
      else {
        bool picked = false;
        for (size_t e = 0; e < q2.expansion_count; e++) {
          qa_q2mp_platform_state state;
          if (q2.expansion_movers[e] == NULL || !qa_q2mp_rogue_movers_platform_state(q2.expansion_movers[e], entity, &state)) continue;
          if (!picked) { picked = true; top = state.top; bottom = state.bottom; phase = phase_index(qa_q2mp_platform_phase_name(state.phase)); }
        }
        has_platform = picked;
      }
      qa_q2_train_route train; bool has_train = qa_q2_mover_module_train_route(q2.movers, entity, q2.game, &train);
      out->enabled = is(entity->solid, "brush");
      qa_q2_base_traversal base = qa_q2_base_entity_module_mover_traversal(q2.base_entities, entity);
      if (base.has_value) { out->locked = base.locked; out->has_destination = base.has_destination; out->destination = base.destination; }
      else {
        qa_q2_traversal traversal = qa_q2_mover_module_traversal(q2.movers, entity);
        out->locked = traversal.locked; out->has_destination = traversal.has_destination; out->destination = traversal.destination;
      }
      if (has_train) {
        if (train.stop_count > w->stop_cap) {
          qa_navigation_train_stop *grown = realloc(w->stops, train.stop_count * sizeof *grown);
          if (grown == NULL) qa_range_error("Out of memory");
          w->stops = grown; w->stop_cap = train.stop_count;
        }
        for (size_t s = 0; s < train.stop_count; s++) {
          const qa_q2_train_stop *stop = &train.stops[s];
          w->stops[s] = (qa_navigation_train_stop){stop->actor.slot, stop->origin, stop->has_next, stop->has_next ? stop->next.slot : 0, stop->wait, stop->teleport};
        }
        out->has_train = true; out->train_origin = entry->body.state.origin; out->train_running = train.running;
        out->train_stops = w->stops; out->train_stop_count = train.stop_count;
      }
      if (has_platform) {
        out->has_elevator = true; out->elevator_top = top; out->elevator_bottom = bottom; out->elevator_phase = phase;
        out->elevator_origin = entry->body.state.origin;
      }
      return true;
    }
    qa_app_sim_nav_q3_source q3source; bool has_q3 = sim->q3_source(sim->ctx, &q3source);
    qa_q3_game_entity *q3 = has_q3 ? qa_q3_base_entity_records_native_by_actor(q3source.records, true, entry->body.actor) : NULL;
    if (q3 != NULL) {
      qa_q3_game_entity *master = q3->teammaster != NULL ? q3->teammaster : q3;
      qa_app_sim_nav_q3_source source; bool has_source = sim->q3_source(sim->ctx, &source);
      size_t trigger_count = 0;
      const qa_spatial_actor *triggers = qa_shared_query_actors(sim->scene, qa_shared_model_bounds(sim->scene, 0), "trigger", &trigger_count);
      bool automatic = false;
      for (size_t t = 0; t < trigger_count && !automatic; t++) {
        qa_q3_game_entity *trigger = has_source ? qa_q3_base_entity_records_native_by_actor(source.records, true, triggers[t].body.actor) : NULL;
        automatic = trigger != NULL && is(qa_q3_game_entity_classname(trigger), "door_trigger") && trigger->parent == master
            && qa_q3_base_entity_shared_linked(&trigger->r) && trigger->touch.fn != NULL;
      }
      const char *classname = qa_q3_game_entity_classname(q3);
      out->enabled = qa_q3_base_entity_shared_linked(&q3->r);
      out->locked = master->mover_state == QA_Q3_MOVER_POS1 && ((is(classname, "func_door") && !automatic)
          || (is(classname, "func_plat") && master->targetname != NULL));
      if (q3->mover_state == QA_Q3_MOVER_ONE_TO_TWO) { out->has_destination = true; out->destination = q3->pos2; }
      else if (q3->mover_state == QA_Q3_MOVER_TWO_TO_ONE) { out->has_destination = true; out->destination = q3->pos1; }
      return true;
    }
    return false;
  }
  return false;
}
static bool world_hazard(void *ctx, qa_bounds bounds) {
  nav_world *w = ctx;
  const qa_app_sim_navigation_simulation *sim = &w->nav->simulation;
  size_t count = 0;
  const qa_spatial_actor *found = qa_shared_query_actors(sim->scene, bounds, "both", &count);
  const qa_spatial_actor *actors = copy_actors(&w->hazards, &w->hazard_cap, found, count);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = actors[i].body.actor;
    qa_q1_actor *q1 = q1_entity(sim, actor);
    if (q1 != NULL && is(q1->classname, "trigger_hurt") && is(q1->solid, "trigger") && q1->damage > 0 && q1->touch.fn != NULL) return true;
    qa_app_sim_nav_q2_source q2; bool has_q2 = sim->q2_source(sim->ctx, &q2);
    qa_q2_entity *entity = has_q2 ? q2.game->entity(q2.game->ctx, &actor) : NULL;
    if (entity != NULL && is(entity->classname, "trigger_hurt") && is(entity->solid, "trigger") && entity->damage > 0 && entity->touch.fn != NULL
        && entity->timestamp <= q2.game->host->now(q2.game->host->ctx)) return true;
    qa_app_sim_nav_q3_source q3; bool has_q3 = sim->q3_source(sim->ctx, &q3);
    qa_q3_game_entity *native = has_q3 ? qa_q3_base_entity_records_native_by_actor(q3.records, true, actor) : NULL;
    if (native != NULL && is(qa_q3_game_entity_classname(native), "trigger_hurt") && qa_q3_base_entity_shared_linked(&native->r)
        && native->damage > 0 && native->touch.fn != NULL && native->timestamp <= q3.level_time) return true;
  }
  return false;
}

/* TS worldFor(selectedPlayer). */
static nav_world *world_for(qa_app_sim_application_bot_navigation *nav, const qa_app_sim_nav_player *selected) {
  nav_world *w = calloc(1, sizeof *w);
  if (w == NULL) qa_range_error("Out of memory");
  w->nav = nav;
  if (selected != NULL) { w->has_player = true; w->player = *selected; w->pass_actor = selected->value.actor.id; }
  w->world.scene = qa_shared_as_scene_queries(nav->simulation.scene);
  w->world.pass_actor = selected != NULL ? &w->pass_actor : NULL;
  w->world.ctx = w;
  w->world.admit = world_admit;
  w->world.begin_route = world_begin_route;
  w->world.get_entity = world_entity;
  w->world.hazard = world_hazard;
  w->world.revision_of = world_revision;
  return w;
}
static const qa_navigation_graph *loaded_graph(qa_app_sim_application_bot_navigation *nav) { return nav->loaded.runtime.graph; }
static nav_slot *slot_create(qa_app_sim_application_bot_navigation *nav, qa_app_sim_locomotion_player locomotion, nav_world *world) {
  nav_slot *slot = calloc(1, sizeof *slot);
  if (slot == NULL) qa_range_error("Out of memory");
  slot->graph = *loaded_graph(nav);
  slot->graph.profile = qa_app_sim_bot_navigation_profile(locomotion);
  slot->world = world;
  if (!qa_navigation_runtime_init(&slot->runtime, &slot->graph, &world->world)) qa_range_error("Out of memory");
  return slot;
}
static qa_navigation_runtime *base_runtime(qa_app_sim_application_bot_navigation *nav) {
  return nav->base != NULL ? &nav->base->runtime : &nav->loaded.runtime;
}

qa_navigation_runtime *qa_app_sim_application_bot_navigation_for_client(qa_app_sim_application_bot_navigation *nav, int64_t client) {
  qa_app_sim_nav_player player = player_for(nav, client);
  nav_client *cached = NULL;
  for (size_t i = 0; i < nav->client_count; i++) if (nav->clients[i].client == client) cached = &nav->clients[i];
  qa_app_sim_locomotion_player locomotion = qa_app_sim_locomotion_of(&player.value);
  if (cached != NULL && cached->has_player && cached->player == player.identity && qa_app_sim_player_locomotion_matches(locomotion, cached->locomotion))
    return &cached->slot->runtime;
  nav_slot *slot = slot_create(nav, locomotion, world_for(nav, &player));
  if (cached == NULL) {
    nav->clients = grow(nav->clients, &nav->client_cap, nav->client_count, sizeof *nav->clients);
    cached = &nav->clients[nav->client_count++];
  } else retire(nav, cached->slot);
  *cached = (nav_client){client, true, player.identity, qa_app_sim_capture_player_locomotion(locomotion), slot};
  return &slot->runtime;
}
static qa_navigation_runtime *selected_for_client(void *ctx, int64_t client) {
  return qa_app_sim_application_bot_navigation_for_client(ctx, client);
}
static qa_bot_travel_prediction_result selected_predict(void *ctx, qa_bot_movement_prediction query) {
  return qa_app_sim_application_bot_navigation_predict_client_movement(ctx, query);
}
static bool selected_travel_weapon(void *ctx, int64_t client, const char *mode, int64_t *out) {
  return qa_app_sim_application_bot_navigation_travel_weapon(ctx, client, mode, out);
}
static const char *provenance_content(const qa_resolved_resource_reference *reference) {
  return reference->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE
      ? reference->provenance.archive_mount->identity.content : reference->provenance.loose_mount->identity.content;
}

qa_app_sim_application_bot_navigation *qa_app_sim_create_application_bot_navigation(
    qa_app_sim_navigation_content content, qa_app_sim_navigation_simulation simulation) {
  if (content.recipe != simulation.recipe) fail("Navigation and simulation must use the same loaded recipe");
  qa_app_sim_application_bot_navigation *nav = calloc(1, sizeof *nav);
  if (nav == NULL) qa_range_error("Out of memory");
  nav->content = content; nav->simulation = simulation;
  qa_app_sim_nav_player first;
  if (first_player(nav, &first)) {
    nav->first_is_player = true; nav->first_identity = first.identity; nav->first = qa_app_sim_locomotion_of(&first.value);
  } else nav->first = qa_app_sim_locomotion_template(*content.recipe);
  qa_navigation_profile profile = qa_app_sim_bot_navigation_profile(nav->first);
  qa_app_sim_nav_player initial; bool has_initial = first_player(nav, &initial);
  nav->loaded_world = world_for(nav, has_initial ? &initial : NULL);
  const qa_resolved_resource_reference *geometry = &content.recipe->map.geometry;
  qa_navigation_load_options options; memset(&options, 0, sizeof options);
  options.construction.geometry = simulation.world;
  options.construction.map = (qa_navigation_map_identity){geometry->requested_path, simulation.world.kind, geometry->digest};
  options.construction.profile = profile;
  options.construction.world = &nav->loaded_world->world;
  size_t connection_count = 0;
  options.construction.connections = simulation.train_connections(simulation.ctx, profile, &connection_count);
  options.construction.connection_count = connection_count;
  options.resources = content.for_content(content.ctx, provenance_content(geometry));
  options.has_navigation_content = true; options.navigation_content = provenance_content(geometry);
  size_t length = 0;
  nav->map_bytes = qa_content_mounted_read_reference(content.mounts, geometry, &length);
  options.map_bytes = nav->map_bytes; options.map_byte_length = length;
  if (!qa_load_navigation(&nav->loaded, options)) qa_range_error("Out of memory");
  nav->selected.ctx = nav;
  nav->selected.runtime = base_runtime(nav);
  nav->selected.for_client = selected_for_client;
  nav->selected.crouched_bounds = profile.has_crouched_shape ? profile.crouched_shape.bounds : nav->first.standing_bounds;
  nav->selected.predict_client_movement = selected_predict;
  nav->selected.travel_weapon = selected_travel_weapon;
  return nav;
}
void qa_app_sim_application_bot_navigation_destroy(qa_app_sim_application_bot_navigation *nav) {
  if (nav == NULL) return;
  for (size_t i = 0; i < nav->client_count; i++) slot_free(nav->clients[i].slot);
  for (size_t i = 0; i < nav->retired_count; i++) slot_free(nav->retired[i]);
  slot_free(nav->base);
  qa_loaded_navigation_destroy(&nav->loaded);
  world_free(nav->loaded_world);
  for (size_t i = 0; i < nav->guest_count; i++) qa_app_q3_guest_movement_projection_destroy(nav->guest[i].projection);
  free(nav->guest); free(nav->players); free(nav->clients); free(nav->retired); free(nav->map_bytes);
  free(nav);
}
const qa_selected_bot_navigation_bots_a *qa_app_sim_application_bot_navigation_selected(qa_app_sim_application_bot_navigation *nav) {
  nav->selected.runtime = base_runtime(nav);
  return &nav->selected;
}
qa_navigation_runtime *qa_app_sim_application_bot_navigation_runtime(qa_app_sim_application_bot_navigation *nav) { return base_runtime(nav); }
qa_bounds qa_app_sim_application_bot_navigation_crouched_bounds(const qa_app_sim_application_bot_navigation *nav) { return nav->selected.crouched_bounds; }
bool qa_app_sim_application_bot_navigation_travel_weapon(qa_app_sim_application_bot_navigation *nav, int64_t client, const char *mode, int64_t *out) {
  (void)nav; (void)client; (void)mode; (void)out; /* TS travelWeapon: () => null */
  return false;
}
qa_bot_travel_prediction_result qa_app_sim_application_bot_navigation_predict_client_movement(
    qa_app_sim_application_bot_navigation *nav, qa_bot_movement_prediction query) {
  /* Entity -1 is a probe with no owner (the jump-pad item test): any admitted player's movement provides the physics. */
  qa_app_sim_nav_player anyone; bool has_anyone = query.entity_num < 0 && first_player(nav, &anyone);
  if (query.entity_num < 0 && !has_anyone) {
    qa_bot_travel_prediction_result out; memset(&out, 0, sizeof out);
    nav->probe_trajectory[0] = query.origin;
    out.end = query.origin; out.velocity = query.velocity; out.frames = 0; out.stop_event = 0; out.has_end_area = false;
    out.trajectory = nav->probe_trajectory; out.trajectory_count = 1; out.seconds = 0; out.grounded = false; out.water_level = 0;
    out.has_trace = false; out.has_bounds = false;
    return out;
  }
  qa_app_sim_nav_player player = has_anyone ? anyone : player_for(nav, query.entity_num);
  qa_navigation_runtime *runtime = !has_anyone ? qa_app_sim_application_bot_navigation_for_client(nav, query.entity_num)
      : qa_app_sim_application_bot_navigation_for_client(nav, anyone.value.client.slot);
  return nav->simulation.predict_movement(nav->simulation.ctx, &player, query, runtime);
}
void qa_app_sim_application_bot_navigation_restart_round(qa_app_sim_application_bot_navigation *nav) {
  qa_app_sim_nav_player first; bool has_first = first_player(nav, &first);
  qa_app_sim_locomotion_player selected = has_first ? qa_app_sim_locomotion_of(&first.value) : qa_app_sim_locomotion_template(*nav->content.recipe);
  nav_slot *slot = slot_create(nav, selected, world_for(nav, NULL));
  retire(nav, nav->base); nav->base = slot;
  for (size_t i = 0; i < nav->client_count; i++) retire(nav, nav->clients[i].slot);
  nav->client_count = 0;
  nav->selected.runtime = base_runtime(nav);
}
bool qa_app_sim_application_bot_navigation_checkpoint(qa_app_sim_application_bot_navigation *nav, qa_app_sim_bot_navigation_checkpoint *out) {
  memset(out, 0, sizeof *out);
  out->version = 1;
  if (!qa_navigation_runtime_make_checkpoint(base_runtime(nav), &out->base)) return false;
  if (nav->client_count) {
    out->clients = calloc(nav->client_count, sizeof *out->clients);
    if (out->clients == NULL) { qa_app_sim_bot_navigation_checkpoint_destroy(out); return false; }
  }
  for (size_t i = 0; i < nav->client_count; i++) {
    const nav_client *cached = &nav->clients[i];
    qa_app_sim_nav_player current; bool has_current = find_player(nav, cached->client, &current);
    qa_app_sim_bot_navigation_client_checkpoint *entry = &out->clients[i];
    entry->client = cached->client;
    entry->reusable = has_current && cached->has_player && current.identity == cached->player
        && qa_app_sim_player_locomotion_matches(qa_app_sim_locomotion_of(&current.value), cached->locomotion);
    if (!qa_navigation_runtime_make_checkpoint(&cached->slot->runtime, &entry->runtime)) { qa_app_sim_bot_navigation_checkpoint_destroy(out); return false; }
    out->client_count = i + 1;
  }
  return true;
}
static qa_save_reader field_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
static bool safe_integer(double value) { return isfinite(value) && trunc(value) == value && fabs(value) <= 9007199254740991.0; }
/* What a restore holds while a SaveFormatError or playerFor error can
 * unwind it (TS GC). */
typedef struct restore_scope {
  qa_save_reader readers[4]; bool live[4]; /* reader, clients, entry, sub */
  bool has_base; qa_navigation_runtime base;
  nav_client *restored; size_t restored_count, restored_cap;
  double *saved; size_t saved_count, saved_cap;
  nav_slot *pending;
} restore_scope;
enum { R_READER, R_CLIENTS, R_ENTRY, R_SUB };
static const qa_save_reader *hold(restore_scope *scope, int index, qa_save_reader reader) {
  scope->readers[index] = reader; scope->live[index] = true; return &scope->readers[index];
}
static void drop(restore_scope *scope, int index) {
  if (scope->live[index]) { qa_save_reader_destroy(&scope->readers[index]); scope->live[index] = false; }
}
static void restore_cleanup(void *arg) {
  restore_scope *scope = arg;
  for (int i = 3; i >= 0; i--) drop(scope, i);
  if (scope->has_base) qa_navigation_runtime_destroy(&scope->base);
  for (size_t i = 0; i < scope->restored_count; i++) slot_free(scope->restored[i].slot);
  slot_free(scope->pending);
  free(scope->restored); free(scope->saved);
}
void qa_app_sim_application_bot_navigation_restore_checkpoint(qa_app_sim_application_bot_navigation *nav,
    const qa_save_value *value, double (*remap_client)(void *ctx, double saved), void *remap_ctx) {
  restore_scope scope; memset(&scope, 0, sizeof scope);
  qa_cleanup guard; qa_cleanup_push(&guard, restore_cleanup, &scope);
  qa_save_reader init;
  if (!qa_save_reader_init(&init, value, "applicationNavigation")) qa_range_error("Out of memory");
  const qa_save_reader *reader = hold(&scope, R_READER, init);
  const qa_save_reader *sub = hold(&scope, R_SUB, field_of(reader, "version"));
  (void)qa_save_reader_literal_number(sub, 1);
  drop(&scope, R_SUB);
  qa_navigation_runtime *current = base_runtime(nav);
  if (!qa_navigation_runtime_init(&scope.base, current->graph, current->world)) qa_range_error("Out of memory");
  scope.has_base = true;
  sub = hold(&scope, R_SUB, field_of(reader, "base"));
  qa_navigation_runtime_restore_checkpoint(&scope.base, sub->value);
  drop(&scope, R_SUB);
  const qa_save_reader *clients = hold(&scope, R_CLIENTS, field_of(reader, "clients"));
  size_t count = qa_save_reader_list_count(clients);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(clients, i, &item)) qa_range_error("Out of memory");
    const qa_save_reader *entry = hold(&scope, R_ENTRY, item);
    sub = hold(&scope, R_SUB, field_of(entry, "client"));
    double saved = qa_save_reader_integer_min(sub, 0);
    drop(&scope, R_SUB);
    double client = remap_client != NULL ? remap_client(remap_ctx, saved) : saved;
    bool duplicate = false;
    for (size_t j = 0; j < scope.saved_count; j++) if (scope.saved[j] == saved) duplicate = true;
    for (size_t j = 0; j < scope.restored_count; j++) if ((double)scope.restored[j].client == client) duplicate = true;
    if (!safe_integer(client) || client < 0 || duplicate) qa_save_reader_fail(entry, "invalid or duplicate navigation client mapping");
    scope.saved = grow(scope.saved, &scope.saved_cap, scope.saved_count, sizeof *scope.saved);
    scope.saved[scope.saved_count++] = saved;
    sub = hold(&scope, R_SUB, field_of(entry, "reusable"));
    bool reusable = qa_save_reader_boolean(sub);
    drop(&scope, R_SUB);
    qa_app_sim_nav_player player; memset(&player, 0, sizeof player);
    if (reusable) player = player_for(nav, (int64_t)client);
    qa_app_sim_locomotion_player locomotion;
    if (reusable) locomotion = qa_app_sim_locomotion_of(&player.value);
    else {
      /* TS `first`: the first player object (read live) or the recipe template. */
      bool live_first = false;
      if (nav->first_is_player) {
        const qa_app_sim_nav_player *list; size_t n = players(nav, &list);
        for (size_t j = 0; j < n && !live_first; j++)
          if (list[j].identity == nav->first_identity) { locomotion = qa_app_sim_locomotion_of(&list[j].value); live_first = true; }
      }
      if (!live_first) locomotion = nav->first;
    }
    locomotion = qa_app_sim_capture_player_locomotion(locomotion);
    scope.pending = slot_create(nav, locomotion, world_for(nav, reusable ? &player : NULL));
    sub = hold(&scope, R_SUB, field_of(entry, "runtime"));
    qa_navigation_runtime_restore_checkpoint(&scope.pending->runtime, sub->value);
    drop(&scope, R_SUB);
    drop(&scope, R_ENTRY);
    scope.restored = grow(scope.restored, &scope.restored_cap, scope.restored_count, sizeof *scope.restored);
    scope.restored[scope.restored_count++] = (nav_client){(int64_t)client, reusable, reusable ? player.identity : NULL, locomotion, scope.pending};
    scope.pending = NULL;
  }
  drop(&scope, R_CLIENTS);
  drop(&scope, R_READER);
  qa_navigation_runtime_checkpoint checkpoint;
  if (!qa_navigation_runtime_make_checkpoint(&scope.base, &checkpoint)) qa_range_error("Out of memory");
  qa_save_value *encoded = qa_app_sim_navigation_runtime_checkpoint_value(&checkpoint);
  qa_navigation_runtime_checkpoint_destroy(&checkpoint);
  if (encoded == NULL) qa_range_error("Out of memory");
  qa_navigation_runtime_restore_checkpoint(current, encoded);
  qa_save_value_free(encoded);
  qa_cleanup_pop(&guard);
  qa_navigation_runtime_destroy(&scope.base);
  for (size_t i = 0; i < nav->client_count; i++) retire(nav, nav->clients[i].slot);
  free(nav->clients);
  nav->clients = scope.restored; nav->client_count = scope.restored_count; nav->client_cap = scope.restored_cap;
  free(scope.saved);
}
