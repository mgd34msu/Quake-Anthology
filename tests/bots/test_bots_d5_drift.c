/*
 * C checks for the TS drift packet D5 (src/bots since 53ad56d), ported from the TS tests
 * added with it:
 *   tests/bots/behavior/asset-files.test.ts      (navigation graphs unread; version 1 digest)
 *   tests/bots/navigation/aas-client-trace.test.ts (AAS_TraceClientBBox on retail q3dm1)
 *   tests/bots/behavior/rerelease/replan.test.ts  (a goal with no route is not replanned every frame;
 *                                                  bun counted 3 plans in 100 frames)
 * plus the MinHeap order of src/bots/navigation/heap.ts and BotAssetFiles.add with legacy
 * bytes. Every expected value below is what bun printed for the same calls on 2026-11-03
 * (TS 84aeef8d): the asset digests, the heap pop order, and for q3dm1 the counts
 * (200 checked, 23 stopped, 0 start-solid, 1933 areas) and an FNV-1a hash over each
 * trace's startSolid/area/lastArea/plane and float32 fraction/end bits (1172410523).
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "qa/bots/behavior/assets.h"
#include "qa/bots/navigation/aas.h"
#include "qa/bots/navigation/heap.h"
#include "qa/bots/behavior/rerelease/data/knowledge_q2.h"
#include "qa/bots/behavior/rerelease/nav.h"
#include "qa/bots/behavior/rerelease/profile.h"
#include "qa/content/archive.h"
#include "qa/foundation/support.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                   \
  } while (0)

/* ---- asset-files.test.ts ---- */
typedef struct asset_row { const char *path, *text; } asset_row;
static const asset_row contents[] = {
    {"bots/weapons.txt", "weapons"}, {"bots/navigation/q2dm1.nav", "graph"}, {"bots/items.txt", "items"}};
typedef struct opener { const char *opened[8]; size_t count; } opener;
static bool open_resource(void *ctx, const char *path, const uint8_t **out_bytes, size_t *out_len) {
  opener *o = ctx;
  if (o->count < 8) o->opened[o->count++] = path;
  for (size_t i = 0; i < 3; i++)
    if (strcmp(contents[i].path, path) == 0) {
      *out_bytes = (const uint8_t *)contents[i].text;
      *out_len = strlen(contents[i].text);
      return true;
    }
  return false;
}
static void legacy_unprepared(void *ctx) { (void)qa_bot_asset_files_legacy_provenance(ctx); }
static void test_asset_files(void) {
  opener o = {{0}, 0};
  const char *paths[] = {contents[0].path, contents[1].path, contents[2].path};
  qa_bot_asset_files files;
  CHECK(qa_load_bot_asset_files(&files, open_resource, &o, paths, 3, NULL, 0));
  CHECK(o.count == 2 && strcmp(o.opened[0], "bots/weapons.txt") == 0 && strcmp(o.opened[1], "bots/items.txt") == 0);
  CHECK(qa_bot_asset_files_read(&files, "bots/navigation/q2dm1.nav", NULL, 0) == -1);
  const char *names[8];
  size_t listed = qa_bot_asset_files_list(&files, "bots", ".txt", names, 8);
  CHECK(listed == 2 && strcmp(names[0], "weapons.txt") == 0 && strcmp(names[1], "items.txt") == 0);
  char digest[65];
  qa_bot_asset_files_provenance(&files, digest);
  CHECK(strcmp(digest, "decfe91964a8aea5226b172c2db9974564fd8733ae6a16ff0063bb5e58372f34") == 0);
  qa_caught_error caught;
  CHECK(!qa_try(legacy_unprepared, &files, &caught));
  CHECK(strstr(caught.message, "prepared") != NULL);
  CHECK(qa_bot_asset_files_prepare_legacy_provenance(&files));
  CHECK(strcmp(qa_bot_asset_files_legacy_provenance(&files),
               "586df8f5a55a9053692f054e92966ada8efd9e1c96836c41b2a611294e58b420") == 0);
  /* add(path, bytes, legacyBytes): the legacy bytes stand in only for the version 1 digest. */
  CHECK(qa_bot_asset_files_add_with_legacy(&files, "arsenal/provenance", (const uint8_t *)"new", 3,
                                           (const uint8_t *)"old", 3));
  qa_bot_asset_files_provenance(&files, digest);
  CHECK(strcmp(digest, "94f4e414ab2078eff76d8567bd549d8252b35b73aecf630b44c314906bec43fc") == 0);
  CHECK(qa_bot_asset_files_prepare_legacy_provenance(&files));
  CHECK(strcmp(qa_bot_asset_files_legacy_provenance(&files),
               "0215ecea55fbcc34305209b897cba6faecd490f4b527285dafd72f7802e52cd9") == 0);
  qa_bot_asset_files_destroy(&files);
}

/* ---- heap.ts ---- */
typedef struct heap_item { int id; double cost; } heap_item;
static double heap_cost(const void *value) { return ((const heap_item *)value)->cost; }
static void test_min_heap_order(void) {
  static const double costs[] = {5, 3, 3, 8, 1, 3, 5, 0, 3, 2, 2, 7};
  static const int expected[] = {7, 4, 9, 10, 101, 1, 100, 8, 2, 5, 0, 6, 11, 3};
  qa_min_heap heap;
  qa_min_heap_init(&heap, sizeof(heap_item), heap_cost);
  for (int i = 0; i < 12; i++) CHECK(qa_min_heap_push(&heap, &(heap_item){i, costs[i]}));
  int out[16], n = 0;
  heap_item item;
  for (int i = 0; i < 4; i++) { CHECK(qa_min_heap_pop(&heap, &item)); out[n++] = item.id; }
  CHECK(qa_min_heap_push(&heap, &(heap_item){100, 3}));
  CHECK(qa_min_heap_push(&heap, &(heap_item){101, 0}));
  while (qa_min_heap_pop(&heap, &item)) out[n++] = item.id;
  CHECK(n == 14);
  for (int i = 0; i < 14 && i < n; i++) CHECK(out[i] == expected[i]);
  CHECK(qa_min_heap_size(&heap) == 0);
  qa_min_heap_destroy(&heap);
}

/* ---- aas-client-trace.test.ts ---- */
static uint32_t fnv;
static void mix(int32_t value) { fnv = (fnv ^ (uint32_t)value) * 0x01000193u; }
static int32_t f32_bits(double value) {
  float f = (float)value;
  int32_t bits;
  memcpy(&bits, &f, sizeof bits);
  return bits;
}
static void nonfinite_trace(void *ctx) {
  qa_aas_trace_client_bbox(ctx, (qa_vec3){NAN, 0, 0}, (qa_vec3){0, 0, 0}, 4, NULL, NULL);
}
static void test_aas_client_trace(void) {
  const char *home = getenv("HOME");
  char path[1024];
  snprintf(path, sizeof path, "%s/qfiles/Quake 3 Arena/baseq3/pak0.pk3", home ? home : "");
  struct stat info;
  if (stat(path, &info) != 0) {
    printf("skipped: q3dm1.aas (no %s)\n", path);
    return;
  }
  qa_archive_handle *archive = qa_open_archive(path, false, 0);
  CHECK(archive != NULL);
  if (archive == NULL) return;
  const qa_archive_entry *entry = NULL;
  CHECK(qa_archive_find_entries(archive, "maps/q3dm1.aas", QA_ARCHIVE_PATH_EXACT, &entry, 1) >= 1);
  size_t length = 0;
  uint8_t *bytes = qa_archive_read_entry_entry(archive, entry, &length);
  qa_archive_destroy(archive);
  qa_aas_asset asset;
  CHECK(qa_parse_aas(&asset, bytes, length, "maps/q3dm1.aas", false, 0));
  free(bytes);
  int checked = 0, stopped = 0, solid = 0;
  fnv = 0x811c9dc5u;
  for (size_t number = 0; number < asset.area_count; number++) {
    if (number == 0 || (asset.settings[number].presence & 4) == 0 || checked >= 200) continue;
    checked++;
    qa_vec3 start = asset.areas[number].center;
    start.z += 0.25;
    qa_vec3 end = start;
    end.z -= 50;
    qa_aas_trace trace = qa_aas_trace_client_bbox(&asset, start, end, 4, NULL, NULL);
    int64_t start_area = qa_aas_point_area(&asset, start);
    CHECK(trace.start_solid == (start_area == 0 || (asset.settings[start_area].presence & 4) == 0));
    CHECK(trace.fraction >= 0 && trace.fraction <= 1);
    if (trace.start_solid) solid++;
    else if (trace.fraction == 1) CHECK(trace.end.x == end.x && trace.end.y == end.y && trace.end.z == end.z);
    else {
      stopped++;
      qa_vec3 below = trace.end;
      below.z -= 0.5;
      int64_t hit = qa_aas_point_area(&asset, below);
      CHECK(hit == 0 || (asset.settings[hit].presence & 4) == 0);
    }
    mix(trace.start_solid ? 1 : 0); mix((int32_t)trace.area); mix((int32_t)trace.last_area); mix((int32_t)trace.plane);
    mix(f32_bits(trace.fraction)); mix(f32_bits(trace.end.x)); mix(f32_bits(trace.end.y)); mix(f32_bits(trace.end.z));
  }
  CHECK(asset.area_count == 1933);
  CHECK(checked == 200 && stopped == 23 && solid == 0);
  CHECK(fnv == 1172410523u);
  qa_caught_error caught;
  CHECK(!qa_try(nonfinite_trace, &asset, &caught));
  CHECK(strstr(caught.message, "Non-finite") != NULL);
  qa_aas_asset_destroy(&asset);
}


/* ---- rerelease/replan.test.ts ---- */
static double replan_now;
static int replan_plans;
static qa_bot_entity replan_enemy;
static double rp_time(void *ctx) { (void)ctx; return replan_now; }
static double rp_frame_time(void *ctx) { (void)ctx; return 0.05; }
static qa_bot_self rp_self(void *ctx) {
  (void)ctx;
  qa_bot_self self;
  memset(&self, 0, sizeof self);
  self.id = 1;
  self.eye = (qa_bot_vec3){0, 0, 22};
  self.health = 100;
  self.on_ground = true;
  self.team = 1;
  return self;
}
static qa_bot_trace rp_line(void *ctx, qa_bot_vec3 start, qa_bot_vec3 end) {
  (void)ctx; (void)start;
  return (qa_bot_trace){1, end, false, -1};
}
static qa_bot_trace rp_box(void *ctx, qa_bot_vec3 start, qa_bot_vec3 mins, qa_bot_vec3 maxs, qa_bot_vec3 end) {
  (void)ctx; (void)start; (void)mins; (void)maxs;
  return (qa_bot_trace){1, end, false, -1};
}
static int64_t rp_contents(void *ctx, qa_bot_vec3 p) { (void)ctx; (void)p; return 0; }
static const qa_bot_entity *rp_entities(void *ctx, size_t *count) { (void)ctx; *count = 1; return &replan_enemy; }
static const qa_bot_sound *rp_hearing(void *ctx, size_t *count) { (void)ctx; *count = 0; return NULL; }
static size_t rp_node_count(void *ctx) { (void)ctx; return 4; }
static size_t rp_nodes(void *ctx, qa_nav_graph_node *out, size_t cap) { (void)ctx; (void)out; (void)cap; return 0; }
static bool rp_plan(void *ctx, qa_bot_vec3 start, qa_bot_vec3 goal, const qa_nav_plan_options *options, qa_nav_path *out) {
  (void)ctx; (void)start; (void)goal; (void)options; (void)out;
  replan_plans++;
  return false;
}
static bool rp_valid(void *ctx, const qa_nav_path *path) { (void)ctx; (void)path; return false; }
static qa_bot_navigation_iface replan_nav;
static qa_bot_navigation_iface *rp_nav(void *ctx) { (void)ctx; return &replan_nav; }
static void rp_pre(void *ctx, const char *name, qa_rerelease_orders orders) { (void)ctx; (void)name; (void)orders; }
static void rp_post(void *ctx, const char *name, qa_bot_usercmd cmd) { (void)ctx; (void)name; (void)cmd; }
static void rp_chat(void *ctx, qa_bot_chat_event event) { (void)ctx; (void)event; }
static void rp_select(void *ctx, double weapon) { (void)ctx; (void)weapon; }
static double rp_impulse(void *ctx, double weapon) { (void)ctx; (void)weapon; return 0; }
static bool rp_near(void *ctx) { (void)ctx; return false; }
static void test_replan_pacing(void) {
  const char *home = getenv("HOME");
  char path[1024];
  snprintf(path, sizeof path, "%s/qfiles/Quake 2/rerelease/baseq2/pak0.pak", home ? home : "");
  struct stat info;
  if (stat(path, &info) != 0) {
    printf("skipped: replan pacing (no %s)\n", path);
    return;
  }
  qa_archive_handle *archive = qa_open_archive(path, false, 0);
  CHECK(archive != NULL);
  if (archive == NULL) return;
  qa_bot_asset_files files;
  qa_bot_asset_files_init(&files);
  for (size_t i = 0; i < archive->entry_count; i++) {
    const char *name = archive->entries[i].path;
    size_t n = strlen(name);
    if (strncmp(name, "bots/", 5) != 0 || n < 4 || strcmp(name + n - 4, ".txt") != 0) continue;
    size_t length = 0;
    uint8_t *bytes = qa_archive_read_entry_entry(archive, &archive->entries[i], &length);
    CHECK(qa_bot_asset_files_add(&files, name, bytes, length));
    free(bytes);
  }
  qa_archive_destroy(archive);
  qa_bot_knowledge knowledge;
  CHECK(qa_bot_load_knowledge(&knowledge, &files, "PC"));
  memset(&replan_enemy, 0, sizeof replan_enemy);
  replan_enemy.id = 2;
  replan_enemy.kind = QA_BOT_ENTITY_PLAYER;
  replan_enemy.classname = "player";
  replan_enemy.origin = (qa_bot_vec3){600, 0, 0};
  replan_enemy.center = replan_enemy.head = (qa_bot_vec3){600, 0, 22};
  replan_enemy.feet = (qa_bot_vec3){600, 0, -24};
  replan_enemy.health = 100;
  replan_enemy.team = 2;
  replan_enemy.has_health = true;
  memset(&replan_nav, 0, sizeof replan_nav);
  replan_nav.node_count = rp_node_count;
  replan_nav.nodes = rp_nodes;
  replan_nav.plan_path = rp_plan;
  replan_nav.path_valid = rp_valid;
  qa_rerelease_profile_options options;
  memset(&options, 0, sizeof options);
  options.definition = "retail-q2-pak0";
  options.source = QA_RERELEASE_Q2;
  options.knowledge = &knowledge;
  options.skill = "nightmare";
  options.seed = 123;
  options.game_mode.game_type = "dm";
  options.game_mode.weapon_stay = false;
  options.max_health = 100;
  options.run_speed = 300;
  options.walk_speed = 150;
  options.movement = (qa_bot_movement_projection){800, 270, 0.675, 40, 56, {-16, -16, -6}, {16, 16, 32}};
  options.callbacks = (qa_rerelease_callbacks){NULL, rp_time, rp_pre, rp_post, rp_chat, rp_select, rp_impulse, rp_near};
  qa_rerelease_bot_behavior behavior;
  CHECK(qa_rerelease_bot_behavior_init(&behavior, options));
  qa_bot_world world = {NULL, rp_time, rp_frame_time, rp_self, rp_line, rp_box, rp_contents, rp_entities, rp_hearing, rp_nav};
  replan_plans = 0;
  for (int frame = 0; frame < 100; frame++) {
    replan_now = frame * 0.05;
    (void)qa_rerelease_bot_behavior_think(&behavior, &world);
  }
  /* 5 simulated seconds at a two second replan interval; before the pacing every frame planned. */
  CHECK(replan_plans > 0 && replan_plans <= 6);
  CHECK(replan_plans == 3);
  qa_rerelease_bot_behavior_destroy(&behavior);
  qa_bot_knowledge_destroy(&knowledge);
  qa_bot_asset_files_destroy(&files);
}

int main(void) {
  test_asset_files();
  test_min_heap_order();
  test_aas_client_trace();
  test_replan_pacing();
  printf("test_bots_d5_drift: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
