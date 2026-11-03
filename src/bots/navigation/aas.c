/* See header. GPL-2.0-or-later. */
#include "qa/bots/navigation/aas.h"
#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"
#include "qa/core/math.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static float fr(double v) { return (float)v; }
void qa_aas_asset_destroy(qa_aas_asset *a) {
  if (!a)
    return;
  free(a->source);
  free(a->lumps);
  free(a->bboxes);
  free(a->vertices);
  free(a->planes);
  free(a->edges);
  free(a->edge_indexes);
  free(a->faces);
  free(a->face_indexes);
  free(a->areas);
  free(a->settings);
  free(a->reachability);
  free(a->nodes);
  free(a->portals);
  free(a->portal_indexes);
  free(a->clusters);
  memset(a, 0, sizeof *a);
}
static qa_vec3 vec(qa_binary_reader *r) {
  double x = qa_binary_reader_finite_f32(r);
  double y = qa_binary_reader_finite_f32(r);
  double z = qa_binary_reader_finite_f32(r);
  qa_vec3 v = {x, y, z};
  return v;
}
static qa_bounds bnd(qa_binary_reader *r) {
  qa_bounds b;
  b.min = vec(r);
  b.max = vec(r);
  return b;
}
static void idx(int64_t v, int64_t count, const char *n) {
  if (v < 0 || v >= count)
    qa_range_error("AAS %s index %lld exceeds %lld", n, (long long)v, (long long)count);
}
static void rng(int64_t first, int64_t count, int64_t len, const char *n) {
  if (first < 0 || count < 0 || first > len - count)
    qa_range_error("AAS %s range %lld+%lld exceeds %lld", n, (long long)first, (long long)count,
                   (long long)len);
}
/* Generic lump reader: returns count, fills reader positioned at lump. */
static bool lump_reader(qa_binary_reader *r, qa_aas_asset *a, int lump, int stride,
                        qa_binary_reader *out, size_t *count) {
  qa_aas_lump span = a->lumps[lump];
  *count = 0;
  if (span.length == 0)
    return true;
  if (!qa_binary_reader_records(r, span.offset, span.length, stride, out))
    return false;
  *count = (size_t)(span.length / stride);
  return true;
}
#define ALLOC(p, n)                                                                                \
  do {                                                                                             \
    if ((n) > 0) {                                                                                 \
      (p) = malloc((n) * sizeof *(p));                                                             \
      if (!(p)) {                                                                                  \
        qa_aas_asset_destroy(out);                                                                 \
        return false;                                                                              \
      }                                                                                            \
    }                                                                                              \
  } while (0)
bool qa_parse_aas(qa_aas_asset *out, uint8_t *bytes, size_t length, const char *source,
                  bool has_expected, int64_t expected) {
  memset(out, 0, sizeof *out);
  const char *src = source ? source : "<aas>";
  qa_binary_reader r;
  if (!qa_binary_reader_init(&r, bytes, length, src))
    return false;
  if (qa_binary_reader_u32(&r) != 0x53414145u)
    qa_binary_error(src, 0, "expected EAAS magic");
  int64_t version = qa_binary_reader_i32(&r);
  out->version = version;
  if (version != 4 && version != 5)
    qa_binary_error(src, 4, "unsupported AAS version %lld", (long long)version);
  uint8_t *decoded = qa_binary_reader_bytes(&r, 116);
  if (!decoded && length >= 124) {
    qa_binary_reader_destroy(&r);
    return false;
  }
  if (version == 5)
    for (int o = 0; o < 116; o++)
      decoded[o] = (uint8_t)(decoded[o] ^ (uint8_t)(o * 119 & 255));
  char hsrc[1024];
  snprintf(hsrc, sizeof hsrc, "%s:header", src);
  qa_binary_reader h;
  if (!qa_binary_reader_init(&h, decoded, 116, hsrc)) {
    free(decoded);
    qa_binary_reader_destroy(&r);
    return false;
  }
  out->bsp_checksum = qa_binary_reader_i32(&h);
  if (has_expected && out->bsp_checksum != (int32_t)expected)
    qa_binary_error(src, 8, "AAS belongs to a different BSP checksum");
  out->source = malloc(strlen(src) + 1);
  if (!out->source)
    return false;
  strcpy(out->source, src);
  out->lump_count = 14;
  ALLOC(out->lumps, 14);
  for (int i = 0; i < 14; i++) {
    int64_t off = qa_binary_reader_i32(&h), len = qa_binary_reader_i32(&h);
    if (len < 0 || (len > 0 && (off < 124 || off > (int64_t)length - len)))
      qa_binary_error(src, 12 + i * 8, "invalid AAS lump range");
    out->lumps[i] = (qa_aas_lump){off, len};
  }
  qa_binary_reader d;
  size_t n = 0;
  if (!lump_reader(&r, out, 0, 32, &d, &n))
    return false;
  out->bbox_count = n;
  ALLOC(out->bboxes, n);
  for (size_t i = 0; i < n; i++) {
    out->bboxes[i].presence = qa_binary_reader_i32(&d);
    out->bboxes[i].flags = qa_binary_reader_i32(&d);
    out->bboxes[i].bounds = bnd(&d);
  }
  if (n)
    qa_binary_reader_destroy(&d);
  if (!lump_reader(&r, out, 1, 12, &d, &n))
    return false;
  out->vertex_count = n;
  ALLOC(out->vertices, n);
  for (size_t i = 0; i < n; i++)
    out->vertices[i] = vec(&d);
  if (n)
    qa_binary_reader_destroy(&d);
  if (!lump_reader(&r, out, 2, 20, &d, &n))
    return false;
  out->plane_count = n;
  ALLOC(out->planes, n);
  for (size_t i = 0; i < n; i++) {
    out->planes[i].normal = vec(&d);
    out->planes[i].distance = qa_binary_reader_finite_f32(&d);
    out->planes[i].type = qa_binary_reader_i32(&d);
  }
  if (n)
    qa_binary_reader_destroy(&d);
  if (!lump_reader(&r, out, 3, 8, &d, &n))
    return false;
  out->edge_count = n;
  ALLOC(out->edges, n);
  for (size_t i = 0; i < n; i++) {
    out->edges[i].vertices[0] = qa_binary_reader_i32(&d);
    out->edges[i].vertices[1] = qa_binary_reader_i32(&d);
  }
  if (n)
    qa_binary_reader_destroy(&d);
  if (!lump_reader(&r, out, 4, 4, &d, &n))
    return false;
  out->edge_index_count = n;
  ALLOC(out->edge_indexes, n);
  for (size_t i = 0; i < n; i++)
    out->edge_indexes[i] = qa_binary_reader_i32(&d);
  if (n)
    qa_binary_reader_destroy(&d);
  if (!lump_reader(&r, out, 5, 24, &d, &n))
    return false;
  out->face_count = n;
  ALLOC(out->faces, n);
  for (size_t i = 0; i < n; i++) {
    qa_aas_face_rec *f = &out->faces[i];
    f->plane = qa_binary_reader_i32(&d);
    f->flags = qa_binary_reader_i32(&d);
    f->edge_count = qa_binary_reader_i32(&d);
    f->first_edge = qa_binary_reader_i32(&d);
    f->front_area = qa_binary_reader_i32(&d);
    f->back_area = qa_binary_reader_i32(&d);
  }
  if (n)
    qa_binary_reader_destroy(&d);
  if (!lump_reader(&r, out, 6, 4, &d, &n))
    return false;
  out->face_index_count = n;
  ALLOC(out->face_indexes, n);
  for (size_t i = 0; i < n; i++)
    out->face_indexes[i] = qa_binary_reader_i32(&d);
  if (n)
    qa_binary_reader_destroy(&d);
  if (!lump_reader(&r, out, 7, 48, &d, &n))
    return false;
  out->area_count = n;
  ALLOC(out->areas, n);
  for (size_t i = 0; i < n; i++) {
    qa_aas_area_rec *a = &out->areas[i];
    a->number = qa_binary_reader_i32(&d);
    a->face_count = qa_binary_reader_i32(&d);
    a->first_face = qa_binary_reader_i32(&d);
    a->bounds = bnd(&d);
    a->center = vec(&d);
  }
  if (n)
    qa_binary_reader_destroy(&d);
  if (!lump_reader(&r, out, 8, 28, &d, &n))
    return false;
  out->setting_count = n;
  ALLOC(out->settings, n);
  for (size_t i = 0; i < n; i++) {
    qa_aas_setting_rec *s = &out->settings[i];
    s->contents = qa_binary_reader_i32(&d);
    s->flags = qa_binary_reader_i32(&d);
    s->presence = qa_binary_reader_i32(&d);
    s->cluster = qa_binary_reader_i32(&d);
    s->cluster_area = qa_binary_reader_i32(&d);
    s->reach_count = qa_binary_reader_i32(&d);
    s->first_reach = qa_binary_reader_i32(&d);
  }
  if (n)
    qa_binary_reader_destroy(&d);
  if (!lump_reader(&r, out, 9, 44, &d, &n))
    return false;
  out->reachability_count = n;
  ALLOC(out->reachability, n);
  for (size_t i = 0; i < n; i++) {
    qa_aas_reach_rec *x = &out->reachability[i];
    x->area = qa_binary_reader_i32(&d);
    x->face = qa_binary_reader_i32(&d);
    x->edge = qa_binary_reader_i32(&d);
    x->start = vec(&d);
    x->end = vec(&d);
    x->travel_type = qa_binary_reader_i32(&d);
    x->travel_time = qa_binary_reader_u16(&d);
    x->padding = qa_binary_reader_u16(&d);
  }
  if (n)
    qa_binary_reader_destroy(&d);
  if (!lump_reader(&r, out, 10, 12, &d, &n))
    return false;
  out->node_count = n;
  ALLOC(out->nodes, n);
  for (size_t i = 0; i < n; i++) {
    out->nodes[i].plane = qa_binary_reader_i32(&d);
    out->nodes[i].children[0] = qa_binary_reader_i32(&d);
    out->nodes[i].children[1] = qa_binary_reader_i32(&d);
  }
  if (n)
    qa_binary_reader_destroy(&d);
  if (!lump_reader(&r, out, 11, 20, &d, &n))
    return false;
  out->portal_count = n;
  ALLOC(out->portals, n);
  for (size_t i = 0; i < n; i++) {
    qa_aas_portal_rec *p = &out->portals[i];
    p->area = qa_binary_reader_i32(&d);
    p->front_cluster = qa_binary_reader_i32(&d);
    p->back_cluster = qa_binary_reader_i32(&d);
    p->cluster_areas[0] = qa_binary_reader_i32(&d);
    p->cluster_areas[1] = qa_binary_reader_i32(&d);
  }
  if (n)
    qa_binary_reader_destroy(&d);
  if (!lump_reader(&r, out, 12, 4, &d, &n))
    return false;
  out->portal_index_count = n;
  ALLOC(out->portal_indexes, n);
  for (size_t i = 0; i < n; i++)
    out->portal_indexes[i] = qa_binary_reader_i32(&d);
  if (n)
    qa_binary_reader_destroy(&d);
  if (!lump_reader(&r, out, 13, 16, &d, &n))
    return false;
  out->cluster_count = n;
  ALLOC(out->clusters, n);
  for (size_t i = 0; i < n; i++) {
    qa_aas_cluster_rec *c = &out->clusters[i];
    c->area_count = qa_binary_reader_i32(&d);
    c->reachability_area_count = qa_binary_reader_i32(&d);
    c->portal_count = qa_binary_reader_i32(&d);
    c->first_portal = qa_binary_reader_i32(&d);
  }
  if (n)
    qa_binary_reader_destroy(&d);
  qa_binary_reader_destroy(&h);
  free(decoded);
  qa_binary_reader_destroy(&r);
  if (out->area_count != out->setting_count)
    qa_binary_error(src, 0, "AAS area/settings counts differ");
  for (size_t i = 0; i < out->edge_count; i++) {
    if (i == 0 && out->edges[i].vertices[0] == 0 && out->edges[i].vertices[1] == 0)
      continue;
    idx(out->edges[i].vertices[0], (int64_t)out->vertex_count, "vertex");
    idx(out->edges[i].vertices[1], (int64_t)out->vertex_count, "vertex");
  }
  for (size_t i = 0; i < out->edge_index_count; i++)
    idx(out->edge_indexes[i] < 0 ? -out->edge_indexes[i] : out->edge_indexes[i],
        (int64_t)out->edge_count, "edge");
  for (size_t i = 0; i < out->face_count; i++) {
    qa_aas_face_rec *f = &out->faces[i];
    idx(f->plane, (int64_t)out->plane_count, "face plane");
    rng(f->first_edge, f->edge_count, (int64_t)out->edge_index_count, "face edges");
    idx(f->front_area, (int64_t)out->area_count, "front area");
    idx(f->back_area, (int64_t)out->area_count, "back area");
  }
  for (size_t i = 0; i < out->face_index_count; i++)
    idx(out->face_indexes[i] < 0 ? -out->face_indexes[i] : out->face_indexes[i],
        (int64_t)out->face_count, "face");
  for (size_t i = 0; i < out->area_count; i++) {
    if ((int64_t)i != out->areas[i].number)
      qa_binary_error(src, 0, "AAS area ordinal differs from stored number");
    rng(out->areas[i].first_face, out->areas[i].face_count, (int64_t)out->face_index_count,
        "area faces");
  }
  for (size_t i = 0; i < out->setting_count; i++)
    rng(out->settings[i].first_reach, out->settings[i].reach_count,
        (int64_t)out->reachability_count, "reachability");
  for (size_t i = 0; i < out->reachability_count; i++)
    idx(out->reachability[i].area, (int64_t)out->area_count, "reachable area");
  if (out->node_count < 2)
    qa_binary_error(src, 0, "AAS has no BSP root node");
  /* A tree gives each node one parent and never re-enters the root; a DAG would make area queries exponential. */
  uint8_t *marks = calloc(out->node_count > out->reachability_count ? out->node_count : out->reachability_count + 1, 1);
  if (marks == NULL)
    return false;
  for (size_t i = 1; i < out->node_count; i++) {
    idx(out->nodes[i].plane, (int64_t)out->plane_count, "node plane");
    for (int k = 0; k < 2; k++) {
      int64_t c = out->nodes[i].children[k];
      idx(c < 0 ? -c : c, c > 0 ? (int64_t)out->node_count : (int64_t)out->area_count,
          "node child");
      if (c <= 0)
        continue;
      if (c == 1 || marks[c] == 1) {
        free(marks);
        qa_binary_error(src, 0, "AAS BSP node has more than one parent");
      }
      marks[c] = 1;
    }
  }
  memset(marks, 0, out->node_count > out->reachability_count ? out->node_count : out->reachability_count + 1);
  for (size_t i = 0; i < out->setting_count; i++)
    for (int64_t reach = out->settings[i].first_reach;
         reach < out->settings[i].first_reach + out->settings[i].reach_count; reach++) {
      if (marks[reach] == 1) {
        free(marks);
        qa_binary_error(src, 0, "AAS area reachability ranges overlap");
      }
      marks[reach] = 1;
    }
  free(marks);
  for (size_t i = 1; i < out->setting_count; i++) {
    const qa_aas_setting_rec *setting = &out->settings[i];
    if (setting->cluster > 0) {
      idx(setting->cluster, (int64_t)out->cluster_count, "area cluster");
      if (setting->cluster_area < 0)
        qa_binary_error(src, 0, "AAS cluster area is negative");
    } else if (setting->cluster < 0)
      idx(-setting->cluster, (int64_t)out->portal_count, "area portal");
  }
  for (size_t i = 0; i < out->portal_count; i++) {
    if (out->portals[i].cluster_areas[0] < 0 || out->portals[i].cluster_areas[1] < 0)
      qa_binary_error(src, 0, "AAS portal cluster area is negative");
    idx(out->portals[i].area, (int64_t)out->area_count, "portal area");
    idx(out->portals[i].front_cluster, (int64_t)out->cluster_count, "front cluster");
    idx(out->portals[i].back_cluster, (int64_t)out->cluster_count, "back cluster");
  }
  for (size_t i = 0; i < out->portal_index_count; i++)
    idx(out->portal_indexes[i], (int64_t)out->portal_count, "portal");
  for (size_t i = 0; i < out->cluster_count; i++)
    rng(out->clusters[i].first_portal, out->clusters[i].portal_count,
        (int64_t)out->portal_index_count, "cluster portals");
  return true;
}
static double plane_dist(const qa_aas_plane_rec *p, qa_vec3 pt) {
  return fr(fr(fr(fr(pt.x * p->normal.x) + fr(pt.y * p->normal.y)) + fr(pt.z * p->normal.z)) -
            (float)p->distance);
}
int64_t qa_aas_point_area(const qa_aas_asset *a, qa_vec3 pt) {
  int64_t num = 1;
  for (int64_t v = 0; num > 0; v++) {
    if (v >= (int64_t)a->node_count)
      qa_range_error("Cyclic AAS BSP tree");
    if (num >= (int64_t)a->node_count)
      qa_range_error("Missing AAS BSP node/plane");
    const qa_aas_node_rec *nd = &a->nodes[num];
    if (nd->plane < 0 || nd->plane >= (int64_t)a->plane_count)
      qa_range_error("Missing AAS BSP node/plane");
    num = plane_dist(&a->planes[nd->plane], pt) > 0 ? nd->children[0] : nd->children[1];
  }
  return -num;
}
typedef struct {
  int64_t node;
  qa_vec3 start, end;
  int64_t depth;
} Frame;
qa_aas_area_crossing *qa_aas_trace_areas(const qa_aas_asset *a, qa_vec3 st, qa_vec3 en,
                                         int64_t maximum, size_t *out_count) {
  if (maximum < 0)
    qa_range_error("Invalid AAS area limit");
  *out_count = 0;
  if (maximum == 0)
    return NULL;
  qa_aas_area_crossing *outp = malloc((size_t)maximum * sizeof *outp);
  size_t cap = 64, sp = 0;
  Frame *stack = malloc(cap * sizeof *stack);
  if (!outp || !stack) {
    free(outp);
    free(stack);
    return NULL;
  }
  stack[sp++] = (Frame){1, st, en, 0};
  while (sp > 0 && *out_count < (size_t)maximum) {
    Frame f = stack[--sp];
    if (f.node <= 0) {
      if (f.node < 0)
        outp[(*out_count)++] = (qa_aas_area_crossing){-f.node, f.start};
      continue;
    }
    if (f.depth >= (int64_t)a->node_count)
      qa_range_error("Cyclic AAS BSP tree");
    if (f.node >= (int64_t)a->node_count)
      qa_range_error("Missing AAS BSP node/plane");
    const qa_aas_node_rec *nd = &a->nodes[f.node];
    if (nd->plane < 0 || nd->plane >= (int64_t)a->plane_count)
      qa_range_error("Missing AAS BSP node/plane");
    const qa_aas_plane_rec *pl = &a->planes[nd->plane];
    double front = plane_dist(pl, f.start), back = plane_dist(pl, f.end);
    int64_t depth = f.depth + 1;
    if (sp + 2 > cap) {
      cap *= 2;
      stack = realloc(stack, cap * sizeof *stack);
      if (!stack) {
        free(outp);
        return NULL;
      }
    }
    if (front > 0 && back > 0) {
      stack[sp++] = (Frame){nd->children[0], f.start, f.end, depth};
      continue;
    }
    if (front <= 0 && back <= 0) {
      stack[sp++] = (Frame){nd->children[1], f.start, f.end, depth};
      continue;
    }
    double frac = fr(front / fr(front - back));
    if (frac < 0)
      frac = 0;
    if (frac > 1)
      frac = 1;
    qa_vec3 mid = {fr(f.start.x + fr(fr(f.end.x - f.start.x) * (float)frac)),
                   fr(f.start.y + fr(fr(f.end.y - f.start.y) * (float)frac)),
                   fr(f.start.z + fr(fr(f.end.z - f.start.z) * (float)frac))};
    int64_t near = front < 0 ? nd->children[1] : nd->children[0],
            far = front < 0 ? nd->children[0] : nd->children[1];
    stack[sp++] = (Frame){far, mid, f.end, depth};
    stack[sp++] = (Frame){near, f.start, mid, depth};
  }
  free(stack);
  return outp;
}
int64_t *qa_aas_bbox_areas(const qa_aas_asset *a, qa_bounds b, int64_t maximum, size_t *out_count) {
  if (maximum < 0)
    qa_range_error("Invalid AAS area limit");
  *out_count = 0;
  size_t cap = 64, found_cap = 64;
  int64_t *found = malloc(found_cap * sizeof *found);
  Frame *stack = malloc(cap * sizeof *stack);
  if (!found || !stack) {
    free(found);
    free(stack);
    return NULL;
  }
  size_t sp = 0;
  stack[sp++] = (Frame){1, {0, 0, 0}, {0, 0, 0}, 0};
  while (sp > 0) {
    Frame f = stack[--sp];
    if (f.node <= 0) {
      if (f.node < 0) {
        bool dup = false;
        for (size_t i = 0; i < *out_count; i++)
          if (found[i] == -f.node)
            dup = true;
        if (!dup) {
          if (*out_count == found_cap) {
            found_cap *= 2;
            found = realloc(found, found_cap * sizeof *found);
          }
          found[(*out_count)++] = -f.node;
        }
      }
      continue;
    }
    if (f.depth >= (int64_t)a->node_count)
      qa_range_error("Cyclic AAS BSP tree");
    if (f.node >= (int64_t)a->node_count)
      qa_range_error("Missing AAS BSP node/plane");
    const qa_aas_node_rec *nd = &a->nodes[f.node];
    if (nd->plane < 0 || nd->plane >= (int64_t)a->plane_count)
      qa_range_error("Missing AAS BSP node/plane");
    const qa_aas_plane_rec *pl = &a->planes[nd->plane];
    qa_vec3 front = {pl->normal.x < 0 ? b.min.x : b.max.x, pl->normal.y < 0 ? b.min.y : b.max.y,
                     pl->normal.z < 0 ? b.min.z : b.max.z};
    qa_vec3 back = {pl->normal.x < 0 ? b.max.x : b.min.x, pl->normal.y < 0 ? b.max.y : b.min.y,
                    pl->normal.z < 0 ? b.max.z : b.min.z};
    if (sp + 2 > cap) {
      cap *= 2;
      stack = realloc(stack, cap * sizeof *stack);
    }
    if (plane_dist(pl, front) >= 0)
      stack[sp++] = (Frame){nd->children[0], {0, 0, 0}, {0, 0, 0}, f.depth + 1};
    if (plane_dist(pl, back) < 0)
      stack[sp++] = (Frame){nd->children[1], {0, 0, 0}, {0, 0, 0}, f.depth + 1};
  }
  free(stack); /* TS: [...found].reverse().slice(0, maximum) */
  for (size_t i = 0, j = *out_count ? *out_count - 1 : 0; i < j; i++, j--) {
    int64_t t = found[i];
    found[i] = found[j];
    found[j] = t;
  }
  if (*out_count > (size_t)maximum)
    *out_count = (size_t)maximum;
  return found;
}

/* ---- aasTraceClientBBox ---- */
static const qa_aas_setting_rec *trace_setting(const qa_aas_asset *a, int64_t index) {
  if (index < 0 || index >= (int64_t)a->setting_count) qa_range_error("Missing AAS trace record %lld", (long long)index);
  return &a->settings[index];
}
static const qa_aas_plane_rec *trace_plane(const qa_aas_asset *a, int64_t index) {
  if (index < 0 || index >= (int64_t)a->plane_count) qa_range_error("Missing AAS trace record %lld", (long long)index);
  return &a->planes[index];
}
static const qa_aas_node_rec *trace_node(const qa_aas_asset *a, int64_t index) {
  if (index < 0 || index >= (int64_t)a->node_count) qa_range_error("Missing AAS trace record %lld", (long long)index);
  return &a->nodes[index];
}
static bool trace_same(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static qa_vec3 trace_advance(qa_vec3 start, double amount, qa_vec3 direction) {
  return qa_add3(start, qa_scale3(direction, amount));
}
typedef struct trace_piece { qa_vec3 start, end; int64_t node, plane; } trace_piece;
qa_aas_trace qa_aas_trace_client_bbox(const qa_aas_asset *asset, qa_vec3 start, qa_vec3 end, int64_t presence,
                                      void (*print)(void *ctx, const char *text), void *print_ctx) {
  if (!isfinite(start.x) || !isfinite(start.y) || !isfinite(start.z) || !isfinite(end.x) || !isfinite(end.y) ||
      !isfinite(end.z))
    qa_range_error("Non-finite AAS trace point");
  /* TS stops pushing at 127 entries, so 128 slots always suffice. */
  trace_piece stack[128];
  size_t count = 0;
  stack[count++] = (trace_piece){start, end, 1, 0};
  int64_t last_area = 0;
  while (count > 0) {
    trace_piece piece = stack[--count];
    if (piece.node <= 0) {
      if (piece.node == 0 || (trace_setting(asset, -piece.node)->presence & presence) == 0) {
        bool start_solid = trace_same(piece.start, start);
        qa_vec3 direction = start_solid ? (qa_vec3){0, 0, 0} : qa_normalize3(qa_sub3(end, start));
        double fraction = start_solid ? 0 : fr(qa_length3(qa_sub3(piece.start, start)) / qa_length3(qa_sub3(end, start)));
        qa_vec3 hit_end = start_solid ? piece.start : trace_advance(piece.start, -0.125, direction);
        int64_t plane = qa_dot3(direction, trace_plane(asset, piece.plane)->normal) > 0 ? piece.plane ^ 1 : piece.plane;
        return (qa_aas_trace){start_solid, fraction, hit_end, 0, last_area, piece.node == 0 ? 0 : -piece.node, plane};
      }
      last_area = -piece.node;
      continue;
    }
    const qa_aas_node_rec *node = trace_node(asset, piece.node);
    const qa_aas_plane_rec *plane = trace_plane(asset, node->plane);
    float front = fr(qa_dot3(piece.start, plane->normal) - plane->distance);
    float back = fr(qa_dot3(piece.end, plane->normal) - plane->distance);
    trace_piece pushes[2];
    size_t push_count = 0;
    if (front >= 0 && back >= 0) {
      pushes[push_count++] = (trace_piece){piece.start, piece.end, node->children[0], piece.plane};
    } else if (front < 0 && back < 0) {
      pushes[push_count++] = (trace_piece){piece.start, piece.end, node->children[1], piece.plane};
    } else {
      if (front == back) front = fr((double)front - (double)fr(0.001));
      float fraction = fr((front < 0 ? (double)front + 0.125 : (double)front - 0.125) / (double)fr((double)front - (double)back));
      if (fraction < 0) fraction = fr(0.001);
      else if (fraction > 1) fraction = fr(0.999);
      qa_vec3 middle = trace_advance(piece.start, fraction, qa_sub3(piece.end, piece.start));
      int side = front < 0 ? 1 : 0;
      pushes[push_count++] = (trace_piece){middle, piece.end, node->children[side == 0 ? 1 : 0], node->plane};
      pushes[push_count++] = (trace_piece){piece.start, middle, node->children[side], piece.plane};
    }
    for (size_t i = 0; i < push_count; i++) {
      stack[count++] = pushes[i];
      if (count >= 127) {
        if (print != NULL) print(print_ctx, "AAS_TraceBoundingBox: stack overflow\n");
        return (qa_aas_trace){false, 0, {0, 0, 0}, 0, last_area, 0, 0};
      }
    }
  }
  return (qa_aas_trace){false, 1, end, 0, last_area, 0, 0};
}
