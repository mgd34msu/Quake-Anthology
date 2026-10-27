/* Port of src/render/scene/shadows.ts. GPL-2.0-or-later. */
#include "qa/render/scene/shadows.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/foundation/support.h"
static float fr(double v) { return (float)v; }
static void *xalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("Allocation failed"); return p; }
static void *xg(void *p, size_t n) { void *q = realloc(p, n ? n : 1); if (!q) qa_range_error("Allocation failed"); return q; }
const qa_shadow_face_basis qa_q2_shadow_cube_faces[6] = {
  { {1,0,0},{0,-1,0},{0,0,1} }, { {-1,0,0},{0,1,0},{0,0,1} }, { {0,1,0},{1,0,0},{0,0,1} },
  { {0,-1,0},{-1,0,0},{0,0,1} }, { {0,0,1},{0,1,0},{1,0,0} }, { {0,0,-1},{0,-1,0},{1,0,0} },
};
static double dot(qa_vec3 a, qa_vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static qa_vec3 vcross(qa_vec3 a, qa_vec3 b) { return (qa_vec3){ a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x }; }
static qa_vec3 diff3(qa_vec3 a, qa_vec3 b) { return (qa_vec3){ a.x-b.x, a.y-b.y, a.z-b.z }; }
static double vlen(qa_vec3 a) { return hypot(hypot(a.x, a.y), a.z); }
static qa_vec3 unit(qa_vec3 a) { double s = vlen(a); return (qa_vec3){ a.x/s, a.y/s, a.z/s }; }
void qa_scene_shadow_mesh_owned_destroy(qa_scene_shadow_mesh_owned *m) { if (!m) return; free(m->owned_positions); memset(m, 0, sizeof *m); }
bool qa_shadow_mesh_new(qa_material_geometry geometry, qa_scene_shadow_mesh_owned *out) {
  memset(out, 0, sizeof *out);
  out->owned_positions = xalloc((geometry.vertex_count ? geometry.vertex_count : 1) * sizeof(qa_vec3));
  for (size_t i = 0; i < geometry.vertex_count; i++) out->owned_positions[i] = geometry.vertices[i].position;
  out->mesh.positions = out->owned_positions; out->mesh.position_count = geometry.vertex_count;
  out->mesh.indices = geometry.indices; out->mesh.index_count = geometry.index_count;
  return true;
}
qa_shadow_caster qa_shadow_caster_new(qa_vec3 origin, const qa_shadow_mesh *meshes, size_t count) {
  double r2 = 0;
  for (size_t i = 0; i < count; i++) for (size_t k = 0; k < meshes[i].position_count; k++) {
    double dx = meshes[i].positions[k].x - origin.x, dy = meshes[i].positions[k].y - origin.y, dz = meshes[i].positions[k].z - origin.z;
    double d = dx*dx + dy*dy + dz*dz; if (d > r2) r2 = d;
  }
  double radius = sqrt(r2);
  return (qa_shadow_caster){ origin, radius > 0 ? radius : 64, meshes, count };
}
double qa_shadow_map_resolution(double requested, double cap) {
  double maximum = cap > 0 ? cap : 1024; if (maximum > 1024) maximum = 1024; if (maximum < 128) maximum = 128;
  double wanted = requested > 0 ? requested : 512; if (wanted < 128) wanted = 128; if (wanted > maximum) wanted = maximum;
  return exp2(floor(log2(wanted)));
}
bool qa_pack_shadow_atlas(const qa_shadow_pack_request *requests, size_t count, bool has_size, double size, qa_shadow_pack_result *out) {
  if (!has_size) size = QA_Q2_SHADOW_ATLAS_SIZE;
  double x = 0, y = 0, shelf = 0;
  for (size_t i = 0; i < count; i++) {
    double w = requests[i].width, h = requests[i].height;
    if (w <= 0 || h <= 0 || w > size || h > size) { out[i].is_null = true; continue; }
    if (x + w > size) { y += shelf; shelf = 0; x = 0; }
    if (y + h > size) { out[i].is_null = true; continue; }
    out[i].is_null = false; out[i].rect = (qa_rect){ x, y, w, h };
    x += w; if (h > shelf) shelf = h;
  }
  return true;
}
typedef struct candidate { size_t index; qa_q2_fragment_light light; double face; const qa_shadow_caster *casters; size_t caster_count; qa_shadow_caster *owned_casters; } candidate;
static qa_shadow_pack_request extent_of(candidate *c) {
  return c->light.cone ? (qa_shadow_pack_request){ c->face, c->face } : (qa_shadow_pack_request){ c->face * 3, c->face * 2 };
}
static int cmp_candidate(const void *a, const void *b) {
  qa_shadow_pack_request x = extent_of((candidate *)a), y = extent_of((candidate *)b);
  double av = x.width * x.height, bv = y.width * y.height;
  return bv > av ? 1 : bv < av ? -1 : 0;
}
static void fit_candidates(candidate *cands, size_t n, qa_shadow_pack_result *slots) {
  /* TS fit sorts candidates in place (observable: iteration order after fit). */
  for (int attempt = -1; attempt < 4; attempt++) {
    qsort(cands, n, sizeof(candidate), cmp_candidate);
    qa_shadow_pack_request *reqs = xalloc((n ? n : 1) * sizeof *reqs);
    for (size_t i = 0; i < n; i++) reqs[i] = extent_of(&cands[i]);
    qa_pack_shadow_atlas(reqs, n, false, 0, slots);
    free(reqs);
    int any_null = 0; for (size_t i = 0; i < n; i++) if (slots[i].is_null) any_null = 1;
    if (!any_null || attempt == 3) return;
    int shrank = 0;
    for (size_t i = 0; i < n; i++) if (!cands[i].light.cone && cands[i].face > 128) { cands[i].face /= 2; shrank = 1; }
    if (!shrank) return;
  }
}
qa_mat4 qa_shadow_matrix_multiply(qa_mat4 a, qa_mat4 b) {
  qa_mat4 r;
  for (int col = 0; col < 4; col++) for (int row = 0; row < 4; row++) {
    double sum = 0; for (int k = 0; k < 4; k++) sum += a.values[k*4+row] * b.values[col*4+k];
    r.values[col*4+row] = fr(sum);
  }
  return r;
}
qa_vec4 qa_shadow_project(qa_mat4 m, qa_vec3 p) {
  return qa_math_vec4(m.values[0]*p.x + m.values[4]*p.y + m.values[8]*p.z + m.values[12],
    m.values[1]*p.x + m.values[5]*p.y + m.values[9]*p.z + m.values[13],
    m.values[2]*p.x + m.values[6]*p.y + m.values[10]*p.z + m.values[14],
    m.values[3]*p.x + m.values[7]*p.y + m.values[11]*p.z + m.values[15]);
}
static qa_mat4 perspective(double fov, double radius) {
  double scale = 1 / tan(fov * 3.141592653589793 / 360);
  double far_p = radius > 8 ? radius : 8, near_p = QA_Q2_SHADOW_NEAR;
  qa_mat4 r = { { fr(scale),0,0,0, 0,fr(scale),0,0, 0,0,fr((far_p+near_p)/(near_p-far_p)),-1, 0,0,fr(2*far_p*near_p/(near_p-far_p)),0 } };
  return r;
}
static qa_mat4 view_mat(qa_vec3 origin, qa_shadow_face_basis b) {
  qa_vec3 r3 = b.right, u = b.up, v = b.forward;
  qa_mat4 r = { { fr(r3.x),fr(u.x),fr(-v.x),0, fr(r3.y),fr(u.y),fr(-v.y),0, fr(r3.z),fr(u.z),fr(-v.z),0,
    fr(-dot(r3, origin)), fr(-dot(u, origin)), fr(dot(v, origin)), 1 } };
  return r;
}
static qa_shadow_face_basis cone_basis(qa_vec3 direction) {
  qa_vec3 forward = vlen(direction) < 1e-6 ? (qa_vec3){ 0, 0, -1 } : unit(direction);
  qa_vec3 reference = fabs(forward.z) >= 0.99 ? (qa_vec3){ 1, 0, 0 } : (qa_vec3){ 0, 0, 1 };
  qa_vec3 right = unit(vcross(forward, reference));
  return (qa_shadow_face_basis){ forward, right, vcross(right, forward) };
}
double qa_shadow_cone_fov(double cos_half) {
  double c = cos_half < -1 ? -1 : cos_half > 1 ? 1 : cos_half;
  double v = acos(c) * 180 / 3.141592653589793 * 2 * 1.15;
  return v < 175 ? v : 175;
}
qa_mat4 qa_shadow_cone_matrix(qa_q2_fragment_light light) {
  if (!light.cone) qa_range_error("A cone projection requires a spotlight direction");
  return qa_shadow_matrix_multiply(perspective(qa_shadow_cone_fov(light.cone->cos_half_angle), light.radius),
                                   view_mat(light.origin, cone_basis(light.cone->direction)));
}
typedef struct contains { qa_vec3 origin; double radius, limit; int has_cone; qa_vec3 direction; } contains_t;
static contains_t make_contains(qa_vec3 origin, double radius, int has_cone, qa_vec3 dir, double cos_half) {
  contains_t t; t.origin = origin; t.radius = radius; t.has_cone = has_cone; t.direction = dir;
  double diagonal = !has_cone ? 3.141592653589793
    : atan(1.4142135623730951 * tan(fmin(qa_shadow_cone_fov(cos_half) * 3.141592653589793 / 360, 87 * 3.141592653589793 / 180)));
  t.limit = cos(diagonal);
  return t;
}
static bool contains_test(contains_t t, qa_shadow_sphere caster) {
  qa_vec3 delta = diff3(caster.origin, t.origin); double distance = vlen(delta);
  if (distance > t.radius + caster.radius) return false;
  if (t.has_cone && distance > caster.radius) {
    double c = dot(delta, t.direction) / distance; c = c < -1 ? -1 : c > 1 ? 1 : c;
    double angle = acos(c);
    double rc = caster.radius / distance; rc = rc < 1 ? rc : 1;
    double separation = angle - asin(rc);
    double cl = separation < 0 ? 0 : separation > 3.141592653589793 ? 3.141592653589793 : separation;
    if (cos(cl) < t.limit) return false;
  }
  return true;
}
static bool eligible(qa_scene_light light) {
  return light.profile.kind && !strcmp(light.profile.kind, "q2") && !(light.radius <= 0)
    && (light.profile.shadow_cast || light.profile.has_cone);
}
qa_shadow_body_filter qa_shadow_body_filter_new(const qa_scene_light *source, size_t count) {
  qa_shadow_body_filter f; memset(&f, 0, sizeof f);
  size_t seen_q2 = 0;
  for (size_t i = 0; i < count && seen_q2 < 8; i++) {
    if (!source[i].profile.kind || strcmp(source[i].profile.kind, "q2")) continue;
    seen_q2++;
    if (!eligible(source[i])) continue;
    size_t k = f.count++;
    f.origins[k] = source[i].origin; f.radii[k] = source[i].radius;
    f.has_cone[k] = source[i].profile.has_cone;
    f.cone_direction[k] = source[i].profile.cone_direction; f.cone_cos[k] = source[i].profile.cone_cos_half_angle;
  }
  return f;
}
bool qa_shadow_body_filter_test(qa_shadow_body_filter f, qa_shadow_sphere sphere) {
  for (size_t i = 0; i < f.count; i++) {
    contains_t t = make_contains(f.origins[i], f.radii[i], f.has_cone[i], f.cone_direction[i], f.cone_cos[i]);
    if (contains_test(t, sphere)) return true;
  }
  return false;
}
/* mesh sphere (no cross-call WeakMap; same values) */
static qa_shadow_sphere mesh_sphere(const qa_shadow_mesh *mesh) {
  double mnx = INFINITY, mny = INFINITY, mnz = INFINITY, mxx = -INFINITY, mxy = -INFINITY, mxz = -INFINITY;
  for (size_t i = 0; i < mesh->position_count; i++) {
    qa_vec3 p = mesh->positions[i];
    if (p.x < mnx) mnx = p.x;
    if (p.x > mxx) mxx = p.x;
    if (p.y < mny) mny = p.y;
    if (p.y > mxy) mxy = p.y;
    if (p.z < mnz) mnz = p.z;
    if (p.z > mxz) mxz = p.z;
  }
  qa_vec3 center = mesh->position_count == 0 ? (qa_vec3){ 0, 0, 0 } : (qa_vec3){ (mnx+mxx)/2, (mny+mxy)/2, (mnz+mxz)/2 };
  double radius = 0;
  for (size_t i = 0; i < mesh->position_count; i++) { double d = vlen(diff3(mesh->positions[i], center)); if (d > radius) radius = d; }
  return (qa_shadow_sphere){ center, radius };
}
static bool world_visible(const qa_shadow_mesh *mesh, qa_q2_fragment_light light, const qa_vec3 *forward) {
  qa_shadow_sphere sphere = mesh_sphere(mesh); qa_vec3 delta = diff3(sphere.origin, light.origin);
  double m = light.radius > 8 ? light.radius : 8;
  if (vlen(delta) > m + sphere.radius) return false;
  return !forward || dot(delta, *forward) + sphere.radius > 0;
}
static qa_depth_atlas_draw make_draw(const qa_shadow_mesh *mesh, qa_mat4 matrix, int entity, double **owned_positions, qa_polygon_offset *owned_offset) {
  double *positions = xalloc((mesh->position_count ? mesh->position_count * 4 : 1) * sizeof(double));
  const double *v = matrix.values;
  for (size_t i = 0; i < mesh->position_count; i++) {
    qa_vec3 p = mesh->positions[i]; size_t o = i * 4;
    positions[o] = v[0]*p.x + v[4]*p.y + v[8]*p.z + v[12];
    positions[o+1] = v[1]*p.x + v[5]*p.y + v[9]*p.z + v[13];
    positions[o+2] = v[2]*p.x + v[6]*p.y + v[10]*p.z + v[14];
    positions[o+3] = v[3]*p.x + v[7]*p.y + v[11]*p.z + v[15];
  }
  *owned_positions = positions;
  *owned_offset = (qa_polygon_offset){ entity ? 1.0 : 2.0, entity ? 2.0 : 4.0 };
  qa_depth_atlas_draw d; memset(&d, 0, sizeof d);
  d.positions = positions; d.position_count = mesh->position_count * 4;
  d.indices.values = mesh->indices; d.indices.count = mesh->index_count;
  d.cull = "none"; d.polygon_offset = NULL; /* set by caller to owned storage */
  return d;
}
static float f32of(double v) { return (float)v; }
static void digest_meshes(const qa_shadow_mesh *meshes, size_t n, char out[16]) {
  uint32_t hash = 0x811c9dc5u;
  for (size_t i = 0; i < n; i++) {
    float fv; uint32_t bits;
    fv = f32of((double)meshes[i].position_count); memcpy(&bits, &fv, 4); hash = (hash ^ bits) * 0x01000193u;
    fv = f32of((double)meshes[i].index_count); memcpy(&bits, &fv, 4); hash = (hash ^ bits) * 0x01000193u;
    for (size_t k = 0; k < meshes[i].position_count; k++) {
      double comps[3] = { meshes[i].positions[k].x, meshes[i].positions[k].y, meshes[i].positions[k].z };
      for (int c = 0; c < 3; c++) { fv = f32of(comps[c]); memcpy(&bits, &fv, 4); hash = (hash ^ bits) * 0x01000193u; }
    }
    for (size_t k = 0; k < meshes[i].index_count; k++) {
      /* JS: hash ^ index with int32 coercion */
      int32_t ii = (int32_t)(isfinite(meshes[i].indices[k]) ? fmod(trunc(meshes[i].indices[k]), 4294967296.0) : 0);
      hash = (hash ^ (uint32_t)ii) * 0x01000193u;
    }
  }
  snprintf(out, 16, "%x", hash);
}
bool qa_static_shadow_world_init(qa_static_shadow_world *w, const qa_shadow_mesh *meshes, size_t count) {
  memset(w, 0, sizeof *w);
  if (count > 64) {
    /* fall back to heap blocks tracked past the fixed table via mesh copies only */
  }
  w->meshes = xalloc((count ? count : 1) * sizeof *w->meshes); w->mesh_count = count;
  w->owned_count = count < 64 ? count : 64;
  for (size_t i = 0; i < count; i++) {
    qa_vec3 *pos = xalloc((meshes[i].position_count ? meshes[i].position_count : 1) * sizeof(qa_vec3));
    memcpy(pos, meshes[i].positions, meshes[i].position_count * sizeof(qa_vec3));
    double *idx = xalloc((meshes[i].index_count ? meshes[i].index_count : 1) * sizeof(double));
    memcpy(idx, meshes[i].indices, meshes[i].index_count * sizeof(double));
    if (i < 64) { w->owned_positions[i] = pos; w->owned_indices[i] = idx; }
    w->meshes[i] = (qa_shadow_mesh){ pos, meshes[i].position_count, idx, meshes[i].index_count };
  }
  return true;
}
void qa_static_shadow_world_destroy(qa_static_shadow_world *w) {
  if (!w) return;
  for (size_t i = 0; i < w->mesh_count; i++) {
    free((void *)w->meshes[i].positions); free((void *)w->meshes[i].indices);
  }
  free(w->meshes); free(w->digest); memset(w, 0, sizeof *w);
}
const char *qa_static_shadow_world_digest(qa_static_shadow_world *w) {
  if (!w->digest) { char buf[16]; digest_meshes(w->meshes, w->mesh_count, buf); w->digest = xalloc(strlen(buf) + 1); strcpy(w->digest, buf); }
  return w->digest;
}
/* signatures */
typedef struct csig { double ox, oy, oz, radius; char key[16]; } csig;
typedef struct lsig {
  double ox, oy, oz, radius; int has_cone; double dx, dy, dz, cos_v;
  double slot_x, slot_y, slot_w, slot_h; char world_key[16];
  csig *casters; size_t caster_count;
} lsig;
typedef struct cache_entry { double index; lsig sig; bool present; } cache_entry;
typedef struct scene_cache { cache_entry entries[8]; size_t count;
  /* world draw cache keyed by string */
  struct wd { char *key; qa_depth_atlas_draw *draws; size_t draw_count; double **positions; qa_polygon_offset *offsets; } *wd; size_t wd_count, wd_cap;
} scene_cache;
void qa_prepared_shadows_destroy(qa_prepared_shadows *p) {
  if (!p) return;
  free(p->lighting.lights);
  if (p->pass_positions) for (size_t i = 0; i < p->pass_count; i++) {
    double **ptrs = (double **)p->pass_positions[i];
    if (ptrs) { for (size_t k = 0; k < p->passes[i].draw_count; k++) free(ptrs[k]); free(ptrs); }
  }
  if (p->pass_draws) for (size_t i = 0; i < p->pass_count; i++) free(p->pass_draws[i]);
  free(p->pass_positions); free(p->pass_draws); free(p->passes); free(p->operations);
  memset(p, 0, sizeof *p);
}
void qa_q2_shadow_scene_init(qa_q2_shadow_scene *s, qa_scene_image_registry *images) {
  memset(s, 0, sizeof *s); s->images = images;
  scene_cache *c = xalloc(sizeof *c); memset(c, 0, sizeof *c); s->_cached = c; s->_world_draw_cache = c;
}
static void free_cache(scene_cache *c) {
  for (size_t i = 0; i < c->count; i++) free(c->entries[i].sig.casters);
  for (size_t i = 0; i < c->wd_count; i++) {
    free(c->wd[i].key);
    if (c->wd[i].positions) for (size_t k = 0; k < c->wd[i].draw_count; k++) free(c->wd[i].positions[k]);
    free(c->wd[i].positions); free(c->wd[i].offsets); free(c->wd[i].draws);
  }
  free(c->wd);
}
void qa_q2_shadow_scene_destroy(qa_q2_shadow_scene *s) {
  if (!s) return;
  if (s->_cached) { free_cache(s->_cached); free(s->_cached); }
  memset(s, 0, sizeof *s);
}
static int same_number(double a, double b) { return a == b || (a != a && b != b); }
static int same_signature(const lsig *a, int present, const lsig *b) {
  if (!present) return 0;
  if (!same_number(a->ox,b->ox)||!same_number(a->oy,b->oy)||!same_number(a->oz,b->oz)||!same_number(a->radius,b->radius)) return 0;
  if (a->has_cone != b->has_cone) return 0;
  if (a->has_cone && (!same_number(a->dx,b->dx)||!same_number(a->dy,b->dy)||!same_number(a->dz,b->dz)||!same_number(a->cos_v,b->cos_v))) return 0;
  if (!same_number(a->slot_x,b->slot_x)||!same_number(a->slot_y,b->slot_y)||!same_number(a->slot_w,b->slot_w)||!same_number(a->slot_h,b->slot_h)) return 0;
  if (strcmp(a->world_key, b->world_key) || a->caster_count != b->caster_count) return 0;
  for (size_t i = 0; i < a->caster_count; i++) {
    if (strcmp(a->casters[i].key, b->casters[i].key) || !same_number(a->casters[i].ox,b->casters[i].ox)
      || !same_number(a->casters[i].oy,b->casters[i].oy) || !same_number(a->casters[i].oz,b->casters[i].oz)
      || !same_number(a->casters[i].radius,b->casters[i].radius)) return 0;
  }
  return 1;
}
static struct wd *world_draws_cached(scene_cache *c, const char *key,
    const qa_shadow_mesh *world, size_t world_count, qa_q2_fragment_light light, const qa_vec3 *forward, qa_mat4 matrix) {
  for (size_t i = 0; i < c->wd_count; i++) if (!strcmp(c->wd[i].key, key)) return &c->wd[i];
  if (c->wd_count >= 256) { for (size_t i = 0; i < c->wd_count; i++) { free(c->wd[i].key); if (c->wd[i].positions) for (size_t k = 0; k < c->wd[i].draw_count; k++) free(c->wd[i].positions[k]); free(c->wd[i].positions); free(c->wd[i].offsets); free(c->wd[i].draws); } c->wd_count = 0; }
  if (c->wd_count == c->wd_cap) { c->wd_cap = c->wd_cap ? c->wd_cap * 2 : 8; c->wd = xg(c->wd, c->wd_cap * sizeof *c->wd); }
  struct wd *entry = &c->wd[c->wd_count++]; memset(entry, 0, sizeof *entry);
  entry->key = xalloc(strlen(key) + 1); strcpy(entry->key, key);
  size_t cap = 0;
  for (size_t i = 0; i < world_count; i++) {
    if (!world_visible(&world[i], light, forward)) continue;
    if (entry->draw_count == cap) {
      cap = cap ? cap * 2 : 8;
      entry->draws = xg(entry->draws, cap * sizeof *entry->draws);
      entry->positions = xg(entry->positions, cap * sizeof *entry->positions);
      entry->offsets = xg(entry->offsets, cap * sizeof *entry->offsets);
    }
    double *owned = NULL; qa_polygon_offset off;
    entry->draws[entry->draw_count] = make_draw(&world[i], matrix, 0, &owned, &off);
    entry->positions[entry->draw_count] = owned; entry->offsets[entry->draw_count] = off;
    entry->draws[entry->draw_count].polygon_offset = &entry->offsets[entry->draw_count];
    entry->draw_count++;
    /* offsets array may move on grow: rebind below */
  }
  for (size_t i = 0; i < entry->draw_count; i++) entry->draws[i].polygon_offset = &entry->offsets[i];
  return entry;
}
static void append_pass(qa_prepared_shadows *out, candidate *cand, qa_rect viewport, qa_mat4 matrix,
                        struct wd *world_draws_entry, const qa_vec3 *forward) {
  size_t cap = out->pass_count + 1;
  out->passes = xg(out->passes, cap * sizeof *out->passes);
  out->pass_draws = xg(out->pass_draws, cap * sizeof *out->pass_draws);
  out->pass_positions = xg(out->pass_positions, cap * sizeof *out->pass_positions);
  size_t pi = out->pass_count++;
  size_t total = world_draws_entry->draw_count;
  for (size_t i = 0; i < cand->caster_count; i++)
    if (!forward || dot(diff3(cand->casters[i].origin, cand->light.origin), *forward) + cand->casters[i].radius > 0) total += cand->casters[i].mesh_count;
  qa_depth_atlas_draw *draws = xalloc((total ? total : 1) * sizeof *draws);
  double **positions = xalloc((total ? total : 1) * sizeof(double *));
  memcpy(draws, world_draws_entry->draws, world_draws_entry->draw_count * sizeof *draws);
  for (size_t i = 0; i < world_draws_entry->draw_count; i++) positions[i] = NULL; /* borrowed from cache */
  size_t n = world_draws_entry->draw_count;
  for (size_t i = 0; i < cand->caster_count; i++) {
    if (forward && dot(diff3(cand->casters[i].origin, cand->light.origin), *forward) + cand->casters[i].radius <= 0) continue;
    for (size_t k = 0; k < cand->casters[i].mesh_count; k++) {
      double *owned = NULL; qa_polygon_offset off;
      draws[n] = make_draw(&cand->casters[i].meshes[k], matrix, 1, &owned, &off);
      positions[n] = owned;
      /* polygon_offset: point into positions-parallel scratch stored at draw tail via cache offsets */
      static const qa_polygon_offset ENTITY_OFF = { 1, 2 };
      draws[n].polygon_offset = &ENTITY_OFF;
      n++;
    }
  }
  out->pass_draws[pi] = draws; out->pass_positions[pi] = (double *)positions;
  qa_depth_atlas_pass *pass = &out->passes[pi]; memset(pass, 0, sizeof *pass);
  pass->viewport = viewport; pass->has_clear_depth = true; pass->clear_depth = 1;
  pass->draws = draws; pass->draw_count = n;
}
bool qa_q2_shadow_scene_prepare(qa_q2_shadow_scene *s, const qa_scene_light *source, size_t source_count,
  const qa_shadow_mesh *world_meshes, size_t world_count, qa_static_shadow_world *world_static,
  const qa_shadow_caster *casters, size_t caster_count, const qa_shadow_atlas_options *options_in, qa_prepared_shadows *out) {
  memset(out, 0, sizeof *out);
  qa_shadow_atlas_options options; memset(&options, 0, sizeof options);
  if (options_in) options = *options_in;
  /* q2 lights first 8 */
  const qa_scene_light *q2src[64]; size_t nq2all = 0;
  for (size_t i = 0; i < source_count && nq2all < 64; i++) if (source[i].profile.kind && !strcmp(source[i].profile.kind, "q2")) q2src[nq2all++] = &source[i];
  size_t nlights = nq2all < 8 ? nq2all : 8;
  qa_q2_fragment_light *lights = xalloc((nlights ? nlights : 1) * sizeof *lights);
  qa_q2_light_cone *cones = xalloc((nlights ? nlights : 1) * sizeof *cones);
  for (size_t i = 0; i < nlights; i++) {
    const qa_scene_light *l = q2src[i];
    memset(&lights[i], 0, sizeof lights[i]);
    lights[i].origin = l->origin; lights[i].radius = l->radius; lights[i].color = l->color; lights[i].scale = l->profile.scale;
    if (l->profile.has_cone) { cones[i] = (qa_q2_light_cone){ l->profile.cone_direction, l->profile.cone_cos_half_angle }; lights[i].cone = &cones[i]; }
    lights[i].shadow.kind = "none";
  }
  candidate cands[8]; size_t ncand = 0;
  int enabled = !options.has_enabled || options.enabled;
  if (enabled) for (size_t index = 0; index < nlights; index++) {
    const qa_scene_light *original = q2src[index];
    if (!eligible(*original)) continue;
    candidate *c = &cands[ncand]; memset(c, 0, sizeof *c);
    c->index = index; c->light = lights[index];
    double res = original->profile.shadow_cast ? original->profile.shadow_resolution : 0;
    c->face = qa_shadow_map_resolution(res, options.has_resolution_cap ? options.resolution_cap : 1024);
    contains_t test = make_contains(c->light.origin, c->light.radius, c->light.cone != NULL, c->light.cone ? c->light.cone->direction : (qa_vec3){0,0,0}, c->light.cone ? c->light.cone->cos_half_angle : 0);
    c->owned_casters = xalloc((caster_count ? caster_count : 1) * sizeof *c->owned_casters);
    size_t nc = 0;
    for (size_t k = 0; k < caster_count; k++) if (contains_test(test, (qa_shadow_sphere){ casters[k].origin, casters[k].radius })) c->owned_casters[nc++] = casters[k];
    c->casters = c->owned_casters; c->caster_count = nc;
    ncand++;
  }
  if (!enabled) qa_q2_shadow_scene_close(s);
  if (ncand == 0) {
    out->lighting.lights = xalloc((nlights ? nlights : 1) * sizeof *out->lighting.lights);
    memcpy(out->lighting.lights, lights, nlights * sizeof *lights);
    /* re-point cones into copies? lights cone pointers reference `cones` (leaked with lights block below replaced) */
    out->lighting.light_count = nlights; out->lighting_atlas = NULL;
    /* cones/lights stay allocated as the returned block's backing */
    return true;
  }
  if (!s->has_image) {
    size_t npix = 2048u * 2048u;
    float *pixels = xalloc(npix * sizeof(float));
    for (size_t i = 0; i < npix; i++) pixels[i] = 1;
    qa_depth_image_level level = { QA_Q2_SHADOW_ATLAS_SIZE, QA_Q2_SHADOW_ATLAS_SIZE, pixels, npix };
    qa_render_image content; memset(&content, 0, sizeof content);
    content.kind = QA_RENDER_IMAGE_DEPTH32F; content.depth_levels = &level; content.depth_level_count = 1;
    qa_texture_sampling sampling = { "clamp", "nearest" };
    s->image = qa_scene_image_registry_register_generated(s->images, "*q2-shadow-atlas", content, sampling);
    s->has_image = true;
    /* pixels/level ownership passes to the queued operation view (TS retains) */
  }
  qa_shadow_pack_result slots[8];
  fit_candidates(cands, ncand, slots);
  const qa_shadow_mesh *world = world_static ? world_static->meshes : world_meshes;
  size_t nworld = world_static ? world_static->mesh_count : world_count;
  char world_key[16];
  if (world_static) snprintf(world_key, sizeof world_key, "%s", qa_static_shadow_world_digest(world_static));
  else digest_meshes(world, nworld, world_key);
  scene_cache *cachep = (scene_cache *)s->_cached;
  cache_entry new_cache[8]; size_t new_count = 0;
  double cached_lights = 0, rebuilt_lights = 0, entity_casters = 0;
  static const qa_mat4 DEPTH_BIAS = { { 0.5,0,0,0, 0,0.5,0,0, 0,0,0.5,0, 0.5,0.5,0.5,1 } };
  for (size_t i = 0; i < ncand; i++) {
    candidate *cand = &cands[i];
    if (slots[i].is_null) continue;
    qa_rect slot = slots[i].rect;
    int has_matrix = cand->light.cone != NULL;
    qa_mat4 matrix = has_matrix ? qa_shadow_cone_matrix(cand->light) : (qa_mat4){ {0} };
    qa_vec4 atlas_rect = { slot.x / QA_Q2_SHADOW_ATLAS_SIZE, slot.y / QA_Q2_SHADOW_ATLAS_SIZE, slot.width / QA_Q2_SHADOW_ATLAS_SIZE, slot.height / QA_Q2_SHADOW_ATLAS_SIZE };
    qa_q2_shadow_projection shadow; memset(&shadow, 0, sizeof shadow);
    if (has_matrix) { shadow.kind = "cone"; shadow.atlas_rect = atlas_rect; shadow.matrix = qa_shadow_matrix_multiply(DEPTH_BIAS, matrix); }
    else { shadow.kind = "point"; shadow.atlas_rect = atlas_rect; }
    lights[cand->index].shadow = shadow;
    lsig sig; memset(&sig, 0, sizeof sig);
    sig.ox = cand->light.origin.x; sig.oy = cand->light.origin.y; sig.oz = cand->light.origin.z; sig.radius = cand->light.radius;
    sig.has_cone = cand->light.cone != NULL;
    if (sig.has_cone) { sig.dx = cand->light.cone->direction.x; sig.dy = cand->light.cone->direction.y; sig.dz = cand->light.cone->direction.z; sig.cos_v = cand->light.cone->cos_half_angle; }
    sig.slot_x = slot.x; sig.slot_y = slot.y; sig.slot_w = slot.width; sig.slot_h = slot.height;
    snprintf(sig.world_key, sizeof sig.world_key, "%s", world_key);
    sig.casters = xalloc((cand->caster_count ? cand->caster_count : 1) * sizeof(csig)); sig.caster_count = cand->caster_count;
    for (size_t k = 0; k < cand->caster_count; k++) {
      char key[16]; digest_meshes(cand->casters[k].meshes, cand->casters[k].mesh_count, key);
      sig.casters[k] = (csig){ cand->casters[k].origin.x, cand->casters[k].origin.y, cand->casters[k].origin.z, cand->casters[k].radius, {0} };
      snprintf(sig.casters[k].key, sizeof key, "%s", key);
    }
    /* find cached */
    const lsig *prev = NULL; int prev_present = 0;
    for (size_t k = 0; k < cachep->count; k++) if (cachep->entries[k].present && cachep->entries[k].index == (double)cand->index) { prev = &cachep->entries[k].sig; prev_present = 1; }
    new_cache[new_count].index = (double)cand->index; new_cache[new_count].sig = sig; new_cache[new_count].present = true; new_count++;
    if (same_signature(prev, prev_present, &sig)) { cached_lights++; continue; }
    rebuilt_lights++; entity_casters += (double)cand->caster_count;
    char prefix[512];
    snprintf(prefix, sizeof prefix, "[%zu,%g,%g,%g,%g,%g,%g,%g,%g,%s]", cand->index, slot.x, slot.y, slot.width, slot.height,
             cand->light.origin.x, cand->light.origin.y, cand->light.origin.z, cand->light.radius, world_key);
    if (has_matrix) {
      char key[560]; snprintf(key, sizeof key, "%s|cone", prefix);
      struct wd *entry = world_draws_cached(cachep, key, world, nworld, cand->light, NULL, matrix);
      append_pass(out, cand, slot, matrix, entry, NULL);
    } else {
      for (int face = 0; face < 6; face++) {
        qa_shadow_face_basis basis = qa_q2_shadow_cube_faces[face];
        qa_mat4 projection = qa_shadow_matrix_multiply(perspective(90, cand->light.radius), view_mat(cand->light.origin, basis));
        char key[560]; snprintf(key, sizeof key, "%s|%d", prefix, face);
        struct wd *entry = world_draws_cached(cachep, key, world, nworld, cand->light, &basis.forward, projection);
        qa_rect viewport = { slot.x + (face % 3) * cand->face, slot.y + (face / 3) * cand->face, cand->face, cand->face };
        append_pass(out, cand, viewport, projection, entry, &basis.forward);
      }
    }
  }
  for (size_t i = 0; i < cachep->count; i++) free(cachep->entries[i].sig.casters);
  memcpy(cachep->entries, new_cache, new_count * sizeof(cache_entry)); cachep->count = new_count;
  for (size_t i = 0; i < ncand; i++) free(cands[i].owned_casters);
  out->lighting.lights = lights; out->lighting.light_count = nlights;
  out->lighting.atlas_value.image = s->image->image;
  out->lighting.atlas_value.texel_size = 1 / QA_Q2_SHADOW_ATLAS_SIZE;
  out->lighting.atlas_value.near_plane = QA_Q2_SHADOW_NEAR;
  out->lighting_atlas = &out->lighting.atlas_value;
  if (out->pass_count) {
    out->operations = xalloc(sizeof *out->operations); out->operation_count = 1;
    memset(out->operations, 0, sizeof *out->operations);
    out->operations[0].kind = QA_RENDER_OP_DEPTH_ATLAS;
    out->operations[0].image = s->image->image;
    out->operations[0].passes = out->passes; out->operations[0].pass_count = out->pass_count;
  }
  out->stats.lights = cached_lights + rebuilt_lights;
  out->stats.cached_lights = cached_lights; out->stats.rebuilt_lights = rebuilt_lights;
  out->stats.faces_rendered = (double)out->pass_count; out->stats.entity_casters = entity_casters;
  (void)cones;
  return true;
}
void qa_q2_shadow_scene_close(qa_q2_shadow_scene *s) {
  if (s->has_image) { qa_scene_image_registry_release(s->images, s->image->image); s->has_image = false; s->image = NULL; }
  scene_cache *c = (scene_cache *)s->_cached;
  if (c) {
    for (size_t i = 0; i < c->count; i++) { free(c->entries[i].sig.casters); c->entries[i].sig.casters = NULL; }
    c->count = 0;
    for (size_t i = 0; i < c->wd_count; i++) { free(c->wd[i].key); if (c->wd[i].positions) for (size_t k = 0; k < c->wd[i].draw_count; k++) free(c->wd[i].positions[k]); free(c->wd[i].positions); free(c->wd[i].offsets); free(c->wd[i].draws); }
    c->wd_count = 0;
  }
}
