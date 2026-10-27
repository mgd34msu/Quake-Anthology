/* See header. GPL-2.0-or-later. */
#include "qa/bots/navigation/aas_optimize.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
typedef struct {
  const qa_aas_asset *asset; bool oom;
  qa_vec3 *verts; size_t vn, vc; qa_aas_edge_rec *edges; size_t en, ec;
  qa_aas_face_rec *faces; size_t fn, fc; int64_t *eidx; size_t ein, eic, *fidx_dummy;
  int64_t *fidx; size_t fin, fic; int64_t *vmap, *emap, *fmap;
} Opt;
static void *grow(void *p, size_t *cap, size_t es, bool *oom) { size_t nc = *cap ? *cap * 2 : 16; void *q = realloc(p, nc * es); if (!q) { *oom = true; return p; } *cap = nc; return q; }
static int64_t opt_vertex(Opt *o, int64_t n) {
  if (n < 0 || n >= (int64_t)o->asset->vertex_count) qa_range_error("AAS optimization index %lld", (long long)n);
  int64_t m = o->vmap[n]; if (m > 0) return m;
  if (o->vn == o->vc) o->verts = grow(o->verts, &o->vc, sizeof *o->verts, &o->oom);
  int64_t r = (int64_t)o->vn; o->verts[o->vn++] = o->asset->vertices[n]; o->vmap[n] = r; return r;
}
static int64_t opt_edge(Opt *o, int64_t n) {
  int64_t a = n < 0 ? -n : n; if (a >= (int64_t)o->asset->edge_count) qa_range_error("AAS optimization index %lld", (long long)a);
  int64_t r = o->emap[a];
  if (r < 0) { r = (int64_t)o->en; if (o->en == o->ec) o->edges = grow(o->edges, &o->ec, sizeof *o->edges, &o->oom);
    o->edges[o->en].vertices[0] = opt_vertex(o, o->asset->edges[a].vertices[0]); o->edges[o->en].vertices[1] = opt_vertex(o, o->asset->edges[a].vertices[1]); o->en++; o->emap[a] = r; }
  return n > 0 ? r : -r;
}
static int64_t opt_face(Opt *o, int64_t n) {
  int64_t a = n < 0 ? -n : n; if (a >= (int64_t)o->asset->face_count) qa_range_error("AAS optimization index %lld", (long long)a);
  const qa_aas_face_rec *s = &o->asset->faces[a]; if ((s->flags & 2) == 0) return 0;
  int64_t r = o->fmap[a];
  if (r < 0) { int64_t fe = (int64_t)o->ein;
    for (int64_t i = 0; i < s->edge_count; i++) { int64_t ei = s->first_edge + i; if (ei < 0 || ei >= (int64_t)o->asset->edge_index_count) qa_range_error("AAS optimization index %lld", (long long)ei);
      if (o->ein == o->eic) o->eidx = grow(o->eidx, &o->eic, sizeof *o->eidx, &o->oom);
      o->eidx[o->ein++] = opt_edge(o, o->asset->edge_indexes[ei]); }
    r = (int64_t)o->fn; if (o->fn == o->fc) o->faces = grow(o->faces, &o->fc, sizeof *o->faces, &o->oom);
    qa_aas_face_rec nf = *s; nf.first_edge = fe; nf.edge_count = (int64_t)o->ein - fe; o->faces[o->fn++] = nf; o->fmap[a] = r; }
  return n > 0 ? r : -r;
}
static int64_t *newmap(size_t n) { int64_t *m = malloc((n ? n : 1) * sizeof *m); if (m) for (size_t i = 0; i < n; i++) m[i] = -1; return m; }
static void *duparr(const void *p, size_t n, size_t es) { if (!n) return NULL; void *q = malloc(n * es); if (q) memcpy(q, p, n * es); return q; }
bool qa_optimize_aas(qa_aas_asset *out, const qa_aas_asset *asset) {
  memset(out, 0, sizeof *out); Opt o; memset(&o, 0, sizeof o); o.asset = asset;
  o.edges = grow(o.edges, &o.ec, sizeof *o.edges, &o.oom); o.edges[o.en++] = (qa_aas_edge_rec){{0, 0}};
  o.faces = grow(o.faces, &o.fc, sizeof *o.faces, &o.oom); o.faces[o.fn++] = (qa_aas_face_rec){0,0,0,0,0,0};
  o.vmap = newmap(asset->vertex_count); o.emap = newmap(asset->edge_count); o.fmap = newmap(asset->face_count);
  if (!o.vmap || !o.emap || !o.fmap) return false;
  qa_aas_area_rec *areas = malloc((asset->area_count ? asset->area_count : 1) * sizeof *areas); if (!areas && asset->area_count) return false;
  for (size_t i = 0; i < asset->area_count; i++) { if (i == 0) { memset(&areas[i], 0, sizeof areas[i]); continue; } int64_t ff = (int64_t)o.fin;
    for (int64_t k = 0; k < asset->areas[i].face_count; k++) { int64_t fi = asset->areas[i].first_face + k; if (fi < 0 || fi >= (int64_t)asset->face_index_count) qa_range_error("AAS optimization index %lld", (long long)fi);
      int64_t m = opt_face(&o, asset->face_indexes[fi]); if (m != 0) { if (o.fin == o.fic) o.fidx = grow(o.fidx, &o.fic, sizeof *o.fidx, &o.oom); o.fidx[o.fin++] = m; } }
    areas[i] = asset->areas[i]; areas[i].first_face = ff; areas[i].face_count = (int64_t)o.fin - ff; }
  if (o.oom) return false;
  qa_aas_reach_rec *reach = duparr(asset->reachability, asset->reachability_count, sizeof *reach);
  for (size_t i = 0; i < asset->reachability_count; i++) { int64_t t = reach[i].travel_type & 0xffffff; if (t == 11 || t == 18 || t == 19) continue;
    int64_t fa = reach[i].face < 0 ? -reach[i].face : reach[i].face, ea = reach[i].edge < 0 ? -reach[i].edge : reach[i].edge;
    reach[i].face = (reach[i].face < 0 ? -1 : 1) * (fa < (int64_t)asset->face_count && o.fmap[fa] >= 0 ? o.fmap[fa] : 0);
    reach[i].edge = (reach[i].edge < 0 ? -1 : 1) * (ea < (int64_t)asset->edge_count && o.emap[ea] >= 0 ? o.emap[ea] : 0); }
  out->source = asset->source ? duparr(asset->source, strlen(asset->source) + 1, 1) : NULL; out->version = asset->version; out->bsp_checksum = asset->bsp_checksum;
  out->bboxes = duparr(asset->bboxes, asset->bbox_count, sizeof *out->bboxes); out->bbox_count = asset->bbox_count;
  out->vertices = o.verts; out->vertex_count = o.vn; out->edges = o.edges; out->edge_count = o.en;
  out->edge_indexes = o.eidx; out->edge_index_count = o.ein; out->faces = o.faces; out->face_count = o.fn;
  out->face_indexes = o.fidx; out->face_index_count = o.fin; out->areas = areas; out->area_count = asset->area_count;
  out->planes = duparr(asset->planes, asset->plane_count, sizeof *out->planes); out->plane_count = asset->plane_count;
  out->settings = duparr(asset->settings, asset->setting_count, sizeof *out->settings); out->setting_count = asset->setting_count;
  out->reachability = reach; out->reachability_count = asset->reachability_count;
  out->nodes = duparr(asset->nodes, asset->node_count, sizeof *out->nodes); out->node_count = asset->node_count;
  out->portals = duparr(asset->portals, asset->portal_count, sizeof *out->portals); out->portal_count = asset->portal_count;
  out->portal_indexes = duparr(asset->portal_indexes, asset->portal_index_count, sizeof *out->portal_indexes); out->portal_index_count = asset->portal_index_count;
  out->clusters = duparr(asset->clusters, asset->cluster_count, sizeof *out->clusters); out->cluster_count = asset->cluster_count;
  free(o.vmap); free(o.emap); free(o.fmap); return true;
}
