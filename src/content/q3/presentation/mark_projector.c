/* Port of src/content/q3/presentation/mark-projector.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/mark_projector.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/core/renderer_math.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
enum { MAX_CLIP = 64, MAX_SURF = 64 };
static float f32(double v) { return (float)v; }
static const void *at_idx(const void *base, size_t stride, size_t count, double index) {
  if (index < 0 || index != (double)(size_t)index || (size_t)index >= count)
    qa_range_error("mark geometry index %s outside %zu", QA_JS_NUMBER(index), count);
  return (const char *)base + stride * (size_t)index;
}
#define AT(arr, count, idx) (*(const __typeof__(*(arr)) *)at_idx((arr), sizeof *(arr), (count), (idx)))
static qa_vec3 finite_vec(qa_vec3 v) {
  if (!isfinite(f32(v.x)) || !isfinite(f32(v.y)) || !isfinite(f32(v.z)))
    qa_range_error("mark coordinates must be finite float32 values");
  return v;
}
static void capacity(double v) {
  if (v != trunc(v) || v < 0 || v > 1000000) qa_range_error("mark buffer capacity must be an integer in [0, 1000000]");
}
static int box_side(qa_bounds b, qa_plane p) {
  if (p.normal.x == 1) { if (p.distance <= b.min.x) return 1; if (p.distance >= b.max.x) return 2; return 3; }
  if (p.normal.y == 1) { if (p.distance <= b.min.y) return 1; if (p.distance >= b.max.y) return 2; return 3; }
  if (p.normal.z == 1) { if (p.distance <= b.min.z) return 1; if (p.distance >= b.max.z) return 2; return 3; }
  return qa_box_on_plane_side(b, p);
}
/* R_ChopPolyBehindPlane. out capacity MAX_CLIP. Returns count. */
static size_t chop(const qa_vec3 *pts, size_t n, qa_plane plane, qa_vec3 *out) {
  if (n >= MAX_CLIP - 2) return 0;
  double dist[MAX_CLIP]; int side[MAX_CLIP]; bool has0 = false, has1 = false;
  for (size_t i = 0; i < n; i++) {
    dist[i] = f32(qa_dot3(pts[i], plane.normal) - plane.distance);
    side[i] = dist[i] > 0.5 ? 0 : dist[i] < -0.5 ? 1 : 2;
    if (side[i] == 0) has0 = true;
    if (side[i] == 1) has1 = true;
  }
  if (!has0) return 0;
  if (!has1) { memcpy(out, pts, n * sizeof *out); return n; }
  size_t m = 0;
  for (size_t i = 0; i < n; i++) {
    size_t next = (i + 1) % n;
    if (side[i] == 2) { out[m++] = pts[i]; continue; }
    if (side[i] == 0) out[m++] = pts[i];
    if (side[next] == 2 || side[next] == side[i]) continue;
    double diff = f32(dist[i] - dist[next]);
    double frac = diff == 0 ? 0 : f32(dist[i] / diff);
    out[m++] = qa_add3(pts[i], qa_scale3(qa_sub3(pts[next], pts[i]), frac));
  }
  return m;
}
void qa_q3_pres_mark_fragments_destroy(qa_q3_pres_mark_fragments *fr) {
  if (!fr) return;
  free(fr->points); free(fr->fragments); memset(fr, 0, sizeof *fr);
}
void qa_q3_pres_bsp_mark_projector_init(qa_q3_pres_bsp_mark_projector *p, qa_q3_pres_mark_geometry g) {
  if ((double)g.surfaces_count != g.surface_count) qa_range_error("mark geometry must retain every BSP surface index");
  p->geometry = g;
}
typedef struct { qa_q3_pres_mark_fragments out; size_t pcap, fcap; bool oom; const qa_q3_pres_mark_projection *q; } collect_t;
static qa_vec3 coll_point(void *ctx, double i) { collect_t *c = ctx; return AT(c->q->points, c->q->point_count, i); }
static qa_vec3 coll_proj(void *ctx) { return ((collect_t *)ctx)->q->projection; }
static bool grow(void **arr, size_t *cap, size_t need, size_t sz) {
  if (*cap >= need) return true;
  size_t nc = *cap ? *cap * 2 : 16; while (nc < need) nc *= 2;
  void *np = realloc(*arr, nc * sz); if (!np) return false; *arr = np; *cap = nc; return true;
}
static void coll_frag(void *ctx, double idx, qa_q3_pres_mark_fragment fr) {
  (void)idx; collect_t *c = ctx;
  if (!grow((void **)&c->out.fragments, &c->fcap, c->out.fragment_count + 1, sizeof *c->out.fragments)) { c->oom = true; return; }
  c->out.fragments[c->out.fragment_count++] = fr;
}
static void coll_points(void *ctx, double first, const qa_vec3 *pts, size_t n) {
  (void)first; collect_t *c = ctx;
  if (!grow((void **)&c->out.points, &c->pcap, c->out.point_count + n, sizeof *c->out.points)) { c->oom = true; return; }
  memcpy(c->out.points + c->out.point_count, pts, n * sizeof *pts); c->out.point_count += n;
}
bool qa_q3_pres_bsp_mark_projector_mark_fragments(qa_q3_pres_bsp_mark_projector *p,
    qa_q3_pres_mark_projection q, qa_q3_pres_mark_fragments *out) {
  memset(out, 0, sizeof *out);
  collect_t c; memset(&c, 0, sizeof c); c.q = &q;
  qa_q3_pres_source_mark_projection rec = {(double)q.point_count, q.max_points, q.max_fragments, &c,
      coll_point, coll_proj, coll_frag, coll_points};
  qa_q3_pres_bsp_mark_projector_mark_fragments_record(p, rec);
  if (c.oom) { qa_q3_pres_mark_fragments_destroy(&c.out); return false; }
  *out = c.out; return true;
}
static size_t box_surfaces(qa_q3_pres_bsp_mark_projector *p, qa_bounds b, qa_vec3 dir,
    const qa_q3_pres_mark_surface **out) {
  const qa_q3_pres_mark_geometry *g = &p->geometry;
  /* visited flags over surface indices via small local set (indices < surfaces_count). */
  size_t sc = g->surfaces_count;
  bool *visited = sc ? calloc(sc, 1) : NULL;
  if (sc && !visited) return 0;
  double stack[4096]; size_t sp = 0, n = 0;
  if (g->node_count == 0) { if (g->leaf_count) stack[sp++] = -1; } else stack[sp++] = 0;
  while (sp > 0) {
    double index = stack[--sp];
    if (index >= 0) {
      qa_q3_pres_mark_node node = AT(g->nodes, g->node_count, index);
      int side = box_side(b, AT(g->planes, g->plane_count, node.plane));
      if ((side & 2) && sp < 4096) stack[sp++] = node.child_is_node[1] ? node.child_index[1] : -1 - node.child_index[1];
      if ((side & 1) && sp < 4096) stack[sp++] = node.child_is_node[0] ? node.child_index[0] : -1 - node.child_index[0];
      continue;
    }
    qa_q3_pres_mark_leaf leaf = AT(g->leaves, g->leaf_count, -index - 1);
    for (double off = 0; off < leaf.surface_count; off++) {
      if (n >= MAX_SURF) break;
      double si = AT(g->leaf_surfaces, g->leaf_surface_count, leaf.first_surface + off);
      const qa_q3_pres_mark_surface *s = &AT(g->surfaces, g->surfaces_count, si);
      if (visited && (size_t)si < sc) { if (visited[(size_t)si]) continue; visited[(size_t)si] = true; }
      if (s->kind == QA_Q3_PRES_MARK_SKIP) continue;
      if (((qa_js_to_int32(s->surface_flags)) & 0x30) != 0 || ((qa_js_to_int32(s->content_flags)) & 64) != 0) continue;
      if (s->kind == QA_Q3_PRES_MARK_FACE &&
          (box_side(b, s->plane) != 3 || qa_dot3(s->plane.normal, dir) > -0.5)) continue;
      out[n++] = s;
    }
  }
  free(visited); return n;
}
double qa_q3_pres_bsp_mark_projector_mark_fragments_record(qa_q3_pres_bsp_mark_projector *p,
    qa_q3_pres_source_mark_projection q) {
  capacity(q.max_points); capacity(q.max_fragments);
  if (q.point_count != trunc(q.point_count) || q.point_count < 1)
    qa_range_error("mark projection requires at least one input point");
  qa_vec3 projection = finite_vec(q.read_projection(q.ctx));
  qa_vec3 direction = qa_normalize3_or_zero(projection);
  qa_vec3 pts[MAX_CLIP]; size_t count = 0;
  qa_vec3 mn = {99999, 99999, 99999}, mx = {-99999, -99999, -99999};
  for (double i = 0; i < q.point_count; i++) {
    qa_vec3 pt = finite_vec(q.read_point(q.ctx, i));
    if (i < MAX_CLIP) pts[count++] = pt;
    qa_vec3 cand[3] = {pt, qa_add3(pt, projection), qa_add3(pt, qa_scale3(direction, -20))};
    for (int k = 0; k < 3; k++) {
      mn.x = qa_js_min(mn.x, cand[k].x); mn.y = qa_js_min(mn.y, cand[k].y); mn.z = qa_js_min(mn.z, cand[k].z);
      mx.x = qa_js_max(mx.x, cand[k].x); mx.y = qa_js_max(mx.y, cand[k].y); mx.z = qa_js_max(mx.z, cand[k].z);
    }
  }
  if (q.max_fragments == 0 || q.max_points == 0) return 0;
  qa_plane planes[MAX_CLIP + 2]; size_t npl = 0;
  for (size_t i = 0; i < count; i++) {
    qa_vec3 edge = qa_sub3(pts[(i + 1) % count], pts[i]);
    qa_vec3 rev = qa_sub3(pts[i], qa_add3(pts[i], projection));
    qa_vec3 normal = qa_normalize_fast3(qa_cross3(edge, rev));
    planes[npl++] = (qa_plane){normal, qa_dot3(normal, pts[i])};
  }
  qa_vec3 inv = qa_scale3(direction, -1);
  planes[npl++] = (qa_plane){direction, f32(qa_dot3(direction, pts[0]) - 32)};
  planes[npl++] = (qa_plane){inv, f32(qa_dot3(inv, pts[0]) - 20)};
  const qa_q3_pres_mark_surface *surfs[MAX_SURF];
  size_t nsurf = box_surfaces(p, (qa_bounds){mn, mx}, direction, surfs);
  double returned_points = 0, returned_fragments = 0;
  for (size_t si = 0; si < nsurf; si++) {
    const qa_q3_pres_mark_surface *s = surfs[si];
    if (s->kind == QA_Q3_PRES_MARK_FACE) {
      if (qa_dot3(s->plane.normal, direction) > -0.5) continue;
      for (size_t i = 0; i < s->index_count; i += 3) {
        qa_vec3 tri[3], cur[MAX_CLIP], nxt[MAX_CLIP];
        for (int o = 0; o < 3; o++)
          tri[o] = qa_add3(AT(s->vertices, s->vertex_count, AT(s->indices, s->index_count, (double)(i + (size_t)o))),
                           qa_scale3(s->plane.normal, 0));
        memcpy(cur, tri, sizeof tri); size_t cn = 3;
        for (size_t pi = 0; pi < npl && cn; pi++) { cn = chop(cur, cn, planes[pi], nxt); memcpy(cur, nxt, cn * sizeof *cur); }
        if (cn && (double)cn + returned_points <= q.max_points) {
          q.write_fragment(q.ctx, returned_fragments, (qa_q3_pres_mark_fragment){returned_points, (double)cn});
          q.write_points(q.ctx, returned_points, cur, cn);
          returned_points += (double)cn; returned_fragments++;
        }
        if (returned_fragments == q.max_fragments) return returned_fragments;
      }
    } else if (s->kind == QA_Q3_PRES_MARK_GRID) {
      double w = s->grid_width, h = s->grid_height;
      for (double row = 0; row < h - 1; row++) for (double col = 0; col < w - 1; col++) {
        double base = row * w + col;
        double idxs[2][3] = {{base, base + w, base + 1}, {base + 1, base + w, base + w + 1}};
        double thr[2] = {-0.1, -0.05};
        for (int t = 0; t < 2; t++) {
          qa_vec3 tri[3];
          for (int o = 0; o < 3; o++) {
            qa_q3_pres_mark_grid_vertex v = AT(s->grid_vertices, s->grid_vertex_count, idxs[t][o]);
            tri[o] = qa_add3(v.position, qa_scale3(v.normal, 0));
          }
          qa_vec3 normal = qa_normalize_fast3(qa_cross3(qa_sub3(tri[0], tri[1]), qa_sub3(tri[2], tri[1])));
          if (qa_dot3(normal, direction) >= thr[t]) continue;
          qa_vec3 cur[MAX_CLIP], nxt[MAX_CLIP]; memcpy(cur, tri, sizeof tri); size_t cn = 3;
          for (size_t pi = 0; pi < npl && cn; pi++) { cn = chop(cur, cn, planes[pi], nxt); memcpy(cur, nxt, cn * sizeof *cur); }
          if (cn && (double)cn + returned_points <= q.max_points) {
            q.write_fragment(q.ctx, returned_fragments, (qa_q3_pres_mark_fragment){returned_points, (double)cn});
            q.write_points(q.ctx, returned_points, cur, cn);
            returned_points += (double)cn; returned_fragments++;
          }
          if (returned_fragments == q.max_fragments) return returned_fragments;
        }
      }
    }
  }
  return returned_fragments;
}
