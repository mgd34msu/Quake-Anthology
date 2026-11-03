/* Checks for divergences found by the gate-render2 full read of src/render. Expected values come from the TS oracle
 * tests/render/oracles/gate_render2.ts (bun), quoted next to each check. GPL-2.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/render/debug_graph.h"
#include "qa/render/scene/geometry.h"
#include "qa/render/scene/image_policy.h"
#include "qa/render/scene/portal.h"
#include "qa/render/scene/resources.h"
#include "qa/render/scene/shadows.h"
#include "qa/render/scene/skin.h"
#include "qa/render/scene/source_sort.h"
#include "qa/render/scene/textures.h"
#include "qa/render/scene/view.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); } } while (0)

/* pack_wrap 2621570943; pack_negative 4294836224 (int32 shifts wrap, no C undefined shift) */
static void test_pack(void) {
  CHECK(qa_pack_source_draw_sort(20000, 1022, 31, 3) == 2621570943u);
  CHECK(qa_pack_source_draw_sort(-1, 0, 0, 0) == 4294836224u);
}

/* flood_short_palette [7,7,7,7]: colors past a short palette are undefined, never black */
static void test_flood(void) {
  static const uint8_t colors[6] = {10, 10, 10, 20, 20, 20};
  qa_palette palette; memset(&palette, 0, sizeof palette);
  palette.colors.bytes = colors; palette.colors.length = 6;
  const uint8_t indices[4] = {5, 5, 7, 5};
  size_t length = 0;
  uint8_t *result = qa_flood_skin(indices, 4, 2, 2, palette, &length);
  CHECK(result != NULL && length == 4);
  if (result) for (int i = 0; i < 4; i++) CHECK(result[i] == 7);
  free(result);
}

static qa_scene_camera camera(void) {
  qa_scene_camera c; memset(&c, 0, sizeof c);
  c.axis.vectors[0] = (qa_vec3){1, 0, 0}; c.axis.vectors[1] = (qa_vec3){0, 1, 0}; c.axis.vectors[2] = (qa_vec3){0, 0, 1};
  c.projection = qa_scene_perspective_projection(90, 90, 4096, 4);
  c.viewport.width = 64; c.viewport.height = 64;
  return c;
}

/* portal_nan false: Math.min(shortest, NaN) is NaN, so the range test fails */
static void test_portal(void) {
  qa_material_vertex v[3]; memset(v, 0, sizeof v);
  v[0].position = (qa_vec3){NAN, 0, 0}; v[1].position = (qa_vec3){50, 1, 0}; v[2].position = (qa_vec3){50, 0, 1};
  for (int i = 0; i < 3; i++) { v[i].normal = (qa_vec3){1, 0, 0}; v[i].color = (qa_vec4){255, 255, 255, 255}; }
  const double indices[3] = {0, 1, 2};
  qa_material_geometry mesh = {v, 3, indices, 3};
  CHECK(!qa_portal_surface_offscreen(mesh, camera(), 10, false));
}

/* far_clip_nan NaN; bounds_nan ["NaN","2","-1","NaN","5","3"] */
static void test_nan_bounds(void) {
  qa_bounds b = {{NAN, 0, 0}, {1, 1, 1}};
  CHECK(isnan(qa_scene_far_clip((qa_vec3){0, 0, 0}, b)));
  qa_vec3 points[2] = {{1, 2, 3}, {NAN, 5, -1}};
  qa_bounds g = qa_scene_geometry_bounds(points, 2);
  CHECK(isnan(g.min.x) && g.min.y == 2 && g.min.z == -1 && isnan(g.max.x) && g.max.y == 5 && g.max.z == 3);
}

/* netgraph_nan [["0","8","2","32","8"],["1","NaN","1","NaN","208"],["0","40","1","0","0"]] */
static void test_netgraph(void) {
  qa_source_debug_graph graph; qa_source_debug_graph_init(&graph);
  qa_debug_graph_settings settings = {0, 0, 1, 32, 1, 0};
  qa_source_debug_graph_add_network(&graph, (qa_debug_graph_packet){0, 0, NAN}, settings);
  qa_debug_graph_bar bars[4];
  qa_rect rect = {0, 0, 2, 40};
  size_t n = qa_source_debug_graph_bars(&graph, rect, settings, bars, 4);
  CHECK(n == 3);
  CHECK(bars[0].rect.x == 0 && bars[0].rect.y == 8 && bars[0].rect.width == 2 && bars[0].rect.height == 32 && bars[0].color == 8);
  CHECK(bars[1].rect.x == 1 && isnan(bars[1].rect.y) && bars[1].rect.width == 1 && isnan(bars[1].rect.height) && bars[1].color == 208);
  CHECK(bars[2].rect.x == 0 && bars[2].rect.y == 40 && bars[2].rect.width == 1 && bars[2].rect.height == 0 && bars[2].color == 0);
}

/* policy_ff_source true */
static void test_policy(void) {
  qa_scene_image_policy_owned owned;
  CHECK(qa_image_policy_from_controls((qa_scene_image_controls){1, 4, "\f source \v"}, &owned));
  CHECK(!owned.policy.has_formats);
  qa_scene_image_policy_owned_destroy(&owned);
}

/* ---- texture loader: skip_undecodable ["pics/x.jpg",2,1,1]; strict_rethrows "JpegSourceError" ---- */
static const uint8_t TGA[26] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 1, 0, 32, 8, 1, 2, 3, 255, 4, 5, 6, 255};
static const uint8_t GARBAGE[4] = {1, 2, 3, 4};
static int reads;
static bool reader_read(void *ctx, const char *path, qa_scene_asset *out, uint8_t **owned) {
  (void)ctx; reads++;
  const uint8_t *src = NULL; size_t n = 0;
  if (!strcmp(path, "pics/x.jpg")) { src = GARBAGE; n = sizeof GARBAGE; }
  else if (!strcmp(path, "pics/x.tga")) { src = TGA; n = sizeof TGA; }
  else return false;
  uint8_t *copy = malloc(n); memcpy(copy, src, n);
  memset(out, 0, sizeof *out); out->bytes = copy; out->length = n; out->source_sample.source_name = path;
  *owned = copy;
  return true;
}
static double clock_zero(void *ctx) { (void)ctx; return 0; }
static int session_token;
static qa_renderer_resource_owner owner(void) {
  qa_renderer_resource_owner o; memset(&o, 0, sizeof o);
  o.identity = &session_token; o.session.token = &session_token; return o;
}
typedef struct strict_load { qa_scene_texture_loader *loader; } strict_load;
static void strict_body(void *ctx) {
  qa_scene_texture_load_options options; memset(&options, 0, sizeof options); options.has_mipmap = true; options.mipmap = false;
  (void)qa_scene_texture_loader_load(((strict_load *)ctx)->loader, "pics/x.jpg", &options);
}
static void test_texture_skip(void) {
  qa_media_clock clock = {NULL, clock_zero};
  qa_scene_asset_reader reader = {NULL, reader_read, NULL};
  qa_scene_image_registry images; qa_scene_image_registry_init(&images, owner(), &clock);
  qa_scene_texture_loader loader;
  CHECK(qa_scene_texture_loader_init(&loader, &images, reader, NULL, NULL));
  qa_scene_texture_load_options options; memset(&options, 0, sizeof options); options.has_mipmap = true; options.mipmap = false;
  const qa_scene_texture *texture = qa_scene_texture_loader_load(&loader, "pics/x.jpg", &options);
  CHECK(texture != NULL);
  if (texture) {
    CHECK(!strcmp(texture->name, "pics/x.jpg"));
    CHECK(texture->width == 2 && texture->height == 1 && texture->content.level_count == 1);
  }
  qa_scene_texture_loader_destroy(&loader);
  qa_scene_image_registry_destroy(&images);

  qa_scene_image_registry strict_images; qa_scene_image_registry_init(&strict_images, owner(), &clock);
  qa_scene_texture_loader strict;
  qa_scene_texture_loader_options strict_options; memset(&strict_options, 0, sizeof strict_options); strict_options.strict = true;
  CHECK(qa_scene_texture_loader_init(&strict, &strict_images, reader, NULL, &strict_options));
  strict_load scope = {&strict};
  qa_caught_error caught;
  /* TS JpegSourceError; the C JPEG decoder raises it as a catchable BinaryError (review-b). */
  CHECK(!qa_try(strict_body, &scope, &caught));
  qa_scene_texture_loader_destroy(&strict);
  qa_scene_image_registry_destroy(&strict_images);
}

/* ---- registry: queued updates own their level; isolate restores the queue on a throw (TS try/finally) ---- */
static void test_update_owns_level(void) {
  qa_media_clock clock = {NULL, clock_zero};
  qa_scene_image_registry images; qa_scene_image_registry_init(&images, owner(), &clock);
  uint8_t px[4] = {9, 8, 7, 6};
  qa_image_level level = {1, 1, {px, 4}};
  qa_render_image content; memset(&content, 0, sizeof content);
  content.kind = QA_RENDER_IMAGE_RGBA8; content.levels = &level; content.level_count = 1;
  qa_texture_sampling sampling = {"repeat", "linear"};
  qa_renderer_image image = qa_scene_image_registry_register_generated(&images, "*img", content, sampling)->image;
  uint8_t *update = malloc(4); memcpy(update, (uint8_t[4]){1, 2, 3, 4}, 4);
  qa_scene_image_registry_update(&images, image, 0, (qa_image_level){1, 1, {update, 4}});
  free(update); /* the producer frees its buffer, as a rebuilt lightmap does */
  size_t count = 0;
  qa_image_resource_operation *ops = qa_scene_image_registry_drain_pending(&images, &count);
  CHECK(count == 2 && ops[1].kind == QA_IMAGE_OP_UPDATE && ops[1].update_level != NULL);
  if (count == 2 && ops[1].update_level) {
    const uint8_t *bytes = ops[1].update_level->pixels.bytes;
    CHECK(ops[1].update_level->pixels.length == 4 && bytes[0] == 1 && bytes[3] == 4);
  }
  free(ops);
  /* a pending update is released with the registry */
  qa_scene_image_registry_update(&images, image, 0, level);
  qa_scene_image_registry_destroy(&images);
}
typedef struct isolate_scope { qa_scene_image_registry *images; qa_renderer_image image; } isolate_scope;
static void isolated_raise(void *ctx) {
  isolate_scope *s = ctx;
  qa_scene_image_registry_release(s->images, s->image);
  qa_raise("Error", 0, 0, "isolated failure");
}
static size_t collected;
static void collect(void *ctx, qa_image_resource_operation *ops, size_t count) { (void)ctx; collected = count; free(ops); }
typedef struct isolate_run { isolate_scope *scope; } isolate_run;
static void isolate_body(void *ctx) {
  isolate_scope *s = ((isolate_run *)ctx)->scope;
  qa_scene_image_registry_isolate(s->images, isolated_raise, s, collect, NULL);
}
static void test_isolate_finally(void) {
  qa_media_clock clock = {NULL, clock_zero};
  qa_scene_image_registry images; qa_scene_image_registry_init(&images, owner(), &clock);
  uint8_t px[4] = {0};
  qa_image_level level = {1, 1, {px, 4}};
  qa_render_image content; memset(&content, 0, sizeof content);
  content.kind = QA_RENDER_IMAGE_RGBA8; content.levels = &level; content.level_count = 1;
  qa_texture_sampling sampling = {"repeat", "linear"};
  qa_renderer_image image = qa_scene_image_registry_register_generated(&images, "*img", content, sampling)->image;
  isolate_scope scope = {&images, image};
  isolate_run run = {&scope};
  qa_caught_error caught;
  collected = 99;
  CHECK(!qa_try(isolate_body, &run, &caught));
  CHECK(!strcmp(caught.message, "isolated failure"));
  CHECK(collected == 1); /* the release reached inside the isolated call */
  size_t count = 0;
  qa_image_resource_operation *ops = qa_scene_image_registry_drain_pending(&images, &count);
  CHECK(count == 1 && ops[0].kind == QA_IMAGE_OP_CREATE); /* the outer queue is restored */
  free(ops);
  qa_scene_image_registry_destroy(&images);
}

/* cone_fov_nan NaN; pack_nan_shelf [["0","0","1500","NaN"],["0","NaN","1000","10"]] */
static void test_shadow_nan(void) {
  CHECK(isnan(qa_shadow_cone_fov(NAN)));
  qa_shadow_pack_request requests[2] = {{1500, NAN}, {1000, 10}};
  qa_shadow_pack_result slots[2];
  qa_pack_shadow_atlas(requests, 2, false, 0, slots);
  CHECK(!slots[0].is_null && slots[0].rect.x == 0 && slots[0].rect.y == 0 && slots[0].rect.width == 1500 && isnan(slots[0].rect.height));
  CHECK(!slots[1].is_null && slots[1].rect.x == 0 && isnan(slots[1].rect.y) && slots[1].rect.width == 1000 && slots[1].rect.height == 10);
}

int main(void) {
  test_shadow_nan();
  test_pack();
  test_flood();
  test_portal();
  test_nan_bounds();
  test_netgraph();
  test_policy();
  test_texture_skip();
  test_update_owns_level();
  test_isolate_finally();
  printf("%d checks, %d failures\n", checks, failures);
  return failures != 0;
}
