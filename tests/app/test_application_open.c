/* Application.open / StartupApplication.open composition
 * (TS src/app/bootstrap/application.ts openSource/step/run/close,
 * startup.ts open/step/run/close): dedicated open composes content,
 * identity, EngineSession and the shared simulation; step drives
 * the session through the frame loop; close tears down in TS
 * order. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "qa/app/bootstrap/application.h"
#include "qa/app/bootstrap/startup.h"
#include "qa/world/session/session.h"

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

static char *dup_text(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  memcpy(p, s, n);
  return p;
}

/* ---- fixture host state ---- */
typedef struct fixture {
  int sequence;
  int content_opened, content_closed_seq;
  int sim_steps, sim_closes, sim_close_seq;
  double sim_elapsed_total;
  int host_open_seq, host_close_seq, host_closes;
  char stages[8][64]; int stage_count;
  char printed[512];
  char commands[8][64]; int command_count;
  char last_command_arg[64];
  const char *engine_content;
  qa_game_family engine_family;
  qa_session_id session_id;
} fixture;

static bool fixture_product(void *ctx, const char *id, qa_game_family *family,
                            const char **edition, const char **campaign,
                            bool *installed, bool *builtin) {
  fixture *f = ctx;
  *family = f->engine_content && strcmp(id, f->engine_content) == 0
                ? f->engine_family : QA_GAME_FAMILY_Q1;
  *edition = "classic";
  *campaign = "base";
  *installed = true;
  *builtin = true;
  return true;
}
static bool fixture_require(void *ctx, const char *id) { (void)ctx; (void)id; return true; }
static const char *fixture_open_loaded(void *ctx, qa_app_loaded_content *content) {
  fixture *f = ctx;
  f->content_opened++;
  qa_executable_recipe recipe = {0};
  recipe.map.geometry.requested_path = "maps/e1m1.bsp";
  recipe.movement.provider = "q1:official";
  recipe.character.appearance.provider = "q1:official";
  recipe.engine_behavior.content = f->engine_content ? f->engine_content : "q1-engine";
  qa_app_loaded_content_set_recipe(content, recipe);
  qa_app_loaded_content_set_mounts(content, (const void *)"fixture-mounts");
  return NULL;
}
static const char *fixture_close_loaded(void *ctx, qa_app_loaded_content *content) {
  fixture *f = ctx;
  (void)content;
  f->content_closed_seq = ++f->sequence;
  return NULL;
}
static const char *fixture_noop_content(void *ctx, qa_app_loaded_content *content) {
  (void)ctx; (void)content; return NULL;
}
static const char *fixture_noop_mods(void *ctx, qa_app_loaded_content *content, const char *purpose) {
  (void)ctx; (void)content; (void)purpose; return NULL;
}
static qa_simulation_output fixture_sim_step(void *ctx, qa_input_batch input) {
  fixture *f = ctx;
  f->sim_steps++;
  f->sim_elapsed_total += input.elapsed_milliseconds;
  qa_simulation_output out = {0};
  out.snapshot.session = f->session_id;
  return out;
}
static qa_save_image fixture_sim_checkpoint(void *ctx) {
  (void)ctx;
  qa_save_image image = {0};
  return image;
}
static void fixture_sim_close(void *ctx) {
  fixture *f = ctx;
  f->sim_closes++;
  f->sim_close_seq = ++f->sequence;
}
static void fixture_print(void *ctx, const char *text) {
  fixture *f = ctx;
  strncat(f->printed, text, sizeof f->printed - strlen(f->printed) - 1);
}
static void fixture_stage(void *ctx, const char *message) {
  fixture *f = ctx;
  if (f->stage_count < 8) {
    (void)snprintf(f->stages[f->stage_count], sizeof f->stages[0], "%s", message);
    f->stage_count++;
  }
}
static const char *fixture_host_open(void *ctx, qa_app_application *app) {
  fixture *f = ctx;
  /* The world is composed before the graphical host phase runs. */
  qa_engine_session *session = qa_app_application_session(app);
  if (session) f->session_id = qa_engine_session_id(session);
  f->host_open_seq = ++f->sequence;
  return NULL;
}
static const char *fixture_host_close(void *ctx, qa_app_application *app) {
  fixture *f = ctx;
  (void)app;
  f->host_closes++;
  f->host_close_seq = ++f->sequence;
  return NULL;
}
static const char *fixture_execute_command(void *ctx, const char *name,
                                           const char *const *args, size_t count,
                                           bool has_seat, qa_seat_id seat,
                                           bool has_source, qa_command_context source) {
  fixture *f = ctx;
  (void)has_seat; (void)seat; (void)has_source; (void)source;
  if (f->command_count < 8)
    (void)snprintf(f->commands[f->command_count++], sizeof f->commands[0], "%s", name);
  if (count > 0) (void)snprintf(f->last_command_arg, sizeof f->last_command_arg, "%s", args[0]);
  return NULL;
}

static qa_app_options fixture_options(bool dedicated, const char *product, const char *mode) {
  qa_app_options options = {0};
  options.product = dup_text(product);
  options.map = dup_text("maps/e1m1.bsp");
  options.corpus_root = dup_text("/corpus");
  options.mode = mode;
  options.network_kind = "offline";
  options.dedicated = dedicated;
  options.skill = 1;
  options.seed = 7;
  return options;
}
static qa_app_application_host fixture_host(fixture *f) {
  qa_app_application_host host = {0};
  host.ctx = f;
  host.print = fixture_print;
  host.has_loading = true;
  host.loading_stage = fixture_stage;
  host.open = fixture_host_open;
  host.close = fixture_host_close;
  host.execute_command = fixture_execute_command;
  host.has_content = true;
  host.content.ctx = f;
  host.content.product = fixture_product;
  host.content.require_product = fixture_require;
  host.content.open_loaded = fixture_open_loaded;
  host.content.close_loaded = fixture_close_loaded;
  host.content.prepare_mods = fixture_noop_mods;
  host.content.prepare_grapple = fixture_noop_content;
  host.content.prepare_weapon_behaviors = fixture_noop_content;
  host.has_simulation = true;
  host.simulation.ctx = f;
  host.simulation.step = fixture_sim_step;
  host.simulation.checkpoint = fixture_sim_checkpoint;
  host.simulation.close = fixture_sim_close;
  return host;
}

/* ---- startup fixture ---- */
typedef struct startup_fixture {
  char order[16]; size_t order_len;
  bool run_entry, fail_select;
  int sequence, persist_seq, close_seq, closes, polled;
  char opened_path[128];
  const char *marker_path;
} startup_fixture;
static void sf_mark(startup_fixture *sf, char code) {
  if (sf->order_len < sizeof sf->order - 1) {
    sf->order[sf->order_len++] = code;
    sf->order[sf->order_len] = '\0';
  }
}
static const char *sf_discover(void *ctx) { sf_mark(ctx, 'd'); return NULL; }
static const char *sf_prefs(void *ctx) { sf_mark(ctx, 'l'); return NULL; }
static const char *sf_splash(void *ctx) { sf_mark(ctx, 'p'); return NULL; }
static const char *sf_select(void *ctx) {
  startup_fixture *sf = ctx;
  sf_mark(sf, 's');
  return sf->fail_select ? "fixture select failed" : NULL;
}
static const char *sf_renderer(void *ctx) { sf_mark(ctx, 'r'); return NULL; }
static const char *sf_audio(void *ctx) { sf_mark(ctx, 'a'); return NULL; }
static const char *sf_input(void *ctx) { sf_mark(ctx, 'i'); return NULL; }
static const char *sf_menu_or_game(void *ctx, bool run_entry) {
  startup_fixture *sf = ctx;
  sf_mark(sf, 'm');
  sf->run_entry = run_entry;
  return NULL;
}
static const char *sf_lobby(void *ctx) { sf_mark(ctx, 'g'); return NULL; }
static const char *sf_pump(void *ctx) { (void)ctx; return NULL; }
static bool sf_true(void *ctx) { (void)ctx; return true; }
static const char *sf_game(void *ctx) { (void)ctx; return "fixture-game"; }
static void sf_persist(void *ctx) {
  startup_fixture *sf = ctx;
  sf->persist_seq = ++sf->sequence;
}
static const char *sf_close(void *ctx) {
  startup_fixture *sf = ctx;
  sf->closes++;
  sf->close_seq = ++sf->sequence;
  if (sf->marker_path) {
    FILE *marker = fopen(sf->marker_path, "w");
    if (marker) { fputs("closed", marker); fclose(marker); }
  }
  return NULL;
}
static bool sf_open_path(void *ctx, const char *path) {
  startup_fixture *sf = ctx;
  (void)snprintf(sf->opened_path, sizeof sf->opened_path, "%s", path);
  return true;
}
static bool sf_poll(void *ctx, qa_sdl_event *out) {
  startup_fixture *sf = ctx;
  sf->polled++;
  memset(out, 0, sizeof *out);
  return true;
}
static const char *sf_backend(void *ctx) { (void)ctx; return "cpu"; }
static qa_app_startup_host startup_host(startup_fixture *sf) {
  qa_app_startup_host host = {0};
  host.ctx = sf;
  host.discover_content = sf_discover;
  host.load_preferences = sf_prefs;
  host.open_splash = sf_splash;
  host.select_product = sf_select;
  host.open_renderer = sf_renderer;
  host.open_audio = sf_audio;
  host.open_input = sf_input;
  host.open_menu_or_game = sf_menu_or_game;
  host.open_lobby = sf_lobby;
  host.pump_frame = sf_pump;
  host.has_selection = sf_true;
  host.has_listening_server = sf_true;
  host.hosted_game = sf_game;
  host.persist_preferences = sf_persist;
  host.close_services = sf_close;
  host.open_path = sf_open_path;
  host.poll_event = sf_poll;
  host.backend = sf_backend;
  return host;
}

int main(void) {
  /* Dedicated open composes content + session + simulation. */
  fixture f = {0};
  qa_app_application *app = qa_app_application_open(
      fixture_options(true, "q1-classic-id1", "deathmatch"), fixture_host(&f),
      NULL, false, false, (qa_app_frontend_preference_overrides){0});
  CHECK(app != NULL);
  CHECK(f.content_opened == 1);
  CHECK(qa_app_application_content(app) != NULL);
  CHECK(qa_app_application_session(app) != NULL);
  CHECK(qa_app_application_simulation(app) != NULL);
  CHECK(qa_engine_session_world(qa_app_application_session(app)) != NULL);
  CHECK(qa_app_application_local_seat_count(app) == 0);
  CHECK(qa_app_application_max_clients(app) == 16); /* deathmatch default */
  CHECK(f.stage_count == 4);
  CHECK(strcmp(f.stages[0], "Preparing configuration...") == 0);
  CHECK(strcmp(f.stages[1], "Loading map...") == 0);
  CHECK(strcmp(f.stages[2], "Preparing world...") == 0);
  CHECK(strcmp(f.stages[3], "Starting game...") == 0);
  CHECK(strstr(f.printed, "Loaded maps/e1m1.bsp with q1:official and q1:official.\n") != NULL);
  CHECK(!qa_app_application_finished(app));

  /* step drives the session world and advances TS counters. */
  const void *output = NULL;
  CHECK(qa_app_application_step(app, 16, &output) == NULL);
  CHECK(qa_app_application_step(app, 16, &output) == NULL);
  CHECK(f.sim_steps == 2);
  CHECK(f.sim_elapsed_total == 32);
  CHECK(qa_app_application_frame_count(app) == 2);
  CHECK(qa_app_application_time_milliseconds(app) == 32);
  CHECK(output == (const void *)qa_app_application_last_output(app));

  /* Queued commands keep name/args and dispatch on step. */
  const char *say_args[] = {"hi", "there"};
  qa_app_application_queue_command(app, "say", say_args, 2, false, (qa_seat_id){0},
                                   false, (qa_command_context){0});
  CHECK(qa_app_application_queued_command_count(app) == 1);
  CHECK(strcmp(qa_app_application_queued_command_name(app, 0), "say") == 0);
  CHECK(qa_app_application_queued_command_argument_count(app, 0) == 2);
  CHECK(strcmp(qa_app_application_queued_command_argument(app, 0, 1), "there") == 0);
  CHECK(qa_app_application_step(app, 8, &output) == NULL);
  CHECK(qa_app_application_queued_command_count(app) == 0);
  CHECK(f.command_count == 1 && strcmp(f.commands[0], "say") == 0);
  CHECK(strcmp(f.last_command_arg, "hi") == 0);

  /* TS step guards abort with the exact contract. */
  CHECK_ABORTS(qa_app_application_step(app, 0, &output));
  CHECK_ABORTS(qa_app_application_step(app, -1, &output));
  CHECK_ABORTS(qa_app_application_step(app, NAN, &output));

  /* Stopping with no local players: retained output, no advance. */
  double frames_before = qa_app_application_frame_count(app);
  qa_app_application_request_quit(app);
  CHECK(qa_app_application_finished(app));
  CHECK(qa_app_application_step(app, 16, &output) == NULL);
  CHECK(qa_app_application_frame_count(app) == frames_before);
  CHECK(f.sim_steps == 3);

  /* No retained lobby: the exact TS error text. */
  CHECK(strcmp(qa_app_application_return_to_lobby(app),
               "This session has no retained lobby") == 0);
  /* Dedicated framebuffer reads use the exact TS message. */
  uint8_t *bytes = (uint8_t *)1;
  size_t count = 9;
  CHECK(strcmp(qa_app_application_read_pixels(app, &bytes, &count),
               "Dedicated applications have no framebuffer") == 0);
  CHECK(bytes == NULL && count == 0);

  /* Close ordering: world (sim bundle) -> host services -> content. */
  CHECK(qa_app_application_close(app) == NULL);
  CHECK(f.sim_closes == 1);
  CHECK(f.sim_close_seq > 0 && f.sim_close_seq < f.host_close_seq);
  CHECK(f.host_close_seq < f.content_closed_seq);
  CHECK(qa_app_application_session(app) == NULL);
  CHECK(qa_app_application_simulation(app) == NULL);
  CHECK(qa_app_application_content(app) == NULL);
  CHECK(qa_app_application_close(app) == NULL); /* idempotent */
  CHECK(f.host_closes == 1);
  CHECK_ABORTS(qa_app_application_step(app, 16, &output));
  qa_seat_input_event event = {0};
  CHECK_ABORTS(qa_app_application_input(app, &event));
  qa_app_application_destroy(app);

  /* run() reaches the frame limit through the real step path. */
  fixture f2 = {0};
  qa_app_options run_options = fixture_options(true, "q1-quakeworld", "deathmatch");
  run_options.has_frame_limit = true;
  run_options.frame_limit = 5;
  qa_app_application *run_app = qa_app_application_open(
      run_options, fixture_host(&f2), NULL, false, false,
      (qa_app_frontend_preference_overrides){0});
  CHECK(qa_app_application_max_clients(run_app) == 8); /* q1-quakeworld */
  CHECK(qa_app_application_run(run_app, 10) == NULL);
  CHECK(qa_app_application_frame_count(run_app) == 5);
  CHECK(f2.sim_steps == 5);
  qa_app_application_destroy(run_app);

  /* Singleplayer capacity follows the engine product family. */
  fixture f3 = {0};
  f3.engine_content = "q3-engine";
  f3.engine_family = QA_GAME_FAMILY_Q3;
  qa_app_application *sp_app = qa_app_application_open(
      fixture_options(true, "q3-baseq3", "singleplayer"), fixture_host(&f3),
      NULL, false, false, (qa_app_frontend_preference_overrides){0});
  CHECK(qa_app_application_max_clients(sp_app) == 8);
  qa_app_application_destroy(sp_app);

  /* Graphical owned open creates loopback session seats; a
   * stopping app with local players still steps (TS step). */
  fixture f4 = {0};
  qa_app_options gfx_options = fixture_options(false, "q1-classic-id1", "deathmatch");
  gfx_options.seats = 2;
  qa_app_application *gfx = qa_app_application_open(
      gfx_options, fixture_host(&f4), NULL, false, false,
      (qa_app_frontend_preference_overrides){0});
  CHECK(qa_app_application_local_seat_count(gfx) == 2);
  qa_engine_session *gfx_session = qa_app_application_session(gfx);
  CHECK(qa_engine_session_client_at(gfx_session, 0) != NULL);
  CHECK(qa_engine_session_client_at(gfx_session, 1) != NULL);
  CHECK(qa_engine_session_client_at(gfx_session, 2) == NULL);
  qa_app_application_request_quit(gfx);
  const void *gfx_output = NULL;
  CHECK(qa_app_application_step(gfx, 16, &gfx_output) == NULL);
  CHECK(qa_app_application_frame_count(gfx) == 1);
  qa_app_application_destroy(gfx);

  /* Remote client kinds are rejected before any composition. */
  fixture f5 = {0};
  qa_app_options remote_options = fixture_options(false, "q3-baseq3", "deathmatch");
  remote_options.network_kind = "q3-client";
  qa_app_application_host remote_host = fixture_host(&f5);
  CHECK_ABORTS(qa_app_application_open(remote_options, remote_host, NULL, false,
                                       false, (qa_app_frontend_preference_overrides){0}));
  CHECK(f5.content_opened == 0);

  /* ---- StartupApplication.open phases over the same fixture style ---- */
  startup_fixture sf = {0};
  qa_app_options startup_options = {0};
  startup_options.has_frame_limit = true;
  startup_options.frame_limit = 3;
  qa_app_startup_application *startup = qa_app_startup_application_open(
      startup_host(&sf), startup_options, "/tmp/qa-saves", "run");
  CHECK(startup != NULL);
  CHECK(strcmp(sf.order, "dlpsraimg") == 0); /* TS open() phase order */
  CHECK(sf.run_entry);
  CHECK(qa_app_startup_application_session(startup) != NULL);
  CHECK(!qa_app_startup_application_finished(startup));
  qa_app_startup_application_pump(startup);
  CHECK(qa_app_startup_application_frame_count(startup) == 1);
  /* Platform services added to the host reach their accessors. */
  qa_sdl_event polled = {0};
  CHECK(qa_app_startup_application_get_event(startup, &polled));
  CHECK(sf.polled == 1);
  CHECK(qa_app_startup_application_open_path(startup, "https://example.invalid"));
  CHECK(strcmp(sf.opened_path, "https://example.invalid") == 0);
  CHECK(qa_app_startup_application_backend(startup) != NULL &&
        strcmp(qa_app_startup_application_backend(startup), "cpu") == 0);
  /* run() pumps until the frame limit (TS run loop condition). */
  qa_app_startup_application_run(startup);
  CHECK(qa_app_startup_application_frame_count(startup) == 3);
  CHECK(qa_app_startup_application_finished(startup));
  /* Quit event requests quit; pump is then a silent no-op (TS step). */
  qa_sdl_event quit_event = {0};
  quit_event.kind = QA_SDL_EVENT_QUIT;
  qa_app_startup_application_handle(startup, &quit_event);
  CHECK(qa_app_startup_application_stopping(startup));
  qa_app_startup_application_pump(startup);
  CHECK(qa_app_startup_application_frame_count(startup) == 3);
  /* Close: persist -> session -> services; idempotent. */
  qa_app_startup_application_close(startup);
  CHECK(sf.persist_seq > 0 && sf.persist_seq < sf.close_seq);
  CHECK(qa_app_startup_application_session(startup) == NULL);
  qa_app_startup_application_close(startup);
  CHECK(sf.closes == 1);
  qa_app_startup_application_destroy(startup);

  /* A failing open phase closes the staged application, then
   * aborts with the phase error (TS open catch). The close is
   * observed through a marker file written by close_services. */
  char marker_path[] = "/tmp/qa-startup-close-XXXXXX";
  int marker_fd = mkstemp(marker_path);
  CHECK(marker_fd >= 0);
  if (marker_fd >= 0) close(marker_fd);
  startup_fixture sf2 = {0};
  sf2.fail_select = true;
  sf2.marker_path = marker_path;
  qa_app_startup_host failing_host = startup_host(&sf2);
  qa_app_options failing_options = {0};
  checks++;
  pid_t pid = fork();
  if (pid == 0) {
    (void)prctl(PR_SET_DUMPABLE, 0);
    freopen("/dev/null", "w", stderr);
    (void)qa_app_startup_application_open(failing_host, failing_options, "/tmp/qa-saves", "menu");
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {
    failures++;
    fprintf(stderr, "FAIL %s:%d: failing startup open did not abort\n", __FILE__, __LINE__);
  }
  FILE *marker = fopen(marker_path, "r");
  char marker_text[16] = {0};
  if (marker) {
    (void)fread(marker_text, 1, sizeof marker_text - 1, marker);
    fclose(marker);
  }
  CHECK(strcmp(marker_text, "closed") == 0);
  (void)unlink(marker_path);

  printf("test_application_open: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
