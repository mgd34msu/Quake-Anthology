/* C checks for the src/app/bootstrap/input.ts port (ApplicationInput,
 * movementDialect), input-devices.ts, controller-settings.ts and
 * render-settings.ts. The TS suite only reaches ApplicationInput through a
 * whole Application; the expected values here come from running the TS
 * module under bun on the same inputs (scratchpad oracle input_oracle.ts),
 * plus tests/render/alias-shadow.test.ts for render-settings.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/app/bootstrap/controller_settings.h"
#include "qa/app/bootstrap/input.h"
#include "qa/app/bootstrap/input_devices.h"
#include "qa/app/bootstrap/render_settings.h"
#include "qa/contracts/execution.h"
#include "qa/foundation/support.h"
#include "qa/materials/compile.h"
#include "qa/platform/controller.h"
#include "qa/platform/sdl.h"

static int checks, failures;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

/* Sentinel ctx: every callback below asserts it receives this cell. */
static int sentinel_cell;
#define SENTINEL ((void *)&sentinel_cell)
static int bad_ctx;
#define EXPECT_CTX(ctx) do { if ((ctx) != SENTINEL) bad_ctx++; } while (0)

/* A raised TS error: runs fn, expects a raise with name and message. */
typedef void (*raise_fn)(void *ctx);
static bool raises(raise_fn fn, void *ctx, const char *name, const char *message) {
  qa_caught_error caught;
  if (qa_try(fn, ctx, &caught)) return false;
  bool ok = strcmp(caught.name, name) == 0 && (message == NULL || strcmp(caught.message, message) == 0);
  if (!ok) fprintf(stderr, "  raised %s: %s\n", caught.name, caught.message);
  return ok;
}

static bool text_eq(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

/* ---- render-settings (tests/render/alias-shadow.test.ts) ---- */
static qa_identity_owner render_owner;
static qa_cvar_registry *new_registry(const qa_identity_owner *owner, qa_command_dialect dialect) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context.session = owner->session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  return qa_cvar_registry_create(&options);
}
static void set_cvar(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(cvars, name, value, false, &snapshot)) qa_cvar_read_free(&snapshot);
}

static void test_render_settings(void) {
  CHECK(qa_identity_owner_create(&render_owner, "shadow-settings"));
  qa_cvar_registry *cvars = new_registry(&render_owner, QA_COMMAND_DIALECT_Q1_NETQUAKE);
  qa_app_register_render_settings(cvars);
  CHECK(qa_cvar_registry_variable_value(cvars, "r_shadows") == 0);
  set_cvar(cvars, "r_shadows", "1");
  CHECK(qa_cvar_registry_variable_value(cvars, "r_shadows") == 1);
  set_cvar(cvars, "r_shadows", "NaN");
  CHECK(qa_cvar_registry_variable_value(cvars, "r_shadows") == 1);
  qa_cvar_registry *restored = new_registry(&render_owner, QA_COMMAND_DIALECT_Q1_NETQUAKE);
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_register(restored, "r_shadows", "1", 0, &snapshot)) qa_cvar_read_free(&snapshot);
  qa_app_register_render_settings(restored);
  CHECK(qa_cvar_registry_variable_value(restored, "r_shadows") == 1);
  qa_cvar_registry_destroy(restored);
  qa_cvar_registry_destroy(cvars);

  /* GAPMAT-004 renderer cvars reach the shader profile, rail widths and curve error. */
  cvars = new_registry(&render_owner, QA_COMMAND_DIALECT_Q1_NETQUAKE);
  CHECK(qa_app_q3_renderer_settings_from(NULL).curve_error == 250);
  qa_app_register_render_settings(cvars);
  qa_mat_finish_profile profile = qa_app_shader_profile_from_cvars(cvars);
  CHECK(!profile.vertex_light && profile.detail_textures);
  qa_app_q3_renderer_settings settings = qa_app_q3_renderer_settings_from(cvars);
  CHECK(settings.rail.core_width == 6 && settings.rail.ring_width == 16 && settings.rail.segment_length == 32);
  CHECK(settings.curve_error == 250);
  set_cvar(cvars, "r_vertexLight", "1"); set_cvar(cvars, "r_detailTextures", "0"); set_cvar(cvars, "r_railWidth", "24");
  set_cvar(cvars, "r_railCoreWidth", "3"); set_cvar(cvars, "r_railSegmentLength", "64"); set_cvar(cvars, "r_lodCurveError", "100");
  set_cvar(cvars, "r_railWidth", "-5");
  profile = qa_app_shader_profile_from_cvars(cvars);
  CHECK(profile.vertex_light && !profile.detail_textures);
  settings = qa_app_q3_renderer_settings_from(cvars);
  CHECK(settings.rail.core_width == 3 && settings.rail.ring_width == 24 && settings.rail.segment_length == 64);
  CHECK(settings.curve_error == 100);
  /* Absent registry: the defaults of the profile and rail settings. */
  profile = qa_app_shader_profile_from_cvars(NULL);
  CHECK(profile.vertex_light == qa_default_shader_profile.vertex_light
        && profile.detail_textures == qa_default_shader_profile.detail_textures);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&render_owner);
}

/* ---- movementDialect ---- */
static void test_movement_dialect(void) {
  qa_app_options options;
  memset(&options, 0, sizeof options);
  options.network_kind = "offline";
  options.movement = QA_GAME_FAMILY_Q1;
  CHECK(qa_app_movement_dialect(&options, NULL) == QA_COMMAND_DIALECT_Q1_NETQUAKE);
  options.movement = QA_GAME_FAMILY_Q2;
  CHECK(qa_app_movement_dialect(&options, NULL) == QA_COMMAND_DIALECT_Q2_CLASSIC);
  options.movement = QA_GAME_FAMILY_Q3;
  CHECK(qa_app_movement_dialect(&options, NULL) == QA_COMMAND_DIALECT_Q3);
  options.movement = QA_GAME_FAMILY_Q1;
  options.network_kind = "qw-client";
  CHECK(qa_app_movement_dialect(&options, NULL) == QA_COMMAND_DIALECT_Q1_QUAKEWORLD);
}

/* ---- ApplicationInput ---- */
typedef struct recorder {
  char executed[64][96]; size_t executed_count;
  char printed[32][128]; size_t printed_count;
  qa_ui_binding_capabilities capabilities;
  const char *impulse;
} recorder;
static recorder rec;

static void record_executed(const char *text) {
  if (rec.executed_count < 64) snprintf(rec.executed[rec.executed_count++], 96, "%s", text);
}
static void act_quit(void *ctx) { EXPECT_CTX(ctx); record_executed("quit"); }
static void act_execute(void *ctx, const char *name, const char *const *args, size_t count, const qa_seat_id *seat,
                        const qa_command_context *source) {
  EXPECT_CTX(ctx);
  (void)source;
  char line[96];
  size_t length = (size_t)snprintf(line, sizeof line, "%s(", name);
  for (size_t i = 0; i < count && length < sizeof line; i++)
    length += (size_t)snprintf(line + length, sizeof line - length, "%s%s", i ? "," : "", args[i]);
  if (length < sizeof line) snprintf(line + length, sizeof line - length, ")%s", seat == NULL ? "" : "@seat");
  record_executed(line);
}
static void act_print(void *ctx, const char *text) {
  EXPECT_CTX(ctx);
  if (rec.printed_count < 32) snprintf(rec.printed[rec.printed_count++], 128, "%s", text);
}
static qa_ui_binding_capabilities act_capabilities(void *ctx) { EXPECT_CTX(ctx); return rec.capabilities; }
static bool act_impulse(void *ctx, qa_seat_id seat, qa_provider_id *out) {
  EXPECT_CTX(ctx); (void)seat;
  if (rec.impulse == NULL) return false;
  *out = rec.impulse;
  return true;
}

static double clock_now = 1000;
static double test_now(void *ctx) { EXPECT_CTX(ctx); return clock_now; }

static qa_app_sim_player_view player_view(void *ctx, qa_actor_id actor) {
  EXPECT_CTX(ctx); (void)actor;
  qa_app_sim_player_view view;
  memset(&view, 0, sizeof view);
  view.angles.y = 90; view.view_height = 22;
  return view;
}

static bool ui_input(void *ctx, const qa_seat_input_event *event, qa_seat_input_focus focus) {
  EXPECT_CTX(ctx); (void)event; (void)focus; return false;
}
static void ui_close(void *ctx) { EXPECT_CTX(ctx); }
static void ui_sample(void *ctx, qa_seat_input_sample *sample) { EXPECT_CTX(ctx); (void)sample; }
static void ui_wheel(void *ctx, qa_wheel_mode mode, bool down) { EXPECT_CTX(ctx); (void)mode; (void)down; }
static bool ui_cycle(void *ctx, int direction) {
  EXPECT_CTX(ctx);
  record_executed(direction > 0 ? "cycle1" : "cycle-1");
  return direction > 0;
}
static bool ui_switch(void *ctx, double first, double second) {
  EXPECT_CTX(ctx);
  char line[32];
  snprintf(line, sizeof line, "switch%g/%g", first, second);
  record_executed(line);
  return first == 2;
}

static size_t dump_has_line(qa_seat_console *console, const char *line) {
  char *dump = qa_console_buffer_dump(&console->buffer);
  size_t found = 0;
  for (char *at = dump; at != NULL && *at != '\0';) {
    char *end = strchr(at, '\n');
    size_t length = end != NULL ? (size_t)(end - at) : strlen(at);
    if (length == strlen(line) && strncmp(at, line, length) == 0) found++;
    at = end != NULL ? end + 1 : NULL;
  }
  free(dump);
  return found;
}

static char *read_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return NULL;
  fseek(file, 0, SEEK_END);
  long length = ftell(file);
  fseek(file, 0, SEEK_SET);
  char *text = malloc((size_t)length + 1);
  size_t got = fread(text, 1, (size_t)length, file);
  text[got] = '\0';
  fclose(file);
  return text;
}

typedef struct input_fixture {
  qa_app_application_input *input;
  qa_seat_id seat;
} input_fixture;
static void attach_again(void *ctx) {
  input_fixture *fixture = ctx;
  qa_app_input_ui ui = {SENTINEL, ui_input, ui_close, NULL, ui_sample, ui_wheel, ui_cycle, ui_switch};
  (void)qa_app_application_input_attach_ui(fixture->input, fixture->seat, ui);
}
static void resume_negative(void *ctx) {
  input_fixture *fixture = ctx;
  qa_app_application_input_resume_commands(fixture->input, -1);
}
static qa_seat_id foreign_seat;
static void arsenal_foreign(void *ctx) {
  input_fixture *fixture = ctx;
  qa_app_application_input_set_arsenal_selection(fixture->input, foreign_seat, NULL);
}

static void test_application_input(void) {
  char root_template[] = "/tmp/input-c-oracle-XXXXXX";
  char *root = mkdtemp(root_template);
  CHECK(root != NULL);
  qa_sdl_window_options window_options;
  memset(&window_options, 0, sizeof window_options);
  window_options.title = "oracle"; window_options.width = 64; window_options.height = 48;
  window_options.hidden = true; window_options.backend = QA_SDL_BACKEND_CPU;
  qa_sdl_window window;
  if (!qa_sdl_window_open(&window_options, &window)) { fprintf(stderr, "SKIP: no SDL window\n"); return; }
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "input-oracle"));
  qa_client_id client_id = qa_identity_owner_client(&identity, 0, 1);
  qa_session_client *client = qa_session_client_create(client_id);
  qa_seat_id seat_id = qa_identity_owner_seat(&identity, 0);
  foreign_seat = qa_identity_owner_seat(&identity, 3);
  qa_session_seat *seat = qa_session_seat_create(seat_id, client);
  qa_actor_id actor = qa_identity_owner_actor(&identity, 1, 1);
  qa_config_store store;
  CHECK(qa_config_store_init(&store, root));
  qa_app_options options;
  memset(&options, 0, sizeof options);
  options.network_kind = "offline"; options.movement = QA_GAME_FAMILY_Q3; options.user_content_root = root;
  qa_app_sim_presentation_access simulation;
  memset(&simulation, 0, sizeof simulation);
  simulation.ctx = SENTINEL; simulation.player_view = player_view;
  memset(&rec, 0, sizeof rec);
  rec.capabilities.score_command = "+scores"; rec.capabilities.offhand_grapple = true;
  qa_app_local_player player = {seat, actor};
  qa_app_input_open_args args;
  memset(&args, 0, sizeof args);
  args.window = qa_app_input_window_from_sdl(&window);
  args.players = &player; args.player_count = 1;
  args.options = &options; args.dialect = QA_COMMAND_DIALECT_Q3;
  args.simulation = &simulation;
  args.actions.ctx = SENTINEL;
  args.actions.quit = act_quit; args.actions.execute = act_execute; args.actions.print = act_print;
  args.actions.binding_capabilities = act_capabilities; args.actions.arsenal_impulse_provider = act_impulse;
  args.now = test_now; args.now_ctx = SENTINEL;
  args.settings = &store;
  qa_app_application_input *input = qa_app_application_input_open(&args);
  CHECK(qa_app_application_input_local_count(input) == 1);
  qa_app_local_input *local = qa_app_application_input_local(input, 0);
  qa_command_context seat_context;
  memset(&seat_context, 0, sizeof seat_context);
  seat_context.session = identity.session;
  seat_context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  seat_context.origin.seat = seat_id; seat_context.origin.client = client_id;
  /* dialects q3 q3 q3 */
  qa_command_buffer *commands = qa_app_application_input_commands(input);
  CHECK(qa_cvar_registry_dialect(qa_app_application_input_cvars(input)) == QA_COMMAND_DIALECT_Q3);
  CHECK(qa_command_buffer_dialect(commands) == QA_COMMAND_DIALECT_Q3);
  CHECK(qa_cvar_registry_dialect(qa_app_application_input_input_cvars(input, true, seat_id)) == QA_COMMAND_DIALECT_Q3);
  static const char *const registered[] = {"in_restart", "midiinfo", "quit", "+grapple", "-grenade", "local_join",
                                           "weapnext", "switchweapon", "snd_restart", "find", "bind"};
  for (size_t i = 0; i < sizeof registered / sizeof registered[0]; i++) CHECK(qa_command_buffer_exists(commands, registered[i]));
  CHECK(!qa_command_buffer_exists(commands, "llm"));
  CHECK(qa_app_application_input_local_player_capacity(input) == 4);
  CHECK(!qa_app_application_input_can_remove_local_player(input, seat_id));
  qa_ui_binding_capabilities capabilities = qa_app_application_input_binding_capabilities(input);
  CHECK(!capabilities.chat && text_eq(capabilities.score_command, "+scores") && capabilities.offhand_grapple
        && !capabilities.offhand_grenades);
  /* The device owner's initialization prints reach actions.print. */
  CHECK(rec.printed_count == 3);
  CHECK(text_eq(rec.printed[0], "\n------- Input Initialization -------\n"));
  CHECK(text_eq(rec.printed[1], "Joystick is not active.\n"));
  CHECK(text_eq(rec.printed[2], "------------------------------------\n"));
  rec.printed_count = 0;
  /* offhand ["+grapple()@seat","-grapple()@seat"]; grenades print to the seat console. */
  qa_command_buffer_append(commands, "+grapple k1 100\n-grapple k1 110\n+grenade k2 120\n", &seat_context, -1);
  (void)qa_command_buffer_execute(commands);
  CHECK(rec.executed_count == 2 && text_eq(rec.executed[0], "+grapple()@seat") && text_eq(rec.executed[1], "-grapple()@seat"));
  CHECK(rec.printed_count == 0);
  CHECK(dump_has_line(local->console, "This session has no selected offhand action.") == 1);
  rec.executed_count = 0;
  /* attachUi and the UI-routed commands. */
  qa_app_input_ui ui = {SENTINEL, ui_input, ui_close, NULL, ui_sample, ui_wheel, ui_cycle, ui_switch};
  qa_app_input_ui_attachment *attachment = qa_app_application_input_attach_ui(input, seat_id, ui);
  input_fixture fixture = {input, seat_id};
  CHECK(raises(attach_again, &fixture, "Error", "Seat UI is already attached"));
  qa_command_buffer_append(commands, "weapnext\nweapprev\nswitchweapon 2 3\nswitchweapon 4 5\nswitchweapon x 5\nuse 7\nsay hi\n",
                           &seat_context, -1);
  (void)qa_command_buffer_execute(commands);
  static const char *const ui_expected[] = {"cycle1", "cycle-1", "weapprev()@seat", "switch2/3", "switch4/5",
                                            "switchweapon(4,5)@seat", "switchweapon(x,5)@seat", "use(7)@seat", "say(hi)@seat"};
  CHECK(rec.executed_count == 9);
  for (size_t i = 0; i < 9 && i < rec.executed_count; i++) CHECK(text_eq(rec.executed[i], ui_expected[i]));
  rec.executed_count = 0;
  qa_app_input_ui_attachment_detach(attachment);
  /* print routing: ["plain\n","console\n"] to actions; all three on the seat console. */
  qa_app_application_input_print(input, "plain\n", NULL);
  qa_command_context console_context;
  memset(&console_context, 0, sizeof console_context);
  console_context.session = identity.session; console_context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  qa_app_application_input_print(input, "console\n", &console_context);
  qa_app_application_input_print(input, "seat\n", &seat_context);
  CHECK(rec.printed_count == 2 && text_eq(rec.printed[0], "plain\n") && text_eq(rec.printed[1], "console\n"));
  CHECK(dump_has_line(local->console, "plain") == 1 && dump_has_line(local->console, "console") == 1
        && dump_has_line(local->console, "seat") == 1);
  rec.printed_count = 0;
  /* build 0 q3 1234 5 {"provider":"q3:base","weapon":"q3:railgun","useHoldable":false} */
  qa_app_q3_command_selection selection = {5, 0.5};
  qa_app_application_input_set_q3_command_selection(input, seat_id, selection);
  qa_app_arsenal_selection arsenal = {"q3:base", "q3:railgun"};
  qa_app_application_input_set_arsenal_selection(input, seat_id, &arsenal);
  clock_now = 2000;
  qa_actor_command built = qa_app_application_input_build_for_seat(input, seat_id, 16, 1234.7, 9, 16);
  CHECK(built.sequence == 0 && built.command.kind == QA_USER_COMMAND_Q3);
  CHECK(built.command.as.q3.server_time_milliseconds == 1234 && built.command.as.q3.weapon == 5);
  CHECK(built.arsenal != NULL && text_eq(built.arsenal->provider, "q3:base") && text_eq(built.arsenal->weapon, "q3:railgun")
        && !built.arsenal->use_holdable && !built.arsenal->has_impulse);
  CHECK(qa_same_actor(built.actor, actor) && built.source.kind == QA_COMMAND_SOURCE_LOCAL_SEAT);
  /* build2 1 1 {"provider":"q3:impulse","weapon":null,"useHoldable":false} */
  rec.impulse = "q3:impulse";
  size_t built_count = 0;
  qa_actor_command *second = qa_app_application_input_build(input, 16, 2000, 10, 16, &built_count);
  CHECK(built_count == 1 && second[0].sequence == 1);
  CHECK(second[0].arsenal != NULL && text_eq(second[0].arsenal->provider, "q3:impulse") && second[0].arsenal->weapon == NULL
        && !second[0].arsenal->use_holdable);
  free(second);
  CHECK(raises(resume_negative, &fixture, "RangeError", "Input command sequence must be a nonnegative safe integer"));
  qa_app_application_input_resume_commands(input, 10);
  CHECK(qa_app_application_input_next_command_sequence(input) == 10);
  qa_app_application_input_resume_commands(input, 3);
  CHECK(qa_app_application_input_next_command_sequence(input) == 10);
  CHECK(raises(arsenal_foreign, &fixture, "Error", "Arsenal selection has no local seat"));
  qa_app_application_input_bind_arsenal_provider(input, seat_id, "q3:other");
  CHECK(!qa_app_application_input_profile_changed(input));
  /* sensitivity 7 7 */
  qa_command_buffer_append(commands, "seta sensitivity 7\n", &seat_context, -1);
  (void)qa_command_buffer_execute(commands);
  CHECK(qa_mouse_input_tuning(qa_input_command_builder_mouse(local->builder)).sensitivity == 7);
  CHECK(qa_cvar_registry_variable_value(qa_app_application_input_input_cvars(input, true, seat_id), "sensitivity") == 7);
  /* saved 1 true 7 true 1 {"kind":"automatic"} 19; routing; archives. */
  qa_app_application_input_enable_archive_persistence(input);
  qa_app_application_input_save_settings(input);
  qa_seat_settings saved;
  CHECK(qa_config_store_load_seat(&store, "input/seat-1.json", &saved));
  CHECK(saved.version == 1 && saved.has_always_run && saved.always_run && saved.mouse.sensitivity == 7);
  CHECK(saved.rumble && saved.rumble_strength == 1 && saved.controller.kind == QA_CONTROLLER_AUTOMATIC);
  CHECK(saved.binding_count == 19);
  qa_seat_settings_free(&saved);
  char path[512];
  snprintf(path, sizeof path, "%s/input/routing.json", root);
  char *routing = read_file(path);
  CHECK(routing != NULL && strstr(routing, "\"keyboardSeat\":0") != NULL);
  free(routing);
  snprintf(path, sizeof path, "%s/cvars/movement/q3.json", root);
  char *movement = read_file(path);
  CHECK(movement != NULL && strstr(movement, "\"dialect\":\"q3\"") != NULL && strstr(movement, "\"sensitivity\"") == NULL);
  free(movement);
  snprintf(path, sizeof path, "%s/cvars/input/q3/0.json", root);
  char *mouse = read_file(path);
  CHECK(mouse != NULL && strstr(mouse, "{\"name\":\"sensitivity\",\"value\":\"7\"}") != NULL);
  free(mouse);
  /* closed-exists false false */
  qa_app_application_input_close(input);
  CHECK(!qa_command_buffer_exists(commands, "+grapple") && !qa_command_buffer_exists(commands, "quit"));
  qa_app_application_input_destroy(input);
  qa_session_seat_destroy(seat);
  qa_session_client_destroy(client);
  qa_config_store_destroy(&store);
  qa_identity_owner_destroy(&identity);
  qa_sdl_window_close(&window);
  char command[600];
  snprintf(command, sizeof command, "rm -rf '%s'", root);
  CHECK(system(command) == 0);
}

/* ---- seat changes (scratchpad oracle input_seats_oracle.ts) ---- */
static qa_app_sim_player_view slot_view(void *ctx, qa_actor_id actor) {
  EXPECT_CTX(ctx);
  qa_app_sim_player_view view;
  memset(&view, 0, sizeof view);
  view.angles.y = (double)actor.slot * 10;
  return view;
}
static void seat_execute(void *ctx, const char *name, const char *const *args, size_t count, const qa_seat_id *seat,
                         const qa_command_context *source) {
  EXPECT_CTX(ctx); (void)source;
  char line[96];
  size_t length = (size_t)snprintf(line, sizeof line, "%s(", name);
  for (size_t i = 0; i < count && length < sizeof line; i++)
    length += (size_t)snprintf(line + length, sizeof line - length, "%s%s", i ? "," : "", args[i]);
  if (length < sizeof line) {
    if (seat == NULL) snprintf(line + length, sizeof line - length, ")");
    else snprintf(line + length, sizeof line - length, ")@%lld", (long long)seat->index);
  }
  record_executed(line);
}
typedef struct seat_fixture {
  qa_app_application_input *input;
  qa_session_seat *seats[2];
  qa_app_local_seat_preparation *preparation;
  qa_app_local_player players[2];
  const qa_app_sim_presentation_access *simulation;
} seat_fixture;
static void retire_again(void *ctx) {
  seat_fixture *fixture = ctx;
  qa_app_application_input_retire_local_seats(fixture->input, &fixture->seats[1], 1, NULL);
}
static void publish_players(void *ctx) {
  seat_fixture *fixture = ctx;
  qa_app_local_seat_preparation_publish(fixture->preparation, fixture->players, 2, NULL, NULL, false);
}
static void prepare_empty(void *ctx) {
  seat_fixture *fixture = ctx;
  (void)qa_app_application_input_prepare_local_seats(fixture->input, NULL, 0);
}
static void rebind_short(void *ctx) {
  seat_fixture *fixture = ctx;
  qa_app_application_input_rebind_players(fixture->input, fixture->players, 1, fixture->simulation, true);
}

static void test_seat_changes(void) {
  char root_template[] = "/tmp/input-c-seats-XXXXXX";
  char *root = mkdtemp(root_template);
  CHECK(root != NULL);
  qa_sdl_window_options window_options;
  memset(&window_options, 0, sizeof window_options);
  window_options.title = "oracle"; window_options.width = 64; window_options.height = 48;
  window_options.hidden = true; window_options.backend = QA_SDL_BACKEND_CPU;
  qa_sdl_window window;
  if (!qa_sdl_window_open(&window_options, &window)) { fprintf(stderr, "SKIP: no SDL window\n"); return; }
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "input-seats"));
  qa_client_id client_a = qa_identity_owner_client(&identity, 0, 1), client_b = qa_identity_owner_client(&identity, 1, 1);
  qa_client_id client_c = qa_identity_owner_client(&identity, 2, 1);
  qa_session_client *clients[3] = {qa_session_client_create(client_a), qa_session_client_create(client_b),
                                   qa_session_client_create(client_c)};
  qa_session_seat *seat_a = qa_session_seat_create(qa_identity_owner_seat(&identity, 0), clients[0]);
  qa_session_seat *seat_b = qa_session_seat_create(qa_identity_owner_seat(&identity, 1), clients[1]);
  qa_session_seat *seat_c = qa_session_seat_create(qa_identity_owner_seat(&identity, 1), clients[2]);
  qa_config_store store;
  CHECK(qa_config_store_init(&store, root));
  qa_app_options options;
  memset(&options, 0, sizeof options);
  options.network_kind = "offline"; options.movement = QA_GAME_FAMILY_Q1; options.user_content_root = root;
  qa_app_sim_presentation_access simulation;
  memset(&simulation, 0, sizeof simulation);
  simulation.ctx = SENTINEL; simulation.player_view = slot_view;
  memset(&rec, 0, sizeof rec);
  qa_app_local_player players[2] = {{seat_a, qa_identity_owner_actor(&identity, 1, 1)},
                                    {seat_b, qa_identity_owner_actor(&identity, 2, 1)}};
  qa_app_input_open_args args;
  memset(&args, 0, sizeof args);
  args.window = qa_app_input_window_from_sdl(&window);
  args.players = players; args.player_count = 2;
  args.options = &options; args.dialect = QA_COMMAND_DIALECT_Q1_NETQUAKE;
  args.simulation = &simulation;
  args.actions.ctx = SENTINEL;
  args.actions.quit = act_quit; args.actions.execute = seat_execute; args.actions.print = act_print;
  args.now = test_now; args.now_ctx = SENTINEL;
  args.settings = &store;
  qa_app_application_input *input = qa_app_application_input_open(&args);
  qa_input_router *router = qa_app_application_input_router(input);
  qa_seat_id keyboard;
  /* locals 2 0 {"kind":"none"} {"kind":"automatic"} */
  CHECK(qa_app_application_input_local_count(input) == 2);
  CHECK(qa_input_router_keyboard_seat(router, &keyboard) && keyboard.index == 0);
  CHECK(qa_input_router_controller_selection(router, qa_session_seat_id(seat_a)).kind
        == QA_CONTROLLER_SELECTION_NONE);
  CHECK(qa_input_router_controller_selection(router, qa_session_seat_id(seat_b)).kind == QA_CONTROLLER_SELECTION_AUTOMATIC);
  qa_seat_id no_seat;
  memset(&no_seat, 0, sizeof no_seat);
  CHECK(qa_app_application_input_input_cvars(input, false, no_seat)
        == qa_app_application_input_input_cvars(input, true, qa_session_seat_id(seat_a)));
  CHECK(qa_app_application_input_can_remove_local_player(input, qa_session_seat_id(seat_a)));
  qa_command_context ctx_b;
  memset(&ctx_b, 0, sizeof ctx_b);
  ctx_b.session = identity.session; ctx_b.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  ctx_b.origin.seat = qa_session_seat_id(seat_b); ctx_b.origin.client = client_b;
  qa_command_buffer *commands = qa_app_application_input_commands(input);
  qa_command_buffer_append(commands, "say_team hi\n", &ctx_b, -1);
  (void)qa_command_buffer_execute(commands);
  CHECK(rec.executed_count == 1 && text_eq(rec.executed[0], "say_team(hi)@1"));
  rec.executed_count = 0;
  /* after-retire 1 0 true null; retire-again */
  qa_app_application_input_retire_local_seats(input, &seat_b, 1, NULL);
  CHECK(qa_app_application_input_local_count(input) == 1);
  CHECK(qa_input_router_keyboard_seat(router, &keyboard) && keyboard.index == 0);
  CHECK(qa_app_application_input_local(input, 0)->player.seat == seat_a);
  CHECK(qa_app_application_input_input_cvars(input, true, qa_session_seat_id(seat_b)) == NULL);
  seat_fixture fixture;
  memset(&fixture, 0, sizeof fixture);
  fixture.input = input; fixture.seats[0] = seat_a; fixture.seats[1] = seat_b; fixture.simulation = &simulation;
  CHECK(raises(retire_again, &fixture, "Error", "Retired local seats differ from their active input owners"));
  /* publish-order, after-join 2 3 {y:30} automatic true, publish-twice */
  qa_session_seat *joining[2] = {seat_a, seat_c};
  fixture.preparation = qa_app_application_input_prepare_local_seats(input, joining, 2);
  fixture.players[0] = (qa_app_local_player){seat_c, players[1].actor};
  fixture.players[1] = (qa_app_local_player){seat_a, players[0].actor};
  CHECK(raises(publish_players, &fixture, "Error", "Local input admission changed prepared identities"));
  fixture.players[0] = (qa_app_local_player){seat_a, players[0].actor};
  fixture.players[1] = (qa_app_local_player){seat_c, qa_identity_owner_actor(&identity, 3, 1)};
  publish_players(&fixture);
  CHECK(qa_app_application_input_local_count(input) == 2);
  qa_app_local_input *joined = qa_app_application_input_local(input, 1);
  CHECK(joined != NULL && joined->player.actor.slot == 3);
  CHECK(joined != NULL && qa_input_command_builder_view_angles(joined->builder).y == 30);
  CHECK(qa_input_router_controller_selection(router, qa_session_seat_id(seat_c)).kind == QA_CONTROLLER_SELECTION_AUTOMATIC);
  CHECK(qa_app_application_input_can_remove_local_player(input, qa_session_seat_id(seat_c)));
  fixture.players[1].actor = players[1].actor;
  CHECK(raises(publish_players, &fixture, "Error", "Local input preparation is stale"));
  qa_app_local_seat_preparation_destroy(fixture.preparation);
  qa_command_context ctx_c = ctx_b;
  ctx_c.origin.client = client_c;
  qa_command_buffer_append(commands, "say joined\n", &ctx_c, -1);
  (void)qa_command_buffer_execute(commands);
  CHECK(rec.executed_count == 1 && text_eq(rec.executed[0], "say(joined)@1"));
  CHECK(raises(prepare_empty, &fixture, "Error", "A graphical world needs one to four local players"));
  /* rebind [[4,{y:40}],[5,{y:50}]]; rebind-count */
  qa_app_local_player rebound[2] = {{seat_a, qa_identity_owner_actor(&identity, 4, 1)},
                                    {seat_c, qa_identity_owner_actor(&identity, 5, 1)}};
  qa_app_application_input_rebind_players(input, rebound, 2, &simulation, true);
  CHECK(qa_app_application_input_local(input, 0)->player.actor.slot == 4
        && qa_input_command_builder_view_angles(qa_app_application_input_local(input, 0)->builder).y == 40);
  CHECK(qa_app_application_input_local(input, 1)->player.actor.slot == 5
        && qa_input_command_builder_view_angles(qa_app_application_input_local(input, 1)->builder).y == 50);
  fixture.players[0] = rebound[0];
  CHECK(raises(rebind_short, &fixture, "Error", "World travel changed the local seat count"));
  qa_app_application_input_close(input);
  qa_app_application_input_destroy(input);
  qa_session_seat_destroy(seat_a); qa_session_seat_destroy(seat_b); qa_session_seat_destroy(seat_c);
  for (size_t i = 0; i < 3; i++) qa_session_client_destroy(clients[i]);
  qa_config_store_destroy(&store);
  qa_identity_owner_destroy(&identity);
  qa_sdl_window_close(&window);
  char command[600];
  snprintf(command, sizeof command, "rm -rf '%s'", root);
  CHECK(system(command) == 0);
}

int main(void) {
  setenv("SDL_VIDEODRIVER", "dummy", 0);
  test_render_settings();
  test_movement_dialect();
  test_application_input();
  test_seat_changes();
  CHECK(bad_ctx == 0);
  printf("test_top_input: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
