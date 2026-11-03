/* TS drift packet D2 (src/app/bootstrap, 2026-11-03): checks for behaviour the TS changed
 * after the C port's baseline. Expected values come from bun
 * (tests/app/oracles/d2_drift_oracle.ts) and from tests/app/cvar-archive-data.test.ts,
 * tests/app/audit-app-fixes.test.ts and tests/first-playable/application/frame-time.test.ts.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/app/bootstrap/audio_settings.h"
#include "qa/app/bootstrap/cvar_archives.h"
#include "qa/app/bootstrap/demo_recording.h"
#include "qa/app/bootstrap/diagnostic_tools.h"
#include "qa/app/bootstrap/frame_time.h"
#include "qa/app/bootstrap/menu_art.h"
#include "qa/app/bootstrap/network/client_download_policy.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/server_map.h"
#include "qa/app/bootstrap/view_settings.h"
#include "qa/foundation/support.h"
#include "qa/settings/config.h"

static int checks, failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)
#define CHECK_STR(actual, expected)                                     \
  do {                                                                  \
    const char *a_ = (actual), *e_ = (expected);                        \
    checks++;                                                           \
    if (a_ == NULL || strcmp(a_, e_) != 0) {                            \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: got \"%s\", want \"%s\"\n", __FILE__, __LINE__, a_ ? a_ : "(null)", e_); \
    }                                                                   \
  } while (0)

static qa_cvar_registry *registry(qa_command_dialect dialect) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  return qa_cvar_registry_create(&options);
}
static void set_cvar(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(cvars, name, value, true, &snapshot)) qa_cvar_read_free(&snapshot);
}

/* ---- cvar-archives.ts archiveDataText (tests/app/cvar-archive-data.test.ts + oracle) ---- */
static bool is_quit(void *ctx, const char *name) { (void)ctx; return strcmp(name, "quit") == 0; }
static bool never(void *ctx, const char *name) { (void)ctx; (void)name; return false; }
static void test_archive_data_text(void) {
  const char *text =
      "// Generated\r\n"
      "seta cl_maxpackets \"30\"\r\n"
      "bind w \"+forward\"\r\n"
      "bind a \"+left; +attack\"\r\n"
      "sensitivity \"3\"\r\n"
      "quit \"1\"\r\n"
      "exec evil.cfg\r\n"
      "seta x \"1\"; exec evil.cfg\r\n"
      "seta y \"unterminated\r\n"
      "alias z \"quit\"\r\n"
      "unbindall";
  char *kept = qa_app_archive_data_text(text, is_quit, NULL);
  CHECK_STR(kept, "seta cl_maxpackets \"30\"\nbind w \"+forward\"\nbind a \"+left; +attack\"\nsensitivity \"3\"\nunbindall\n");
  free(kept);
  kept = qa_app_archive_data_text("", never, NULL);
  CHECK_STR(kept, "");
  free(kept);
  /* oracle archive2 */
  kept = qa_app_archive_data_text("  SETA  a \"1\"  \r\rBIND x y\n\tname  \"v\"\nname \"a\" \"b\"\n//c\nset q \"1\"\"\nunbind k", never, NULL);
  CHECK_STR(kept, "SETA  a \"1\"\nBIND x y\nname  \"v\"\nunbind k\n");
  free(kept);
}

/* ---- cvar-archives.ts loadCvarArchive: a malformed archive restores nothing (oracle archiveDup) ---- */
static void test_archive_malformed(void) {
  char root[] = "/tmp/d2-archive-XXXXXX";
  CHECK(mkdtemp(root) != NULL);
  qa_config_store store;
  CHECK(qa_config_store_init(&store, root));
  CHECK(qa_config_store_dump(&store, "cvars/client/x.json",
      "{\"version\":1,\"dialect\":\"q3\",\"entries\":[{\"name\":\"a\",\"value\":\"1\"},{\"name\":\"a\",\"value\":\"2\"}]}"));
  const char *owner[] = {"client", "x"};
  size_t count = 99;
  qa_cvar_archive_entry *entries = qa_app_load_cvar_archive(&store, owner, 2, QA_COMMAND_DIALECT_Q3, &count);
  CHECK(entries == NULL && count == 0);
  CHECK(qa_config_store_dump(&store, "cvars/client/x.json", "{\"version\":1,\"dialect\":\"q2-classic\",\"entries\":[]}"));
  entries = qa_app_load_cvar_archive(&store, owner, 2, QA_COMMAND_DIALECT_Q3, &count);
  CHECK(entries == NULL && count == 0);
  qa_config_store_destroy(&store);
}

/* ---- options.ts (tests/app/audit-app-fixes.test.ts + oracle) ---- */
typedef struct parse_call { const char *const *argv; size_t argc; qa_app_application_command command; } parse_call;
static void parse_body(void *ctx) {
  parse_call *call = ctx;
  qa_content_root_search search = {0};
  search.own_directory = "/tmp";
  search.home_directory = "/tmp";
  call->command = qa_app_parse_application_command(call->argv, call->argc, search, NULL);
}
static void check_rejected(const char *const *argv, size_t argc, const char *message) {
  parse_call call = {argv, argc, {0}};
  qa_caught_error caught;
  bool ok = qa_try(parse_body, &call, &caught);
  CHECK(!ok);
  if (ok) qa_app_application_command_destroy(&call.command);
  else CHECK_STR(caught.message, message);
}
static void test_options(void) {
#define ROOT "--content-root", "/nonexistent-content-root"
  const char *a[] = {ROOT, "--game", "--dedicated"};
  check_rejected(a, 4, "Missing value for --game");
  const char *b[] = {ROOT, "--connect-q2", "--seats", "2"};
  check_rejected(b, 5, "Missing value for --connect-q2");
  const char *c[] = {ROOT, "--listen", "27500", "--bind", "not a host"};
  check_rejected(c, 6, "Invalid bind address: not a host");
  const char *d[] = {ROOT, "--dedicated", "--connect-q2", "127.0.0.1"};
  check_rejected(d, 5, "A dedicated server cannot connect to a remote server");
  const char *e[] = {ROOT, "--mode", "singleplayer", "--seats", "2"};
  check_rejected(e, 6, "--mode singleplayer conflicts with --seats above 1 and with network options");
  const char *f[] = {ROOT, "--mode", "singleplayer", "--listen", "27500"};
  check_rejected(f, 6, "--mode singleplayer conflicts with --seats above 1 and with network options");
  const char *g[] = {ROOT, "--connect-q2", "127.0.0.1", "--bot-skill", "3"};
  check_rejected(g, 6, "--bot-skill sets bot difficulty on the host and cannot be used when joining a server");
  const char *h[] = {ROOT, "--vm-game", "3"};
  check_rejected(h, 4, "--vm-game requires 0 (native module), 1 (interpreted) or 2 (compiled)");
  parse_call ok = {(const char *const[]){ROOT, "--mode", "singleplayer", "--map", "e1m1"}, 6, {0}};
  qa_caught_error caught;
  CHECK(qa_try(parse_body, &ok, &caught));
  CHECK_STR(ok.command.kind, "run");
  qa_app_application_command_destroy(&ok.command);
  parse_call vm = {(const char *const[]){ROOT, "--vm-game", "0", "--map", "q3dm1"}, 6, {0}};
  CHECK(qa_try(parse_body, &vm, &caught));
  CHECK(vm.command.options.has_vm_game && vm.command.options.vm_game == 0);
  qa_app_application_command_destroy(&vm.command);
  parse_call bind = {(const char *const[]){ROOT, "--listen", "27500", "--bind", "[::1]"}, 6, {0}};
  CHECK(qa_try(parse_body, &bind, &caught));
  CHECK_STR(bind.command.options.network_kind, "native-server");
  CHECK_STR(bind.command.options.network_host, "[::1]");
  CHECK(bind.command.options.network_port == 27500);
  qa_app_application_command_destroy(&bind.command);
#undef ROOT
}

/* ---- server-map.ts (audit-app-fixes) ---- */
static void escape_body(void *ctx) { free(qa_app_server_map_path(ctx)); }
static void test_server_map(void) {
  char *path = qa_app_server_map_path("q3dm17.v2");
  CHECK_STR(path, "maps/q3dm17.v2.bsp");
  free(path);
  path = qa_app_server_map_path("maps\\ctf-2.fort.bsp");
  CHECK_STR(path, "maps/ctf-2.fort.bsp");
  free(path);
  qa_caught_error caught;
  CHECK(!qa_try(escape_body, "../escape", &caught));
  CHECK_STR(caught.message, "Invalid relative resource path: ../escape");
}

/* ---- audio-settings.ts / view-settings.ts warnings (audit-app-fixes + oracle warnings) ---- */
typedef struct warnings { char text[2048]; } warnings;
static void warn(void *ctx, const char *text) {
  warnings *w = ctx;
  strncat(w->text, text, sizeof w->text - strlen(w->text) - 1);
}
static void view_changed(void *ctx, double value) { (void)ctx; (void)value; }
static void test_preference_warnings(void) {
  char root[] = "/tmp/d2-prefs-XXXXXX";
  CHECK(mkdtemp(root) != NULL);
  qa_config_store store;
  CHECK(qa_config_store_init(&store, root));
  warnings w = {""};
  CHECK(qa_config_store_dump(&store, "audio.json", "{\"version\":1,\"deviceName\":null,\"effectsVolume\":5,\"musicVolume\":0.5}"));
  CHECK(qa_config_store_dump(&store, "view.json", "{\"version\":1,\"fieldOfView\":170}"));
  qa_app_audio_preferences audio;
  CHECK(!qa_app_load_audio_settings_warn(&store, &audio, warn, &w));
  qa_app_view_settings *view = qa_app_view_settings_create(view_changed, NULL);
  qa_app_view_settings_load_warn(view, &store, warn, &w);
  CHECK(qa_app_view_settings_field_of_view(view) == 90);
  CHECK(qa_config_store_dump(&store, "view.json", "{\"version\":2,\"fieldOfView\":100}"));
  qa_app_view_settings_load_warn(view, &store, warn, &w);
  CHECK(qa_config_store_dump(&store, "view.json", "12"));
  qa_app_view_settings_load_warn(view, &store, warn, &w);
  CHECK(qa_config_store_dump(&store, "view.json", "{not json"));
  qa_app_view_settings_load_warn(view, &store, warn, &w);
  CHECK(qa_app_view_settings_field_of_view(view) == 90);
  CHECK(qa_config_store_dump(&store, "audio.json",
      "{\"version\":1,\"deviceName\":null,\"effectsVolume\":0.5,\"musicVolume\":0.5,\"outputFormat\":{\"sampleRate\":4,\"channels\":1,\"sampleBits\":8}}"));
  CHECK(!qa_app_load_audio_settings_warn(&store, &audio, warn, &w));
  CHECK_STR(w.text,
      "Ignoring audio.json: Invalid audio preferences\n"
      "Ignoring view.json: Field of view must be between 60 and 160 degrees\n"
      "Ignoring view.json: Invalid view preferences\n"
      "Ignoring view.json: Invalid view preferences\n"
      "Ignoring audio.json: Audio output requires 8000\xe2\x80\x93" "192000 Hz, 1 or 2 channels, and 8 or 16 bits\n");
  /* outputFormat presence is tracked (TS outputFormat !== undefined) */
  CHECK(qa_config_store_dump(&store, "audio.json", "{\"version\":1,\"deviceName\":null,\"effectsVolume\":0.5,\"musicVolume\":0.5}"));
  CHECK(qa_app_load_audio_settings(&store, &audio));
  CHECK(!audio.has_output_format);
  free(audio.menu_track); free(audio.device_name);
  CHECK(qa_config_store_dump(&store, "audio.json",
      "{\"version\":1,\"deviceName\":null,\"effectsVolume\":0.5,\"musicVolume\":0.5,\"outputFormat\":{\"sampleRate\":22050,\"channels\":2,\"sampleBits\":16}}"));
  CHECK(qa_app_load_audio_settings(&store, &audio));
  CHECK(audio.has_output_format && audio.output_format.sample_rate == 22050);
  free(audio.menu_track); free(audio.device_name);
  qa_app_view_settings_destroy(view);
  qa_config_store_destroy(&store);
}

/* ---- client-download-policy.ts cl_http_package_downloads (oracle pkg*) ---- */
static void test_package_downloads(void) {
  qa_cvar_registry *cvars = registry(QA_COMMAND_DIALECT_Q2_CLASSIC);
  qa_app_client_download_permission *permitted = qa_app_create_client_download_permission(cvars, false);
  CHECK(qa_app_client_download_permitted(permitted, QA_APP_DL_HTTP, QA_APP_DL_PACKAGE));
  set_cvar(cvars, "cl_http_package_downloads", "0");
  CHECK(!qa_app_client_download_permitted(permitted, QA_APP_DL_HTTP, QA_APP_DL_PACKAGE));
  CHECK(qa_app_client_download_permitted(permitted, QA_APP_DL_NATIVE, QA_APP_DL_PACKAGE));
  CHECK(qa_app_client_download_permitted(permitted, QA_APP_DL_HTTP, QA_APP_DL_MAP));
  qa_app_client_download_permission_destroy(permitted);
  qa_cvar_registry_destroy(cvars);
}

/* ---- frame-time.ts (frame-time.test.ts + oracle) ---- */
static void test_frame_time(void) {
  qa_app_frame_time_controls defaults = {1, 0, 0, 0};
  qa_app_frame_host local = {false, true}, dedicated = {true, true}, remote = {false, false};
  CHECK(qa_app_source_frame_milliseconds(QA_COMMAND_DIALECT_Q2_CLASSIC, 5000, defaults, local) == 200);
  CHECK(qa_app_source_frame_milliseconds(QA_COMMAND_DIALECT_Q2_RERELEASE, 5000, defaults, local) == 200);
  CHECK(qa_app_source_frame_milliseconds(QA_COMMAND_DIALECT_Q2_RERELEASE, 9000, defaults, dedicated) == 5000);
  qa_app_frame_time_controls fixed = {1, 25, 0, 0};
  CHECK(qa_app_source_frame_milliseconds(QA_COMMAND_DIALECT_Q2_CLASSIC, 5000, fixed, local) == 25);
  CHECK(qa_app_source_frame_milliseconds(QA_COMMAND_DIALECT_Q3, 9000, defaults, local) == 200);
  CHECK(qa_app_source_frame_milliseconds(QA_COMMAND_DIALECT_Q3, 9000, defaults, remote) == 5000);
  /* oracle q3overflow: an overflowing timescale stops the clock, then the floor of 1 applies */
  qa_app_frame_time_controls huge = {1e30, 0, 0, 0}, negative = {-1e30, 0, 0, 0};
  CHECK(qa_app_source_frame_milliseconds(QA_COMMAND_DIALECT_Q3, 16, huge, local) == 1);
  CHECK(qa_app_source_frame_milliseconds(QA_COMMAND_DIALECT_Q3, 16, negative, local) == 1);
  /* Q1 pacing with registered sys_ticrate (oracle q1min*) */
  const qa_command_dialect q1_dialects[] = {QA_COMMAND_DIALECT_Q1_NETQUAKE, QA_COMMAND_DIALECT_Q1_QUAKEWORLD};
  for (size_t i = 0; i < 2; i++) {
    qa_cvar_registry *cvars = registry(q1_dialects[i]);
    qa_app_register_frame_time_cvars(cvars);
    CHECK(qa_app_minimum_frame_milliseconds(cvars, false) == 13.88888888888889);
    CHECK(qa_app_minimum_frame_milliseconds(cvars, true) == 50.00000074505806);
    set_cvar(cvars, "sys_ticrate", "0.1");
    CHECK(qa_app_minimum_frame_milliseconds(cvars, true) == 100.00000149011612);
    set_cvar(cvars, "sys_ticrate", "0");
    CHECK(qa_app_minimum_frame_milliseconds(cvars, true) == 13.88888888888889);
    qa_cvar_registry_destroy(cvars);
  }
  const qa_command_dialect others[] = {QA_COMMAND_DIALECT_Q2_CLASSIC, QA_COMMAND_DIALECT_Q2_RERELEASE, QA_COMMAND_DIALECT_Q3};
  for (size_t i = 0; i < 3; i++) {
    qa_cvar_registry *cvars = registry(others[i]);
    qa_app_register_frame_time_cvars(cvars);
    CHECK(qa_app_minimum_frame_milliseconds(cvars, false) == 4);
    CHECK(qa_app_minimum_frame_milliseconds(cvars, true) == 4);
    qa_cvar_registry_destroy(cvars);
  }
  CHECK(qa_app_minimum_frame_milliseconds(NULL, true) == 4);
}

/* ---- diagnostic-tools.ts sourceCaptureFrame (oracle capture*) ---- */
static void capture_body(void *ctx) { (void)ctx; (void)qa_app_source_capture_frame(-1, -1, 1, true, false); }
static void test_capture_frame(void) {
  qa_app_capture_frame_result frame = qa_app_source_capture_frame(16, -1, 1, true, false);
  CHECK(frame.milliseconds == 16 && !frame.capture);
  frame = qa_app_source_capture_frame(16, 30, -2, true, false);
  CHECK(frame.milliseconds == 16 && !frame.capture);
  frame = qa_app_source_capture_frame(16, 1.0 / 0.0, 1, true, false);
  CHECK(frame.milliseconds == 16 && !frame.capture);
  qa_caught_error caught;
  CHECK(!qa_try(capture_body, NULL, &caught));
  CHECK_STR(caught.message, "Invalid capture clock");
}

/* ---- demo-recording.ts open(..., replace) ---- */
static void test_demo_replace(void) {
  char root[] = "/tmp/d2-demo-XXXXXX";
  CHECK(mkdtemp(root) != NULL);
  const uint8_t message[] = {1, 2, 3, 4};
  qa_app_demo_recording_packet packet;
  memset(&packet, 0, sizeof packet);
  packet.kind = QA_APP_RECORDING_Q3;
  packet.message = message; packet.message_length = sizeof message;
  qa_app_demo_recording_seed seed;
  memset(&seed, 0, sizeof seed);
  seed.identity.kind = QA_APP_RECORDING_Q3;
  seed.identity.protocol = 68;
  seed.packets = &packet; seed.packet_count = 1;
  const char *error = NULL;
  qa_app_demo_recording *first = qa_app_demo_recording_open(root, "typed", seed, false, &error);
  CHECK(first != NULL && error == NULL);
  if (first != NULL) { (void)qa_app_demo_recording_stop(first); qa_app_demo_recording_destroy(first); }
  qa_app_demo_recording *again = qa_app_demo_recording_open(root, "typed", seed, false, &error);
  CHECK(again == NULL && error != NULL && strstr(error, "EEXIST") != NULL);
  qa_app_demo_recording *replaced = qa_app_demo_recording_open(root, "typed", seed, true, &error);
  CHECK(replaced != NULL && error == NULL);
  if (replaced != NULL) { (void)qa_app_demo_recording_stop(replaced); qa_app_demo_recording_destroy(replaced); }
}

/* ---- menu-art.ts: title.png joins the embedded artwork ---- */
static void unknown_art_body(void *ctx) { qa_image_level level; qa_app_load_menu_art_image(ctx, &level); }
static void test_menu_art(void) {
  qa_caught_error caught;
  CHECK(!qa_try(unknown_art_body, "assets/ui/unknown.png", &caught));
  CHECK_STR(caught.message, "Unknown menu artwork: assets/ui/unknown.png");
  if (access("../quake-anthology-ts/assets/ui/title.png", R_OK) != 0) return;
  qa_image_level level;
  memset(&level, 0, sizeof level);
  qa_app_load_menu_art_image("assets/ui/title.png", &level);
  CHECK(level.width > 0 && level.height > 0 && level.pixels.length == (size_t)(level.width * level.height * 4));
  free((void *)level.pixels.bytes);
  qa_app_load_menu_art_image("assets/ui/menu-panel.png", &level);
  CHECK(level.width == 1254 && level.height == 1254);
  free((void *)level.pixels.bytes);
}

int main(void) {
  test_archive_data_text();
  test_archive_malformed();
  test_options();
  test_server_map();
  test_preference_warnings();
  test_package_downloads();
  test_frame_time();
  test_capture_frame();
  test_demo_replace();
  test_menu_art();
  printf("test_d2_drift: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
