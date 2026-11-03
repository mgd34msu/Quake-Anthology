/* Port of key cases from tests/app/ (match-modes, player-progress,
 * q2-damage-blend, startup options, loading) for the w7-top-b slice.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "qa/app/bootstrap/loading.h"
#include "qa/app/bootstrap/local_lobby.h"
#include "qa/app/bootstrap/match_modes.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/player_progress.h"
#include "qa/app/bootstrap/presentation.h"
#include "qa/app/bootstrap/q1_session_actions.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/app/bootstrap/q2_damage_blend.h"
#include "qa/app/bootstrap/q2_travel.h"
#include "qa/app/bootstrap/q3_map_command.h"
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
static void test_match_modes(void) {
  qa_app_match_mode_selection horde = {QA_GAME_FAMILY_Q1, "", "", "deathmatch", "horde"};
  CHECK(text_eq(qa_app_match_mode_unavailable(horde), "Horde requires single player or cooperative mode"));
  qa_app_match_mode_selection ctf = {QA_GAME_FAMILY_Q1, "", "", "deathmatch", "ctf"};
  CHECK(qa_app_match_mode_unavailable(ctf) == NULL);
  qa_app_match_mode_selection tag_q3 = {QA_GAME_FAMILY_Q3, "", "", "deathmatch", "tag"};
  /* tag/deathball are family-agnostic in the TS switch. */
  CHECK(qa_app_match_mode_unavailable(tag_q3) == NULL);
  qa_app_match_mode_selection horde_q3 = {QA_GAME_FAMILY_Q3, "", "", "deathmatch", "horde"};
  CHECK(qa_app_match_mode_unavailable(horde_q3) == NULL);
  char scratch[512];
  const char *names[] = {"worldspawn"};
  qa_app_match_mode_selection dm = {QA_GAME_FAMILY_Q2, "", "", "deathmatch", "standard"};
  CHECK(text_eq(qa_app_match_map_unavailable(dm, names, 1, scratch, sizeof scratch),
      "Deathmatch requires an authored player spawn"));
  const char *with_spawn[] = {"worldspawn", "info_player_deathmatch"};
  CHECK(qa_app_match_map_unavailable(dm, with_spawn, 2, scratch, sizeof scratch) == NULL);
}
static void test_travel(void) {
  qa_app_q2_travel_target *target = qa_app_parse_q2_travel("*base1$start+boss2");
  CHECK(strcmp(target->kind, "map") == 0 && strcmp(target->name, "base1") == 0 && target->new_unit);
  CHECK(strcmp(target->spawn_point, "start") == 0);
  CHECK(target->next != NULL && strcmp(target->next->name, "boss2") == 0);
  char *command = qa_app_q2_next_server_command(target);
  CHECK(strcmp(command, "gamemap \"boss2\"") == 0);
  free(command);
  qa_app_q2_travel_target_destroy(target);
  CHECK_ABORTS(qa_app_parse_q2_travel(""));
  CHECK_ABORTS(qa_app_parse_q2_travel("base 1"));
}
static void test_damage_blend(void) {
  qa_vec4 previous = {1, 0, 0, 0.5}, current = {0, 1, 0, 1};
  qa_vec4 mid = qa_app_interpolate_q2_damage_blend(&previous, current, 0.5);
  CHECK(mid.x == 0.5f && mid.y == 0.5f && mid.w == 0.75f);
  qa_vec4 empty = {1, 1, 1, 0};
  qa_vec4 untouched = qa_app_interpolate_q2_damage_blend(&empty, current, 0.5);
  CHECK(untouched.x == current.x && untouched.y == current.y && untouched.w == current.w);
  qa_draw_batch batch;
  qa_render_vertex vertices[8];
  double indices[24];
  qa_renderer_image white = {0};
  size_t count = qa_app_prepare_q2_damage_blend((qa_vec4){1, 0, 0, 0.5},
      (qa_rect){0, 0, 640, 480}, white, 0.25, &batch, vertices, indices);
  CHECK(count == 1 && batch.vertex_count == 8 && batch.indices.count == 24);
  CHECK(count == 1 && vertices[0].color.w > 0.49 && vertices[0].color.w < 0.51);
  CHECK(qa_app_prepare_q2_damage_blend(empty, (qa_rect){0, 0, 640, 480}, white, 0, &batch, vertices, indices) == 0);
}
static void test_options(void) {
  qa_content_root_search search = {0};
  search.own_directory = "/tmp";
  search.home_directory = "/tmp";
  const char *argv[] = {"--game", "q1-rerelease-id1", "--map", "e1m2", "--width", "1280", "--mode", "coop"};
  qa_app_application_command command = qa_app_parse_application_command(argv, 8, search, NULL);
  CHECK(text_eq(command.kind, "run"));
  CHECK(strcmp(command.options.product, "q1-rerelease-id1") == 0);
  CHECK(strcmp(command.options.map, "maps/e1m2.bsp") == 0);
  CHECK(command.options.width == 1280 && text_eq(command.options.mode, "coop"));
  qa_app_application_command_destroy(&command);
  const char *menu_argv[] = {"--menu"};
  qa_app_application_command menu = qa_app_parse_application_command(menu_argv, 1, search, NULL);
  CHECK(text_eq(menu.kind, "menu"));
  qa_app_application_command_destroy(&menu);
  const char *help_argv[] = {"--help"};
  qa_app_application_command help = qa_app_parse_application_command(help_argv, 1, search, NULL);
  CHECK(text_eq(help.kind, "help"));
  qa_app_application_command_destroy(&help);
  char *map = qa_app_map_resource_path("base1");
  CHECK(strcmp(map, "maps/base1.bsp") == 0);
  free(map);
  CHECK_ABORTS(qa_app_map_resource_path("../evil"));
  const char *bad[] = {"--nonsense", "x"};
  CHECK_ABORTS(qa_app_parse_application_command(bad, 2, search, NULL));
}
static void test_progress(void) {
  const char *path = "/tmp/topb-progress/player.json";
  qa_app_player_progress_store *store = qa_app_player_progress_store_open(path);
  qa_app_player_progress_event event = {"level-completed", "q1", "local-seat:0", "e1m1-done", NULL, "e1m1", 0};
  CHECK(qa_app_player_progress_store_record(store, event));
  CHECK(!qa_app_player_progress_store_record(store, event)); /* duplicate identity */
  const qa_app_player_progress_event **rows = NULL;
  size_t count = qa_app_player_progress_store_list(store, "local-seat:0", &rows);
  CHECK(count == 1 && strcmp(rows[0]->map, "e1m1") == 0);
  free((void *)rows);
  char *identity = qa_app_player_progress_identity(event);
  CHECK(strcmp(identity, "[\"q1\",\"local-seat:0\",\"e1m1-done\"]") == 0);
  free(identity);
  qa_app_player_progress_store_destroy(store);
  qa_app_player_progress_store *reopened = qa_app_player_progress_store_open(path);
  count = qa_app_player_progress_store_list(reopened, "local-seat:0", &rows);
  CHECK(count == 1);
  free((void *)rows);
  qa_app_player_progress_store_destroy(reopened);
  remove(path);
  remove("/tmp/topb-progress/player.json.pending");
}
static int load_sentinel;
typedef struct load_result { int value; bool saw_failure; } load_result;
static void load_service_ok(void *ctx) { (*(int *)ctx)++; }
static void load_operation_ok(void *ctx, qa_app_loading_frame *frame) {
  load_result *result = ctx;
  qa_app_loading_next_frame(frame); qa_app_loading_next_frame(frame);
  result->value = 42;
}
static void load_service_fail(void *ctx) { CHECK(ctx == &load_sentinel); qa_raise("Error", 0, 0, "service broke"); }
static void next_frame_call(void *ctx) { qa_app_loading_next_frame(ctx); }
static void load_operation_catching(void *ctx, qa_app_loading_frame *frame) {
  load_result *result = ctx;
  qa_caught_error caught;
  /* the operation catches the rejected frame and settles with a value; the service error still wins */
  result->saw_failure = !qa_try(next_frame_call, frame, &caught) && strcmp(caught.message, "service broke") == 0;
  result->value = 7;
}
typedef struct load_call { qa_app_loading_operation_fn operation; load_result *result; void (*service)(void *); void *service_ctx; } load_call;
static void run_load(void *ctx) {
  load_call *call = ctx;
  qa_app_service_loading(call->operation, call->result, call->service, call->service_ctx);
}
static void test_loading(void) {
  int service_calls = 0;
  load_result result = {0, false};
  qa_app_service_loading(load_operation_ok, &result, load_service_ok, &service_calls);
  CHECK(result.value == 42 && service_calls == 2);
  result.value = 0;
  load_call call = {load_operation_ok, &result, load_service_fail, &load_sentinel};
  qa_caught_error caught;
  CHECK(!qa_try(run_load, &call, &caught));
  CHECK(text_eq(caught.message, "service broke"));
  CHECK(result.value == 0);
  load_call catching = {load_operation_catching, &result, load_service_fail, &load_sentinel};
  CHECK(!qa_try(run_load, &catching, &caught));
  CHECK(text_eq(caught.message, "service broke"));
  CHECK(result.saw_failure && result.value == 7);
}
static const char *lobby_host_unexpected(void *ctx, const qa_lobby *lobby, qa_net_address *a, qa_wire_selection *b) {
  (void)ctx; (void)lobby; (void)a; (void)b;
  return "unexpected";
}
static const char *lobby_noop(void *ctx, const qa_lobby *lobby) { (void)ctx; (void)lobby; return NULL; }
static void test_local_lobby_guards(void) {
  qa_online_account account = {0};
  account.id = (char *)"acct";
  account.name = (char *)"Player";
  qa_app_local_lobby_transitions transitions = {NULL, lobby_host_unexpected, lobby_noop, lobby_noop, lobby_noop};
  qa_app_application_local_lobby lobby;
  qa_app_application_local_lobby_init(&lobby, NULL, &account, transitions);
  CHECK(qa_app_application_local_lobby_current(&lobby) == NULL);
  CHECK(text_eq(qa_app_application_local_lobby_ready(&lobby, true), "No current local lobby membership"));
  CHECK(qa_app_application_local_lobby_close(&lobby) == NULL);
  CHECK(text_eq(qa_app_application_local_lobby_ready(&lobby, true), "Local lobby session is closed"));
  qa_app_application_local_lobby_destroy(&lobby);
}
static void test_map_command_and_sessions(void) {
  size_t count = 0;
  const char *const *commands = qa_q3_product_map_commands(
      (qa_q3_product_policy){QA_Q3_PRODUCT_RETAIL, QA_Q3_TEAM_ARENA_UI_RETAIL}, &count);
  CHECK(count > 0);
  qa_app_q3_map_launch launch;
  qa_app_q3_map_launch_make(&launch,
      (qa_q3_product_policy){QA_Q3_PRODUCT_RETAIL, QA_Q3_TEAM_ARENA_UI_RETAIL}, "spmap", 0);
  CHECK(launch.single_player && launch.game_type == 2 && launch.has_max_clients && launch.cvar_count == 4);
  CHECK(text_eq(launch.cvars[0].value, "2"));
  CHECK_ABORTS(qa_app_q3_map_launch_make(&launch,
      (qa_q3_product_policy){QA_Q3_PRODUCT_RETAIL, QA_Q3_TEAM_ARENA_UI_RETAIL}, "bogus", 0));
  qa_q1_event intermission; memset(&intermission, 0, sizeof intermission);
  intermission.kind = "intermission";
  qa_app_sim_presentation_event event; memset(&event, 0, sizeof event);
  event.kind = "q1"; event.q1 = &intermission;
  CHECK(text_eq(qa_app_source_level_completion(&event), "q1"));
  qa_rect viewport = qa_app_seat_viewport(1, 2, 1920, 1080);
  CHECK(viewport.x == 0 && viewport.y == 540 && viewport.height == 540);
  (void)commands;
}
int main(void) {
  test_match_modes();
  test_travel();
  test_damage_blend();
  test_options();
  test_progress();
  test_loading();
  test_local_lobby_guards();
  test_map_command_and_sessions();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
