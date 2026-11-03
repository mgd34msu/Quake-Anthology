/* C checks for src/app/bootstrap/character-turntable.ts, reproducing
 * tests/first-playable/character-turntable.test.ts. Expected values come
 * from running the TS module under bun on the same inputs (scene digests,
 * rendered-image FNV-1a hashes). Content rows need QTS_TEST_CORPUS (the
 * same root the TS test reads); they are skipped without it.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/character_turntable.h"
#include "qa/app/bootstrap/content.h"
#include "qa/foundation/support.h"
static int checks, failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

static uint32_t fnv(const uint8_t *bytes, size_t length) {
  uint32_t hash = 0x811c9dc5U;
  for (size_t i = 0; i < length; i++) { hash ^= bytes[i]; hash *= 0x01000193U; }
  return hash;
}
static size_t visible_pixels(const qa_image_level *image) {
  size_t count = 0;
  for (size_t offset = 3; offset < image->pixels.length; offset += 4) if (image->pixels.bytes[offset] > 20) count++;
  return count;
}
static size_t differing_bytes(const qa_image_level *left, const qa_image_level *right) {
  CHECK(left->pixels.length == right->pixels.length);
  size_t count = 0;
  for (size_t offset = 0; offset < left->pixels.length; offset++) if (left->pixels.bytes[offset] != right->pixels.bytes[offset]) count++;
  return count;
}

/* ---- rasterizer against bun (no content) ---- */
static qa_app_turntable_texture texture(double width, double height, const char *alpha_test, int seed) {
  size_t length = (size_t)(width * height * 4);
  uint8_t *pixels = malloc(length);
  for (size_t i = 0; i < length; i++) pixels[i] = (uint8_t)((i * 37 + (size_t)seed * 11) & 255);
  return (qa_app_turntable_texture){width, height, pixels, length, alpha_test};
}
static qa_app_turntable_vertex vertex(double x, double y, double z, double nx, double ny, double nz, double u, double t, double texture_index) {
  return (qa_app_turntable_vertex){{x, y, z}, {nx, ny, nz}, {u, t}, texture_index};
}
typedef struct render_args { const qa_app_character_turntable_scene *scene; double width, height; } render_args;
static void render_invalid(void *arg) {
  render_args *args = arg;
  qa_image_level image;
  qa_app_render_character_turntable(args->scene, 0, args->width, args->height, &image);
  free((void *)image.pixels.bytes);
}
static void test_rasterizer(void) {
  qa_app_turntable_vertex vertices[] = {vertex(-10,0,-20,0,-1,0,0,0,0), vertex(10,0,-20,0,-1,0,1,0,0), vertex(0,0,20,0,-1,0.2,0.5,1,0),
    vertex(-8,5,-10,1,0,0,0.2,0.1,1), vertex(8,-5,15,0,1,0,1.7,-0.3,1), vertex(0,9,0,0,0,1,0.4,2.2,1),
    vertex(-5,-5,-5,0,0,-1,0.1,0.9,2), vertex(5,5,5,0.5,0.5,0.5,0.9,0.1,2), vertex(-5,5,10,0,0,0,0.3,0.3,2),
    vertex(0,0,0,0,0,1,0,0,7), vertex(1,1,1,0,0,1,1,1,7), vertex(2,0,3,0,0,1,0,1,7)};
  double indices[] = {0,1,2, 3,4,5, 6,7,8, 9,10,11, 0,1,99, 2,1,0};
  qa_app_turntable_texture textures[] = {texture(4,4,"none",1), texture(8,2,"gt0",2), texture(3,5,"ge128",3)};
  qa_app_character_turntable_scene scene = {vertices, 12, indices, 18, textures, 3, {0.5, -0.25, 1}, 14, 20, -20, 0.85, 1};
  static const struct { double yaw, width, height; uint32_t hash; } rows[] = {
    {0, 64, 48, 0xcc3e6a7cU}, {0.85, 64, 48, 0x934cebfaU}, {2.1, 33, 71, 0xc138fe64U}, {-1.3, 96, 96, 0x2585e11cU}};
  for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
    qa_image_level image;
    qa_app_render_character_turntable(&scene, rows[i].yaw, rows[i].width, rows[i].height, &image);
    CHECK(image.width == rows[i].width && image.height == rows[i].height);
    CHECK(fnv(image.pixels.bytes, image.pixels.length) == rows[i].hash);
    free((void *)image.pixels.bytes);
  }
  textures[2].alpha_test = "lt128";
  qa_image_level image;
  qa_app_render_character_turntable(&scene, 0.4, 50, 60, &image);
  CHECK(fnv(image.pixels.bytes, image.pixels.length) == 0xc3defc82U);
  free((void *)image.pixels.bytes);
  render_args invalid = {&scene, 1.5, 3};
  qa_caught_error caught;
  CHECK(!qa_try(render_invalid, &invalid, &caught));
  CHECK(strcmp(caught.name, "RangeError") == 0 && strcmp(caught.message, "Invalid turntable dimensions") == 0);
  invalid.width = 0; invalid.height = 4;
  CHECK(!qa_try(render_invalid, &invalid, &caught));
  for (size_t i = 0; i < 3; i++) free(textures[i].pixels);
}

/* ---- content rows ---- */
typedef struct content_row {
  const char *id; qa_game_family family; const char *model, *skin; double top, bottom, yaw;
  size_t vertices, indices; uint32_t vertex_hash, index_hash; qa_vec3 center;
  double radius, half_height, floor_z, initial_yaw; const char *textures; size_t visible; uint32_t image;
} content_row;
static const content_row rows[] = {
  {"q1-classic-id1", QA_GAME_FAMILY_Q1, "player", "", 4.0, 12.0, 0.0, 1224, 1224, 0x8eef5c9aU, 0xb8ead310U, {0.23720359802246094, -1.5741605758666992, 1.262969970703125}, 24.481442153795204, 25.919055938720703, -24.656085968017578, -0.55, "296x194:gt0:9fe3e6d8", 13516, 0x4d88dcabU},
  {"q1-classic-id1", QA_GAME_FAMILY_Q1, "player", "", 0.0, 0.0, 0.0, 1224, 1224, 0x8eef5c9aU, 0xb8ead310U, {0.23720359802246094, -1.5741605758666992, 1.262969970703125}, 24.481442153795204, 25.919055938720703, -24.656085968017578, -0.55, "296x194:gt0:853d479c", 13516, 0x6e929b12U},
  {"q2-classic-baseq2", QA_GAME_FAMILY_Q2, "male", "grunt", 0.0, 0.0, 0.0, 1770, 1770, 0x34829e7eU, 0x8466d3acU, {0.8856410980224609, -3.506232261657715, 0.7968940734863281}, 14.247806681439565, 24.904600143432617, -24.10770606994629, 0.85, "284x195:gt0:c1b53027", 17324, 0x1289613fU},
  {"q2-classic-baseq2", QA_GAME_FAMILY_Q2, "male", "cipher", 0.0, 0.0, 0.0, 1770, 1770, 0x34829e7eU, 0x8466d3acU, {0.8856410980224609, -3.506232261657715, 0.7968940734863281}, 14.247806681439565, 24.904600143432617, -24.10770606994629, 0.85, "284x195:gt0:8caeb572", 17324, 0x60aa3312U},
  {"q3-baseq3", QA_GAME_FAMILY_Q3, "sarge", "blue", 0.0, 0.0, 0.0, 799, 2385, 0x40aac2b4U, 0xfcdbd74aU, {1.1562871932983398, -3.775515556335449, 4.2974090576171875}, 17.7589290555231, 28.922409057617188, -24.625, 0.85, "256x256:none:2a5f2b54,16x8:none:e5e0f705", 14877, 0x5cf32aaaU},
  {"q3-baseq3", QA_GAME_FAMILY_Q3, "sarge", "red", 0.0, 0.0, 0.0, 799, 2385, 0x40aac2b4U, 0xfcdbd74aU, {1.1562871932983398, -3.775515556335449, 4.2974090576171875}, 17.7589290555231, 28.922409057617188, -24.625, 0.85, "256x256:none:652517c6,16x8:none:e5e0f705", 14877, 0xf0ce18a8U},
  {"q3-baseq3", QA_GAME_FAMILY_Q3, "sarge", "blue", 0.0, 0.0, 2.1, 799, 2385, 0x40aac2b4U, 0xfcdbd74aU, {1.1562871932983398, -3.775515556335449, 4.2974090576171875}, 17.7589290555231, 28.922409057617188, -24.625, 0.85, "256x256:none:2a5f2b54,16x8:none:e5e0f705", 13852, 0xd1220879U},
  {"q3-baseq3", QA_GAME_FAMILY_Q3, "ranger", "", 0.0, 0.0, 0.0, 808, 2559, 0x9d906601U, 0xa136f137U, {1.0452446937561035, -1.3368558883666992, 5.807258605957031}, 17.52436395232092, 30.01038360595703, -24.203125, 0.85, "256x256:none:b0ac520,64x64:none:cba209f7", 16801, 0x384c86cU},
  {"q3-baseq3", QA_GAME_FAMILY_Q3, "ranger", "wrack", 0.0, 0.0, 0.0, 808, 2559, 0x9d906601U, 0xa136f137U, {1.0452446937561035, -1.3368558883666992, 5.807258605957031}, 17.52436395232092, 30.01038360595703, -24.203125, 0.85, "256x256:none:553bf4d6,64x64:none:1fbe7aa4", 16801, 0xa48009dbU},
  {"q3-baseq3", QA_GAME_FAMILY_Q3, "tankjr", "", 0.0, 0.0, 0.0, 1036, 2340, 0x252bff44U, 0x59a79caeU, {-3.7578125, 0.3984375, 8.375}, 26.55708814354583, 32.890625, -24.515625, 0.85, "256x256:none:1dec346b", 20770, 0x4fab0589U},
  {"q3-baseq3", QA_GAME_FAMILY_Q3, "xaero", "", 0.0, 0.0, 0.0, 865, 2700, 0xde00c045U, 0xb828e0aU, {-0.453125, 3.484375, 5.381574630737305}, 22.140542298716397, 29.819074630737305, -24.4375, 0.85, "256x256:none:b3754f5f,128x128:none:35949490,128x128:none:d8e02e7b", 16659, 0x3e704009U},
  {"q3-baseq3", QA_GAME_FAMILY_Q3, "crash", "", 0.0, 0.0, 0.0, 769, 2568, 0xe234bcfcU, 0x28a1b4a0U, {0.26657962799072266, -1.9609375, 4.928775787353516}, 14.774054393848624, 29.069400787353516, -24.140625, 0.85, "256x256:none:581a6c10,128x128:none:c61ab6,64x64:none:e1d337f2", 11358, 0xffbdccfdU},
  {"q3-baseq3", QA_GAME_FAMILY_Q3, "hunter", "harpy", 0.0, 0.0, 0.0, 743, 2550, 0xa2f6cde3U, 0xad26fcc9U, {1.522101879119873, -1.5798020362854004, 9.264772415161133}, 16.960534623767156, 33.34289741516113, -24.078125, 0.85, "256x256:none:8ec1cfc7,128x128:none:4f22ea9e", 10454, 0x6f496bb8U},
  {"q3-baseq3", QA_GAME_FAMILY_Q3, "razor", "patriot", 0.0, 0.0, 0.0, 718, 2565, 0x63376bdU, 0xe2826e44U, {1.9140625, -1.8080966472625732, 4.964815139770508}, 15.534555032057698, 29.027315139770508, -24.0625, 0.85, "256x256:none:f9981502,128x128:none:cd7e388b,128x128:none:d5f2b0e", 12925, 0x6706b750U},
  {"q3-baseq3", QA_GAME_FAMILY_Q3, "tim", "red", 0.0, 0.0, 0.0, 406, 1890, 0xe42d46d8U, 0x74ba0117U, {-0.09789419174194336, -2.4663310050964355, 1.6067790985107422}, 15.89578526057333, 25.731779098510742, -24.125, 0.85, "64x127:none:949fbad2,64x128:none:53c4c50b,128x64:none:b81ed4f8,256x128:none:7178172b", 15800, 0x964fc6e8U},
};

typedef struct corpus {
  qa_content_catalog catalog;
  bool ready;
} corpus;
static corpus shared;
static bool open_corpus(void) {
  if (shared.ready) return true;
  const char *root = getenv("QTS_TEST_CORPUS");
  if (root == NULL || root[0] == '\0') return false;
  qa_discover_content_options discover;
  memset(&discover, 0, sizeof discover);
  discover.corpus_root = root; discover.discover_mods = false;
  discover.inspector = qa_app_default_archive_inspector();
  if (!qa_discover_installed_content(&discover, &shared.catalog)) return false;
  shared.ready = true;
  return true;
}
typedef struct opened_plan {
  qa_catalog_mount_list list; qa_mount_id *order; char *id; qa_content_mounted *mounted;
} opened_plan;
static void open_plan(const char *product, const char *name, opened_plan *out) {
  memset(out, 0, sizeof *out);
  CHECK(qa_content_catalog_mounts_for(&shared.catalog, product, &out->list));
  out->order = calloc(out->list.count + 1, sizeof *out->order);
  for (size_t i = 0; i < out->list.count; i++)
    out->order[i] = out->list.mounts[i].kind == QA_CONTENT_MOUNT_ARCHIVE ? out->list.mounts[i].as.archive.identity.id
        : out->list.mounts[i].as.loose.identity.id;
  out->id = qa_create_mount_plan_id("test", name);
  qa_resolved_mount_plan plan = {out->id, out->list.mounts, out->list.count, out->order, out->list.count, NULL, 0};
  qa_open_mount_options options;
  memset(&options, 0, sizeof options);
  out->mounted = qa_open_mount_plan(&plan, &options);
}
static void close_plan(opened_plan *plan) {
  qa_app_turntable_forget_mounts(plan->mounted);
  qa_content_mounted_destroy(plan->mounted);
  qa_content_catalog_mount_list_free(&plan->list);
  free(plan->order); free(plan->id);
}
static bool missing_checker(const qa_app_turntable_texture *texture) {
  const uint8_t *p = texture->pixels;
  return texture->width == 16 && texture->height == 16 && p[0] == 72 && p[1] == 72 && p[2] == 78
      && p[4] == 38 && p[5] == 38 && p[6] == 44;
}

static void test_content(void) {
  if (!open_corpus()) { printf("skipped content rows: QTS_TEST_CORPUS unset\n"); return; }
  qa_image_level images[sizeof rows / sizeof rows[0]];
  for (size_t r = 0; r < sizeof rows / sizeof rows[0]; r++) {
    const content_row *row = &rows[r];
    const qa_catalog_discovered_product *product = qa_content_catalog_require(&shared.catalog, row->id);
    opened_plan plan;
    open_plan(product->id, row->id, &plan);
    qa_app_turntable_request request = {product->id, row->family, row->model, row->skin, row->top, row->bottom};
    qa_app_character_turntable_scene *scene = qa_app_load_character_turntable_scene(plan.mounted, &request);
    CHECK(scene->vertex_count == row->vertices);
    CHECK(scene->index_count == row->indices);
    double *flat = malloc(scene->vertex_count * 9 * sizeof *flat);
    for (size_t i = 0; i < scene->vertex_count; i++) {
      const qa_app_turntable_vertex *v = &scene->vertices[i];
      double values[9] = {v->position.x, v->position.y, v->position.z, v->normal.x, v->normal.y, v->normal.z, v->tex_coord.x, v->tex_coord.y, v->texture};
      memcpy(flat + i * 9, values, sizeof values);
    }
    CHECK(fnv((const uint8_t *)flat, scene->vertex_count * 9 * sizeof *flat) == row->vertex_hash);
    free(flat);
    CHECK(fnv((const uint8_t *)scene->indices, scene->index_count * sizeof *scene->indices) == row->index_hash);
    CHECK(scene->center.x == row->center.x && scene->center.y == row->center.y && scene->center.z == row->center.z);
    CHECK(scene->radius == row->radius && scene->half_height == row->half_height && scene->floor_z == row->floor_z);
    CHECK(scene->initial_yaw == row->initial_yaw);
    char textures[1024] = "";
    for (size_t t = 0; t < scene->texture_count; t++) {
      char part[128];
      snprintf(part, sizeof part, "%s%gx%g:%s:%x", t == 0 ? "" : ",", scene->textures[t].width, scene->textures[t].height,
          scene->textures[t].alpha_test, fnv(scene->textures[t].pixels, scene->textures[t].pixel_length));
      strncat(textures, part, sizeof textures - strlen(textures) - 1);
    }
    if (strcmp(textures, row->textures) != 0) fprintf(stderr, "  %s/%s textures %s != %s\n", row->model, row->skin, textures, row->textures);
    CHECK(strcmp(textures, row->textures) == 0);
    qa_app_render_character_turntable(scene, scene->initial_yaw + row->yaw, 192, 256, &images[r]);
    CHECK(visible_pixels(&images[r]) == row->visible);
    CHECK(visible_pixels(&images[r]) > 2500);
    CHECK(fnv(images[r].pixels.bytes, images[r].pixels.length) == row->image);
    /* TS: Q3 diffuse alpha is ancillary; ranger/wrack, tankjr, xaero textures stay opaque. */
    if (row->family == QA_GAME_FAMILY_Q3 && (strcmp(row->model, "tankjr") == 0 || strcmp(row->model, "xaero") == 0
        || (strcmp(row->model, "ranger") == 0 && strcmp(row->skin, "wrack") == 0))) {
      size_t non_opaque = 0;
      for (size_t t = 0; t < scene->texture_count; t++)
        for (size_t offset = 3; offset < scene->textures[t].pixel_length; offset += 4) if (scene->textures[t].pixels[offset] != 255) non_opaque++;
      CHECK(non_opaque == 0);
    }
    /* TS: script shaders resolve instead of falling back to the checker. */
    if (strcmp(row->model, "crash") == 0 || strcmp(row->model, "hunter") == 0 || strcmp(row->model, "razor") == 0
        || strcmp(row->model, "tim") == 0) {
      bool any = false;
      for (size_t t = 0; t < scene->texture_count; t++) if (missing_checker(&scene->textures[t])) any = true;
      CHECK(!any);
    }
    qa_app_character_turntable_scene_release(scene);
    close_plan(&plan);
  }
  /* The TS cross-row expectations. */
  CHECK(differing_bytes(&images[0], &images[1]) > 1000);
  CHECK(differing_bytes(&images[2], &images[3]) > 500);
  CHECK(differing_bytes(&images[4], &images[5]) > 500);
  CHECK(differing_bytes(&images[4], &images[6]) > 1000);
  CHECK(visible_pixels(&images[8]) == visible_pixels(&images[7]));
  CHECK(differing_bytes(&images[7], &images[8]) > 500);
  for (size_t r = 0; r < sizeof rows / sizeof rows[0]; r++) free((void *)images[r].pixels.bytes);
}

/* ---- CharacterTurntable over a registry, sentinel contexts ---- */
static int catalog_sentinel, print_sentinel, clock_sentinel;
static bool catalog_fails;
static int catalog_calls, bad_ctx;
static char printed[8][256]; static int print_count;
static double clock_now;
static qa_content_catalog *catalog_callback(void *ctx) {
  if (ctx != &catalog_sentinel) bad_ctx++;
  catalog_calls++;
  if (catalog_fails) qa_raise("Error", 0, 0, "catalog unavailable");
  return &shared.catalog;
}
static void print_callback(void *ctx, const char *text) {
  if (ctx != &print_sentinel) bad_ctx++;
  if (print_count < 8) snprintf(printed[print_count], sizeof printed[0], "%s", text);
  print_count++;
}
static double clock_sample(void *ctx) {
  if (ctx != &clock_sentinel) bad_ctx++;
  return clock_now;
}
static size_t update_count(qa_scene_image_registry *images) {
  size_t count = 0, updates = 0;
  qa_image_resource_operation *operations = qa_scene_image_registry_drain_operations(images, &count);
  for (size_t i = 0; i < count; i++) if (operations[i].kind == QA_IMAGE_OP_UPDATE) updates++;
  for (size_t i = 0; i < count; i++) if (operations[i].kind == QA_IMAGE_OP_UPDATE) free((void *)operations[i].update_level);
  free(operations);
  return updates;
}

static void test_turntable_object(void) {
  static int owner_identity;
  qa_renderer_resource_owner owner;
  memset(&owner, 0, sizeof owner);
  owner.identity = &owner_identity;
  qa_media_clock clock = {&clock_sentinel, clock_sample};
  qa_scene_image_registry images;
  qa_scene_image_registry_init(&images, owner, &clock);
  qa_app_character_turntable_options options = {&catalog_sentinel, catalog_callback, &images, &print_sentinel, print_callback};
  qa_app_character_turntable *turntable = qa_app_character_turntable_create(options);
  const qa_image_picture *picture = qa_app_character_turntable_picture(turntable, qa_app_character_turntable_resource);
  CHECK(picture != NULL && strcmp(picture->name, "character-turntable") == 0);
  CHECK(qa_app_character_turntable_picture(turntable, "resource:other") == NULL);
  CHECK(picture->image.width == QA_APP_TURNTABLE_WIDTH && picture->image.height == QA_APP_TURNTABLE_HEIGHT);
  /* A throwing catalog leaves the request at the viewer resource. */
  catalog_fails = true;
  qa_app_turntable_request blank = {"q1-classic-id1", QA_GAME_FAMILY_Q1, " ", "", 0, 0};
  CHECK(strcmp(qa_app_character_turntable_request(turntable, &blank, 0), qa_app_character_turntable_resource) == 0);
  CHECK(catalog_calls == 1 && print_count == 0);
  CHECK(update_count(&images) == 0);
  if (open_corpus()) {
    catalog_fails = false;
    /* A failed preview prints once per key. */
    CHECK(strcmp(qa_app_character_turntable_request(turntable, &blank, 0), qa_app_character_turntable_resource) == 0);
    CHECK(print_count == 1 && strcmp(printed[0], "Character preview unavailable: Character preview has no model\n") == 0);
    qa_app_turntable_request missing = {shared.catalog.products[0].id, QA_GAME_FAMILY_Q2, "nosuch", "", 0, 0};
    for (size_t i = 0; i < shared.catalog.product_count; i++)
      if (strcmp(shared.catalog.products[i].id, "q2-classic-baseq2") == 0) missing.product = shared.catalog.products[i].id;
    qa_app_character_turntable_request(turntable, &missing, 10);
    CHECK(print_count == 2 && strcmp(printed[1], "Character preview unavailable: Character preview resource missing: players/nosuch/tris.md2\n") == 0);
    qa_app_character_turntable_request(turntable, &missing, 20);
    CHECK(print_count == 2);
    qa_app_character_turntable_request(turntable, &blank, 30);
    qa_app_character_turntable_request(turntable, &missing, 40);
    CHECK(print_count == 2); /* cached null scenes, failures remembered */
    qa_app_turntable_request q1 = {"q1-classic-id1", QA_GAME_FAMILY_Q1, "player", "", 4, 12};
    for (size_t i = 0; i < shared.catalog.product_count; i++)
      if (strcmp(shared.catalog.products[i].id, "q1-classic-id1") == 0) q1.product = shared.catalog.products[i].id;
    qa_app_character_turntable_request(turntable, &q1, 1000);
    clock_now = 1000;
    CHECK(update_count(&images) == 1); /* first tick renders */
    clock_now = 1020;
    CHECK(update_count(&images) == 0); /* within the 40 ms frame */
    clock_now = 1040;
    CHECK(update_count(&images) == 1); /* the 40 ms frame boundary */
    qa_app_character_turntable_request(turntable, &q1, 1250);
    clock_now = 1290;
    CHECK(update_count(&images) == 1);
    clock_now = 1700;
    CHECK(update_count(&images) == 0); /* not requested for over 300 ms */
  }
  CHECK(bad_ctx == 0);
  qa_app_character_turntable_close(turntable);
  qa_app_character_turntable_close(turntable);
  CHECK(!qa_scene_image_registry_is_resident(&images, picture->image));
  qa_app_turntable_request after = {"x", QA_GAME_FAMILY_Q1, "player", "", 0, 0};
  int calls = catalog_calls;
  CHECK(strcmp(qa_app_character_turntable_request(turntable, &after, 5000), qa_app_character_turntable_resource) == 0);
  CHECK(catalog_calls == calls);
  qa_app_character_turntable_destroy(turntable);
  size_t count = 0;
  free(qa_scene_image_registry_drain_operations(&images, &count));
  qa_scene_image_registry_destroy(&images);
}

int main(void) {
  test_rasterizer();
  test_content();
  test_turntable_object();
  if (shared.ready) qa_content_catalog_destroy(&shared.catalog);
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
