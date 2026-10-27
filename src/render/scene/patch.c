/* Port of src/render/scene/patch.ts. GPL-2.0-or-later. */
#include "qa/render/scene/patch.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/foundation/support.h"
typedef qa_q3_bsp_vertex V;
static float fr(double v) { return (float)v; }
static void *xalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("Patch allocation failed"); return p; }
static V atv(const V *values, size_t count, long index) {
  if (index < 0 || (size_t)index >= count) qa_range_error("patch index %ld outside %zu", index, count);
  return values[index];
}
void qa_patch_mesh_destroy(qa_patch_mesh *m) { if (!m) return; free(m->vertices); free(m->indices); free(m->width_lod_error); free(m->height_lod_error); memset(m, 0, sizeof *m); }
static V midp(V a, V b) {
  V r;
  r.position = qa_scale3(qa_add3(a.position, b.position), 0.5);
  r.normal = qa_normalize3(qa_add3(a.normal, b.normal));
  r.tex_coord = qa_math_vec2(fr(a.tex_coord.x + b.tex_coord.x) * 0.5, fr(a.tex_coord.y + b.tex_coord.y) * 0.5);
  r.lightmap_coord = qa_math_vec2(fr(a.lightmap_coord.x + b.lightmap_coord.x) * 0.5, fr(a.lightmap_coord.y + b.lightmap_coord.y) * 0.5);
  r.color = qa_math_vec4(((int32_t)a.color.x + (int32_t)b.color.x) >> 1, ((int32_t)a.color.y + (int32_t)b.color.y) >> 1,
    ((int32_t)a.color.z + (int32_t)b.color.z) >> 1, ((int32_t)a.color.w + (int32_t)b.color.w) >> 1);
  return r;
}
/* dynamic rows */
typedef struct row { V *v; int n, cap; } row;
static void row_init(row *r, const V *src, int n) { r->cap = n > 4 ? n : 4; r->v = xalloc((size_t)r->cap * sizeof(V)); memcpy(r->v, src, (size_t)n * sizeof(V)); r->n = n; }
static void row_grow(row *r, int need) { if (need > r->cap) { while (r->cap < need) r->cap *= 2; r->v = realloc(r->v, (size_t)r->cap * sizeof(V)); if (!r->v) qa_range_error("Patch allocation failed"); } }
/* splice: replace del items at pos with ins items */
static void row_splice(row *r, int pos, int del, const V *ins, int nins) {
  int nn = r->n - del + nins; row_grow(r, nn);
  memmove(r->v + pos + nins, r->v + pos + del, (size_t)(r->n - pos - del) * sizeof(V));
  if (nins) memcpy(r->v + pos, ins, (size_t)nins * sizeof(V));
  r->n = nn;
}
static row *transpose_rows(row *rows, int nrows) {
  int w = rows[0].n; row *out = xalloc((size_t)w * sizeof(row));
  for (int x = 0; x < w; x++) { row r; r.cap = nrows; r.v = xalloc((size_t)nrows * sizeof(V)); r.n = nrows; for (int y = 0; y < nrows; y++) r.v[y] = rows[y].v[x]; out[x] = r; }
  return out;
}
static void free_rows(row *rows, int n) { for (int i = 0; i < n; i++) free(rows[i].v); free(rows); }
static bool create_mesh(row *rows, int nrows, double *we, int nwe, double *he, int nhe, qa_patch_mesh *out) {
  int width = rows[0].n, height = nrows;
  int wrap_w = 1, wrap_h = 1;
  for (int y = 0; y < height; y++) { qa_vec3 d = qa_sub3(rows[y].v[0].position, rows[y].v[width-1].position); if (qa_dot3(d,d) > 1) wrap_w = 0; }
  for (int x = 0; x < width; x++) { qa_vec3 d = qa_sub3(rows[0].v[x].position, rows[height-1].v[x].position); if (qa_dot3(d,d) > 1) wrap_h = 0; }
  static const int NB[8][2] = {{0,1},{1,1},{1,0},{1,-1},{0,-1},{-1,-1},{-1,0},{-1,1}};
  out->vertices = xalloc((size_t)(width*height) * sizeof(V)); out->vertex_count = (size_t)(width*height);
  for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
    V point = rows[y].v[x]; qa_vec3 around[8]; int has[8];
    for (int k = 0; k < 8; k++) {
      has[k] = 0;
      for (int dist = 1; dist <= 3; dist++) {
        int nx = x + NB[k][0]*dist, ny = y + NB[k][1]*dist;
        if (wrap_w) { if (nx < 0) nx = width - 1 + nx; else if (nx >= width) nx = 1 + nx - width; }
        if (wrap_h) { if (ny < 0) ny = height - 1 + ny; else if (ny >= height) ny = 1 + ny - height; }
        if (nx < 0 || nx >= width || ny < 0 || ny >= height) break;
        qa_vec3 delta = qa_sub3(rows[ny].v[nx].position, point.position);
        if (qa_length3(delta) != 0) { around[k] = qa_normalize3_or_zero(delta); has[k] = 1; break; }
      }
    }
    qa_vec3 normal = {0,0,0};
    for (int k = 0; k < 8; k++) if (has[k] && has[(k+1)&7]) { qa_vec3 cr = qa_cross3(around[(k+1)&7], around[k]); if (qa_length3(cr) != 0) normal = qa_add3(normal, qa_normalize3_or_zero(cr)); }
    point.normal = qa_normalize3_or_zero(normal);
    out->vertices[y*width+x] = point;
  }
  out->index_count = (size_t)((width-1)*(height-1)*6); out->indices = xalloc((out->index_count?out->index_count:1) * sizeof(double));
  size_t ii = 0;
  for (int y = 0; y < height-1; y++) for (int x = 0; x < width-1; x++) { int a = y*width+x, b = a+width; out->indices[ii++]=a; out->indices[ii++]=b; out->indices[ii++]=a+1; out->indices[ii++]=a+1; out->indices[ii++]=b; out->indices[ii++]=b+1; }
  out->width = width; out->height = height;
  out->width_lod_error = xalloc((size_t)(nwe?nwe:1) * sizeof(double)); memcpy(out->width_lod_error, we, (size_t)nwe * sizeof(double));
  out->height_lod_error = xalloc((size_t)(nhe?nhe:1) * sizeof(double)); memcpy(out->height_lod_error, he, (size_t)nhe * sizeof(double));
  return true;
}
bool qa_tessellate_patch(const V *points, size_t count, double width_d, double height_d, double subdiv, qa_patch_mesh *out) {
  if (width_d != trunc(width_d) || height_d != trunc(height_d)) qa_range_error("invalid quadratic patch dimensions or subdivision threshold");
  int width = (int)width_d, height = (int)height_d;
  if (width < 3 || height < 3 || width > 65 || height > 65 || width*height > 1024 || width % 2 != 1 || height % 2 != 1 || (int)count != width*height || !isfinite(subdiv))
    qa_range_error("invalid quadratic patch dimensions or subdivision threshold");
  memset(out, 0, sizeof *out);
  row *rows = xalloc((size_t)height * sizeof(row)); int nrows = height;
  for (int y = 0; y < height; y++) row_init(&rows[y], points + y*width, width);
  double errors[2][65];
  for (int dir = 0; dir < 2; dir++) {
    double *error = errors[dir]; for (int i = 0; i < 65; i++) error[i] = 0;
    for (int x = 0; x + 2 < rows[0].n; x += 2) {
      double maximum = 0;
      for (int y = 0; y < nrows; y++) {
        qa_vec3 a = rows[y].v[x].position, b = rows[y].v[x+1].position, c = rows[y].v[x+2].position;
        qa_vec3 curve = qa_sub3(qa_scale3(qa_add3(qa_add3(a, qa_scale3(b, 2)), c), 0.25), a);
        qa_vec3 line = qa_normalize3(qa_sub3(c, a));
        qa_vec3 dist = qa_sub3(curve, qa_scale3(line, qa_dot3(curve, line)));
        double d = qa_dot3(dist, dist); if (d > maximum) maximum = d;
      }
      maximum = fr(sqrt(maximum));
      if (maximum < fr(0.1)) { error[x+1] = 999; continue; }
      if (rows[0].n + 2 > 65 || maximum <= subdiv) { error[x+1] = fr(1/maximum); continue; }
      error[x+2] = fr(1/maximum);
      for (int y = 0; y < nrows; y++) {
        V prev = midp(rows[y].v[x], rows[y].v[x+1]), next = midp(rows[y].v[x+1], rows[y].v[x+2]);
        V ins[3] = { prev, midp(prev, next), next };
        row_splice(&rows[y], x+1, 1, ins, 3);
      }
      x -= 2;
    }
    row *t = transpose_rows(rows, nrows); int nr = rows[0].n; free_rows(rows, nrows); rows = t; nrows = nr;
  }
  width = rows[0].n; height = nrows;
  for (int x = 0; x < width; x++) for (int y = 1; y < height; y += 2) {
    V point = rows[y].v[x];
    rows[y].v[x] = midp(midp(point, rows[y+1].v[x]), midp(point, rows[y-1].v[x]));
  }
  for (int y = 0; y < height; y++) for (int x = 1; x < width; x += 2) {
    V point = rows[y].v[x];
    rows[y].v[x] = midp(midp(point, rows[y].v[x+1]), midp(point, rows[y].v[x-1]));
  }
  /* errors are dynamic JS arrays (spliced); mirror with memmove lists */
  double we[65], he[65]; int nwe = 65, nhe = 65;
  memcpy(we, errors[0], sizeof we); memcpy(he, errors[1], sizeof he);
  for (int x = 1; x < width - 1; x++) {
    if (we[x] != 999) continue;
    for (int y = 0; y < nrows; y++) row_splice(&rows[y], x, 1, NULL, 0);
    memmove(we+x, we+x+1, (size_t)(nwe-x-1)*sizeof(double)); nwe--; width--;
  }
  for (int y = 1; y < height - 1; y++) {
    if (he[y] != 999) continue;
    free(rows[y].v); memmove(rows+y, rows+y+1, (size_t)(nrows-y-1)*sizeof(row)); nrows--;
    memmove(he+y, he+y+1, (size_t)(nhe-y-1)*sizeof(double)); nhe--; height--;
  }
  if (height > width) {
    row *t = transpose_rows(rows, nrows); int nr = rows[0].n; free_rows(rows, nrows); rows = t; nrows = nr;
    for (int y = 0; y < nrows; y++) for (int i = 0, j = rows[y].n-1; i < j; i++, j--) { V tmp = rows[y].v[i]; rows[y].v[i]=rows[y].v[j]; rows[y].v[j]=tmp; }
    int ow = width; double owe[65]; memcpy(owe, we, sizeof owe); int onwe = nwe;
    width = height; height = ow;
    double nwe2[65]; for (int i = 0; i < width; i++) nwe2[i] = he[width-1-i];
    memcpy(we, nwe2, sizeof we); nwe = width;
    memcpy(he, owe, (size_t)onwe * sizeof(double)); nhe = height;
  }
  bool ok = create_mesh(rows, nrows, we, width < nwe ? width : nwe, he, height < nhe ? height : nhe, out);
  /* create_mesh slices errors to width/height like TS slice(0,width) */
  free_rows(rows, nrows);
  return ok;
}
bool qa_insert_patch_strip(const qa_patch_mesh *mesh, bool dir_w, double index_d, double edge_d, qa_vec3 position, double lod_error, qa_patch_mesh *out) {
  int mw = (int)mesh->width, mh = (int)mesh->height;
  int width = mw + (dir_w ? 1 : 0), height = mh + (dir_w ? 0 : 1);
  int old_size = dir_w ? mw : mh, edge_size = dir_w ? mh : mw;
  if (width > 65 || height > 65 || index_d != trunc(index_d) || index_d < 1 || index_d >= old_size || edge_d != trunc(edge_d) || edge_d < 0 || edge_d >= edge_size)
    qa_range_error("invalid patch insertion");
  int index = (int)index_d, edge = (int)edge_d;
  memset(out, 0, sizeof *out);
  row *rows = xalloc((size_t)height * sizeof(row));
  for (int y = 0; y < height; y++) {
    rows[y].cap = width; rows[y].v = xalloc((size_t)width * sizeof(V)); rows[y].n = width;
    for (int x = 0; x < width; x++) {
      int coord = dir_w ? x : y;
      if (coord != index) {
        int ox = x - (dir_w && x > index ? 1 : 0), oy = y - (!dir_w && y > index ? 1 : 0);
        rows[y].v[x] = atv(mesh->vertices, mesh->vertex_count, oy*mw+ox);
      } else {
        int next = y*mw+x, prev = next - (dir_w ? 1 : mw);
        V point = midp(atv(mesh->vertices, mesh->vertex_count, prev), atv(mesh->vertices, mesh->vertex_count, next));
        if ((dir_w ? y : x) == edge) point.position = position;
        rows[y].v[x] = point;
      }
    }
  }
  double we[66], he[66]; int nwe = mw, nhe = mh;
  memcpy(we, mesh->width_lod_error, (size_t)mw*sizeof(double)); memcpy(he, mesh->height_lod_error, (size_t)mh*sizeof(double));
  if (dir_w) { memmove(we+index+1, we+index, (size_t)(nwe-index)*sizeof(double)); we[index]=lod_error; nwe++; }
  else { memmove(he+index+1, he+index, (size_t)(nhe-index)*sizeof(double)); he[index]=lod_error; nhe++; }
  bool ok = create_mesh(rows, height, we, nwe, he, nhe, out);
  free_rows(rows, height);
  return ok;
}
/* ---- allocated (44-byte block) form ---- */
static void put_f32(uint8_t *b, size_t o, double v) { float f = fr(v); memcpy(b+o, &f, 4); }
static double get_f32(const uint8_t *b, size_t o) { float f; memcpy(&f, b+o, 4); return f; }
static void put_i32(uint8_t *b, size_t o, int32_t v) { memcpy(b+o, &v, 4); }
static int32_t get_i32(const uint8_t *b, size_t o) { int32_t v; memcpy(&v, b+o, 4); return v; }
static void put_vec(uint8_t *b, size_t o, qa_vec3 v) { put_f32(b,o,v.x); put_f32(b,o+4,v.y); put_f32(b,o+8,v.z); }
static qa_vec3 get_vec(const uint8_t *b, size_t o) { return qa_math_vec3(get_f32(b,o), get_f32(b,o+4), get_f32(b,o+8)); }
typedef struct tblock { uint8_t *bytes; size_t length; double address; qa_patch_allocation *alloc; int live; } tblock;
static tblock *tblock_new(qa_patch_memory_profile p, size_t length) {
  tblock *t = xalloc(sizeof *t); t->live = 1; t->length = length; t->alloc = NULL;
  if (p.zone) { qa_patch_allocation a = p.zone->allocate(p.zone->ctx, (double)length); qa_patch_allocation *s = xalloc(sizeof a); *s = a; t->alloc = s; t->bytes = a.bytes; t->address = a.address; }
  else { t->bytes = xalloc(length); t->address = 0; }
  return t;
}
static uint8_t *tbytes(tblock *t) { if (!t->live) qa_range_error("Patch allocation is no longer valid"); return t->alloc ? t->alloc->bytes : t->bytes; }
static void tblock_free(tblock *t, qa_patch_memory_profile p) {
  if (p.zone) { if (t->alloc && t->alloc->free_fn) t->alloc->free_fn(t->alloc->ctx, t->alloc); }
  else (void)tbytes(t);
  t->live = 0;
  if (!p.zone) free(t->bytes);
  free(t->alloc); free(t);
}
double qa_allocated_patch_mesh_width(const qa_allocated_patch_mesh *m) { return get_i32(m->block, 76); }
double qa_allocated_patch_mesh_height(const qa_allocated_patch_mesh *m) { return get_i32(m->block, 80); }
qa_q3_bsp_vertex qa_allocated_patch_mesh_vertex(const qa_allocated_patch_mesh *m, double index) {
  size_t o = 92 + (size_t)index * 44; V v;
  v.position = get_vec(m->block, o); v.normal = get_vec(m->block, o+28);
  v.tex_coord = qa_math_vec2(get_f32(m->block,o+12), get_f32(m->block,o+16));
  v.lightmap_coord = qa_math_vec2(get_f32(m->block,o+20), get_f32(m->block,o+24));
  v.color = qa_math_vec4(m->block[o+40], m->block[o+41], m->block[o+42], m->block[o+43]);
  return v;
}
double qa_allocated_patch_mesh_width_error(const qa_allocated_patch_mesh *m, double i) { return get_f32(m->width_block, (size_t)i*4); }
double qa_allocated_patch_mesh_height_error(const qa_allocated_patch_mesh *m, double i) { return get_f32(m->height_block, (size_t)i*4); }
qa_vec3 qa_allocated_patch_mesh_lod_origin(const qa_allocated_patch_mesh *m) { return get_vec(m->block, 52); }
double qa_allocated_patch_mesh_lod_radius(const qa_allocated_patch_mesh *m) { return get_f32(m->block, 64); }
void qa_allocated_patch_mesh_set_lod_radius(qa_allocated_patch_mesh *m, double r) { put_f32(m->block, 64, r); }
double qa_allocated_patch_mesh_lod_fixed(const qa_allocated_patch_mesh *m) { return get_i32(m->block, 68); }
void qa_allocated_patch_mesh_set_lod_fixed(qa_allocated_patch_mesh *m, double v) { put_i32(m->block, 68, (int32_t)v); }
bool qa_allocated_patch_mesh_lod_stitched(const qa_allocated_patch_mesh *m) { return get_i32(m->block, 72) != 0; }
void qa_allocated_patch_mesh_set_lod_stitched(qa_allocated_patch_mesh *m, bool v) { put_i32(m->block, 72, v?1:0); }
void qa_allocated_patch_mesh_set_lod_volume(qa_allocated_patch_mesh *m, qa_vec3 o, double r) { put_vec(m->block, 52, o); qa_allocated_patch_mesh_set_lod_radius(m, r); }
void qa_temporary_patch_mesh_create(const qa_patch_mesh *mesh, qa_patch_memory_profile profile, qa_allocated_patch_mesh *out) {
  memset(out, 0, sizeof *out); out->temporary = true; out->profile = profile; out->indices = mesh->indices; out->index_count = mesh->index_count;
  int w = (int)mesh->width, h = (int)mesh->height;
  tblock *grid = tblock_new(profile, 92 + (size_t)(w*h)*44); uint8_t *block = tbytes(grid); memset(block, 0, grid->length);
  tblock *wb = tblock_new(profile, (size_t)w*4); put_i32(block, 84, (int32_t)wb->address);
  for (int i = 0; i < w; i++) put_f32(tbytes(wb), (size_t)i*4, mesh->width_lod_error[i]);
  tblock *hb = tblock_new(profile, (size_t)h*4); put_i32(block, 88, (int32_t)hb->address);
  for (int i = 0; i < h; i++) put_f32(tbytes(hb), (size_t)i*4, mesh->height_lod_error[i]);
  put_i32(block, 76, w); put_i32(block, 80, h); put_i32(block, 0, 3);
  qa_vec3 mn = {99999,99999,99999}, mx = {-99999,-99999,-99999};
  for (int x = 0; x < w; x++) for (int y = 0; y < h; y++) {
    int idx = y*w+x; V pt = atv(mesh->vertices, mesh->vertex_count, idx); size_t o = 92 + (size_t)idx*44;
    put_vec(block,o,pt.position); put_vec(block,o+28,pt.normal);
    put_f32(block,o+12,pt.tex_coord.x); put_f32(block,o+16,pt.tex_coord.y);
    put_f32(block,o+20,pt.lightmap_coord.x); put_f32(block,o+24,pt.lightmap_coord.y);
    block[o+40]=(uint8_t)pt.color.x; block[o+41]=(uint8_t)pt.color.y; block[o+42]=(uint8_t)pt.color.z; block[o+43]=(uint8_t)pt.color.w;
    if (pt.position.x<mn.x) mn.x=pt.position.x;
    if (pt.position.x>mx.x) mx.x=pt.position.x;
    if (pt.position.y<mn.y) mn.y=pt.position.y;
    if (pt.position.y>mx.y) mx.y=pt.position.y;
    if (pt.position.z<mn.z) mn.z=pt.position.z;
    if (pt.position.z>mx.z) mx.z=pt.position.z;
  }
  put_vec(block,12,mn); put_vec(block,24,mx);
  qa_vec3 origin = qa_scale3(qa_add3(mn,mx),0.5); double radius = qa_length3(qa_sub3(mn,origin));
  put_vec(block,36,origin); put_f32(block,48,radius);
  out->block = block; out->width_block = tbytes(wb); out->height_block = tbytes(hb);
  out->_grid_alloc = grid; out->_width_alloc = wb; out->_height_alloc = hb;
  qa_allocated_patch_mesh_set_lod_volume(out, origin, radius);
}
void qa_temporary_patch_mesh_free(qa_allocated_patch_mesh *m) {
  tblock_free((tblock*)m->_width_alloc, m->profile); tblock_free((tblock*)m->_height_alloc, m->profile); tblock_free((tblock*)m->_grid_alloc, m->profile);
}
void qa_temporary_patch_mesh_replace(qa_allocated_patch_mesh *temp, const qa_patch_mesh *mesh, qa_allocated_patch_mesh *out) {
  qa_vec3 origin = qa_allocated_patch_mesh_lod_origin(temp); double radius = qa_allocated_patch_mesh_lod_radius(temp);
  qa_temporary_patch_mesh_free(temp);
  qa_temporary_patch_mesh_create(mesh, temp->profile, out);
  qa_allocated_patch_mesh_set_lod_volume(out, origin, radius);
}
void qa_temporary_patch_mesh_move_to_hunk(qa_allocated_patch_mesh *temp, qa_patch_hunk_alloc_fn allocate, void *ctx, qa_allocated_patch_mesh *out) {
  memset(out, 0, sizeof *out); out->indices = temp->indices; out->index_count = temp->index_count;
  tblock *g = (tblock*)temp->_grid_alloc, *wb = (tblock*)temp->_width_alloc;
  double waddr = 0, haddr = 0;
  uint8_t *block = allocate(ctx, "R_MovePatchSurfacesToHunk:grid", g->length, &(double){0});
  memcpy(block, temp->block, g->length);
  int w = (int)qa_allocated_patch_mesh_width(temp), h = (int)qa_allocated_patch_mesh_height(temp);
  uint8_t *widthb = allocate(ctx, "R_MovePatchSurfacesToHunk:widthLodError", (size_t)w*4, &waddr);
  memcpy(widthb, temp->width_block, (size_t)w*4); put_i32(block, 84, (int32_t)waddr);
  uint8_t *heightb = allocate(ctx, "R_MovePatchSurfacesToHunk:heightLodError", (size_t)h*4, &haddr);
  put_i32(block, 88, (int32_t)haddr);
  /* TS self-copies the old height array; new hunk array stays zeroed. */
  memmove(temp->height_block, temp->height_block, (size_t)h*4);
  (void)wb;
  qa_temporary_patch_mesh_free(temp);
  out->block = block; out->width_block = widthb; out->height_block = heightb;
}
bool qa_allocated_patch_mesh_snapshot(const qa_allocated_patch_mesh *mesh, qa_patch_mesh *out) {
  memset(out, 0, sizeof *out);
  int w = (int)qa_allocated_patch_mesh_width(mesh), h = (int)qa_allocated_patch_mesh_height(mesh);
  out->width = w; out->height = h; out->vertex_count = (size_t)(w*h);
  out->vertices = xalloc((out->vertex_count?out->vertex_count:1)*sizeof(V));
  for (int i = 0; i < w*h; i++) out->vertices[i] = qa_allocated_patch_mesh_vertex(mesh, i);
  out->index_count = mesh->index_count;
  out->indices = xalloc((out->index_count?out->index_count:1)*sizeof(double));
  if (out->index_count) memcpy(out->indices, mesh->indices, out->index_count*sizeof(double));
  out->width_lod_error = xalloc((size_t)(w?w:1)*sizeof(double));
  for (int i = 0; i < w; i++) out->width_lod_error[i] = qa_allocated_patch_mesh_width_error(mesh, i);
  out->height_lod_error = xalloc((size_t)(h?h:1)*sizeof(double));
  for (int i = 0; i < h; i++) out->height_lod_error[i] = qa_allocated_patch_mesh_height_error(mesh, i);
  return true;
}
bool qa_insert_patch_strip_allocated(qa_allocated_patch_mesh *temp, bool dir_w, double index, double edge, qa_vec3 position, double lod_error, qa_allocated_patch_mesh *out) {
  qa_patch_mesh snap, result;
  qa_allocated_patch_mesh_snapshot(temp, &snap);
  /* TS copies vertex fields for temporary meshes; snapshot does so. */
  bool ok = qa_insert_patch_strip(&snap, dir_w, index, edge, position, lod_error, &result);
  qa_patch_mesh_destroy(&snap);
  if (!ok) return false;
  qa_temporary_patch_mesh_replace(temp, &result, out);
  qa_patch_mesh_destroy(&result);
  return true;
}
