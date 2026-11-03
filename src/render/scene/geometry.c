/* Port of src/render/scene/geometry.ts. GPL-2.0-or-later. */
#include "qa/render/scene/geometry.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
static void *xalloc(size_t n) { void *p = malloc(n?n:1); if (!p) qa_range_error("Allocation failed"); return p; }
static void *xg(void *p, size_t n) { void *q = realloc(p, n?n:1); if (!q) qa_range_error("Allocation failed"); return q; }
qa_bounds qa_scene_geometry_bounds(const qa_vec3 *pos, size_t n) {
  if (!n) return (qa_bounds){{0,0,0},{0,0,0}};
  qa_vec3 mn = {INFINITY,INFINITY,INFINITY}, mx = {-INFINITY,-INFINITY,-INFINITY};
  for (size_t i = 0; i < n; i++) {
    /* Math.min/Math.max keep NaN */
    mn.x = qa_js_min(mn.x, pos[i].x); mn.y = qa_js_min(mn.y, pos[i].y); mn.z = qa_js_min(mn.z, pos[i].z);
    mx.x = qa_js_max(mx.x, pos[i].x); mx.y = qa_js_max(mx.y, pos[i].y); mx.z = qa_js_max(mx.z, pos[i].z);
  }
  return (qa_bounds){ mn, mx };
}
qa_bounds qa_scene_geometry_bounds_vertices(const qa_material_vertex *v, size_t n) {
  if (!n) return (qa_bounds){{0,0,0},{0,0,0}};
  qa_vec3 *tmp = xalloc(n*sizeof *tmp); for (size_t i = 0; i < n; i++) tmp[i] = v[i].position;
  qa_bounds b = qa_scene_geometry_bounds(tmp, n); free(tmp); return b;
}
typedef struct poly { qa_vec3 *pts; size_t n; } poly;
typedef struct polys { poly *items; size_t n, cap; } polys;
static double axis_v(qa_vec3 v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }
static void split_water(const qa_vec3 *pts, size_t n, double size, polys *out) {
  qa_bounds b = qa_scene_geometry_bounds(pts, n);
  for (int a = 0; a < 3; a++) {
    double middle = size * floor((axis_v(b.min,a)+axis_v(b.max,a))*0.5/size + 0.5);
    if (axis_v(b.max,a) - middle < 8 || middle - axis_v(b.min,a) < 8) continue;
    qa_vec3 *front = xalloc((n*2+2)*sizeof *front), *back = xalloc((n*2+2)*sizeof *back); size_t nf = 0, nb = 0;
    for (size_t i = 0; i < n; i++) {
      qa_vec3 pt = pts[i], next = pts[(i+1)%n];
      double d = axis_v(pt,a)-middle, nd = axis_v(next,a)-middle;
      if (d >= 0) front[nf++] = pt;
      if (d <= 0) back[nb++] = pt;
      if (d == 0 || nd == 0 || (d > 0) == (nd > 0)) continue;
      double f = d / (d - nd);
      qa_vec3 is = { pt.x + f*(next.x-pt.x), pt.y + f*(next.y-pt.y), pt.z + f*(next.z-pt.z) };
      front[nf++] = is; back[nb++] = is;
    }
    split_water(front, nf, size, out); split_water(back, nb, size, out);
    free(front); free(back); return;
  }
  if (out->n == out->cap) { out->cap = out->cap ? out->cap*2 : 8; out->items = xg(out->items, out->cap*sizeof(poly)); }
  poly p; p.n = n; p.pts = xalloc((n?n:1)*sizeof *p.pts); memcpy(p.pts, pts, n*sizeof *p.pts);
  out->items[out->n++] = p;
}
static const void *atg(const void *base, size_t count, size_t sz, double index) {
  /* TS values[index]: fractional, negative and NaN indices are undefined. */
  if (!(index >= 0 && index == floor(index) && index < (double)count)) qa_range_error("Brush surface index %s outside %zu", QA_JS_NUMBER(index), count);
  return (const char *)base + (size_t)index*sz;
}
static qa_q1_texture_info at_q1ti(const qa_q1_texture_info*a,size_t n,double i){return *(const qa_q1_texture_info*)atg(a,n,sizeof*a,i);}
static qa_q2_texture_info at_q2ti(const qa_q2_texture_info*a,size_t n,double i){return *(const qa_q2_texture_info*)atg(a,n,sizeof*a,i);}
static qa_bsp_plane at_bp(const qa_bsp_plane*a,size_t n,double i){return *(const qa_bsp_plane*)atg(a,n,sizeof*a,i);}
static qa_bsp_edge at_be(const qa_bsp_edge*a,size_t n,double i){return *(const qa_bsp_edge*)atg(a,n,sizeof*a,i);}
static qa_vec3 at_v3(const qa_vec3*a,size_t n,double i){return *(const qa_vec3*)atg(a,n,sizeof*a,i);}
void qa_prepared_brush_face_destroy(qa_prepared_brush_face *f) { if (!f) return; free(f->owned_vertices); free(f->owned_indices); memset(f, 0, sizeof *f); }
bool qa_prepare_brush_face(bool is_q1, const qa_q1_world_geometry *q1, const qa_q2_world_geometry *q2,
  qa_bsp_face face, double face_index, qa_vec2 image_size, bool warp, double subdivision, qa_prepared_brush_face *out) {
  memset(out, 0, sizeof *out);
  /* unified views */
  const qa_bsp_plane *planes; size_t nplanes; const qa_vec3 *verts; size_t nverts;
  const qa_bsp_edge *edges; size_t nedges; qa_double_view sedges;
  qa_texture_projection proj; const qa_decoupled_lightmap *dec; size_t ndec;
  qa_bsp_lighting lighting; bool rgb_q1;
  if (is_q1) {
    qa_q1_texture_info info = at_q1ti(q1->texture_info, q1->texture_info_count, face.texture_info);
    proj = info.projection; planes=q1->planes; nplanes=q1->plane_count; verts=q1->vertices; nverts=q1->vertex_count;
    edges=q1->edges; nedges=q1->edge_count; sedges=q1->surface_edges; dec=q1->decoupled_lightmaps; ndec=q1->decoupled_lightmap_count;
    lighting=q1->lighting; rgb_q1 = lighting.is_rgb8;
  } else {
    qa_q2_texture_info info = at_q2ti(q2->texture_info, q2->texture_info_count, face.texture_info);
    proj = info.projection; planes=q2->planes; nplanes=q2->plane_count; verts=q2->vertices; nverts=q2->vertex_count;
    edges=q2->edges; nedges=q2->edge_count; sedges=q2->surface_edges; dec=q2->decoupled_lightmaps; ndec=q2->decoupled_lightmap_count;
    lighting=q2->lighting; rgb_q1 = false;
  }
  qa_plane raw = at_bp(planes, nplanes, face.plane).plane;
  qa_plane plane = face.back ? (qa_plane){ qa_scale3(raw.normal,-1), -raw.distance } : raw;
  size_t npts = face.edges.count > 0 ? (size_t)face.edges.count : 0;
  qa_vec3 *points = xalloc((npts?npts:1)*sizeof *points);
  for (size_t ei = 0; ei < npts; ei++) {
    double sd = face.edges.first + (double)ei;
    if (!(sd >= 0 && sd == floor(sd) && sd < (double)sedges.count)) qa_range_error("Brush surface index %s outside %zu", QA_JS_NUMBER(face.edges.first+(double)ei), sedges.count);
    double signed_edge = sedges.values[(size_t)sd];
    qa_bsp_edge e = at_be(edges, nedges, fabs(signed_edge));
    points[ei] = at_v3(verts, nverts, e.vertices[signed_edge >= 0 ? 0 : 1]);
  }
  double minS=INFINITY,minT=INFINITY,maxS=-INFINITY,maxT=-INFINITY;
  for (size_t i = 0; i < npts; i++) {
    double s = qa_dot3(points[i], (qa_vec3){proj.s.x,proj.s.y,proj.s.z}) + proj.s.w;
    double t = qa_dot3(points[i], (qa_vec3){proj.t.x,proj.t.y,proj.t.z}) + proj.t.w;
    minS = qa_js_min(minS, s); minT = qa_js_min(minT, t);
    maxS = qa_js_max(maxS, s); maxT = qa_js_max(maxT, t);
  }
  qa_vec2 mins = { floor(minS/16)*16, floor(minT/16)*16 };
  const qa_decoupled_lightmap *mapping = NULL;
  if (dec && face_index == trunc(face_index) && face_index >= 0 && face_index < (double)ndec) mapping = &dec[(size_t)face_index];
  qa_lightmap_projection projection;
  memset(&projection, 0, sizeof projection);
  if (!mapping) { projection.kind = "classic"; projection.texture = proj; projection.texture_mins = mins; }
  else { projection.kind = "decoupled"; projection.mapping = *mapping; }
  double width = mapping ? mapping->width : ceil(maxS/16) - floor(minS/16) + 1;
  double height = mapping ? mapping->height : ceil(maxT/16) - floor(minT/16) + 1;
  bool has_off; double sample_off;
  if (!mapping) { has_off = face.has_lighting_offset; sample_off = face.lighting_offset; }
  else { has_off = mapping->has_lighting_offset; sample_off = mapping->lighting_offset; }
  bool has_offset = has_off; double offset = has_off ? sample_off * (is_q1 && rgb_q1 ? 3 : 1) : 0;
  /* TS lighting: geometry.lighting (the face keeps a reference to its own map's lighting; one static copy shared by
   * every map made faces of a world read the last decoded BSP's samples, e.g. a Quake ammo box's). */
  const qa_bsp_lighting *lighting_source = is_q1 ? &q1->lighting : &q2->lighting;
  qa_lightmap_face lm; memset(&lm, 0, sizeof lm);
  lm.width = width; lm.height = height; lm.plane = plane; lm.projection = projection;
  lm.lighting = has_offset ? lighting_source : NULL; lm.offset = has_offset ? offset : 0;
  lm.styles = face.styles.values; lm.style_count = face.styles.count;
  polys list = {0};
  if (warp) split_water(points, npts, subdivision, &list);
  else { list.items = xalloc(sizeof(poly)); list.items[0].pts = points; list.items[0].n = npts; list.n = 1; list.cap = 1; points = NULL; }
  qa_material_vertex *vertices = NULL; size_t nv = 0, cv = 0;
  double *indices = NULL; size_t ni = 0, ci = 0;
  for (size_t pi = 0; pi < list.n; pi++) {
    poly pg = list.items[pi]; size_t first = nv;
    for (size_t i = 0; i < pg.n; i++) {
      qa_vec3 pos = pg.pts[i];
      qa_vec2 uv = { qa_dot3(pos,(qa_vec3){proj.s.x,proj.s.y,proj.s.z}) + proj.s.w, qa_dot3(pos,(qa_vec3){proj.t.x,proj.t.y,proj.t.z}) + proj.t.w };
      qa_material_vertex v; memset(&v, 0, sizeof v); v.position = pos; v.normal = plane.normal;
      v.tex_coord = warp ? uv : (qa_vec2){ uv.x/image_size.x, uv.y/image_size.y };
      v.lightmap_coord = qa_lightmap_atlas_coordinates(pos, projection, (qa_vec2){0,0}, width, height);
      v.color = qa_math_vec4(255,255,255,255);
      if (nv == cv) { cv = cv ? cv*2 : 16; vertices = xg(vertices, cv*sizeof *vertices); }
      vertices[nv++] = v;
    }
    for (size_t idx = 2; idx < pg.n; idx++) {
      if (ni + 3 > ci) { ci = ci ? ci*2 : 16; while (ni+3 > ci) ci *= 2; indices = xg(indices, ci*sizeof *indices); }
      indices[ni++] = (double)first; indices[ni++] = (double)(first+idx-1); indices[ni++] = (double)(first+idx);
    }
    free(pg.pts);
  }
  free(list.items); free(points);
  out->owned_vertices = vertices; out->owned_indices = indices;
  out->geometry.vertices = vertices; out->geometry.vertex_count = nv;
  out->geometry.indices = indices; out->geometry.index_count = ni;
  out->plane = plane; out->lightmap = lm; out->decoupled = mapping != NULL;
  return true;
}
