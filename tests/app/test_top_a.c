/* Port of key cases from tests/app/ (base-arena-progression,
 * base-arena-selection, gtv-commands, demo-playback, demo-recording)
 * plus frame clock/time for the w7-top-a slice. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "qa/app/bootstrap/audio_settings.h"
#include "qa/app/bootstrap/base_arena_catalog.h"
#include "qa/app/bootstrap/base_arena_postgame.h"
#include "qa/app/bootstrap/base_arena_progression.h"
#include "qa/app/bootstrap/base_arena_selection.h"
#include "qa/app/bootstrap/component_bodies.h"
#include "qa/app/bootstrap/cvar_archives.h"
#include "qa/app/bootstrap/demo_playback.h"
#include "qa/app/bootstrap/demo_recording.h"
#include "qa/app/bootstrap/diagnostic_tools.h"
#include "qa/app/bootstrap/frame_clock.h"
#include "qa/app/bootstrap/frame_time.h"
#include "qa/app/bootstrap/gtv_commands.h"
#include "qa/contracts/identity.h"
#include "qa/core/cvars/index.h"
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
#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
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
static bool text_eq(const char *a, const char *b) {
  return (a == NULL && b == NULL) || (a != NULL && b != NULL && strcmp(a, b) == 0);
}
static qa_identity_owner owner;
static qa_cvar_registry *make_cvars(qa_command_dialect dialect) {
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = owner.session;
  context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context = context;
  return qa_cvar_registry_create(&options);
}
static void test_frame_clock_and_time(void) {
  qa_app_presentation_time clock;
  qa_app_presentation_time_init(&clock);
  CHECK_ABORTS(qa_app_presentation_time_milliseconds(&clock));
  qa_app_presentation_time_advance(&clock, 1000, 16, 16);
  CHECK(qa_app_presentation_time_milliseconds(&clock) == 1000);
  qa_app_presentation_time_advance(&clock, 2000, 100, 8);
  CHECK(qa_app_presentation_time_milliseconds(&clock) == 1008);
  qa_app_frame_time_controls q1 = {2, 0, 0, 0};
  CHECK(qa_app_source_frame_milliseconds(QA_COMMAND_DIALECT_Q1_NETQUAKE, 16, q1, (qa_app_frame_host){false, true}) == 32);
  CHECK(qa_app_source_frame_milliseconds(QA_COMMAND_DIALECT_Q1_NETQUAKE, 90, q1, (qa_app_frame_host){false, true}) == 100);
  qa_app_frame_time_controls q1_fixed = {0, 0, 0.05, 0};
  CHECK(qa_app_source_frame_milliseconds(QA_COMMAND_DIALECT_Q1_QUAKEWORLD, 16, q1_fixed, (qa_app_frame_host){false, true}) == 50);
  qa_app_frame_time_controls q3 = {1, 5, 0, 0};
  CHECK(qa_app_source_frame_milliseconds(QA_COMMAND_DIALECT_Q3, 16.9, q3, (qa_app_frame_host){false, true}) == 5);
  CHECK_ABORTS(qa_app_source_frame_milliseconds(QA_COMMAND_DIALECT_Q3, -1, q3, (qa_app_frame_host){false, true}));
  qa_app_capture_frame_result frame = qa_app_source_capture_frame(16, 30, 1, true, false);
  CHECK(frame.capture && frame.milliseconds == 33);
  CHECK(qa_app_source_capture_frame(16, 0, 1, true, true).capture == false);
}
static qa_app_arena_result base_result(void) {
  return (qa_app_arena_result){0, 2, 1, 55, 2, 1, 0, 99, false};
}
static void test_progression_and_postgame(void) {
  qa_cvar_registry *cvars = make_cvars(QA_COMMAND_DIALECT_Q3);
  qa_app_base_arena_progression *progress = qa_app_base_arena_progression_create(cvars, (qa_app_arena_progression_catalog){4, 6, 4, 5});
  CHECK(qa_app_base_arena_progression_current_level(progress) == 4);
  qa_app_arena_result result = base_result();
  result.level = 4;
  qa_app_arena_progression_result recorded = qa_app_base_arena_progression_record(progress, result);
  CHECK(recorded.unlocked_movie == 1);
  free(recorded.awards);
  CHECK(qa_app_base_arena_progression_current_level(progress) == 0);
  result = base_result();
  recorded = qa_app_base_arena_progression_record(progress, result);
  free(recorded.awards);
  result.skill = 5;
  recorded = qa_app_base_arena_progression_record(progress, result);
  free(recorded.awards);
  qa_app_arena_best best = qa_app_base_arena_progression_best(progress, 0);
  CHECK(best.rank == 1 && best.skill == 5);
  CHECK(qa_app_base_arena_progression_award(progress, 0) >= 1);
  result = base_result();
  result.level = 99;
  CHECK_ABORTS(qa_app_base_arena_progression_record(progress, result));
  const char *args[] = {"2", "4", "51", "3", "2", "1", "101", "1", "4", "64", "10", "8", "1", "9"};
  qa_app_arena_postgame game = qa_app_parse_arena_postgame(args, 14, 0, 3);
  CHECK(game.result.rank == 1 && game.result.perfect && game.player_count == 2);
  CHECK(game.players[0].client == 4 && game.players[1].client == 8);
  qa_app_base_arena_progression *fresh_owner = qa_app_base_arena_progression_create(make_cvars(QA_COMMAND_DIALECT_Q3), (qa_app_arena_progression_catalog){4, 6, 4, 5});
  qa_app_arena_progression_result scored = qa_app_base_arena_progression_record(fresh_owner, game.result);
  qa_app_arena_postgame_presentation view = qa_app_arena_postgame_presentation_make(&game, scored);
  CHECK(text_eq(view.music_command, "music music/win") && view.winner_announcement_after_milliseconds == 1000);
  CHECK(view.podium_count == 2 && view.podium[0].client == 4);
  free(scored.awards);
  free(game.players);
  qa_app_base_arena_progression_destroy(progress);
  qa_app_base_arena_progression_destroy(fresh_owner);
  qa_cvar_registry_destroy(cvars);
}
static void test_catalog_and_selection(void) {
  const char *texts[] = {"{ map q3dm0 longname Training type single special training bots Crash }\n"
      "{ map q3dm1 longname \"Arena one\" type single bots \"Sarge Grunt\" fraglimit 15 }\n"
      "{ map q3dm2 type single } { map q3dm3 type single } { map q3dm4 type single }\n"
      "{ map q3tourney6 type single special final bots Xaero }"};
  qa_app_base_arena_catalog catalog = qa_app_parse_base_arena_catalog(texts, 1);
  CHECK(catalog.regular_count == 4 && catalog.tier_count == 1 && catalog.arena_count == 6);
  const qa_app_base_arena *arena = qa_app_base_arena_for_map(&catalog, "q3dm1");
  CHECK(text_eq(arena->title, "Arena one") && arena->frag_limit == 15 && arena->bot_count == 2);
  CHECK_ABORTS(qa_app_base_arena_for_map(&catalog, "nope"));
  qa_cvar_registry *cvars = make_cvars(QA_COMMAND_DIALECT_Q3);
  qa_app_base_arena_progression *progress = qa_app_base_arena_progression_create(cvars, (qa_app_arena_progression_catalog){4, 6, 4, 5});
  qa_app_arena_selection locked = qa_app_arena_selection_make(&catalog, progress, "");
  CHECK(locked.tier_count == 3 && text_eq(locked.tiers[0].label, "Training") && text_eq(locked.tiers[1].label, "Tier 1"));
  CHECK(text_eq(locked.current, "maps/q3dm0.bsp"));
  size_t available = 0;
  for (size_t i = 0; i < locked.row_count; i++) if (locked.rows[i].available) available++;
  CHECK(available == 1);
  qa_app_arena_selection_destroy(&locked);
  { qa_cvar_snapshot snap; if (qa_cvar_registry_set(cvars, "g_spScores3", "\\l4\\1", true, &snap)) qa_cvar_read_free(&snap); }
  qa_app_arena_selection unlocked = qa_app_arena_selection_make(&catalog, progress, "-4");
  CHECK(text_eq(unlocked.current, "maps/q3dm0.bsp"));
  qa_app_arena_selection_destroy(&unlocked);
  qa_app_base_arena_progression_destroy(progress);
  qa_cvar_registry_destroy(cvars);
  qa_app_base_arena_catalog_destroy(&catalog);
}
static void test_gtv(void) {
  const char *args[] = {"-n", "Match one", "--user", "viewer", "-p", "two words", "127.0.0.1:27911"};
  qa_app_gtv_connect_request request;
  CHECK(qa_app_parse_gtv_connect(args, 7, "default", "default", &request));
  CHECK(text_eq(request.address, "127.0.0.1:27911") && text_eq(request.username, "viewer"));
  CHECK(text_eq(request.password, "two words") && text_eq(request.label, "Match one"));
  const char *bad[] = {"-p"};
  CHECK_ABORTS(qa_app_parse_gtv_connect(bad, 1, "", "", &request));
  qa_app_gtv_current current = {3, "Match one"};
  CHECK(qa_app_matches_gtv_disconnect(NULL, 0, &current));
  const char *by_id[] = {"3"};
  CHECK(qa_app_matches_gtv_disconnect(by_id, 1, &current));
  const char *wrong[] = {"2"};
  CHECK_ABORTS(qa_app_matches_gtv_disconnect(wrong, 1, &current));
  qa_cvar_registry *cvars = make_cvars(QA_COMMAND_DIALECT_Q2_CLASSIC);
  qa_app_register_gtv_cvars(cvars);
  qa_cvar_read read;
  CHECK(qa_cvar_registry_find(cvars, "mvd_password", &read) && (read.flags & QA_Q2_CVAR_FLAG_PRIVATE) != 0);
  qa_cvar_read_free(&read);
  qa_cvar_registry_destroy(cvars);
}
static bool read_demo(void *ctx, const char *path, uint8_t **out, size_t *count) {
  const char **seen = ctx;
  *seen = path;
  if (strstr(path, "demo1") == NULL) return false;
  if (strstr(path, ".dm_") != NULL && strstr(path, "68") == NULL) return false;
  *out = malloc(3);
  (*out)[0] = 1; (*out)[1] = 2; (*out)[2] = 3;
  *count = 3;
  return true;
}
static void print_noop(void *ctx, const char *text) { (void)ctx; (void)text; }
static void test_demo_playback_and_recording(void) {
  CHECK(qa_app_demo_family_of("test.qwd", QA_APP_DEMO_Q3) == QA_APP_DEMO_QW);
  CHECK(qa_app_demo_family_of("test.dem", QA_APP_DEMO_Q2) == QA_APP_DEMO_Q1);
  CHECK(qa_app_demo_family_of("test.dm_68", QA_APP_DEMO_Q1) == QA_APP_DEMO_Q3);
  CHECK(qa_app_demo_family_of("test", QA_APP_DEMO_QW) == QA_APP_DEMO_QW);
  const char *seen = NULL;
  qa_app_demo_reader reader = {&seen, read_demo, print_noop};
  qa_app_demo_resource q2 = qa_app_open_demo_resource((qa_app_demo_request){QA_APP_DEMO_Q2, "demo1", false}, reader);
  CHECK(text_eq(q2.path, "demos/demo1.dm2") && q2.byte_count == 3);
  qa_app_demo_resource_destroy(&q2);
  qa_app_demo_resource q3 = qa_app_open_demo_resource((qa_app_demo_request){QA_APP_DEMO_Q3, "demo1", false}, reader);
  CHECK(text_eq(q3.path, "demos/demo1.dm_68") && q3.protocol == 68);
  qa_app_demo_resource_destroy(&q3);
  qa_app_demo_recording_identity identity = {QA_APP_RECORDING_Q3, 68, {0}, 0};
  char *path = qa_app_recording_path("/tmp/recordings", "demo1", identity);
  CHECK(text_eq(path, "/tmp/recordings/demos/demo1.dm_68"));
  free(path);
  identity = (qa_app_demo_recording_identity){QA_APP_RECORDING_Q1, 15, {0}, -1};
  path = qa_app_recording_path("/tmp/recordings", "demo1.dem", identity);
  CHECK(text_eq(path, "/tmp/recordings/demo1.dem"));
  free(path);
}
static void test_archives_audio_bodies(void) {
  qa_config_store store;
  CHECK(qa_config_store_init(&store, "/tmp/top-a-settings"));
  qa_cvar_registry *cvars = make_cvars(QA_COMMAND_DIALECT_Q3);
  { qa_cvar_snapshot snap; if (qa_cvar_registry_register(cvars, "name", "player", QA_CVAR_FLAG_ARCHIVE, &snap)) qa_cvar_read_free(&snap); }
  { qa_cvar_snapshot snap; if (qa_cvar_registry_set(cvars, "name", "ranger", false, &snap)) qa_cvar_read_free(&snap); }
  const char *owner_path[] = {"client", "q3:test", "q3:official", "0"};
  qa_app_save_cvar_archive(&store, owner_path, 4, cvars, NULL, 0);
  size_t count = 0;
  qa_cvar_archive_entry *entries = qa_app_load_cvar_archive(&store, owner_path, 4, QA_COMMAND_DIALECT_Q3, &count);
  bool found = false;
  for (size_t i = 0; i < count; i++) if (text_eq(entries[i].name, "name") && text_eq(entries[i].value, "ranger")) found = true;
  CHECK(found);
  qa_app_cvar_archive_entries_free(entries, count);
  qa_app_audio_settings_input input = {0};
  input.effects_volume = 0.5;
  input.music_volume = 0.25;
  qa_app_save_audio_settings(&store, input);
  qa_app_audio_preferences preferences;
  CHECK(qa_app_load_audio_settings(&store, &preferences));
  CHECK(preferences.effects_volume == 0.5 && preferences.music_volume == 0.25 && preferences.device_name == NULL);
  free(preferences.menu_track);
  free(preferences.device_name);
  qa_app_body_material material = {0};
  material.shader_rgba = (qa_vec4){1, 1, 1, 1};
  qa_app_component_body_part parts[] = {{"body", true, &material, 1}, {"head", true, &material, 1}};
  qa_app_component_body body = {0};
  body.parts = parts;
  body.part_count = 2;
  const qa_app_body_material *out[4];
  /* Equal passes from distinct parts collapse to one. */
  CHECK(qa_app_body_materials(&body, "body", out, 4) == 1);
  CHECK(qa_app_body_base_visible(&body, 1, body.actor, "head"));
  qa_cvar_registry_destroy(cvars);
  qa_config_store_destroy(&store);
}
int main(void) {
  if (!qa_identity_owner_create(&owner, "top-a")) return 1;
  test_frame_clock_and_time();
  test_progression_and_postgame();
  test_catalog_and_selection();
  test_gtv();
  test_demo_playback_and_recording();
  test_archives_audio_bodies();
  qa_identity_owner_destroy(&owner);
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
