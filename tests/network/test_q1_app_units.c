/* C checks for the unit layer of tests/network/q1/qw-skins.test.ts and
 * tests/network/hardening/truncated-demo.test.ts. GPL-2.0-or-later. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/q1_demo.h"
#include "qa/app/bootstrap/network/qw_skins.h"
#include "qa/app/bootstrap/network/qw_downloads.h"
#include <dirent.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/app/bootstrap/network/recorded_source.h"
#include "qa/core/cvars/index.h"
#include "qa/formats/images/indexed.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/demos.h"
#include "qa/network/q1/profile.h"
#include "qa/network/q1/qw_recording.h"
#include "qa/network/q1/recording.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- qw-skins.test.ts ---- */
static uint8_t *pcx(int width, int height, size_t *length) {
  qa_indexed_image image;
  image.width = width; image.height = height;
  image.indices_length = (size_t)(width * height);
  image.indices = malloc(image.indices_length ? image.indices_length : 1);
  for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) image.indices[y * width + x] = (uint8_t)((x + y) % 190);
  uint8_t palette[768] = {0};
  uint8_t *bytes = qa_encode_pcx(&image, palette, sizeof palette, length);
  free(image.indices);
  return bytes;
}
typedef struct skin_file { char *path; uint8_t *bytes; size_t length; } skin_file;
typedef struct skin_fixture {
  skin_file files[8]; size_t file_count;
  char *reads[32]; size_t read_count;
  double noskins; const char *baseskin, *allskins;
} skin_fixture;
static void set_file(skin_fixture *f, const char *path, uint8_t *bytes, size_t length) {
  for (size_t i = 0; i < f->file_count; i++)
    if (strcmp(f->files[i].path, path) == 0) { free(f->files[i].bytes); f->files[i].bytes = bytes; f->files[i].length = length; return; }
  f->files[f->file_count].path = strdup(path); f->files[f->file_count].bytes = bytes; f->files[f->file_count].length = length;
  f->file_count++;
}
static bool skin_read(void *ctx, const char *path, uint8_t **out, size_t *length) {
  skin_fixture *f = ctx;
  CHECK(f != NULL && f->baseskin != NULL);
  if (f->read_count < 32) f->reads[f->read_count++] = strdup(path);
  for (size_t i = 0; i < f->file_count; i++)
    if (strcmp(f->files[i].path, path) == 0) {
      *out = malloc(f->files[i].length); memcpy(*out, f->files[i].bytes, f->files[i].length);
      *length = f->files[i].length;
      return true;
    }
  *out = NULL; *length = 0;
  return false;
}
static double skin_noskins(void *ctx) { return ((skin_fixture *)ctx)->noskins; }
static const char *skin_baseskin(void *ctx) { return ((skin_fixture *)ctx)->baseskin; }
static const char *skin_allskins(void *ctx) { return ((skin_fixture *)ctx)->allskins; }
static qa_app_qw_player_skins *fixture(skin_fixture *f) {
  memset(f, 0, sizeof *f);
  f->baseskin = "base"; f->allskins = "";
  qa_app_qw_skin_options options = {f, skin_read, skin_noskins, skin_baseskin, skin_allskins};
  return qa_app_qw_player_skins_create(options);
}
static void fixture_free(skin_fixture *f, qa_app_qw_player_skins *skins) {
  qa_app_qw_player_skins_destroy(skins);
  for (size_t i = 0; i < f->file_count; i++) { free(f->files[i].path); free(f->files[i].bytes); }
  for (size_t i = 0; i < f->read_count; i++) free(f->reads[i]);
}
static void test_skin_crop_and_pad(void) {
  skin_fixture f;
  qa_app_qw_player_skins *skins = fixture(&f);
  size_t n = 0;
  uint8_t *b = pcx(320, 200, &n); set_file(&f, "skins/full.pcx", b, n);
  b = pcx(2, 2, &n); set_file(&f, "skins/small.pcx", b, n);
  const qa_indexed_model_skin *full = qa_app_qw_player_skins_select(skins, "full");
  CHECK(full != NULL);
  if (full) {
    CHECK(full->width == 296 && full->height == 194);
    CHECK(full->pixels.bytes[296] == 1 && full->pixels.bytes[295] == 105);
    CHECK(full->pixels.bytes[193 * 296 + 295] == (193 + 295) % 190);
    CHECK(strstr(full->name, ":crop:0,0,296,194:stride320") != NULL);
  }
  const qa_indexed_model_skin *small = qa_app_qw_player_skins_select(skins, "small");
  CHECK(small != NULL);
  if (small) {
    CHECK(small->pixels.bytes[0] == 0 && small->pixels.bytes[1] == 1 && small->pixels.bytes[2] == 0);
    CHECK(small->pixels.bytes[296] == 1 && small->pixels.bytes[297] == 2 && small->pixels.bytes[298] == 0);
    CHECK(small->pixels.bytes[592] == 0);
    if (full) CHECK(strcmp(small->name, full->name) != 0);
  }
  fixture_free(&f, skins);
}
static void test_skin_selection(void) {
  skin_fixture f;
  qa_app_qw_player_skins *skins = fixture(&f);
  size_t n = 0;
  uint8_t *b = pcx(2, 2, &n); set_file(&f, "skins/base.pcx", b, n);
  f.noskins = 1;
  CHECK(qa_app_qw_player_skins_select(skins, "user") == NULL);
  CHECK(f.read_count == 0);
  f.noskins = 2;
  CHECK(qa_app_qw_player_skins_select(skins, "user") != NULL);
  CHECK(f.read_count == 2 && strcmp(f.reads[0], "skins/user.pcx") == 0 && strcmp(f.reads[1], "skins/base.pcx") == 0);
  qa_app_qw_player_skins_select(skins, "user");
  CHECK(f.read_count == 2);
  f.allskins = "abcdefghijklmnop.pcx";
  qa_app_qw_player_skins_select(skins, "ignored");
  bool found = false;
  for (size_t i = 0; i < f.read_count; i++) if (strcmp(f.reads[i], "skins/abcdefghijklmno.pcx") == 0) found = true;
  CHECK(found);
  f.allskins = "";
  qa_app_qw_player_skins_select(skins, "../unsafe");
  CHECK(f.read_count > 0 && strcmp(f.reads[f.read_count - 1], "skins/base.pcx") == 0);
  qa_app_qw_player_skins_clear(skins);
  b = pcx(3, 3, &n); set_file(&f, "skins/user.pcx", b, n);
  const qa_indexed_model_skin *replaced = qa_app_qw_player_skins_select(skins, "user");
  CHECK(replaced != NULL && replaced->pixels.bytes[2] == 2);
  fixture_free(&f, skins);
}
static void test_skin_malformed(void) {
  skin_fixture f;
  qa_app_qw_player_skins *skins = fixture(&f);
  size_t n = 0;
  uint8_t *b = pcx(2, 2, &n); set_file(&f, "skins/base.pcx", b, n);
  set_file(&f, "skins/bad.pcx", calloc(5, 1), 5);
  CHECK(qa_app_qw_player_skins_select(skins, "bad") == NULL);
  CHECK(f.read_count == 1 && strcmp(f.reads[0], "skins/bad.pcx") == 0);
  b = pcx(2, 2, &n); set_file(&f, "skins/bad.pcx", b, n);
  CHECK(qa_app_qw_player_skins_select(skins, "bad") == NULL);
  qa_app_qw_player_skins_clear(skins);
  CHECK(qa_app_qw_player_skins_select(skins, "bad") != NULL);
  b = pcx(321, 2, &n); set_file(&f, "skins/wide.pcx", b, n);
  CHECK(qa_app_qw_player_skins_select(skins, "wide") == NULL);
  /* A present PCX whose RLE body is malformed decodes to null, not a process abort. */
  b = pcx(4, 4, &n);
  for (size_t i = 128; i < n; i++) b[i] = 0xff;
  set_file(&f, "skins/rle.pcx", b, n);
  CHECK(qa_app_qw_player_skins_select(skins, "rle") == NULL);
  fixture_free(&f, skins);
}
static void test_skin_odd_width(void) {
  skin_fixture f;
  qa_app_qw_player_skins *skins = fixture(&f);
  size_t n = 0;
  uint8_t *b = pcx(3, 2, &n); set_file(&f, "skins/odd.pcx", b, n);
  const qa_indexed_model_skin *skin = qa_app_qw_player_skins_select(skins, "odd");
  CHECK(skin != NULL);
  if (skin) {
    CHECK(skin->pixels.bytes[0] == 0 && skin->pixels.bytes[1] == 1 && skin->pixels.bytes[2] == 2 && skin->pixels.bytes[3] == 0);
    CHECK(skin->pixels.bytes[296] == 1 && skin->pixels.bytes[297] == 2 && skin->pixels.bytes[298] == 3 && skin->pixels.bytes[299] == 0);
  }
  fixture_free(&f, skins);
}

/* ---- truncated-demo.test.ts ---- */
#define REMOTE_SENTINEL ((void *)0x5e)
static bool remote_false(void *ctx) { CHECK(ctx == REMOTE_SENTINEL); return false; }
static double remote_seconds(void *ctx) { CHECK(ctx == REMOTE_SENTINEL); return 0; }
static void remote_receive(void *ctx, qa_netquake_message *messages, size_t count, double now,
    qa_app_demo_assert_fn assert_current, void *assert_ctx) {
  (void)messages; (void)count; (void)now; (void)assert_current; (void)assert_ctx;
  CHECK(ctx == REMOTE_SENTINEL);
}
static void remote_angles(void *ctx, qa_vec3 angles, bool demo) { (void)angles; (void)demo; CHECK(ctx == REMOTE_SENTINEL); }
static void remote_sample(void *ctx, double seconds) { (void)seconds; CHECK(ctx == REMOTE_SENTINEL); }
static qa_app_recorded_q1_result playback_advance(void *ctx, double elapsed_seconds, double frame, bool timedemo) {
  qa_app_netquake_demo_input *input = ctx;
  qa_app_q1_demo_frame f = {elapsed_seconds, frame, timedemo};
  qa_app_q1_demo_progress progress = qa_app_netquake_demo_input_advance(input, f);
  qa_app_recorded_q1_result result = {progress.phase, progress.reason};
  return result;
}
static void playback_close(void *ctx) { qa_app_netquake_demo_input_close(ctx); }
static char reasons[4][32]; static int reason_count = 0;
static void complete(void *ctx, const char *reason, const qa_app_demo_timing *timing) {
  (void)timing;
  CHECK(ctx == (void *)0x5f);
  if (reason_count < 4) snprintf(reasons[reason_count++], 32, "%s", reason);
}
static void test_truncated_demo(void) {
  static const uint8_t bytes[] = {0x2d, 0x31, 0x0a, 0x40, 0x00, 0x00, 0x00, 1, 2, 3};
  qa_netquake_demo_reader reader;
  qa_netquake_demo_reader_init(&reader, bytes, sizeof bytes, 64000);
  qa_app_q1_demo_remote remote = {REMOTE_SENTINEL, remote_false, remote_false, remote_seconds,
      remote_receive, remote_angles, remote_sample};
  qa_app_netquake_demo_input *input = qa_app_netquake_demo_input_create(&reader, remote);
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&options);
  qa_app_recorded_playback playback;
  memset(&playback, 0, sizeof playback);
  playback.ctx = input; playback.family = QA_APP_REC_Q1;
  playback.advance_q1 = playback_advance; playback.close = playback_close;
  qa_app_recorded_demo_resource resource = {QA_APP_REC_Q1, bytes, sizeof bytes, "cut.dem"};
  qa_app_recorded_source *source = qa_app_recorded_source_create(resource, QA_APP_REC_Q1, playback,
      false, cvars, complete, (void *)0x5f);
  qa_app_recorded_source_advance(source, 16, 1, 16);
  CHECK(reason_count == 1 && strcmp(reasons[0], "truncated") == 0);
  CHECK(strcmp(qa_app_recorded_source_phase(source), "closed") == 0);
  qa_app_recorded_source_destroy(source);
  qa_cvar_registry_destroy(cvars);
  qa_app_netquake_demo_input_destroy(input);
}

/* demo-input.test.ts: 'NQ malformed demo errors propagate without converting them into normal EOF'. */
typedef struct advance_call { qa_app_netquake_demo_input *input; qa_app_q1_demo_frame frame; qa_app_q1_demo_progress progress; } advance_call;
static void advance_body(void *ctx) {
  advance_call *call = ctx;
  call->progress = qa_app_netquake_demo_input_advance(call->input, call->frame);
}
static void test_malformed_demo_propagates(void) {
  static const uint8_t bytes[] = {0x2d, 0x31, 0x0a, 1};
  qa_netquake_demo_reader reader;
  qa_netquake_demo_reader_init(&reader, bytes, sizeof bytes, 64000);
  qa_app_q1_demo_remote remote = {REMOTE_SENTINEL, remote_false, remote_false, remote_seconds,
      remote_receive, remote_angles, remote_sample};
  qa_app_netquake_demo_input *input = qa_app_netquake_demo_input_create(&reader, remote);
  advance_call call;
  memset(&call, 0, sizeof call);
  call.input = input;
  qa_caught_error caught;
  CHECK(!qa_try(advance_body, &call, &caught));
  CHECK(strcmp(caught.name, "PacketError") == 0);
  qa_app_netquake_demo_input_close(input);
  call.frame.frame = 1;
  CHECK(qa_try(advance_body, &call, &caught));
  CHECK(strcmp(call.progress.phase, "ended") == 0 && call.progress.reason && strcmp(call.progress.reason, "closed") == 0);
  qa_app_netquake_demo_input_destroy(input);
}

/* ---- recording.test.ts ---- */
typedef struct seed_call { qa_netquake_recording_state *state; qa_q1_protocol_identity protocol; } seed_call;
static void seed_body(void *ctx) {
  seed_call *call = ctx;
  size_t *lens = NULL, count = 0;
  uint8_t **packets = qa_netquake_recording_state_seed(call->state, call->protocol, &lens, &count);
  for (size_t i = 0; i < count; i++) free(packets[i]);
  free(packets); free(lens);
}
static size_t seed_messages(qa_netquake_recording_state *state, qa_q1_protocol_identity protocol,
    qa_netquake_decoder *decoder, qa_netquake_message **out) {
  size_t *lens = NULL, count = 0, total = 0;
  uint8_t **packets = qa_netquake_recording_state_seed(state, protocol, &lens, &count);
  *out = NULL;
  for (size_t i = 0; i < count; i++) {
    size_t n = 0;
    qa_netquake_message *messages = qa_netquake_decoder_decode(decoder, packets[i], lens[i], &n);
    *out = realloc(*out, (total + n + 1) * sizeof **out);
    memcpy(*out + total, messages, n * sizeof *messages);
    total += n;
    free(messages); free(packets[i]);
  }
  free(packets); free(lens);
  return total;
}
static void free_messages(qa_netquake_message *messages, size_t count) {
  for (size_t i = 0; i < count; i++) qa_netquake_message_destroy(&messages[i]);
  free(messages);
}
static void test_netquake_recording(void) {
  qa_q1_protocol_identity protocols[3];
  memset(protocols, 0, sizeof protocols);
  protocols[0].kind = "q1-netquake"; protocols[0].version = 15;
  protocols[1].kind = "q1-fitzquake"; protocols[1].version = 666;
  protocols[2].kind = "q1-rmq"; protocols[2].version = 999; protocols[2].flags = 0;
  for (int p = 0; p < 3; p++) {
    qa_netquake_recording_state *state = qa_netquake_recording_state_create();
    seed_call call = {state, protocols[p]};
    qa_caught_error caught;
    CHECK(!qa_try(seed_body, &call, &caught) && strstr(caught.message, "server info") != NULL);
    char *models[] = {(char *)"maps/start.bsp"};
    qa_netquake_message first[4];
    memset(first, 0, sizeof first);
    first[0].kind = "server-info"; first[0].protocol = protocols[p]; first[0].max_clients = 1;
    first[0].level = (char *)"start"; first[0].models = models; first[0].model_count = 1;
    first[1].kind = "name"; first[1].slot = 0; first[1].str_value = (char *)"first";
    first[2].kind = "stat"; first[2].index = 0; first[2].value = 100;
    first[3].kind = "set-view"; first[3].entity = 1;
    qa_netquake_recording_state_observe(state, first, 4);
    qa_netquake_message second[2];
    memset(second, 0, sizeof second);
    second[0].kind = "name"; second[0].slot = 0; second[0].str_value = (char *)"current";
    second[1].kind = "stat"; second[1].index = 0; second[1].value = 75;
    qa_netquake_recording_state_observe(state, second, 2);
    qa_netquake_decoder decoder;
    qa_q1_protocol_identity retail = {0};
    retail.kind = "q1-netquake"; retail.version = 15;
    qa_netquake_decoder_init(&decoder, retail, "known-retail", true);
    qa_netquake_message *messages = NULL;
    size_t count = seed_messages(state, protocols[p], &decoder, &messages);
    CHECK(decoder.protocol.version == protocols[p].version && decoder.protocol.flags == protocols[p].flags);
    int names = 0, stats = 0, signons = 0;
    for (size_t i = 0; i < count; i++) {
      if (strcmp(messages[i].kind, "name") == 0) { names++; CHECK(messages[i].slot == 0 && strcmp(messages[i].str_value, "current") == 0); }
      if (strcmp(messages[i].kind, "stat") == 0) { stats++; CHECK(messages[i].index == 0 && messages[i].value == 75); }
      if (strcmp(messages[i].kind, "signon") == 0) { signons++; CHECK(messages[i].stage == signons); }
    }
    CHECK(names == 1 && stats == 1 && signons == 3);
    free_messages(messages, count);
    qa_netquake_message travel;
    memset(&travel, 0, sizeof travel);
    char *e1m1[] = {(char *)"maps/e1m1.bsp"};
    travel.kind = "server-info"; travel.protocol = protocols[p]; travel.max_clients = 1;
    travel.level = (char *)"e1m1"; travel.models = e1m1; travel.model_count = 1;
    qa_netquake_recording_state_observe(state, &travel, 1);
    count = seed_messages(state, protocols[p], &decoder, &messages);
    names = 0;
    for (size_t i = 0; i < count; i++) if (strcmp(messages[i].kind, "name") == 0) names++;
    CHECK(names == 0);
    free_messages(messages, count);
    qa_netquake_decoder_destroy(&decoder);
    qa_netquake_recording_state_free(state);
  }
}
static void test_quakeworld_recording(void) {
  qa_quakeworld_message data;
  memset(&data, 0, sizeof data);
  data.kind = "server-data"; data.protocol = qa_quakeworld_profile_make(28, 0); data.server_count = 7;
  data.game_directory = (char *)"id1"; data.player_slot = 3; data.spectator = true; data.level_name = (char *)"start";
  qa_qw_move_variables mv = {800, 100, 320, 500, 10, 0.7, 10, 4, 4, 1};
  data.move_variables = mv;
  qa_quakeworld_recording_state *state = qa_quakeworld_recording_state_create();
  qa_quakeworld_message first[3];
  memset(first, 0, sizeof first);
  first[0] = data;
  first[1].kind = "userinfo"; first[1].slot = 3; first[1].user_id = 6; first[1].str_value = (char *)"\\name\\old";
  first[2].kind = "set-info"; first[2].slot = 3; first[2].key = (char *)"name"; first[2].str_value = (char *)"stale";
  qa_quakeworld_recording_state_observe(state, first, 3);
  qa_quakeworld_message update;
  memset(&update, 0, sizeof update);
  update.kind = "userinfo"; update.slot = 3; update.user_id = 6; update.str_value = (char *)"\\name\\new";
  qa_quakeworld_recording_state_observe(state, &update, 1);
  const char *models[] = {"maps/start.bsp", "progs/player.mdl"};
  size_t record_count = 0;
  qa_quakeworld_demo_record *records = qa_quakeworld_recording_state_seed(state, &data, models, 2, NULL, 0, 4, 20, 40, &record_count);
  qa_quakeworld_decoder decoder;
  qa_quakeworld_decoder_init(&decoder, qa_quakeworld_profile_make(28, 0));
  bool first_seen = false, set_info = false;
  int model_lists = 0;
  for (size_t i = 0; i < record_count; i++) {
    if (strcmp(records[i].kind, "packet") != 0) continue;
    int32_t sequence = (int32_t)((uint32_t)records[i].message[0] | ((uint32_t)records[i].message[1] << 8)
        | ((uint32_t)records[i].message[2] << 16) | ((uint32_t)records[i].message[3] << 24));
    size_t n = 0;
    qa_quakeworld_message *messages = qa_quakeworld_decoder_decode(&decoder, records[i].message + 8, records[i].length - 8, sequence, &n);
    for (size_t k = 0; k < n; k++) {
      if (!first_seen) { first_seen = true; CHECK(strcmp(messages[k].kind, "server-data") == 0 && messages[k].spectator && messages[k].player_slot == 3); }
      if (strcmp(messages[k].kind, "set-info") == 0) set_info = true;
      if (strcmp(messages[k].kind, "model-list") == 0) {
        model_lists++;
        if (model_lists == 1) CHECK(messages[k].first == 0 && messages[k].name_count == 1 && strcmp(messages[k].names[0], "maps/start.bsp") == 0 && messages[k].next == 1);
        if (model_lists == 2) CHECK(messages[k].first == 1 && messages[k].name_count == 1 && strcmp(messages[k].names[0], "progs/player.mdl") == 0 && messages[k].next == 0);
      }
      qa_quakeworld_message_destroy(&messages[k]);
    }
    free(messages);
  }
  CHECK(first_seen && model_lists == 2 && !set_info);
  CHECK(record_count > 0 && strcmp(records[record_count - 1].kind, "sequences") == 0
      && records[record_count - 1].seconds == 4 && records[record_count - 1].outgoing == 20 && records[record_count - 1].incoming == 40);
  for (size_t i = 0; i < record_count; i++) qa_quakeworld_demo_record_destroy(&records[i]);
  free(records);
  qa_quakeworld_decoder_destroy(&decoder);
  qa_quakeworld_recording_state_free(state);
}

/* ---- protocol-host.test.ts: version-only records retain RMQ flags ---- */
static bool particle_equal(const qa_netquake_message *m) {
  return strcmp(m->kind, "particle") == 0 && m->origin.x == 5000.0625 && m->origin.y == -9000.125 && m->origin.z == 12.875
      && m->direction.x == 0 && m->direction.y == 0 && m->direction.z == 0 && m->count == 1 && m->color_value == 5;
}
static size_t decode_buf(qa_netquake_decoder *decoder, qa_size_buf *sb, qa_netquake_message **out) {
  size_t count = 0;
  *out = qa_netquake_decoder_decode(decoder, sb->data, (size_t)sb->cursize, &count);
  return count;
}
static void test_version_records_keep_flags(void) {
  qa_q1_protocol_identity profile = qa_default_netquake_profile(999);
  qa_netquake_decoder decoder;
  qa_netquake_decoder_init(&decoder, profile, "known-retail", true);
  qa_netquake_message particle, version;
  memset(&particle, 0, sizeof particle);
  particle.kind = "particle"; particle.origin.x = 5000.0625; particle.origin.y = -9000.125; particle.origin.z = 12.875;
  particle.count = 1; particle.color_value = 5;
  memset(&version, 0, sizeof version);
  version.kind = "version"; version.version = 666;
  qa_size_buf sb;
  qa_size_buf_init(&sb, 128, false);
  qa_write_netquake_message(&sb, profile, &version, "known-retail", true);
  qa_write_netquake_message(&sb, profile, &particle, "known-retail", true);
  qa_netquake_message *messages = NULL;
  size_t count = decode_buf(&decoder, &sb, &messages);
  CHECK(count == 2 && particle_equal(&messages[1]));
  CHECK(decoder.protocol.version == 666 && qa_netquake_decoder_get_flags(&decoder) == 130);
  free_messages(messages, count);
  qa_size_buf_clear(&sb);
  qa_write_netquake_message(&sb, profile, &particle, "known-retail", true);
  count = decode_buf(&decoder, &sb, &messages);
  CHECK(count == 1 && particle_equal(&messages[0]));
  free_messages(messages, count);
  qa_size_buf_clear(&sb);
  version.version = 999;
  qa_write_netquake_message(&sb, profile, &version, "known-retail", true);
  count = decode_buf(&decoder, &sb, &messages);
  free_messages(messages, count);
  CHECK(decoder.protocol.version == 999 && decoder.protocol.flags == profile.flags && strcmp(decoder.protocol.kind, "q1-rmq") == 0);
  qa_size_buf_clear(&sb);
  qa_q1_protocol_identity fitz = qa_netquake_profile(666, 0);
  qa_netquake_message info;
  memset(&info, 0, sizeof info);
  info.kind = "server-info"; info.protocol = fitz; info.max_clients = 1; info.level = (char *)"reset";
  qa_write_netquake_message(&sb, fitz, &info, "known-retail", true);
  count = decode_buf(&decoder, &sb, &messages);
  free_messages(messages, count);
  CHECK(qa_netquake_decoder_get_flags(&decoder) == 0);
  qa_size_buf_destroy(&sb);
  qa_netquake_decoder_destroy(&decoder);
}

/* ---- qw-downloads.test.ts (temp data under build/agents/network2-q1/tmp, removed after) ---- */
typedef struct dl_fixture {
  char root[256], game[300], skins[300];
  char commands[16][128]; int command_count;
  double noskins; bool recording, playback;
} dl_fixture;
static bool dl_exists(void *ctx, const char *path, qa_app_qw_download_category category) {
  dl_fixture *f = ctx;
  char full[700];
  snprintf(full, sizeof full, "%s/%s", category == QA_APP_QW_DL_SKIN ? f->skins : f->game, path);
  struct stat st;
  return stat(full, &st) == 0;
}
static void dl_send(void *ctx, const char *text) {
  dl_fixture *f = ctx;
  if (f->command_count < 16) snprintf(f->commands[f->command_count++], 128, "%s", text);
}
static void dl_print(void *ctx, const char *text) { (void)text; CHECK(ctx != NULL); }
static double dl_noskins(void *ctx) { return ((dl_fixture *)ctx)->noskins; }
static bool dl_recording(void *ctx) { return ((dl_fixture *)ctx)->recording; }
static bool dl_playback(void *ctx) { return ((dl_fixture *)ctx)->playback; }
static int remove_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw) {
  (void)st; (void)flag; (void)ftw;
  return remove(path);
}
static qa_app_qw_download_receiver *dl_open(dl_fixture *f) {
  memset(f, 0, sizeof *f);
  mkdir("build/agents/network2-q1/tmp", 0700);
  snprintf(f->root, sizeof f->root, "build/agents/network2-q1/tmp/qw-download-XXXXXX");
  CHECK(mkdtemp(f->root) != NULL);
  snprintf(f->game, sizeof f->game, "%s/game", f->root);
  snprintf(f->skins, sizeof f->skins, "%s/qw", f->root);
  mkdir(f->game, 0700); mkdir(f->skins, 0700);
  qa_app_qw_download_options options;
  memset(&options, 0, sizeof options);
  options.ctx = f; options.game_root = f->game; options.skin_root = f->skins;
  options.exists = dl_exists; options.send_command = dl_send; options.print = dl_print;
  options.noskins = dl_noskins; options.demo_recording = dl_recording; options.demo_playback = dl_playback;
  options.has_maximum_bytes = true; options.maximum_bytes = 10;
  return qa_app_qw_download_receiver_create(options);
}
static void dl_close(dl_fixture *f, qa_app_qw_download_receiver *r) {
  qa_app_qw_download_receiver_destroy(r);
  nftw(f->root, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
  rmdir("build/agents/network2-q1/tmp");
}
static size_t read_file(const char *path, uint8_t *out, size_t cap) {
  FILE *file = fopen(path, "rb");
  if (!file) return (size_t)-1;
  size_t n = fread(out, 1, cap, file);
  fclose(file);
  return n;
}
static size_t dir_count(const char *path) {
  DIR *dir = opendir(path);
  if (!dir) return 0;
  size_t n = 0;
  struct dirent *e;
  while ((e = readdir(dir))) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n++;
  closedir(dir);
  return n;
}
static qa_app_qw_download_result block(double percent, const uint8_t *bytes, size_t length) {
  qa_app_qw_download_result r = {false, percent, bytes, length};
  return r;
}
static const char *last_command(dl_fixture *f) { return f->command_count ? f->commands[f->command_count - 1] : ""; }
static void test_qw_download_blocks(void) {
  dl_fixture f;
  qa_app_qw_download_receiver *r = dl_open(&f);
  const struct { const char *path; qa_app_qw_download_category category; } entries[] = {
    {"sound/a.wav", QA_APP_QW_DL_SOUND}, {"maps/a.bsp", QA_APP_QW_DL_MODEL},
    {"progs/a.mdl", QA_APP_QW_DL_MODEL}, {"skins/a.pcx", QA_APP_QW_DL_SKIN}};
  static const uint8_t first[] = {1, 2}, second[] = {3};
  for (int i = 0; i < 4; i++) {
    CHECK(qa_app_qw_download_receiver_request(r, entries[i].path, entries[i].category) == QA_APP_QW_DL_WAITING);
    char expected[64];
    snprintf(expected, sizeof expected, "download %s", entries[i].path);
    CHECK(strcmp(last_command(&f), expected) == 0);
    CHECK(qa_app_qw_download_receiver_receive(r, block(50, first, 2)) == QA_APP_QW_DL_RX_WAITING);
    CHECK(strcmp(last_command(&f), "nextdl") == 0);
    char destination[700];
    snprintf(destination, sizeof destination, "%s/%s", entries[i].category == QA_APP_QW_DL_SKIN ? f.skins : f.game, entries[i].path);
    struct stat st;
    CHECK(stat(destination, &st) != 0);
    CHECK(qa_app_qw_download_receiver_receive(r, block(100, second, 1)) == QA_APP_QW_DL_RX_COMPLETE);
    uint8_t bytes[8];
    CHECK(read_file(destination, bytes, sizeof bytes) == 3 && bytes[0] == 1 && bytes[1] == 2 && bytes[2] == 3);
    CHECK(qa_app_qw_download_receiver_request(r, entries[i].path, entries[i].category) == QA_APP_QW_DL_AVAILABLE);
  }
  dl_close(&f, r);
}
static void test_qw_download_policy(void) {
  dl_fixture f;
  qa_app_qw_download_receiver *r = dl_open(&f);
  for (int value = 1; value <= 2; value++) {
    f.noskins = value;
    CHECK(qa_app_qw_download_receiver_request(r, "skins/new.pcx", QA_APP_QW_DL_SKIN) == QA_APP_QW_DL_SKIPPED);
  }
  char path[700];
  snprintf(path, sizeof path, "%s/skins", f.skins); mkdir(path, 0700);
  snprintf(path, sizeof path, "%s/skins/old.pcx", f.skins);
  FILE *file = fopen(path, "wb"); fputc(1, file); fclose(file);
  CHECK(qa_app_qw_download_receiver_request(r, "skins/old.pcx", QA_APP_QW_DL_SKIN) == QA_APP_QW_DL_AVAILABLE);
  f.recording = true;
  CHECK(qa_app_qw_download_receiver_request(r, "maps/a.bsp", QA_APP_QW_DL_MODEL) == QA_APP_QW_DL_SKIPPED);
  f.recording = false; f.playback = true;
  CHECK(qa_app_qw_download_receiver_request(r, "sound/a.wav", QA_APP_QW_DL_SOUND) == QA_APP_QW_DL_SKIPPED);
  static const uint8_t one[] = {1};
  CHECK(qa_app_qw_download_receiver_receive(r, block(100, one, 1)) == QA_APP_QW_DL_RX_WAITING);
  CHECK(f.command_count == 0);
  dl_close(&f, r);
}
typedef struct request_call { qa_app_qw_download_receiver *r; const char *path; qa_app_qw_download_category category; qa_app_qw_download_result result; } request_call;
static void request_body(void *ctx) { request_call *c = ctx; qa_app_qw_download_receiver_request(c->r, c->path, c->category); }
static void receive_body(void *ctx) { request_call *c = ctx; qa_app_qw_download_receiver_receive(c->r, c->result); }
static void test_qw_download_denials(void) {
  dl_fixture f;
  qa_app_qw_download_receiver *r = dl_open(&f);
  const struct { const char *path, *message; } invalid[] = {
    {"../maps/a.bsp", "File path must be relative"}, {"/maps/a.bsp", "File path must be relative"},
    {"maps/a.bsp;quit", "Invalid QuakeWorld download path"}, {"skins/a.pcx", "Invalid QuakeWorld download path"}};
  for (int i = 0; i < 4; i++) {
    request_call call = {r, invalid[i].path, QA_APP_QW_DL_MODEL, {0}};
    qa_caught_error caught;
    CHECK(!qa_try(request_body, &call, &caught) && strstr(caught.message, invalid[i].message) != NULL);
  }
  static const uint8_t one[] = {1}, two[] = {2}, eleven[11] = {0};
  CHECK(qa_app_qw_download_receiver_request(r, "maps/a.bsp", QA_APP_QW_DL_MODEL) == QA_APP_QW_DL_WAITING);
  qa_app_qw_download_result missing = {true, 0, NULL, 0};
  CHECK(qa_app_qw_download_receiver_receive(r, missing) == QA_APP_QW_DL_RX_MISSING);
  qa_app_qw_download_receiver_request(r, "maps/a.bsp", QA_APP_QW_DL_MODEL);
  qa_app_qw_download_receiver_receive(r, block(20, one, 1));
  qa_app_qw_download_receiver_close(r);
  char maps[700];
  snprintf(maps, sizeof maps, "%s/maps", f.game);
  CHECK(dir_count(maps) == 0);
  qa_app_qw_download_receiver_request(r, "maps/a.bsp", QA_APP_QW_DL_MODEL);
  qa_app_qw_download_receiver_receive(r, block(20, one, 1));
  char target[760];
  snprintf(target, sizeof target, "%s/a.bsp", maps);
  FILE *file = fopen(target, "wb"); fputc(9, file); fclose(file);
  CHECK(qa_app_qw_download_receiver_receive(r, block(100, two, 1)) == QA_APP_QW_DL_RX_MISSING);
  uint8_t bytes[4];
  CHECK(read_file(target, bytes, sizeof bytes) == 1 && bytes[0] == 9);
  CHECK(dir_count(maps) == 1);
  qa_app_qw_download_receiver_request(r, "maps/b.bsp", QA_APP_QW_DL_MODEL);
  CHECK(qa_app_qw_download_receiver_receive(r, block(100, eleven, 11)) == QA_APP_QW_DL_RX_MISSING);
  qa_app_qw_download_receiver_request(r, "maps/c.bsp", QA_APP_QW_DL_MODEL);
  request_call call = {r, NULL, QA_APP_QW_DL_MODEL, block(101, one, 1)};
  qa_caught_error caught;
  CHECK(!qa_try(receive_body, &call, &caught) && strcmp(caught.message, "Invalid QuakeWorld download block") == 0);
  CHECK(dir_count(maps) == 1);
  dl_close(&f, r);
}
static void test_qw_download_progress(void) {
  dl_fixture f;
  qa_app_qw_download_receiver *r = dl_open(&f);
  static const uint8_t three[] = {1, 2, 3};
  CHECK(qa_app_qw_download_receiver_request(r, "maps/a.bsp", QA_APP_QW_DL_MODEL) == QA_APP_QW_DL_WAITING);
  qa_app_qw_download_receiver_receive(r, block(40, three, 3));
  qa_app_client_download_progress progress;
  CHECK(qa_app_qw_download_receiver_progress(r, &progress) == 1);
  CHECK(strcmp(progress.path, "maps/a.bsp") == 0 && progress.received == 3 && progress.percent == 40 && !progress.has_total);
  dl_close(&f, r);
}
static void test_qw_download_cancel_retry(void) {
  dl_fixture f;
  qa_app_qw_download_receiver *r = dl_open(&f);
  static const uint8_t one[] = {1}, two[] = {2}, pair[] = {3, 4};
  qa_app_qw_download_receiver_request(r, "maps/a.bsp", QA_APP_QW_DL_MODEL);
  qa_app_qw_download_receiver_receive(r, block(40, one, 1));
  qa_app_qw_download_receiver_cancel(r);
  qa_app_qw_download_receiver_cancel(r);
  CHECK(qa_app_qw_download_receiver_retry(r) == QA_APP_QW_DL_WAITING);
  CHECK(f.command_count == 2 && strcmp(f.commands[0], "download maps/a.bsp") == 0 && strcmp(f.commands[1], "nextdl") == 0);
  CHECK(qa_app_qw_download_receiver_receive(r, block(100, two, 1)) == QA_APP_QW_DL_RX_WAITING);
  CHECK(strcmp(last_command(&f), "download maps/a.bsp") == 0);
  qa_app_qw_download_receiver_receive(r, block(100, pair, 2));
  char target[700];
  snprintf(target, sizeof target, "%s/maps/a.bsp", f.game);
  uint8_t bytes[4];
  CHECK(read_file(target, bytes, sizeof bytes) == 2 && bytes[0] == 3 && bytes[1] == 4);
  dl_close(&f, r);
}

int main(void) {
  test_qw_download_blocks();
  test_qw_download_policy();
  test_qw_download_denials();
  test_qw_download_progress();
  test_qw_download_cancel_retry();
  test_version_records_keep_flags();
  test_netquake_recording();
  test_quakeworld_recording();
  test_malformed_demo_propagates();
  test_skin_crop_and_pad();
  test_skin_selection();
  test_skin_malformed();
  test_skin_odd_width();
  test_truncated_demo();
  printf("q1_app_units: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
