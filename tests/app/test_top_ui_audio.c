/* C checks for src/app/bootstrap/audio.ts (ApplicationAudio) and
 * audio/q3.ts: tests/audio/q3-custom-sound.test.ts, the logic of
 * tests/audio/bootstrap.test.ts over a synthetic loose-content fixture,
 * and printed text taken from running the TS module under bun on the same
 * fixture (scratchpad oracle.ts). The ui section reproduces ApplicationSeatUi
 * behavior observed by running the TS class under bun on the same stub inputs
 * (scratchpad oracle/ui_oracle.ts); the TS binding-actions and
 * binding-lifecycle tests need a whole Application.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "qa/app/bootstrap/audio.h"
#include "qa/app/bootstrap/input.h"
#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/app/bootstrap/ui.h"
#include "qa/content/composition/q1/types.h"
#include "qa/text/atlas.h"
#include "qa/app/bootstrap/audio/q3.h"
#include "qa/app/bootstrap/content.h"
#include "qa/audio/engine.h"
#include "qa/content/catalog/index.h"
#include "qa/content/catalog/products.h"
#include "qa/content/mounts.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/core/cvars/index.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"

static int checks, failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)
static bool text_eq(const char *a, const char *b) {
  return (a == NULL && b == NULL) || (a != NULL && b != NULL && strcmp(a, b) == 0);
}

/* ---- audio ---- */

/* Sentinel contexts: every callback checks it receives its own. */
static int print_sentinel, content_sentinel, now_sentinel, scene_sentinel, haptics_sentinel, active_sentinel, team_sentinel;
static int sentinel_failures;
#define SENTINEL(ctx, expected) do { if ((ctx) != (void *)&(expected)) sentinel_failures++; } while (0)

/* Captured print output. */
static char printed[64][1024];
static size_t printed_count;
static void capture_print(void *ctx, const char *text) {
  SENTINEL(ctx, print_sentinel);
  if (printed_count < 64) snprintf(printed[printed_count++], sizeof printed[0], "%s", text);
}
static void clear_printed(void) { printed_count = 0; }
static bool printed_has(const char *text) {
  for (size_t i = 0; i < printed_count; i++) if (strcmp(printed[i], text) == 0) return true;
  return false;
}
static bool printed_contains(const char *part) {
  for (size_t i = 0; i < printed_count; i++) if (strstr(printed[i], part) != NULL) return true;
  return false;
}
static double fixed_now(void *ctx) { SENTINEL(ctx, now_sentinel); return 1000; }

/* Fixture files. */
static char fixture_root[256];
static void mkdir_p(const char *path) {
  char buf[512];
  snprintf(buf, sizeof buf, "%s", path);
  for (char *p = buf + 1; *p; p++) if (*p == '/') { *p = '\0'; mkdir(buf, 0777); *p = '/'; }
  mkdir(buf, 0777);
}
static void write_bytes(const char *relative, const uint8_t *bytes, size_t length) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s", fixture_root, relative);
  char dir[512]; snprintf(dir, sizeof dir, "%s", path);
  char *slash = strrchr(dir, '/'); if (slash) { *slash = '\0'; mkdir_p(dir); }
  FILE *file = fopen(path, "wb");
  if (file == NULL) { perror(path); exit(1); }
  fwrite(bytes, 1, length, file); fclose(file);
}
static void put32(uint8_t *b, size_t o, uint32_t v) { b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); b[o + 2] = (uint8_t)(v >> 16); b[o + 3] = (uint8_t)(v >> 24); }
static void put16(uint8_t *b, size_t o, uint16_t v) { b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); }
/* Same bytes as oracle.ts wav(frames, loop). */
static void write_wav(const char *relative, uint32_t frames, bool loop) {
  uint32_t data = frames * 2, cue = loop ? 8 + 4 + 24 : 0;
  size_t length = 44 + data + cue;
  uint8_t *b = calloc(1, length);
  memcpy(b, "RIFF", 4); put32(b, 4, 36 + data + cue); memcpy(b + 8, "WAVEfmt ", 8); put32(b, 16, 16);
  put16(b, 20, 1); put16(b, 22, 1); put32(b, 24, 22050); put32(b, 28, 44100); put16(b, 32, 2); put16(b, 34, 16);
  memcpy(b + 36, "data", 4); put32(b, 40, data);
  for (uint32_t i = 0; i < frames; i++) put16(b, 44 + i * 2, (uint16_t)(int16_t)((int)((i * 37) % 2000) - 1000));
  if (loop) { size_t o = 44 + data; memcpy(b + o, "cue ", 4); put32(b, o + 4, 28); put32(b, o + 8, 1); put32(b, o + 32, 0); }
  write_bytes(relative, b, length);
  free(b);
}
static void write_text_file(const char *relative, const char *text) { write_bytes(relative, (const uint8_t *)text, strlen(text)); }
static void build_fixture(void) {
  snprintf(fixture_root, sizeof fixture_root, "/tmp/qa-top-audio-XXXXXX");
  const char *tmp = getenv("TMPDIR");
  if (tmp != NULL) snprintf(fixture_root, sizeof fixture_root, "%s/qa-top-audio-XXXXXX", tmp);
  if (mkdtemp(fixture_root) == NULL) { perror("mkdtemp"); exit(1); }
  write_wav("q2/sound/weapons/rg_hum.wav", 4096, true);
  write_wav("q2/sound/player/male/pain25_1.wav", 2048, false);
  write_wav("q2/sound/misc/talk.wav", 2048, false);
  write_wav("q2/sound/misc/menu1.wav", 2048, false);
  write_wav("q2/sound/soldier/solatck2.wav", 2048, false);
  write_wav("q2/sound/weapons/railgf1a.wav", 1000, false);
  write_wav("q3/sound/player/sarge/pain25_1.wav", 2048, false);
  write_text_file("q3/models/players/sarge/animation.cfg", "footsteps boot\n");
  write_text_file("q3/sound/default.environments", "{}");
  write_wav("q1/sound/ambience/drip1.wav", 2048, true);
  const char *kinds[] = {"boot", "clank", "splash"};
  for (int k = 0; k < 3; k++) for (int i = 1; i <= 4; i++) {
    char path[128]; snprintf(path, sizeof path, "q3/sound/player/footsteps/%s%d.wav", kinds[k], i);
    write_wav(path, 512, false);
  }
}
static void remove_fixture(void) {
  char command[512];
  snprintf(command, sizeof command, "rm -rf '%s'", fixture_root);
  if (system(command) != 0) fprintf(stderr, "could not remove %s\n", fixture_root);
}

/* Catalog of the three products, ids equal to their expectation ids. */
static const char *const product_ids[3] = {"q1-classic-id1", "q2-classic-baseq2", "q3-baseq3"};
static const char *const product_dirs[3] = {"q1", "q2", "q3"};
static qa_catalog_discovered_product products[3];
static qa_content_catalog catalog;
static qa_content_mounted *mounted[3];
static char *mount_ids[3], *plan_ids[3], *roots[3];
static qa_content_mount plan_mounts[3];
static qa_mount_id plan_orders[3];
static void build_catalog(void) {
  memset(products, 0, sizeof products);
  for (int i = 0; i < 3; i++) {
    for (size_t e = 0; e < qa_expected_product_count; e++)
      if (strcmp(qa_expected_products[e].id, product_ids[i]) == 0) products[i].expectation = qa_expected_products[e];
    products[i].id = (char *)product_ids[i];
    products[i].availability.kind = QA_CATALOG_INSTALLED;
  }
  qa_content_catalog_init(&catalog, "/unused", products, 3, NULL, 0, 0, NULL, NULL, 0);
  for (int i = 0; i < 3; i++) {
    mount_ids[i] = qa_create_mount_id("oracle", product_dirs[i]);
    plan_ids[i] = qa_create_mount_plan_id("oracle", product_dirs[i]);
    roots[i] = malloc(512); snprintf(roots[i], 512, "%s/%s", fixture_root, product_dirs[i]);
    memset(&plan_mounts[i], 0, sizeof plan_mounts[i]);
    plan_mounts[i].kind = QA_CONTENT_MOUNT_LOOSE;
    plan_mounts[i].as.loose.identity = qa_create_mount_identity(mount_ids[i], product_ids[i], 0);
    plan_mounts[i].as.loose.root_path = roots[i];
    plan_orders[i] = mount_ids[i];
    qa_resolved_mount_plan plan;
    memset(&plan, 0, sizeof plan);
    plan.id = plan_ids[i]; plan.mounts = &plan_mounts[i]; plan.mount_count = 1;
    plan.default_order = &plan_orders[i]; plan.default_order_count = 1;
    qa_open_mount_options options; memset(&options, 0, sizeof options);
    mounted[i] = qa_open_mount_plan(&plan, &options);
    if (mounted[i] == NULL) { fprintf(stderr, "mount failed\n"); exit(1); }
  }
}
static void free_catalog(void) {
  for (int i = 0; i < 3; i++) { qa_content_mounted_destroy(mounted[i]); free(mount_ids[i]); free(plan_ids[i]); free(roots[i]); }
  qa_content_catalog_destroy(&catalog);
}

/* Loaded content through the host-shaped loader. */
static const char *host_open(void *ctx, qa_app_loaded_content *content) {
  SENTINEL(ctx, content_sentinel);
  qa_app_loaded_content_set_catalog(content, &catalog);
  return NULL;
}
static const char *host_for_content(void *ctx, qa_app_loaded_content *content, qa_content_id id, const void **out) {
  SENTINEL(ctx, content_sentinel);
  (void)content;
  for (int i = 0; i < 3; i++) if (strcmp(id, product_ids[i]) == 0) { *out = mounted[i]; return NULL; }
  return "no mounts";
}
static qa_provider_timing timing_rows[1];
static qa_executable_recipe make_recipe(const char *audio_content, const char *character_content,
    qa_environment_selection_kind environment, qa_resource_request selected) {
  qa_executable_recipe recipe;
  memset(&recipe, 0, sizeof recipe);
  recipe.character.definition.content = character_content; recipe.character.definition.provider = "p:char";
  recipe.presentation.audio.content = audio_content; recipe.presentation.audio.provider = "p:audio";
  recipe.presentation.environment.kind = environment;
  recipe.presentation.environment.resource = selected;
  memset(timing_rows, 0, sizeof timing_rows);
  timing_rows[0].provider = "p:engine";
  recipe.timing = timing_rows; recipe.timing_count = 1;
  recipe.engine_behavior.provider = "p:engine"; recipe.engine_behavior.content = product_ids[1];
  recipe.map.entities.content = product_ids[1];
  return recipe;
}
static qa_app_loaded_content *load_content(qa_executable_recipe recipe) {
  qa_app_content_host host;
  memset(&host, 0, sizeof host);
  host.ctx = &content_sentinel; host.open_loaded = host_open; host.for_content = host_for_content;
  qa_app_options options;
  memset(&options, 0, sizeof options);
  return qa_app_load_application_content(host, options, &recipe, true);
}
static qa_app_application_audio *make_audio(qa_app_loaded_content *content, bool defer) {
  qa_app_audio_options options;
  memset(&options, 0, sizeof options);
  options.defer_output = defer;
  return qa_app_application_audio_create(content, fixed_now, &now_sentinel, 1, "missing-custom-model",
                                         capture_print, &print_sentinel, &options);
}

/* qa_try closures. */
typedef struct call { void (*run)(struct call *); qa_app_application_audio *audio; const void *arg; } call;
static void run_call(void *ctx) { call *c = ctx; c->run(c); }
static bool raises(call c, const char *message) {
  qa_caught_error caught;
  if (qa_try(run_call, &c, &caught)) return false;
  if (message != NULL && strcmp(caught.message, message) != 0) { fprintf(stderr, "  raised: %s\n", caught.message); return false; }
  return true;
}
static bool command(qa_app_application_audio *audio, const char *name, const char *const *args, size_t count) {
  qa_app_audio_command_request request;
  memset(&request, 0, sizeof request);
  request.name = name; request.args = args; request.arg_count = count;
  return qa_app_application_audio_command(audio, &request);
}
static void run_play_none(call *c) { command(c->audio, "play", NULL, 0); }
static void run_play_x(call *c) { static const char *const a[] = {"x"}; command(c->audio, "play", a, 1); }
static void run_play_missing(call *c) { static const char *const a[] = {"missing/audio-diagnostic-file"}; command(c->audio, "play", a, 1); }
static void run_s_info(call *c) { command(c->audio, "s_info", NULL, 0); }
static void run_reset(call *c) { qa_app_application_audio_reset_round(c->audio); }

static bool mix_nonzero(qa_app_application_audio *audio, double frames) {
  size_t count = (size_t)frames * 2;
  int16_t *out = calloc(count, sizeof *out);
  qa_unified_audio_mix(qa_app_application_audio_engine(audio), frames, out);
  bool any = false;
  for (size_t i = 0; i < count; i++) if (out[i] != 0) any = true;
  free(out);
  return any;
}
static int16_t *mix_copy(qa_app_application_audio *audio, double frames) {
  int16_t *out = calloc((size_t)frames * 2, sizeof *out);
  qa_unified_audio_mix(qa_app_application_audio_engine(audio), frames, out);
  return out;
}

static qa_identity_owner identity;
static qa_actor_id actor;
static qa_seat_id seat;
static qa_audio_listener listener;
static qa_world_snapshot snapshot_with(bool actors) {
  static qa_actor_observation observation;
  static qa_body_snapshot body;
  memset(&observation, 0, sizeof observation); observation.id = actor;
  memset(&body, 0, sizeof body); body.actor = actor;
  qa_world_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.frame.frame = 1; snapshot.frame.elapsed.kind = QA_SOURCE_TIME_MILLISECONDS; snapshot.frame.elapsed.value = 100;
  if (actors) { snapshot.actors = &observation; snapshot.actor_count = 1; snapshot.bodies = &body; snapshot.body_count = 1; }
  return snapshot;
}
static qa_app_sim_presentation_event event_of(const char *kind, const char *event_kind, const char *content, double sequence, const void *payload) {
  qa_app_sim_presentation_event event;
  memset(&event, 0, sizeof event);
  event.kind = kind; event.content = content; event.sequence = sequence;
  /* The union member named after kind; the payload carries its own nested kind. */
  if (strcmp(kind, "q1") == 0) event.q1 = payload;
  else if (strcmp(kind, "q2") == 0) event.q2 = payload;
  else if (strcmp(kind, "q2-player") == 0) event.q2_player = payload;
  else if (strcmp(kind, "q3-character") == 0) event.q3_character = *(const qa_app_sim_q3_character_presentation_event *)payload;
  else if (strcmp(kind, "presentation-owner") == 0) event.presentation_owner.kind = event_kind;
  else abort();
  return event;
}
static qa_audio_audience world(void) { qa_audio_audience a; memset(&a, 0, sizeof a); return a; }

/* tests/audio/q3-custom-sound.test.ts */
static void test_q3_voice_fallback(void) {
  size_t count = qa_expected_product_count;
  qa_catalog_discovered_product *all = calloc(count + 1, sizeof *all);
  char **ids = calloc(count + 1, sizeof *ids);
  size_t team = count;
  for (size_t i = 0; i < count; i++) {
    qa_content_identity identity_row = {qa_expected_products[i].family, qa_expected_products[i].edition, qa_expected_products[i].campaign, "fixture"};
    ids[i] = qa_create_content_id(identity_row);
    all[i].id = ids[i]; all[i].expectation = qa_expected_products[i]; all[i].availability.kind = QA_CATALOG_INSTALLED;
    if (strcmp(qa_expected_products[i].id, "q3-missionpack") == 0) team = i;
  }
  CHECK(team < count);
  qa_content_identity mod_identity = {QA_GAME_FAMILY_Q3, "classic", "ta-voice-mod", "fixture"};
  ids[count] = qa_create_content_id(mod_identity);
  all[count] = all[team];
  all[count].id = ids[count];
  all[count].expectation.id = "ta-voice-mod"; all[count].expectation.campaign = "custom-team-game";
  all[count].expectation.base_product = "q3-missionpack";
  qa_content_catalog fixture;
  qa_content_catalog_init(&fixture, "/unused", all, count + 1, NULL, 0, 0, NULL, NULL, 0);
  const char *worlds[] = {"q1-classic-id1", "q2-classic-baseq2", "q3-baseq3"};
  for (int w = 0; w < 3; w++) {
    CHECK(text_eq(qa_content_catalog_product(&fixture, worlds[w])->expectation.id, worlds[w]));
    CHECK(text_eq(qa_app_q3_voice_fallback(&fixture, all[team].id, true), "james"));
    CHECK(text_eq(qa_app_q3_voice_fallback(&fixture, all[team].id, false), "sarge"));
    CHECK(text_eq(qa_app_q3_voice_fallback(&fixture, all[count].id, true), "james"));
    CHECK(text_eq(qa_app_q3_voice_fallback(&fixture, qa_content_catalog_product(&fixture, "q3-baseq3")->id, true), "sarge"));
  }
  qa_content_catalog_destroy(&fixture);
  for (size_t i = 0; i <= count; i++) free(ids[i]);
  free(ids); free(all);
}

/* Command text; expected strings from the TS oracle on the same fixture. */
static void test_commands(void) {
  qa_resource_request none = {NULL, NULL};
  qa_app_loaded_content *content = load_content(make_recipe(product_ids[1], product_ids[1], QA_ENVIRONMENT_AUDIO_CONTENT, none));
  qa_app_application_audio *audio = make_audio(content, true);
  clear_printed();
  CHECK(command(audio, "soundinfo", NULL, 0));
  CHECK(printed_count == 2);
  CHECK(text_eq(printed[0], "Sound output: detached\n"));
  CHECK(text_eq(printed[1], "Effects volume 0.7; music volume 0.25; listeners 0\n"));
  clear_printed();
  qa_app_application_audio_preload_sound(audio, product_ids[1], "sound/weapons/railgf1a.wav", NULL);
  qa_app_application_audio_preload_sound(audio, product_ids[1], "missing/thing.wav", NULL);
  CHECK(command(audio, "soundlist", NULL, 0));
  CHECK(printed_count == 4);
  CHECK(text_eq(printed[0], "Sound unavailable: q2-classic-baseq2/missing/thing.wav\n"));
  CHECK(text_eq(printed[1], "unavailable: q2-classic-baseq2/q2/missing/thing.wav/\n"));
  CHECK(strncmp(printed[2], "  22050 Hz 1ch 16-bit 1000 frames 2000 bytes : sound/weapons/railgf1a.wav [resource:q2-classic-baseq2:mount:oracle:q2:0:", 117) == 0);
  CHECK(strstr(printed[2], ":mount-plan:oracle:q2:default:0]\n") != NULL);
  CHECK(text_eq(printed[3], "Decoded PCM storage: 2000 bytes in 1 registered resources\n"));
  /* A registration list adds sounds; a null registration reports. */
  clear_printed();
  qa_q3_pres_sound_registration registrations[1] = {{"sound/absent.wav", false, NULL}};
  qa_app_audio_command_request request;
  memset(&request, 0, sizeof request);
  request.name = "s_list"; request.registrations = registrations; request.registration_count = 1;
  request.print = capture_print; request.print_ctx = &print_sentinel;
  CHECK(qa_app_application_audio_command(audio, &request));
  CHECK(printed_has("unavailable: sound/absent.wav\n"));
  CHECK(raises((call){run_play_none, audio, NULL}, "Usage: play <sound> [sound ...]"));
  CHECK(raises((call){run_play_x, audio, NULL}, "Sound playback requires a local listener"));
  CHECK(!command(audio, "not-an-audio-command", NULL, 0));
  qa_app_application_audio_destroy(audio);
  qa_app_loaded_content_destroy(content);
}

/* World audio over a dummy device (bootstrap.test.ts logic). */
static void test_world_audio(void) {
  qa_resource_request none = {NULL, NULL};
  qa_app_loaded_content *content = load_content(make_recipe(product_ids[1], product_ids[1], QA_ENVIRONMENT_AUDIO_CONTENT, none));
  qa_app_application_audio *audio = make_audio(content, false);
  qa_q2_presentation_event loop;
  memset(&loop, 0, sizeof loop);
  loop.kind = "sound"; loop.has_actor = true; loop.actor = actor; loop.path = "weapons/rg_hum.wav";
  loop.channel = 0; loop.volume = 1; loop.attenuation = 1; loop.loop = "start";
  qa_app_sim_presentation_event events[2];
  events[0] = event_of("q2", "sound", product_ids[1], 100, &loop);
  qa_world_snapshot snapshot = snapshot_with(true);
  clear_printed();
  qa_app_application_audio_frame(audio, &snapshot, &listener, 1, events, 1, qa_app_audio_now_milliseconds(), NULL, 0);
  CHECK(qa_unified_audio_output_state_of(qa_app_application_audio_engine(audio)) == QA_UNIFIED_AUDIO_PLAYING);
  int16_t *dry = mix_copy(audio, 1024);
  bool dry_nonzero = false;
  for (size_t i = 0; i < 2048; i++) if (dry[i] != 0) dry_nonzero = true;
  CHECK(dry_nonzero);
  qa_world_snapshot empty = snapshot_with(false);
  qa_app_application_audio_frame(audio, &empty, &listener, 1, NULL, 0, qa_app_audio_now_milliseconds(), NULL, 0);
  CHECK(!mix_nonzero(audio, 1024));
  /* soundinfo with a device and a listener (TS oracle shapes). */
  clear_printed();
  CHECK(command(audio, "soundinfo", NULL, 0));
  CHECK(printed_count == 5);
  CHECK(text_eq(printed[0], "Sound output: playing\n"));
  CHECK(text_eq(printed[1], "SDL device: system default\n"));
  CHECK(strstr(printed[2], " Hz, 2 channels, 16-bit PCM; buffer ") != NULL);
  CHECK(strstr(printed[3], " queued frames, maximum ") != NULL);
  CHECK(text_eq(printed[4], "Effects volume 0.7; music volume 0.25; listeners 1\n"));
  clear_printed();
  CHECK(raises((call){run_play_missing, audio, NULL}, "Sound unavailable: q2-classic-baseq2/missing/audio-diagnostic-file.wav"));
  CHECK(printed_has("Sound unavailable: q2-classic-baseq2/missing/audio-diagnostic-file.wav\n"));
  qa_app_application_audio_destroy(audio);

  qa_app_application_audio *wet = make_audio(content, false);
  qa_q2_player_event view;
  memset(&view, 0, sizeof view);
  view.kind = "view"; view.actor = actor; view.view.underwater = true;
  events[1] = event_of("q2-player", "view", product_ids[1], 100, &view);
  qa_app_application_audio_frame(wet, &snapshot, &listener, 1, events, 2, qa_app_audio_now_milliseconds(), NULL, 0);
  int16_t *wet_mix = mix_copy(wet, 1024);
  bool wet_nonzero = false, differs = false;
  for (size_t i = 0; i < 2048; i++) { if (wet_mix[i] != 0) wet_nonzero = true; if (wet_mix[i] != dry[i]) differs = true; }
  CHECK(wet_nonzero); CHECK(differs);
  free(wet_mix); free(dry);
  qa_unified_audio_stop_all(qa_app_application_audio_engine(wet));
  /* Userinfo selects an unknown model; the sexed sound falls back to male. */
  qa_q2_player_event userinfo;
  memset(&userinfo, 0, sizeof userinfo);
  userinfo.kind = "userinfo"; userinfo.actor = actor; userinfo.name = "player"; userinfo.skin = "missing-model/default";
  qa_q2_presentation_event pain;
  memset(&pain, 0, sizeof pain);
  pain.kind = "sound"; pain.has_actor = true; pain.actor = actor; pain.path = "*pain25_1.wav";
  pain.channel = 2; pain.volume = 1; pain.attenuation = 1; pain.loop = "once";
  qa_app_sim_presentation_event sexed[2] = {event_of("q2-player", "userinfo", product_ids[1], 100, &userinfo),
                                           event_of("q2", "sound", product_ids[1], 100, &pain)};
  qa_app_application_audio_receive(wet, sexed, 2, world(), true);
  CHECK(mix_nonzero(wet, 4096));
  qa_unified_audio_stop_all(qa_app_application_audio_engine(wet));
  /* Q3 character pain: plays, then the 500 ms throttle suppresses. */
  qa_app_sim_q3_character_presentation_event character;
  memset(&character, 0, sizeof character);
  character.actor = actor; character.sequence = 1; character.time_milliseconds = 1000; character.event = QA_Q3_EV_PAIN; character.parameter = 20;
  qa_app_sim_presentation_event q3 = event_of("q3-character", NULL, product_ids[2], 100, &character);
  qa_app_application_audio_receive(wet, &q3, 1, world(), true);
  CHECK(mix_nonzero(wet, 4096));
  qa_unified_audio_stop_all(qa_app_application_audio_engine(wet));
  character.sequence = 2; character.time_milliseconds = 1200;
  qa_app_application_audio_receive(wet, &q3, 1, world(), true);
  CHECK(!mix_nonzero(wet, 128));
  /* Monster muzzle flash 39 is soldier/solatck2.wav; 9999 is unresolved. */
  qa_q2_presentation_event flash;
  memset(&flash, 0, sizeof flash);
  flash.kind = "monster-muzzleflash"; flash.has_actor = true; flash.actor = actor; flash.flash = 39;
  qa_app_sim_presentation_event muzzle = event_of("q2", "monster-muzzleflash", product_ids[1], 100, &flash);
  qa_unified_audio_stop_all(qa_app_application_audio_engine(wet));
  qa_app_application_audio_receive(wet, &muzzle, 1, world(), true);
  CHECK(mix_nonzero(wet, 4096));
  clear_printed();
  flash.flash = 9999;
  qa_app_application_audio_receive(wet, &muzzle, 1, world(), true);
  CHECK(printed_count == 1 && text_eq(printed[0], "Unresolved Quake II monster muzzle sound 9999\n"));
  /* Chat print plays the talk sound to its listener. */
  qa_unified_audio_stop_all(qa_app_application_audio_engine(wet));
  qa_q2_presentation_event print;
  memset(&print, 0, sizeof print);
  print.kind = "print"; print.level = "chat"; print.text = "hi";
  qa_app_sim_presentation_event chat = event_of("q2", "print", product_ids[1], 100, &print);
  qa_app_application_audio_receive(wet, &chat, 1, world(), true);
  CHECK(mix_nonzero(wet, 2048));
  /* A UI sound plays on the next frame (q2 "open" is misc/menu1.wav). */
  qa_unified_audio_stop_all(qa_app_application_audio_engine(wet));
  qa_app_application_audio_ui_sound(wet, "open", seat);
  qa_app_application_audio_frame(wet, &snapshot, &listener, 1, NULL, 0, qa_app_audio_now_milliseconds(), NULL, 0);
  CHECK(mix_nonzero(wet, 1024));
  /* s_stop clears everything. */
  CHECK(command(wet, "s_stop", NULL, 0));
  CHECK(qa_unified_audio_queued_frames(qa_app_application_audio_engine(wet)) == 0);
  CHECK(!mix_nonzero(wet, 256));
  qa_app_application_audio_close(wet);
  CHECK(qa_unified_audio_output_state_of(qa_app_application_audio_engine(wet)) == QA_UNIFIED_AUDIO_CLOSED);
  CHECK(raises((call){run_s_info, wet, NULL}, "Sound system is closed"));
  CHECK(raises((call){run_reset, wet, NULL}, "Application audio closed"));
  qa_app_application_audio_destroy(wet);
  qa_app_loaded_content_destroy(content);
}

/* Presentation owners: retirement removes owned statics; stale sequences are dropped. */
static void test_owners(void) {
  qa_resource_request none = {NULL, NULL};
  qa_app_loaded_content *content = load_content(make_recipe(product_ids[1], product_ids[1], QA_ENVIRONMENT_AUDIO_CONTENT, none));
  qa_app_application_audio *audio = make_audio(content, false);
  qa_q1_event ambient;
  memset(&ambient, 0, sizeof ambient);
  ambient.kind = "ambient"; ambient.path = "ambience/drip1.wav"; ambient.volume = 1; ambient.attenuation = 0;
  qa_app_sim_presentation_event event = event_of("q1", "ambient", product_ids[0], 10, &ambient);
  event.has_owner = true; event.owner.provider = "p:owned"; event.owner.generation = 1;
  qa_world_snapshot snapshot = snapshot_with(true);
  qa_app_application_audio_frame(audio, &snapshot, &listener, 1, &event, 1, qa_app_audio_now_milliseconds(), NULL, 0);
  CHECK(mix_nonzero(audio, 1024));
  qa_app_sim_presentation_event retire = event_of("presentation-owner", "retired", product_ids[0], 11, NULL);
  retire.has_owner = true; retire.owner = event.owner; retire.presentation_owner.owner = event.owner;
  qa_app_application_audio_receive(audio, &retire, 1, world(), false);
  CHECK(!mix_nonzero(audio, 1024));
  /* A retired owner's later events are ignored. */
  event.sequence = 12;
  qa_app_application_audio_frame(audio, &snapshot, &listener, 1, &event, 1, qa_app_audio_now_milliseconds(), NULL, 0);
  CHECK(!mix_nonzero(audio, 1024));
  /* A refreshed owner rejects events at or below its revision. */
  qa_app_sim_presentation_event refresh = event_of("presentation-owner", "refreshed", product_ids[0], 20, NULL);
  refresh.has_owner = true; refresh.owner.provider = "p:other"; refresh.owner.generation = 1;
  refresh.presentation_owner.owner = refresh.owner;
  qa_app_application_audio_receive(audio, &refresh, 1, world(), false);
  qa_app_sim_presentation_event other = event;
  other.owner = refresh.owner; other.sequence = 20;
  qa_app_application_audio_frame(audio, &snapshot, &listener, 1, &other, 1, qa_app_audio_now_milliseconds(), NULL, 0);
  CHECK(!mix_nonzero(audio, 1024));
  other.sequence = 21;
  qa_app_application_audio_frame(audio, &snapshot, &listener, 1, &other, 1, qa_app_audio_now_milliseconds(), NULL, 0);
  CHECK(mix_nonzero(audio, 1024));
  qa_app_application_audio_destroy(audio);
  qa_app_loaded_content_destroy(content);
}

/* Environment: the default file is optional, a selected one is not. */
static int trace_calls;
static qa_trace_result scene_trace(void *ctx, qa_trace_query query) {
  SENTINEL(ctx, scene_sentinel);
  trace_calls++;
  qa_trace_result result;
  memset(&result, 0, sizeof result);
  result.kind = "q2"; result.fraction = 1; result.end = query.end;
  return result;
}
static void run_prepare(call *c) { qa_app_application_audio_prepare_environment(c->audio, c->arg); }
static void test_environment(void) {
  qa_scene_queries scene;
  memset(&scene, 0, sizeof scene);
  scene.ctx = &scene_sentinel; scene.trace = scene_trace;
  qa_resource_request none = {NULL, NULL};
  qa_app_loaded_content *content = load_content(make_recipe(product_ids[2], product_ids[1], QA_ENVIRONMENT_AUDIO_CONTENT, none));
  qa_app_application_audio *audio = make_audio(content, true);
  clear_printed();
  qa_app_application_audio_prepare_environment(audio, &scene);
  CHECK(printed_count == 1 && text_eq(printed[0], "Sound environments unavailable: Sound environments require an environments array\n"));
  /* Geometry acoustics follow s_geometryAcoustics on the volume registry. */
  qa_cvar_registry_options cvar_options;
  memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q2_CLASSIC; cvar_options.print = capture_print; cvar_options.print_ctx = &print_sentinel;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&cvar_options);
  qa_cvar_snapshot ignored;
  if (qa_cvar_registry_register(cvars, "volume", "0.5", 0, &ignored)) qa_cvar_read_free(&ignored);
  if (qa_cvar_registry_register(cvars, "bgmvolume", "2", 0, &ignored)) qa_cvar_read_free(&ignored);
  if (qa_cvar_registry_register(cvars, "s_geometryAcoustics", "1", 0, &ignored)) qa_cvar_read_free(&ignored);
  qa_app_application_audio_bind_volume_cvars(audio, cvars);
  CHECK(qa_app_application_audio_effects_volume(audio) == 0.5);
  CHECK(qa_app_application_audio_music_volume(audio) == 1); /* clamped */
  qa_app_application_audio_set_effects_volume(audio, 0.25);
  CHECK(qa_cvar_registry_variable_value(cvars, "volume") == 0.25);
  qa_app_application_audio_set_music_volume(audio, 0.5);
  CHECK(qa_cvar_registry_variable_value(cvars, "bgmvolume") == 0.5);
  qa_app_application_audio_destroy(audio);
  qa_resource_request missing = {product_ids[1], "sound/none.environments"};
  qa_app_loaded_content *selected = load_content(make_recipe(product_ids[2], product_ids[1], QA_ENVIRONMENT_SELECTED, missing));
  qa_app_application_audio *strict = make_audio(selected, true);
  CHECK(raises((call){run_prepare, strict, &scene}, "Required environment resource is missing: q2-classic-baseq2/sound/none.environments"));
  qa_app_application_audio_destroy(strict);
  qa_app_loaded_content_destroy(selected);
  /* Missing timing for the engine provider. */
  qa_executable_recipe untimed = make_recipe(product_ids[2], product_ids[1], QA_ENVIRONMENT_DISABLED, none);
  untimed.engine_behavior.provider = "p:other";
  qa_app_loaded_content *untimed_content = load_content(untimed);
  qa_app_application_audio *untimed_audio = make_audio(untimed_content, true);
  CHECK(raises((call){run_prepare, untimed_audio, &scene}, "Audio geometry has no numeric profile"));
  qa_app_application_audio_destroy(untimed_audio);
  qa_app_loaded_content_destroy(untimed_content);
  /* With a device, an enabled geometry setting traces the scene each mixed voice. */
  qa_app_application_audio *traced = make_audio(content, false);
  qa_app_application_audio_bind_volume_cvars(traced, cvars);
  clear_printed();
  qa_app_application_audio_prepare_environment(traced, &scene);
  qa_q2_presentation_event shot;
  memset(&shot, 0, sizeof shot);
  shot.kind = "sound"; shot.has_actor = false; shot.path = "weapons/railgf1a.wav"; shot.origin.x = 64;
  shot.channel = 1; shot.volume = 1; shot.attenuation = 1; shot.loop = "once";
  qa_app_sim_presentation_event event = event_of("q2", "sound", product_ids[1], 1, &shot);
  qa_world_snapshot snapshot = snapshot_with(true);
  trace_calls = 0;
  qa_app_application_audio_frame(traced, &snapshot, &listener, 1, &event, 1, qa_app_audio_now_milliseconds(), NULL, 0);
  mix_nonzero(traced, 512);
  CHECK(trace_calls > 0);
  qa_app_application_audio_destroy(traced);
  qa_cvar_registry_destroy(cvars);
  qa_app_loaded_content_destroy(content);
}

/* Haptics: bindHaptics hands the mount loader; a rejected soundHaptics reports. */
typedef struct haptic_log { int sounds; char last_sound[256]; char last_content[64]; bool fail; qa_app_audio_haptic_load_fn load; void *load_ctx; } haptic_log;
static haptic_log haptic_state;
static void sound_haptics(void *ctx, qa_content_id content, const char *sound, bool has_actor, qa_actor_id who, qa_audio_audience audience) {
  SENTINEL(ctx, haptics_sentinel);
  (void)has_actor; (void)who; (void)audience;
  haptic_state.sounds++;
  snprintf(haptic_state.last_sound, sizeof haptic_state.last_sound, "%s", sound);
  snprintf(haptic_state.last_content, sizeof haptic_state.last_content, "%s", content);
  if (haptic_state.fail) qa_raise("Error", 0, 0, "rumble failed");
}
static void bind_haptics(void *ctx, qa_app_audio_haptic_load_fn load, void *load_ctx) {
  SENTINEL(ctx, haptics_sentinel);
  haptic_state.load = load; haptic_state.load_ctx = load_ctx;
}
static void test_haptics(void) {
  qa_resource_request none = {NULL, NULL};
  qa_app_loaded_content *content = load_content(make_recipe(product_ids[1], product_ids[1], QA_ENVIRONMENT_DISABLED, none));
  qa_app_application_audio *audio = make_audio(content, false);
  memset(&haptic_state, 0, sizeof haptic_state);
  qa_app_audio_haptics haptics = {&haptics_sentinel, sound_haptics, bind_haptics};
  qa_app_application_audio_bind_haptics(audio, haptics);
  CHECK(haptic_state.load != NULL);
  size_t length = 0;
  qa_resource_request present = {product_ids[1], "sound/misc/talk.wav"};
  uint8_t *bytes = haptic_state.load(haptic_state.load_ctx, present, &length);
  CHECK(bytes != NULL && length == 44 + 2048 * 2);
  free(bytes);
  qa_resource_request absent = {product_ids[1], "tactile/none.bnvib"};
  CHECK(haptic_state.load(haptic_state.load_ctx, absent, &length) == NULL);
  qa_q2_presentation_event shot;
  memset(&shot, 0, sizeof shot);
  shot.kind = "sound"; shot.has_actor = true; shot.actor = actor; shot.path = "weapons/railgf1a.wav";
  shot.channel = 1; shot.volume = 1; shot.attenuation = 1; shot.loop = "once";
  qa_app_sim_presentation_event event = event_of("q2", "sound", product_ids[1], 1, &shot);
  qa_world_snapshot snapshot = snapshot_with(true);
  qa_app_application_audio_frame(audio, &snapshot, &listener, 1, &event, 1, qa_app_audio_now_milliseconds(), NULL, 0);
  CHECK(haptic_state.sounds == 1 && text_eq(haptic_state.last_sound, "sound/weapons/railgf1a.wav"));
  haptic_state.fail = true;
  clear_printed();
  qa_app_application_audio_receive(audio, &event, 1, world(), true);
  CHECK(printed_count == 1 && text_eq(printed[0], "Controller vibration unavailable: rumble failed\n"));
  CHECK(mix_nonzero(audio, 512)); /* the sound still plays */
  qa_app_application_audio_destroy(audio);
  qa_app_loaded_content_destroy(content);
}

/* Cgame frames and effect sounds. */
static bool active_flag;
static bool media_active(void *ctx) { SENTINEL(ctx, active_sentinel); return active_flag; }
static void run_release_frame(call *c) {
  qa_world_snapshot snapshot = snapshot_with(true);
  qa_app_application_audio_frame(c->audio, &snapshot, &listener, 1, NULL, 0, qa_app_audio_now_milliseconds(), NULL, 0);
}
static void test_cgame_and_effects(void) {
  qa_resource_request none = {NULL, NULL};
  qa_app_loaded_content *content = load_content(make_recipe(product_ids[1], product_ids[1], QA_ENVIRONMENT_DISABLED, none));
  qa_app_application_audio *audio = make_audio(content, false);
  memset(&haptic_state, 0, sizeof haptic_state);
  qa_app_audio_haptics haptics = {&haptics_sentinel, sound_haptics, bind_haptics};
  qa_app_application_audio_bind_haptics(audio, haptics);
  qa_app_application_effect_sound effect;
  memset(&effect, 0, sizeof effect);
  /* A frame-lifetime loop survives the frame's device pump; a one-shot would be consumed by it. */
  effect.content = product_ids[1]; effect.path = "sound/weapons/rg_hum.wav"; effect.channel = 1; effect.volume = 1;
  effect.playback_kind = "loop"; effect.actor = actor;
  qa_app_application_audio_receive_effect_sounds(audio, &effect, 1);
  qa_world_snapshot snapshot = snapshot_with(true);
  qa_app_application_audio_frame(audio, &snapshot, &listener, 1, NULL, 0, qa_app_audio_now_milliseconds(), NULL, 0);
  CHECK(mix_nonzero(audio, 512));
  CHECK(haptic_state.sounds == 0); /* loops report no haptics */
  /* An actor effect is a one-shot reporting haptics. */
  qa_unified_audio_stop_all(qa_app_application_audio_engine(audio));
  qa_app_application_effect_sound actor_effect = effect;
  actor_effect.playback_kind = "actor"; actor_effect.path = "misc/talk.wav";
  qa_app_application_audio_receive_effect_sounds(audio, &actor_effect, 1);
  qa_app_application_audio_frame(audio, &snapshot, &listener, 1, NULL, 0, qa_app_audio_now_milliseconds(), NULL, 0);
  CHECK(haptic_state.sounds == 1 && text_eq(haptic_state.last_sound, "sound/misc/talk.wav"));
  CHECK(text_eq(haptic_state.last_content, product_ids[1]));
  /* A seat-scoped cgame play operation reports haptics with the frame's content. */
  qa_sound_bank *bank = qa_sound_bank_create(mounted[1]);
  const qa_sound_asset *talk = NULL;
  CHECK(qa_sound_bank_register(bank, "misc/talk.wav", QA_SOUND_FAMILY_Q3, &talk) && talk != NULL);
  qa_app_q3_seat_audio_operation operation;
  memset(&operation, 0, sizeof operation);
  operation.kind = QA_APP_Q3_SEAT_AUDIO_PLAY;
  operation.play.sound = talk; operation.play.family = QA_SOUND_FAMILY_Q3; operation.play.origin.kind = QA_SOUND_ORIGIN_LOCAL;
  operation.play.audience.kind = QA_AUDIO_AUDIENCE_SEAT; operation.play.audience.seat = seat;
  operation.play.volume = 1;
  qa_app_q3_seat_audio_frame played;
  memset(&played, 0, sizeof played);
  played.content = product_ids[2]; played.seat = seat; played.operations = &operation; played.operation_count = 1;
  qa_app_application_audio_receive_cgame_frame(audio, &played);
  qa_app_application_audio_frame(audio, &snapshot, &listener, 1, NULL, 0, qa_app_audio_now_milliseconds(), NULL, 0);
  CHECK(haptic_state.sounds == 2 && text_eq(haptic_state.last_content, product_ids[2]));
  qa_app_q3_seat_audio_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.content = product_ids[2]; frame.seat = seat;
  operation.kind = QA_APP_Q3_SEAT_AUDIO_RELEASE_OWNER;
  frame.operations = &operation; frame.operation_count = 1;
  qa_app_application_audio_receive_cgame_frame(audio, &frame);
  CHECK(raises((call){run_release_frame, audio, NULL}, "Component audio retirement requires its source owner"));
  /* Component media: an inactive request does nothing; music-stop stops. */
  active_flag = false;
  qa_component_presentation_media_request stop;
  memset(&stop, 0, sizeof stop);
  stop.kind = QA_MEDIA_REQUEST_MUSIC_STOP;
  qa_app_application_audio_play_component_media(audio, NULL, product_ids[1], &stop, media_active, &active_sentinel);
  active_flag = true;
  qa_app_application_audio_play_component_media(audio, NULL, product_ids[1], &stop, media_active, &active_sentinel);
  qa_app_application_audio_destroy(audio);
  qa_sound_bank_destroy(bank);
  qa_app_loaded_content_destroy(content);
}

/* Q3 footsteps: an unparsable authored config falls back to normal (TS oracle). */
static void test_footsteps(void) {
  qa_resource_request none = {NULL, NULL};
  qa_app_loaded_content *content = load_content(make_recipe(product_ids[2], product_ids[2], QA_ENVIRONMENT_DISABLED, none));
  qa_app_audio_options options;
  memset(&options, 0, sizeof options);
  options.defer_output = true;
  options.q3_team_game = NULL;
  qa_app_application_audio *audio = qa_app_application_audio_create(content, fixed_now, &now_sentinel, 1, "missing-custom-model",
                                                                     capture_print, &print_sentinel, &options);
  clear_printed();
  qa_app_application_audio_preload_character_footsteps(audio);
  CHECK(command(audio, "s_list", NULL, 0));
  CHECK(printed_count == 17);
  for (int i = 1; i <= 4; i++) {
    char line[128];
    snprintf(line, sizeof line, "Sound unavailable: q3-baseq3/player/footsteps/step%d.wav\n", i);
    CHECK(text_eq(printed[i - 1], line));
    snprintf(line, sizeof line, "unavailable: q3-baseq3/q3/player/footsteps/step%d.wav/\n", i);
    CHECK(text_eq(printed[i + 3], line));
  }
  CHECK(strstr(printed[8], "1024 bytes : sound/player/footsteps/clank1.wav [") != NULL);
  CHECK(strstr(printed[15], "1024 bytes : sound/player/footsteps/splash4.wav [") != NULL);
  CHECK(text_eq(printed[16], "Decoded PCM storage: 8192 bytes in 8 registered resources\n"));
  qa_app_application_audio_destroy(audio);
  qa_app_loaded_content_destroy(content);
}

static bool team_game(void *ctx) { SENTINEL(ctx, team_sentinel); return true; }
/* The team-game option reaches q3VoiceFallback for "*" Q3 sounds. */
static void test_team_voice(void) {
  qa_resource_request none = {NULL, NULL};
  qa_app_loaded_content *content = load_content(make_recipe(product_ids[2], product_ids[2], QA_ENVIRONMENT_DISABLED, none));
  qa_app_audio_options options;
  memset(&options, 0, sizeof options);
  options.defer_output = true; options.q3_team_game = team_game; options.q3_team_game_ctx = &team_sentinel;
  qa_app_application_audio *audio = qa_app_application_audio_create(content, fixed_now, &now_sentinel, 1, "missing-custom-model",
                                                                     capture_print, &print_sentinel, &options);
  clear_printed();
  qa_app_application_audio_preload_sound(audio, product_ids[2], "*pain25_1.wav", NULL);
  CHECK(command(audio, "s_list", NULL, 0));
  CHECK(printed_contains("sound/player/sarge/pain25_1.wav"));
  qa_app_application_audio_destroy(audio);
  qa_app_loaded_content_destroy(content);
}

static void audio_tests(void) {
  setenv("SDL_AUDIODRIVER", "dummy", 1);
  build_fixture();
  build_catalog();
  if (!qa_identity_owner_create(&identity, "top-ui-audio")) exit(1);
  actor = qa_identity_owner_actor(&identity, 1, 0);
  seat = qa_identity_owner_seat(&identity, 0);
  memset(&listener, 0, sizeof listener);
  listener.seat = seat; listener.has_actor = true; listener.actor = actor; listener.gain = 1;
  listener.axis.vectors[0].x = 1; listener.axis.vectors[1].y = 1; listener.axis.vectors[2].z = 1;
  test_q3_voice_fallback();
  test_commands();
  test_world_audio();
  test_owners();
  test_environment();
  test_haptics();
  test_cgame_and_effects();
  test_footsteps();
  test_team_voice();
  CHECK(sentinel_failures == 0);
  qa_identity_owner_destroy(&identity);
  free_catalog();
  remove_fixture();
}

/* ---- ui ---- */
static int ui_sentinel_cell;
#define UI_SENTINEL ((void *)&ui_sentinel_cell)
static int ui_bad_ctx;
static char ui_commands[16][64]; static size_t ui_command_count;
static int ui_quits;
static qa_item_id ui_active_weapon = "q2:weapon_blaster";
static qa_app_sim_player_ui_item ui_items[1];
static size_t ui_item_count;
static qa_app_sim_player_ui ui_player(void *ctx, qa_actor_id who) {
  if (ctx != UI_SENTINEL) ui_bad_ctx++;
  (void)who;
  qa_app_sim_player_ui player;
  memset(&player, 0, sizeof player);
  player.arsenal_warning = "none"; player.health = 100; player.armor.powered.kind = "none";
  player.active_weapon = ui_active_weapon;
  player.items = ui_items; player.item_count = ui_item_count;
  return player;
}
static qa_app_sim_player_view ui_view(void *ctx, qa_actor_id who) {
  if (ctx != UI_SENTINEL) ui_bad_ctx++;
  (void)who;
  qa_app_sim_player_view view;
  memset(&view, 0, sizeof view);
  view.view_height = 22;
  return view;
}
static void ui_command(void *ctx, const char *name, const char *const *args, size_t count) {
  if (ctx != UI_SENTINEL) ui_bad_ctx++;
  if (ui_command_count >= 16) return;
  size_t length = (size_t)snprintf(ui_commands[ui_command_count], 64, "%s(", name);
  for (size_t i = 0; i < count && length < 64; i++)
    length += (size_t)snprintf(ui_commands[ui_command_count] + length, 64 - length, "%s%s", i ? "," : "", args[i]);
  if (length < 64) snprintf(ui_commands[ui_command_count] + length, 64 - length, ")");
  ui_command_count++;
}
static void ui_quit(void *ctx) { if (ctx != UI_SENTINEL) ui_bad_ctx++; ui_quits++; }
static double ui_now(void *ctx) { if (ctx != UI_SENTINEL) ui_bad_ctx++; return 1000; }
static void ui_action_quit(void *ctx) { if (ctx != UI_SENTINEL) ui_bad_ctx++; }
static void ui_action_execute(void *ctx, const char *name, const char *const *args, size_t count, const qa_seat_id *who,
                              const qa_command_context *source) {
  if (ctx != UI_SENTINEL) ui_bad_ctx++;
  (void)name; (void)args; (void)count; (void)who; (void)source;
}
static void ui_action_print(void *ctx, const char *text) { if (ctx != UI_SENTINEL) ui_bad_ctx++; (void)text; }
static void ui_open_game(void *ctx) { qa_native_ui_controller_open_menu(ctx, "menu:application:game"); }
static qa_seat_input_event ui_key(qa_seat_id who, double code) {
  qa_seat_input_event event;
  memset(&event, 0, sizeof event);
  event.kind = "key"; event.seat = who; event.time_milliseconds = 1000; event.code = code; event.down = true;
  return event;
}
static qa_seat_input_focus ui_focus(const char *kind) {
  qa_seat_input_focus focus;
  memset(&focus, 0, sizeof focus);
  focus.kind = kind;
  return focus;
}
static qa_app_sim_presentation_event ui_event(const char *kind) {
  qa_app_sim_presentation_event event;
  memset(&event, 0, sizeof event);
  event.kind = kind; event.sequence = 1; event.content = "q1-classic-id1"; event.seconds = 2;
  return event;
}

static void ui_tests(void) {
  setenv("SDL_VIDEODRIVER", "dummy", 0);
  setenv("SDL_AUDIODRIVER", "dummy", 1);
  build_fixture();
  build_catalog();
  qa_resource_request none = {0};
  qa_app_loaded_content *content = load_content(make_recipe(product_ids[1], product_ids[1], QA_ENVIRONMENT_DISABLED, none));
  qa_app_application_audio *audio = make_audio(content, true);
  char root_template[256];
  snprintf(root_template, sizeof root_template, "%s/ui-c-oracle-XXXXXX", getenv("TMPDIR") != NULL ? getenv("TMPDIR") : "/tmp");
  char *root = mkdtemp(root_template);
  CHECK(root != NULL);
  qa_sdl_window_options window_options;
  memset(&window_options, 0, sizeof window_options);
  window_options.title = "oracle"; window_options.width = 64; window_options.height = 48;
  window_options.hidden = true; window_options.backend = QA_SDL_BACKEND_CPU;
  qa_sdl_window window;
  if (!qa_sdl_window_open(&window_options, &window)) { fprintf(stderr, "SKIP ui: no SDL window\n"); return; }
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "ui-oracle"));
  qa_client_id client_id = qa_identity_owner_client(&owner, 0, 1);
  qa_session_client *client = qa_session_client_create(client_id);
  qa_seat_id seat_id = qa_identity_owner_seat(&owner, 0);
  qa_session_seat *session_seat = qa_session_seat_create(seat_id, client);
  qa_actor_id who = qa_identity_owner_actor(&owner, 1, 1);
  qa_config_store store;
  CHECK(qa_config_store_init(&store, root));
  qa_app_options options;
  memset(&options, 0, sizeof options);
  options.network_kind = "offline"; options.movement = QA_GAME_FAMILY_Q3; options.user_content_root = root;
  qa_app_sim_presentation_access simulation;
  memset(&simulation, 0, sizeof simulation);
  simulation.ctx = UI_SENTINEL; simulation.player_view = ui_view; simulation.player_ui = ui_player;
  qa_app_local_player player = {session_seat, who};
  qa_app_input_open_args args;
  memset(&args, 0, sizeof args);
  args.window = qa_app_input_window_from_sdl(&window);
  args.players = &player; args.player_count = 1;
  args.options = &options; args.dialect = QA_COMMAND_DIALECT_Q3; args.simulation = &simulation;
  args.actions.ctx = UI_SENTINEL;
  args.actions.quit = ui_action_quit; args.actions.execute = ui_action_execute; args.actions.print = ui_action_print;
  args.now = ui_now; args.now_ctx = UI_SENTINEL; args.settings = &store;
  qa_app_application_input *input = qa_app_application_input_open(&args);
  qa_app_local_input *local = qa_app_application_input_local(input, 0);
  qa_renderer_image image;
  memset(&image, 0, sizeof image);
  image.owner.identity = &owner; image.owner.session = owner.session;
  image.source_name = "test"; image.width = 128; image.height = 128;
  qa_text_atlas atlas;
  CHECK(qa_classic_charset(image, NULL, true, &atlas));
  qa_text_font_selection font;
  memset(&font, 0, sizeof font);
  font.classic = &atlas;
  qa_content_catalog empty;
  qa_content_catalog_init(&empty, "/unused", NULL, 0, NULL, 0, 0, NULL, NULL, 0);
  /* No installed product: the classic font for body and title; no image is registered. */
  qa_app_menu_typography *typography = qa_app_load_menu_typography(&empty, NULL, &atlas, NULL);
  qa_native_ui_art art;
  memset(&art, 0, sizeof art);
  art.skin = qa_ui_default_skin("resource:font");
  art.white.name = "white"; art.white.image = image;
  qa_app_seat_ui_options ui_options;
  memset(&ui_options, 0, sizeof ui_options);
  ui_options.local = local; ui_options.art = &art; ui_options.input = input;
  ui_options.simulation = &simulation; ui_options.font = font; ui_options.audio = audio;
  ui_options.quit = ui_quit; ui_options.quit_ctx = UI_SENTINEL;
  ui_options.command = ui_command; ui_options.command_ctx = UI_SENTINEL;
  ui_options.typography = typography;
  qa_app_seat_ui *ui = qa_app_seat_ui_create(&ui_options);
  qa_native_ui_controller *controller = qa_app_seat_ui_controller(ui);
  /* active null false */
  CHECK(qa_native_ui_controller_active_menu(controller) == NULL && !qa_app_seat_ui_pause_menu_open(ui));
  /* escape true menu:application:game true */
  qa_seat_input_event escape = ui_key(seat_id, 27);
  CHECK(qa_app_seat_ui_input(ui, &escape, ui_focus("game")));
  CHECK(text_eq(qa_native_ui_controller_active_menu(controller), "menu:application:game") && qa_app_seat_ui_pause_menu_open(ui));
  /* draw-texts ["Paused","Resume game","Save game","Load game","Options","Console","Match controls","End game"] */
  qa_ui_draw_context context;
  memset(&context, 0, sizeof context);
  context.binding.seat = seat_id; context.binding.client = client_id;
  context.binding.viewport.width = 640; context.binding.viewport.height = 480;
  context.binding.safe_area.width = 640; context.binding.safe_area.height = 480;
  context.binding.hud_scale = 1; context.time_milliseconds = 1000;
  size_t draw_count = 0;
  const qa_ui_draw_command *drawn = qa_native_ui_controller_draw(controller, context, &draw_count);
  static const char *const texts[] = {"Paused", "Resume game", "Save game", "Load game", "Options", "Console", "Match controls", "End game"};
  size_t text_count = 0;
  for (size_t i = 0; i < draw_count; i++) {
    if (drawn[i].kind != QA_UI_DRAW_TEXT) continue;
    CHECK(text_count < 8 && text_eq(drawn[i].text, texts[text_count]));
    text_count++;
  }
  CHECK(text_count == 8);
  qa_native_ui_controller_close_all(controller);
  CHECK(qa_native_ui_controller_active_menu(controller) == NULL && !qa_app_seat_ui_pause_menu_open(ui));
  /* controller6 true menu:application:game */
  qa_seat_input_event button;
  memset(&button, 0, sizeof button);
  button.kind = "controller-button"; button.seat = seat_id; button.time_milliseconds = 1000; button.button = 6; button.down = true;
  CHECK(qa_app_seat_ui_input(ui, &button, ui_focus("game")));
  CHECK(text_eq(qa_native_ui_controller_active_menu(controller), "menu:application:game"));
  qa_app_seat_ui_close_menus(ui);
  /* console-focus false; switch false false; switch-q1 true [] */
  CHECK(!qa_app_seat_ui_input(ui, &escape, ui_focus("console")));
  CHECK(!qa_app_seat_ui_switch_weapon(ui, 1, 2) && !qa_app_seat_ui_cycle_weapon(ui, 1));
  ui_active_weapon = "q1:weapon_nailgun";
  CHECK(qa_app_seat_ui_switch_weapon(ui, 1, 2) && ui_command_count == 0);
  /* wheel-open true; sample-open attack cleared; wheel-closed false []; sample-closed unchanged. */
  memset(ui_items, 0, sizeof ui_items);
  ui_items[0].id = "q3:weapon_rocket"; ui_items[0].label = "Rocket"; ui_items[0].kind = "weapon";
  ui_items[0].source_ordinal = 5; ui_items[0].owned = true; ui_items[0].has_ammo = true;
  ui_items[0].has_count = true; ui_items[0].count = 10; ui_items[0].warning_count = 5;
  ui_item_count = 1;
  qa_app_seat_ui_wheel(ui, QA_WHEEL_WEAPONS, true);
  CHECK(qa_seat_weapon_wheel_is_open(qa_app_seat_ui_weapon_wheel(ui)));
  qa_button_sample attack = {(char *)"attack", 1, true, true};
  qa_seat_input_sample sample;
  memset(&sample, 0, sizeof sample);
  sample.seat = seat_id; sample.now_milliseconds = 1000; sample.frame_milliseconds = 16; sample.focus = ui_focus("game");
  sample.buttons = &attack; sample.button_count = 1;
  qa_app_seat_ui_sample(ui, &sample);
  CHECK(attack.fraction == 0 && !attack.active && !attack.pressed);
  qa_app_seat_ui_wheel(ui, QA_WHEEL_WEAPONS, false);
  CHECK(!qa_seat_weapon_wheel_is_open(qa_app_seat_ui_weapon_wheel(ui)) && ui_command_count == 0);
  attack.fraction = 1; attack.active = true; attack.pressed = true; sample.now_milliseconds = 5000;
  qa_app_seat_ui_sample(ui, &sample);
  CHECK(attack.fraction == 1 && attack.active && attack.pressed);
  /* receive routing: notifications ["q1 notify:false","q2 chat:true","q2p chat:true","q3 chat:true"], center "q3 cp". */
  qa_q1_event q1_center, q1_notify;
  memset(&q1_center, 0, sizeof q1_center);
  q1_center.kind = "message"; q1_center.has_player = true; q1_center.player = who; q1_center.center = true; q1_center.text = "q1 center";
  q1_notify = q1_center; q1_notify.center = false; q1_notify.text = "q1 notify";
  qa_q2_presentation_event q2_center, q2_chat;
  memset(&q2_center, 0, sizeof q2_center);
  q2_center.kind = "centerprint"; q2_center.has_actor = true; q2_center.actor = who; q2_center.text = "q2 center";
  memset(&q2_chat, 0, sizeof q2_chat);
  q2_chat.kind = "print"; q2_chat.level = "chat"; q2_chat.text = "q2 chat";
  qa_q2_player_event q2p_chat;
  memset(&q2p_chat, 0, sizeof q2p_chat);
  q2p_chat.kind = "print"; q2p_chat.level = "chat"; q2p_chat.text = "q2p chat";
  qa_app_q3_source_event q3_cp, q3_chat, q3_other;
  memset(&q3_cp, 0, sizeof q3_cp);
  q3_cp.kind = QA_APP_Q3_EVENT_SERVER_COMMAND; q3_cp.client = -1; q3_cp.text = "cp \"q3 cp\"";
  q3_chat = q3_cp; q3_chat.client = 0; q3_chat.text = "chat \"q3 chat\"";
  q3_other = q3_cp; q3_other.client = 5; q3_other.text = "chat \"other\"";
  qa_app_sim_presentation_event events[8];
  events[0] = ui_event("q1"); events[0].q1 = &q1_center;
  events[1] = ui_event("q1"); events[1].q1 = &q1_notify;
  events[2] = ui_event("q2"); events[2].q2 = &q2_center;
  events[3] = ui_event("q2"); events[3].q2 = &q2_chat;
  events[4] = ui_event("q2-player"); events[4].q2_player = &q2p_chat;
  events[5] = ui_event("q3-source"); events[5].q3_source = &q3_cp;
  events[6] = ui_event("q3-source"); events[6].q3_source = &q3_chat;
  events[7] = ui_event("q3-source"); events[7].q3_source = &q3_other;
  qa_app_seat_ui_receive(ui, events, 8);
  qa_seat_hud_active active;
  CHECK(qa_seat_hud_messages_active(qa_app_seat_ui_messages(ui), 2100, &active));
  static const char *const notes[] = {"q1 notify", "q2 chat", "q2p chat", "q3 chat"};
  static const bool chats[] = {false, true, true, true};
  CHECK(active.notification_count == 4);
  for (size_t i = 0; i < 4 && i < active.notification_count; i++)
    CHECK(text_eq(active.notifications[i].text, notes[i]) && active.notifications[i].chat == chats[i]);
  CHECK(active.has_center_print && text_eq(active.center_print.text, "q3 cp") && active.center_print.instant
        && !active.center_print.has_character_milliseconds);
  qa_seat_hud_active_free(&active);
  /* after-close Unknown menu: menu:application:game; reattach ok */
  qa_app_seat_ui_close(ui);
  qa_caught_error caught;
  CHECK(!qa_try(ui_open_game, controller, &caught) && text_eq(caught.message, "Unknown menu: menu:application:game"));
  qa_app_input_ui again = {UI_SENTINEL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
  qa_app_input_ui_attachment *reattached = qa_app_application_input_attach_ui(input, seat_id, again);
  CHECK(reattached != NULL);
  qa_app_input_ui_attachment_detach(reattached);
  CHECK(ui_quits == 0 && ui_bad_ctx == 0);
  qa_app_seat_ui_destroy(ui);
  qa_app_menu_typography_close(typography);
  qa_content_catalog_destroy(&empty);
  qa_text_atlas_destroy(&atlas);
  qa_app_application_input_close(input);
  qa_app_application_input_destroy(input);
  qa_session_seat_destroy(session_seat);
  qa_session_client_destroy(client);
  qa_config_store_destroy(&store);
  qa_identity_owner_destroy(&owner);
  qa_sdl_window_close(&window);
  qa_app_application_audio_destroy(audio);
  qa_app_loaded_content_destroy(content);
  char command[600];
  snprintf(command, sizeof command, "rm -rf '%s'", root);
  CHECK(system(command) == 0);
  free_catalog();
  remove_fixture();
}

int main(void) {
  audio_tests();
  ui_tests();
  printf("test_top_ui_audio: %d checks, %d failures\n", checks, failures);
  fflush(stdout);
  return failures == 0 ? 0 : 1;
}
