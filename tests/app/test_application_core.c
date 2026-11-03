/* Checks for the Application class core (application.ts -> application_core.c,
 * frame_loop.c, frame_time.c minimumFrameMilliseconds, user_data.c
 * userDataDirectory).
 *
 * Expected values come from TS under bun on the same inputs:
 * tests/app/oracles/application_core.ts calls the Application.prototype members
 * on fake `this` records (`cd ../quake-anthology-ts && bun
 * ../quake-anthology-c/tests/app/oracles/application_core.ts`). The C struct is
 * built by qa_application_construct over a zeroed SharedSimulation record that
 * holds only the members each check reads. Every callback checks a sentinel
 * ctx. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../../src/app/bootstrap/application_internal.h"
#include "qa/app/bootstrap/frame_loop.h"
#include "qa/app/bootstrap/frame_time.h"
#include "qa/content/user_data.h"
#include "qa/content/mounts.h"

static int failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                       \
    }                                                                   \
  } while (0)
#define CHECK_STR(a, b) CHECK((a) != NULL && strcmp((a), (b)) == 0)

/* ---- host with a sentinel ctx ---- */
static int host_sentinel;
static char printed[4096];
static int print_count;
static void host_print(void *ctx, const char *text) {
  CHECK(ctx == &host_sentinel);
  print_count++;
  strncat(printed, text, sizeof printed - strlen(printed) - 1);
}
static qa_application_host test_host(void) {
  qa_application_host host;
  memset(&host, 0, sizeof host);
  host.ctx = &host_sentinel;
  host.print = host_print;
  return host;
}

static qa_identity_owner identity;
static qa_app_shared_simulation *fake_simulation(void) {
  qa_app_shared_simulation *sim = calloc(1, sizeof *sim);
  sim->source.kind = QA_SS_SOURCE_LOADING;
  qa_ss_set_source_strategy(sim); /* D1 SourceStrategy: accessors dispatch through it */
  sim->options.mode = "singleplayer";
  return sim;
}
static qa_application *make_app(qa_app_shared_simulation *sim, const qa_client_id *clients, qa_session_seat *const *seats, size_t count) {
  qa_app_options options;
  memset(&options, 0, sizeof options);
  options.network_kind = "offline";
  options.mode = "singleplayer";
  options.map = "e1m1";
  qa_config_store store;
  CHECK(qa_config_store_init(&store, "/tmp/qa-application-core-input"));
  qa_application_ownership owned = { NULL };
  return qa_application_construct(options, NULL, NULL, sim, test_host(), &identity, clients, seats, count, store, owned);
}
static qa_cvar_registry *registry(qa_command_dialect dialect) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  return qa_cvar_registry_create(&options);
}

/* TS field initializers and the constructor body. */
static void test_construct(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  CHECK(app->frames == 0 && app->elapsed == 0 && app->presentation_frames == 0);
  CHECK(!app->has_presentation_milliseconds);
  CHECK(app->last_restart_frame == -1);
  CHECK(app->native_world_count == 1);
  CHECK(app->world_operation == QA_APPLICATION_IDLE && app->world_operations == 0);
  CHECK(app->local_snapshot_server_bit == 0);
  CHECK(app->graphical == NULL && app->network == NULL && app->bots == NULL);
  CHECK(app->keys != NULL && app->music_controls != NULL);
  CHECK(app->view_settings != NULL && app->debug_graph != NULL && app->campaign_unit != NULL);
  CHECK(qa_application_simulation(app) == sim);
  CHECK(qa_application_frame_count(app) == 0);
  CHECK(qa_application_world_operation_count(app) == 0);
  CHECK(!qa_application_world_failed(app));
  /* the TS print reaches the host's print with the host's ctx */
  print_count = 0; printed[0] = '\0';
  qa_application_printf(app, "Loaded %s.\n", "e1m1");
  CHECK(print_count == 1);
  CHECK_STR(printed, "Loaded e1m1.\n");
  qa_application_destroy(app);
  free(sim);
}

/* oracle take.* */
static void test_ranking_menu_requests(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  qa_seat_id seat0 = qa_identity_owner_seat(&identity, 0), seat1 = qa_identity_owner_seat(&identity, 1);
  app->ranking_menu_requests = malloc(2 * sizeof *app->ranking_menu_requests);
  app->ranking_menu_requests[0] = seat0; app->ranking_menu_requests[1] = seat1;
  app->ranking_menu_request_count = 2; app->ranking_menu_request_capacity = 2;
  CHECK(qa_application_take_ranking_menu_request(app, seat0) == true);
  CHECK(qa_application_take_ranking_menu_request(app, seat0) == false);
  CHECK(qa_application_take_ranking_menu_request(app, seat1) == true);
  qa_application_destroy(app);
  free(sim);
}

/* oracle override / override.error */
typedef struct override_case { qa_application *app; } override_case;
static void override_missing(void *ctx) {
  override_case *c = ctx;
  qa_app_override_registry rows[2];
  (void)qa_application_override_seats(c->app, NULL, NULL, 0, rows, 2);
}
static void test_override_seats(void) {
  qa_session_client *client0 = qa_session_client_create(qa_identity_owner_client(&identity, 3, 0));
  qa_session_client *client1 = qa_session_client_create(qa_identity_owner_client(&identity, 5, 2));
  qa_session_seat *s0 = qa_session_seat_create(qa_identity_owner_seat(&identity, 0), client0);
  qa_session_seat *s1 = qa_session_seat_create(qa_identity_owner_seat(&identity, 1), client1);
  qa_client_id clients[2] = { qa_session_client_id(client0), qa_session_client_id(client1) };
  qa_session_seat *seats[2] = { s0, s1 };
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, clients, seats, 2);
  qa_cvar_registry *r0 = registry(QA_COMMAND_DIALECT_Q3), *r1 = registry(QA_COMMAND_DIALECT_Q3);
  qa_seat_id cvar_seats[2] = { qa_identity_owner_seat(&identity, 1), qa_identity_owner_seat(&identity, 0) };
  qa_cvar_registry *cvars[2] = { r1, r0 };
  qa_app_override_registry rows[2];
  CHECK(qa_application_override_seats(app, cvar_seats, cvars, 2, rows, 2) == 2);
  CHECK(rows[0].seat == 0 && rows[0].client == 3 && rows[0].cvars == r0);
  CHECK(rows[1].seat == 1 && rows[1].client == 5 && rows[1].cvars == r1);
  override_case c = { app };
  qa_caught_error caught;
  CHECK(!qa_try(override_missing, &c, &caught));
  CHECK_STR(caught.message, "Missing local override registry");
  qa_application_destroy(app);
  free(sim);
  qa_cvar_registry_destroy(r0); qa_cvar_registry_destroy(r1);
  qa_session_seat_destroy(s0); qa_session_seat_destroy(s1);
  qa_session_client_destroy(client0); qa_session_client_destroy(client1);
}

/* oracle recovery=[true,true,true,false,false,false,false] */
static void test_level_recovery(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  static int dummy;
  qa_app_options options;
  memset(&options, 0, sizeof options);
  options.network_kind = "offline";
  sim->source.kind = QA_SS_SOURCE_Q1;
  qa_ss_set_source_strategy(sim);
  CHECK(qa_application_level_recovery_available(app, sim, &options) == true);
  sim->source.kind = QA_SS_SOURCE_Q2; sim->source.product = (qa_q2_product_runtime *)&dummy;
  qa_ss_set_source_strategy(sim);
  CHECK(qa_application_level_recovery_available(app, sim, &options) == true);
  sim->source.kind = QA_SS_SOURCE_QUAKEC; sim->source.quakec = (qa_app_sim_quakec_source *)&dummy;
  qa_ss_set_source_strategy(sim);
  CHECK(qa_application_level_recovery_available(app, sim, &options) == true);
  sim->source.kind = QA_SS_SOURCE_LOADING;
  qa_ss_set_source_strategy(sim);
  CHECK(qa_application_level_recovery_available(app, sim, &options) == false);
  sim->source.kind = QA_SS_SOURCE_Q1; sim->options.mode = "deathmatch";
  qa_ss_set_source_strategy(sim);
  CHECK(qa_application_level_recovery_available(app, sim, &options) == false);
  sim->options.mode = "singleplayer"; options.dedicated = true;
  CHECK(qa_application_level_recovery_available(app, sim, &options) == false);
  options.dedicated = false; options.network_kind = "q1-server";
  CHECK(qa_application_level_recovery_available(app, sim, &options) == false);
  sim->source.kind = QA_SS_SOURCE_LOADING;
  qa_ss_set_source_strategy(sim);
  qa_application_destroy(app);
  free(sim);
}

/* oracle sourceCvars=["q2","q1",...,null] (the QuakeC arm needs a constructed QuakeCSource) */
static void test_source_cvars(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  qa_cvar_registry *q2 = registry(QA_COMMAND_DIALECT_Q2_CLASSIC), *q1 = registry(QA_COMMAND_DIALECT_Q1_NETQUAKE);
  sim->q2_server_registry = q2; sim->source.kind = QA_SS_SOURCE_Q1; sim->source.q1_cvars = q1;
  qa_ss_set_source_strategy(sim);
  CHECK(qa_application_source_cvars(app, NULL) == q2);
  CHECK(qa_application_source_cvars(app, sim) == q2);
  sim->q2_server_registry = NULL;
  CHECK(qa_application_source_cvars(app, NULL) == q1);
  sim->source.kind = QA_SS_SOURCE_LOADING;
  qa_ss_set_source_strategy(sim);
  CHECK(qa_application_source_cvars(app, NULL) == NULL);
  qa_application_destroy(app);
  free(sim);
  qa_cvar_registry_destroy(q2); qa_cvar_registry_destroy(q1);
}

/* oracle queued / stopping */
static void test_queue_command(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  qa_seat_id seat1 = qa_identity_owner_seat(&identity, 1);
  const char *args[2] = { "e1m1", "x" };
  qa_application_queue_command(app, "map", args, 2, &seat1);
  qa_application_queue_command(app, "quit", NULL, 0, NULL);
  qa_application_request_quit(app);
  size_t count = 0;
  qa_application_command_request *taken = qa_event_ring_take(&app->requested_commands, &app->requested_cursor, &count);
  CHECK(count == 2);
  CHECK(taken[0].target == QA_APPLICATION_TARGET_APPLICATION);
  CHECK_STR(taken[0].name, "map");
  CHECK(taken[0].argument_count == 2);
  CHECK_STR(taken[0].arguments[0], "e1m1");
  CHECK_STR(taken[0].arguments[1], "x");
  CHECK(taken[0].has_seat && taken[0].seat.index == 1);
  CHECK_STR(taken[1].name, "quit");
  CHECK(taken[1].argument_count == 0 && !taken[1].has_seat);
  for (size_t i = 0; i < count; i++) qa_application_command_request_free(&taken[i]);
  free(taken);
  CHECK(app->stopping == true);
  qa_application_destroy(app);
  free(sim);
}

/* oracle input.closed / input.saving.headless / input.idle.headless */
typedef struct input_case { qa_application *app; qa_seat_input_event event; } input_case;
static void input_closed(void *ctx) { input_case *c = ctx; (void)qa_application_input(c->app, &c->event); }
static void test_input(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  input_case c;
  memset(&c, 0, sizeof c);
  c.app = app;
  c.event.seat = qa_identity_owner_seat(&identity, 0);
  app->closed = true;
  qa_caught_error caught;
  CHECK(!qa_try(input_closed, &c, &caught));
  CHECK_STR(caught.message, "Application is closed");
  app->closed = false;
  app->world_operation = QA_APPLICATION_SAVING;
  CHECK(qa_application_input(app, &c.event) == false);
  CHECK(app->deferred_input_count == 0);
  app->world_operation = QA_APPLICATION_IDLE;
  CHECK(qa_application_input(app, &c.event) == false);
  qa_application_destroy(app);
  free(sim);
}

/* oracle finished=[false,true,true,false,true,true] */
static void test_finished(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  app->frames = 10;
  CHECK(qa_application_finished(app) == false);
  app->frames = 0; app->stopping = true;
  CHECK(qa_application_finished(app) == true);
  app->stopping = false; app->closed = true;
  CHECK(qa_application_finished(app) == true);
  app->closed = false; app->launch_options.has_frame_limit = true; app->launch_options.frame_limit = 5; app->frames = 4;
  CHECK(qa_application_finished(app) == false);
  app->frames = 5;
  CHECK(qa_application_finished(app) == true);
  app->launch_options.frame_limit = 0; app->frames = 0;
  CHECK(qa_application_finished(app) == true);
  qa_application_destroy(app);
  free(sim);
}

/* oracle movie.* */
static bool movie_current(void *ctx) { (void)ctx; return true; }
static void movie_complete(void *ctx) { (void)ctx; }
static void test_campaign_movie(void) {
  qa_app_shared_simulation *sim_a = fake_simulation(), *sim_b = fake_simulation();
  qa_application *app = make_app(sim_a, NULL, NULL, 0);
  CHECK(qa_application_campaign_movie(app) == NULL);
  qa_application_movie movie;
  memset(&movie, 0, sizeof movie);
  movie.playback = (qa_app_campaign_cinematic *)&movie; movie.request = 7; movie.current = movie_current; movie.complete = movie_complete;
  qa_application_set_campaign_movie(app, &movie);
  CHECK(qa_application_campaign_movie(app) != NULL && qa_application_campaign_movie(app)->request == 7);
  app->world_simulation = sim_b;
  CHECK(qa_application_campaign_movie(app) == NULL);
  app->world_simulation = sim_a;
  app->movie_request_simulations = malloc(sizeof *app->movie_request_simulations);
  app->movie_requests = malloc(sizeof *app->movie_requests);
  app->movie_request_simulations[0] = sim_a; app->movie_requests[0] = 7;
  app->movie_request_count = 1; app->movie_request_capacity = 1;
  qa_application_set_campaign_movie(app, NULL);
  CHECK(qa_application_campaign_movie(app) == NULL);
  CHECK(app->movie_request_count == 0);
  CHECK(app->movie_count == 0);
  qa_application_destroy(app);
  free(sim_a); free(sim_b);
}

/* oracle dir.*, store.root, saveDirectory.* */
static void test_directories(void) {
  char *saves = qa_user_data_directory("saves", "/tmp/qa-root/../qa-user");
  CHECK_STR(saves, "/tmp/qa-user/saves");
  free(saves);
  char cwd[4096];
  CHECK(getcwd(cwd, sizeof cwd) != NULL);
  char expected[4200];
  (void)snprintf(expected, sizeof expected, "%s/relative/root/settings", cwd);
  char *settings = qa_user_data_directory("settings", "relative/root");
  CHECK_STR(settings, expected);
  free(settings);
  const char *home = getenv("HOME");
  (void)snprintf(expected, sizeof expected, "%s/.local/share/quake-typescript/saves", home != NULL ? home : "");
  char *fallback = qa_user_data_directory("saves", NULL);
  CHECK_STR(fallback, expected);
  free(fallback);
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  app->host.save_directory = "/tmp/x/saves";
  CHECK_STR(qa_application_server_profile_store(app)->root, "/tmp/x/settings");
  CHECK_STR(qa_application_save_directory(app), "/tmp/x/saves");
  app->host.save_directory = NULL;
  app->launch_options.user_content_root = "/tmp/u";
  CHECK_STR(qa_application_save_directory(app), "/tmp/u/saves");
  qa_application_destroy(app);
  free(sim);
}

/* oracle minimum=[4,4,13.88888888888889,50,100.00000149011612,13.88888888888889] */
static void test_minimum_frame(void) {
  qa_cvar_registry *q1 = registry(QA_COMMAND_DIALECT_Q1_NETQUAKE), *q3 = registry(QA_COMMAND_DIALECT_Q3);
  qa_cvar_registry *qw = registry(QA_COMMAND_DIALECT_Q1_QUAKEWORLD);
  qa_cvar_snapshot snapshot;
  CHECK(qa_cvar_registry_register(qw, "sys_ticrate", "0.1", 0, &snapshot));
  qa_cvar_read_free(&snapshot);
  CHECK(qa_app_minimum_frame_milliseconds(NULL, true) == 4);
  CHECK(qa_app_minimum_frame_milliseconds(q3, true) == 4);
  CHECK(qa_app_minimum_frame_milliseconds(q1, false) == 13.88888888888889);
  CHECK(qa_app_minimum_frame_milliseconds(q1, true) == 50);
  CHECK(qa_app_minimum_frame_milliseconds(qw, true) == 100.00000149011612);
  CHECK(qa_app_minimum_frame_milliseconds(qw, false) == 13.88888888888889);
  qa_cvar_registry_destroy(q1); qa_cvar_registry_destroy(q3); qa_cvar_registry_destroy(qw);
}

/* oracle loop=[4,1,8], loop.recover.message=boom, loop.rethrow=fatal frame */
static int loop_sentinel;
typedef struct loop_state { int steps, recovered, states; double now; bool fatal; } loop_state;
static loop_state loop;
static bool loop_alive(void *ctx) { CHECK(ctx == &loop_sentinel); return loop.fatal || loop.steps < 4; }
static double loop_minimum(void *ctx) { CHECK(ctx == &loop_sentinel); return 0; }
static void loop_step(void *ctx, double elapsed) {
  CHECK(ctx == &loop_sentinel);
  (void)elapsed;
  if (loop.fatal) qa_raise("Error", 0, 0, "fatal frame");
  loop.steps++;
  if (loop.steps == 2) qa_raise("Error", 0, 0, "boom");
}
static void loop_clock_state(void *ctx, char *out, size_t capacity) {
  CHECK(ctx == &loop_sentinel);
  loop.states++;
  (void)snprintf(out, capacity, "x");
}
static void loop_recover(void *ctx, const qa_caught_error *error) {
  CHECK(ctx == &loop_sentinel);
  CHECK_STR(error->message, "boom");
  loop.recovered++;
}
static int clock_sentinel;
static double loop_now(void *ctx) { CHECK(ctx == &clock_sentinel); return loop.now += 1; }
static void loop_sleep(void *ctx, double milliseconds) { CHECK(ctx == &clock_sentinel); loop.now += milliseconds; }
static void run_fatal_loop(void *ctx) {
  (void)ctx;
  qa_app_frame_loop_host host = { &loop_sentinel, loop_alive, loop_minimum, loop_step, loop_clock_state, NULL };
  qa_app_frame_loop_clock clock = { &clock_sentinel, loop_now, loop_sleep };
  qa_app_run_frame_loop_clocked(host, clock);
}
static void test_frame_loop(void) {
  memset(&loop, 0, sizeof loop);
  qa_app_frame_loop_host host = { &loop_sentinel, loop_alive, loop_minimum, loop_step, loop_clock_state, loop_recover };
  qa_app_frame_loop_clock clock = { &clock_sentinel, loop_now, loop_sleep };
  qa_app_run_frame_loop_clocked(host, clock);
  CHECK(loop.steps == 4 && loop.recovered == 1 && loop.states == 8);
  memset(&loop, 0, sizeof loop);
  loop.fatal = true;
  qa_caught_error caught;
  CHECK(!qa_try(run_fatal_loop, NULL, &caught));
  CHECK_STR(caught.message, "fatal frame");
}

/* The coordinator's apply callback receives the application (pendingTransition). */
static void test_transition_callback(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  qa_transition_mode mode;
  memset(&mode, 0, sizeof mode);
  mode.kind = "competitive"; mode.match = "q1:dm";
  qa_transition_intent intent;
  memset(&intent, 0, sizeof intent);
  intent.kind = "match-rotation"; intent.match = "q1:dm"; intent.map = "q1:e1m2";
  int64_t id = -1;
  qa_transition_decision decision = qa_shared_transition_coordinator_resolve(app->transitions, mode, &intent, 1, &id);
  CHECK_STR(decision.kind, "travel");
  qa_shared_transition_coordinator_commit(app->transitions, id, decision);
  CHECK(app->has_pending_transition);
  CHECK_STR(app->pending_transition.kind, "travel");
  CHECK_STR(app->pending_transition.map, "q1:e1m2");
  qa_application_destroy(app);
  free(sim);
}

/* ==== P1 / P7 members ==== */
/* Weak doubles for members of groups still being ported (P5, q3-client,
 * application-tools, the q2-match view). The checks below never reach them
 * except qa_app_shared_sim_q2_match_simulation, whose view answers TS
 * prepareQ2MatchReports's early return (no Q2 source or native world). An
 * archive member that defines one of them overrides the weak definition. */
static void unreached(const char *name) { fprintf(stderr, "unreached test double: %s\n", name); abort(); }
__attribute__((weak)) void qa_app_application_q3_client_input(qa_app_application_q3_client *client, const qa_seat_input_event *event) {
  (void)client; (void)event; unreached("ApplicationQ3Client.input"); }
__attribute__((weak)) qa_q3_pres_q3_renderer_resources *qa_app_application_q3_client_resource_diagnostics(qa_app_application_q3_client *client) {
  (void)client; unreached("ApplicationQ3Client.resourceDiagnostics"); return NULL; }
__attribute__((weak)) qa_app_application_tools *qa_app_application_tools_open(qa_command_buffer *commands, qa_app_application_tool_services services) {
  (void)commands; (void)services; unreached("ApplicationTools"); return NULL; }
/* application_services.c (P2) arrives with destroy's qa_application_services_free and references these P3/P6 members. */
__attribute__((weak)) void qa_application_commands_routed(qa_application *app, bool route_applications) {
  (void)app; (void)route_applications; unreached("commands"); }
__attribute__((weak)) void qa_application_change_local_players(qa_application *app, const char *kind, const char *const *args, size_t count,
    qa_seat_id invoking_seat) { (void)app; (void)kind; (void)args; (void)count; (void)invoking_seat; unreached("changeLocalPlayers"); }
/* P3 publishLocalGuestSnapshots (application_step.c and P6 changeLocalPlayers reference it). */
__attribute__((weak)) void qa_application_publish_local_guest_snapshots(qa_application *app) {
  (void)app; unreached("publishLocalGuestSnapshots"); }
static bool match_false(void *ctx) { (void)ctx; return false; }
__attribute__((weak)) qa_app_q2_match_simulation qa_app_shared_sim_q2_match_simulation(qa_app_shared_simulation *sim) {
  qa_app_q2_match_simulation view;
  memset(&view, 0, sizeof view);
  view.ctx = sim;
  view.mode = sim->options.mode;
  view.has_q2_source = match_false;
  view.has_q2_native = match_false;
  return view;
}


/* Weak doubles for the members P4/P5 call that other groups or lower layers have not defined yet (the
 * "Found by P4/P5" list in docs/application_layout.md). The P4/P5 checks below stop at the TS guards before reaching
 * any of them. */
#define UNREACHED_WEAK(ret, name, params, value) \
  __attribute__((weak)) ret name params { unreached(#name); return value; }
#define UNREACHED_WEAK_VOID(name, params) \
  __attribute__((weak)) void name params { unreached(#name); }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
UNREACHED_WEAK(qa_app_q3_presentation_hosts, qa_app_application_assets_q3_presentation_hosts, (qa_app_application_assets *assets, qa_app_application_audio *audio), (qa_app_q3_presentation_hosts){0})
UNREACHED_WEAK(qa_app_capture *, qa_app_application_capture_open, (qa_app_application_input *input, const char *root, const char *(*map_name)(void *ctx), void *map_ctx, void (*print)(void *ctx, const char *text), void *print_ctx, qa_app_native_renderer *renderer), NULL)
UNREACHED_WEAK(qa_app_audio_haptics, qa_app_application_input_audio_haptics, (qa_app_application_input *input), (qa_app_audio_haptics){0})
UNREACHED_WEAK_VOID(qa_app_apply_frontend_input, (qa_app_frontend_preference_values values, qa_app_local_input *local))
UNREACHED_WEAK_VOID(qa_app_apply_frontend_preferences, (qa_app_frontend_preference_overrides overrides, qa_app_application_input *input, qa_app_application_audio *audio))
UNREACHED_WEAK(qa_app_campaign_cinematic *, qa_app_campaign_cinematic_prepare_content, (qa_app_screen_cinematic_request request, const qa_content_mounted *mounts, qa_app_application_assets *assets, qa_app_application_audio *audio, qa_app_native_renderer *renderer, qa_seat_id seat, bool (*current)(void *ctx), void *current_ctx, qa_application_screen_cinematic_captions *captions), NULL)
UNREACHED_WEAK(qa_app_campaign_cinematic *, qa_app_campaign_cinematic_restore_content, (const qa_save_value *value, const qa_content_mounted *mounts, qa_app_application_assets *assets, qa_app_application_audio *audio, qa_app_native_renderer *renderer, qa_seat_id seat, bool (*current)(void *ctx), void *current_ctx, qa_application_screen_cinematic_captions *captions), NULL)
UNREACHED_WEAK_VOID(qa_app_client_bootstrap_publish_output, (qa_app_client_bootstrap *client, qa_unified_audio *engine))
UNREACHED_WEAK(qa_app_component_media_host, qa_app_component_media_host_of, (qa_app_shared_simulation *simulation, qa_app_application_audio *audio, qa_app_application_assets *assets), (qa_app_component_media_host){0})
UNREACHED_WEAK(qa_cvar_registry *, qa_application_create_client_cvars, (qa_application *app, qa_app_shared_simulation *simulation, qa_app_loaded_content *content, const qa_app_options *options, qa_session_seat *seat, const char *userinfo, qa_cvar_registry *previous, const qa_cvar_archive_entry *archive, size_t archive_count, bool selected_identity), NULL)
UNREACHED_WEAK(bool, qa_application_create_q3_seat_client, (qa_application *app, qa_app_local_input *local, qa_app_application_assets *assets, qa_app_application_audio *audio, qa_app_application_input *input, qa_app_native_renderer *renderer, qa_app_shared_simulation *simulation, const qa_cvar_snapshot *settings, size_t setting_count, qa_application_local_q3_guest_world *local_guest, qa_application_local_q3_guest_browser *browser, qa_cvar_registry *cvars, qa_application_q3_seat_client *out), false)
UNREACHED_WEAK(qa_app_seat_ui *, qa_application_create_seat_ui, (qa_application *app, qa_app_local_input *local, qa_native_ui_art *art, qa_app_application_input *input, qa_app_native_renderer *renderer, qa_app_shared_simulation *simulation, qa_text_font_selection font, qa_app_application_audio *audio, const qa_app_menu_typography *typography, qa_app_rerelease_presentation *rerelease, qa_app_loaded_content *content, const qa_app_options *options, bool qvm, const qa_seat_id *client_cvar_seats, qa_cvar_registry *const *client_cvars, size_t client_cvar_count), NULL)
UNREACHED_WEAK(qa_app_world_seat_presentation *, qa_application_create_world_seat_presentation, (qa_application *app, qa_app_local_input *local, qa_app_application_assets *assets, qa_app_native_renderer *renderer, qa_app_shared_simulation *simulation, double seats, qa_text_font_selection font, const qa_q3_character_assets *characters, qa_app_seat_ui *ui, qa_app_application_effects *effects, qa_application_q3_seat_client *source_client, qa_app_rerelease_presentation *rerelease, qa_application_native_q2_seat_client *native_seat, qa_app_loaded_content *content), NULL)
UNREACHED_WEAK_VOID(qa_application_guest_options, (qa_app_loaded_content *content, const qa_app_options *options, const qa_application_host *host, qa_command_buffer *(*guest_commands)(void *ctx), void *guest_ctx, qa_source_debug_graph *graph, void (*native_command)(void *ctx, const char *text), void *native_ctx, qa_app_simulation_options *out))
UNREACHED_WEAK(qa_command_context, qa_application_native_command_context, (qa_app_loaded_content *content, qa_session_id session), (qa_command_context){0})
UNREACHED_WEAK(qa_application_local_q3_guest_browser *, qa_application_open_guest_browser, (qa_application *app), NULL)
UNREACHED_WEAK_VOID(qa_application_prepare_guest_bots, (qa_application *app, qa_app_loaded_content *content, qa_app_shared_simulation *simulation))
UNREACHED_WEAK_VOID(qa_application_open_graphical, (qa_application *app, const qa_save_image *initial_save))
UNREACHED_WEAK_VOID(qa_application_publish_borrowed, (qa_application *app, qa_app_client_bootstrap *client, const qa_app_image_client_settings *images))
UNREACHED_WEAK_VOID(qa_application_prepare_local_guest_seat, (qa_application *app, qa_application_local_q3_guest_world *world, qa_app_shared_simulation *simulation, qa_session_seat *seat, qa_cvar_registry *cvars, const char *userinfo, const void *initial))
UNREACHED_WEAK(bool, qa_application_prepare_native_q2_seat, (qa_application *app, qa_app_local_input *local, qa_app_application_assets *assets, qa_app_shared_simulation *simulation, qa_cvar_registry *cvars, qa_application_native_q2_seat_client *out), false)
UNREACHED_WEAK(qa_actor_id, qa_application_prepare_restored_guest_seat, (qa_application *app, qa_application_local_q3_guest_world *world, qa_app_shared_simulation *simulation, qa_session_seat *seat, qa_app_loaded_content *content, const qa_app_options *options, qa_cvar_registry *previous, const qa_cvar_archive_entry *archive, size_t archive_count), (qa_actor_id){0})
UNREACHED_WEAK(qa_application_saved_clients *, qa_application_prepare_saved_clients, (qa_application *app, const double *slots, size_t slot_count, const qa_app_sim_bot_transport_connection *bot_clients, size_t bot_count), NULL)
UNREACHED_WEAK(bool, qa_application_prepare_video_restart, (qa_application *app, qa_app_prepared_video_presentation *out), false)
UNREACHED_WEAK_VOID(qa_application_publish_local_guest_snapshots_with, (qa_application *app, qa_application_local_q3_guest_world *local, qa_app_shared_simulation *simulation))
UNREACHED_WEAK_VOID(qa_application_request_restored_scores, (qa_application *app))
UNREACHED_WEAK_VOID(qa_application_restore_source_input_angles, (qa_application *app, qa_app_application_input *input, qa_app_shared_simulation *simulation, const qa_seat_id *seat))
UNREACHED_WEAK_VOID(qa_application_start_team_arena_skirmish, (qa_application *app, const qa_app_team_arena_skirmish *skirmish))
UNREACHED_WEAK(const qa_q3_character_assets *, qa_app_load_q3_character_for_content, (qa_app_loaded_content *content, qa_content_id appearance, const char *model), NULL)
UNREACHED_WEAK(const char *, qa_app_native_renderer_driver_name, (qa_app_native_renderer *renderer), NULL)
UNREACHED_WEAK(qa_app_qvm_display_renderer, qa_app_native_renderer_qvm_display, (qa_app_native_renderer *renderer), (qa_app_qvm_display_renderer){0})
UNREACHED_WEAK_VOID(qa_app_prepare_application_save_native, (const qa_app_options *options, const qa_content_catalog *catalog, const char *path, const char *source_product, qa_app_prepared_application_save *out, void **out_backing))
UNREACHED_WEAK_VOID(qa_app_prepared_application_save_free, (qa_app_prepared_application_save *save, void *backing))
UNREACHED_WEAK_VOID(qa_app_prepare_world_resources, (qa_app_application_assets *assets, qa_app_shared_simulation *simulation, qa_app_application_audio *audio, qa_app_application_effects *effects, const qa_application_loading *loading, void (*print)(void *ctx, const char *text), void *print_ctx))
UNREACHED_WEAK(const char *const *, qa_app_q3_source_runtime_setting_names, (qa_app_q3_source_runtime *runtime, size_t *out_count), NULL)
UNREACHED_WEAK_VOID(qa_app_save_audio_settings_from, (const qa_config_store *store, qa_app_application_audio *audio))
UNREACHED_WEAK(qa_app_simulation_prediction_source, qa_app_shared_sim_prediction_source, (qa_app_shared_simulation *sim), (qa_app_simulation_prediction_source){0})
UNREACHED_WEAK(qa_app_sim_presentation_access, qa_app_shared_sim_presentation_access, (qa_app_shared_simulation *sim), (qa_app_sim_presentation_access){0})
UNREACHED_WEAK(const qa_scene_queries *, qa_app_shared_sim_scene_queries, (qa_app_shared_simulation *sim), NULL)
/* P6 changeLocalPlayers (application_dispatch.c, being ported). */
UNREACHED_WEAK_VOID(qa_application_change_local_players_seat, (qa_application *app, const char *kind, const char *const *args, size_t count, const qa_seat_id *invoking_seat))
#pragma GCC diagnostic pop

static qa_catalog_discovered_product test_product(const char *id, qa_game_family family, const char *edition,
    const char *directory, const char *user_root) {
  qa_catalog_discovered_product product;
  memset(&product, 0, sizeof product);
  product.id = (char *)id;
  product.expectation.id = id;
  product.expectation.family = family;
  product.expectation.edition = edition;
  product.expectation.campaign = "";
  product.expectation.content_directory = directory;
  if (user_root != NULL) {
    product.user_content = calloc(1, sizeof *product.user_content);
    product.user_content->root = (char *)user_root;
  }
  return product;
}
static qa_app_loaded_content *test_content(qa_content_catalog *catalog, const char *engine, const char *map, qa_content_mounted *mounts) {
  qa_executable_recipe recipe;
  memset(&recipe, 0, sizeof recipe);
  recipe.engine_behavior.content = engine;
  recipe.engine_behavior.provider = "p:engine";
  recipe.map.entities.content = map;
  recipe.map.entities.provider = "p:map";
  recipe.match.provider = "m:ffa";
  qa_app_content_host host;
  memset(&host, 0, sizeof host);
  qa_app_options options;
  memset(&options, 0, sizeof options);
  qa_app_loaded_content *content = qa_app_load_application_content(host, options, &recipe, true);
  qa_app_loaded_content_set_catalog(content, catalog);
  qa_app_loaded_content_set_mounts(content, mounts);
  return content;
}

/* oracle dialect=[...] */
static void test_content_dialect(void) {
  qa_catalog_discovered_product products[5] = {
    test_product("q1/id1", QA_GAME_FAMILY_Q1, "classic", "id1", NULL), test_product("q1/qw", QA_GAME_FAMILY_Q1, "quakeworld", "qw", NULL),
    test_product("q2/base", QA_GAME_FAMILY_Q2, "classic", "baseq2", NULL), test_product("q2/rr", QA_GAME_FAMILY_Q2, "rerelease", "rr", NULL),
    test_product("q3/base", QA_GAME_FAMILY_Q3, "retail", "baseq3", NULL) };
  qa_content_catalog catalog;
  memset(&catalog, 0, sizeof catalog);
  catalog.products = products; catalog.product_count = 5;
  const char *ids[5] = { "q1/id1", "q1/qw", "q2/base", "q2/rr", "q3/base" };
  qa_command_dialect expected[5] = { QA_COMMAND_DIALECT_Q1_NETQUAKE, QA_COMMAND_DIALECT_Q1_QUAKEWORLD, QA_COMMAND_DIALECT_Q2_CLASSIC,
    QA_COMMAND_DIALECT_Q2_RERELEASE, QA_COMMAND_DIALECT_Q3 };
  for (size_t i = 0; i < 5; i++) {
    qa_app_loaded_content *content = test_content(&catalog, ids[i], ids[i], NULL);
    CHECK(qa_application_content_dialect(content) == expected[i]);
    qa_app_loaded_content_destroy(content);
  }
}

/* oracle config.source / config.client */
static void test_product_configs(void) {
  qa_catalog_discovered_product products[2] = {
    test_product("q1/id1", QA_GAME_FAMILY_Q1, "classic", "id1", "/tmp/qa-user/id1-own"), test_product("q3/base", QA_GAME_FAMILY_Q3, "retail", "baseq3", NULL) };
  qa_content_catalog catalog;
  memset(&catalog, 0, sizeof catalog);
  catalog.products = products; catalog.product_count = 2;
  qa_app_loaded_content *content = test_content(&catalog, "q3/base", "q1/id1", NULL);
  qa_app_options options;
  memset(&options, 0, sizeof options);
  options.user_content_root = "/tmp/qa-user";
  qa_config_store source = qa_application_source_config(&options, content), client = qa_application_client_config(&options, content);
  CHECK_STR(source.root, "/tmp/qa-user/id1-own");
  CHECK_STR(client.root, "/tmp/qa-user/baseq3");
  qa_config_store_destroy(&source); qa_config_store_destroy(&client);
  qa_app_loaded_content_destroy(content);
  free(products[0].user_content);
}

static qa_cvar_registry *session_registry(qa_command_dialect dialect, qa_engine_session *session) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context.session = qa_engine_session_id(session);
  options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  return qa_cvar_registry_create(&options);
}
static void register_value(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  CHECK(qa_cvar_registry_register(cvars, name, value, 0, &snapshot));
  qa_cvar_read_free(&snapshot);
}

/* oracle graph.cvars / graph.none */
static void test_debug_graph_settings(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  qa_debug_graph_settings none = qa_application_debug_graph_settings(app);
  CHECK(none.debuggraph == 0 && none.timegraph == 0 && none.netgraph == 0 && none.height == 32 && none.scale == 1 && none.shift == 0);
  qa_cvar_registry *cvars = registry(QA_COMMAND_DIALECT_Q1_NETQUAKE);
  register_value(cvars, "debuggraph", "1"); register_value(cvars, "timegraph", "0"); register_value(cvars, "netgraph", "0");
  register_value(cvars, "graphheight", "48"); register_value(cvars, "graphscale", "2.5"); register_value(cvars, "graphshift", "-3");
  sim->source.kind = QA_SS_SOURCE_Q1;
  qa_ss_set_source_strategy(sim);
  sim->source.q1_cvars = cvars;
  qa_debug_graph_settings set = qa_application_debug_graph_settings(app);
  CHECK(set.debuggraph == 1 && set.timegraph == 0 && set.netgraph == 0 && set.height == 48 && set.scale == 2.5 && set.shift == -3);
  qa_application_destroy(app);
  qa_cvar_registry_destroy(cvars);
  free(sim);
}

/* oracle prep.* (prepareSourceCommands, Q1 branch) */
static int action_sentinel;
static qa_application_command_request actions[8];
static size_t action_count;
static void record_action(void *ctx, const qa_application_command_request *request) {
  CHECK(ctx == &action_sentinel);
  if (action_count < 8) actions[action_count++] = *request;
  else { qa_application_command_request copy = *request; qa_application_command_request_free(&copy); }
}
static int compare_names(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static void test_prepare_source_commands(void) {
  qa_engine_session *session = qa_engine_session_create(&identity, (qa_session_mode){ QA_SESSION_MODE_HEADLESS });
  qa_catalog_discovered_product products[1] = { test_product("q1/id1", QA_GAME_FAMILY_Q1, "classic", "id1", NULL) };
  qa_content_catalog catalog;
  memset(&catalog, 0, sizeof catalog);
  catalog.products = products; catalog.product_count = 1;
  qa_resolved_mount_plan plan;
  memset(&plan, 0, sizeof plan);
  plan.id = "mount-plan:test:0";
  qa_open_mount_options mount_options;
  memset(&mount_options, 0, sizeof mount_options);
  qa_content_mounted *mounts = qa_open_mount_plan(&plan, &mount_options);
  CHECK(mounts != NULL);
  qa_app_loaded_content *content = test_content(&catalog, "q1/id1", "q1/id1", mounts);
  qa_app_shared_simulation *sim = fake_simulation();
  sim->options.max_clients = 4;
  qa_cvar_registry *cvars = session_registry(QA_COMMAND_DIALECT_Q1_NETQUAKE, session);
  sim->source.kind = QA_SS_SOURCE_Q1;
  qa_ss_set_source_strategy(sim);
  sim->source.q1_cvars = cvars;
  qa_application *app = make_app(sim, NULL, NULL, 0);
  app->session = session;
  app->loaded_content = content;
  action_count = 0;
  qa_application_prepared_source_commands *prepared = qa_application_prepare_source_commands(app, sim, content, false, record_action, &action_sentinel);
  CHECK(prepared->options != NULL && prepared->options->dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE);
  CHECK(prepared->commands != NULL && prepared->program == NULL && prepared->q2_console == NULL);
  size_t name_count = 0;
  char **names = qa_command_buffer_registered_names(prepared->commands, &name_count);
  qsort(names, name_count, sizeof *names, compare_names);
  static const char *const expected[] = { "addbot","alias","botlist","cd","changelevel","cmd","cmdlist","color","cvarlist","dec","echo","exec","fly",
    "gamemap","give","giveall","god","inc","kick","kill","load","map","modcmd","music","name","noclip","notarget","pause","ping","play","quit",
    "removebot","reset","resetall","s_info","s_list","s_stop","save","say","set","seta","sets","setu","sizedown","sizeup","snd_restart",
    "soundinfo","soundlist","status","stopsound","stuffcmds","suicide","toggle","vstr","wait" };
  size_t expected_count = sizeof expected / sizeof expected[0];
  CHECK(name_count == expected_count);
  for (size_t i = 0; i < name_count && i < expected_count; i++) {
    if (strcmp(names[i], expected[i]) != 0) { fprintf(stderr, "name %zu: %s != %s\n", i, names[i], expected[i]); failures++; break; }
  }
  for (size_t i = 0; i < name_count; i++) free(names[i]);
  free(names);
  static const struct { const char *name, *value; double flags; } registered[] = {
    {"cl_avidemo","0",0},{"cl_forceavidemo","0",0},{"debuggraph","0",0},{"timegraph","0",0},{"netgraph","0",0},
    {"graphheight","32",0},{"graphscale","1",0},{"graphshift","0",0},{"sv_autosave","1",1},{"sv_autosave_interval","0",1} };
  for (size_t i = 0; i < 10; i++) {
    qa_cvar_read read;
    CHECK(qa_cvar_registry_find(cvars, registered[i].name, &read));
    char *value = qa_application_cvar_string(cvars, registered[i].name);
    CHECK_STR(value, registered[i].value);
    CHECK(read.flags == registered[i].flags);
    free(value);
    qa_cvar_read_free(&read);
  }
  (void)qa_command_buffer_execute_now(prepared->commands, "map e1m2 x", NULL, -1);
  (void)qa_command_buffer_execute_now(prepared->commands, "say hello", NULL, -1);
  CHECK(action_count == 2);
  CHECK(actions[0].target == QA_APPLICATION_TARGET_APPLICATION);
  CHECK_STR(actions[0].name, "map");
  CHECK(actions[0].argument_count == 2);
  CHECK_STR(actions[0].arguments[0], "e1m2");
  CHECK_STR(actions[0].arguments[1], "x");
  CHECK(!actions[0].has_seat && actions[0].has_source && actions[0].source.origin.kind == QA_COMMAND_ORIGIN_SERVER_CONSOLE);
  CHECK_STR(actions[1].name, "say");
  CHECK(actions[1].argument_count == 1);
  CHECK_STR(actions[1].arguments[0], "hello");
  /* a component may queue a declared engine command (map) */
  qa_command_producer producer;
  memset(&producer, 0, sizeof producer);
  producer.kind = "game-module";
  producer.module.id = "mod:x";
  producer.instance = &producer;
  qa_command_context component;
  memset(&component, 0, sizeof component);
  component.session = qa_engine_session_id(session);
  component.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  component.producer = &producer;
  (void)qa_command_buffer_execute_now(prepared->commands, "map e1m3", &component, -1);
  CHECK(action_count == 3);
  for (size_t i = 0; i < action_count; i++) qa_application_command_request_free(&actions[i]);
  prepared->destroy(prepared->ctx);
  app->loaded_content = NULL;
  app->session = NULL;
  qa_application_destroy(app);
  qa_cvar_registry_destroy(cvars);
  free(sim);
  qa_app_loaded_content_destroy(content);
  qa_content_mounted_destroy(mounts);
  qa_engine_session_destroy(session);
}

/* oracle kick.*, restart.error, append, append.error */
typedef struct kick_case { qa_application *app; size_t count; const char *const *args; } kick_case;
static void kick_targets_case(void *ctx) {
  kick_case *c = ctx;
  qa_actor_id *out = NULL;
  (void)qa_application_kick_targets(c->app, c->args, c->count, &out);
  free(out);
}
static void restart_case(void *ctx) { kick_case *c = ctx; qa_application_request_restart(c->app, NULL, 0); }
static void append_case(void *ctx) {
  kick_case *c = ctx;
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "server-command"; event.text = "x";
  qa_app_sim_presentation_event source;
  memset(&source, 0, sizeof source);
  source.kind = "q1"; source.q1 = &event;
  const qa_app_sim_presentation_event *events[1] = { &source };
  qa_application_append_q1_commands(c->app, events, 1);
}
static int append_print_sentinel;
static void append_print(void *ctx, const char *text, const qa_command_context *source) { (void)source; CHECK(ctx == &append_print_sentinel); (void)text; }
static void test_command_errors(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  qa_caught_error caught;
  kick_case c = { app, 0, NULL };
  CHECK(!qa_try(kick_targets_case, &c, &caught));
  CHECK_STR(caught.message, "Usage: kick <player name|slot|all|allbots>");
  const char *ranger[1] = { "Ranger" };
  c.count = 1; c.args = ranger;
  CHECK(!qa_try(kick_targets_case, &c, &caught));
  CHECK_STR(caught.message, "Player Ranger is not on the server");
  CHECK(!qa_try(restart_case, &c, &caught));
  CHECK_STR(caught.message, "map_restart requires the Quake III game provider");
  CHECK(!qa_try(append_case, &c, &caught));
  CHECK_STR(caught.message, "Q1 source console has no command owner");
  /* append: only q1 server-command events reach the buffer */
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q1_NETQUAKE;
  options.print = append_print; options.print_ctx = &append_print_sentinel;
  app->source_commands = qa_command_buffer_create(&options);
  qa_q1_event command, sound;
  memset(&command, 0, sizeof command); memset(&sound, 0, sizeof sound);
  command.kind = "server-command"; command.text = "changelevel e1m2\n";
  sound.kind = "sound";
  qa_app_sim_presentation_event a, b;
  memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
  a.kind = "q1"; a.q1 = &command;
  b.kind = "q1"; b.q1 = &sound;
  const qa_app_sim_presentation_event *events[2] = { &a, &b };
  qa_application_append_q1_commands(app, events, 2);
  char *pending = qa_command_buffer_pending_text(app->source_commands);
  CHECK_STR(pending, "changelevel e1m2\n");
  free(pending);
  qa_application_destroy(app);
  free(sim->ordered_players); /* the fake simulation's players() cache */
  free(sim);
}

/* oracle guest.error, operator */
static void guest_output_case(void *ctx) { (void)qa_application_guest_output(ctx, NULL, true); }
static int operator_sentinel, original_sentinel;
static char operator_log[256];
static void original_print(void *ctx, const char *text) {
  CHECK(ctx == &original_sentinel);
  strncat(operator_log, "host:", sizeof operator_log - strlen(operator_log) - 1);
  strncat(operator_log, text, sizeof operator_log - strlen(operator_log) - 1);
  strncat(operator_log, ",", sizeof operator_log - strlen(operator_log) - 1);
}
static void operator_write(void *ctx, const char *text) {
  CHECK(ctx == &operator_sentinel);
  strncat(operator_log, "op:", sizeof operator_log - strlen(operator_log) - 1);
  strncat(operator_log, text, sizeof operator_log - strlen(operator_log) - 1);
  strncat(operator_log, ",", sizeof operator_log - strlen(operator_log) - 1);
}
static void test_guest_output_and_operator_host(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  app->launch_options.network_kind = "q3-server";
  qa_caught_error caught;
  CHECK(!qa_try(guest_output_case, app, &caught));
  CHECK_STR(caught.message, "Q3 guest has no server output owner");
  qa_application_destroy(app);
  free(sim);
  qa_application_host host;
  memset(&host, 0, sizeof host);
  host.ctx = &original_sentinel; host.print = original_print;
  qa_application_operator_output box = { NULL, NULL };
  void *record = NULL;
  qa_application_host wrapped = qa_application_operator_host_wrap(host, &box, &record);
  operator_log[0] = '\0';
  wrapped.print(wrapped.ctx, "a");
  box.write = operator_write; box.ctx = &operator_sentinel;
  wrapped.print(wrapped.ctx, "b");
  box.write = NULL;
  wrapped.print(wrapped.ctx, "c");
  CHECK_STR(operator_log, "host:a,op:b,host:c,");
  free(record);
}

/* oracle dedicated.* */
static void test_dedicated_console(void) {
  qa_engine_session *session = qa_engine_session_create(&identity, (qa_session_mode){ QA_SESSION_MODE_HEADLESS });
  qa_catalog_discovered_product products[1] = { test_product("q1/id1", QA_GAME_FAMILY_Q1, "classic", "id1", NULL) };
  qa_content_catalog catalog;
  memset(&catalog, 0, sizeof catalog);
  catalog.products = products; catalog.product_count = 1;
  qa_app_loaded_content *content = test_content(&catalog, "q1/id1", "q1/id1", NULL);
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  app->session = session;
  app->loaded_content = content;
  qa_application_open_dedicated_console(app);
  CHECK(app->dedicated_console != NULL && app->dedicated_commands != NULL);
  static const char *const names[] = { "addbot", "botlist", "load", "map", "quit", "removebot", "save", "say" };
  for (size_t i = 0; i < 8; i++) CHECK(qa_command_buffer_exists(app->dedicated_commands, names[i]));
  (void)qa_command_buffer_execute_now(app->dedicated_commands, "map e1m4", NULL, -1);
  (void)qa_command_buffer_execute_now(app->dedicated_commands, "addbot 3", NULL, -1);
  size_t count = 0;
  qa_application_command_request *queued = qa_event_ring_take(&app->requested_commands, &app->requested_cursor, &count);
  CHECK(count == 2);
  if (count == 2) {
    CHECK_STR(queued[0].name, "map");
    CHECK(queued[0].argument_count == 1 && !queued[0].has_seat && !queued[0].has_source);
    CHECK_STR(queued[0].arguments[0], "e1m4");
    CHECK_STR(queued[1].name, "addbot");
    CHECK_STR(queued[1].arguments[0], "3");
  }
  for (size_t i = 0; i < count; i++) qa_application_command_request_free(&queued[i]);
  free(queued);
  (void)qa_command_buffer_execute_now(app->dedicated_commands, "quit", NULL, -1);
  CHECK(app->stopping);
  app->loaded_content = NULL;
  app->session = NULL;
  qa_application_destroy(app);
  free(sim);
  qa_app_loaded_content_destroy(content);
  qa_engine_session_destroy(session);
}


/* ==== P3 members (oracle skirmish / guest.queue / reader) ==== */
static void test_team_arena_skirmish(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  /* oracle skirmish[0]: no Q3 source, simulation defaults to this.simulation */
  CHECK(!qa_application_is_team_arena_skirmish(app, NULL));
  CHECK(!qa_application_is_team_arena_skirmish(app, sim));
  qa_application_destroy(app);
  free(sim);
}
static void test_queue_local_guest_command(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  qa_application_local_q3_guest_world current, candidate;
  memset(&current, 0, sizeof current); memset(&candidate, 0, sizeof candidate);
  CHECK(qa_event_ring_init(&candidate.pending_commands, 64, sizeof(qa_application_command_request)));
  qa_event_cursor pending_cursor;
  CHECK(qa_event_ring_open_cursor(&candidate.pending_commands, &pending_cursor, false));
  app->local_guest = &current;
  qa_seat_id seat0 = qa_identity_owner_seat(&identity, 0), seat1 = qa_identity_owner_seat(&identity, 1);
  qa_application_command_request score, team;
  memset(&score, 0, sizeof score); memset(&team, 0, sizeof team);
  score.target = QA_APPLICATION_TARGET_CLIENT; score.name = strdup("score"); score.has_seat = true; score.seat = seat0;
  team.target = QA_APPLICATION_TARGET_CLIENT; team.name = strdup("team"); team.has_seat = true; team.seat = seat1;
  team.arguments = malloc(sizeof *team.arguments); team.arguments[0] = strdup("red"); team.argument_count = 1;
  qa_application_queue_local_guest_command(app, &current, &score);
  qa_application_queue_local_guest_command(app, &candidate, &team);
  size_t count = 0;
  qa_application_command_request *queued = qa_event_ring_take(&app->requested_commands, &app->requested_cursor, &count);
  CHECK(count == 1 && strcmp(queued[0].name, "score") == 0 && queued[0].argument_count == 0);
  for (size_t i = 0; i < count; i++) qa_application_command_request_free(&queued[i]);
  free(queued);
  qa_application_command_request *pending = qa_event_ring_take(&candidate.pending_commands, &pending_cursor, &count);
  CHECK(count == 1 && strcmp(pending[0].name, "team") == 0 && pending[0].argument_count == 1 && strcmp(pending[0].arguments[0], "red") == 0);
  for (size_t i = 0; i < count; i++) qa_application_command_request_free(&pending[i]);
  free(pending);
  qa_event_ring_close_cursor(&candidate.pending_commands, &pending_cursor);
  qa_event_ring_destroy(&candidate.pending_commands);
  app->local_guest = NULL;
  qa_application_destroy(app);
  free(sim);
}
static void write_text(const char *path, const char *text) {
  FILE *file = fopen(path, "w");
  CHECK(file != NULL);
  if (file != NULL) { fputs(text, file); fclose(file); }
}
static void test_startup_script_reader(void) {
  const char *root = "/tmp/qa-application-core-p3-scripts";
  char command[256];
  snprintf(command, sizeof command, "rm -rf %s && mkdir -p %s/base %s/game %s/user", root, root, root, root);
  CHECK(system(command) == 0);
  write_text("/tmp/qa-application-core-p3-scripts/base/default.cfg", "base default");
  write_text("/tmp/qa-application-core-p3-scripts/base/shared.cfg", "base shared");
  write_text("/tmp/qa-application-core-p3-scripts/game/shared.cfg", "game shared");
  write_text("/tmp/qa-application-core-p3-scripts/user/user.cfg", "user text");
  qa_catalog_discovered_product products[2] = {
    test_product("q1/id1", QA_GAME_FAMILY_Q1, "classic", "id1", NULL),
    test_product("q1/mod", QA_GAME_FAMILY_Q1, "classic", "mod", "/tmp/qa-application-core-p3-scripts/user") };
  products[0].loose_root = "/tmp/qa-application-core-p3-scripts/base";
  products[1].loose_root = "/tmp/qa-application-core-p3-scripts/game";
  products[1].expectation.base_product = "q1/id1";
  qa_content_catalog catalog;
  memset(&catalog, 0, sizeof catalog);
  catalog.products = products; catalog.product_count = 2;
  qa_app_loaded_content *content = test_content(&catalog, "q1/mod", "q1/mod", NULL);
  qa_app_options options;
  memset(&options, 0, sizeof options);
  qa_app_prepared_scoped_read_fn read = NULL;
  void *read_ctx = NULL;
  CHECK(qa_application_startup_script_reader(content, &options, NULL, &read, &read_ctx));
  qa_command_context source;
  memset(&source, 0, sizeof source);
  source.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  /* oracle reader[0..4] */
  static const struct { const char *name, *scope, *expected; } cases[] = {
    { "shared.cfg", "loose", "game shared" }, { "default.cfg", "loose", "base default" }, { "shared.cfg", "base-loose", "base shared" },
    { "default.cfg", "game-loose", NULL }, { "user.cfg", "game-loose", "user text" } };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    char *text = read(read_ctx, cases[i].name, source, cases[i].scope);
    if (cases[i].expected == NULL) CHECK(text == NULL);
    else CHECK_STR(text, cases[i].expected);
    free(text);
  }
  qa_application_startup_script_reader_free(read_ctx);
  qa_app_loaded_content_destroy(content);
  free(products[1].user_content);
}

/* ---- P3-assigned fixes ---- */
/* A request queued by a script command keeps its context after the script's
 * chunk is freed (TS keeps the caller frames and producer alive). Under ASan a
 * borrowed context reads freed storage here. */
static int lifetime_sentinel;
typedef struct lifetime_case { qa_application *app; } lifetime_case;
static lifetime_case *lifetime_current;
static void lifetime_map(void *ctx, qa_command_invocation *command) {
  CHECK(ctx == &lifetime_sentinel);
  const char *args[1] = { command->argc > 1 ? command->argv[1] : "" };
  qa_application_queue_command_source(lifetime_current->app, "map", args, 1, NULL, &command->source);
}
static void test_request_context_lifetime(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  lifetime_case c = { app };
  lifetime_current = &c;
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q1_NETQUAKE;
  options.context.session = identity.session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  qa_command_buffer *buffer = qa_command_buffer_create(&options);
  CHECK(buffer != NULL);
  CHECK(qa_command_buffer_register_engine(buffer, "map", lifetime_map, &lifetime_sentinel, NULL));
  /* A script ("autoexec.cfg") run from the local console, produced by a game module. */
  char *script_name = strdup("autoexec.cfg"), *module_id = strdup("mod:test");
  qa_command_origin caller;
  memset(&caller, 0, sizeof caller);
  caller.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  qa_command_producer *producer = calloc(1, sizeof *producer);
  producer->kind = "game-module"; producer->module.id = module_id;
  producer->module.artifact_path = "a"; producer->module.digest = "d"; producer->module.revision = "d";
  qa_command_context script;
  memset(&script, 0, sizeof script);
  script.session = identity.session;
  script.origin.kind = QA_COMMAND_ORIGIN_SCRIPT;
  script.origin.name = script_name;
  script.origin.caller = &caller;
  script.producer = producer;
  qa_command_buffer_append(buffer, "map e1m3\n", &script, -1);
  /* The script's own storage goes away before the buffer runs, and the chunk copy after dispatch. */
  free(script_name); free(module_id); free(producer);
  CHECK(qa_command_buffer_execute(buffer) == 1);
  qa_command_buffer_destroy(buffer);
  size_t count = 0;
  qa_application_command_request *queued = qa_event_ring_take(&app->requested_commands, &app->requested_cursor, &count);
  CHECK(count == 1);
  if (count == 1) {
    CHECK(queued[0].has_source);
    CHECK(queued[0].source.origin.kind == QA_COMMAND_ORIGIN_SCRIPT);
    CHECK_STR(queued[0].source.origin.name, "autoexec.cfg");
    CHECK(queued[0].source.origin.caller != NULL && queued[0].source.origin.caller->kind == QA_COMMAND_ORIGIN_LOCAL_CONSOLE);
    CHECK(queued[0].source.producer != NULL);
    if (queued[0].source.producer != NULL) {
      CHECK_STR(queued[0].source.producer->kind, "game-module");
      CHECK_STR(queued[0].source.producer->module.id, "mod:test");
    }
    CHECK(strcmp(queued[0].arguments[0], "e1m3") == 0);
  }
  for (size_t i = 0; i < count; i++) qa_application_command_request_free(&queued[i]);
  free(queued);
  qa_application_destroy(app);
  free(sim);
}
/* qa_app_shared_sim_q2_native writes only the out-pointers it is given. */
static void test_q2_native_null_outputs(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  CHECK(!qa_app_shared_sim_q2_native(sim, NULL, NULL));
  sim->source.kind = QA_SS_SOURCE_Q2_NATIVE;
  qa_ss_set_source_strategy(sim);
  sim->source.classic_world = (qa_app_sim_classic_guest_world *)&lifetime_sentinel;
  CHECK(qa_app_shared_sim_q2_native(sim, NULL, NULL));
  qa_app_sim_classic_guest_world *classic = NULL;
  CHECK(qa_app_shared_sim_q2_native(sim, &classic, NULL));
  CHECK(classic == (qa_app_sim_classic_guest_world *)&lifetime_sentinel);
  free(sim);
}


/* ==== P4 / P5 members (application_transition.c, application_travel.c) ==== */
typedef struct travel_case { qa_application *app; const char *text; } travel_case;
static void save_case(void *ctx) { travel_case *c = ctx; qa_application_save_game(c->app, c->text, NULL); }
static void load_case(void *ctx) { travel_case *c = ctx; qa_application_load_game(c->app, c->text, NULL); }
static void restore_case(void *ctx) { travel_case *c = ctx; qa_application_restore_saved_game(c->app, c->text, NULL); }
static void replace_case(void *ctx) { travel_case *c = ctx; qa_application_replace_world(c->app, c->text, NULL); }
static void change_level_case(void *ctx) { travel_case *c = ctx; qa_application_change_level(c->app, c->text, NULL); }
static void apply_case(void *ctx) { travel_case *c = ctx; qa_application_apply_transition(c->app); }
static void report_case(void *ctx) {
  travel_case *c = ctx;
  qa_caught_error error;
  memset(&error, 0, sizeof error);
  error.name = "Error";
  snprintf(error.message, sizeof error.message, "%s", c->text);
  qa_application_report_campaign_travel_error(c->app, &error);
}
/* oracle save.guards / load.guards / replace.guards / change.guards */
static void test_world_operation_guards(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  qa_content_catalog catalog;
  memset(&catalog, 0, sizeof catalog);
  qa_app_loaded_content *content = test_content(&catalog, "q1/id1", "q1/id1", NULL);
  app->loaded_content = content;
  qa_caught_error caught;
  travel_case c = { app, "slot1.sav" };
  /* saveGame: closed first, then the save policy. */
  app->closed = true;
  CHECK(!qa_try(save_case, &c, &caught));
  CHECK_STR(caught.message, "Application is closed");
  app->closed = false;
  const char *unavailable = qa_application_save_unavailable(app, QA_SAVE_PURPOSE_MANUAL);
  CHECK(unavailable != NULL);
  CHECK(!qa_try(save_case, &c, &caught));
  CHECK(unavailable != NULL && strcmp(caught.message, unavailable) == 0);
  CHECK(app->world_operation == QA_APPLICATION_IDLE);
  /* loadGame: hosting, then a stepping application; restoreSavedGame: cinematic, then a running operation. */
  qa_application_native_server server;
  memset(&server, 0, sizeof server);
  app->network = &server;
  CHECK(!qa_try(load_case, &c, &caught));
  CHECK_STR(caught.message, "Save/load unavailable while hosting a network game.");
  app->network = NULL;
  app->stepping = true;
  CHECK(!qa_try(load_case, &c, &caught));
  CHECK_STR(caught.message, "Save restoration requires an idle open application");
  app->stepping = false;
  app->world_operation = QA_APPLICATION_SAVING;
  CHECK(!qa_try(restore_case, &c, &caught));
  CHECK_STR(caught.message, "Another world operation is in progress");
  CHECK(!qa_try(replace_case, &c, &caught));
  CHECK_STR(caught.message, "Another world operation is in progress");
  app->world_operation = QA_APPLICATION_IDLE;
  qa_application_movie movie;
  memset(&movie, 0, sizeof movie);
  movie.current = movie_current; movie.complete = movie_complete;
  qa_application_set_campaign_movie(app, &movie);
  CHECK(!qa_try(restore_case, &c, &caught));
  CHECK_STR(caught.message, "Finish or skip the campaign cinematic before loading a save");
  CHECK(!qa_try(replace_case, &c, &caught));
  CHECK_STR(caught.message, "Finish or skip the campaign cinematic before changing worlds");
  qa_application_set_campaign_movie(app, NULL);
  CHECK(app->world_operations == 0);
  /* changeLevel requires an idle open application. */
  app->stepping = true;
  CHECK(!qa_try(change_level_case, &c, &caught));
  CHECK_STR(caught.message, "World travel requires an idle open application");
  app->stepping = false;
  app->loaded_content = NULL;
  qa_application_destroy(app);
  qa_app_loaded_content_destroy(content);
  free(sim);
}
/* oracle report / apply.complete / apply.unknown */
static void test_transition_reports(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  qa_caught_error caught;
  travel_case c = { app, "Map e9m9 is not installed" };
  print_count = 0; printed[0] = '\0';
  CHECK(qa_try(report_case, &c, &caught));
  CHECK_STR(printed, "Campaign transition failed: Map e9m9 is not installed\n");
  app->fatal_world_failure = true;
  CHECK(!qa_try(report_case, &c, &caught));
  CHECK_STR(caught.message, "Map e9m9 is not installed");
  app->fatal_world_failure = false;
  /* applyTransition: no pending work is a no-op; campaign completion prints; another kind needs a match provider. */
  print_count = 0; printed[0] = '\0';
  CHECK(qa_try(apply_case, &c, &caught));
  CHECK(print_count == 0);
  memset(&app->pending_transition, 0, sizeof app->pending_transition);
  app->pending_transition.kind = "campaign-complete";
  app->has_pending_transition = true;
  CHECK(qa_try(apply_case, &c, &caught));
  CHECK(!app->has_pending_transition);
  CHECK_STR(printed, "Campaign complete.\n");
  app->pending_transition.kind = "round-restart";
  app->has_pending_transition = true;
  CHECK(!qa_try(apply_case, &c, &caught));
  CHECK_STR(caught.message, "Round restart requires the selected match provider");
  CHECK(!app->has_pending_transition);
  CHECK(!qa_application_capture_blocks_transition(app));
  qa_application_destroy(app);
  free(sim);
}
/* oracle viewport.* and movie requests */
static void test_viewport_and_movies(void) {
  qa_session_client *client0 = qa_session_client_create(qa_identity_owner_client(&identity, 0, 0));
  qa_session_client *client1 = qa_session_client_create(qa_identity_owner_client(&identity, 1, 0));
  qa_session_seat *s0 = qa_session_seat_create(qa_identity_owner_seat(&identity, 0), client0);
  qa_session_seat *s1 = qa_session_seat_create(qa_identity_owner_seat(&identity, 1), client1);
  qa_client_id clients[2] = { qa_session_client_id(client1), qa_session_client_id(client0) };
  qa_session_seat *seats[2] = { s1, s0 }; /* insertion order differs from seat order */
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, clients, seats, 2);
  qa_rect first = qa_application_local_viewport(app, qa_identity_owner_seat(&identity, 0), 640, 480);
  qa_rect second = qa_application_local_viewport(app, qa_identity_owner_seat(&identity, 1), 640, 480);
  qa_rect expected0 = qa_app_seat_viewport(0, 2, 640, 480), expected1 = qa_app_seat_viewport(1, 2, 640, 480);
  CHECK(first.x == expected0.x && first.y == expected0.y && first.width == expected0.width && first.height == expected0.height);
  CHECK(second.x == expected1.x && second.y == expected1.y && second.width == expected1.width && second.height == expected1.height);
  CHECK(first.x == 0 && first.y == 0 && first.width == 640 && first.height == 240);
  CHECK(second.x == 0 && second.y == 240 && second.width == 640 && second.height == 240);
  /* reserveMovie: one request per simulation, a new token each time. */
  uint64_t a = qa_application_reserve_movie(app, sim), b = qa_application_reserve_movie(app, sim);
  CHECK(a != b);
  CHECK(app->movie_request_count == 1 && app->movie_request_simulations[0] == sim && app->movie_requests[0] == b);
  qa_application_destroy(app);
  free(sim);
  qa_session_seat_destroy(s0); qa_session_seat_destroy(s1);
  qa_session_client_destroy(client0); qa_session_client_destroy(client1);
}

/* ---- P6 (application_dispatch.c) ---- */
/* oracle p6.commands.* / p6.commandActor / p6.pendingShell: graphics-free dispatch through
 * applicationCommand, guestCommand and sourceCommand; a caught error prints its message
 * through the host (no seat, no graphics) and the next request still runs. */
static void p6_command_actor(void *ctx) { (void)qa_application_command_actor(ctx, NULL); }
static void test_p6_commands(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  sim->options.max_clients = 1;
  qa_application *app = make_app(sim, NULL, NULL, 0);
  const char *names[] = { "cinematicpause", "foo", "+grapple", "local_join", "say", "quit" };
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) qa_application_queue_command(app, names[i], NULL, 0, NULL);
  print_count = 0; printed[0] = '\0';
  qa_application_commands(app);
  CHECK_STR(printed, "No cinematic is playing\n" "Unknown application command: foo\n" "Offhand commands require an invoking local seat\n"
      "Local player changes require an active graphical world\n" "Selected guest source chat requires its native command channel\n");
  CHECK(print_count == 5);
  CHECK(app->stopping == true);
  CHECK(qa_event_ring_pending(&app->requested_commands, &app->requested_cursor) == 0);
  qa_caught_error caught;
  CHECK(!qa_try(p6_command_actor, app, &caught));
  CHECK_STR(caught.message, "Command requires a player");
  CHECK(!qa_application_pending_shell_publication(app));
  /* rankLocalMatch without host.localRankings / lobby is a no-op; finishQ2Match outside a Q2 deathmatch is false. */
  qa_application_rank_local_match(app);
  CHECK(!qa_application_finish_q2_match(app, NULL));
  qa_application_destroy(app);
  free(sim);
}

/* presentOutput without a graphical owner (dedicated): TS bumps presentationFrames, publishes
 * the frame time for the call and restores the previous presentationMilliseconds in finally. */
static void test_present_output_dedicated(void) {
  qa_app_shared_simulation *sim = fake_simulation();
  qa_application *app = make_app(sim, NULL, NULL, 0);
  qa_simulation_output output;
  memset(&output, 0, sizeof output);
  qa_application_present_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.time_milliseconds = 250;
  frame.frame_milliseconds = 16;
  qa_application_present_output(app, output, &frame);
  CHECK(app->presentation_frames == 1);
  CHECK(!app->has_presentation_milliseconds);
  app->has_presentation_milliseconds = true;
  app->presentation_milliseconds = 40;
  qa_application_present_output(app, output, &frame);
  CHECK(app->presentation_frames == 2);
  CHECK(app->has_presentation_milliseconds && app->presentation_milliseconds == 40);
  CHECK(app->unhandled_effect_count == 0);
  qa_application_destroy(app);
  free(sim);
}

int main(void) {
  test_p6_commands();
  test_present_output_dedicated();
  CHECK(qa_identity_owner_create(&identity, "application-core"));
  test_construct();
  test_ranking_menu_requests();
  test_override_seats();
  test_level_recovery();
  test_source_cvars();
  test_queue_command();
  test_input();
  test_finished();
  test_campaign_movie();
  test_directories();
  test_minimum_frame();
  test_frame_loop();
  test_transition_callback();
  test_content_dialect();
  test_product_configs();
  test_debug_graph_settings();
  test_prepare_source_commands();
  test_command_errors();
  test_guest_output_and_operator_host();
  test_dedicated_console();
  test_team_arena_skirmish();
  test_queue_local_guest_command();
  test_startup_script_reader();
  test_request_context_lifetime();
  test_q2_native_null_outputs();
  test_world_operation_guards();
  test_transition_reports();
  test_viewport_and_movies();
  qa_identity_owner_destroy(&identity);
  if (failures != 0) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
  printf("test_application_core: ok\n");
  return 0;
}
