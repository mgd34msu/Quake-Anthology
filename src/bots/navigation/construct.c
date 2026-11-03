/* Port of src/bots/navigation/construct.ts. GPL-2.0-or-later. */
#include "qa/bots/navigation/construct.h"
#include "qa/bots/navigation/graph.h"
#include "qa/bots/navigation/helpers.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

static qa_query_target map_target(void) {
  qa_query_target t; memset(&t, 0, sizeof t); t.is_model = true; t.model = 0; return t;
}
static void at_check_d(double index, size_t count) {
  if (!isfinite(index) || index != trunc(index) || index < 0 || index >= (double)count)
    qa_range_error("Navigation record %s exceeds %zu", QA_JS_NUMBER(index), count); /* `${index}`: NaN, 1.5 */
}

typedef struct {
  const qa_navigation_world *world;
  qa_navigation_profile profile;
  bool has_crouched; qa_navigation_profile crouched;
  double spacing, link_distance; int64_t maximum_nodes;
  qa_navigation_node *nodes; size_t node_count, node_cap;
  qa_navigation_edge *edges; size_t edge_count, edge_cap;
  qa_navigation_rejected *rejected; size_t rejected_count, rejected_cap;
  /* seen keys */
  int64_t *seen; size_t seen_count, seen_cap; /* triples */
  /* grounded cache */
  struct GEntry { bool crouched; double x, y, z; bool has; qa_vec3 result; } *gcache;
  size_t g_count, g_cap;
  bool oom;
} Build;

static bool seen_has(Build *b, int64_t kx, int64_t ky, int64_t kz) {
  for (size_t i = 0; i < b->seen_count; i += 3)
    if (b->seen[i] == kx && b->seen[i + 1] == ky && b->seen[i + 2] == kz) return true;
  return false;
}
static void seen_add(Build *b, int64_t kx, int64_t ky, int64_t kz) {
  if (b->seen_count + 3 > b->seen_cap) {
    size_t c = b->seen_cap ? b->seen_cap * 2 : 192;
    int64_t *p = realloc(b->seen, c * sizeof *p);
    if (!p) { b->oom = true; return; }
    b->seen = p; b->seen_cap = c;
  }
  b->seen[b->seen_count++] = kx; b->seen[b->seen_count++] = ky; b->seen[b->seen_count++] = kz;
}
static bool grounded(Build *b, const qa_navigation_profile *profile, qa_vec3 point, qa_vec3 *out) {
  double height = -profile->shape.bounds.min.z;
  qa_vec3 end = point; end.z = point.z + height - profile->maximum_step - 4;
  qa_vec3 start = point; start.z = point.z + height + profile->maximum_step + 2;
  qa_trace_result result = qa_nav_trace(b->world, profile, start, end, map_target());
  if (result.start_solid) {
    start.z = point.z + height + 1;
    result = qa_nav_trace(b->world, profile, start, end, map_target());
  }
  if (result.start_solid || result.all_solid || result.fraction == 1 || !result.contact.has_plane ||
      result.contact.plane.normal.z < profile->minimum_floor_normal) return false;
  *out = result.end;
  return true;
}
static bool key_eq(double a, double b) {
  if (isnan(a) && isnan(b)) return true;
  if (a != b) return false;
  if (a == 0) return signbit(a) == signbit(b);
  return true;
}
static bool grounded_once(Build *b, const qa_navigation_profile *posture, bool crouched, qa_vec3 point, qa_vec3 *out) {
  for (size_t i = 0; i < b->g_count; i++) {
    struct GEntry *e = &b->gcache[i];
    if (e->crouched == crouched && key_eq(e->x, point.x) && key_eq(e->y, point.y) && key_eq(e->z, point.z)) {
      if (e->has) *out = e->result;
      return e->has;
    }
  }
  bool has = grounded(b, posture, point, out);
  if (b->g_count == b->g_cap) {
    size_t c = b->g_cap ? b->g_cap * 2 : 64;
    struct GEntry *p = realloc(b->gcache, c * sizeof *p);
    if (!p) { b->oom = true; return has; }
    b->gcache = p; b->g_cap = c;
  }
  b->gcache[b->g_count++] = (struct GEntry){crouched, point.x, point.y, point.z, has, has ? *out : point};
  return has;
}
static void insert_node(Build *b, qa_vec3 origin, qa_navigation_source source, const qa_navigation_profile *posture, bool crouched) {
  int64_t kx = (int64_t)qa_js_round(origin.x / 8), ky = (int64_t)qa_js_round(origin.y / 8), kz = (int64_t)qa_js_round(origin.z / 4);
  if (seen_has(b, kx, ky, kz) || !qa_nav_clear(b->world, posture, origin, origin, map_target())) return;
  int64_t medium = qa_nav_contents(b->world, &b->profile, origin, map_target());
  if ((medium & 6) != 0) return;
  if ((int64_t)b->node_count >= b->maximum_nodes)
    qa_range_error("Navigation construction exceeds %lld nodes; no partial graph was published", (long long)b->maximum_nodes);
  seen_add(b, kx, ky, kz);
  if (b->oom) return;
  if (b->node_count == b->node_cap) {
    size_t c = b->node_cap ? b->node_cap * 2 : 64;
    qa_navigation_node *p = realloc(b->nodes, c * sizeof *p);
    if (!p) { b->oom = true; return; }
    b->nodes = p; b->node_cap = c;
  }
  qa_navigation_node node; memset(&node, 0, sizeof node);
  node.id = (int64_t)b->node_count;
  node.origin = origin;
  node.bounds = qa_nav_translated(origin, posture->shape.bounds);
  node.radius = b->spacing / 2;
  node.contents = medium;
  node.flags = 0;
  node.presence = crouched ? 4 : 2;
  node.has_source_cluster = false;
  node.source = source;
  b->nodes[b->node_count++] = node;
}
static void publish_candidate(Build *b, qa_vec3 point, qa_navigation_source source) {
  qa_vec3 origin;
  if (grounded_once(b, &b->profile, false, point, &origin)) insert_node(b, origin, source, &b->profile, false);
  else if (b->has_crouched) {
    if (grounded_once(b, &b->crouched, true, point, &origin)) insert_node(b, origin, source, &b->crouched, true);
  }
}
static bool inside_polygon(qa_vec3 point, const qa_vec3 *polygon, size_t count) {
  double sign = 0; /* Math.sign: a NaN cross product makes every later test fail */
  for (size_t i = 0; i < count; i++) {
    qa_vec3 a = polygon[i], bb = polygon[(i + 1) % count];
    double cross = (bb.x - a.x) * (point.y - a.y) - (bb.y - a.y) * (point.x - a.x);
    if (fabs(cross) < 0.001) continue;
    double s = qa_js_sign(cross);
    if (sign != 0 && s != sign) return false;
    sign = s;
  }
  return true;
}
static void polygon_candidates(Build *b, const qa_vec3 *polygon, size_t count, qa_vec3 normal,
                               qa_navigation_source source) {
  if (count < 3 || normal.z <= 0) return;
  qa_vec3 center = {0, 0, 0};
  for (size_t i = 0; i < count; i++) {
    center.x += polygon[i].x / (double)count;
    center.y += polygon[i].y / (double)count;
    center.z += polygon[i].z / (double)count;
  }
  publish_candidate(b, center, source);
  for (size_t i = 0; i < count; i++) publish_candidate(b, qa_nav_midpoint(center, polygon[i]), source);
  qa_vec3 first = polygon[0];
  double plane = first.x * normal.x + first.y * normal.y + first.z * normal.z;
  double min_x = polygon[0].x, max_x = polygon[0].x, min_y = polygon[0].y, max_y = polygon[0].y;
  for (size_t i = 1; i < count; i++) {
    if (polygon[i].x < min_x) min_x = polygon[i].x;
    if (polygon[i].x > max_x) max_x = polygon[i].x;
    if (polygon[i].y < min_y) min_y = polygon[i].y;
    if (polygon[i].y > max_y) max_y = polygon[i].y;
  }
  for (double x = ceil(min_x / b->spacing) * b->spacing; x <= max_x; x += b->spacing)
    for (double y = ceil(min_y / b->spacing) * b->spacing; y <= max_y; y += b->spacing) {
      qa_vec3 point = {x, y, (plane - x * normal.x - y * normal.y) / normal.z};
      if (inside_polygon(point, polygon, count)) publish_candidate(b, point, source);
    }
}
static qa_vec3 triangle_normal(qa_vec3 a, qa_vec3 bb, qa_vec3 c) {
  double x = (bb.y - a.y) * (c.z - a.z) - (bb.z - a.z) * (c.y - a.y);
  double y = (bb.z - a.z) * (c.x - a.x) - (bb.x - a.x) * (c.z - a.z);
  double z = (bb.x - a.x) * (c.y - a.y) - (bb.y - a.y) * (c.x - a.x);
  double length = qa_js_hypot3(x, y, z);
  if (length == 0) return (qa_vec3){0, 0, 0};
  return (qa_vec3){x / length, y / length, z / length};
}
static void geometry_candidates_q12(Build *b, const qa_bsp_plane *planes, size_t plane_count,
                                    const qa_vec3 *vertices, size_t vertex_count,
                                    const qa_bsp_edge *edges, size_t edge_count,
                                    qa_double_view surface_edges,
                                    const qa_bsp_face *faces, size_t face_count,
                                    qa_index_range model_faces, double slope) {
  (void)face_count;
  for (double index = model_faces.first; index < model_faces.first + model_faces.count; index++) {
    at_check_d(index, face_count);
    const qa_bsp_face *face = &faces[(size_t)index];
    at_check_d(face->plane, plane_count);
    const qa_bsp_plane *plane = &planes[(size_t)face->plane];
    double sign = face->back ? -1 : 1;
    qa_vec3 normal = {plane->plane.normal.x * sign, plane->plane.normal.y * sign, plane->plane.normal.z * sign};
    if (normal.z < slope) continue;
    size_t poly_cap = 16, poly_count = 0;
    qa_vec3 *polygon = malloc(poly_cap * sizeof *polygon);
    if (!polygon) { b->oom = true; return; }
    for (double ei = face->edges.first; ei < face->edges.first + face->edges.count; ei++) {
      at_check_d(ei, surface_edges.count);
      double signed_edge = surface_edges.values[(size_t)ei];
      double abs_edge = fabs(signed_edge);
      at_check_d(abs_edge, edge_count);
      const qa_bsp_edge *edge = &edges[(size_t)abs_edge];
      double vertex_index = edge->vertices[signed_edge < 0 ? 1 : 0];
      at_check_d(vertex_index, vertex_count);
      if (poly_count == poly_cap) { poly_cap *= 2; polygon = realloc(polygon, poly_cap * sizeof *polygon); if (!polygon) { b->oom = true; return; } }
      polygon[poly_count++] = vertices[(size_t)vertex_index];
    }
    qa_navigation_source source = {QA_NS_CONSTRUCTED, (int64_t)index, false, 0};
    /* TS source for constructed is {surface, leaf}; primary=surface */
    polygon_candidates(b, polygon, poly_count, normal, source);
    free(polygon);
    if (b->oom) return;
  }
}
static void geometry_candidates_q3(Build *b, const qa_q3_world_geometry *world, double slope) {
  const qa_q3_bsp_model *model = &world->models[0];
  for (double index = model->surfaces.first; index < model->surfaces.first + model->surfaces.count; index++) {
    at_check_d(index, world->surface_count);
    const qa_q3_bsp_surface *surface = &world->surfaces[(size_t)index];
    qa_navigation_source source = {QA_NS_CONSTRUCTED, (int64_t)index, false, -1};
    if (surface->kind && !strcmp(surface->kind, "flare")) continue;
    if (surface->kind && !strcmp(surface->kind, "patch")) {
      for (int64_t row = 0; row + 1 < surface->height; row++)
        for (int64_t column = 0; column + 1 < surface->width; column++) {
          double first = surface->vertices.first + (double)row * surface->width + (double)column;
          double idxs[4] = {first, first + 1, first + surface->width + 1, first + surface->width};
          qa_vec3 polygon[4];
          for (int k = 0; k < 4; k++) { at_check_d(idxs[k], world->vertex_count); polygon[k] = world->vertices[(size_t)idxs[k]].position; }
          qa_vec3 normal = triangle_normal(polygon[0], polygon[1], polygon[2]);
          qa_vec3 authored = world->vertices[(size_t)first].normal;
          if (normal.x * authored.x + normal.y * authored.y + normal.z * authored.z < 0)
            normal = (qa_vec3){-normal.x, -normal.y, -normal.z};
          if (normal.z >= slope) polygon_candidates(b, polygon, 4, normal, source);
          if (b->oom) return;
        }
    } else {
      for (double offset = surface->indices.first; offset + 2 < surface->indices.first + surface->indices.count; offset += 3) {
        qa_vec3 polygon[3];
        double first_index = 0;
        for (int k = 0; k < 3; k++) {
          at_check_d(offset + k, world->indices.count);
          double vi = surface->vertices.first + world->indices.values[(size_t)(offset + k)];
          at_check_d(vi, world->vertex_count);
          polygon[k] = world->vertices[(size_t)vi].position;
          if (k == 0) first_index = vi;
        }
        qa_vec3 normal = triangle_normal(polygon[0], polygon[1], polygon[2]);
        qa_vec3 authored = world->vertices[(size_t)first_index].normal;
        if (normal.x * authored.x + normal.y * authored.y + normal.z * authored.z < 0)
          normal = (qa_vec3){-normal.x, -normal.y, -normal.z};
        if (normal.z >= slope) polygon_candidates(b, polygon, 3, normal, source);
        if (b->oom) return;
      }
    }
  }
}
typedef struct { int64_t a, bb; bool crouched; bool value; } PairEntry;
typedef struct { PairEntry *v; size_t n, cap; } PairCache;
static bool pair_get(PairCache *c, int64_t a, int64_t bb, bool crouched, bool *out) {
  int64_t lo = a < bb ? a : bb, hi = a < bb ? bb : a;
  for (size_t i = 0; i < c->n; i++) if (c->v[i].a == lo && c->v[i].bb == hi && c->v[i].crouched == crouched) { *out = c->v[i].value; return true; }
  return false;
}
static void pair_set(PairCache *c, int64_t a, int64_t bb, bool crouched, bool value, bool *oom) {
  if (c->n == c->cap) { size_t nc = c->cap ? c->cap * 2 : 64; PairEntry *p = realloc(c->v, nc * sizeof *p); if (!p) { *oom = true; return; } c->v = p; c->cap = nc; }
  int64_t lo = a < bb ? a : bb, hi = a < bb ? bb : a;
  c->v[c->n++] = (PairEntry){lo, hi, crouched, value};
}
static void push_edge(Build *b, qa_navigation_edge edge) {
  if (b->edge_count == b->edge_cap) {
    size_t c = b->edge_cap ? b->edge_cap * 2 : 64;
    qa_navigation_edge *p = realloc(b->edges, c * sizeof *p);
    if (!p) { b->oom = true; return; }
    b->edges = p; b->edge_cap = c;
  }
  b->edges[b->edge_count++] = edge;
}
static void push_rejected(Build *b, qa_navigation_source source, const char *reason) {
  if (b->rejected_count == b->rejected_cap) {
    size_t c = b->rejected_cap ? b->rejected_cap * 2 : 8;
    qa_navigation_rejected *p = realloc(b->rejected, c * sizeof *p);
    if (!p) { b->oom = true; return; }
    b->rejected = p; b->rejected_cap = c;
  }
  char *copy = malloc(strlen(reason) + 1);
  if (!copy) { b->oom = true; return; }
  strcpy(copy, reason);
  b->rejected[b->rejected_count++] = (qa_navigation_rejected){source, copy};
}
bool qa_construct_navigation(qa_navigation_graph *out, qa_navigation_construction options) {
  memset(out, 0, sizeof *out);
  qa_nav_validate_profile(&options.profile);
  if (options.map.format != options.geometry.kind)
    qa_range_error("Navigation map identity and decoded geometry disagree");
  double spacing = options.has_spacing ? options.spacing : 48;
  double link_distance = options.has_link_distance ? options.link_distance : spacing * 2.1;
  int64_t maximum_nodes = options.has_maximum_nodes ? options.maximum_nodes : 100000;
  if (!isfinite(spacing) || spacing < 8 || !isfinite(link_distance) || link_distance < spacing || maximum_nodes < 1)
    qa_range_error("Invalid navigation construction limits");
  Build b; memset(&b, 0, sizeof b);
  b.world = options.world; b.profile = options.profile;
  b.spacing = spacing; b.link_distance = link_distance; b.maximum_nodes = maximum_nodes;
  b.has_crouched = qa_nav_crouched_profile(&options.profile, &b.crouched);
  const qa_decoded_world *geometry = &options.geometry;
  /* geometry candidates */
  if (geometry->kind != QA_DECODED_WORLD_Q3) {
    if (geometry->kind == QA_DECODED_WORLD_Q1) {
      const qa_q1_world_geometry *w = geometry->as.q1;
      if (!w || w->model_count == 0) qa_range_error("Navigation construction requires a world model");
      geometry_candidates_q12(&b, w->planes, w->plane_count, w->vertices, w->vertex_count, w->edges, w->edge_count,
                              w->surface_edges, w->faces, w->face_count, w->models[0].faces, options.profile.minimum_floor_normal);
    } else {
      const qa_q2_world_geometry *w = geometry->as.q2;
      if (!w || w->model_count == 0) qa_range_error("Navigation construction requires a world model");
      geometry_candidates_q12(&b, w->planes, w->plane_count, w->vertices, w->vertex_count, w->edges, w->edge_count,
                              w->surface_edges, w->faces, w->face_count, w->models[0].faces, options.profile.minimum_floor_normal);
    }
  } else {
    const qa_q3_world_geometry *w = geometry->as.q3;
    if (!w || w->model_count == 0) qa_range_error("Navigation construction requires a world model");
    geometry_candidates_q3(&b, w, options.profile.minimum_floor_normal);
  }
  /* leaf centers in water/ladder media */
  size_t leaf_count = geometry->kind == QA_DECODED_WORLD_Q1 ? geometry->as.q1->leaf_count
                      : geometry->kind == QA_DECODED_WORLD_Q2 ? geometry->as.q2->leaf_count
                                                              : geometry->as.q3->leaf_count;
  for (size_t i = 0; i < leaf_count && !b.oom; i++) {
    qa_bounds bounds = geometry->kind == QA_DECODED_WORLD_Q1 ? geometry->as.q1->leaves[i].bounds
                       : geometry->kind == QA_DECODED_WORLD_Q2 ? geometry->as.q2->leaves[i].bounds
                                                               : geometry->as.q3->leaves[i].bounds;
    qa_vec3 center = qa_nav_midpoint(bounds.min, bounds.max);
    int64_t medium = qa_nav_contents(b.world, &b.profile, center, map_target());
    if ((medium & 9) != 0 && (medium & 6) == 0) {
      qa_navigation_source source = {QA_NS_CONSTRUCTED, -1, true, (int64_t)i};
      insert_node(&b, center, source, &b.profile, false);
    }
  }
  /* bins keyed by cell */
  typedef struct { int64_t cx, cy; int64_t *ids; size_t n, cap; } Bin;
  Bin *bins = NULL; size_t bin_count = 0, bin_cap = 0;
  for (size_t i = 0; i < b.node_count && !b.oom; i++) {
    qa_vec3 o = b.nodes[i].origin;
    int64_t cx = (int64_t)floor(o.x / link_distance), cy = (int64_t)floor(o.y / link_distance);
    Bin *bin = NULL;
    for (size_t k = 0; k < bin_count; k++) if (bins[k].cx == cx && bins[k].cy == cy) bin = &bins[k];
    if (!bin) {
      if (bin_count == bin_cap) { bin_cap = bin_cap ? bin_cap * 2 : 32; bins = realloc(bins, bin_cap * sizeof *bins); if (!bins) { b.oom = true; break; } }
      bins[bin_count] = (Bin){cx, cy, NULL, 0, 0}; bin = &bins[bin_count++];
    }
    if (bin->n == bin->cap) { bin->cap = bin->cap ? bin->cap * 2 : 8; bin->ids = realloc(bin->ids, bin->cap * sizeof *bin->ids); if (!bin->ids) { b.oom = true; break; } }
    bin->ids[bin->n++] = b.nodes[i].id;
  }
  PairCache pair_clear = {0}, pair_grounded = {0};
  for (size_t ni = 0; ni < b.node_count && !b.oom; ni++) {
    qa_navigation_node node = b.nodes[ni];
    for (int dx = -1; dx <= 1 && !b.oom; dx++) for (int dy = -1; dy <= 1 && !b.oom; dy++) {
      int64_t cx = (int64_t)floor(node.origin.x / link_distance) + dx;
      int64_t cy = (int64_t)floor(node.origin.y / link_distance) + dy;
      Bin *bin = NULL;
      for (size_t k = 0; k < bin_count; k++) if (bins[k].cx == cx && bins[k].cy == cy) bin = &bins[k];
      if (!bin) continue;
      for (size_t k = 0; k < bin->n && !b.oom; k++) {
        int64_t other_id = bin->ids[k];
        if (node.id == other_id) continue;
        const qa_navigation_node *other = b.nodes ? &b.nodes[other_id] : NULL;
        /* node ids are insertion indexes in constructed graphs */
        double rise = other->origin.z - node.origin.z;
        if (hypot(node.origin.x - other->origin.x, node.origin.y - other->origin.y) > link_distance ||
            fabs(rise) > qa_js_max(b.profile.maximum_drop, link_distance)) continue;
        qa_travel_mode mode = ((node.contents & 1) != 0 && (other->contents & 1) != 0) ? QA_TM_SWIM
                            : ((node.contents & 8) != 0 && (other->contents & 8) != 0) ? QA_TM_LADDER
                            : rise > b.profile.maximum_step ? QA_TM_JUMP
                            : rise < -b.profile.maximum_step ? QA_TM_DROP
                            : (node.presence == 4 || other->presence == 4) ? QA_TM_CROUCH : QA_TM_WALK;
        if (!(b.profile.capabilities & (1u << mode)) || (mode == QA_TM_DROP && -rise > b.profile.maximum_drop)) continue;
        if (mode == QA_TM_WALK || mode == QA_TM_CROUCH) {
          bool crouched = mode == QA_TM_CROUCH;
          const qa_navigation_profile *posture = crouched && b.has_crouched ? &b.crouched : &b.profile;
          bool posture_is_crouched = posture == &b.crouched;
          bool passable;
          if (!pair_get(&pair_clear, node.id, other_id, posture_is_crouched, &passable)) {
            double up = b.profile.maximum_step + 1;
            qa_vec3 a2 = node.origin; a2.z += up;
            qa_vec3 b2 = other->origin; b2.z += up;
            passable = qa_nav_clear(b.world, posture, node.origin, other->origin, map_target()) ||
                       qa_nav_clear(b.world, posture, a2, b2, map_target());
            pair_set(&pair_clear, node.id, other_id, posture_is_crouched, passable, &b.oom);
            if (b.oom) break;
          }
          if (!passable) continue;
          bool supported;
          if (!pair_get(&pair_grounded, node.id, other_id, posture_is_crouched, &supported)) {
            qa_vec3 middle = qa_nav_midpoint(node.origin, other->origin);
            qa_vec3 foot = middle; foot.z += b.profile.shape.bounds.min.z;
            qa_vec3 g;
            supported = grounded_once(&b, posture, posture_is_crouched, foot, &g);
            pair_set(&pair_grounded, node.id, other_id, posture_is_crouched, supported, &b.oom);
            if (b.oom) break;
          }
          if (!supported) continue;
        } else if (mode == QA_TM_SWIM) {
          bool passable;
          if (!pair_get(&pair_clear, node.id, other_id, false, &passable)) {
            passable = qa_nav_clear(b.world, &b.profile, node.origin, other->origin, map_target());
            pair_set(&pair_clear, node.id, other_id, false, passable, &b.oom);
            if (b.oom) break;
          }
          if (!passable) continue;
        }
        qa_navigation_edge edge; memset(&edge, 0, sizeof edge);
        edge.id = (int64_t)b.edge_count;
        edge.from = node.id; edge.to = other->id; edge.mode = mode;
        edge.start = node.origin; edge.end = other->origin;
        double ts = qa_nav_distance(node.origin, other->origin) / 320;
        edge.travel_seconds = ts > 0.01 ? ts : 0.01;
        edge.source = node.source;
        push_edge(&b, edge);
      }
    }
  }
  free(pair_clear.v); free(pair_grounded.v);
  for (size_t i = 0; i < bin_count; i++) free(bins[i].ids);
  free(bins);
  /* connections */
  for (size_t i = 0; i < options.connection_count && !b.oom; i++) {
    const qa_navigation_connection *connection = &options.connections[i];
    const qa_navigation_node *from = NULL, *to = NULL;
    double from_best = link_distance * 2, to_best = link_distance * 2;
    for (size_t k = 0; k < b.node_count; k++) {
      double d = qa_nav_distance(b.nodes[k].origin, connection->request.from);
      if (d < from_best) { from = &b.nodes[k]; from_best = d; }
      d = qa_nav_distance(b.nodes[k].origin, connection->request.to);
      if (d < to_best) { to = &b.nodes[k]; to_best = d; }
    }
    if (!from || !to) {
      char buf[192];
      snprintf(buf, sizeof buf, "Source connection %lld has no nearby navigation %s",
               (long long)connection->id, !from ? "start" : "destination");
      qa_navigation_source source = {QA_NS_CONSTRUCTED, -1, false, -1};
      push_rejected(&b, source, buf);
      continue;
    }
    qa_navigation_edge edge; memset(&edge, 0, sizeof edge);
    edge.id = (int64_t)b.edge_count;
    edge.from = from->id; edge.to = to->id; edge.mode = connection->request.mode;
    edge.start = connection->request.from; edge.end = connection->request.to;
    edge.travel_seconds = connection->travel_seconds;
    edge.source_travel_type = connection->source_travel_type;
    if (connection->request.has_hint) { edge.has_hint = true; edge.hint = connection->request.hint; }
    else if (connection->request.mode == QA_TM_MOVER) {
      edge.has_hint = true;
      edge.hint = (qa_traversal_hint){from->origin, connection->request.from, connection->request.to, false, {0, 0, 0}};
    }
    edge.has_entity = connection->request.has_entity;
    edge.entity = connection->request.entity;
    edge.source = (qa_navigation_source){QA_NS_CONSTRUCTED, -1, false, -1};
    push_edge(&b, edge);
  }
  size_t cluster_count = 0;
  qa_navigation_cluster_list *clusters = NULL;
  if (!b.oom) {
    clusters = qa_navigation_clusters(b.nodes, b.node_count, b.edges, b.edge_count, &cluster_count);
    if (!clusters && b.node_count > 0) b.oom = true;
  }
  free(b.seen); free(b.gcache);
  if (b.oom) {
    free(b.nodes); free(b.edges);
    for (size_t i = 0; i < b.rejected_count; i++) free((void *)b.rejected[i].reason);
    free(b.rejected);
    qa_navigation_clusters_destroy(clusters, cluster_count);
    return false;
  }
  out->map = options.map; out->profile = options.profile;
  out->aas_asset = NULL; out->kex_asset = NULL;
  out->nodes = b.nodes; out->node_count = b.node_count;
  out->edges = b.edges; out->edge_count = b.edge_count;
  out->clusters = clusters; out->cluster_count = cluster_count;
  out->rejected = b.rejected; out->rejected_count = b.rejected_count;
  return true;
}
