/* Concrete SharedSimulation assembly (TS loadSimulation over the
 * landed sibling modules): build over a decoded Q1 world, admit a
 * player at info_player_start, step frames through the real
 * scheduler/physics/movement pipeline, checkpoint a save image, and
 * close cleanly. Self-contained: the Q1 world is an in-memory
 * fixture in the style of test_sim_c's physics world. Q2/Q3 worlds
 * are synthesized minimal IBSP38/IBSP46 files decoded from loose
 * mounts by the concrete builder itself.
 * GPL-2.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/stat.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/simulation/quakec_source.h"
#include "qa/app/bootstrap/simulation/runtime.h"
#include "qa/compat/qc/program.h"
#include "qa/content/mounts.h"
#include "qa/core/numeric.h"
#include "qa/formats/bsp_kind.h"
#include "qa/formats/q1_map.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/body.h"

static int checks, failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)
#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0);                                  \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

/* Empty Q1 world: solid leaf 0, empty leaf 1, one model. */
static qa_q1_leaf leaves[2];
static qa_q1_world_model models[1];
static double headnodes[4] = {-2, -1, -1, -1};
static qa_q1_entity entities[2];
static qa_q1_entity_property world_props[2], start_props[3];
static qa_q1_map q1_map;
static void build_world(void) {
  qa_bounds b = {{-512, -512, -512}, {512, 512, 512}};
  leaves[0] = (qa_q1_leaf){-2, b, {0, 0}, false, 0, {0, 0, 0, 0}};
  leaves[1] = (qa_q1_leaf){-1, b, {0, 0}, false, 0, {0, 0, 0, 0}};
  memset(models, 0, sizeof models);
  models[0].bounds = b;
  models[0].headnodes = (qa_double_view){headnodes, 4};
  models[0].visible_leaves = 1;
  world_props[0] = (qa_q1_entity_property){"classname", "worldspawn"};
  world_props[1] = (qa_q1_entity_property){"message", "Concrete Test"};
  start_props[0] = (qa_q1_entity_property){"classname", "info_player_start"};
  start_props[1] = (qa_q1_entity_property){"origin", "32 64 128"};
  start_props[2] = (qa_q1_entity_property){"angle", "90"};
  entities[0] = (qa_q1_entity){world_props, 2};
  entities[1] = (qa_q1_entity){start_props, 3};
  memset(&q1_map, 0, sizeof q1_map);
  q1_map.world.format = "bsp29";
  q1_map.world.entities = "";
  q1_map.world.leaves = leaves; q1_map.world.leaf_count = 2;
  q1_map.world.models = models; q1_map.world.model_count = 1;
  q1_map.entity_list = entities; q1_map.entity_count = 2;
}

/* --- Synthetic progs.dat for the QuakeC source-game scenario.
 * Format follows the loader-verified builder in test_compat_qc.c.
 * Functions: worldspawn/info_player_start (count spawns),
 * test_spinner (counts, precaches the map model, OP_STATE
 * schedules spinner_think), spinner_think (counts + reschedules),
 * StartFrame (counts frames). --- */
typedef struct { uint8_t *data; size_t length, capacity; } qc_buf;
static void qc_put(qc_buf *b, const void *p, size_t n) {
  if (b->length + n > b->capacity) { b->capacity = (b->length + n) * 2 + 64; b->data = realloc(b->data, b->capacity); }
  if (n > 0) memcpy(b->data + b->length, p, n);
  b->length += n;
}
static void qc_i32(qc_buf *b, int32_t v) { qc_put(b, &v, 4); }
static void qc_u16(qc_buf *b, uint16_t v) { qc_put(b, &v, 2); }
static void qc_patch(qc_buf *b, size_t offset, int32_t v) { memcpy(b->data + offset, &v, 4); }
static int32_t qc_str(qc_buf *strings, const char *s) {
  for (size_t i = 0; i < strings->length; i++)
    if (strcmp((const char *)strings->data + i, s) == 0 && (i == 0 || strings->data[i - 1] == 0)) return (int32_t)i;
  int32_t offset = (int32_t)strings->length;
  qc_put(strings, s, strlen(s) + 1);
  return offset;
}
typedef struct { const char *name; uint16_t type; uint16_t offset; } qc_def;
static const qc_def qc_globals[] = {
  {"self", 4, 28}, {"other", 4, 29}, {"time", 2, 30}, {"frametime", 2, 31},
  {"mapname", 1, 32}, {"msg_entity", 4, 33}, {"force_retouch", 2, 34},
  {"skill", 2, 35}, {"deathmatch", 2, 36}, {"coop", 2, 37}, {"teamplay", 2, 38},
  {"v_forward", 3, 39}, {"v_right", 3, 42}, {"v_up", 3, 45},
  {"trace_allsolid", 2, 48}, {"trace_startsolid", 2, 49}, {"trace_fraction", 2, 50},
  {"trace_inwater", 2, 51}, {"trace_inopen", 2, 52}, {"trace_plane_dist", 2, 53},
  {"trace_endpos", 3, 54}, {"trace_plane_normal", 3, 57}, {"trace_ent", 4, 60},
  {"one", 2, 61}, {"frame0", 2, 62}, {"spinner_think_ptr", 6, 63},
  {"counter_spawns", 2, 64}, {"counter_frames", 2, 65}, {"counter_thinks", 2, 66},
  {"map_path", 1, 67}, {"precache_model_ptr", 6, 68},
};
static const qc_def qc_fields[] = {
  {"classname", 1, 0}, {"origin", 3, 1}, {"angles", 3, 4}, {"velocity", 3, 7},
  {"avelocity", 3, 10}, {"mins", 3, 13}, {"maxs", 3, 16}, {"size", 3, 19},
  {"absmin", 3, 22}, {"absmax", 3, 25}, {"oldorigin", 3, 28}, {"solid", 2, 31},
  {"movetype", 2, 32}, {"model", 1, 33}, {"modelindex", 2, 34}, {"frame", 2, 35},
  {"skin", 2, 36}, {"colormap", 2, 37}, {"effects", 2, 38}, {"health", 2, 39},
  {"max_health", 2, 40}, {"takedamage", 2, 41}, {"flags", 2, 42}, {"owner", 4, 43},
  {"enemy", 4, 44}, {"groundentity", 4, 45}, {"chain", 4, 46}, {"think", 6, 47},
  {"nextthink", 2, 48}, {"touch", 6, 49}, {"use", 6, 50}, {"blocked", 6, 51},
  {"ltime", 2, 52}, {"watertype", 2, 53}, {"waterlevel", 2, 54}, {"view_ofs", 3, 55},
  {"v_angle", 3, 58}, {"netname", 1, 61}, {"team", 2, 62}, {"armorvalue", 2, 63},
  {"armortype", 2, 64}, {"invincible_finished", 2, 65}, {"spawnflags", 2, 66},
  {"target", 1, 67}, {"targetname", 1, 68}, {"message", 1, 69},
};
enum { QC_FIELD_WORDS = 72, QC_GLOBAL_WORDS = 80 };
static uint8_t *build_synthetic_progs(size_t *out_length) {
  qc_buf strings = {0}; qc_put(&strings, "", 1);
  int32_t map_path_offset = qc_str(&strings, "maps/e1m1.bsp");
  qc_buf out = {0};
  qc_i32(&out, 6); qc_i32(&out, 5927);
  size_t slots[6];
  for (int i = 0; i < 6; i++) { slots[i] = out.length; qc_i32(&out, 0); qc_i32(&out, 0); }
  qc_i32(&out, QC_FIELD_WORDS);
  /* 0 DONE; 1-2 worldspawn; 3 info_player_start; 4-8 test_spinner;
   * 9-11 spinner_think; 12-13 StartFrame. */
  static const uint16_t stmts[14][4] = {
    {0, 0, 0, 0},
    {6, 64, 61, 64}, {43, 62, 0, 0},
    {43, 62, 0, 0},
    {6, 64, 61, 64}, {33, 67, 4, 0}, {52, 68, 0, 0}, {60, 62, 63, 0}, {43, 62, 0, 0},
    {6, 66, 61, 66}, {60, 62, 63, 0}, {43, 62, 0, 0},
    {6, 65, 61, 65}, {43, 62, 0, 0},
  };
  qc_patch(&out, slots[0], (int32_t)out.length); qc_patch(&out, slots[0] + 4, 14);
  for (int i = 0; i < 14; i++) for (int j = 0; j < 4; j++) qc_u16(&out, stmts[i][j]);
  qc_patch(&out, slots[1], (int32_t)out.length);
  qc_patch(&out, slots[1] + 4, (int32_t)(sizeof qc_globals / sizeof *qc_globals));
  for (size_t i = 0; i < sizeof qc_globals / sizeof *qc_globals; i++) {
    qc_u16(&out, qc_globals[i].type); qc_u16(&out, qc_globals[i].offset);
    qc_i32(&out, qc_str(&strings, qc_globals[i].name));
  }
  qc_patch(&out, slots[2], (int32_t)out.length);
  qc_patch(&out, slots[2] + 4, (int32_t)(sizeof qc_fields / sizeof *qc_fields));
  for (size_t i = 0; i < sizeof qc_fields / sizeof *qc_fields; i++) {
    qc_u16(&out, qc_fields[i].type); qc_u16(&out, qc_fields[i].offset);
    qc_i32(&out, qc_str(&strings, qc_fields[i].name));
  }
  typedef struct { int32_t first; const char *name; uint8_t parms[8]; int32_t nparms; } qc_fn;
  static const qc_fn fns[] = {
    {0, "", {0}, 0}, {1, "worldspawn", {0}, 0}, {3, "info_player_start", {0}, 0},
    {4, "test_spinner", {0}, 0}, {9, "spinner_think", {0}, 0}, {12, "StartFrame", {0}, 0},
    {-20, "precache_model", {1}, 1},
  };
  qc_patch(&out, slots[3], (int32_t)out.length);
  qc_patch(&out, slots[3] + 4, (int32_t)(sizeof fns / sizeof *fns));
  for (size_t i = 0; i < sizeof fns / sizeof *fns; i++) {
    qc_i32(&out, fns[i].first); qc_i32(&out, 28); qc_i32(&out, 0); qc_i32(&out, 0);
    qc_i32(&out, qc_str(&strings, fns[i].name)); qc_i32(&out, qc_str(&strings, "test.qc"));
    qc_i32(&out, fns[i].nparms); qc_put(&out, fns[i].parms, 8);
  }
  qc_patch(&out, slots[4], (int32_t)out.length); qc_patch(&out, slots[4] + 4, (int32_t)strings.length);
  qc_put(&out, strings.data, strings.length);
  qc_patch(&out, slots[5], (int32_t)out.length); qc_patch(&out, slots[5] + 4, QC_GLOBAL_WORDS);
  for (int i = 0; i < QC_GLOBAL_WORDS; i++) {
    if (i == 61) { float one = 1.0f; int32_t bits; memcpy(&bits, &one, 4); qc_i32(&out, bits); }
    else if (i == 63) qc_i32(&out, 4);            /* spinner_think fn index */
    else if (i == 67) qc_i32(&out, map_path_offset);
    else if (i == 68) qc_i32(&out, 6);            /* precache_model fn index */
    else qc_i32(&out, 0);
  }
  free(strings.data);
  *out_length = out.length;
  return out.data;
}
static bool read_synthetic_artifact(void *ctx, const qa_resolved_resource_reference *artifact,
                                    uint8_t **out_bytes, size_t *out_length) {
  (void)artifact;
  size_t length = 0;
  uint8_t *bytes = build_synthetic_progs(&length);
  *(size_t *)ctx = length;
  *out_bytes = bytes; *out_length = length;
  return bytes != NULL;
}

static qa_provider_reference ref(const char *provider) {
  qa_provider_reference out = {provider, "q1:classic:id1:installed"};
  return out;
}
static qa_provider_reference weapons_storage[1];
static qa_provider_timing timing_storage[1];
static qa_execution_module execution_storage[1];
static qa_app_application_world app_world;
static qa_app_simulation_options concrete_options(qa_identity_owner *identity) {
  qa_app_simulation_options options; memset(&options, 0, sizeof options);
  qa_executable_recipe *recipe = &options.recipe;
  recipe->schema_version = 3;
  recipe->map.geometry.requested_path = "maps/e1m1.bsp";
  recipe->map.geometry_content = "q1:classic:id1:installed";
  recipe->map.entities = ref("q1:official");
  recipe->movement = ref("q1:official");
  recipe->character.definition = ref("q1:official");
  recipe->character.appearance = ref("q1:official");
  weapons_storage[0] = ref("q1:official");
  recipe->weapons.values = weapons_storage; recipe->weapons.count = 1;
  recipe->engine_behavior = ref("q1:official");
  recipe->combat = ref("q1:official");
  recipe->inventory = ref("q1:official");
  recipe->match = ref("q1:official");
  recipe->transition = ref("q1:official");
  qa_clock_profile clock; memset(&clock, 0, sizeof clock);
  clock.kind = QA_CLOCK_Q1_NETQUAKE;
  clock.minimum_frame_seconds = 0.001; clock.maximum_frame_seconds = 0.1;
  timing_storage[0] = (qa_provider_timing){"q1:official", clock, qa_q1_donor_profile};
  recipe->timing = timing_storage; recipe->timing_count = 1;
  recipe->ordering.kind = QA_FRAME_ORDERING_NATIVE;
  recipe->ordering.clock = clock;
  memset(execution_storage, 0, sizeof execution_storage);
  execution_storage[0].kind = QA_EXECUTION_MODULE_TYPESCRIPT;
  execution_storage[0].owner = ref("q1:official");
  execution_storage[0].role = "server-game";
  execution_storage[0].api.kind = QA_GAME_API_QUAKEC;
  execution_storage[0].api.as.quakec.kind = QA_QUAKEC_API_Q1_NETQUAKE;
  recipe->execution = execution_storage; recipe->execution_count = 1;
  options.identity = *identity;
  app_world = (qa_app_application_world){"q1-bsp", &q1_map};
  options.world = &app_world;
  options.skill = 1; options.mode = "singleplayer";
  options.seed = 7; options.max_clients = 1;
  return options;
}

/* --- Q2/Q3 synthesized BSP fixtures (mounts decode dispatch) --- */
typedef struct { uint8_t *data; size_t length, cap; } byte_buf;
static void bb_put(byte_buf *b, const void *p, size_t n) {
  if (b->length + n > b->cap) {
    b->cap = (b->length + n) * 2 + 64;
    b->data = realloc(b->data, b->cap);
    if (!b->data) { fprintf(stderr, "oom\n"); exit(2); }
  }
  if (n > 0) memcpy(b->data + b->length, p, n);
  b->length += n;
}
static void bb_i32(byte_buf *b, int32_t v) { bb_put(b, &v, 4); }
static void bb_f32(byte_buf *b, float v) { bb_put(b, &v, 4); }
static void bb_i16(byte_buf *b, int16_t v) { bb_put(b, &v, 2); }
static const char *FIXTURE_ENTITIES =
    "{\n\"classname\" \"worldspawn\"\n\"message\" \"fixture\"\n}\n"
    "{\n\"classname\" \"info_player_start\"\n\"origin\" \"16 32 48\"\n"
    "\"angle\" \"270\"\n}\n";
/* Minimal IBSP46: entities, 1 shader/plane/node/leaf/model. */
static uint8_t *build_q3_bsp(size_t *out_len) {
  byte_buf l[17]; memset(l, 0, sizeof l);
  bb_put(&l[0], FIXTURE_ENTITIES, strlen(FIXTURE_ENTITIES) + 1);
  char name[64] = "textures/test/wall";
  bb_put(&l[1], name, 64); bb_i32(&l[1], 0); bb_i32(&l[1], 1);
  bb_f32(&l[2], 0); bb_f32(&l[2], 0); bb_f32(&l[2], 1); bb_f32(&l[2], 0);
  bb_i32(&l[3], 0); bb_i32(&l[3], -1); bb_i32(&l[3], -1);
  for (int i = 0; i < 3; i++) bb_i32(&l[3], -512);
  for (int i = 0; i < 3; i++) bb_i32(&l[3], 512);
  bb_i32(&l[4], -1); bb_i32(&l[4], 0);
  for (int i = 0; i < 3; i++) bb_i32(&l[4], -512);
  for (int i = 0; i < 3; i++) bb_i32(&l[4], 512);
  bb_i32(&l[4], 0); bb_i32(&l[4], 0); bb_i32(&l[4], 0); bb_i32(&l[4], 0);
  for (int i = 0; i < 3; i++) bb_f32(&l[7], -512);
  for (int i = 0; i < 3; i++) bb_f32(&l[7], 512);
  bb_i32(&l[7], 0); bb_i32(&l[7], 0); bb_i32(&l[7], 0); bb_i32(&l[7], 0);
  byte_buf out; memset(&out, 0, sizeof out);
  bb_put(&out, "IBSP", 4); bb_i32(&out, 46);
  size_t off = 8 + 17 * 8;
  for (int i = 0; i < 17; i++) { bb_i32(&out, (int32_t)off); bb_i32(&out, (int32_t)l[i].length); off += l[i].length; }
  for (int i = 0; i < 17; i++) { bb_put(&out, l[i].data, l[i].length); free(l[i].data); }
  *out_len = out.length;
  return out.data;
}
/* Minimal IBSP38: entities, 1 plane/node/model, solid+empty leaves. */
static uint8_t *build_q2_bsp(size_t *out_len) {
  byte_buf l[19]; memset(l, 0, sizeof l);
  bb_put(&l[0], FIXTURE_ENTITIES, strlen(FIXTURE_ENTITIES) + 1);
  bb_f32(&l[1], 0); bb_f32(&l[1], 0); bb_f32(&l[1], 1); bb_f32(&l[1], 0); bb_i32(&l[1], 2);
  bb_i32(&l[4], 0); bb_i32(&l[4], -1); bb_i32(&l[4], -2);
  for (int i = 0; i < 3; i++) bb_i16(&l[4], -512);
  for (int i = 0; i < 3; i++) bb_i16(&l[4], 512);
  bb_i16(&l[4], 0); bb_i16(&l[4], 0);
  for (int leaf = 0; leaf < 2; leaf++) {
    bb_i32(&l[8], leaf == 0 ? 1 : 0); bb_i16(&l[8], -1); bb_i16(&l[8], 0);
    for (int i = 0; i < 3; i++) bb_i16(&l[8], -512);
    for (int i = 0; i < 3; i++) bb_i16(&l[8], 512);
    bb_i16(&l[8], 0); bb_i16(&l[8], 0); bb_i16(&l[8], 0); bb_i16(&l[8], 0);
  }
  for (int i = 0; i < 3; i++) bb_f32(&l[13], -512);
  for (int i = 0; i < 3; i++) bb_f32(&l[13], 512);
  for (int i = 0; i < 3; i++) bb_f32(&l[13], 0);
  bb_i32(&l[13], 0); bb_i32(&l[13], 0); bb_i32(&l[13], 0);
  bb_i32(&l[17], 0); bb_i32(&l[17], 0); bb_i32(&l[17], 0); bb_i32(&l[17], 0);
  byte_buf out; memset(&out, 0, sizeof out);
  bb_put(&out, "IBSP", 4); bb_i32(&out, 38);
  size_t off = 8 + 19 * 8;
  for (int i = 0; i < 19; i++) { bb_i32(&out, (int32_t)off); bb_i32(&out, (int32_t)l[i].length); off += l[i].length; }
  for (int i = 0; i < 19; i++) { bb_put(&out, l[i].data, l[i].length); free(l[i].data); }
  *out_len = out.length;
  return out.data;
}
static bool write_bytes(const char *path, const uint8_t *bytes, size_t length) {
  FILE *file = fopen(path, "wb");
  if (!file) return false;
  bool ok = fwrite(bytes, 1, length, file) == length;
  fclose(file);
  return ok;
}
static qa_provider_reference fam_refs[1];
static qa_provider_timing fam_timing[1];
static qa_execution_module fam_execution[1];
static qa_provider_reference fam_ref(const char *provider, const char *content) {
  qa_provider_reference out = {provider, content};
  return out;
}
static qa_app_simulation_options family_options(qa_identity_owner *identity, int family,
                                                qa_content_mounted *mounts,
                                                const qa_app_application_world *world) {
  const char *provider = family == 2 ? "q2:official" : "q3:official";
  const char *content = family == 2 ? "q2:classic:baseq2:installed"
                                    : "q3:classic:baseq3:installed";
  qa_app_simulation_options options; memset(&options, 0, sizeof options);
  qa_executable_recipe *recipe = &options.recipe;
  recipe->schema_version = 3;
  recipe->map.geometry.requested_path = family == 2 ? "maps/probe2.bsp" : "maps/probe3.bsp";
  recipe->map.geometry_content = content;
  recipe->map.entities = fam_ref(provider, content);
  recipe->movement = fam_ref(provider, content);
  recipe->character.definition = fam_ref(provider, content);
  recipe->character.appearance = fam_ref(provider, content);
  fam_refs[0] = fam_ref(provider, content);
  recipe->weapons.values = fam_refs; recipe->weapons.count = 1;
  recipe->engine_behavior = fam_ref(provider, content);
  recipe->combat = fam_ref(provider, content);
  recipe->inventory = fam_ref(provider, content);
  recipe->match = fam_ref(provider, content);
  recipe->transition = fam_ref(provider, content);
  qa_clock_profile clock; memset(&clock, 0, sizeof clock);
  if (family == 2) { clock.kind = QA_CLOCK_Q2_CLASSIC; clock.frame_milliseconds = 100; }
  else { clock.kind = QA_CLOCK_Q3; clock.server_frame_milliseconds = 50;
         clock.maximum_command_milliseconds = 200; }
  fam_timing[0] = (qa_provider_timing){provider, clock,
      family == 2 ? qa_q2_donor_profile : qa_q3_binary32_profile};
  recipe->timing = fam_timing; recipe->timing_count = 1;
  recipe->ordering.kind = QA_FRAME_ORDERING_NATIVE;
  recipe->ordering.clock = clock;
  memset(fam_execution, 0, sizeof fam_execution);
  fam_execution[0].kind = QA_EXECUTION_MODULE_TYPESCRIPT;
  fam_execution[0].owner = fam_ref(provider, content);
  fam_execution[0].role = "server-game";
  if (family == 2) {
    fam_execution[0].api.kind = QA_GAME_API_Q2_GAME;
    fam_execution[0].api.as.q2_game.kind = QA_Q2_GAME_API_CLASSIC;
  } else fam_execution[0].api.kind = QA_GAME_API_Q3;
  recipe->execution = fam_execution; recipe->execution_count = 1;
  options.identity = *identity;
  options.world = world;
  options.mounts = mounts;
  options.skill = 1; options.mode = "singleplayer";
  options.seed = 11; options.max_clients = 1;
  return options;
}
/* Build/admit/step/close one family over the loose mounts, with no
 * decoded world handed in: the concrete builder must classify and
 * decode the mounted BSP itself. */
static void test_family_world(int family, qa_content_mounted *mounts,
                              const qa_app_application_world *world) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, family == 2 ? "concrete-q2" : "concrete-q3"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identity, 4096);
  CHECK(actors != NULL);
  qa_app_simulation_options options = family_options(&identity, family, mounts, world);
  qa_app_sim_shared_simulation *sim = qa_app_sim_load_simulation_concrete(
      options, actors, qa_session_actor_registry_session(actors), NULL, NULL);
  CHECK(sim != NULL);
  qa_app_sim_concrete_simulation *concrete = qa_app_sim_shared_simulation_concrete(sim);
  CHECK(concrete != NULL);
  CHECK(qa_app_sim_concrete_simulation_scene(concrete) != NULL);
  qa_actor_id world_actor; memset(&world_actor, 0, sizeof world_actor);
  CHECK(qa_app_sim_concrete_simulation_world_actor(concrete, &world_actor));
  qa_owned_actor at_world; memset(&at_world, 0, sizeof at_world);
  CHECK(qa_session_actor_registry_at_source(actors,
      family == 2 ? "q2:official" : "q3:official", family == 2 ? 0 : 1022, &at_world));
  CHECK(qa_same_actor(at_world.id, world_actor));
  qa_client_id client = qa_identity_owner_client(&identity, 0, 1);
  qa_app_sim_player_admission admission =
      qa_app_sim_concrete_simulation_admit_player(concrete, client);
  qa_shared_body_table *bodies =
      qa_app_sim_shared_physics_bodies(qa_app_sim_concrete_simulation_physics(concrete));
  qa_body_state_result spawned = qa_shared_body_table_read(bodies, admission.actor);
  CHECK(spawned.has_value);
  CHECK(spawned.value.origin.x == 16 && spawned.value.origin.y == 32
        && spawned.value.origin.z == 48);
  CHECK(spawned.value.angles.y == 270);
  qa_input_batch input; memset(&input, 0, sizeof input);
  input.elapsed_milliseconds = family == 2 ? 100 : 50;
  qa_simulation_output output;
  memset(&output, 0, sizeof output);
  for (int i = 0; i < 5; i++) output = qa_app_sim_shared_simulation_step(sim, input);
  CHECK(output.snapshot.frame.frame >= 5);
  CHECK(output.snapshot.frame.time.value > 0);
  CHECK(output.snapshot.actor_count >= 2);
  CHECK(output.snapshot.scene.world != NULL);
  CHECK(output.snapshot.scene.world->geometry.kind ==
        (family == 2 ? QA_DECODED_WORLD_Q2 : QA_DECODED_WORLD_Q3));
  qa_app_sim_shared_simulation_close(sim);
  qa_app_sim_shared_simulation_destroy(sim);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identity);
}
static void test_q2_q3_worlds(void) {
  size_t q2_length = 0, q3_length = 0;
  uint8_t *q2_bytes = build_q2_bsp(&q2_length);
  uint8_t *q3_bytes = build_q3_bsp(&q3_length);
  const char *family2 = qa_classify_bsp(q2_bytes, q2_length, "maps/probe2.bsp");
  const char *family3 = qa_classify_bsp(q3_bytes, q3_length, "maps/probe3.bsp");
  CHECK(family2 != NULL && strcmp(family2, "q2") == 0);
  CHECK(family3 != NULL && strcmp(family3, "q3") == 0);
  char root[] = "/tmp/qa-concrete-XXXXXX";
  CHECK(mkdtemp(root) != NULL);
  char maps_dir[256], q2_path[256], q3_path[256];
  (void)snprintf(maps_dir, sizeof maps_dir, "%s/maps", root);
  (void)snprintf(q2_path, sizeof q2_path, "%s/maps/probe2.bsp", root);
  (void)snprintf(q3_path, sizeof q3_path, "%s/maps/probe3.bsp", root);
  CHECK(mkdir(maps_dir, 0700) == 0);
  CHECK(write_bytes(q2_path, q2_bytes, q2_length));
  CHECK(write_bytes(q3_path, q3_bytes, q3_length));
  free(q2_bytes); free(q3_bytes);
  qa_content_mount mount; memset(&mount, 0, sizeof mount);
  mount.kind = QA_CONTENT_MOUNT_LOOSE;
  mount.as.loose.identity = qa_create_mount_identity("mount:test:concrete",
      "q2:classic:baseq2:installed", 0);
  mount.as.loose.root_path = root;
  qa_mount_id order[1] = {"mount:test:concrete"};
  qa_resolved_mount_plan plan; memset(&plan, 0, sizeof plan);
  plan.id = "mount-plan:test:concrete";
  plan.mounts = &mount; plan.mount_count = 1;
  plan.default_order = order; plan.default_order_count = 1;
  qa_content_mounted *mounts = qa_open_mount_plan(&plan, NULL);
  CHECK(mounts != NULL);
  /* Q2 through the {NULL, NULL} world view application.c passes for
   * host-loaded content; Q3 with no world view at all. */
  qa_app_application_world empty_world = {NULL, NULL};
  if (mounts) {
    test_family_world(2, mounts, &empty_world);
    test_family_world(3, mounts, NULL);
    qa_content_mounted_destroy(mounts);
  }
  (void)remove(q2_path); (void)remove(q3_path);
  (void)rmdir(maps_dir); (void)rmdir(root);
}

static void count_progress(void *ctx, qa_simulation_progress frame) {
  (*(int *)ctx)++;
  CHECK(frame.output != NULL && frame.pending_milliseconds == 0);
}

int main(void) {
  build_world();
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "concrete-test"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identity, 1024);
  CHECK(actors != NULL);
  qa_session_id session = qa_session_actor_registry_session(actors);
  qa_app_simulation_options options = concrete_options(&identity);

  /* Build: world actor at source slot 0, zeroed source frame. */
  qa_app_sim_shared_simulation *sim =
      qa_app_sim_load_simulation_concrete(options, actors, session, NULL, NULL);
  CHECK(sim != NULL);
  qa_app_sim_concrete_simulation *concrete = qa_app_sim_shared_simulation_concrete(sim);
  CHECK(concrete != NULL);
  CHECK(qa_app_sim_concrete_simulation_physics(concrete) != NULL);
  CHECK(qa_app_sim_concrete_simulation_scene(concrete) != NULL);
  qa_actor_id world_actor; memset(&world_actor, 0, sizeof world_actor);
  CHECK(qa_app_sim_concrete_simulation_world_actor(concrete, &world_actor));
  qa_owned_actor at_world; memset(&at_world, 0, sizeof at_world);
  CHECK(qa_session_actor_registry_at_source(actors, "q1:official", 0, &at_world));
  CHECK(qa_same_actor(at_world.id, world_actor));
  CHECK(qa_app_sim_concrete_simulation_frame(concrete).time.value == 0);

  /* Admit: spawns at the fixture info_player_start. */
  qa_client_id client = qa_identity_owner_client(&identity, 0, 1);
  qa_app_sim_player_admission admission =
      qa_app_sim_concrete_simulation_admit_player(concrete, client);
  CHECK(!qa_same_actor(admission.actor, world_actor));
  CHECK(admission.view_height == 22);
  qa_shared_body_table *bodies =
      qa_app_sim_shared_physics_bodies(qa_app_sim_concrete_simulation_physics(concrete));
  qa_body_state_result spawned = qa_shared_body_table_read(bodies, admission.actor);
  CHECK(spawned.has_value);
  CHECK(spawned.value.origin.x == 32 && spawned.value.origin.y == 64
        && spawned.value.origin.z == 128);
  CHECK(spawned.value.angles.y == 90);
  CHECK_ABORTS(qa_app_sim_concrete_simulation_admit_player(concrete, client));

  /* Step N frames with no commands: the free Q1 clock advances by
   * the clamped elapsed (50ms) once per step. */
  qa_input_batch input; memset(&input, 0, sizeof input);
  input.elapsed_milliseconds = 50;
  qa_simulation_output output = qa_app_sim_shared_simulation_step(sim, input);
  for (int i = 1; i < 10; i++) output = qa_app_sim_shared_simulation_step(sim, input);
  CHECK(fabs(output.snapshot.frame.time.value - 0.5) < 1e-9);
  CHECK(output.snapshot.frame.frame == 10);
  CHECK(output.snapshot.actor_count >= 2);
  CHECK(output.snapshot.body_count >= 2);
  CHECK(output.snapshot.configuration_count == 1);
  CHECK(output.snapshot.inventory_count == 1);
  CHECK(output.snapshot.scene.world != NULL);
  CHECK(output.snapshot.scene.world->geometry.kind == QA_DECODED_WORLD_Q1);

  /* A NetQuake command through the movement player moves the body
   * (gravity + forward wish velocity in the empty leaf). */
  qa_body_state_result before = qa_shared_body_table_read(bodies, admission.actor);
  qa_actor_command command; memset(&command, 0, sizeof command);
  command.actor = admission.actor;
  command.source.kind = QA_COMMAND_SOURCE_LOCAL_SEAT;
  command.source.client = client;
  command.sequence = 1;
  command.command.kind = QA_USER_COMMAND_Q1;
  command.command.as.q1.view_angles = (qa_vec3){0, 90, 0};
  command.command.as.q1.forward_move = 400;
  input.commands = &command; input.command_count = 1;
  for (int i = 0; i < 4; i++) {
    command.sequence = 1 + i;
    output = qa_app_sim_shared_simulation_step(sim, input);
  }
  qa_body_state_result after = qa_shared_body_table_read(bodies, admission.actor);
  CHECK(after.has_value);
  CHECK(after.value.origin.x != before.value.origin.x
        || after.value.origin.y != before.value.origin.y
        || after.value.origin.z != before.value.origin.z
        || after.value.velocity.x != 0 || after.value.velocity.y != 0
        || after.value.velocity.z != 0);

  /* stepAsync falls through to step() and reports progress once. */
  input.command_count = 0; input.commands = NULL;
  qa_simulation as_simulation = qa_app_sim_shared_simulation_as_simulation(sim);
  int progress_calls = 0;
  output = as_simulation.step_async(as_simulation.ctx, input, count_progress, &progress_calls);
  CHECK(progress_calls == 1);
  CHECK(output.snapshot.frame.frame == 15);

  /* Commands for an unadmitted actor abort (TS step guard). */
  qa_actor_command bad = command;
  bad.actor.slot += 500;
  qa_input_batch bad_input = input; bad_input.commands = &bad; bad_input.command_count = 1;
  CHECK_ABORTS((void)qa_app_sim_shared_simulation_step(sim, bad_input));
  /* A second admission for the same client aborts in-process too. */
  input.command_count = 0; input.commands = NULL;

  /* Checkpoint: a validated save image over the concrete owners. */
  qa_save_image image = qa_app_sim_shared_simulation_checkpoint(sim);
  CHECK(image.schema_version == 3);
  CHECK(image.clock_count == 1 && image.random_count == 1);
  CHECK(image.clocks[0].time.value == image.frame.time.value);
  CHECK(image.actor_count >= 2);
  CHECK(image.body_count >= 2);
  CHECK(image.combat_count == 1);
  CHECK(image.configuration_count == 1);
  CHECK(image.provider_count == 2);
  CHECK(strcmp(image.providers[0].schema, "world:source-slots") == 0);
  CHECK(strcmp(image.providers[1].schema, "world:simulation") == 0);
  /* Full TS validation also requires the q1:foundation provider
   * capture from the Q1 source game, which the concrete builder
   * does not construct yet: validation must refuse this image
   * rather than pass it silently. */
  CHECK_ABORTS(qa_app_sim_validate_simulation_save(image));
  qa_app_sim_saved_settings settings = qa_app_sim_saved_simulation_settings(image);
  CHECK(settings.skill == 1 && strcmp(settings.mode, "singleplayer") == 0);
  CHECK(settings.max_clients == 1 && settings.seed == 7);
  CHECK(settings.client_slot_count == 1 && settings.client_slots[0] == 0);
  CHECK(settings.host_milliseconds == 750);
  qa_save_value_free(settings.value);
  free(settings.client_slots);

  /* Close is clean and idempotent through the shared guards; a
   * stepped-after-close call aborts in a child. */
  qa_app_sim_shared_simulation_close(sim);
  qa_app_sim_shared_simulation_close(sim);
  CHECK_ABORTS((void)qa_app_sim_shared_simulation_step(sim, input));
  qa_app_sim_shared_simulation_destroy(sim);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identity);

  /* QuakeC source game: spawnMap runs the synthetic progs.dat
   * spawn functions (worldspawn precache/spawns), StartFrame runs
   * per map frame, and OP_STATE-scheduled thinks execute. */
  {
    static qa_q1_entity_property spinner_a[2], spinner_b[2], mystery[1];
    spinner_a[0] = (qa_q1_entity_property){"classname", "test_spinner"};
    spinner_a[1] = (qa_q1_entity_property){"origin", "16 16 24"};
    spinner_b[0] = (qa_q1_entity_property){"classname", "test_spinner"};
    spinner_b[1] = (qa_q1_entity_property){"origin", "-16 -16 24"};
    mystery[0] = (qa_q1_entity_property){"classname", "mystery_class"};
    static qa_q1_entity qc_entities[5];
    qc_entities[0] = (qa_q1_entity){world_props, 2};
    qc_entities[1] = (qa_q1_entity){start_props, 3};
    qc_entities[2] = (qa_q1_entity){spinner_a, 2};
    qc_entities[3] = (qa_q1_entity){spinner_b, 2};
    qc_entities[4] = (qa_q1_entity){mystery, 1};
    static qa_q1_map qc_map;
    qc_map = q1_map;
    qc_map.entity_list = qc_entities; qc_map.entity_count = 5;

    qa_identity_owner qc_identity;
    CHECK(qa_identity_owner_create(&qc_identity, "concrete-quakec"));
    qa_session_actor_registry *qc_actors = qa_session_actor_registry_create(&qc_identity, 4096);
    CHECK(qc_actors != NULL);
    qa_app_simulation_options qc_options = concrete_options(&qc_identity);
    static qa_app_application_world qc_world;
    qc_world = (qa_app_application_world){"q1-bsp", &qc_map};
    qc_options.world = &qc_world;
    static qa_resolved_resource_reference qc_artifact;
    memset(&qc_artifact, 0, sizeof qc_artifact);
    qc_artifact.requested_path = "progs.dat";
    static qa_execution_module qc_execution;
    memset(&qc_execution, 0, sizeof qc_execution);
    qc_execution.kind = QA_EXECUTION_MODULE_QUAKEC;
    qc_execution.owner = ref("q1:official");
    qc_execution.role = "server-game";
    qc_execution.artifact = &qc_artifact;
    qc_execution.api.kind = QA_GAME_API_QUAKEC;
    qc_execution.api.as.quakec.kind = QA_QUAKEC_API_Q1_NETQUAKE;
    qc_execution.api.as.quakec.program_version = 6;
    qc_execution.api.as.quakec.system_crc = 5927;
    qc_options.recipe.execution = &qc_execution;
    qc_options.recipe.execution_count = 1;
    /* Prepared through the landed quakec-source preparer. */
    size_t artifact_length = 0;
    qa_app_sim_quakec_prepare_host prepare_host = {0};
    prepare_host.ctx = &artifact_length;
    prepare_host.read_artifact = read_synthetic_artifact;
    qa_app_sim_prepared_quakec_source prepared =
        qa_app_sim_prepare_quakec_source(qc_execution, "", prepare_host);
    CHECK(artifact_length > 0);
    CHECK(prepared.kind != NULL && strcmp(prepared.kind, "netquake") == 0);
    qc_options.prepared_quakec = &prepared;
    /* Without the prepared artifact the same recipe aborts. */
    qa_app_simulation_options unprepared = qc_options;
    unprepared.prepared_quakec = NULL;
    CHECK_ABORTS((void)qa_app_sim_load_simulation_concrete(unprepared, qc_actors,
        qa_session_actor_registry_session(qc_actors), NULL, NULL));

    qa_app_sim_shared_simulation *qc_sim = qa_app_sim_load_simulation_concrete(
        qc_options, qc_actors, qa_session_actor_registry_session(qc_actors), NULL, NULL);
    CHECK(qc_sim != NULL);
    qa_app_sim_concrete_simulation *qc_concrete = qa_app_sim_shared_simulation_concrete(qc_sim);
    CHECK(qc_concrete != NULL);
    qa_app_sim_concrete_quakec_stats stats;
    CHECK(qa_app_sim_concrete_simulation_quakec_stats(qc_concrete, &stats));
    /* worldspawn + info_player_start + 2 spinners ran; mystery_class
     * has no spawn function and was freed. */
    CHECK(stats.spawn_count == 4);
    CHECK(stats.precache_count == 1); /* precache_model("maps/e1m1.bsp") */
    CHECK(stats.entity_count >= 6);
    CHECK(stats.start_frame_count == 0 && stats.think_count == 0);
    qa_actor_id qc_world_actor; memset(&qc_world_actor, 0, sizeof qc_world_actor);
    CHECK(qa_app_sim_concrete_simulation_world_actor(qc_concrete, &qc_world_actor));
    qa_owned_actor qc_slot3; memset(&qc_slot3, 0, sizeof qc_slot3);
    CHECK(qa_session_actor_registry_at_source(qc_actors, "q1:official", 3, &qc_slot3));

    qa_input_batch qc_input; memset(&qc_input, 0, sizeof qc_input);
    qc_input.elapsed_milliseconds = 50;
    qa_simulation_output qc_output;
    memset(&qc_output, 0, sizeof qc_output);
    for (int i = 0; i < 30; i++) qc_output = qa_app_sim_shared_simulation_step(qc_sim, qc_input);
    CHECK(qa_app_sim_concrete_simulation_quakec_stats(qc_concrete, &stats));
    CHECK(stats.start_frame_count == 30);
    /* Thinks only run because the QC spawn functions executed
     * OP_STATE (nextthink = time + 0.1, think = spinner_think) and
     * spinner_think rescheduled itself the same way. */
    CHECK(stats.think_count >= 10);
    /* Game-side actors exist beyond the world + reserved client. */
    CHECK(qc_output.snapshot.actor_count >= 5);
    CHECK(qc_output.snapshot.body_count >= 5);
    printf("quakec source game: spawns=%zu precache=%zu entities=%lld "
           "startframes=%zu thinks=%zu actors=%zu\n",
           stats.spawn_count, stats.precache_count, (long long)stats.entity_count,
           stats.start_frame_count, stats.think_count, qc_output.snapshot.actor_count);
    qa_app_sim_shared_simulation_close(qc_sim);
    qa_app_sim_shared_simulation_destroy(qc_sim);
    qa_compat_qc_program_destroy(&prepared.program);
    qa_session_actor_registry_destroy(qc_actors);
    qa_identity_owner_destroy(&qc_identity);
  }

  /* Constructor guards: no weapon provider, guest execution kinds. */
  qa_identity_owner identity2;
  CHECK(qa_identity_owner_create(&identity2, "concrete-guards"));
  qa_session_actor_registry *actors2 = qa_session_actor_registry_create(&identity2, 64);
  qa_app_simulation_options no_weapons = concrete_options(&identity2);
  no_weapons.recipe.weapons.count = 0;
  CHECK_ABORTS((void)qa_app_sim_load_simulation_concrete(no_weapons, actors2,
      qa_session_actor_registry_session(actors2), NULL, NULL));
  qa_execution_module quakec; memset(&quakec, 0, sizeof quakec);
  quakec.kind = QA_EXECUTION_MODULE_QUAKEC;
  qa_app_simulation_options guest = concrete_options(&identity2);
  guest.recipe.execution = &quakec; guest.recipe.execution_count = 1;
  CHECK_ABORTS((void)qa_app_sim_load_simulation_concrete(guest, actors2,
      qa_session_actor_registry_session(actors2), NULL, NULL));
  /* No world and no mounts cannot construct. */
  qa_app_simulation_options no_world = concrete_options(&identity2);
  no_world.world = NULL;
  CHECK_ABORTS((void)qa_app_sim_load_simulation_concrete(no_world, actors2,
      qa_session_actor_registry_session(actors2), NULL, NULL));
  qa_session_actor_registry_destroy(actors2);
  qa_identity_owner_destroy(&identity2);

  test_q2_q3_worlds();

  printf("test_sim_concrete: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
