/* Port of src/app/bootstrap/simulation/monster-placement.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/monster_placement.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q1/base/species.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

static bool same(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
/* TS string !== string where either side may be undefined. */
static bool differ(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a != b;
  return strcmp(a, b) != 0;
}

bool qa_app_sim_monster_same_vector(qa_vec3 left, qa_vec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

bool qa_app_sim_is_q1_teleport_staging(qa_q1_services *game, qa_body_state body) {
  for (size_t i = 0; i < game->entity_count; i++) {
    const qa_q1_actor *trigger = game->entities[i];
    if (!same(trigger->classname, "trigger_teleport") || trigger->targetname == NULL || trigger->targetname[0] == '\0'
        || !same(trigger->solid, "trigger") || trigger->touch.fn == NULL || (qa_js_to_int32(trigger->spawnflags) & 1) != 0) continue;
    qa_linked_body_result linked = qa_shared_body_table_linked(game->host.bodies, trigger->actor.id);
    if (!linked.has_value) continue;
    qa_bounds bounds = linked.value.absolute_bounds;
    if (body.origin.x + body.bounds.max.x >= bounds.min.x && body.origin.x + body.bounds.min.x <= bounds.max.x
        && body.origin.y + body.bounds.max.y >= bounds.min.y && body.origin.y + body.bounds.min.y <= bounds.max.y
        && body.origin.z + body.bounds.max.z >= bounds.min.z && body.origin.z + body.bounds.min.z <= bounds.max.z
        && qa_q1_find(game, trigger->target != NULL ? trigger->target : "", NULL, 0) != 0) return true;
  }
  return false;
}

static const qa_q1_base_monster_species *species_for(const char *classname) {
  for (size_t i = 0; i < QA_Q1_BASE_SPECIES_COUNT; i++)
    for (size_t j = 0; j < QA_Q1_BASE_SPECIES[i].classname_count; j++)
      if (same(QA_Q1_BASE_SPECIES[i].classnames[j], classname)) return &QA_Q1_BASE_SPECIES[i];
  return NULL;
}

bool qa_app_sim_preserves_authored_q1_placement(qa_app_sim_q1_placement_input input) {
  const qa_monster_definition_reference *definition = &input.definition;
  if (differ(input.map.entities.provider, "q1:official")
      || (differ(definition->source.provider, "q1:monsters/classic/id1") && differ(definition->source.provider, "q1:monsters/rerelease/id1"))
      || differ(input.map.entities.content, definition->source.content) || differ(input.map.geometry_content, definition->source.content)
      || input.authored == NULL) return false;
  const char *classname = qa_q1_entity_value(input.authored, "classname");
  if (differ(classname, definition->classname) || differ(input.entity->classname, classname)) return false;
  const qa_q1_base_monster_species *species = species_for(classname);
  if (species == NULL || !same(input.entity->movement, "step")) return false;
  size_t length = strlen("progs/") + strlen(species->model) + strlen(".mdl") + 1;
  char *model = malloc(length);
  if (model == NULL) qa_range_error("Out of memory");
  strcpy(model, "progs/"); strcat(model, species->model); strcat(model, ".mdl");
  bool model_differs = differ(input.entity->model, model);
  free(model);
  if (model_differs || !qa_app_sim_monster_same_vector(input.body.bounds.min, species->bounds.min)
      || !qa_app_sim_monster_same_vector(input.body.bounds.max, species->bounds.max)) return false;
  const char *text = qa_q1_entity_value(input.authored, "origin");
  qa_vec3 origin = qa_q1_parse_vector(text != NULL ? text : "");
  if (same(species->movement, "fly"))
    return (qa_js_to_int32(input.entity->movement_flags) & 3) == 1 && qa_app_sim_monster_same_vector(input.body.origin, origin);
  if (!same(species->movement, "walk") || input.entity->movement_flags != 32 || input.body.ground != NULL
      || !same(input.entity->solid, "slidebox")) return false;
  qa_vec3 start = origin;
  start.z = origin.z + 1;
  if (!qa_app_sim_monster_same_vector(input.body.origin, start)) return false;
  qa_q1_trace_request request;
  memset(&request, 0, sizeof request);
  request.start = start;
  request.end = start; request.end.z = start.z - 256;
  request.bounds = input.body.bounds;
  request.has_ignore = true; request.ignore = input.entity->actor.id;
  request.monsters = true;
  qa_q1_trace floor = qa_q1_host_trace(input.game, request);
  return floor.fraction == 1 || floor.all_solid;
}

bool qa_app_sim_preserves_authored_q2_placement(qa_app_sim_q2_placement_input input) {
  const qa_monster_definition_reference *definition = &input.definition;
  const qa_q2_entity *entity = input.entity;
  if (differ(input.map.entities.provider, "q2:official") || differ(definition->source.provider, "q2:monsters/classic/baseq2")
      || differ(input.map.entities.content, definition->source.content) || differ(input.map.geometry_content, definition->source.content)
      || input.game->options.edition != QA_Q2_EDITION_CLASSIC || input.authored == NULL || input.native == NULL
      || differ(input.authored->classname, definition->classname) || differ(entity->classname, input.authored->classname)) return false;
  const qa_q2m_definition *native = input.native;
  bool walks = !native->has_locomotion || native->locomotion == QA_Q2M_LOCO_WALK;
  qa_vec3 gravity_down = {0, 0, -1};
  if (!walks || !same(entity->motion, "step") || (qa_js_to_int32(entity->flags) & 3) != 0 || differ(entity->model, native->model)
      || entity->scale != 1 || entity->gravity != 1 || !qa_app_sim_monster_same_vector(entity->gravity_vector, gravity_down)
      || (double)(qa_js_to_int32(qa_q2_integer_field(input.authored, "spawnflags", 0)) & ~0x1f00) != entity->spawnflags
      || (qa_js_to_int32(entity->spawnflags) & 2) != 0
      || !qa_app_sim_monster_same_vector(input.body.bounds.min, native->bounds.min)
      || !qa_app_sim_monster_same_vector(input.body.bounds.max, native->bounds.max)) return false;
  qa_vec3 start = qa_q2_add(qa_q2_vector_field(input.authored, "origin"), ((qa_vec3){0, 0, 1}));
  qa_q2_trace_request request;
  memset(&request, 0, sizeof request);
  request.start = start;
  request.end = qa_q2_add(start, ((qa_vec3){0, 0, -256}));
  request.bounds = &input.body.bounds;
  request.ignore = &entity->actor.id;
  request.mask = qa_q2m_monster_solid_mask(input.game);
  qa_q2_foundation_host *host = input.game->host;
  qa_trace_result floor = host->trace(host->ctx, request);
  return floor.fraction != 1 && !floor.all_solid && floor.hit.kind == QA_TRACE_HIT_WORLD
      && qa_app_sim_monster_same_vector(input.body.origin, floor.end);
}

static int cmp_offset(const void *a, const void *b) {
  const qa_app_sim_placement_offset *x = a, *y = b;
  double d = x->x * x->x + x->y * x->y - y->x * y->x - y->y * y->y;
  if (d != 0 && d == d) return d < 0 ? -1 : 1;
  d = x->y - y->y;
  if (d != 0 && d == d) return d < 0 ? -1 : 1;
  d = x->x - y->x;
  if (d != 0 && d == d) return d < 0 ? -1 : 1;
  return 0;
}
qa_app_sim_placement_offset *qa_app_sim_placement_offsets_within(double radius, size_t *out_count) {
  size_t cap = 16, n = 0;
  qa_app_sim_placement_offset *out = malloc(cap * sizeof *out);
  if (out == NULL) qa_range_error("Out of memory");
  /* JS loop bounds stay doubles: a NaN radius runs no iteration. */
  double limit = ceil(radius / 4);
  for (double x = -limit; x <= limit; x++) {
    for (double y = -limit; y <= limit; y++) {
      if ((x * x + y * y) * 16 <= radius * radius) {
        if (n == cap) {
          cap *= 2;
          qa_app_sim_placement_offset *next = realloc(out, cap * sizeof *out);
          if (next == NULL) qa_range_error("Out of memory");
          out = next;
        }
        out[n].x = x * 4; out[n].y = y * 4; n++;
      }
    }
  }
  qa_js_sort(out, n, sizeof *out, cmp_offset);
  *out_count = n;
  return out;
}

/* TS Set<string> of `${dx},${dy}` keys: String() maps -0 to "0" and every
 * NaN to "NaN", so keys compare as numbers with those two folds. */
typedef struct key_set { double *keys; size_t count, capacity; size_t *slots; size_t slot_count; } key_set;
static double fold(double v) { return v != v ? NAN : v == 0 ? 0 : v; }
static bool key_equal(double a, double b) { return (a != a && b != b) || a == b; }
static size_t key_hash(double x, double y) {
  uint64_t bx, by;
  x = fold(x); y = fold(y);
  memcpy(&bx, &x, sizeof bx); memcpy(&by, &y, sizeof by);
  uint64_t h = bx * 0x9E3779B97F4A7C15ull ^ (by + 0x632BE59BD9B4E019ull + (bx << 6) + (bx >> 2));
  return (size_t)(h ^ (h >> 31));
}
static bool key_has(const key_set *set, double x, double y) {
  if (set->slot_count == 0) return false;
  for (size_t i = key_hash(x, y) & (set->slot_count - 1);; i = (i + 1) & (set->slot_count - 1)) {
    size_t slot = set->slots[i];
    if (slot == 0) return false;
    if (key_equal(set->keys[(slot - 1) * 2], fold(x)) && key_equal(set->keys[(slot - 1) * 2 + 1], fold(y))) return true;
  }
}
static void key_add(key_set *set, double x, double y) {
  if (key_has(set, x, y)) return;
  if ((set->count + 1) * 2 > set->slot_count) {
    size_t slot_count = set->slot_count == 0 ? 64 : set->slot_count * 2;
    size_t *slots = calloc(slot_count, sizeof *slots);
    if (slots == NULL) qa_range_error("Out of memory");
    for (size_t k = 0; k < set->count; k++) {
      size_t i = key_hash(set->keys[k * 2], set->keys[k * 2 + 1]) & (slot_count - 1);
      while (slots[i] != 0) i = (i + 1) & (slot_count - 1);
      slots[i] = k + 1;
    }
    free(set->slots);
    set->slots = slots; set->slot_count = slot_count;
  }
  if (set->count == set->capacity) {
    size_t capacity = set->capacity == 0 ? 64 : set->capacity * 2;
    double *keys = realloc(set->keys, capacity * 2 * sizeof *keys);
    if (keys == NULL) qa_range_error("Out of memory");
    set->keys = keys; set->capacity = capacity;
  }
  set->keys[set->count * 2] = fold(x);
  set->keys[set->count * 2 + 1] = fold(y);
  size_t i = key_hash(x, y) & (set->slot_count - 1);
  while (set->slots[i] != 0) i = (i + 1) & (set->slot_count - 1);
  set->slots[i] = ++set->count;
}

static qa_trace_query query_with(const qa_app_sim_nearby_placement_input *input, qa_vec3 start, qa_vec3 end, const char *shape,
                                 qa_bounds bounds) {
  qa_trace_query query = input->query;
  query.start = start;
  query.end = end;
  query.shape.kind = shape;
  query.shape.bounds = bounds;
  return query;
}
static qa_trace_result route_trace(const qa_app_sim_nearby_placement_input *input, qa_trace_query value) {
  if (same(value.policy.kind, "q1")) value.policy.move = "no-monsters";
  qa_trace_result result = input->trace(input->ctx, value);
  if (result.fraction < 1 && result.hit.kind == QA_TRACE_HIT_ACTOR && input->blocked_by != NULL)
    input->blocked_by(input->ctx, result.hit.actor);
  return result;
}
static bool reachable(const qa_app_sim_nearby_placement_input *input, qa_vec3 source_origin, qa_vec3 end) {
  qa_bounds none = {{0, 0, 0}, {0, 0, 0}};
  qa_trace_result route = route_trace(input, query_with(input, source_origin, end, "box", input->authored.bounds));
  if (route.all_solid || route.fraction != 1) return false;
  if (!route.start_solid) return true;
  /* Authored hulls can overlap a wall. Require a clear center route and a full-hull exit. */
  qa_trace_result center = route_trace(input, query_with(input, source_origin, end, "point", none));
  qa_trace_result exit = route_trace(input, query_with(input, end, end, "box", input->authored.bounds));
  return !center.start_solid && !center.all_solid && center.fraction == 1 && !exit.start_solid && !exit.all_solid;
}
static qa_app_sim_nearby_placement placed(qa_vec3 origin, const qa_actor_id *ground) {
  qa_app_sim_nearby_placement out;
  memset(&out, 0, sizeof out);
  out.found = true;
  out.origin = origin;
  if (ground != NULL) { out.has_ground = true; out.ground = *ground; }
  return out;
}

qa_app_sim_nearby_placement qa_app_sim_nearby_monster_placement(const qa_app_sim_nearby_placement_input *input) {
  qa_app_sim_nearby_placement none;
  memset(&none, 0, sizeof none);
  const qa_body_state *body = &input->body;
  const qa_bounds authored_bounds = input->authored.bounds;
  bool authored_walks = same(input->authored.locomotion, "walk");
  qa_vec3 source_start = input->authored.origin;
  source_start.z = input->authored.origin.z + 1;
  qa_vec3 source_end = source_start;
  source_end.z = source_start.z - 256;
  qa_trace_result source_floor = route_trace(input, query_with(input, source_start, source_end, "box", authored_bounds));
  bool supported = !source_floor.start_solid && !source_floor.all_solid && source_floor.fraction < 1;
  qa_vec3 source_origin = supported && authored_walks ? source_floor.end : input->authored.origin;
  double feet = source_origin.z + authored_bounds.min.z;
  qa_vec3 anchor = {source_floor.end.x, source_floor.end.y, feet - body->bounds.min.z};
  double widths[4] = {body->bounds.max.x - body->bounds.min.x, body->bounds.max.y - body->bounds.min.y,
                      authored_bounds.max.x - authored_bounds.min.x, authored_bounds.max.y - authored_bounds.min.y};
  double radius = 2 * qa_js_max(qa_js_max(widths[0], widths[1]), qa_js_max(widths[2], widths[3]));
  size_t offset_count = 0;
  qa_app_sim_placement_offset *offsets = qa_app_sim_placement_offsets_within(radius, &offset_count);
  if (!same(input->locomotion, "walk")) {
    size_t vertical_count = 1, vertical_capacity = 16;
    double *vertical = malloc(vertical_capacity * sizeof *vertical);
    if (vertical == NULL) qa_range_error("Out of memory");
    vertical[0] = 0;
    for (double z = 4; z <= body->bounds.max.z - body->bounds.min.z; z += 4) {
      if (vertical_count + 2 > vertical_capacity) {
        vertical_capacity *= 2;
        double *next = realloc(vertical, vertical_capacity * sizeof *vertical);
        if (next == NULL) qa_range_error("Out of memory");
        vertical = next;
      }
      vertical[vertical_count++] = -z;
      vertical[vertical_count++] = z;
    }
    for (size_t o = 0; o < offset_count; o++) for (size_t v = 0; v < vertical_count; v++) {
      qa_vec3 origin = {body->origin.x + offsets[o].x, body->origin.y + offsets[o].y, body->origin.z + vertical[v]};
      if (!input->same_medium(input->ctx, origin)) continue;
      qa_trace_result fit = input->trace(input->ctx, query_with(input, origin, origin, "box", body->bounds));
      if (fit.start_solid || fit.all_solid) continue;
      qa_vec3 source_fit = origin;
      source_fit.z = origin.z + body->bounds.min.z - authored_bounds.min.z;
      if (reachable(input, source_origin, source_fit)) { free(vertical); free(offsets); return placed(origin, NULL); }
    }
    free(vertical);
  }
  if (same(input->locomotion, "walk")) {
    static const double lifts[2] = {1, 18};
    for (size_t o = 0; o < offset_count; o++) for (size_t l = 0; l < 2; l++) {
      qa_vec3 start = {anchor.x + offsets[o].x, anchor.y + offsets[o].y, anchor.z + lifts[l]};
      qa_vec3 end = start;
      end.z = anchor.z - (supported && authored_walks ? 18 : 256);
      qa_trace_result floor = input->trace(input->ctx, query_with(input, start, end, "box", body->bounds));
      if (floor.start_solid || floor.all_solid || floor.fraction == 1 || !floor.contact.has_plane || floor.contact.plane.normal.z < 0.7) continue;
      if (!input->same_medium(input->ctx, floor.end)) continue;
      qa_trace_result fit = input->trace(input->ctx, query_with(input, floor.end, floor.end, "box", body->bounds));
      if (fit.start_solid || fit.all_solid) continue;
      qa_vec3 source_fit = floor.end;
      source_fit.z = floor.end.z + body->bounds.min.z - authored_bounds.min.z;
      if (!reachable(input, source_origin, source_fit)) continue;
      free(offsets);
      return placed(floor.end, floor.hit.kind == QA_TRACE_HIT_ACTOR ? &floor.hit.actor : input->world_actor);
    }
  }
  free(offsets);
  if (!supported || !authored_walks) return none;
  /* Tight authored pockets can open around a corner: follow walkable source-hull edges. */
  size_t pending_count = 1, pending_capacity = 64;
  qa_vec3 *pending = malloc(pending_capacity * sizeof *pending);
  if (pending == NULL) qa_range_error("Out of memory");
  pending[0] = source_origin;
  key_set visited;
  memset(&visited, 0, sizeof visited);
  key_add(&visited, 0, 0);
  double reach = qa_js_max(512, radius);
  static const double directions[4][2] = {{8, 0}, {-8, 0}, {0, 8}, {0, -8}};
  static const double raises[2] = {0, 18};
  qa_app_sim_nearby_placement result = none;
  for (size_t index = 0; index < pending_count && index < 4096 && !result.found; index++) {
    qa_vec3 current = pending[index];
    for (size_t d = 0; d < 4 && !result.found; d++) {
      double x = current.x + directions[d][0], y = current.y + directions[d][1];
      double kx = x - source_origin.x, ky = y - source_origin.y;
      if (key_has(&visited, kx, ky) || qa_js_hypot2(x - source_origin.x, y - source_origin.y) > reach) continue;
      for (size_t l = 0; l < 2; l++) {
        qa_vec3 start = current;
        start.z = current.z + raises[l];
        qa_trace_result raised = route_trace(input, query_with(input, current, start, "box", authored_bounds));
        if (raised.start_solid || raised.all_solid || raised.fraction != 1) continue;
        qa_trace_result across = route_trace(input, query_with(input, start, (qa_vec3){x, y, start.z}, "box", authored_bounds));
        if (across.start_solid || across.all_solid || across.fraction != 1) continue;
        qa_trace_result floor = route_trace(input, query_with(input, across.end, (qa_vec3){x, y, current.z - 18}, "box", authored_bounds));
        if (floor.start_solid || floor.all_solid || floor.fraction == 1 || !floor.contact.has_plane || floor.contact.plane.normal.z < 0.7) continue;
        key_add(&visited, kx, ky);
        if (pending_count == pending_capacity) {
          pending_capacity *= 2;
          qa_vec3 *next = realloc(pending, pending_capacity * sizeof *pending);
          if (next == NULL) qa_range_error("Out of memory");
          pending = next;
        }
        pending[pending_count++] = floor.end;
        qa_vec3 origin = floor.end;
        origin.z = floor.end.z + authored_bounds.min.z - body->bounds.min.z;
        if (input->same_medium(input->ctx, origin)) {
          qa_trace_result fit = input->trace(input->ctx, query_with(input, origin, origin, "box", body->bounds));
          if (!fit.start_solid && !fit.all_solid) {
            const qa_actor_id *ground = !same(input->locomotion, "walk") ? NULL
                : floor.hit.kind == QA_TRACE_HIT_ACTOR ? &floor.hit.actor : input->world_actor;
            result = placed(origin, ground);
          }
        }
        break;
      }
    }
  }
  free(pending);
  free(visited.keys);
  free(visited.slots);
  return result;
}
