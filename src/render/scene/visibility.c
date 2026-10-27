/* Port of src/render/scene/visibility.ts. GPL-2.0-or-later. */
#include "qa/render/scene/visibility.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/formats/q1_map/queries.h"
#include "qa/foundation/support.h"
#include "qa/materials/dlight.h"
#include "qa/render/scene/view.h"
static float fr(double v) { return (float)v; }
static void *xalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("Allocation failed"); return p; }
static void *xg(void *p, size_t n) { void *q = realloc(p, n ? n : 1); if (!q) qa_range_error("Allocation failed"); return q; }
/* unified views */
typedef struct view {
  int kind; /* 0 q1 1 q2 2 q3 */
  const qa_bsp_node *nodes; size_t node_count;
  const qa_bsp_plane *planes; size_t plane_count;
  size_t leaf_count;
  const void *map_key;
} view_t;
static view_t view_of(qa_decoded_world m) {
  view_t v; memset(&v, 0, sizeof v);
  if (m.kind == QA_DECODED_WORLD_Q1) { v.kind = 0; v.nodes = m.as.q1->nodes; v.node_count = m.as.q1->node_count; v.planes = m.as.q1->planes; v.plane_count = m.as.q1->plane_count; v.leaf_count = m.as.q1->leaf_count; v.map_key = m.as.q1; }
  else if (m.kind == QA_DECODED_WORLD_Q2) { v.kind = 1; v.nodes = m.as.q2->nodes; v.node_count = m.as.q2->node_count; v.planes = m.as.q2->planes; v.plane_count = m.as.q2->plane_count; v.leaf_count = m.as.q2->leaf_count; v.map_key = m.as.q2; }
  else { v.kind = 2; v.nodes = m.as.q3->nodes; v.node_count = m.as.q3->node_count; v.planes = m.as.q3->planes; v.plane_count = m.as.q3->plane_count; v.leaf_count = m.as.q3->leaf_count; v.map_key = m.as.q3; }
  return v;
}
static const qa_bsp_node *node_at(view_t v, double index) {
  long i = (long)index;
  if (index != (double)i || i < 0 || (size_t)i >= v.node_count) qa_range_error("World visibility index %g outside %zu", index, v.node_count);
  return &v.nodes[i];
}
static qa_plane plane_at(view_t v, double index) {
  long i = (long)index;
  if (index != (double)i || i < 0 || (size_t)i >= v.plane_count) qa_range_error("World visibility index %g outside %zu", index, v.plane_count);
  return v.planes[i].plane;
}
double qa_world_point_leaf(qa_decoded_world map, qa_vec3 point) {
  view_t v = view_of(map);
  if (v.node_count == 0) return v.leaf_count == 0 ? -1 : 0;
  qa_bsp_child child = { true, 0 };
  while (child.is_node) {
    const qa_bsp_node *node = node_at(v, child.index);
    qa_plane plane = plane_at(v, node->plane);
    child = node->children[qa_dot3(point, plane.normal) > plane.distance ? 0 : 1];
  }
  return child.index;
}
void qa_world_visibility_cache_init(qa_world_visibility_cache *c) { memset(c, 0, sizeof *c); }
void qa_world_visibility_cache_destroy(qa_world_visibility_cache *c) {
  if (!c) return;
  if (c->stamps_init) qa_stamp_set_destroy(&c->stamps);
  for (size_t i = 0; i < c->q1_count; i++) free(c->q1_entries[i].bytes);
  for (size_t i = 0; i < c->q2_count; i++) free(c->q2_entries[i].bytes);
  free(c->q1_entries); free(c->q2_entries); memset(c, 0, sizeof *c);
}
typedef struct pentry { double key; uint8_t *bytes; size_t length; } pentry;
static const uint8_t *cache_get(pentry *e, size_t n, double key, size_t *len) {
  for (size_t i = 0; i < n; i++) if (e[i].key == key) { *len = e[i].length; return e[i].bytes; }
  return NULL;
}
static void cache_put(struct qa_scene_pvs_entry **e, size_t *n, size_t *cap, double key, uint8_t *bytes, size_t len) {
  if (*n == *cap) { *cap = *cap ? *cap * 2 : 8; *e = xg(*e, *cap * sizeof **e); }
  (*e)[*n].key = key; (*e)[*n].bytes = bytes; (*e)[*n].length = len; (*n)++;
}
static double leaf_cluster(qa_decoded_world m, view_t v, double index) {
  long i = (long)index;
  if (index != (double)i || i < 0 || (size_t)i >= v.leaf_count) qa_range_error("World visibility index %g outside %zu", index, v.leaf_count);
  return v.kind == 1 ? m.as.q2->leaves[i].cluster : m.as.q3->leaves[i].cluster;
}
static double leaf_contents_q2(qa_decoded_world m, double index) { return m.as.q2->leaves[(long)index].contents; }
static uint8_t *q2_pvs(qa_decoded_world map, double cluster, qa_world_visibility_cache *cache, int use_cache, size_t *out_len, int *present) {
  const qa_q2_visibility *vis = map.as.q2->visibility;
  *present = 1;
  if (!vis || cluster < 0) { *present = 0; return NULL; }
  if (use_cache) {
    const uint8_t *hit = cache_get((pentry *)cache->q2_entries, cache->q2_count, cluster, out_len);
    if (hit) return (uint8_t *)hit;
  }
  long ci = (long)cluster;
  if (cluster != (double)ci || ci < 0 || (size_t)ci >= vis->cluster_count) qa_range_error("World visibility index %g outside %zu", cluster, vis->cluster_count);
  qa_q2_visibility_cluster entry = vis->clusters[ci];
  const uint8_t *bytes = vis->compressed.bytes; size_t blen = vis->compressed.length;
  size_t rlen = (size_t)ceil((double)vis->cluster_count / 8);
  uint8_t *result = xalloc(rlen); memset(result, 0, rlen);
  long readp = (long)entry.pvs_offset;
  if (readp < 0) { free(result); *present = 0; return NULL; }
  size_t writep = 0;
  while (writep < rlen) {
    if ((size_t)readp >= blen) qa_range_error("Truncated Q2 PVS");
    uint8_t value = bytes[readp++];
    if (value != 0) result[writep++] = value;
    else {
      if ((size_t)readp >= blen) qa_range_error("Invalid Q2 PVS run");
      uint8_t count = bytes[readp++];
      if (count == 0 || writep + count > rlen) qa_range_error("Invalid Q2 PVS run");
      writep += count;
    }
  }
  if (out_len) *out_len = rlen;
  if (use_cache) cache_put(&cache->q2_entries, &cache->q2_count, &cache->q2_capacity, cluster, result, rlen);
  return result;
}
static int remaining_frustum(qa_bounds bounds, const qa_plane *planes, size_t n, int bits, int *out) {
  for (size_t i = 0; i < n; i++) {
    int bit = 1 << (int)i; qa_plane plane = planes[i];
    if (!(bits & bit)) continue;
    qa_vec3 front = { plane.normal.x < 0 ? bounds.min.x : bounds.max.x, plane.normal.y < 0 ? bounds.min.y : bounds.max.y, plane.normal.z < 0 ? bounds.min.z : bounds.max.z };
    if (fr(qa_dot3(front, plane.normal) - plane.distance) < 0) return 0;
    qa_vec3 back = { plane.normal.x < 0 ? bounds.max.x : bounds.min.x, plane.normal.y < 0 ? bounds.max.y : bounds.min.y, plane.normal.z < 0 ? bounds.max.z : bounds.min.z };
    if (fr(qa_dot3(back, plane.normal) - plane.distance) >= 0) bits &= ~bit;
  }
  *out = bits; return 1;
}
void qa_visible_world_destroy(qa_visible_world *v) {
  if (!v) return;
  free(v->leaves); free(v->surfaces); free(v->mask_surfaces); free(v->mask_values); memset(v, 0, sizeof *v);
}
bool qa_visible_world_new(qa_decoded_world map, qa_scene_camera camera, const qa_world_visibility_options *options_in, qa_world_visibility_cache *cache, qa_visible_world *out) {
  memset(out, 0, sizeof *out);
  qa_world_visibility_options options; memset(&options, 0, sizeof options);
  if (options_in) options = *options_in;
  view_t v = view_of(map);
  qa_vec3 origin = options.has_pvs_origin ? options.pvs_origin : camera.origin;
  double eye = qa_world_point_leaf(map, origin);
  out->leaf = eye;
  if (eye < 0) return true;
  int use_cache = cache && cache->map_key == v.map_key;
  if (cache && !use_cache) {
    /* one cache per map: reinitialize for a new map */
    qa_world_visibility_cache_destroy(cache); qa_world_visibility_cache_init(cache); cache->map_key = v.map_key; use_cache = 1;
  }
  const uint8_t *pvs = NULL; size_t pvs_len = 0; uint8_t *pvs_owned = NULL;
  double from_cluster = -1;
  if (!options.no_vis) {
    if (v.kind == 0) {
      if (use_cache) {
        const uint8_t *hit = cache_get((pentry *)cache->q1_entries, cache->q1_count, eye, &pvs_len);
        if (hit) pvs = hit;
        else { uint8_t *r = qa_q1_leaf_pvs(map.as.q1, eye, &pvs_len); cache_put(&cache->q1_entries, &cache->q1_count, &cache->q1_capacity, eye, r, pvs_len); pvs = r; }
      } else { pvs_owned = qa_q1_leaf_pvs(map.as.q1, eye, &pvs_len); pvs = pvs_owned; }
    } else if (v.kind == 1) {
      from_cluster = leaf_cluster(map, v, eye);
      int present = 0; uint8_t *r = q2_pvs(map, from_cluster, cache, use_cache, &pvs_len, &present);
      if (present) { if (use_cache) pvs = r; else { pvs_owned = r; pvs = r; } }
      /* secondary */
      int has_second = 0; double second = 0;
      if (options.secondary == 2) { has_second = 1; second = options.secondary_cluster; }
      else if (options.secondary == 0) {
        double probe = qa_world_point_leaf(map, (qa_vec3){ origin.x, origin.y, origin.z + (leaf_contents_q2(map, eye) == 0 ? -16 : 16) });
        double oc = leaf_contents_q2(map, probe), ocl = leaf_cluster(map, v, probe);
        /* TS (contents & 1) with JS int coercion */
        int32_t oc32 = (int32_t)(isfinite(oc) ? fmod(trunc(oc), 4294967296.0) : 0);
        if ((oc32 & 1) == 0 && ocl != from_cluster) { has_second = 1; second = ocl; }
      }
      if (has_second && second >= 0 && pvs) {
        size_t olen = 0; int opresent = 0; uint8_t *other = q2_pvs(map, second, cache, use_cache, &olen, &opresent);
        if (!opresent) pvs = NULL;
        else {
          uint8_t *merged = xalloc(pvs_len); memcpy(merged, pvs, pvs_len);
          for (size_t i = 0; i < pvs_len; i++) merged[i] = (uint8_t)(merged[i] | (i < olen ? other[i] : 0));
          free(pvs_owned); pvs_owned = merged; pvs = merged;
        }
      }
    } else {
      from_cluster = leaf_cluster(map, v, eye);
      const qa_q3_visibility *vis = map.as.q3->visibility;
      if (vis && from_cluster >= 0 && from_cluster < vis->cluster_count) {
        size_t off = (size_t)from_cluster * (size_t)vis->bytes_per_cluster;
        size_t avail = vis->bits.length > off ? vis->bits.length - off : 0;
        pvs_len = avail < (size_t)vis->bytes_per_cluster ? avail : (size_t)vis->bytes_per_cluster;
        pvs = vis->bits.bytes + off;
      }
    }
  }
  qa_plane planes[5]; size_t nplanes = 0;
  if (!options.no_cull) {
    nplanes = qa_scene_camera_frustum(camera, planes);
    if (v.kind == 2 && nplanes > 4) nplanes = 4;
  }
  /* stamps */
  size_t surf_total = v.kind == 2 ? map.as.q3->surface_count : (v.kind == 0 ? map.as.q1->face_count : map.as.q2->face_count);
  qa_stamp_set *stamps; qa_stamp_set local_stamps;
  if (use_cache) {
    if (!cache->stamps_init) { if (!qa_stamp_set_init(&cache->stamps, surf_total)) { free(pvs_owned); return false; } cache->stamps_init = true; }
    stamps = &cache->stamps;
  } else { if (!qa_stamp_set_init(&local_stamps, surf_total)) { free(pvs_owned); return false; } stamps = &local_stamps; }
  qa_stamp_set_begin(stamps);
  const qa_q3_dynamic_light *lights = options.q3_lights; size_t nlights = options.q3_light_count;
  if (nlights > 32) qa_range_error("Q3 world lighting supports the source 32-light mask");
  int32_t initial_mask = nlights == 32 ? -1 : (int32_t)((1u << nlights) - 1);
  /* stacks */
  double *stack_child = xalloc(64 * sizeof(double)); int *stack_mask = xalloc(64 * sizeof(int)); int *stack_planes = xalloc(64 * sizeof(int));
  size_t sp = 0, scap = 64;
  stack_child[sp] = v.node_count == 0 ? -1 : 0; stack_mask[sp] = (int)initial_mask; stack_planes[sp] = (1 << (int)nplanes) - 1; sp++;
  size_t cleaf = 0, csurf = 0;
  while (sp) {
    sp--; double encoded = stack_child[sp]; int mask = stack_mask[sp], item_planes = stack_planes[sp];
    if (encoded >= 0) {
      const qa_bsp_node *node = node_at(v, encoded);
      int remaining = 0, ok;
      if (v.kind == 2) ok = remaining_frustum(node->bounds, planes, nplanes, item_planes, &remaining);
      else { ok = qa_scene_bounds_in_frustum(node->bounds, planes, nplanes); remaining = item_planes; }
      if (!ok) continue;
      qa_plane plane = plane_at(v, node->plane);
      double c0 = node->children[0].is_node ? node->children[0].index : -1 - node->children[0].index;
      double c1 = node->children[1].is_node ? node->children[1].index : -1 - node->children[1].index;
      if (sp + 2 > scap) { scap *= 2; stack_child = xg(stack_child, scap * sizeof(double)); stack_mask = xg(stack_mask, scap * sizeof(int)); stack_planes = xg(stack_planes, scap * sizeof(int)); }
      if (v.kind == 2) {
        qa_dlight_mask_pair masks = qa_split_dlight_mask(lights, nlights, (int32_t)mask, plane);
        stack_child[sp] = c1; stack_mask[sp] = masks.back; stack_planes[sp] = remaining; sp++;
        stack_child[sp] = c0; stack_mask[sp] = masks.front; stack_planes[sp] = remaining; sp++;
      } else {
        int front = qa_dot3(camera.origin, plane.normal) >= plane.distance ? 0 : 1;
        stack_child[sp] = front == 0 ? c1 : c0; stack_mask[sp] = mask; stack_planes[sp] = remaining; sp++;
        stack_child[sp] = front == 0 ? c0 : c1; stack_mask[sp] = mask; stack_planes[sp] = remaining; sp++;
      }
      continue;
    }
    double leaf_index = -1 - encoded;
    /* leafVisible */
    int visible_flag;
    if (v.kind == 0) {
      visible_flag = leaf_index != 0 && (!pvs || ((((size_t)((long)leaf_index - 1) >> 3) < pvs_len ? pvs[((size_t)((long)leaf_index - 1)) >> 3] : 0) & (1 << (((long)leaf_index - 1) & 7))) != 0);
    } else {
      long li = (long)leaf_index;
      if (leaf_index != (double)li || li < 0 || (size_t)li >= v.leaf_count) qa_range_error("World visibility index %g outside %zu", leaf_index, v.leaf_count);
      double area = v.kind == 1 ? map.as.q2->leaves[li].area : map.as.q3->leaves[li].area;
      if (options.has_visible_areas) {
        int found = 0; for (size_t i = 0; i < options.visible_area_count; i++) if (options.visible_areas[i] == area) found = 1;
        if (!found) continue;
      }
      double cluster = leaf_cluster(map, v, leaf_index);
      if (v.kind == 2 && from_cluster < 0) visible_flag = cluster != -1;
      else visible_flag = !pvs || (cluster >= 0 && ((((size_t)((long)cluster) >> 3) < pvs_len ? pvs[((size_t)((long)cluster)) >> 3] : 0) & (1 << (((long)cluster) & 7))) != 0);
    }
    if (!visible_flag) continue;
    long li = (long)leaf_index;
    qa_bounds lbounds; qa_index_range range; qa_double_view members;
    if (v.kind == 0) { const qa_q1_leaf *l = &map.as.q1->leaves[li]; lbounds = l->bounds; range = l->faces; members = map.as.q1->leaf_faces; }
    else if (v.kind == 1) { const qa_q2_leaf *l = &map.as.q2->leaves[li]; lbounds = l->bounds; range = l->faces; members = map.as.q2->leaf_faces; }
    else { const qa_q3_bsp_leaf *l = &map.as.q3->leaves[li]; lbounds = l->bounds; range = l->surfaces; members = map.as.q3->leaf_surfaces; }
    int rem = 0;
    if (v.kind == 2 ? !remaining_frustum(lbounds, planes, nplanes, item_planes, &rem) : !qa_scene_bounds_in_frustum(lbounds, planes, nplanes)) continue;
    if (out->leaf_count == cleaf) { cleaf = cleaf ? cleaf * 2 : 16; out->leaves = xg(out->leaves, cleaf * sizeof(double)); }
    out->leaves[out->leaf_count++] = leaf_index;
    for (long i = 0; i < (long)range.count; i++) {
      long mi = (long)(range.first + i);
      if (range.first + i != (double)mi || mi < 0 || (size_t)mi >= members.count) qa_range_error("World visibility index %g outside %zu", range.first + i, members.count);
      double surface = members.values[mi];
      long si = (long)surface;
      /* qa_stamp_set_mark: true first time this epoch (TS seen.mark). */
      bool first = (surface == (double)si && si >= 0 && (size_t)si < surf_total)
        ? qa_stamp_set_mark(stamps, (size_t)si) : false;
      if (first) {
        if (out->surface_count == csurf) { csurf = csurf ? csurf * 2 : 16; out->surfaces = xg(out->surfaces, csurf * sizeof(double)); out->mask_surfaces = xg(out->mask_surfaces, csurf * sizeof(double)); out->mask_values = xg(out->mask_values, csurf * sizeof(int)); }
        out->surfaces[out->surface_count] = surface;
        out->mask_surfaces[out->mask_count] = surface; out->mask_values[out->mask_count] = mask; out->mask_count++;
        out->surface_count++;
      }
    }
  }
  free(stack_child); free(stack_mask); free(stack_planes); free(pvs_owned);
  if (!use_cache) qa_stamp_set_destroy(&local_stamps);
  return true;
}
