/*
 * CampaignCinematic (src/app/bootstrap/campaign-cinematic.ts) over retail Q2 content (baseq2 video/eou1_.cin), real
 * ApplicationAssets images, the ApplicationAudio engine (SDL dummy output) and a CPU NativeRenderer: the scenario of
 * tests/app/oracles/app_concrete_cinematic.ts, whose bun transcript (app_concrete_cinematic.txt) this test reproduces
 * line for line (the oracle's lines naming executed render commands are its renderer double's record and are skipped
 * here). The current callback checks a sentinel ctx. Needs QTS_TEST_CORPUS. GPL-2.0-or-later.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/audio.h"
#include "qa/app/bootstrap/campaign_cinematic.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/content/mods/declaration_writer.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/support.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    checks++;                                                             \
    if (!(cond)) {                                                        \
      failures++;                                                         \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                     \
  } while (0)

static char transcript[16384];
static void out(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void out(const char *format, ...) {
  char line[4096];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof line, format, args);
  va_end(args);
  strncat(transcript, line, sizeof transcript - strlen(transcript) - 2);
  strcat(transcript, "\n");
}

typedef struct live_state { int sentinel; bool current; } live_state;
static live_state live = {0x5a, true};
static bool is_live(void *ctx) {
  live_state *state = ctx;
  if (state != &live || state->sentinel != 0x5a) { fprintf(stderr, "wrong current ctx\n"); abort(); }
  return state->current;
}
static double now_ms(void *ctx) { (void)ctx; return 0; }
static void quiet(void *ctx, const char *text) { (void)ctx; (void)text; }

typedef struct env {
  qa_app_loaded_content *content;
  qa_app_application_assets *assets;
  qa_app_application_audio *audio;
  qa_app_native_renderer *renderer;
  qa_seat_id seat;
  const char *name;
  const qa_save_value *saved;
  qa_app_campaign_cinematic *result;
} env;
static void prepare_body(void *ctx) {
  env *e = ctx;
  qa_app_screen_cinematic_request request = {e->name, false, false, false};
  e->result = qa_app_campaign_cinematic_prepare_content(request, qa_app_loaded_content_mounts(e->content), e->assets, e->audio, e->renderer,
      e->seat, is_live, &live, NULL);
}
static void restore_body(void *ctx) {
  env *e = ctx;
  e->result = qa_app_campaign_cinematic_restore_content(e->saved, qa_app_loaded_content_mounts(e->content), e->assets, e->audio,
      e->renderer, e->seat, is_live, &live, NULL);
}
static void attempt(env *e, const char *label, const char *name) {
  e->name = name;
  qa_caught_error caught;
  if (qa_try(prepare_body, e, &caught)) {
    out("%s: prepared", label);
    qa_app_campaign_cinematic_destroy(e->result);
  } else out("%s: %s", label, caught.message);
}
static void activate_body(void *ctx) { qa_app_campaign_cinematic_activate(ctx); }

static const void *owner_identity;

int main(void) {
  const char *root = getenv("QTS_TEST_CORPUS");
  if (root == NULL) { printf("skip: QTS_TEST_CORPUS not set\n0 checks, 0 failures\n"); return 0; }
  setenv("SDL_AUDIODRIVER", "dummy", 1);
  const char *argv[] = {"--game", "q2-classic-baseq2", "--map", "base1", "--dedicated", "--content-root", root};
  qa_content_root_search search = {NULL, NULL};
  qa_app_application_command command = qa_app_parse_application_command(argv, 7, search, NULL);
  CHECK(strcmp(command.kind, "run") == 0);
  qa_app_content_host host;
  memset(&host, 0, sizeof host);
  env e;
  memset(&e, 0, sizeof e);
  e.content = qa_app_load_application_content_native(host, command.options, NULL, false, NULL, NULL, NULL);
  qa_identity_owner ids;
  CHECK(qa_identity_owner_create(&ids, "campaign-cinematic"));
  qa_renderer_resource_owner owner = {&owner_identity, ids.session, 0};
  qa_app_application_assets_options asset_options;
  memset(&asset_options, 0, sizeof asset_options);
  e.assets = qa_app_application_assets_create(e.content, owner, NULL, &asset_options);
  qa_app_audio_options audio_options;
  memset(&audio_options, 0, sizeof audio_options);
  e.audio = qa_app_application_audio_create(e.content, now_ms, NULL, 1, "missing-custom-model", quiet, NULL, &audio_options);
  qa_app_native_renderer_host renderer_host = {0};
  e.renderer = qa_app_native_renderer_open(renderer_host, "cpu", 64, 48, true, 1, false, owner, NULL);
  CHECK(e.renderer != NULL);
  e.seat = qa_identity_owner_seat(&ids, 0);

  attempt(&e, "dotdot", "../eou1_.cin");
  attempt(&e, "missing", "none");
  attempt(&e, "cin-fallback", "none.cin");
  attempt(&e, "format", "pics/colormap.bad");
  live.current = false;
  attempt(&e, "retired", "eou1_.cin");
  live.current = true;
  e.name = "eou1_.cin";
  prepare_body(&e);
  qa_app_campaign_cinematic *movie = e.result;
  out("prepared status=%s", qa_app_campaign_cinematic_status(movie));
  qa_app_campaign_cinematic_activate(movie);
  out("active status=%s", qa_app_campaign_cinematic_status(movie));
  bool ended = false;
  for (int i = 0; i < 6; i++) ended = qa_app_campaign_cinematic_frame(movie, i == 0 ? 0 : 50, i + 1, false, false);
  qa_save_value *saved = NULL;
  qa_cinematic_timeline timeline = qa_app_campaign_cinematic_timeline(movie);
  out("frames ended=%s status=%s elapsed=%s source=%s", ended ? "true" : "false", qa_app_campaign_cinematic_status(movie),
      QA_JS_NUMBER(timeline.elapsed_milliseconds), QA_JS_NUMBER(timeline.source_time_milliseconds));
  qa_app_campaign_cinematic_pause(movie, true);
  out("paused status=%s", qa_app_campaign_cinematic_status(movie));
  qa_app_campaign_cinematic_pause(movie, false);
  saved = qa_app_campaign_cinematic_capture_checkpoint(movie);
  char keys[512] = "";
  for (size_t i = 0; i < saved->entry_count; i++) {
    if (i != 0) strcat(keys, ",");
    strcat(keys, saved->entries[i].key);
  }
  char *request_json = qa_json_stringify_value(qa_save_value_get(saved, "request"), NULL);
  out("checkpoint keys=%s version=%s clock=%s audioStarted=%s request=%s", keys, QA_JS_NUMBER(qa_save_value_get(saved, "version")->number),
      QA_JS_NUMBER(qa_save_value_get(saved, "clock")->number), qa_save_value_get(saved, "audioStarted")->boolean ? "true" : "false", request_json);
  free(request_json);
  out("checkpoint resource=%s initialAudio=%zu", qa_save_value_get(qa_save_value_get(saved, "resource"), "requestedPath")->text,
      qa_save_value_get(saved, "initialAudio")->item_count);
  qa_seat_input_event key;
  memset(&key, 0, sizeof key);
  key.kind = "key";
  key.down = true;
  bool input = qa_app_campaign_cinematic_input(movie, &key);
  out("input=%s status=%s", input ? "true" : "false", qa_app_campaign_cinematic_status(movie));
  qa_app_campaign_cinematic_close(movie, 9);
  out("closed status=%s", qa_app_campaign_cinematic_status(movie));
  qa_app_campaign_cinematic_destroy(movie);
  e.saved = saved;
  restore_body(&e);
  qa_app_campaign_cinematic *restored = e.result;
  out("restored status=%s", qa_app_campaign_cinematic_status(restored));
  qa_app_campaign_cinematic_activate(restored);
  qa_app_campaign_cinematic_frame(restored, 50, 10, false, false);
  out("restored frame status=%s elapsed=%s", qa_app_campaign_cinematic_status(restored),
      QA_JS_NUMBER(qa_app_campaign_cinematic_timeline(restored).elapsed_milliseconds));
  qa_app_campaign_cinematic_close(restored, 11);
  qa_caught_error caught;
  if (!qa_try(activate_body, restored, &caught)) out("activate closed: %s", caught.message);
  qa_app_campaign_cinematic_destroy(restored);
  /* request.name changed: the opened resource no longer matches the saved one. */
  qa_save_value *request = (qa_save_value *)qa_save_value_get(saved, "request");
  qa_save_value *name = (qa_save_value *)qa_save_value_get(request, "name");
  free(name->text);
  name->text = strdup("eou2_.cin");
  if (qa_try(restore_body, &e, &caught)) { out("changed: restored"); qa_app_campaign_cinematic_destroy(e.result); }
  else out("changed: %s", caught.message);
  qa_save_value_free(saved);

  FILE *file = fopen("tests/app/oracles/app_concrete_cinematic.txt", "r");
  CHECK(file != NULL);
  if (file != NULL) {
    char expected[16384] = "", line[4096];
    while (fgets(line, sizeof line, file) != NULL)
      if (strncmp(line, "executed", 8) != 0) strncat(expected, line, sizeof expected - strlen(expected) - 1);
    fclose(file);
    CHECK(strcmp(expected, transcript) == 0);
    if (strcmp(expected, transcript) != 0) fprintf(stderr, "expected:\n%s\nactual:\n%s\n", expected, transcript);
  }
  qa_app_native_renderer_destroy(e.renderer);
  qa_app_application_audio_destroy(e.audio);
  qa_app_application_assets_destroy(e.assets);
  qa_app_loaded_content_close(e.content);
  qa_app_loaded_content_destroy(e.content);
  qa_identity_owner_destroy(&ids);
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
