/* Tests for top-c bootstrap ports: save requests, startup commands,
 * team arena demo/scores/skirmish, weapon view, shared/view settings,
 * direct servers, master list, weapon-behavior tool options, q2
 * player name tokens, seat pump, camera. GPL-2.0-or-later. */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/app/bootstrap/remote_seat_pump.h"
#include "qa/app/bootstrap/rerelease_presentation.h"
#include "qa/app/bootstrap/save_requests.h"
#include "qa/app/bootstrap/server_browser_addresses.h"
#include "qa/app/bootstrap/server_master_list.h"
#include "qa/app/bootstrap/shared_setting_cvars.h"
#include "qa/app/bootstrap/startup_commands.h"
#include "qa/app/bootstrap/team_arena_demo.h"
#include "qa/app/bootstrap/team_arena_scores.h"
#include "qa/app/bootstrap/team_arena_skirmish.h"
#include "qa/app/bootstrap/view_settings.h"
#include "qa/app/bootstrap/weapon_behavior_tool_options.h"
#include "qa/app/bootstrap/weapon_behavior_selection.h"
#include "qa/app/bootstrap/weapon_view.h"
#include "qa/contracts/common.h"
#include "qa/core/cvars/index.h"

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

static void print_sink(void *ctx, const char *text) { (void)ctx; (void)text; }
static qa_cvar_registry *make_cvars(qa_command_dialect dialect) {
  qa_cvar_registry_options opts; memset(&opts, 0, sizeof opts);
  opts.dialect = dialect;
  opts.print = print_sink;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&opts);
  if (!cvars) { fprintf(stderr, "no cvars\n"); exit(1); }
  return cvars;
}

static void test_save_requests(void) {
  const char *args1[] = {"quicksave"};
  qa_app_save_request save = qa_app_parse_save_request(args1, 1);
  CHECK(strcmp(save.name, "quicksave") == 0 && strcmp(save.format, "shared") == 0);
  const char *args2[] = {"original", "v5"};
  save = qa_app_parse_save_request(args2, 2);
  CHECK(strcmp(save.format, "v5") == 0);
  const char *bad[] = {"x", "v9"};
  CHECK_ABORTS(qa_app_parse_save_request(bad, 2));
  CHECK_ABORTS(qa_app_parse_save_request(NULL, 0));
  const char *load_args[] = {"slot", "q1-classic-id1"};
  qa_app_load_request load = qa_app_parse_load_request(load_args, 2);
  CHECK(strcmp(load.source_product, "q1-classic-id1") == 0);
  CHECK(qa_app_save_command_documentation("save") != NULL);
  CHECK(qa_app_save_command_documentation("load") != NULL);
  CHECK(qa_app_save_command_documentation("kick") == NULL);
}

static void test_startup_commands(void) {
  const char *argv[] = {"+map", "e1m1", "+set", "skill", "2"};
  qa_app_startup_command first = qa_app_read_startup_command(argv, 5, 0);
  CHECK(strcmp(first.text, "map \"e1m1\"") == 0 && first.end == 1);
  free(first.text);
  const char *connect[] = {"+connect", "host"};
  CHECK_ABORTS(qa_app_read_startup_command(connect, 2, 0));
  const char *lines[] = {"set skill 2", "map start"};
  CHECK(qa_app_startup_requests_world(lines, 2));
  CHECK(!qa_app_startup_requests_world(lines, 1));
  const char *q3_lines[] = {"set g_gametype 0", "map q3dm1"};
  qa_app_startup_phases phases = qa_app_startup_command_phases(q3_lines, 2, QA_COMMAND_DIALECT_Q3);
  CHECK(strcmp(phases.early, "") == 0);
  CHECK(phases.variable_count == 1 && strcmp(phases.variables[0].name, "g_gametype") == 0);
  CHECK(strcmp(phases.late, "set g_gametype 0\nmap q3dm1\n") == 0);
  qa_app_startup_phases_free(&phases);
}

static bool demo_exists(void *ctx, const char *path) { return strcmp(path, (const char *)ctx) == 0; }
static void test_team_arena_demo(void) {
  qa_app_team_arena_demo demo; memset(&demo, 0, sizeof demo);
  CHECK(qa_app_team_arena_demo_lookup("mpq3ctf1", 4, 68, demo_exists, (void *)"demos/mpq3ctf1_4.dm_68", &demo));
  CHECK(strcmp(demo.name, "mpq3ctf1_4") == 0);
  qa_app_team_arena_demo_free(&demo);
  CHECK(!qa_app_team_arena_demo_lookup("mpq3ctf1", 4, 68, demo_exists, (void *)"no", &demo));
  CHECK_ABORTS(qa_app_team_arena_demo_lookup("../x", 4, 68, demo_exists, (void *)"no", &demo));
}

static void test_team_arena_scores(void) {
  const char *args[] = {"ui", "postgame", "80", "2", "1", "3", "4", "5", "100", "1", "5", "0", "61000", "2"};
  qa_app_team_arena_postgame stats = qa_app_parse_team_arena_postgame(args, 14);
  CHECK(stats.accuracy == 80 && stats.base_score == 100 && stats.end_time == 61000 && stats.captures == 2);
  qa_app_team_arena_score previous; memset(&previous, 0, sizeof previous);
  previous.time = 999;
  qa_app_team_arena_score_input input = {stats, 1000, 2, 120};
  qa_app_team_arena_score_result result = qa_app_calculate_team_arena_score(input, previous);
  CHECK(result.won && result.score.time == 60);
  CHECK(result.score.time_bonus == 600 && result.score.shutout_bonus == 100);
  CHECK(result.score.score == (100 + 100 + 600) * 2);
  CHECK(result.new_high_score && result.new_best_time);
  uint8_t encoded[68];
  qa_app_encode_team_arena_score(result.score, encoded);
  qa_app_team_arena_score decoded = qa_app_decode_team_arena_score(encoded, 68);
  CHECK(decoded.score == result.score.score && decoded.time == 60);
  char *path = qa_app_team_arena_score_path("mpq3dm1", 4);
  CHECK(strcmp(path, "games/mpq3dm1_4.game") == 0);
  free(path);
}

static void test_skirmish(void) {
  const char *game_info =
      "Gametypes { { FreeForAll 0 FFA } { Tournament 1 T } { Skirmish 2 S } { TeamFFA 3 TFFA } { CTF 4 CTF } }\n"
      "Maps { { First mpq3dm1 2 Stroggs 1 60 2 60 4 90 } { Second mpq3ctf1 3 Pagans 2 90 4 120 } }\n";
  const char *team_info =
      "Teams { { Pagans x Anna Boris Cora Dave Erin } { Stroggs x Flip Gina Hank Iona Jack } }\n"
      "Aliases { { anna AnnaBot } }\n";
  qa_app_team_arena_campaign *campaign = qa_app_parse_team_arena_campaign(game_info, team_info);
  size_t choice_count = 0;
  char **choices = qa_app_team_arena_team_choices(campaign, &choice_count);
  CHECK(choice_count == 2 && strcmp(choices[0], "pagans") == 0);
  free(choices);
  qa_app_team_arena_cursor cursor = {4, 0}; /* gameTypes[4] = 4 (CTF) */
  qa_app_team_arena_skirmish setup;
  qa_app_team_arena_teams teams = {"Pagans", "Stroggs"};
  qa_app_team_arena_skirmish_init_default(&setup, campaign, 2, cursor, teams);
  CHECK(strcmp(setup.map, "maps/mpq3dm1.bsp") == 0);
  CHECK(setup.game_type == 4 && setup.max_clients == 4 && setup.time_to_beat == 90);
  CHECK(strcmp(setup.player_team, "Red") == 0 && strcmp(setup.player_model, "james") == 0);
  CHECK(setup.bot_count == 3);
  /* opponent side first: Stroggs 2 members, then Pagans 1 (teamMembers-1). */
  CHECK(strcmp(setup.bots[0].name, "Flip") == 0 && strcmp(setup.bots[0].team, "Blue") == 0);
  CHECK(setup.bots[0].delay_milliseconds == 500 && setup.bots[1].delay_milliseconds == 1000);
  CHECK(strcmp(setup.bots[2].ai, "AnnaBot") == 0 && strcmp(setup.bots[2].team, "Red") == 0);
  qa_app_team_arena_cursor next = qa_app_next_team_arena_cursor(campaign, cursor);
  CHECK(next.map_index == 1 && next.game_type_index == 4);
  qa_app_team_arena_skirmish_free(&setup);
  qa_app_team_arena_campaign_destroy(campaign);
}

static void test_weapon_view(void) {
  qa_app_simulation_presentation_view q1 = {true, "q1", {1, 2, 3}};
  qa_vec3 origin = qa_app_weapon_view_origin(q1);
  CHECK(origin.z == 5 && origin.x == 1);
  q1.family = "q2";
  CHECK(qa_app_weapon_view_origin(q1).z == 3);
  qa_scene_camera camera; memset(&camera, 0, sizeof camera);
  camera.viewport = (qa_rect){0, 0, 800, 600};
  for (int i = 0; i < 16; i++) camera.projection.values[i] = i == 0 || i == 5 || i == 10 || i == 15 ? 1 : 0;
  qa_rect occupied[] = {{0, 480, 800, 120}};
  qa_scene_camera shifted = qa_app_weapon_view_camera(camera, occupied, 1);
  CHECK(fabs(shifted.projection.values[5] - 1.0) < 1e-9);
  CHECK(fabs(shifted.projection.values[13] - 0.2) < 1e-9);
}

static double changed_value;
static void on_changed(void *ctx, double value) { *(double *)ctx = value; }
static void test_settings(void) {
  CHECK(qa_app_validate_field_of_view("90") == NULL);
  CHECK(qa_app_validate_field_of_view("59") != NULL);
  CHECK(qa_app_validate_field_of_view("abc") != NULL);
  qa_app_view_settings *view = qa_app_view_settings_create(on_changed, &changed_value);
  changed_value = 0;
  qa_app_view_settings_set_field_of_view(view, 120);
  CHECK(changed_value == 120 && qa_app_view_settings_field_of_view(view) == 120);
  double override = 0;
  CHECK(qa_app_view_settings_override(view, &override) && override == 120);
  CHECK_ABORTS(qa_app_view_settings_set_field_of_view(view, 200));
  qa_cvar_registry *cvars = make_cvars(QA_COMMAND_DIALECT_Q3);
  qa_app_view_settings_bind_cvars(view, cvars);
  CHECK(qa_app_view_settings_field_of_view(view) == 120);
  qa_app_view_settings_unbind_cvars(view);
  qa_app_view_settings_destroy(view);
  qa_cvar_registry_destroy(cvars);
}

static void test_direct_servers(void) {
  const char *text = "{\"version\":1,\"servers\":[{\"remote\":\"example.com:27500\","
                     "\"address\":{\"kind\":\"ipv4\",\"host\":[127,0,0,1],\"port\":27500}}]}";
  size_t count = 0;
  qa_app_direct_server_address *servers = qa_app_read_direct_servers(text, &count);
  CHECK(count == 1 && servers[0].address.port == 27500);
  CHECK(strcmp(servers[0].remote, "example.com:27500") == 0);
  qa_app_direct_servers_free(servers, count);
  CHECK_ABORTS(qa_app_read_direct_servers("{\"version\":2,\"servers\":[]}", &count));
  const char *body = "# comment\none.example:27000\n\none.example:27000\ntwo.example\n";
  size_t list_count = 0;
  char **list = qa_app_parse_server_master_list((const uint8_t *)body, strlen(body), &list_count);
  CHECK(list_count == 2 && strcmp(list[1], "two.example") == 0);
  for (size_t i = 0; i < list_count; i++) free(list[i]);
  free(list);
}

static void test_tool_options_and_selection(void) {
  const char *argv[] = {"inspect", "q3-demo"};
  qa_app_weapon_behavior_tool_command command = qa_app_parse_weapon_behavior_tool(argv, 2);
  CHECK(strcmp(command.action, "inspect") == 0 && strcmp(command.product, "q3-demo") == 0);
  qa_app_weapon_behavior_tool_command_free(&command);
  const char *declare[] = {"declare", "q1-id1", "--id", "id1:rocket", "--role", "rocket", "--fire", "W_FireRocket"};
  command = qa_app_parse_weapon_behavior_tool(declare, 8);
  CHECK(strcmp(command.role, "rocket") == 0 && strcmp(command.fire, "W_FireRocket") == 0);
  qa_app_weapon_behavior_tool_command_free(&command);
  const char *bad[] = {"inspect", "p", "--role", "rocket"};
  CHECK_ABORTS(qa_app_parse_weapon_behavior_tool(bad, 4));
  qa_app_weapon_behavior_request request = qa_app_read_weapon_behavior_request("product/ns:id");
  CHECK(strcmp(request.product, "product") == 0 && strcmp(request.id, "ns:id") == 0);
  qa_app_weapon_behavior_request_free(&request);
  CHECK_ABORTS(qa_app_read_weapon_behavior_request("nocolon"));
}

static void test_name_tokens(void) {
  double slots[] = {0, 1};
  const char *names[] = {"Anna", "Boris"};
  char *text = qa_app_q2_player_name_tokens("##P0 hit ##P1 (##P9)", slots, names, 2);
  CHECK(strcmp(text, "Anna hit Boris ()") == 0);
  free(text);
}

typedef struct pump_ctx { int executed; int rejected; } pump_ctx;
static const char *pump_poll(void *ctx, double now_ms) { (void)ctx; (void)now_ms; return NULL; }
static void pump_close(void *ctx) { (void)ctx; }
static bool pub_ready(void *ctx) { (void)ctx; return true; }
static const char *pub_execute(void *ctx) { ((pump_ctx *)ctx)->executed++; return NULL; }
static void pub_reject(void *ctx, const char *error) { (void)error; ((pump_ctx *)ctx)->rejected++; }
static void test_pump(void) {
  pump_ctx state = {0, 0};
  qa_app_remote_seat_pump_source source = {&state, pump_poll, pump_close};
  qa_app_remote_seat_pump *pump = qa_app_remote_seat_pump_create(source);
  qa_app_remote_seat_publication publication = {&state, pub_ready, pub_execute, pub_reject};
  CHECK(qa_app_remote_seat_pump_enqueue(pump, publication));
  qa_app_remote_seat_pump_drain_ready(pump);
  CHECK(state.executed == 1);
  CHECK(qa_app_remote_seat_pump_enqueue(pump, publication));
  qa_app_remote_seat_pump_close(pump);
  CHECK(state.rejected == 1);
  CHECK(!qa_app_remote_seat_pump_enqueue(pump, publication));
  CHECK(state.rejected == 2);
  qa_app_remote_seat_pump_destroy(pump);
}

int main(void) {
  test_save_requests();
  test_startup_commands();
  test_team_arena_demo();
  test_team_arena_scores();
  test_skirmish();
  test_weapon_view();
  test_settings();
  test_direct_servers();
  test_tool_options_and_selection();
  test_name_tokens();
  test_pump();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
