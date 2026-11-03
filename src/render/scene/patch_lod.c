/* Port of src/render/scene/patch-lod.ts. GPL-2.0-or-later. */
#include "qa/render/scene/patch_lod.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/foundation/support.h"
static float fr(double v) { return (float)v; }
static void *xalloc(size_t n) { void *p = malloc(n?n:1); if (!p) qa_range_error("Patch allocation failed"); return p; }
qa_patch_grid qa_create_patch_grid(qa_patch_mesh mesh, qa_vec3 b[2]) {
  qa_patch_grid g; memset(&g, 0, sizeof g); g.mesh = mesh;
  g.lod_origin = qa_scale3(qa_add3(b[0], b[1]), 0.5);
  g.lod_radius = qa_length3(qa_sub3(b[0], g.lod_origin));
  return g;
}
bool qa_create_patch_grid_allocated(qa_allocated_patch_mesh *mesh, qa_vec3 b[2], qa_patch_grid *out) {
  qa_vec3 origin = qa_scale3(qa_add3(b[0], b[1]), 0.5);
  double radius = qa_length3(qa_sub3(b[0], origin));
  qa_allocated_patch_mesh_set_lod_volume(mesh, origin, radius);
  memset(out, 0, sizeof *out); out->lod_origin = origin; out->lod_radius = radius;
  return qa_allocated_patch_mesh_snapshot(mesh, &out->mesh);
}
typedef struct edge { bool width; int boundary, count, offset, stride; const double *errors; } edge;
static edge edges_of(const qa_patch_mesh *m, edge out[4]) {
  int w = (int)m->width, h = (int)m->height;
  out[0] = (edge){ true, 0, w, 0, 1, m->width_lod_error };
  out[1] = (edge){ true, h-1, w, (h-1)*w, 1, m->width_lod_error };
  out[2] = (edge){ false, 0, h, 0, w, m->height_lod_error };
  out[3] = (edge){ false, w-1, h, w-1, w, m->height_lod_error };
  return out[0];
}
static qa_vec3 epoint(const qa_patch_mesh *m, edge e, int i) {
  int idx = e.offset + e.stride*i;
  if (idx < 0 || (size_t)idx >= m->vertex_count) qa_range_error("patch LOD index %d outside %zu", idx, m->vertex_count);
  return m->vertices[idx].position;
}
static double eerr(const double *errs, int count, int i) { if (i < 0 || i >= count) qa_range_error("patch LOD index %d outside %d", i, count); return errs[i]; }
static bool matches(qa_vec3 a, qa_vec3 b) { return fabs(fr(a.x-b.x)) <= 0.1 && fabs(fr(a.y-b.y)) <= 0.1 && fabs(fr(a.z-b.z)) <= 0.1; }
static bool merged(const qa_patch_mesh *m, edge e) {
  for (int i = 1; i < e.count-1; i++) for (int j = i+1; j < e.count-1; j++) if (matches(epoint(m,e,i), epoint(m,e,j))) return true;
  return false;
}
static bool same_group(qa_vec3 ao, double ar, const qa_patch_grid *b) { return ar == b->lod_radius && ao.x==b->lod_origin.x && ao.y==b->lod_origin.y && ao.z==b->lod_origin.z; }
static bool stitch(const qa_patch_mesh *source, qa_patch_mesh *target) {
  edge tedges[4]; edges_of(target, tedges); edge sedges[4]; edges_of(source, sedges);
  for (int rev = 0; rev < 2; rev++) for (int se = 0; se < 4; se++) {
    edge sedge = sedges[se]; if (merged(source, sedge)) continue;
    for (int k = rev ? sedge.count-1 : 0; rev ? k > 1 : k < sedge.count-2; k += rev ? -2 : 2) {
      int next = k + (rev ? -2 : 2), middle = k + (rev ? -1 : 1);
      for (int te = 0; te < 4; te++) {
        edge tedge = tedges[te]; if (tedge.count >= 65) continue;
        for (int l = 0; l < tedge.count-1; l++) {
          qa_vec3 a = epoint(target, tedge, l), b = epoint(target, tedge, l+1);
          if (!matches(epoint(source,sedge,k), a) || !matches(epoint(source,sedge,next), b)) continue;
          if (fabs(fr(a.x-b.x)) < 0.01 && fabs(fr(a.y-b.y)) < 0.01 && fabs(fr(a.z-b.z)) < 0.01) continue;
          int ei = (rev && k+1 == sedge.count) ? middle : k+1;
          int ecount = sedge.errors == source->width_lod_error ? (int)source->width : (int)source->height;
          double err = eerr(sedge.errors, ecount, ei);
          qa_patch_mesh out;
          qa_insert_patch_strip(target, tedge.width, l+1, tedge.boundary, epoint(source, sedge, middle), err, &out);
          qa_patch_mesh_destroy(target); *target = out;
          return true;
        }
      }
    }
  }
  return false;
}
static void synchronize(qa_patch_grid *grids, size_t count, size_t start, size_t si) {
  /* iterative-safe recursion mirror (depth bounded by grid count) */
  qa_patch_grid *source = &grids[si];
  for (size_t j = start; j < count; j++) {
    qa_patch_grid *target = &grids[j];
    if (target->lod_fixed == 2 || !same_group(source->lod_origin, source->lod_radius, target)) continue;
    bool touch = false;
    edge sedges[4]; edges_of(&source->mesh, sedges);
    for (int se = 0; se < 4; se++) {
      edge sedge = sedges[se]; if (merged(&source->mesh, sedge)) continue;
      for (int k = 1; k < sedge.count-1; k++) {
        edge tedges[4]; edges_of(&target->mesh, tedges);
        for (int te = 0; te < 4; te++) {
          edge tedge = tedges[te]; if (merged(&target->mesh, tedge)) continue;
          for (int l = 1; l < tedge.count-1; l++) {
            if (!matches(epoint(&source->mesh,sedge,k), epoint(&target->mesh,tedge,l))) continue;
            double *errs = tedge.width ? target->mesh.width_lod_error : target->mesh.height_lod_error;
            errs[l] = sedge.errors[k]; touch = true;
          }
        }
      }
    }
    if (touch) { target->lod_fixed = 2; synchronize(grids, count, start, j); }
  }
}
void qa_prepare_patch_grids(qa_patch_grid *grids, size_t count, qa_scene_count_fn on_stitched, void *sctx, qa_scene_grid_fn on_replaced, void *rctx) {
  for (size_t i = 0; i < count; i++) { grids[i].lod_fixed = 0; grids[i].lod_stitched = false; }
  int32_t stitch_count = 0; bool visited;
  do {
    visited = false;
    for (size_t i = 0; i < count; i++) {
      if (grids[i].lod_stitched) continue;
      grids[i].lod_stitched = true; visited = true;
      qa_vec3 go = grids[i].lod_origin; double gr = grids[i].lod_radius;
      for (size_t j = 0; j < count; j++) {
        if (!same_group(go, gr, &grids[j])) continue;
        while (stitch(&grids[i].mesh, &grids[j].mesh)) {
          /* TS replaces grids[j] with a fresh record (lodFixed 0, lodStitched false), the source grid included when
           * it stitched itself: R_CreateSurfaceGridMesh zeroes the new grid, so it is visited again. */
          grids[j].lod_fixed = 0; grids[j].lod_stitched = false;
          if (on_replaced) on_replaced(rctx, (double)j, &grids[j]);
          stitch_count = (int32_t)((uint32_t)stitch_count + 1u); /* (stitchCount + 1) | 0 */
        }
      }
    }
  } while (visited);
  if (on_stitched) on_stitched(sctx, stitch_count);
  for (size_t i = 0; i < count; i++) { if (grids[i].lod_fixed != 0) continue; grids[i].lod_fixed = 2; synchronize(grids, count, i+1, i); }
}
bool qa_select_patch_lod(const qa_patch_grid *grid, qa_vec3 world_origin, qa_vec3 view_origin, qa_vec3 view_forward, double curve_error, qa_patch_mesh *out) {
  double distance = qa_dot3(qa_sub3(world_origin, view_origin), view_forward);
  if (distance < 0) distance = -distance;
  distance = fr(distance - grid->lod_radius); if (distance < 1) distance = 1;
  double error = curve_error < 0 ? 0 : fr(curve_error / distance);
  const qa_patch_mesh *mesh = &grid->mesh;
  int cols[65], rows[65]; int nc = 0, nr = 0;
  cols[nc++] = 0;
  for (int i = 1; i < (int)mesh->width-1; i++) if (mesh->width_lod_error[i] <= error) cols[nc++] = i;
  cols[nc++] = (int)mesh->width-1;
  rows[nr++] = 0;
  for (int i = 1; i < (int)mesh->height-1; i++) if (mesh->height_lod_error[i] <= error) rows[nr++] = i;
  rows[nr++] = (int)mesh->height-1;
  memset(out, 0, sizeof *out);
  if (nc == (int)mesh->width && nr == (int)mesh->height) {
    /* TS returns the same mesh; C returns an owned copy. */
    out->width = mesh->width; out->height = mesh->height; out->vertex_count = mesh->vertex_count; out->index_count = mesh->index_count;
    out->vertices = xalloc((out->vertex_count?out->vertex_count:1)*sizeof(qa_q3_bsp_vertex)); memcpy(out->vertices, mesh->vertices, out->vertex_count*sizeof(qa_q3_bsp_vertex));
    out->indices = xalloc((out->index_count?out->index_count:1)*sizeof(double)); memcpy(out->indices, mesh->indices, out->index_count*sizeof(double));
    out->width_lod_error = xalloc((size_t)(int)mesh->width*sizeof(double)); memcpy(out->width_lod_error, mesh->width_lod_error, (size_t)(int)mesh->width*sizeof(double));
    out->height_lod_error = xalloc((size_t)(int)mesh->height*sizeof(double)); memcpy(out->height_lod_error, mesh->height_lod_error, (size_t)(int)mesh->height*sizeof(double));
    return true;
  }
  out->width = nc; out->height = nr; out->vertex_count = (size_t)(nc*nr);
  out->vertices = xalloc(out->vertex_count*sizeof(qa_q3_bsp_vertex));
  for (int y = 0; y < nr; y++) for (int x = 0; x < nc; x++) out->vertices[y*nc+x] = mesh->vertices[rows[y]*(int)mesh->width+cols[x]];
  out->index_count = (size_t)((nc-1)*(nr-1)*6); out->indices = xalloc((out->index_count?out->index_count:1)*sizeof(double));
  size_t ii = 0;
  for (int y = 0; y < nr-1; y++) for (int x = 0; x < nc-1; x++) { int a=y*nc+x,b=a+nc; out->indices[ii++]=a;out->indices[ii++]=b;out->indices[ii++]=a+1;out->indices[ii++]=a+1;out->indices[ii++]=b;out->indices[ii++]=b+1; }
  out->width_lod_error = xalloc((size_t)nc*sizeof(double)); for (int i = 0; i < nc; i++) out->width_lod_error[i] = mesh->width_lod_error[cols[i]];
  out->height_lod_error = xalloc((size_t)nr*sizeof(double)); for (int i = 0; i < nr; i++) out->height_lod_error[i] = mesh->height_lod_error[rows[i]];
  return true;
}
