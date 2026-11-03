/*
 * Port of tests/render/cpu/row-parallel.test.ts plus the row-band, registry and
 * lifecycle behavior of row-parallel.ts and row-worker.ts, and a differential
 * check of triangle-kernel.ts. The KERNEL_CASES table and the SCENE_* hashes were
 * produced by Bun running the TypeScript kernel and SoftwareRenderer over the same
 * framebuffer, setups and scenes that build_setup and render_scene construct here.
 * A failing hash means the C differs from the TS oracle. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qa/contracts/identity.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/render/cpu/lighting.h"
#include "qa/render/cpu/rasterizer.h"
#include "qa/render/cpu/row_parallel.h"
#include "qa/render/cpu/row_worker.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NAMED(cond, name) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s: %s\n", __FILE__, __LINE__, name, #cond); } } while (0)

static uint32_t fnv(uint32_t h, const void *data, size_t length) {
  const uint8_t *bytes = (const uint8_t *)data;
  for (size_t i = 0; i < length; i++) { h ^= bytes[i]; h *= 16777619u; }
  return h;
}
#define FNV_START 2166136261u
static qa_vec3 v3(double x, double y, double z) { qa_vec3 v; v.x = x; v.y = y; v.z = z; return v; }
static qa_vec4 v4(double x, double y, double z, double w) { qa_vec4 v; v.x = x; v.y = y; v.z = z; v.w = w; return v; }

/* --- row-parallel.test.ts scene, with culled-light and multi-job variants --- */
#define WIDTH 320
#define HEIGHT 240
enum { COLS = 16, ROWS = 12, VERTEX_COUNT = COLS * ROWS * 4, INDEX_COUNT = COLS * ROWS * 6, DEPTH_SAMPLES = 56 };
enum { SCENE_VERTEX, SCENE_FRAGMENT, SCENE_CULLED, SCENE_TWO_JOBS };
/* SoftwareRenderer output from Bun for QTS_CPU_WORKERS=0; TS '3' matched these too. */
#define SCENE_VERTEX_PIXELS 0x6d52452au
#define SCENE_FRAGMENT_PIXELS 0x1438dc77u
#define SCENE_CULLED_PIXELS 0x32f75223u
#define SCENE_DEPTHS 0x4d543f55u
static qa_render_vertex vertices[VERTEX_COUNT]; static double indices[INDEX_COUNT];
static qa_vec3 world[VERTEX_COUNT], normals[VERTEX_COUNT];
static float shadow_depth[64];
static qa_q2_light_cone scene_cone;
static void scene_shadow(qa_q2_fragment_light *light, bool shadowed) {
  light->shadow.kind = shadowed ? "cone" : "none";
  if (shadowed) { light->shadow.matrix = qa_identity_mat4(); light->shadow.atlas_rect = v4(0, 0, 1, 1); }
}
static size_t scene_lights(qa_q2_fragment_light *lights, int kind) {
  memset(lights, 0, 4 * sizeof *lights);
  if (kind == SCENE_FRAGMENT) {
    lights[0].origin = v3(0, 0, 64); lights[0].radius = 128; lights[0].color = v3(1, 0.5, 0.25); lights[0].scale = 1; scene_shadow(&lights[0], true);
    return 1;
  }
  scene_cone.direction = v3(0, 0, -1); scene_cone.cos_half_angle = 0.25;
  lights[0].origin = v3(-24, -24, 16); lights[0].radius = 20; lights[0].color = v3(1, 0.5, 0.25); lights[0].scale = 1; scene_shadow(&lights[0], true);
  lights[1].origin = v3(24, -24, 16); lights[1].radius = 20; lights[1].color = v3(0.25, 1, 0.5); lights[1].scale = 1; scene_shadow(&lights[1], false);
  lights[2].origin = v3(-24, 24, 16); lights[2].radius = 20; lights[2].color = v3(0.5, 0.25, 1); lights[2].scale = 1; lights[2].cone = &scene_cone; scene_shadow(&lights[2], false);
  lights[3].origin = v3(24, 24, 16); lights[3].radius = 20; lights[3].color = v3(1, 1, 0.25); lights[3].scale = 0.75; scene_shadow(&lights[3], true);
  return 4;
}
static void scene_geometry(void) {
  int nv = 0, ni = 0;
  for (int row = 0; row < ROWS; row++) for (int col = 0; col < COLS; col++) {
    double x0 = -1 + ((double)col / COLS) * 2, x1 = -1 + ((col + 1.7) / COLS) * 2;
    double y0 = -1 + ((double)row / ROWS) * 2, y1 = -1 + ((row + 1.7) / ROWS) * 2;
    double z = ((col * 31 + row * 17) % 89) / 89.0 * 0.8 - 0.4;
    qa_vec4 color = v4(((col * 53) % 101) / 100.0, ((row * 41) % 103) / 102.0, ((col + row * 7) % 97) / 96.0, 1);
    int base = nv;
    double corners[4][4] = {{x0, y0, 0, 0}, {x1, y0, 2, 0}, {x0, y1, 0, 2}, {x1, y1, 2, 2}};
    for (int c = 0; c < 4; c++) {
      vertices[nv].position = v4(corners[c][0], corners[c][1], z, 1); vertices[nv].tex_coord.x = corners[c][2]; vertices[nv].tex_coord.y = corners[c][3]; vertices[nv].color = color;
      world[nv] = v3(corners[c][0] * 32, corners[c][1] * 32, z * 32 + 16); normals[nv] = v3(0, 0, 1);
      nv++;
    }
    int quad[6] = {base, base + 1, base + 2, base + 1, base + 3, base + 2};
    for (int i = 0; i < 6; i++) indices[ni++] = quad[i];
  }
}
typedef struct scene_result { uint8_t *pixels; double batches, depths[DEPTH_SAMPLES]; int depth_count; double after_close; } scene_result;
static qa_renderer_image make_image(qa_renderer_resource_owner owner, double ordinal, double w, double h) {
  qa_renderer_image image; memset(&image, 0, sizeof image);
  image.owner = owner; image.ordinal = ordinal; image.source_name = "generated"; image.width = w; image.height = h;
  return image;
}
static scene_result render_scene(const char *workers, int kind) {
  setenv("QTS_CPU_WORKERS", workers, 1);
  qa_identity_owner identity; CHECK(qa_identity_owner_create(&identity, "cpu parallel"));
  qa_renderer_resource_owner owner; memset(&owner, 0, sizeof owner); owner.identity = &identity; owner.session = identity.session;
  qa_software_renderer renderer; CHECK(qa_software_renderer_init(&renderer, WIDTH, HEIGHT, owner, 8, 8, 8));
  qa_renderer_image image = make_image(owner, 0, 4, 4);
  uint8_t checker[64];
  for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
    int o = (y * 4 + x) * 4, bright = (x + y) % 2 == 0 ? 255 : 96;
    checker[o] = (uint8_t)bright; checker[o + 1] = (uint8_t)(255 - bright); checker[o + 2] = 160; checker[o + 3] = 255;
  }
  qa_image_level level = {4, 4, {checker, 64}};
  qa_image_resource_operation create; memset(&create, 0, sizeof create);
  create.kind = QA_IMAGE_OP_CREATE; create.image = image; create.content.kind = QA_RENDER_IMAGE_RGBA8; create.content.levels = &level; create.content.level_count = 1;
  create.sampling.filter = "nearest"; create.sampling.wrap = "repeat";
  qa_software_renderer_apply_image_resource(&renderer, create);
  qa_render_view_state view; memset(&view, 0, sizeof view);
  view.viewport.width = WIDTH; view.viewport.height = HEIGHT;
  qa_vec4 clear_color = v4(0.1, 0.2, 0.3, 1); qa_render_view_clear clear; clear.depth = 1; clear.color = &clear_color; clear.stencil = true; view.clear = &clear;
  qa_software_renderer_begin_view(&renderer, view);
  scene_geometry();
  qa_draw_batch batch; memset(&batch, 0, sizeof batch);
  batch.lighting.kind = "vertex"; batch.texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE; batch.texture.image = image; batch.indices.values = indices; batch.indices.count = INDEX_COUNT;
  batch.state = QA_CPU_OPAQUE_STATE; batch.vertices = vertices; batch.vertex_count = VERTEX_COUNT;
  qa_q2_fragment_light lights[4]; qa_q2_shadow_atlas atlas;
  if (kind != SCENE_VERTEX) {
    qa_renderer_image depth_image = make_image(owner, 1, 8, 8);
    for (int i = 0; i < 64; i++) shadow_depth[i] = 0.9f;
    qa_depth_image_level depth_level; depth_level.width = 8; depth_level.height = 8; depth_level.pixels = shadow_depth; depth_level.pixel_count = 64;
    qa_image_resource_operation depth_create; memset(&depth_create, 0, sizeof depth_create);
    depth_create.kind = QA_IMAGE_OP_CREATE; depth_create.image = depth_image; depth_create.content.kind = QA_RENDER_IMAGE_DEPTH32F; depth_create.content.depth_levels = &depth_level; depth_create.content.depth_level_count = 1;
    depth_create.sampling.filter = "nearest"; depth_create.sampling.wrap = "clamp";
    qa_software_renderer_apply_image_resource(&renderer, depth_create);
    atlas.image = depth_image; atlas.texel_size = 1.0 / 8; atlas.near_plane = 4;
    size_t light_count = scene_lights(lights, kind);
    batch.lighting.kind = "q2-world"; batch.lighting.pass = "texture"; batch.lighting.lights = lights; batch.lighting.light_count = light_count; batch.lighting.atlas = &atlas;
    batch.lighting.world_positions = world; batch.lighting.world_position_count = VERTEX_COUNT; batch.lighting.normals = normals; batch.lighting.normal_count = VERTEX_COUNT;
  }
  qa_software_renderer_draw(&renderer, batch);
  if (kind == SCENE_TWO_JOBS) { /* A later job: new light sets, persisted depth id, reused job buffer. */
    lights[0].radius = 28; lights[1].color = v3(1, 1, 1); batch.state.depth_test = "always";
    batch.state.blend.source = "one"; batch.state.blend.destination = "one";
    qa_software_renderer_draw(&renderer, batch);
    batch.lighting.kind = "vertex"; batch.state.blend.source = "src-alpha"; batch.state.blend.destination = "one-minus-src-alpha";
    for (int i = 0; i < VERTEX_COUNT; i++) vertices[i].color.w = 0.5;
    qa_software_renderer_draw(&renderer, batch);
  }
  scene_result out; memset(&out, 0, sizeof out);
  for (int y = 0; y < HEIGHT; y += 37) for (int x = 0; x < WIDTH; x += 41) out.depths[out.depth_count++] = qa_software_renderer_read_depth_pixel(&renderer, x, y);
  qa_image_level read; CHECK(qa_software_renderer_read_rgba(&renderer, &read));
  out.pixels = (uint8_t *)(uintptr_t)read.pixels.bytes; out.batches = qa_software_renderer_row_parallel_batches(&renderer);
  qa_software_renderer_close(&renderer);
  out.after_close = qa_software_renderer_row_parallel_batches(&renderer);
  qa_identity_owner_destroy(&identity);
  return out;
}
static void compare_scene(int kind, double expected_batches, bool has_oracle, uint32_t oracle_pixels) {
  scene_result serial = render_scene("0", kind), parallel = render_scene("3", kind);
  CHECK(serial.batches == 0);
  CHECK(parallel.batches == expected_batches);
  CHECK(parallel.after_close == 0);
  CHECK(serial.depth_count == DEPTH_SAMPLES && parallel.depth_count == DEPTH_SAMPLES);
  CHECK(memcmp(parallel.pixels, serial.pixels, (size_t)WIDTH * HEIGHT * 4) == 0);
  CHECK(memcmp(parallel.depths, serial.depths, sizeof serial.depths) == 0);
  if (has_oracle) {
    CHECK(fnv(FNV_START, serial.pixels, (size_t)WIDTH * HEIGHT * 4) == oracle_pixels);
    CHECK(fnv(FNV_START, serial.depths, sizeof serial.depths) == SCENE_DEPTHS);
  }
  free(serial.pixels); free(parallel.pixels);
}
/* The batch really holds different culled light sets over one position array. */
static void test_culled_sets_are_distinct(void) {
  scene_geometry();
  qa_q2_fragment_light lights[4], kept[4]; size_t count = scene_lights(lights, SCENE_CULLED);
  qa_cpu_lighting lighting; memset(&lighting, 0, sizeof lighting);
  lighting.parameters.kind = "q2-world"; lighting.parameters.pass = "texture"; lighting.parameters.lights = lights; lighting.parameters.light_count = count;
  bool seen[16]; memset(seen, 0, sizeof seen); int distinct = 0;
  for (int i = 0; i + 2 < INDEX_COUNT; i += 3) {
    qa_vec3 positions[3]; for (int k = 0; k < 3; k++) positions[k] = world[(int)indices[i + k]];
    qa_cpu_lighting culled = qa_cull_triangle_lights(lighting, positions, kept);
    unsigned mask = 0;
    for (size_t k = 0; k < culled.parameters.light_count; k++) for (size_t l = 0; l < count; l++)
      if (culled.parameters.lights[k].origin.x == lights[l].origin.x && culled.parameters.lights[k].origin.y == lights[l].origin.y) mask |= 1u << l;
    if (!seen[mask]) { seen[mask] = true; distinct++; }
  }
  CHECK(distinct >= 4);
}
/* A batch dispatches only at >= 4 setups and >= 50,000 covered pixels. */
static void test_dispatch_threshold(void) {
  setenv("QTS_CPU_WORKERS", "3", 1);
  qa_identity_owner identity; CHECK(qa_identity_owner_create(&identity, "threshold"));
  qa_renderer_resource_owner owner; memset(&owner, 0, sizeof owner); owner.identity = &identity; owner.session = identity.session;
  qa_software_renderer r; CHECK(qa_software_renderer_init(&r, WIDTH, HEIGHT, owner, 8, 8, 8));
  qa_renderer_image image = make_image(owner, 0, 1, 1);
  static const uint8_t white[4] = {255, 255, 255, 255};
  qa_image_level level = {1, 1, {white, 4}};
  qa_image_resource_operation create; memset(&create, 0, sizeof create);
  create.kind = QA_IMAGE_OP_CREATE; create.image = image; create.content.kind = QA_RENDER_IMAGE_RGBA8; create.content.levels = &level; create.content.level_count = 1;
  create.sampling.filter = "nearest"; create.sampling.wrap = "repeat";
  qa_software_renderer_apply_image_resource(&r, create);
  qa_render_vertex v[6]; memset(v, 0, sizeof v);
  static const double xs[6] = {-1, 1, -1, 1, 1, -1}, ys[6] = {-1, -1, 1, -1, 1, 1};
  for (int i = 0; i < 6; i++) { v[i].position = v4(xs[i], ys[i], 0, 1); v[i].color = v4(1, 1, 1, 1); }
  double big[12] = {0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5};
  qa_draw_batch b; memset(&b, 0, sizeof b);
  b.lighting.kind = "vertex"; b.texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE; b.texture.image = image; b.state = QA_CPU_OPAQUE_STATE; b.vertices = v; b.vertex_count = 6;
  b.indices.values = big; b.indices.count = 9; /* 3 setups, enough pixels */
  qa_software_renderer_draw(&r, b);
  CHECK(qa_software_renderer_row_parallel_batches(&r) == 0);
  b.indices.count = 12; /* 4 setups, enough pixels */
  qa_software_renderer_draw(&r, b);
  CHECK(qa_software_renderer_row_parallel_batches(&r) == 1);
  qa_render_vertex small[4]; memset(small, 0, sizeof small);
  static const double sx[4] = {-1, -0.9, -1, -0.9}, sy[4] = {-1, -1, -0.9, -0.9};
  for (int i = 0; i < 4; i++) { small[i].position = v4(sx[i], sy[i], 0, 1); small[i].color = v4(1, 1, 1, 1); }
  double few[12] = {0, 1, 2, 1, 3, 2, 0, 1, 2, 1, 3, 2};
  b.vertices = small; b.vertex_count = 4; b.indices.values = few; b.indices.count = 12; /* 4 setups, too few pixels */
  qa_software_renderer_draw(&r, b);
  CHECK(qa_software_renderer_row_parallel_batches(&r) == 1);
  qa_software_renderer_close(&r); qa_identity_owner_destroy(&identity);
  unsetenv("QTS_CPU_WORKERS");
}

/* --- pool: QTS_CPU_WORKERS, registries, shutdown, errors --- */
static uint32_t pool_color[32 * 32]; static double pool_depth[32 * 32]; static uint32_t pool_stencil[32 * 32];
static qa_row_shared_framebuffer pool_shared(void) {
  qa_row_shared_framebuffer s; s.width = 32; s.height = 32; s.color = (uint8_t *)pool_color; s.depth = pool_depth; s.stencil = pool_stencil; return s;
}
static void check_worker_env(const char *value, bool created, double workers) {
  if (value) setenv("QTS_CPU_WORKERS", value, 1); else unsetenv("QTS_CPU_WORKERS");
  qa_row_parallel_pool pool;
  bool made = qa_row_parallel_pool_try_create(&pool, pool_shared());
  CHECK_NAMED(made == created, value ? value : "(unset)");
  CHECK_NAMED(qa_row_parallel_pool_worker_count(&pool) == workers, value ? value : "(unset)");
  if (made) { CHECK(pool.jobs_run == 0); qa_row_parallel_pool_close(&pool); }
  CHECK(pool.impl == NULL);
}
static void test_worker_env(void) {
  check_worker_env("0", false, 0); check_worker_env("-2", false, 0); check_worker_env("", false, 0); check_worker_env("abc", false, 0);
  check_worker_env("3", true, 3); check_worker_env(" +2", true, 2); check_worker_env("3abc", true, 3); check_worker_env("1.9", true, 1);
  check_worker_env("16", true, 16); check_worker_env("99", true, 16); check_worker_env("99999999999999999999", true, 16);
  check_worker_env("0x10", false, 0);
  long cpus = sysconf(_SC_NPROCESSORS_ONLN); double expected = cpus - 1 > 4 ? 4 : cpus - 1 < 0 ? 0 : (double)(cpus - 1);
  check_worker_env(NULL, expected >= 1, expected);
}

/* --- kernel differential: every row is one fragment state against the TS kernel --- */
typedef struct kernel_case {
  const char *name, *src, *dst, *blending, *depth_test, *alpha_test; double alpha_bits; int stencil; const char *fn, *df, *dp; double cm, wm, max, fill;
  int color_write, depth_write, consumed, tex, sec; const char *env; int fog, lum, white, lit, iw_zero, constant_depth; double offset; uint32_t hash; int sampled_triangles; const char *error;
} kernel_case;
static const kernel_case KERNEL_CASES[] = {
  {"base","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xce2eba4fu,2,NULL},
  {"blend src-alpha/one-minus-src-alpha a8","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xffae8ac0u,2,NULL},
  {"blend src-alpha/one-minus-src-alpha a0","src-alpha","one-minus-src-alpha","alpha","less-equal","none",0,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x7a2febb2u,2,NULL},
  {"blend one/one a8","one","one","add","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xad7664cau,2,NULL},
  {"blend one/one a0","one","one","add","less-equal","none",0,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xb81319efu,2,NULL},
  {"blend dst-color/zero a8","dst-color","zero","multiply","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xad9f7edbu,2,NULL},
  {"blend dst-color/zero a0","dst-color","zero","multiply","less-equal","none",0,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x417a7e3eu,2,NULL},
  {"blend zero/src-color a8","zero","src-color","multiply","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xad9f7edbu,2,NULL},
  {"blend zero/src-color a0","zero","src-color","multiply","less-equal","none",0,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x417a7e3eu,2,NULL},
  {"blend dst-color/one-minus-dst-alpha a8","dst-color","one-minus-dst-alpha","dst-color-inverse-dst-alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x2b73fb5au,2,NULL},
  {"blend dst-color/one-minus-dst-alpha a0","dst-color","one-minus-dst-alpha","dst-color-inverse-dst-alpha","less-equal","none",0,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x417a7e3eu,2,NULL},
  {"general src zero","zero","one-minus-src-alpha","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x46bb5e36u,2,NULL},
  {"general dst zero","src-alpha","zero","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x34a18b30u,2,NULL},
  {"general src one","one","one-minus-src-alpha","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x36a705u,2,NULL},
  {"general dst one","src-alpha","one","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xbd964b17u,2,NULL},
  {"general src src-color","src-color","one-minus-src-alpha","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x21241e13u,2,NULL},
  {"general dst src-color","src-alpha","src-color","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x653095cbu,2,NULL},
  {"general src one-minus-src-color","one-minus-src-color","one-minus-src-alpha","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xcb50e03du,2,NULL},
  {"general dst one-minus-src-color","src-alpha","one-minus-src-color","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x99d24179u,2,NULL},
  {"general src dst-color","dst-color","one-minus-src-alpha","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xfd5856c3u,2,NULL},
  {"general dst dst-color","src-alpha","dst-color","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xbbff02adu,2,NULL},
  {"general src one-minus-dst-color","one-minus-dst-color","one-minus-src-alpha","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x8d1cf808u,2,NULL},
  {"general dst one-minus-dst-color","src-alpha","one-minus-dst-color","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x82edb407u,2,NULL},
  {"general src src-alpha","src-alpha","one-minus-src-alpha","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xffae8ac0u,2,NULL},
  {"general dst src-alpha","src-alpha","src-alpha","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xa655c74au,2,NULL},
  {"general src one-minus-src-alpha","one-minus-src-alpha","one-minus-src-alpha","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xe9cf2f00u,2,NULL},
  {"general dst one-minus-src-alpha","src-alpha","one-minus-src-alpha","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xffae8ac0u,2,NULL},
  {"general src dst-alpha","dst-alpha","one-minus-src-alpha","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x46dc807cu,2,NULL},
  {"general dst dst-alpha","src-alpha","dst-alpha","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x98878ff9u,2,NULL},
  {"general src one-minus-dst-alpha","one-minus-dst-alpha","one-minus-src-alpha","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xd6d01408u,2,NULL},
  {"general dst one-minus-dst-alpha","src-alpha","one-minus-dst-alpha","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xd6ad25e5u,2,NULL},
  {"general src src-alpha-saturate","src-alpha-saturate","one-minus-src-alpha","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x2f306142u,2,NULL},
  {"general dst src-alpha-saturate","src-alpha","src-alpha-saturate","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xfb1361fdu,2,NULL},
  {"general a0","src-alpha-saturate","dst-alpha","general","less-equal","none",0,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x389486f4u,2,NULL},
  {"depth equal","one","zero","opaque","equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xab7bd7b7u,0,NULL},
  {"depth always","one","zero","opaque","always","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x2f1c6d77u,2,NULL},
  {"alpha gt0","one","zero","opaque","less-equal","gt0",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,0,NULL,0,0,0,0,0,0,0,0x9496faebu,2,NULL},
  {"alpha gt0 nocolor","one","zero","opaque","less-equal","gt0",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,0,1,1,1,0,NULL,0,0,0,0,0,0,0,0x73b00d53u,2,NULL},
  {"alpha lt128","one","zero","opaque","less-equal","lt128",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,0,NULL,0,0,0,0,0,0,0,0x9496faebu,2,NULL},
  {"alpha lt128 nocolor","one","zero","opaque","less-equal","lt128",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,0,1,1,1,0,NULL,0,0,0,0,0,0,0,0x73b00d53u,2,NULL},
  {"alpha ge128","one","zero","opaque","less-equal","ge128",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,0,NULL,0,0,0,0,0,0,0,0xab7bd7b7u,2,NULL},
  {"alpha ge128 nocolor","one","zero","opaque","less-equal","ge128",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,0,1,1,1,0,NULL,0,0,0,0,0,0,0,0xab7bd7b7u,2,NULL},
  {"depth only","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,0,1,0,3,0,NULL,0,0,0,0,0,0,0,0x73b00d53u,0,NULL},
  {"no depth write","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,0,1,3,0,NULL,0,0,0,0,0,0,0,0x82bbbecbu,2,NULL},
  {"no writes","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,0,0,0,3,0,NULL,0,0,0,0,0,0,0,0xab7bd7b7u,0,NULL},
  {"constant depth","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,1,0,0x7bee4a0au,2,NULL},
  {"polygon offset","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0.125,0x51bfef8eu,2,NULL},
  {"stencil inc","one","zero","opaque","less-equal","none",8,1,"always","keep","increment",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x75a866c2u,2,NULL},
  {"stencil dec","one","zero","opaque","less-equal","none",8,1,"always","keep","decrement",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xe135f1bu,2,NULL},
  {"stencil fail inc","one","zero","opaque","less-equal","none",8,1,"always","increment","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xdbac1ae4u,2,NULL},
  {"stencil fail dec","one","zero","opaque","less-equal","none",8,1,"always","decrement","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x4dd3d78fu,2,NULL},
  {"stencil nonzero","one","zero","opaque","less-equal","none",8,1,"nonzero","keep","increment",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x5883332fu,2,NULL},
  {"stencil nonzero mask","one","zero","opaque","less-equal","none",8,1,"nonzero","keep","decrement",2.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x15c751bbu,2,NULL},
  {"stencil write mask","one","zero","opaque","less-equal","none",8,1,"always","keep","increment",4294967295.0,1.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xda9da842u,2,NULL},
  {"stencil sat 255","one","zero","opaque","less-equal","none",8,1,"always","keep","increment",4294967295.0,4294967295.0,255.0,255.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0x660e4ecfu,2,NULL},
  {"stencil bits32 sat","one","zero","opaque","less-equal","none",8,1,"always","keep","increment",4294967295.0,4294967295.0,4294967295.0,4294967295.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xc9a5decfu,2,NULL},
  {"stencil bits32 dec zero","one","zero","opaque","less-equal","none",8,1,"always","keep","decrement",4294967295.0,4294967295.0,4294967295.0,0.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xba7feecfu,2,NULL},
  {"stencil depth-only","one","zero","opaque","less-equal","none",8,1,"always","keep","increment",4294967295.0,4294967295.0,255.0,-1.0,0,1,0,3,0,NULL,0,0,0,0,0,0,0,0x71318f86u,0,NULL},
  {"tex 0","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,0,0,NULL,0,0,0,0,0,0,0,0x4a48700cu,0,NULL},
  {"tex 0 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,0,0,NULL,0,0,0,0,0,0,0,0x9cf19903u,0,NULL},
  {"tex 1","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,0,NULL,0,0,0,0,0,0,0,0x9496faebu,2,NULL},
  {"tex 1 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,0,NULL,0,0,0,0,0,0,0,0x87ad3e63u,2,NULL},
  {"tex 2","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,2,0,NULL,0,0,0,0,0,0,0,0xb84ca833u,2,NULL},
  {"tex 2 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,2,0,NULL,0,0,0,0,0,0,0,0xd46e0de8u,2,NULL},
  {"tex 3","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xce2eba4fu,2,NULL},
  {"tex 3 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,0,0,0,0xffae8ac0u,2,NULL},
  {"tex 4","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,4,0,NULL,0,0,0,0,0,0,0,0x31f1cdccu,2,NULL},
  {"tex 4 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,4,0,NULL,0,0,0,0,0,0,0,0xad259ddfu,2,NULL},
  {"tex 5","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,5,0,NULL,0,0,0,0,0,0,0,0xd00ec605u,2,NULL},
  {"tex 5 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,5,0,NULL,0,0,0,0,0,0,0,0x1d37256au,2,NULL},
  {"tex 6","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,6,0,NULL,0,0,0,0,0,0,0,0xea693422u,2,NULL},
  {"tex 6 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,6,0,NULL,0,0,0,0,0,0,0,0xef2be50eu,2,NULL},
  {"tex 7","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,7,0,NULL,0,0,0,0,0,0,0,0x2370ae0u,2,NULL},
  {"tex 7 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,7,0,NULL,0,0,0,0,0,0,0,0xf0b464a2u,2,NULL},
  {"tex 8","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,8,0,NULL,0,0,0,0,0,0,0,0x1f24a194u,2,NULL},
  {"tex 8 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,8,0,NULL,0,0,0,0,0,0,0,0x8393c5e1u,2,NULL},
  {"tex 9","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,9,0,NULL,0,0,0,0,0,0,0,0xcd800fa6u,2,NULL},
  {"tex 9 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,9,0,NULL,0,0,0,0,0,0,0,0xc7815a4eu,2,NULL},
  {"tex 10","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,10,0,NULL,0,0,0,0,0,0,0,0xd0ec570bu,2,NULL},
  {"tex 10 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,10,0,NULL,0,0,0,0,0,0,0,0xbdcae78bu,2,NULL},
  {"tex 11","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,11,0,NULL,0,0,0,0,0,0,0,0x100a895bu,2,NULL},
  {"tex 11 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,11,0,NULL,0,0,0,0,0,0,0,0x2cfcf1c7u,2,NULL},
  {"tex 12","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,12,0,NULL,0,0,0,0,0,0,0,0x2ec4da60u,2,NULL},
  {"tex 12 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,12,0,NULL,0,0,0,0,0,0,0,0xe12b71f9u,2,NULL},
  {"tex 13","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,13,0,NULL,0,0,0,0,0,0,0,0x4366fc88u,2,NULL},
  {"tex 13 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,13,0,NULL,0,0,0,0,0,0,0,0xa918b454u,2,NULL},
  {"tex 14","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,14,0,NULL,0,0,0,0,0,0,0,0x8268adaeu,2,NULL},
  {"tex 14 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,14,0,NULL,0,0,0,0,0,0,0,0xbb65a3a0u,2,NULL},
  {"tex 15","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,15,0,NULL,0,0,0,0,0,0,0,0x79579223u,2,NULL},
  {"tex 15 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,15,0,NULL,0,0,0,0,0,0,0,0x4698b367u,2,NULL},
  {"tex 16","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,16,0,NULL,0,0,0,0,0,0,0,0x818ffc06u,2,NULL},
  {"tex 16 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,16,0,NULL,0,0,0,0,0,0,0,0x8085ce8bu,2,NULL},
  {"sec modulate 0","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,"modulate",0,0,0,0,0,0,0,0xce2eba4fu,2,NULL},
  {"sec modulate 1","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,1,"modulate",0,0,0,0,0,0,0,0x7dfbaae7u,2,NULL},
  {"sec modulate 2","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,2,"modulate",0,0,0,0,0,0,0,0x5d93ec83u,2,NULL},
  {"sec modulate 3","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,3,"modulate",0,0,0,0,0,0,0,0x1f653c4du,2,NULL},
  {"sec modulate 5","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,5,"modulate",0,0,0,0,0,0,0,0x5190e2bau,2,NULL},
  {"sec modulate 8","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,8,"modulate",0,0,0,0,0,0,0,0x1e824fdeu,2,NULL},
  {"sec modulate 9","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,9,"modulate",0,0,0,0,0,0,0,0x2da7b405u,2,NULL},
  {"sec modulate 11","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,11,"modulate",0,0,0,0,0,0,0,0x1a66f7f4u,2,NULL},
  {"sec modulate 14","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,14,"modulate",0,0,0,0,0,0,0,0xbb0138f3u,2,NULL},
  {"sec modulate 15","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,15,"modulate",0,0,0,0,0,0,0,0xd7540973u,2,NULL},
  {"sec add 0","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,"add",0,0,0,0,0,0,0,0xce2eba4fu,2,NULL},
  {"sec add 1","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,1,"add",0,0,0,0,0,0,0,0xb377611du,2,NULL},
  {"sec add 2","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,2,"add",0,0,0,0,0,0,0,0x142ceefau,2,NULL},
  {"sec add 3","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,3,"add",0,0,0,0,0,0,0,0x248cbb54u,2,NULL},
  {"sec add 5","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,5,"add",0,0,0,0,0,0,0,0x30ec8e53u,2,NULL},
  {"sec add 8","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,8,"add",0,0,0,0,0,0,0,0x899f9fa7u,2,NULL},
  {"sec add 9","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,9,"add",0,0,0,0,0,0,0,0x23feb414u,2,NULL},
  {"sec add 11","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,11,"add",0,0,0,0,0,0,0,0x658d0ac7u,2,NULL},
  {"sec add 14","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,14,"add",0,0,0,0,0,0,0,0x7bf660a6u,2,NULL},
  {"sec add 15","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,15,"add",0,0,0,0,0,0,0,0xdb402fc0u,2,NULL},
  {"sec replace 0","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,"replace",0,0,0,0,0,0,0,0xce2eba4fu,2,NULL},
  {"sec replace 1","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,1,"replace",0,0,0,0,0,0,0,0x60af3e55u,2,NULL},
  {"sec replace 2","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,2,"replace",0,0,0,0,0,0,0,0x69dd3efdu,2,NULL},
  {"sec replace 3","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,3,"replace",0,0,0,0,0,0,0,0x21298961u,2,NULL},
  {"sec replace 5","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,5,"replace",0,0,0,0,0,0,0,0x459bd50eu,2,NULL},
  {"sec replace 8","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,8,"replace",0,0,0,0,0,0,0,0x233d70d4u,2,NULL},
  {"sec replace 9","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,9,"replace",0,0,0,0,0,0,0,0xdcfe0e69u,2,NULL},
  {"sec replace 11","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,11,"replace",0,0,0,0,0,0,0,0xa69cc131u,2,NULL},
  {"sec replace 14","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,14,"replace",0,0,0,0,0,0,0,0xc536a66au,2,NULL},
  {"sec replace 15","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,15,"replace",0,0,0,0,0,0,0,0xc8dfb9a8u,2,NULL},
  {"fog 1","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,1,0,0,0,0,0,0,0x48437fc9u,2,NULL},
  {"fog 1 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,1,0,0,0,0,0,0,0xce279834u,2,NULL},
  {"fog 2","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,2,0,0,0,0,0,0,0x94c0558u,2,NULL},
  {"fog 2 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,2,0,0,0,0,0,0,0x5d14c07u,2,NULL},
  {"fog 3","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,3,0,0,0,0,0,0,0x94c0558u,2,NULL},
  {"fog 3 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,3,0,0,0,0,0,0,0x5d14c07u,2,NULL},
  {"fog 4","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,4,0,0,0,0,0,0,0xce2eba4fu,2,NULL},
  {"fog 4 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,4,0,0,0,0,0,0,0xffae8ac0u,2,NULL},
  {"fog 5","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,5,0,0,0,0,0,0,0xcaad6a89u,2,NULL},
  {"fog 5 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,5,0,0,0,0,0,0,0xbe3f75bcu,2,NULL},
  {"fog 6","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,6,0,0,0,0,0,0,0xea64beacu,2,NULL},
  {"fog 6 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,6,0,0,0,0,0,0,0xda3ff4a3u,2,NULL},
  {"fog 7","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,7,0,0,0,0,0,0,0x2c18601au,2,NULL},
  {"fog 7 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,7,0,0,0,0,0,0,0x27274d02u,2,NULL},
  {"fog 8","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,8,0,0,0,0,0,0,0xb2961475u,2,NULL},
  {"fog 8 alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,8,0,0,0,0,0,0,0x73b00d53u,2,NULL},
  {"lum","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,1,0,0,0,0,0,0xce137fddu,2,NULL},
  {"lum const","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,0,NULL,0,1,0,0,0,0,0,0x2381c91bu,2,NULL},
  {"lum lit","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,1,0,2,0,0,0,0x734165d2u,2,NULL},
  {"white","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,1,0,0,0,0,0x83ca3529u,2,NULL},
  {"white incomplete","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,0,0,NULL,0,0,1,0,0,0,0,0xc7b65a25u,0,NULL},
  {"lit 1 tex 0","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,0,0,NULL,0,0,0,1,0,0,0,0xc7b65a25u,0,NULL},
  {"lit 1 tex 3","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,1,0,0,0,0x3fefc08eu,2,NULL},
  {"lit 2 tex 0","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,0,0,NULL,0,0,0,2,0,0,0,0xd44b3e22u,0,NULL},
  {"lit 2 tex 3","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,2,0,0,0,0xa3174ccfu,2,NULL},
  {"lit 3 tex 0","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,0,0,NULL,0,0,0,3,0,0,0,0x80fcd584u,0,NULL},
  {"lit 3 tex 3","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,3,0,0,0,0xce5f7a98u,2,NULL},
  {"lit 4 tex 0","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,0,0,NULL,0,0,0,4,0,0,0,0x6c565f88u,0,NULL},
  {"lit 4 tex 3","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,4,0,0,0,0x99ba81eeu,2,NULL},
  {"lit 5 tex 0","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,0,0,NULL,0,0,0,5,0,0,0,0xab9e9025u,0,NULL},
  {"lit 5 tex 3","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,5,0,0,0,0x4e43a4fau,2,NULL},
  {"lit 6 tex 0","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,0,0,NULL,0,0,0,6,0,0,0,0xdb2d2b2bu,0,NULL},
  {"lit 6 tex 3","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,6,0,0,0,0xc5524482u,2,NULL},
  {"lit fog sec","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,3,"modulate",2,0,0,2,0,0,0,0xe3f72cf9u,2,NULL},
  {"nan tex 0","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,0,0,NULL,0,0,0,0,1,0,0,0x605b6f85u,0,NULL},
  {"nan tex 1","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,0,NULL,0,0,0,0,1,0,0,0x605b6f85u,2,NULL},
  {"nan tex 3","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,1,0,0,0x605b6f85u,2,NULL},
  {"nan tex 4","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,4,0,NULL,0,0,0,0,1,0,0,0x605b6f85u,2,NULL},
  {"nan tex 6","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,6,0,NULL,0,0,0,0,1,0,0,0x605b6f85u,2,NULL},
  {"nan tex 7","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,7,0,NULL,0,0,0,0,1,0,0,0x605b6f85u,2,NULL},
  {"nan tex 8","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,8,0,NULL,0,0,0,0,1,0,0,0x605b6f85u,2,NULL},
  {"nan tex 10","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,10,0,NULL,0,0,0,0,1,0,0,0x605b6f85u,2,NULL},
  {"nan tex 11","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,11,0,NULL,0,0,0,0,1,0,0,0u,0,"Texture LOD is outside its complete mip chain"},
  {"nan tex 14","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,14,0,NULL,0,0,0,0,1,0,0,0u,0,"Texture LOD is outside its complete mip chain"},
  {"inf tex 3","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,3,0,NULL,0,0,0,0,2,0,0,0x605b6f85u,2,NULL},
  {"inf tex 3 sec","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,3,"modulate",0,0,0,0,2,0,0,0x605b6f85u,2,NULL},
  {"inf tex 4","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,4,0,NULL,0,0,0,0,2,0,0,0x605b6f85u,2,NULL},
  {"inf tex 4 sec","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,4,"modulate",0,0,0,0,2,0,0,0x605b6f85u,2,NULL},
  {"inf tex 5","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,5,0,NULL,0,0,0,0,2,0,0,0x7f93ebedu,2,NULL},
  {"inf tex 5 sec","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,5,"modulate",0,0,0,0,2,0,0,0xe2eef785u,2,NULL},
  {"inf tex 6","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,6,0,NULL,0,0,0,0,2,0,0,0x605b6f85u,2,NULL},
  {"inf tex 6 sec","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,6,"modulate",0,0,0,0,2,0,0,0x605b6f85u,2,NULL},
  {"inf tex 7","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,7,0,NULL,0,0,0,0,2,0,0,0x605b6f85u,2,NULL},
  {"inf tex 7 sec","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,7,"modulate",0,0,0,0,2,0,0,0x605b6f85u,2,NULL},
  {"inf tex 8","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,8,0,NULL,0,0,0,0,2,0,0,0x605b6f85u,2,NULL},
  {"inf tex 8 sec","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,8,"modulate",0,0,0,0,2,0,0,0x605b6f85u,2,NULL},
  {"inf tex 9","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,9,0,NULL,0,0,0,0,2,0,0,0x7f93ebedu,2,NULL},
  {"inf tex 9 sec","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,9,"modulate",0,0,0,0,2,0,0,0xe2eef785u,2,NULL},
  {"inf tex 10","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,10,0,NULL,0,0,0,0,2,0,0,0x605b6f85u,2,NULL},
  {"inf tex 10 sec","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,10,"modulate",0,0,0,0,2,0,0,0x605b6f85u,2,NULL},
  {"inf tex 11","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,11,0,NULL,0,0,0,0,2,0,0,0u,0,"Texture LOD is outside its complete mip chain"},
  {"inf tex 11 sec","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,11,"modulate",0,0,0,0,2,0,0,0u,0,"Texture LOD is outside its complete mip chain"},
  {"inf tex 12","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,12,0,NULL,0,0,0,0,2,0,0,0u,0,"Texture LOD is outside its complete mip chain"},
  {"inf tex 12 sec","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,12,"modulate",0,0,0,0,2,0,0,0u,0,"Texture LOD is outside its complete mip chain"},
  {"inf tex 13","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,13,0,NULL,0,0,0,0,2,0,0,0u,0,"Texture LOD is outside its complete mip chain"},
  {"inf tex 13 sec","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,13,"modulate",0,0,0,0,2,0,0,0u,0,"Texture LOD is outside its complete mip chain"},
  {"inf tex 14","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,14,0,NULL,0,0,0,0,2,0,0,0u,0,"Texture LOD is outside its complete mip chain"},
  {"inf tex 14 sec","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,14,"modulate",0,0,0,0,2,0,0,0u,0,"Texture LOD is outside its complete mip chain"},
  {"inf tex 15","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,15,0,NULL,0,0,0,0,2,0,0,0u,0,"Texture LOD is outside its complete mip chain"},
  {"inf tex 15 sec","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,15,"modulate",0,0,0,0,2,0,0,0u,0,"Texture LOD is outside its complete mip chain"},
  {"inf tex 16","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,16,0,NULL,0,0,0,0,2,0,0,0u,0,"Texture LOD is outside its complete mip chain"},
  {"inf tex 16 sec","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,1,16,"modulate",0,0,0,0,2,0,0,0u,0,"Texture LOD is outside its complete mip chain"},
  {"nan white","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,0,0,NULL,0,0,1,0,1,0,0,0x605b6f85u,0,NULL},
  {"nan saturate","src-alpha-saturate","zero","general","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,0,0,NULL,0,0,0,0,1,0,0,0x605b6f85u,0,NULL},
  {"nan alpha","src-alpha","one-minus-src-alpha","alpha","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,0,0,NULL,0,0,0,0,1,0,0,0x605b6f85u,0,NULL},
  {"nan alpha test","one","zero","opaque","less-equal","gt0",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,0,0,NULL,0,0,0,0,1,0,0,0xab7bd7b7u,0,NULL},
  {"nan fog","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,0,0,NULL,2,0,0,0,1,0,0,0x605b6f85u,0,NULL},
  {"nan sec","one","zero","opaque","less-equal","none",8,0,"always","keep","keep",4294967295.0,4294967295.0,255.0,-1.0,1,1,1,0,4,"add",0,0,0,0,1,0,0,0x605b6f85u,2,NULL},
};
#define W 32
#define H 32
static qa_texture_storage tex_levels[17][4]; static uint8_t tex_pixels[17][4][8 * 8 * 4];
static qa_bound_texture TEX[17];
static qa_bound_texture image_tex(int id, const char *format, const char *wrap, bool min_linear, bool mag_linear, const char *mip, double limit, int w, int h, int levels) {
  qa_bound_texture t; memset(&t, 0, sizeof t);
  t.kind = QA_BOUND_TEXTURE_IMAGE; t.internal_format = format; t.wrap = wrap; t.minify_linear = min_linear; t.magnify_linear = mag_linear; t.mipmapping = mip; t.magnification_limit = limit;
  t.border_color = v4(0.1, 0.2, 0.3, 0.4); t.levels = tex_levels[id]; t.level_count = (size_t)levels;
  int lw = w, lh = h;
  for (int l = 0; l < levels; l++) {
    uint8_t *px = tex_pixels[id][l];
    for (int y = 0; y < lh; y++) for (int x = 0; x < lw; x++) for (int c = 0; c < 4; c++) px[(y * lw + x) * 4 + c] = (uint8_t)((x * 31 + y * 17 + c * 53 + l * 11) % 256);
    qa_texture_storage *s = &tex_levels[id][l];
    memset(s, 0, sizeof *s); s->width = lw; s->height = lh; s->pixels = px; s->pixel_length = (size_t)(lw * lh * 4); s->internal_format = format; s->has_alpha = !strcmp(format, "rgba8"); s->component_maximum = 255;
    lw = lw >> 1 > 1 ? lw >> 1 : 1; lh = lh >> 1 > 1 ? lh >> 1 : 1;
  }
  return t;
}
static qa_batch_fog FOG[9]; static float kernel_depth_px[64]; static qa_depth_image_level kernel_depth; static qa_q2_shadow_atlas kernel_atlas;
static qa_q2_light_cone kernel_cone; static qa_q2_fragment_light L[3]; static qa_q2_model_shadow_light SHADOW_L[2]; static qa_batch_lighting LIT[7];
static void kernel_data(void) {
  memset(TEX, 0, sizeof TEX);
  TEX[0].kind = QA_BOUND_TEXTURE_INCOMPLETE;
  TEX[1].kind = QA_BOUND_TEXTURE_CONSTANT; TEX[1].internal_format = "rgba8"; TEX[1].sample.r = 0.25; TEX[1].sample.g = 0.5; TEX[1].sample.b = 0.75; TEX[1].sample.a = 0.5;
  TEX[2].kind = QA_BOUND_TEXTURE_CONSTANT; TEX[2].internal_format = "rgb8"; TEX[2].sample.r = 0.75; TEX[2].sample.g = 0.5; TEX[2].sample.b = 0.25; TEX[2].sample.a = 1;
  TEX[3] = image_tex(3, "rgba8", "repeat", false, false, "none", 1, 8, 8, 1); TEX[4] = image_tex(4, "rgba8", "repeat", true, true, "none", 1, 8, 8, 1);
  TEX[5] = image_tex(5, "rgb8", "repeat", true, true, "none", 1, 8, 8, 1); TEX[6] = image_tex(6, "rgba8", "repeat", true, true, "none", 1, 6, 3, 1);
  TEX[7] = image_tex(7, "rgba8", "clamp", false, false, "none", 1, 8, 8, 1); TEX[8] = image_tex(8, "rgba8", "clamp", true, true, "none", 1, 8, 8, 1);
  TEX[9] = image_tex(9, "rgb8", "clamp", true, true, "none", 1, 8, 8, 1); TEX[10] = image_tex(10, "rgba8", "clamp", true, true, "none", 1, 6, 3, 1);
  TEX[11] = image_tex(11, "rgba8", "repeat", false, false, "nearest", 1, 8, 8, 4); TEX[12] = image_tex(12, "rgba8", "repeat", true, false, "nearest", M_SQRT2, 8, 8, 4);
  TEX[13] = image_tex(13, "rgba8", "clamp", true, true, "nearest", 2, 8, 8, 4); TEX[14] = image_tex(14, "rgba8", "repeat", true, true, "linear", 1, 8, 8, 4);
  TEX[15] = image_tex(15, "rgb8", "clamp", true, true, "linear", 1, 8, 8, 4); TEX[16] = image_tex(16, "rgba8", "repeat", false, true, "linear", M_SQRT2, 8, 8, 1);
  memset(FOG, 0, sizeof FOG);
  FOG[1].kind = "constant"; FOG[1].color = v3(0.1, 0.2, 0.3); FOG[1].amount = 0.4;
  FOG[2].kind = "exp2"; FOG[2].color = v3(0.9, 0.8, 0.7); FOG[2].density = 0.5;
  static const char *const effects[6] = {"color", "none", "rgb", "alpha", "rgba", "overlay"};
  for (int i = 0; i < 6; i++) { FOG[3 + i] = FOG[2]; FOG[3 + i].effect = effects[i]; }
  for (int i = 0; i < 64; i++) kernel_depth_px[i] = (float)(0.25 + i / 128.0);
  kernel_depth.width = 8; kernel_depth.height = 8; kernel_depth.pixels = kernel_depth_px; kernel_depth.pixel_count = 64;
  memset(&kernel_atlas, 0, sizeof kernel_atlas); kernel_atlas.texel_size = 0.125; kernel_atlas.near_plane = 4;
  qa_q2_shadow_projection cone; memset(&cone, 0, sizeof cone); cone.kind = "cone"; cone.matrix = qa_identity_mat4(); cone.atlas_rect = v4(0, 0, 1, 1);
  qa_q2_shadow_projection cube = cone; cube.kind = "cube";
  qa_q2_shadow_projection none; memset(&none, 0, sizeof none); none.kind = "none";
  memset(L, 0, sizeof L); kernel_cone.direction = v3(0, 0, -1); kernel_cone.cos_half_angle = 0.5;
  L[0].origin = v3(16, 16, 8); L[0].radius = 64; L[0].color = v3(0.8, 0.5, 0.3); L[0].scale = 1; L[0].shadow = cone; L[0].fraction = v3(0.5, 0.25, 0.1);
  L[1].origin = v3(4, 4, 4); L[1].radius = 32; L[1].color = v3(0.2, 0.4, 0.6); L[1].scale = 1; L[1].shadow = cube; L[1].fraction = v3(0.2, 0.4, 0.6);
  L[2].origin = v3(24, 8, 16); L[2].radius = 40; L[2].color = v3(0.5, 0.5, 0.5); L[2].scale = 0.5; L[2].cone = &kernel_cone; L[2].shadow = none; L[2].fraction = v3(0, 0, 0);
  memset(SHADOW_L, 0, sizeof SHADOW_L);
  for (int i = 0; i < 2; i++) { SHADOW_L[i].origin = L[i].origin; SHADOW_L[i].radius = L[i].radius; SHADOW_L[i].fraction = L[i].fraction; SHADOW_L[i].shadow = L[i].shadow; }
  memset(LIT, 0, sizeof LIT);
  LIT[0].kind = "vertex";
  static const char *const passes[3] = {"lightmap", "texture", "material-lightmap"};
  for (int i = 1; i <= 3; i++) { LIT[i].kind = "q2-world"; LIT[i].pass = passes[i - 1]; LIT[i].lights = L; LIT[i].light_count = 3; LIT[i].atlas = &kernel_atlas; }
  LIT[4] = LIT[1]; LIT[4].pass = "model"; LIT[4].has_shade_scale = true; LIT[4].shade_scale = 1.5;
  static qa_q2_fragment_light l1l3[2]; l1l3[0] = L[0]; l1l3[1] = L[2];
  LIT[5] = LIT[1]; LIT[5].pass = "model"; LIT[5].lights = l1l3; LIT[5].light_count = 2;
  LIT[6].kind = "q2-model-shadow"; LIT[6].model_shadow_lights = SHADOW_L; LIT[6].model_shadow_light_count = 2; LIT[6].atlas_value = kernel_atlas; LIT[6].has_shade_scale = true; LIT[6].shade_scale = 1.5;
}
typedef struct kv { double x, y, z, iw, u, v, u2, v2, c[4]; } kv;
static const kv TRIS[2][3] = {
  {{1.5, 2.25, -0.5, 1, 0, 0, 1, 1, {1, 0.5, 0.25, 0.75}}, {29.75, 4.5, 0.25, 0.5, 2.5, -0.5, 0, 2, {0.25, 1, 0.5, 0.5}}, {6.25, 30.5, 0.75, 0.25, 0.25, 3, 3, 0, {0.5, 0.25, 1, 0.25}}},
  {{30.25, 27.5, 0.5, 0.25, 0, 0, 0.5, 0.5, {0.2, 0.9, 0.4, 1}}, {3.5, 28.75, -0.25, 1, 8, 1, 4, 1, {0.9, 0.2, 0.6, 0.5}}, {27.25, 1.25, 0, 0.5, 2, 9, 1, 5, {0.4, 0.6, 0.1, 0}}},
};
static double kedge(const kv *a, const kv *b, double x, double y) { return (a->y - b->y) * x + (b->x - a->x) * y + (a->x * b->y - a->y * b->x); }
static bool kll(const kv *a, const kv *b) { return b->y < a->y || (b->y == a->y && b->x < a->x); }
static double min3(double a, double b, double c) { return fmin(fmin(a, b), c); }
static double max3(double a, double b, double c) { return fmax(fmax(a, b), c); }
static qa_triangle_setup build_setup(const kv tri[3], const kernel_case *o, qa_cpu_triangle_lighting *lighting) {
  const kv *a = &tri[0], *b = &tri[1], *c = &tri[2];
  double area = kedge(a, b, c->x, c->y);
  if (area < 0) { const kv *p = b; b = c; c = p; area = -area; }
  double inv = 1 / area;
  double aiw = o->iw_zero ? 0 : a->iw, biw = o->iw_zero ? 0 : b->iw, ciw = o->iw_zero ? 0 : c->iw;
  double aax = b->y - c->y, aay = c->x - b->x, aac = b->x * c->y - b->y * c->x, abx = c->y - a->y, aby = a->x - c->x, abc = c->x * a->y - c->y * a->x, acx = a->y - b->y, acy = b->x - a->x, acc = a->x * b->y - a->y * b->x;
  double ua = a->u, va = a->v, u2a = a->u2, v2a = a->v2;
  double au = (a->u - ua) * aiw, bu = (b->u - ua) * biw, cu = (c->u - ua) * ciw, av = (a->v - va) * aiw, bv = (b->v - va) * biw, cv = (c->v - va) * ciw;
  double au2 = (a->u2 - u2a) * aiw, bu2 = (b->u2 - u2a) * biw, cu2 = (c->u2 - u2a) * ciw, av2 = (a->v2 - v2a) * aiw, bv2 = (b->v2 - v2a) * biw, cv2 = (c->v2 - v2a) * ciw;
  double qdx = (aiw * aax + biw * abx + ciw * acx) * inv, qdy = (aiw * aay + biw * aby + ciw * acy) * inv;
  const qa_bound_texture *tex = &TEX[o->tex], *sec = &TEX[o->sec];
  memset(lighting, 0, sizeof *lighting);
  lighting->base.parameters = LIT[o->lit]; lighting->base.depth = o->lit == 0 ? NULL : &kernel_depth;
  const kv *order[3] = {a, b, c};
  for (int i = 0; i < 3; i++) lighting->positions[i] = v3(order[i]->x, order[i]->y, order[i]->z * 8);
  lighting->normals[0] = v3(0, 0, 1); lighting->normals[1] = v3(0, 0.6, 0.8); lighting->normals[2] = v3(0.6, 0, 0.8);
  qa_triangle_setup s; memset(&s, 0, sizeof s);
  s.lighting = lighting;
  s.min_x = fmax(0, ceil(min3(a->x, b->x, c->x) - 0.5)); s.max_x = fmin(W - 1, floor(max3(a->x, b->x, c->x) - 0.5));
  s.min_y = fmax(0, ceil(min3(a->y, b->y, c->y) - 0.5)); s.max_y = fmin(H - 1, floor(max3(a->y, b->y, c->y) - 0.5));
  s.inverse_area = inv; s.depth_near = 0; s.depth_far = 1;
  s.edge_ax = aax; s.edge_ay = aay; s.edge_ac = aac; s.edge_bx = abx; s.edge_by = aby; s.edge_bc = abc; s.edge_cx = acx; s.edge_cy = acy; s.edge_cc = acc;
  s.attribute_ax = aax; s.attribute_ay = aay; s.attribute_ac = aac; s.attribute_bx = abx; s.attribute_by = aby; s.attribute_bc = abc; s.attribute_cx = acx; s.attribute_cy = acy; s.attribute_cc = acc;
  s.az = a->z; s.bz = b->z; s.cz = c->z; s.polygon_depth_offset = o->offset;
  s.plane_depth = qa_cpu_clamp01(qa_cpu_clamp01(a->z * 0.5 + 0.5) + o->offset);
  s.aiw = aiw; s.biw = biw; s.ciw = ciw; s.au = au; s.bu = bu; s.cu = cu; s.av = av; s.bv = bv; s.cv = cv; s.au2 = au2; s.bu2 = bu2; s.cu2 = cu2; s.av2 = av2; s.bv2 = bv2; s.cv2 = cv2;
  /* iw_zero 2 keeps finite colors over a zero 1/w plane: colors reach +-Infinity and
   * clamp while texture coordinates are NaN, so the sampler's NaN handling shows. */
  double cwa = o->iw_zero == 2 ? 1 : aiw, cwb = o->iw_zero == 2 ? 1 : biw, cwc = o->iw_zero == 2 ? 1 : ciw;
  s.ar = a->c[0] * cwa; s.br = b->c[0] * cwb; s.cr = c->c[0] * cwc; s.ag = a->c[1] * cwa; s.bg = b->c[1] * cwb; s.cg = c->c[1] * cwc;
  s.ab = a->c[2] * cwa; s.bb = b->c[2] * cwb; s.cb = c->c[2] * cwc; s.aa = a->c[3] * cwa; s.ba = b->c[3] * cwb; s.ca = c->c[3] * cwc;
  s.edge_a_inclusive = kll(b, c); s.edge_b_inclusive = kll(c, a); s.edge_c_inclusive = kll(a, b);
  s.white = o->white; s.depth_write = o->depth_write; s.stencil_enabled = o->stencil; s.color_write = o->color_write; s.texture_consumed = o->consumed;
  s.primary_alpha = tex->kind != QA_BOUND_TEXTURE_INCOMPLETE && !strcmp(tex->internal_format, "rgba8");
  s.secondary_alpha = sec->kind != QA_BOUND_TEXTURE_INCOMPLETE && !strcmp(sec->internal_format, "rgba8");
  s.constant_depth = o->constant_depth; s.width = W; s.height = H; s.blend.source = o->src; s.blend.destination = o->dst; s.alpha_bits = o->alpha_bits;
  s.blending = o->blending; s.depth_test = o->depth_test; s.alpha_test = o->alpha_test; s.secondary_environment = o->env; s.texture = *tex; s.secondary_texture = *sec;
  qa_texture_plane_derivative d;
  d.u_anchor = ua; d.v_anchor = va; d.u_dx = (au * aax + bu * abx + cu * acx) * inv; d.v_dx = (av * aax + bv * abx + cv * acx) * inv; d.q_dx = qdx;
  d.u_dy = (au * aay + bu * aby + cu * acy) * inv; d.v_dy = (av * aay + bv * aby + cv * acy) * inv; d.q_dy = qdy; s.derivative = d;
  d.u_anchor = u2a; d.v_anchor = v2a; d.u_dx = (au2 * aax + bu2 * abx + cu2 * acx) * inv; d.v_dx = (av2 * aax + bv2 * abx + cv2 * acx) * inv;
  d.u_dy = (au2 * aay + bu2 * aby + cu2 * acy) * inv; d.v_dy = (av2 * aay + bv2 * aby + cv2 * acy) * inv; s.secondary_derivative = d;
  s.stencil_function = o->fn; s.stencil_compare_mask = o->cm; s.stencil_write_mask = o->wm; s.stencil_maximum = o->max; s.stencil_depth_fail = o->df; s.stencil_depth_pass = o->dp;
  if (o->fog != 0) { s.fog = &FOG[o->fog]; s.has_fog_depth_scale = true; s.fog_depth_scale = 1; }
  if (o->lum) s.texture_effect = "luminance-alpha";
  return s;
}
static void fill_fb(qa_cpu_framebuffer *fb, uint32_t *color, double *depth, uint32_t *stencil, double fill) {
  for (uint32_t i = 0; i < W * H; i++) { color[i] = (i + 1) * 2654435761u; depth[i] = (i % 7) / 7.0; stencil[i] = fill >= 0 ? (uint32_t)fill : i % 5; }
  memset(fb, 0, sizeof *fb);
  fb->width = W; fb->height = H; fb->pixels = (uint8_t *)color; fb->color_words = (int32_t *)color; fb->depth = depth; fb->stencil = stencil; fb->stride = W; fb->storage_count = W * H;
}
typedef struct kernel_run { const kernel_case *c; qa_cpu_framebuffer *fb; int sampled; } kernel_run;
static void kernel_run_fn(void *ctx) {
  kernel_run *k = (kernel_run *)ctx; qa_cpu_sample sampled = {1, 1, 1, 1};
  for (int t = 0; t < 2; t++) { qa_cpu_triangle_lighting lighting; qa_triangle_setup s = build_setup(TRIS[t], k->c, &lighting); if (qa_run_triangle_rows_default(&s, k->fb, &sampled)) k->sampled++; }
}
static uint32_t fb_hash(const uint32_t *color, double *depth, const uint32_t *stencil) {
  uint32_t h = fnv(fnv(FNV_START, color, W * H * 4), stencil, W * H * 4);
  static const uint64_t canonical_nan = 0x7ff8000000000000ULL;
  for (int i = 0; i < W * H; i++) h = isnan(depth[i]) ? fnv(h, &canonical_nan, 8) : fnv(h, &depth[i], 8);
  return h;
}
static void test_kernel_oracle(void) {
  kernel_data();
  size_t n = sizeof KERNEL_CASES / sizeof KERNEL_CASES[0]; CHECK(n > 150);
  for (size_t i = 0; i < n; i++) {
    const kernel_case *c = &KERNEL_CASES[i];
    static uint32_t color[W * H], stencil[W * H]; static double depth[W * H]; qa_cpu_framebuffer fb;
    fill_fb(&fb, color, depth, stencil, c->fill);
    kernel_run run = {c, &fb, 0}; char message[256] = "";
    bool ok = qa_range_error_catch(kernel_run_fn, &run, message, sizeof message);
    if (c->error != NULL) { CHECK_NAMED(!ok && !strcmp(message, c->error), c->name); continue; }
    CHECK_NAMED(ok, c->name);
    if (!ok) { fprintf(stderr, "  raised: %s\n", message); continue; }
    uint32_t h = fb_hash(color, depth, stencil);
    if (h != c->hash) fprintf(stderr, "  %s: got 0x%08x want 0x%08x\n", c->name, h, c->hash);
    CHECK_NAMED(h == c->hash, c->name);
    CHECK_NAMED(run.sampled == c->sampled_triangles, c->name);
  }
}
/* Direct edges: oracle values printed by Bun from the TS functions. */
static void test_kernel_edges(void) {
  CHECK(isnan(qa_cpu_clamp01(NAN)));
  CHECK(!signbit(qa_cpu_clamp01(-0.0)) && qa_cpu_clamp01(-1) == 0 && qa_cpu_clamp01(2) == 1 && qa_cpu_clamp01(0.25) == 0.25);
  CHECK(qa_cpu_color_word(NAN, 1, 1, 1) == 0xffffff00u); /* colorWord(NaN,1,1,1)>>>0 is 4294967040 */
  qa_blend_state one_zero = {"one", "zero"};
  CHECK(qa_cpu_blend(NAN, 0.5, 0.5, 0.5, one_zero, false) == 0); /* TS: NaN, stored to a Uint8Array as 0 */
  uint32_t stencil = 0xffffffffu;
  CHECK(qa_cpu_stencil_fragment(&stencil, 1, 0, true, "always", 4294967295.0, 4294967295.0, 4294967295.0, "keep", "increment"));
  CHECK(stencil == 0xffffffffu); /* Math.min(2^32-1, 2^32), not a wrap to 0 */
}
/* Bit-identical parallel kernel runs over every kernel fragment state: the coordinator
 * and workers use the encoded and decoded setup, so this also covers the codec. */
static void test_pool_kernel_cases(void) {
  setenv("QTS_CPU_WORKERS", "3", 1);
  kernel_data();
  size_t n = sizeof KERNEL_CASES / sizeof KERNEL_CASES[0];
  for (size_t i = 0; i < n; i++) {
    const kernel_case *c = &KERNEL_CASES[i];
    if (c->error != NULL) continue;
    static uint32_t color[W * H], stencil[W * H]; static double depth[W * H]; qa_cpu_framebuffer fb;
    fill_fb(&fb, color, depth, stencil, c->fill);
    qa_row_shared_framebuffer shared; shared.width = W; shared.height = H; shared.color = (uint8_t *)color; shared.depth = depth; shared.stencil = stencil;
    qa_row_parallel_pool pool; CHECK(qa_row_parallel_pool_try_create(&pool, shared));
    qa_cpu_triangle_lighting lighting[2]; qa_triangle_setup setups[2];
    for (int t = 0; t < 2; t++) setups[t] = build_setup(TRIS[t], c, &lighting[t]);
    qa_row_parallel_pool_run(&pool, setups, 2);
    CHECK(pool.jobs_run == 1);
    qa_row_parallel_pool_close(&pool);
    uint32_t h = fb_hash(color, depth, stencil);
    if (h != c->hash) fprintf(stderr, "  pool %s: got 0x%08x want 0x%08x\n", c->name, h, c->hash);
    CHECK_NAMED(h == c->hash, c->name);
  }
}
/* TS textureId/lightingId/depthId and the job-scoped id reset. */
static void test_registries(void) {
  setenv("QTS_CPU_WORKERS", "2", 1);
  qa_row_parallel_pool pool; CHECK(qa_row_parallel_pool_try_create(&pool, pool_shared()));
  qa_row_registries reg = qa_row_parallel_pool_registries(&pool);
  kernel_data();
  CHECK(reg.texture_id(reg.ctx, &TEX[0]) == -1);
  double image_a = reg.texture_id(reg.ctx, &TEX[3]), image_b = reg.texture_id(reg.ctx, &TEX[4]);
  CHECK(image_a != image_b); CHECK(reg.texture_id(reg.ctx, &TEX[3]) == image_a);
  double constant_a = reg.texture_id(reg.ctx, &TEX[1]), constant_b = reg.texture_id(reg.ctx, &TEX[2]);
  qa_bound_texture copy = TEX[1];
  CHECK(constant_a != constant_b); CHECK(reg.texture_id(reg.ctx, &copy) == constant_a);
  /* The known defect: one position array, different culled sets. */
  qa_q2_fragment_light set_a[1], set_b[1]; set_a[0] = L[0]; set_b[0] = L[1];
  qa_batch_lighting lit_a = LIT[2], lit_b = LIT[2];
  lit_a.world_positions = world; lit_b.world_positions = world; lit_a.lights = set_a; lit_a.light_count = 1; lit_b.lights = set_b; lit_b.light_count = 1;
  double id_a = reg.lighting_id(reg.ctx, &lit_a), id_b = reg.lighting_id(reg.ctx, &lit_b);
  CHECK(id_a != id_b); CHECK(reg.lighting_id(reg.ctx, &lit_a) == id_a); CHECK(reg.lighting_id(reg.ctx, &lit_b) == id_b);
  for (int i = 0; i < 300; i++) { /* growth rehash keeps earlier ids */
    static qa_q2_fragment_light many[300]; qa_batch_lighting l = LIT[2]; l.lights = &many[i]; l.light_count = 1;
    double id = reg.lighting_id(reg.ctx, &l); CHECK(id != id_a && reg.lighting_id(reg.ctx, &l) == id);
  }
  CHECK(reg.lighting_id(reg.ctx, &lit_a) == id_a);
  CHECK(reg.depth_id(reg.ctx, NULL) == -1);
  double depth_id = reg.depth_id(reg.ctx, &kernel_depth); CHECK(depth_id >= 0);
  qa_row_parallel_pool_run(&pool, NULL, 0); /* new job: texture and lighting ids restart, never reused */
  double image_again = reg.texture_id(reg.ctx, &TEX[3]); CHECK(image_again > constant_b);
  CHECK(reg.lighting_id(reg.ctx, &lit_a) > id_b);
  CHECK(reg.depth_id(reg.ctx, &kernel_depth) == depth_id); /* depth ids persist */
  qa_row_parallel_pool_close(&pool);
  unsetenv("QTS_CPU_WORKERS");
}
typedef struct run_ctx { qa_row_parallel_pool *pool; const qa_triangle_setup *setups; size_t count; } run_ctx;
static void run_fn(void *ctx) { run_ctx *c = (run_ctx *)ctx; qa_row_parallel_pool_run(c->pool, c->setups, c->count); }
static void registries_fn(void *ctx) { (void)qa_row_parallel_pool_registries((qa_row_parallel_pool *)ctx); }
static void test_shutdown_and_errors(void) {
  setenv("QTS_CPU_WORKERS", "3", 1);
  qa_row_parallel_pool pool; CHECK(qa_row_parallel_pool_try_create(&pool, pool_shared()));
  CHECK(qa_row_parallel_pool_worker_count(&pool) == 3);
  /* Every band reaches a fragment outside the buffer; the barrier still completes. */
  kernel_case base = KERNEL_CASES[0]; kv bad[3];
  memcpy(bad, TRIS[0], sizeof bad); bad[0].x = 0; bad[0].y = 0; bad[1].x = 5000; bad[1].y = 0; bad[2].x = 0; bad[2].y = 5000;
  qa_cpu_triangle_lighting lighting; qa_triangle_setup setup = build_setup(bad, &base, &lighting); setup.max_x = 4000;
  run_ctx failing = {&pool, &setup, 1}; char message[256] = "";
  CHECK(!qa_range_error_catch(run_fn, &failing, message, sizeof message));
  CHECK(!strcmp(message, "Row-parallel worker failed: Fragment is outside the depth buffer"));
  CHECK(pool.jobs_run == 1);
  /* The failure does not stick to later jobs. */
  qa_cpu_triangle_lighting good_lighting; qa_triangle_setup good = build_setup(TRIS[0], &base, &good_lighting);
  run_ctx ok = {&pool, &good, 1};
  CHECK(qa_range_error_catch(run_fn, &ok, message, sizeof message));
  CHECK(pool.jobs_run == 2);
  qa_row_parallel_pool_close(&pool); qa_row_parallel_pool_close(&pool); qa_row_parallel_pool_close(NULL);
  CHECK(pool.impl == NULL); CHECK(pool.jobs_run == 2); CHECK(qa_row_parallel_pool_worker_count(&pool) == 3);
  CHECK(!qa_range_error_catch(run_fn, &ok, message, sizeof message));
  CHECK(!strcmp(message, "Row-parallel pool is closed"));
  CHECK(!qa_range_error_catch(registries_fn, &pool, message, sizeof message));
  CHECK(!strcmp(message, "Row-parallel pool is closed"));
  unsetenv("QTS_CPU_WORKERS");
}

int main(void) {
  compare_scene(SCENE_VERTEX, 1, true, SCENE_VERTEX_PIXELS);
  compare_scene(SCENE_FRAGMENT, 1, true, SCENE_FRAGMENT_PIXELS);
  test_culled_sets_are_distinct();
  compare_scene(SCENE_CULLED, 1, true, SCENE_CULLED_PIXELS);
  compare_scene(SCENE_TWO_JOBS, 3, false, 0);
  test_dispatch_threshold();
  test_worker_env();
  test_kernel_edges();
  test_kernel_oracle();
  test_pool_kernel_cases();
  test_registries();
  test_shutdown_and_errors();
  printf("row parallel: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
