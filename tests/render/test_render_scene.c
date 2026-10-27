/*
 * Headless numeric port of key cases for src/render/scene: view.ts
 * perspective staging (bit-exact vs the TS), patch.ts tessellation,
 * shadows.ts resolution/packing, source-sort.ts, skin.ts,
 * geometry.ts brush lightmap offsets (tests/render/
 * brush-lightmap-offset.test.ts expectations on the offset rule) and
 * flare.ts image naming. Expected matrices/errors were produced by
 * running the TypeScript with bun. GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/render/scene/flare.h"
#include "qa/render/scene/geometry.h"
#include "qa/render/scene/patch.h"
#include "qa/render/scene/shadows.h"
#include "qa/render/scene/skin.h"
#include "qa/render/scene/source_sort.h"
#include "qa/render/scene/view.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_ABORTS(...) do { checks++; pid_t pid = fork(); if (pid == 0) { \
  (void)prctl(PR_SET_DUMPABLE, 0); freopen("/dev/null", "w", stderr); __VA_ARGS__; _exit(0); } \
  int status = 0; waitpid(pid, &status, 0); \
  if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) { failures++; fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__, __LINE__, #__VA_ARGS__); } } while (0)

static uint32_t fbits(double v) { float f = (float)v; uint32_t b; memcpy(&b, &f, 4); return b; }

static void test_view(void) {
  qa_mat4 m = qa_scene_perspective_projection(90, 60, 4096, 4);
  static const uint32_t expected[16] = { 1065353216,0,0,0, 0,1071494104,0,0, 0,0,3212853264u,3212836864u, 0,0,3238010888u,0 };
  for (int i = 0; i < 16; i++) CHECK(fbits(m.values[i]) == expected[i]);
  qa_vec4 pt = qa_scene_project_point(m, (qa_vec3){ 10, 20, -30 });
  CHECK(pt.x == 10 && pt.w == 30);
  CHECK(fbits(pt.y) == fbits(34.64101791381836));
  CHECK_ABORTS(qa_scene_perspective_projection(0, 60, 100, 4));
  CHECK_ABORTS(qa_scene_perspective_projection(90, 60, 4, 4));
  qa_scene_model_transform bad;
  memset(&bad, 0, sizeof bad); bad.has_scale = true; bad.scale = 0;
  CHECK_ABORTS(qa_scene_model_scale(bad));
  qa_scene_model_transform unit;
  memset(&unit, 0, sizeof unit); unit.scale = 1;
  unit.axis.vectors[0] = (qa_vec3){ 1, 0, 0 }; unit.axis.vectors[1] = (qa_vec3){ 0, 1, 0 }; unit.axis.vectors[2] = (qa_vec3){ 0, 0, 1 };
  qa_vec3 w = qa_scene_world_point((qa_vec3){ 1, 2, 3 }, unit);
  CHECK(w.x == 1 && w.y == 2 && w.z == 3);
  CHECK(qa_scene_far_clip((qa_vec3){ 0, 0, 0 }, (qa_bounds){ { -3, 0, 0 }, { 0, 4, 0 } }) == 5);
  qa_plane planes[1] = { { { 0, 0, 1 }, 2 } };
  CHECK(!qa_scene_bounds_in_frustum((qa_bounds){ { 0, 0, 0 }, { 1, 1, 1 } }, planes, 1));
  CHECK(qa_scene_bounds_in_frustum((qa_bounds){ { 0, 0, 2 }, { 1, 1, 3 } }, planes, 1));
}

/* sort range over a plain array */
typedef struct arr { uint32_t *v; } arr;
static uint32_t get_sort(void *ctx, size_t i) { return ((arr *)ctx)->v[i]; }
static void set_sort(void *ctx, size_t i, uint32_t s) { ((arr *)ctx)->v[i] = s; }
static void swap_sort(void *ctx, size_t a, size_t b) { uint32_t *v = ((arr *)ctx)->v; uint32_t t = v[a]; v[a] = v[b]; v[b] = t; }
static void test_sort(void) {
  CHECK(qa_pack_source_draw_sort(1, 2, 3, 4) == ((1u << 17) | (2u << 7) | (3u << 2) | 4u));
  uint32_t values[20];
  for (int i = 0; i < 20; i++) values[i] = (uint32_t)((19 - i) * 2654435761u);
  arr a = { values };
  qa_source_draw_sort_range range = { &a, 20, get_sort, set_sort, swap_sort };
  qa_sort_draw_surfs(range);
  for (int i = 1; i < 20; i++) CHECK(values[i - 1] <= values[i]);
}

static void test_shadows(void) {
  CHECK(qa_shadow_map_resolution(0, 1024) == 512);
  CHECK(qa_shadow_map_resolution(100, 1024) == 128);
  CHECK(qa_shadow_map_resolution(512, 1024) == 512);
  CHECK(qa_shadow_map_resolution(2048, 1024) == 1024);
  CHECK(qa_shadow_map_resolution(300, 256) == 256);
  qa_shadow_pack_request reqs[3] = { {1024,1024},{1024,1024},{2048,1} };
  qa_shadow_pack_result out[3];
  qa_pack_shadow_atlas(reqs, 3, false, 0, out);
  CHECK(!out[0].is_null && out[0].rect.x == 0 && out[0].rect.y == 0);
  CHECK(!out[1].is_null && out[1].rect.x == 1024 && out[1].rect.y == 0);
  CHECK(!out[2].is_null && out[2].rect.x == 0 && out[2].rect.y == 1024);
  CHECK(fabs(qa_shadow_cone_fov(0.5) - 138) < 1e-9);
  CHECK(qa_shadow_cone_fov(-1) == 175);
  CHECK(qa_shadow_cone_fov(2) == 0);
  qa_shadow_mesh mesh;
  qa_vec3 positions[3] = { {0,0,0},{30,0,0},{0,40,0} };
  double indices[3] = { 0, 1, 2 };
  mesh = (qa_shadow_mesh){ positions, 3, indices, 3 };
  qa_shadow_caster caster = qa_shadow_caster_new((qa_vec3){ 0, 0, 0 }, &mesh, 1);
  CHECK(caster.radius == 40);
  CHECK_ABORTS(qa_shadow_cone_matrix((qa_q2_fragment_light){ 0 }));
}

static qa_q3_bsp_vertex vertex(double x, double y, double z) {
  qa_q3_bsp_vertex v;
  memset(&v, 0, sizeof v);
  v.position = (qa_vec3){ x, y, z }; v.normal = (qa_vec3){ 0, 0, 1 };
  v.color = (qa_vec4){ 255, 255, 255, 255 };
  return v;
}
static void test_patch(void) {
  qa_q3_bsp_vertex pts[9];
  for (int y = 0; y < 3; y++) for (int x = 0; x < 3; x++) {
    pts[y * 3 + x] = vertex(x * 32, y * 32, (x == 1 && y == 1) ? 32 : 0);
    pts[y * 3 + x].tex_coord = (qa_vec2){ x / 2.0, y / 2.0 };
  }
  qa_patch_mesh mesh;
  CHECK(qa_tessellate_patch(pts, 9, 3, 3, 4, &mesh));
  CHECK(mesh.width == 5 && mesh.height == 5 && mesh.vertex_count == 25 && mesh.index_count == 96);
  CHECK(fbits(mesh.width_lod_error[1]) == fbits(0.279508501291275));
  CHECK(fbits(mesh.width_lod_error[2]) == fbits(0.0625));
  CHECK(fbits(mesh.height_lod_error[1]) == fbits(0.5153881907463074));
  qa_patch_mesh_destroy(&mesh);
  for (int i = 0; i < 9; i++) pts[i].position.z = 0;
  CHECK(qa_tessellate_patch(pts, 9, 3, 3, 4, &mesh));
  CHECK(mesh.width == 2 && mesh.height == 2 && mesh.vertex_count == 4 && mesh.index_count == 6);
  static const double expect_idx[6] = { 0, 2, 1, 1, 2, 3 };
  for (int i = 0; i < 6; i++) CHECK(mesh.indices[i] == expect_idx[i]);
  qa_patch_mesh_destroy(&mesh);
  CHECK_ABORTS(qa_tessellate_patch(pts, 9, 4, 3, 4, &mesh));
}

static void test_skin_and_flare(void) {
  uint8_t palette_bytes[768];
  memset(palette_bytes, 7, sizeof palette_bytes);
  palette_bytes[0] = palette_bytes[1] = palette_bytes[2] = 0; /* index 0 black */
  qa_palette palette;
  memset(&palette, 0, sizeof palette);
  palette.colors.bytes = palette_bytes; palette.colors.length = sizeof palette_bytes;
  uint8_t indices[9] = { 5, 5, 7, 5, 5, 7, 7, 7, 7 };
  size_t out_len = 0;
  uint8_t *result = qa_flood_skin(indices, 9, 3, 3, palette, &out_len);
  CHECK(result != NULL && out_len == 9);
  CHECK(result[0] == 0); /* seed has only fill neighbors -> black */
  CHECK(result[1] == 7 && result[3] == 7 && result[4] == 7);
  free(result);
  CHECK(qa_default_flare_image("misc/flare.tga"));
  CHECK(qa_default_flare_image("MISC\\FLARE.TGA"));
  CHECK(qa_default_flare_image("sprites/psx_flare.tga"));
  CHECK(!qa_default_flare_image("sprites/glow.tga"));
}

static void test_brush_offsets(void) {
  qa_bsp_plane planes[1] = { { { { 0, 0, 1 }, 0 }, 2, 0 } };
  qa_vec3 vertices[3] = { {0,0,0},{16,0,0},{0,16,0} };
  qa_bsp_edge edges[3] = { {{0,1}},{{1,2}},{{2,0}} };
  double sedges[3] = { 0, 1, 2 };
  double styles[2] = { 0, 255 };
  qa_texture_projection proj = { {1,0,0,0}, {0,1,0,0} };
  qa_q1_texture_info q1info[1] = { { proj, 0, 0 } };
  qa_q2_texture_info q2info[1];
  memset(q2info, 0, sizeof q2info); q2info[0].projection = proj; q2info[0].surface.name = "stone";
  qa_bsp_face face;
  memset(&face, 0, sizeof face);
  face.plane = 0; face.edges = (qa_index_range){ 0, 3 }; face.texture_info = 0;
  face.styles.values = styles; face.styles.count = 2;
  face.has_lighting_offset = true; face.lighting_offset = 1;
  uint8_t samples[16];
  memset(samples, 16, sizeof samples);
  qa_q1_world_geometry q1;
  memset(&q1, 0, sizeof q1);
  q1.format = "bsp29"; q1.entities = "";
  q1.planes = planes; q1.plane_count = 1; q1.vertices = vertices; q1.vertex_count = 3;
  q1.edges = edges; q1.edge_count = 3; q1.surface_edges.values = sedges; q1.surface_edges.count = 3;
  q1.texture_info = q1info; q1.texture_info_count = 1; q1.faces = &face; q1.face_count = 1;
  q1.lighting.is_rgb8 = true; q1.lighting.source = "lit"; q1.lighting.samples.bytes = samples; q1.lighting.samples.length = sizeof samples;
  qa_prepared_brush_face prepared;
  CHECK(qa_prepare_brush_face(true, &q1, NULL, face, 0, (qa_vec2){ 64, 64 }, false, 64, &prepared));
  CHECK(prepared.lightmap.offset == 3 && prepared.lightmap.lighting != NULL && !prepared.decoupled);
  CHECK(prepared.geometry.vertex_count == 3 && prepared.geometry.index_count == 3);
  qa_prepared_brush_face_destroy(&prepared);
  q1.lighting.is_rgb8 = false;
  CHECK(qa_prepare_brush_face(true, &q1, NULL, face, 0, (qa_vec2){ 64, 64 }, false, 64, &prepared));
  CHECK(prepared.lightmap.offset == 1);
  qa_prepared_brush_face_destroy(&prepared);
  face.has_lighting_offset = false;
  CHECK(qa_prepare_brush_face(true, &q1, NULL, face, 0, (qa_vec2){ 64, 64 }, false, 64, &prepared));
  CHECK(prepared.lightmap.lighting == NULL);
  qa_prepared_brush_face_destroy(&prepared);
  face.has_lighting_offset = true;
  qa_q2_world_geometry q2;
  memset(&q2, 0, sizeof q2);
  q2.format = "ibsp38"; q2.entities = "";
  q2.planes = planes; q2.plane_count = 1; q2.vertices = vertices; q2.vertex_count = 3;
  q2.edges = edges; q2.edge_count = 3; q2.surface_edges.values = sedges; q2.surface_edges.count = 3;
  q2.texture_info = q2info; q2.texture_info_count = 1; q2.faces = &face; q2.face_count = 1;
  q2.lighting.is_rgb8 = true; q2.lighting.source = "bsp"; q2.lighting.samples.bytes = samples; q2.lighting.samples.length = sizeof samples;
  CHECK(qa_prepare_brush_face(false, NULL, &q2, face, 0, (qa_vec2){ 64, 64 }, false, 64, &prepared));
  CHECK(prepared.lightmap.offset == 1);
  qa_prepared_brush_face_destroy(&prepared);
  qa_bounds empty = qa_scene_geometry_bounds(NULL, 0);
  CHECK(empty.min.x == 0 && empty.max.x == 0);
}

int main(void) {
  test_view();
  test_sort();
  test_shadows();
  test_patch();
  test_skin_and_flare();
  test_brush_offsets();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
