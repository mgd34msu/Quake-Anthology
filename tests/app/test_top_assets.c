/* C checks for src/app/bootstrap/demo-local-recording.ts and
 * src/app/bootstrap/assets.ts: tests/app/demo-local-recording.test.ts,
 * NetQuake recording bytes from the TS module under bun, the TS error
 * paths, and ApplicationAssets over retail content when ~/qfiles is
 * installed (tests/first-playable/q1-button-texture, world-text,
 * monster-animation: loadWorld/model/provider). Every callback checks
 * its sentinel ctx. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/demo_local_recording.h"
#include "qa/app/bootstrap/network/q1.h"
#include "qa/app/bootstrap/options.h"
#include "qa/content/catalog/index.h"
#include "qa/foundation/support.h"
#include "qa/network/common/session.h"
#include "qa/network/q3/server_message.h"

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static char *hex(const uint8_t *bytes, size_t length) {
  char *text = malloc(length * 2 + 1);
  for (size_t i = 0; i < length; i++) snprintf(text + i * 2, 3, "%02x", bytes[i]);
  text[length * 2] = '\0';
  return text;
}
static bool hex_is(const uint8_t *bytes, size_t length, const char *expected) {
  char *text = hex(bytes, length);
  bool same = strcmp(text, expected) == 0;
  if (!same) fprintf(stderr, "  got      %s\n  expected %s\n", text, expected);
  free(text);
  return same;
}

/* --- captured raises --- */
typedef struct raise_scope { void (*fn)(void *); void *ctx; } raise_scope;
static bool raises(void (*fn)(void *), void *ctx, const char *message) {
  qa_caught_error caught;
  if (qa_try(fn, ctx, &caught)) { fprintf(stderr, "  expected raise: %s\n", message); return false; }
  if (strcmp(caught.message, message) != 0) { fprintf(stderr, "  raised %s: %s\n", caught.name, caught.message); return false; }
  return true;
}

/* --- recorded sink --- */
typedef struct recorded_packet {
  qa_app_demo_recording_kind kind; uint8_t *bytes; size_t length; double sequence; qa_vec3 view_angles;
} recorded_packet;
typedef struct sink_state { int sentinel; recorded_packet packets[32]; size_t count; } sink_state;
static sink_state sink_sentinel_holder;
static const char *sink_append(void *ctx, qa_app_demo_recording_packet packet) {
  sink_state *sink = ctx;
  CHECK(sink == &sink_sentinel_holder && sink->sentinel == 0x51);
  recorded_packet *row = &sink->packets[sink->count++];
  row->kind = packet.kind;
  const uint8_t *bytes = packet.kind == QA_APP_RECORDING_QW ? packet.record.message : packet.message;
  size_t length = packet.kind == QA_APP_RECORDING_QW ? packet.record.length : packet.message_length;
  row->bytes = malloc(length ? length : 1);
  if (length) memcpy(row->bytes, bytes, length);
  row->length = length;
  row->sequence = packet.sequence;
  row->view_angles = packet.view_angles;
  return NULL;
}
static void sink_clear(sink_state *sink) {
  for (size_t i = 0; i < sink->count; i++) free(sink->packets[i].bytes);
  sink->count = 0;
}

/* --- Q3: tests/app/demo-local-recording.test.ts --- */
typedef struct q3_fixture {
  int sentinel;
  qa_identity_owner identity;
  qa_app_q3_player player;
  qa_q3_product product;
  qa_wire_admission supported;
  double source_time;
  char map_value[64];
  qa_q3_gamestate_entry entry;
  qa_q3_gamestate state;
  qa_q3_player_state_record player_state;
  uint8_t area_mask[32];
  qa_app_q3_server_host host;
  int world_a, world_b;
  const int *world;
  int unused_calls, reads;
} q3_fixture;
static q3_fixture q3;
static const void *q3_support(void *ctx) { CHECK(ctx == &q3); return &q3.supported; }
static double q3_time(void *ctx) { CHECK(ctx == &q3); return q3.source_time; }
static const void *q3_game_state(void *ctx, qa_app_q3_player player, double server_id) {
  CHECK(ctx == &q3);
  CHECK(qa_same_actor(player.actor, q3.player.actor));
  CHECK(server_id == 12);
  q3.entry.value = q3.map_value;
  return &q3.state;
}
static qa_app_q3_snapshot q3_snapshot(void *ctx, qa_app_q3_player player) {
  CHECK(ctx == &q3);
  CHECK(qa_same_actor(player.actor, q3.player.actor));
  qa_app_q3_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.player = &q3.player_state;
  snapshot.area_mask = q3.area_mask;
  snapshot.area_mask_length = 32;
  return snapshot;
}
static qa_app_q3_admission q3_admit(void *ctx, const void *request) {
  (void)request; CHECK(ctx == &q3); q3.unused_calls++;
  qa_app_q3_admission admission; memset(&admission, 0, sizeof admission); return admission;
}
static qa_app_local_recording_source q3_read(void *ctx) {
  CHECK(ctx == &q3);
  q3.reads++;
  qa_app_local_recording_source source;
  memset(&source, 0, sizeof source);
  source.kind = QA_APP_LOCAL_RECORDING_Q3;
  source.world = q3.world;
  source.q3_host = &q3.host;
  source.q3_player = q3.player;
  source.server_id = 12;
  source.snapshot_server_bit = 4;
  return source;
}
static void test_q3_recording(void) {
  memset(&q3, 0, sizeof q3);
  q3.sentinel = 0x33;
  CHECK(qa_identity_owner_create(&q3.identity, "local recording"));
  q3.player.client = qa_identity_owner_client(&q3.identity, 2, 0);
  q3.player.actor = qa_identity_owner_actor(&q3.identity, 2, 0);
  q3.player.source_entity = 2;
  q3.product = QA_Q3_PRODUCT_MISSIONPACK;
  q3.supported.supported = true;
  q3.source_time = 14500;
  snprintf(q3.map_value, sizeof q3.map_value, "\\mapname\\mpteam1");
  q3.entry.kind = QA_Q3_GS_CONFIGSTRING; q3.entry.index = 0;
  q3.state.command_sequence = 0; q3.state.entries = &q3.entry; q3.state.entry_count = 1;
  q3.state.client_number = 2; q3.state.checksum_feed = 42;
  qa_q3_player_state_record_init(&q3.player_state, QA_Q3_PRODUCT_MISSIONPACK, 0, 0, 0);
  q3.player_state.client_num = 2;
  q3.host.ctx = &q3;
  q3.host.product = &q3.product;
  q3.host.max_clients = 6;
  q3.host.supports_source_wire = q3_support;
  q3.host.time = q3_time;
  q3.host.game_state = q3_game_state;
  q3.host.snapshot = q3_snapshot;
  q3.host.admit = q3_admit;
  q3.world = &q3.world_a;
  qa_app_local_demo_recording *recording = qa_app_local_demo_recording_create(q3_read, &q3);
  sink_state *sink = &sink_sentinel_holder;
  memset(sink, 0, sizeof *sink);
  sink->sentinel = 0x51;
  qa_app_demo_recording_seed seed = qa_app_local_demo_recording_seed(recording);
  CHECK(seed.identity.kind == QA_APP_RECORDING_Q3 && seed.identity.protocol == 68);
  CHECK(seed.packet_count == 1);
  for (size_t i = 0; i < seed.packet_count; i++) (void)sink_append(sink, seed.packets[i]);
  qa_app_demo_recording_sink handle = {sink, sink_append};
  qa_app_local_demo_recording_attachment attachment = qa_app_local_demo_recording_attach(recording, handle);
  qa_app_local_demo_recording_publish(recording, NULL, NULL, 0);
  q3.source_time = 15000; q3.world = &q3.world_b;
  snprintf(q3.map_value, sizeof q3.map_value, "\\mapname\\mpteam2");
  qa_app_local_demo_recording_publish(recording, NULL, NULL, 0);
  qa_app_local_demo_recording_detach(&attachment);
  qa_app_local_demo_recording_detach(&attachment);
  int reads = q3.reads;
  qa_app_local_demo_recording_publish(recording, NULL, NULL, 0);
  CHECK(q3.reads == reads);
  CHECK(sink->count == 4);
  CHECK(q3.unused_calls == 0);
  const char *maps[2] = {NULL, NULL};
  size_t map_count = 0;
  double times[2] = {0, 0}, clients[2] = {0, 0}, flags[2] = {0, 0};
  size_t snapshot_count = 0;
  char map_storage[2][64];
  for (size_t i = 0; i < sink->count; i++) {
    CHECK(sink->packets[i].kind == QA_APP_RECORDING_Q3);
    CHECK(sink->packets[i].sequence == (double)i);
    qa_q3_server_message_context context;
    memset(&context, 0, sizeof context);
    context.product = QA_Q3_PRODUCT_MISSIONPACK;
    context.message_number = (int64_t)sink->packets[i].sequence;
    qa_q3_server_message decoded;
    memset(&decoded, 0, sizeof decoded);
    CHECK(qa_q3_decode_server_message(sink->packets[i].bytes, sink->packets[i].length, &context, NULL, &decoded));
    for (size_t o = 0; o < decoded.operation_count; o++) {
      const qa_q3_server_operation *operation = &decoded.operations[o];
      if (operation->kind == QA_Q3_OP_GAMESTATE && operation->gamestate.entry_count > 0
          && operation->gamestate.entries[0].kind == QA_Q3_GS_CONFIGSTRING && map_count < 2) {
        snprintf(map_storage[map_count], 64, "%s", operation->gamestate.entries[0].value);
        maps[map_count] = map_storage[map_count];
        map_count++;
      }
      if (operation->kind == QA_Q3_OP_SNAPSHOT && snapshot_count < 2) {
        times[snapshot_count] = (double)operation->snapshot.server_time;
        clients[snapshot_count] = operation->snapshot.player_state.client_num;
        flags[snapshot_count] = (double)operation->snapshot.flags;
        snapshot_count++;
      }
    }
    qa_q3_server_message_destroy(&decoded);
  }
  CHECK(map_count == 2 && strcmp(maps[0], "\\mapname\\mpteam1") == 0 && strcmp(maps[1], "\\mapname\\mpteam2") == 0);
  CHECK(snapshot_count == 2 && times[0] == 14500 && times[1] == 15000);
  CHECK(clients[0] == 2 && clients[1] == 2);
  CHECK(flags[0] == 4 && flags[1] == 4);
  sink_clear(sink);
  qa_app_local_demo_recording_destroy(recording);
  qa_identity_owner_destroy(&q3.identity);
}

/* --- Q1: bytes from the TS module under bun (scratch dlr.ts) --- */
typedef struct q1_fixture {
  int sentinel;
  qa_wire_admission support;
  char *reasons[2];
  qa_netquake_message info, signon, spawn, reliable, message;
  qa_app_q1_baseline_entry baseline;
  qa_app_q1_baselines baselines;
  qa_q1_extended_entity_state entities[2];
  qa_app_q1_server_host host;
  int world_a, world_b;
  const int *world;
  qa_app_local_recording_kind kind;
} q1_fixture;
static q1_fixture q1;
static qa_q1_extended_entity_state q1_entity(double number, double x) {
  qa_q1_extended_entity_state state;
  memset(&state, 0, sizeof state);
  state.base.number = number;
  state.base.origin = (qa_vec3){x, 2, 3};
  state.base.angles = (qa_vec3){0, 90, 0};
  state.base.model_index = 4; state.base.frame = 1;
  state.alpha = 0; state.scale = 16;
  return state;
}
static const void *q1_support(void *ctx) { CHECK(ctx == &q1); return &q1.support; }
static qa_app_q1_game_state q1_game_state(void *ctx, qa_app_q1_player player) {
  CHECK(ctx == &q1); CHECK(player.source_entity == 1);
  qa_app_q1_game_state state;
  memset(&state, 0, sizeof state);
  state.info = &q1.info;
  state.baselines = &q1.baselines;
  state.signon = (const void *const *)&q1.signon;
  state.signon_count = 1;
  return state;
}
static const void *const *q1_spawn(void *ctx, qa_app_q1_player player, size_t *out_count) {
  CHECK(ctx == &q1); CHECK(player.source_entity == 1);
  *out_count = 1;
  return (const void *const *)&q1.spawn;
}
static qa_app_q1_frame q1_frame(void *ctx, qa_app_q1_player player, const void *output) {
  CHECK(ctx == &q1); CHECK(player.source_entity == 1); CHECK(output == &q1.world_a);
  qa_app_q1_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.seconds = 1.5;
  frame.reliable = (const void *const *)&q1.reliable; frame.reliable_count = 1;
  frame.messages = (const void *const *)&q1.message; frame.message_count = 1;
  frame.entities = (const void *const *)q1.entities; frame.entity_count = 2;
  return frame;
}
static qa_app_local_recording_source q1_read(void *ctx) {
  CHECK(ctx == &q1);
  qa_app_local_recording_source source;
  memset(&source, 0, sizeof source);
  source.kind = q1.kind;
  source.world = q1.world;
  source.q1_host = &q1.host;
  source.q1_player.source_entity = 1;
  source.view_angles = (qa_vec3){1, 2, 3};
  source.q3_host = &q3.host;
  return source;
}
static void seed_body(void *ctx) { (void)qa_app_local_demo_recording_seed(ctx); }
static void attach_body(void *ctx) {
  qa_app_demo_recording_sink handle = {&sink_sentinel_holder, sink_append};
  (void)qa_app_local_demo_recording_attach(ctx, handle);
}
static void publish_body(void *ctx) { qa_app_local_demo_recording_publish(ctx, &q1.world_a, NULL, 0); }
static void test_q1_recording(void) {
  memset(&q1, 0, sizeof q1);
  q1.sentinel = 0x11;
  q1.support.supported = true;
  q1.info.kind = "print"; q1.info.text = "info\n";
  q1.signon.kind = "stufftext"; q1.signon.text = "signon\n";
  q1.spawn.kind = "print"; q1.spawn.text = "spawn\n";
  q1.reliable.kind = "print"; q1.reliable.text = "r\n";
  q1.message.kind = "center-print"; q1.message.text = "m";
  q1.baseline.number = 5; q1.baseline.state = q1_entity(5, 10);
  q1.baselines.entries = &q1.baseline; q1.baselines.count = 1;
  q1.entities[0] = q1_entity(5, 12); q1.entities[1] = q1_entity(6, 7);
  q1.host.ctx = &q1;
  q1.host.protocol.kind = "q1-fitzquake"; q1.host.protocol.version = 666;
  q1.host.supports_source_wire = q1_support;
  q1.host.game_state = q1_game_state;
  q1.host.spawn = q1_spawn;
  q1.host.frame = q1_frame;
  q1.world = &q1.world_a;
  q1.kind = QA_APP_LOCAL_RECORDING_Q1;
  qa_app_local_demo_recording *recording = qa_app_local_demo_recording_create(q1_read, &q1);
  CHECK(raises(attach_body, recording, "Local recording requires an unattached prepared seed"));
  qa_app_demo_recording_seed seed = qa_app_local_demo_recording_seed(recording);
  CHECK(seed.identity.kind == QA_APP_RECORDING_Q1 && seed.identity.protocol == 666 && seed.identity.track == -1);
  static const char *const expected[] = {"08696e666f0a00", "050100", "1901", "097369676e6f6e0a00",
      "16050004010000500000100040180000", "1902", "08737061776e0a00", "1903"};
  CHECK(seed.packet_count == 8);
  for (size_t i = 0; i < seed.packet_count && i < 8; i++) {
    CHECK(seed.packets[i].kind == QA_APP_RECORDING_Q1);
    CHECK(seed.packets[i].view_angles.x == 1 && seed.packets[i].view_angles.z == 3);
    CHECK(hex_is(seed.packets[i].message, seed.packets[i].message_length, expected[i]));
  }
  sink_state *sink = &sink_sentinel_holder;
  memset(sink, 0, sizeof *sink);
  sink->sentinel = 0x51;
  qa_app_demo_recording_sink handle = {sink, sink_append};
  qa_app_local_demo_recording_attachment attachment = qa_app_local_demo_recording_attach(recording, handle);
  CHECK(raises(attach_body, recording, "Local recording requires an unattached prepared seed"));
  qa_app_local_demo_recording_publish(recording, &q1.world_a, NULL, 0);
  CHECK(sink->count == 1);
  if (sink->count == 1) CHECK(hex_is(sink->packets[0].bytes, sink->packets[0].length,
      "070000c03f08720a001a6d0082056000df0406040138001000401800"));
  /* A new world whose protocol differs from the seed identity. */
  q1.world = &q1.world_b;
  q1.host.protocol.kind = "q1-netquake"; q1.host.protocol.version = 15;
  CHECK(raises(publish_body, recording, "Local recording source protocol changed"));
  /* The source kind changes without a world boundary. */
  q1.kind = QA_APP_LOCAL_RECORDING_Q3;
  CHECK(raises(publish_body, recording, "Local recording source changed without a world boundary"));
  qa_app_local_demo_recording_detach(&attachment);
  sink_clear(sink);
  /* Unsupported wire admission joins its reasons. */
  q1.kind = QA_APP_LOCAL_RECORDING_Q1;
  q1.support.supported = false;
  q1.reasons[0] = "first reason"; q1.reasons[1] = "second reason";
  q1.support.reasons = q1.reasons; q1.support.reason_count = 2;
  CHECK(raises(seed_body, recording, "first reason; second reason"));
  qa_app_local_demo_recording_destroy(recording);
}

/* --- ApplicationAssets over installed retail content --- */
typedef struct warn_state { int sentinel; int count; char last[256]; } warn_state;
static warn_state warn_holder;
static void assets_warn(void *ctx, const char *text) {
  warn_state *warn = ctx;
  CHECK(warn == &warn_holder && warn->sentinel == 0x77);
  warn->count++;
  snprintf(warn->last, sizeof warn->last, "%s", text);
}
typedef struct profile_state { int sentinel; int calls; } profile_state;
static profile_state profile_holder;
static qa_finish_shader_profile assets_profile(void *ctx) {
  profile_state *profile = ctx;
  CHECK(profile == &profile_holder && profile->sentinel == 0x99);
  profile->calls++;
  return qa_default_shader_profile;
}
static double clock_ms;
static double assets_clock(void *ctx) { CHECK(ctx == &clock_ms); return clock_ms; }

static qa_app_application_command loaded_command;
static qa_app_loaded_content *load_content(const char *root, const char *game, const char *map) {
  const char *argv[] = {"--game", game, "--map", map, "--dedicated", "--content-root", root};
  qa_content_root_search search = {NULL, NULL};
  loaded_command = qa_app_parse_application_command(argv, 7, search, NULL);
  if (strcmp(loaded_command.kind, "run") != 0) return NULL;
  qa_app_content_host host;
  memset(&host, 0, sizeof host);
  /* The loaded content takes over the options (destroyed with it). */
  qa_app_loaded_content *content = qa_app_load_application_content_native(host, loaded_command.options, NULL, false, NULL, NULL, NULL);
  memset(&loaded_command.options, 0, sizeof loaded_command.options);
  return content;
}
static void world_body(void *ctx) { (void)qa_app_application_assets_world(ctx); }
typedef struct model_call { qa_app_application_assets *assets; const char *content, *path; } model_call;
static void model_body(void *ctx) { model_call *call = ctx; (void)qa_app_application_assets_model(call->assets, call->content, call->path); }
static void closed_provider_body(void *ctx) { model_call *call = ctx; (void)qa_app_application_assets_provider(call->assets, call->content); }

static void test_q1_assets(void) {
  struct stat status;
  const char *home = getenv("HOME");
  char root[512];
  snprintf(root, sizeof root, "%s/qfiles/Quake/Id1/PAK0.PAK", home != NULL ? home : "");
  if (stat(root, &status) != 0) { fprintf(stderr, "skip: no installed Quake content\n"); return; }
  snprintf(root, sizeof root, "%s/qfiles", home != NULL ? home : "");
  qa_app_loaded_content *content = load_content(root, "q1-classic-id1", "start");
  CHECK(content != NULL);
  if (content == NULL) return;
  qa_identity_owner ids;
  CHECK(qa_identity_owner_create(&ids, "assets"));
  int owner_symbol = 0;
  qa_renderer_resource_owner owner = {&owner_symbol, ids.session, 0};
  qa_media_clock clock = {&clock_ms, assets_clock};
  warn_holder.sentinel = 0x77; warn_holder.count = 0;
  profile_holder.sentinel = 0x99; profile_holder.calls = 0;
  qa_app_application_assets_options options;
  memset(&options, 0, sizeof options);
  options.warn = assets_warn; options.warn_ctx = &warn_holder;
  options.shader_profile = assets_profile; options.shader_profile_ctx = &profile_holder;
  qa_app_application_assets *assets = qa_app_application_assets_create(content, owner, &clock, &options);
  CHECK(assets != NULL);
  CHECK(qa_app_application_assets_image_policy(assets) == NULL);
  qa_model_replacement_policy policy = qa_app_application_assets_model_policy(assets);
  CHECK(policy.q1_enhanced == qa_default_model_replacement_policy.q1_enhanced);
  CHECK(raises(world_body, assets, "World presentation has not loaded"));
  qa_app_application_assets_warn_once(assets, "key", "missing\n");
  qa_app_application_assets_warn_once(assets, "key", "missing again\n");
  CHECK(warn_holder.count == 1 && strcmp(warn_holder.last, "missing\n") == 0);
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(content);
  qa_world_scene *world = qa_app_application_assets_load_world(assets);
  CHECK(world != NULL);
  CHECK(qa_app_application_assets_world(assets) == world);
  CHECK(qa_app_application_assets_load_world(assets) == world);
  CHECK(profile_holder.calls == 1);
  qa_app_provider_scene_assets provider = qa_app_application_assets_provider(assets, recipe->presentation.assets);
  CHECK(provider.family == QA_GAME_FAMILY_Q1);
  CHECK(provider.palette != NULL && provider.palette->colors.length == 768);
  CHECK(provider.textures == provider.shaders->textures);
  CHECK(world->shaders == provider.shaders);
  /* Inline models resolve against the loaded world. */
  const char *entities_content = recipe->map.entities.content;
  qa_app_model_asset inline_model = qa_app_application_assets_model(assets, entities_content, "*1");
  CHECK(inline_model.model.kind == QA_DECODED_MODEL_BRUSH_MODEL && inline_model.model.brush_model_index == 1);
  CHECK(inline_model.brush_scene == world);
  model_call missing = {assets, entities_content, "*9999"};
  CHECK(raises(model_body, &missing, "Missing inline model *9999"));
  model_call negative = {assets, entities_content, "*-1"};
  CHECK(raises(model_body, &negative, "Missing inline model *-1"));
  model_call absent = {assets, entities_content, "progs/absent.mdl"};
  char absent_message[256];
  snprintf(absent_message, sizeof absent_message, "Model is absent from selected content: %s/progs/absent.mdl", entities_content);
  CHECK(raises(model_body, &absent, absent_message));
  /* An alias model with its variants, cached by content and path. */
  qa_app_model_asset flame = qa_app_application_assets_model(assets, entities_content, "progs/flame.mdl");
  CHECK(flame.model.kind == QA_DECODED_MODEL_Q1_ALIAS);
  CHECK(flame.variants != NULL);
  CHECK(flame.brush_scene == NULL);
  qa_app_model_asset again = qa_app_application_assets_model(assets, entities_content, "progs/flame.mdl");
  CHECK(again.variants == flame.variants && again.model.as.q1_alias == flame.model.as.q1_alias);
  /* A BSP model loads its own brush scene. */
  qa_app_model_asset box = qa_app_application_assets_model(assets, entities_content, "maps/b_bh10.bsp");
  CHECK(box.model.kind == QA_DECODED_MODEL_BRUSH_MODEL && box.model.brush_model_index == 0);
  CHECK(box.brush_scene != NULL && box.brush_scene != world);
  /* An image refresh stages every provider and rebinds the worlds. */
  qa_scene_image_policy image_policy = qa_default_image_policy();
  qa_app_prepared_images *prepared = qa_app_application_assets_prepare_image_refresh(assets, &image_policy, NULL);
  qa_app_provider_scene_assets staged = qa_app_prepared_images_provider(prepared, recipe->presentation.assets);
  CHECK(staged.shaders != provider.shaders && staged.textures != provider.textures);
  qa_app_prepared_images_discard(prepared);
  prepared = qa_app_application_assets_prepare_image_refresh(assets, &image_policy, NULL);
  qa_scene_texture_loader *staged_textures = qa_app_prepared_images_provider(prepared, recipe->presentation.assets).textures;
  qa_app_prepared_images_commit(prepared);
  provider = qa_app_application_assets_provider(assets, recipe->presentation.assets);
  CHECK(provider.textures == staged_textures);
  CHECK(world->shaders == provider.shaders);
  CHECK(qa_app_application_assets_image_policy(assets) != NULL);
  qa_app_application_assets_finish_image_refresh(assets);
  qa_app_application_assets_close(assets);
  model_call closed = {assets, entities_content, NULL};
  CHECK(raises(closed_provider_body, &closed, "Application assets are closed"));
  qa_app_application_assets_destroy(assets);
  qa_identity_owner_destroy(&ids);
  qa_app_loaded_content_close(content);
  qa_app_loaded_content_destroy(content);
  qa_app_application_command_destroy(&loaded_command);
}

/* --- ApplicationAssets over a synthetic loose Q1 provider --- */
typedef struct synthetic_content {
  int sentinel;
  qa_content_mounted *mounts, *q3_mounts;
  qa_executable_recipe recipe;
  qa_content_catalog *catalog;
  int for_content_calls;
} synthetic_content;
static synthetic_content synthetic;
static const char *synthetic_open_loaded(void *ctx, qa_app_loaded_content *content) {
  CHECK(ctx == &synthetic && synthetic.sentinel == 0x5c);
  qa_app_loaded_content_set_catalog(content, synthetic.catalog);
  qa_app_loaded_content_set_mounts(content, synthetic.mounts);
  qa_app_loaded_content_set_recipe(content, synthetic.recipe);
  return NULL;
}
static const char *synthetic_for_content(void *ctx, qa_app_loaded_content *content, qa_content_id id, const void **out) {
  (void)content;
  CHECK(ctx == &synthetic && synthetic.sentinel == 0x5c);
  CHECK(strcmp(id, "q1-classic-id1") == 0 || strcmp(id, "q2-classic-baseq2") == 0);
  synthetic.for_content_calls++;
  *out = strcmp(id, "q2-classic-baseq2") == 0 ? synthetic.q3_mounts : synthetic.mounts;
  return NULL;
}
static void write_file(const char *dir, const char *name, const void *bytes, size_t length) {
  char path[1024];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  FILE *file = fopen(path, "wb");
  CHECK(file != NULL);
  if (file == NULL) return;
  fwrite(bytes, 1, length, file);
  fclose(file);
}
/* An 8-bit PCX of size x size pixels with a trailing 256-color palette. */
static void write_pcx(const char *dir, const char *name, int size) {
  size_t pixels = (size_t)size * (size_t)size, length = 128 + pixels + 769;
  uint8_t *bytes = calloc(1, length);
  bytes[0] = 10; bytes[1] = 5; bytes[2] = 1; bytes[3] = 8;
  bytes[8] = (uint8_t)(size - 1); bytes[10] = (uint8_t)(size - 1);
  bytes[65] = 1; bytes[66] = (uint8_t)size; bytes[68] = 1;
  for (size_t i = 0; i < pixels; i++) bytes[128 + i] = (uint8_t)(i % 0xc0);
  bytes[128 + pixels] = 12;
  for (size_t i = 0; i < 768; i++) bytes[128 + pixels + 1 + i] = (uint8_t)(255 - i / 3);
  write_file(dir, name, bytes, length);
  free(bytes);
}
static void registry_release(void *arg) { qa_scene_image_registry_destroy(arg); free(arg); }
static void registry_owner_body(void *ctx) {
  qa_app_application_assets **out = ctx;
  qa_scene_image_registry *other = calloc(1, sizeof *other);
  qa_cleanup guard;
  qa_cleanup_push(&guard, registry_release, other);
  int foreign_symbol = 0;
  qa_renderer_resource_owner foreign = {&foreign_symbol, {"foreign", &foreign_symbol}, 0};
  qa_scene_image_registry_init(other, foreign, NULL);
  qa_app_application_assets_options options;
  memset(&options, 0, sizeof options);
  options.image_registry = other;
  int owner_symbol = 0;
  qa_renderer_resource_owner owner = {&owner_symbol, {"owner", &owner_symbol}, 0};
  *out = qa_app_application_assets_create(NULL, owner, NULL, &options);
  qa_cleanup_pop(&guard);
  registry_release(other);
}
static void prepare_closed_body(void *ctx) {
  qa_scene_image_policy policy = qa_default_image_policy();
  (void)qa_app_application_assets_prepare_image_refresh(ctx, &policy, NULL);
}
static void test_create_world_assets(qa_app_loaded_content *content);
static void test_synthetic_assets(void) {
  const char *base = getenv("TMPDIR");
  char dir[512], sub[800];
  snprintf(dir, sizeof dir, "%s/top-assets-XXXXXX", base != NULL ? base : "/tmp");
  CHECK(mkdtemp(dir) != NULL);
  snprintf(sub, sizeof sub, "%s/gfx", dir); mkdir(sub, 0700);
  snprintf(sub, sizeof sub, "%s/scripts", dir); mkdir(sub, 0700);
  snprintf(sub, sizeof sub, "%s/scripts/folder.shader", dir); mkdir(sub, 0700);
  uint8_t palette[768];
  for (size_t i = 0; i < sizeof palette; i++) palette[i] = (uint8_t)(i / 3);
  write_file(dir, "gfx/palette.lmp", palette, sizeof palette);
  const char *a = "textures/alpha\n{\n}\n", *b = "textures/beta\n{\n}\n", *z = "textures/zeta\n{\n}\n";
  write_file(dir, "scripts/b.shader", b, strlen(b));
  write_file(dir, "scripts/a.shader", a, strlen(a));
  write_file(dir, "scripts/z.SHADER", z, strlen(z));
  write_file(dir, "scripts/notes.txt", a, strlen(a));

  char q3_dir[600];
  snprintf(q3_dir, sizeof q3_dir, "%s/q2", dir); mkdir(q3_dir, 0700);
  snprintf(sub, sizeof sub, "%s/pics", q3_dir); mkdir(sub, 0700);
  write_pcx(q3_dir, "pics/colormap.pcx", 16);
  write_pcx(q3_dir, "pics/conchars.pcx", 128);
  qa_catalog_discovered_product products[2];
  memset(products, 0, sizeof products);
  products[0].id = "q1-classic-id1";
  products[0].expectation = qa_expected_products[0];
  products[0].availability.kind = QA_CATALOG_INSTALLED;
  products[1].id = "q2-classic-baseq2";
  for (size_t i = 0; i < qa_expected_product_count; i++)
    if (strcmp(qa_expected_products[i].id, "q2-classic-baseq2") == 0) products[1].expectation = qa_expected_products[i];
  products[1].availability.kind = QA_CATALOG_INSTALLED;
  products[1].loose_root = q3_dir;
  /* No Quake III or rerelease product: typography is the classic charset. */
  qa_content_catalog catalog;
  qa_content_catalog_init(&catalog, dir, products, 2, NULL, 0, 0, NULL, NULL, 0);
  qa_content_mount mount;
  memset(&mount, 0, sizeof mount);
  mount.kind = QA_CONTENT_MOUNT_LOOSE;
  mount.as.loose.identity.id = "mount:test:loose";
  mount.as.loose.identity.content = "q1-classic-id1";
  mount.as.loose.root_path = dir;
  qa_mount_id order[1] = {"mount:test:loose"};
  qa_resolved_mount_plan plan;
  memset(&plan, 0, sizeof plan);
  plan.id = "mount-plan:test:loose";
  plan.mounts = &mount; plan.mount_count = 1;
  plan.default_order = order; plan.default_order_count = 1;
  memset(&synthetic, 0, sizeof synthetic);
  synthetic.sentinel = 0x5c;
  synthetic.catalog = &catalog;
  synthetic.mounts = qa_open_mount_plan(&plan, NULL);
  CHECK(synthetic.mounts != NULL);
  qa_content_mount q3_mount = mount;
  q3_mount.as.loose.identity.id = "mount:test:q2";
  q3_mount.as.loose.identity.content = "q2-classic-baseq2";
  q3_mount.as.loose.root_path = q3_dir;
  qa_mount_id q3_order[1] = {"mount:test:q2"};
  qa_resolved_mount_plan q3_plan = plan;
  q3_plan.id = "mount-plan:test:q2";
  q3_plan.mounts = &q3_mount; q3_plan.default_order = q3_order;
  synthetic.q3_mounts = qa_open_mount_plan(&q3_plan, NULL);
  CHECK(synthetic.q3_mounts != NULL);
  synthetic.recipe.presentation.assets = "q2-classic-baseq2";
  qa_app_content_host host;
  memset(&host, 0, sizeof host);
  host.ctx = &synthetic;
  host.open_loaded = synthetic_open_loaded;
  host.for_content = synthetic_for_content;
  qa_app_options options;
  memset(&options, 0, sizeof options);
  qa_app_loaded_content *content = qa_app_load_application_content(host, options, NULL, false);

  qa_app_application_assets *rejected = NULL;
  CHECK(raises(registry_owner_body, &rejected, "Application images belong to another renderer"));
  qa_identity_owner ids;
  CHECK(qa_identity_owner_create(&ids, "synthetic"));
  int owner_symbol = 0;
  qa_renderer_resource_owner owner = {&owner_symbol, ids.session, 0};
  qa_media_clock clock = {&clock_ms, assets_clock};
  profile_holder.sentinel = 0x99; profile_holder.calls = 0;
  warn_holder.sentinel = 0x77; warn_holder.count = 0;
  qa_scene_image_policy image_policy = qa_default_image_policy();
  qa_model_replacement_policy model_policy = qa_default_model_replacement_policy;
  model_policy.q1_enhanced = !model_policy.q1_enhanced;
  qa_app_application_assets_options assets_options;
  memset(&assets_options, 0, sizeof assets_options);
  assets_options.image_policy = &image_policy;
  assets_options.model_policy = &model_policy;
  assets_options.shader_profile = assets_profile; assets_options.shader_profile_ctx = &profile_holder;
  assets_options.warn = assets_warn; assets_options.warn_ctx = &warn_holder;
  qa_app_application_assets *assets = qa_app_application_assets_create(content, owner, &clock, &assets_options);
  CHECK(qa_app_application_assets_content(assets) == content);
  const qa_scene_image_policy *stored = qa_app_application_assets_image_policy(assets);
  CHECK(stored != NULL && stored != &image_policy && stored->override_level == image_policy.override_level);
  CHECK(qa_app_application_assets_model_policy(assets).q1_enhanced == model_policy.q1_enhanced);
  qa_app_application_assets_set_model_policy(assets, qa_default_model_replacement_policy);
  CHECK(qa_app_application_assets_model_policy(assets).q1_enhanced == qa_default_model_replacement_policy.q1_enhanced);
  CHECK(raises(world_body, assets, "World presentation has not loaded"));
  qa_app_application_assets_warn_once(assets, "palette", "first\n");
  qa_app_application_assets_warn_once(assets, "palette", "second\n");
  qa_app_application_assets_warn_once(assets, "other", "third\n");
  CHECK(warn_holder.count == 2 && strcmp(warn_holder.last, "third\n") == 0);

  qa_app_provider_scene_assets provider = qa_app_application_assets_provider(assets, "q1-classic-id1");
  CHECK(provider.family == QA_GAME_FAMILY_Q1);
  CHECK(provider.mounts == synthetic.mounts);
  CHECK(provider.palette != NULL && provider.palette->colors.length == 768
      && memcmp(provider.palette->colors.bytes, palette, 768) == 0);
  CHECK(provider.textures != NULL && provider.textures == provider.shaders->textures);
  CHECK(provider.model_policy.q1_enhanced == qa_default_model_replacement_policy.q1_enhanced);
  CHECK(profile_holder.calls == 1);
  /* shaderPaths: loose files matching /\.shader$/i (z.SHADER included),
   * sorted case-insensitively; the folder named folder.shader is skipped. */
  CHECK(qa_scene_shader_registry_has_authored(provider.shaders, "textures/alpha"));
  CHECK(qa_scene_shader_registry_has_authored(provider.shaders, "textures/beta"));
  CHECK(qa_scene_shader_registry_has_authored(provider.shaders, "textures/zeta"));
  qa_app_provider_scene_assets again = qa_app_application_assets_provider(assets, "q1-classic-id1");
  CHECK(again.shaders == provider.shaders && again.textures == provider.textures && profile_holder.calls == 1);
  CHECK(synthetic.for_content_calls == 1);

  /* An image refresh stages the provider; discard leaves it bound, commit
   * rebinds its textures and retires the old ones until finish. */
  qa_text_font_selection font = qa_app_application_assets_load_console_font(assets);
  CHECK(font.classic != NULL);
  CHECK(qa_app_application_assets_load_console_font(assets).classic == font.classic);
  const qa_app_menu_typography *typography = qa_app_application_assets_load_menu_typography(assets);
  CHECK(typography != NULL && qa_app_application_assets_load_menu_typography(assets) == typography);
  CHECK(qa_app_menu_typography_body(typography).classic == font.classic);
  qa_app_provider_scene_assets q2 = qa_app_application_assets_provider(assets, "q2-classic-baseq2");
  CHECK(q2.family == QA_GAME_FAMILY_Q2 && q2.palette != NULL && q2.palette->colors.length == 768
      && q2.palette->colors.bytes[0] == 255 && q2.palette->colors.bytes[767] == 0);
  qa_scene_image_policy refresh_policy = qa_default_image_policy();
  refresh_policy.override_level = 0;
  qa_app_prepared_images *prepared = qa_app_application_assets_prepare_image_refresh(assets, &refresh_policy, NULL);
  qa_app_provider_scene_assets staged = qa_app_prepared_images_provider(prepared, "q1-classic-id1");
  CHECK(staged.shaders != provider.shaders && staged.textures != provider.textures && staged.palette == provider.palette);
  CHECK(qa_app_prepared_images_policy(prepared)->override_level == 0);
  CHECK(qa_app_prepared_images_font(prepared).classic != font.classic);
  qa_app_prepared_images_discard(prepared);
  provider = qa_app_application_assets_provider(assets, "q1-classic-id1");
  CHECK(provider.textures == provider.shaders->textures);
  CHECK(qa_app_application_assets_image_policy(assets)->override_level == image_policy.override_level);
  prepared = qa_app_application_assets_prepare_image_refresh(assets, &refresh_policy, NULL);
  qa_scene_texture_loader *staged_textures = qa_app_prepared_images_provider(prepared, "q1-classic-id1").textures;
  qa_text_font_selection staged_font = qa_app_prepared_images_font(prepared);
  qa_app_prepared_images_commit(prepared);
  qa_app_provider_scene_assets committed = qa_app_application_assets_provider(assets, "q1-classic-id1");
  CHECK(committed.shaders == provider.shaders && committed.textures == staged_textures);
  CHECK(qa_scene_shader_registry_has_authored(committed.shaders, "textures/alpha"));
  CHECK(qa_app_application_assets_image_policy(assets)->override_level == 0);
  CHECK(qa_app_application_assets_load_console_font(assets).classic == staged_font.classic);
  CHECK(raises(prepare_closed_body, assets, "Previous image refresh has not finished rebinding"));
  qa_app_application_assets_finish_image_refresh(assets);
  qa_app_application_assets_close(assets);
  qa_app_application_assets_close(assets);
  model_call closed = {assets, "q1-classic-id1", NULL};
  CHECK(raises(closed_provider_body, &closed, "Application assets are closed"));
  CHECK(raises(prepare_closed_body, assets, "Application assets are closed"));
  qa_app_application_assets_destroy(assets);
  test_create_world_assets(content);
  qa_identity_owner_destroy(&ids);
  qa_app_loaded_content_destroy(content);
  qa_content_mounted_destroy(synthetic.mounts);
  qa_content_mounted_destroy(synthetic.q3_mounts);
  char command[700];
  snprintf(command, sizeof command, "rm -rf '%s'", dir);
  CHECK(system(command) == 0);
}

/* TS createWorldAssets: shares the renderer's image registry (forked) and
 * adopts the world image and model policies. */
static int world_warn_cell, world_warn_bad;
static void world_warn(void *ctx, const char *text) { (void)text; if (ctx != &world_warn_cell) world_warn_bad++; }
static void test_create_world_assets(qa_app_loaded_content *content) {
  int owner_symbol = 0;
  qa_renderer_resource_owner owner = {&owner_symbol, {"world-owner", &owner_symbol}, 0};
  qa_scene_image_registry *root = calloc(1, sizeof *root);
  qa_scene_image_registry_init(root, owner, NULL);
  qa_identity_owner ids;
  CHECK(qa_identity_owner_create(&ids, "world-assets"));
  qa_cvar_registry_options cvar_options;
  memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q3;
  cvar_options.context.session = ids.session;
  cvar_options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&cvar_options);
  qa_app_world_asset_images images;
  memset(&images, 0, sizeof images);
  images.policy = qa_default_image_policy();
  images.policy.override_level = 2;
  images.model_policy = qa_default_model_replacement_policy;
  images.model_policy.q1_enhanced = !images.model_policy.q1_enhanced;
  images.cvars = cvars;
  qa_app_application_assets *assets = qa_app_create_world_assets(content, owner, root, world_warn, &world_warn_cell, &images);
  CHECK(assets != NULL);
  CHECK(qa_app_application_assets_images(assets) != root); /* imageRegistry.fork(clock) */
  CHECK(qa_app_application_assets_image_policy(assets) != NULL
        && qa_app_application_assets_image_policy(assets)->override_level == 2);
  CHECK(qa_app_application_assets_model_policy(assets).q1_enhanced == images.model_policy.q1_enhanced);
  qa_app_application_assets_warn_once(assets, "k", "missing\n");
  qa_app_application_assets_warn_once(assets, "k", "missing\n");
  CHECK(world_warn_bad == 0);
  qa_app_application_assets_destroy(assets);
  /* images null: TS {} options beyond the registry and warn. */
  assets = qa_app_create_world_assets(content, owner, root, world_warn, &world_warn_cell, NULL);
  CHECK(qa_app_application_assets_image_policy(assets) == NULL);
  qa_app_application_assets_destroy(assets);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&ids);
  qa_scene_image_registry_destroy(root);
  free(root);
}

int main(void) {
  test_q3_recording();
  test_q1_recording();
  test_synthetic_assets();
  test_q1_assets();
  printf("test_top_assets: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
