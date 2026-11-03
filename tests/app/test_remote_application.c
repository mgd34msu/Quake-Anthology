/* C checks for src/app/bootstrap/remote-application.ts (RemoteApplication).
 * Every TS test importing remote-application.ts runs a full client against a
 * server (openSource + step), which is not ported yet; this file reproduces the
 * unit-reachable parts: prepareRemoteConfiguration over the corpus for each
 * native dialect (the client cvars, values, flags and userinfo the TS tests
 * read through clientCommands.cvars, e.g. the download-permission test's
 * saved client.cfg applying before signon), the static recordedOptions,
 * allocateQport, queueCommand and requireQwAddress. Expected values come from
 * TS under bun (the oracle scripts are described beside each table).
 * Content rows need QTS_TEST_CORPUS (the TS corpus root). GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../../src/app/bootstrap/remote_application_internal.h"
#include "qa/app/bootstrap/q1_client_settings.h"
#include "qa/content/roots.h"
#include "qa/foundation/support.h"
#include "qa/ui/settings/accessibility.h"

static int checks, failures;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STR(a, b) do { const char *a_ = (a), *b_ = (b); checks++; \
  if (a_ == NULL || b_ == NULL || strcmp(a_, b_) != 0) { failures++; \
    fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, a_ ? a_ : "(null)", b_ ? b_ : "(null)"); } } while (0)

static int sentinel;
static int random_sentinel;

/* ---- allocateQport --------------------------------------------------- */
/* bun: crypto.getRandomValues mocked to 0,256,513,1,65535,257,0x1201;
 * RemoteApplication.prototype.allocateQport.call({ qports: new Set() }). */
static const uint16_t random_values[] = { 0, 256, 513, 1, 65535, 257, 0x1201 };
static size_t random_at;
static uint16_t next_random(void *ctx) {
  CHECK(ctx == &random_sentinel);
  return random_values[random_at++];
}
static void test_allocate_qport(void) {
  qa_remote_application app;
  memset(&app, 0, sizeof app);
  app.random_u16 = next_random; app.random_ctx = &random_sentinel;
  static const uint16_t expected[] = { 1, 258, 515, 4, 65535, 261, 4614 };
  for (size_t i = 0; i < 7; i++) CHECK(qa_remote_application_allocate_qport(&app) == expected[i]);
  CHECK(app.qport_count == 7);
  for (size_t i = 0; i < 7; i++) CHECK(app.qports[i] == expected[i]);
  free(app.qports);
}

/* ---- recordedOptions ------------------------------------------------- */
/* bun: RemoteApplication["recordedOptions"](options, { kind, path, bytes }). */
typedef struct recorded_row {
  qa_app_demo_family kind;
  const char *product, *character_model, *remote_directory;
  qa_game_family character; const char *character_model_in;
  const char *prior_base, *prior_directory;
  const char *expected_product, *expected_base, *expected_directory, *expected_model;
  qa_game_family expected_family;
} recorded_row;
static void test_recorded_options(void) {
  static const recorded_row rows[] = {
    { QA_APP_DEMO_Q1, "q1-classic-id1", NULL, NULL, QA_GAME_FAMILY_Q1, "player", NULL, NULL, "q1-classic-id1", NULL, NULL, "player", QA_GAME_FAMILY_Q1 },
    { QA_APP_DEMO_QW, "q1-classic-id1", NULL, NULL, QA_GAME_FAMILY_Q1, "player", NULL, NULL, "q1-quakeworld", "q1-quakeworld", "qw", "player", QA_GAME_FAMILY_Q1 },
    { QA_APP_DEMO_Q3, "q1-classic-id1", NULL, NULL, QA_GAME_FAMILY_Q1, "player", NULL, NULL, "q3-baseq3", "q3-baseq3", "baseq3", "sarge", QA_GAME_FAMILY_Q3 },
    { QA_APP_DEMO_QW, "q1-classic-id1", NULL, NULL, QA_GAME_FAMILY_Q1, "player", "q1-quakeworld", "fortress", "q1-quakeworld-mod-fortress", "q1-quakeworld", "fortress", "player", QA_GAME_FAMILY_Q1 },
    { QA_APP_DEMO_Q3, "q3-baseq3", NULL, NULL, QA_GAME_FAMILY_Q3, "doom", NULL, NULL, "q3-baseq3", "q3-baseq3", "baseq3", "doom", QA_GAME_FAMILY_Q3 },
    { QA_APP_DEMO_Q1, "q1-classic-hipnotic", NULL, NULL, QA_GAME_FAMILY_Q1, "player", NULL, NULL, "q1-classic-hipnotic", NULL, NULL, "player", QA_GAME_FAMILY_Q1 },
    { QA_APP_DEMO_Q1, "q2-classic-baseq2", NULL, NULL, QA_GAME_FAMILY_Q1, "player", NULL, NULL, "q1-classic-id1", NULL, NULL, "player", QA_GAME_FAMILY_Q1 },
  };
  for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
    const recorded_row *row = &rows[i];
    qa_app_options options;
    memset(&options, 0, sizeof options);
    options.product = strdup(row->product);
    options.network_kind = "q1-client"; options.network_remote = strdup("x:1");
    options.seats = 3; options.rules = "coop";
    options.movement = QA_GAME_FAMILY_Q1; options.character = row->character;
    options.character_model = strdup(row->character_model_in);
    options.movement_product = strdup("m"); options.quakec_program = strdup("p");
    options.has_bot_skill = true; options.bot_skill = 2;
    options.map = strdup("e1m1");
    if (row->prior_base != NULL) {
      options.has_remote_content = true;
      CHECK(qa_remote_content_selection_make(row->prior_base, row->prior_directory, &options.remote_content));
    }
    qa_app_demo_resource resource;
    memset(&resource, 0, sizeof resource);
    resource.kind = row->kind; resource.path = (char *)"demos/x";
    qa_app_options out;
    qa_remote_application_recorded_options(&options, &resource, &out);
    CHECK_STR(out.product, row->expected_product);
    CHECK(out.has_remote_content == (row->expected_base != NULL));
    if (row->expected_base != NULL) {
      CHECK_STR(out.remote_content.base, row->expected_base);
      CHECK_STR(out.remote_content.directory, row->expected_directory);
    }
    CHECK_STR(out.network_kind, "offline");
    CHECK(out.network_remote == NULL);
    CHECK(out.seats == 1);
    CHECK_STR(out.rules, "standard");
    CHECK(out.movement == row->expected_family && out.character == row->expected_family);
    CHECK_STR(out.character_model, row->expected_model);
    CHECK(out.movement_product == NULL && out.quakec_program == NULL && !out.has_bot_skill);
    CHECK_STR(out.map, "e1m1");
    CHECK(!out.dedicated);
    qa_app_options_destroy(&out);
    qa_app_options_destroy(&options);
  }
}

/* ---- queueCommand ---------------------------------------------------- */
static void queue_closed(void *ctx) {
  qa_remote_application_queue_command(ctx, "quit", NULL, 0, NULL, NULL);
}
static void test_queue_command(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "quake:remote:test"));
  qa_remote_application app;
  memset(&app, 0, sizeof app);
  qa_seat_id seat1 = qa_identity_owner_seat(&identity, 1), seat2 = qa_identity_owner_seat(&identity, 2);
  /* a script whose caller is a local seat: the seat comes from the unwound origin */
  qa_command_origin *caller = calloc(1, sizeof *caller);
  caller->kind = QA_COMMAND_ORIGIN_LOCAL_SEAT; caller->seat = seat1;
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.origin.kind = QA_COMMAND_ORIGIN_SCRIPT;
  context.origin.name = "autoexec.cfg";
  context.origin.caller = caller;
  const char *args[] = { "a", "b" };
  qa_remote_application_queue_command(&app, "color", args, 2, NULL, &context);
  /* an explicit seat wins over the source */
  qa_remote_application_queue_command(&app, "skins", NULL, 0, &seat2, &context);
  /* no seat and no source (no client command owner): null seat, no source */
  qa_remote_application_queue_command(&app, "allskins", NULL, 0, NULL, NULL);
  free(caller); /* the queued context owns its copy of the chain */
  CHECK(app.command_count == 3);
  CHECK_STR(app.commands[0].name, "color");
  CHECK(app.commands[0].arg_count == 2);
  CHECK_STR(app.commands[0].args[1], "b");
  CHECK(app.commands[0].has_seat && app.commands[0].seat.index == 1 && app.commands[0].seat.token == seat1.token);
  CHECK(app.commands[0].has_source);
  CHECK_STR(app.commands[0].source.origin.name, "autoexec.cfg");
  CHECK(app.commands[0].source.origin.caller != NULL && app.commands[0].source.origin.caller->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT);
  CHECK(app.commands[1].has_seat && app.commands[1].seat.index == 2);
  CHECK(!app.commands[2].has_seat && !app.commands[2].has_source);
  app.closing = true;
  qa_caught_error caught;
  CHECK(!qa_try(queue_closed, &app, &caught));
  CHECK_STR(caught.message, "Remote application is closed");
  CHECK(app.command_count == 3);
  for (size_t i = 0; i < app.command_count; i++) qa_remote_command_free(&app.commands[i]);
  free(app.commands);
  qa_identity_owner_destroy(&identity);
}

/* ---- requireQwAddress ------------------------------------------------ */
static void require_ipx(void *ctx) {
  (void)ctx;
  qa_net_address address;
  memset(&address, 0, sizeof address);
  address.kind = QA_NET_IPX;
  qa_remote_require_qw_address(address);
}
static void test_require_qw_address(void) {
  qa_net_address address;
  memset(&address, 0, sizeof address);
  address.kind = QA_NET_IPV4; address.port = 27500;
  CHECK(qa_remote_require_qw_address(address).port == 27500);
  qa_caught_error caught;
  CHECK(!qa_try(require_ipx, NULL, &caught));
  CHECK_STR(caught.message, "QuakeWorld requires UDP");
}

/* ---- prepareRemoteConfiguration -------------------------------------- */
/* bun oracle: remote-application.ts with `export { prepareRemoteConfiguration }`
 * appended (imports made absolute), called as openSource calls it on options
 * parsed from the argv below with userContentRoot set, and owned ownership. */
typedef struct cvar_row { const char *name, *value; int flags; } cvar_row;
typedef struct config_case {
  const char *name;
  const char *argv[12]; size_t argc;
  double seats;
  const char *cfg_dir, *cfg;
  const char *dialect;
  bool permission;
  const cvar_row *values; size_t value_count;
  const char *const *absent; size_t absent_count;
  const char *userinfo;
  const char *seat2_name;
  const char *prints;
} config_case;

static char print_buffer[8192];
static void host_print(void *ctx, const char *text) {
  CHECK(ctx == &sentinel);
  size_t used = strlen(print_buffer);
  snprintf(print_buffer + used, sizeof print_buffer - used, "%s", text);
}
static void image_print(void *ctx, const char *text) { host_print(ctx, text); }
static int cheats_never(void *ctx) { CHECK(ctx == &sentinel); return 0; }

static void make_directories(const char *path) {
  char buffer[1024];
  snprintf(buffer, sizeof buffer, "%s", path);
  for (char *p = buffer + 1; *p != '\0'; p++)
    if (*p == '/') { *p = '\0'; mkdir(buffer, 0700); *p = '/'; }
  mkdir(buffer, 0700);
}
static void remove_tree(const char *path) {
  char command[1200];
  snprintf(command, sizeof command, "rm -rf '%s'", path);
  if (system(command) != 0) fprintf(stderr, "cleanup failed: %s\n", path);
}

static void run_config_case(const config_case *row, const char *corpus, const char *root) {
  char users[1024];
  snprintf(users, sizeof users, "%s/%s", root, row->name);
  remove_tree(users);
  make_directories(users);
  if (row->cfg != NULL) {
    char directory[1100], file[1200];
    snprintf(directory, sizeof directory, "%s/%s", users, row->cfg_dir);
    make_directories(directory);
    snprintf(file, sizeof file, "%s/client.cfg", directory);
    FILE *out = fopen(file, "w");
    CHECK(out != NULL);
    if (out != NULL) { fputs(row->cfg, out); fclose(out); }
  }
  qa_content_root_search search = { NULL, NULL };
  qa_app_application_command command = qa_app_parse_application_command(row->argv, row->argc, search, NULL);
  CHECK_STR(command.kind, "run");
  qa_app_options options = command.options;
  free(options.corpus_root); options.corpus_root = strdup(corpus);
  free(options.user_content_root); options.user_content_root = strdup(users);
  options.seats = row->seats;
  if (options.network_kind != NULL && strcmp(options.network_kind, "q2-client") == 0) {
    /* openSource: q2Protocol, remoteContent and product from the selection */
    qa_q2_protocol_identity protocol = qa_app_live_q2_protocol(&options, false);
    qa_remote_content_selection selection;
    CHECK(qa_remote_content_selection_make(strcmp(protocol.kind, "q2-kex") == 0 || strcmp(protocol.kind, "q2-rerelease") == 0
        ? "q2-rerelease-baseq2" : "q2-classic-baseq2", "baseq2", &selection));
    options.has_q2_protocol = true; options.q2_protocol = protocol;
    char *product = qa_remote_content_product(&selection, NULL, 0);
    free(options.product); options.product = product;
    free(options.remote_content.directory);
    options.has_remote_content = true; options.remote_content = selection;
  }
  qa_app_configuration_content *content = qa_app_open_remote_application_content_native(&options);
  CHECK(content != NULL);
  qa_q3_application_product q3_product;
  if (qa_app_configuration_content_q3_value(content, &q3_product)) { options.has_q3_product = true; options.q3_product = q3_product; }
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "quake:remote:oracle"));
  qa_engine_session *session = qa_engine_session_create(&identity, (qa_session_mode){ QA_SESSION_MODE_LOCAL });
  print_buffer[0] = '\0';
  qa_app_image_settings_options image_options;
  memset(&image_options, 0, sizeof image_options);
  image_options.context.session = qa_engine_session_id(session);
  image_options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  image_options.dialect = QA_COMMAND_DIALECT_Q2_CLASSIC;
  image_options.user_content_root = users;
  image_options.has_gamma = true; image_options.gamma = options.gamma;
  image_options.print = image_print; image_options.print_ctx = &sentinel;
  qa_app_image_settings *image = qa_app_image_settings_open(image_options);
  CHECK(image != NULL);
  qa_application_host host;
  memset(&host, 0, sizeof host);
  host.ctx = &sentinel; host.print = host_print;
  qa_remote_ownership ownership = { false, NULL };
  qa_remote_arena arena = { NULL, 0, 0 };
  qa_remote_configuration *configuration = qa_remote_prepare_configuration(&options, content, session, image, ownership,
      &host, cheats_never, &sentinel, NULL, NULL, &arena);
  CHECK(configuration != NULL);
  if (configuration != NULL) {
    CHECK_STR(qa_app_configuration_dialect_name(qa_cvar_registry_dialect(configuration->source)), row->dialect);
    CHECK(configuration->seat_count == (size_t)row->seats);
    CHECK(qa_session_seat_id(configuration->seat).index == 0);
    CHECK(configuration->seat_source_count == (size_t)row->seats);
    for (size_t i = 0; i < configuration->seat_source_count; i++) CHECK(configuration->seat_sources[i].id.index == (int64_t)i);
    CHECK((configuration->download_permission != NULL) == row->permission);
    CHECK(configuration->download_permission_count == (size_t)row->seats);
    for (size_t i = 0; i < configuration->download_permission_count; i++)
      CHECK((configuration->download_permissions[i].permission != NULL) == row->permission);
    size_t selected = 0, added = 0;
    qa_app_remote_seat_identities_selected(configuration->identities, &selected);
    qa_app_remote_seat_identities_added(configuration->identities, &added);
    CHECK(!qa_app_remote_seat_identities_published(configuration->identities));
    CHECK(selected == (size_t)row->seats && added == (size_t)row->seats);
    for (size_t i = 0; i < row->value_count; i++) {
      qa_cvar_snapshot value;
      bool found = qa_cvar_registry_get(configuration->source, row->values[i].name, &value);
      CHECK(found);
      if (!found) { fprintf(stderr, "  %s: missing %s\n", row->name, row->values[i].name); continue; }
      CHECK_STR(value.value, row->values[i].value);
      if (value.flags != row->values[i].flags) fprintf(stderr, "  %s: %s flags %d != %d\n", row->name, row->values[i].name, value.flags, row->values[i].flags);
      CHECK(value.flags == row->values[i].flags);
      qa_cvar_read_free(&value);
    }
    for (size_t i = 0; i < row->absent_count; i++) {
      qa_cvar_snapshot value;
      bool found = qa_cvar_registry_get(configuration->source, row->absent[i], &value);
      CHECK(!found);
      if (found) qa_cvar_read_free(&value);
    }
    char *userinfo = qa_cvar_registry_info_string(configuration->source, QA_CVAR_FLAG_USER_INFO, -1);
    CHECK_STR(userinfo, row->userinfo);
    free(userinfo);
    if (row->seat2_name != NULL) {
      qa_cvar_registry *second = qa_remote_configuration_seat_source(configuration, configuration->seat_sources[1].id);
      char *name = second != NULL ? qa_cvar_registry_variable_string(second, "name") : NULL;
      CHECK_STR(name, row->seat2_name);
      free(name);
    }
    CHECK_STR(print_buffer, row->prints);
    qa_remote_configuration_validate_source(configuration);
    qa_remote_configuration_publish_source(configuration);
    CHECK(qa_app_remote_seat_identities_published(configuration->identities));
  }
  qa_remote_arena_release(&arena);
  qa_app_image_settings_destroy(image);
  qa_app_configuration_content_destroy(content);
  qa_engine_session_destroy(session);
  qa_identity_owner_destroy(&identity);
  qa_app_options_destroy(&options);
  command.options = (qa_app_options){0};
  qa_app_application_command_destroy(&command);
  remove_tree(users);
}

static const cvar_row q2_values[] = {
  { "name", "Player", 3 }, { "skin", "male/grunt", 3 }, { "rate", "24900", 3 }, { "msg", "1", 3 }, { "hand", "0", 3 },
  { "fov", "90", 3 }, { "gender", "male", 3 }, { "password", "", 2 }, { "spectator", "0", 2 },
  { "allow_download", "0", 1 }, { "cl_http_downloads", "1", 1 },
};
static const char *const q2_absent[] = { "cl_maxfps" };
static const cvar_row q1_values[] = { { "name", "Player", 1 }, { "color", "0", 1 } };
static const char *const q1_absent[] = { "password", "cl_maxfps", "host_maxfps" };
static const cvar_row qw_values[] = {
  { "name", "unnamed", 3 }, { "team", "", 3 }, { "skin", "", 3 }, { "topcolor", "0", 3 }, { "bottomcolor", "0", 3 },
  { "noaim", "0", 3 }, { "msg", "1", 3 }, { "rate", "25000", 3 }, { "noskins", "0", 1 }, { "baseskin", "base", 1 },
  { "cl_hightrack", "0", 0 }, { "cl_chasecam", "0", 0 }, { "password", "", 2 },
};
static const cvar_row q3_values[] = {
  { "name", "Player", 3 }, { "model", "sarge/default", 3 }, { "headmodel", "sarge/default", 3 }, { "rate", "25000", 3 },
  { "snaps", "20", 3 }, { "cl_allowDownload", "0", 1 }, { "password", "", 2 },
};
static const char *const q3_absent[] = { "com_maxfps" };

static const char q1_prints[] = "execing quake.rc\nexecing default.cfg\nexecing config.cfg\nUnknown command \"joystick\"\n"
  "Unknown command \"cl_backspeed\"\nUnknown command \"cl_forwardspeed\"\nUnknown command \"_cl_color\"\nUnknown command \"_cl_name\"\n"
  "Unknown command \"_snd_mixahead\"\nUnknown command \"_windowed_mouse\"\nUnknown command \"vid_stretch_by_2\"\n"
  "Unknown command \"vid_config_y\"\nUnknown command \"vid_config_x\"\nUnknown command \"_vid_default_mode_win\"\n"
  "Unknown command \"_vid_default_mode\"\nUnknown command \"_vid_wait_override\"\nUnknown command \"vid_nopageflip\"\n"
  "Unknown command \"_config_modem_hangup\"\nUnknown command \"_config_modem_init\"\nUnknown command \"_config_modem_clear\"\n"
  "Unknown command \"_config_modem_dialtype\"\nUnknown command \"_config_com_modem\"\nUnknown command \"_config_com_baud\"\n"
  "Unknown command \"_config_com_irq\"\nUnknown command \"_config_com_port\"\nUnknown command \"saved4\"\nUnknown command \"saved3\"\n"
  "Unknown command \"saved2\"\nUnknown command \"saved1\"\nUnknown command \"savedgamecfg\"\ncouldn't exec autoexec.cfg\n";

static void test_prepare_configuration(const char *corpus) {
  const char *tmp = getenv("TMPDIR");
  char template[1024];
  snprintf(template, sizeof template, "%s/qa-remote-config-XXXXXX", tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp");
  char *made = mkdtemp(template);
  CHECK(made != NULL);
  if (made == NULL) return;
  const config_case cases[] = {
    { "q2", { "--game", "q2-classic-baseq2", "--movement", "q2", "--character", "q2", "--mode", "coop", "--connect-q2", "127.0.0.1:27910", "--hidden" }, 11, 1,
      "q2/baseq2/settings", "set allow_download 0\nset cl_http_downloads 1\n", "q2-classic", true,
      q2_values, sizeof q2_values / sizeof q2_values[0], q2_absent, 1,
      "\\spectator\\0\\gender\\male\\fov\\90\\hand\\0\\msg\\1\\rate\\24900\\skin\\male/grunt\\name\\Player", NULL,
      "execing default.cfg\nexecing config.cfg\ncouldn't exec autoexec.cfg\n" },
    { "q1", { "--game", "q1-classic-id1", "--movement", "q1", "--character", "q1", "--connect-q1", "127.0.0.1:26000", "--hidden" }, 9, 1,
      NULL, NULL, "q1-netquake", false, q1_values, 2, q1_absent, 3, "", NULL, q1_prints },
    { "qw", { "--game", "q1-classic-id1", "--movement", "q1", "--character", "q1", "--connect-qw", "127.0.0.1:27500", "--hidden" }, 9, 2,
      NULL, NULL, "q1-quakeworld", false, qw_values, sizeof qw_values / sizeof qw_values[0], NULL, 0,
      "\\msg\\1\\noaim\\0\\bottomcolor\\0\\topcolor\\0\\name\\unnamed\\rate\\25000", "unnamed",
      "execing quake.rc\nexecing default.cfg\nexecing config.cfg\ncouldn't exec autoexec.cfg\n" },
    { "q3", { "--game", "q3-baseq3", "--movement", "q3", "--character", "q3", "--connect-q3", "127.0.0.1:27960", "--hidden" }, 9, 1,
      NULL, NULL, "q3", true, q3_values, sizeof q3_values / sizeof q3_values[0], q3_absent, 1,
      "\\rate\\25000\\snaps\\20\\name\\Player\\model\\sarge/default\\headmodel\\sarge/default\\team_model\\sarge/default"
      "\\team_headmodel\\sarge/default\\color1\\4\\color2\\5\\sex\\male\\cl_anonymous\\0\\cg_predictItems\\1\\teamtask\\0\\handicap\\100", NULL,
      "execing default.cfg\nexecing q3config.cfg\ncouldn't exec autoexec.cfg\n" },
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) run_config_case(&cases[i], corpus, made);
  remove_tree(made);
}

int main(void) {
  test_allocate_qport();
  test_recorded_options();
  test_queue_command();
  test_require_qw_address();
  const char *corpus = getenv("QTS_TEST_CORPUS");
  if (corpus != NULL && corpus[0] != '\0') test_prepare_configuration(corpus);
  else printf("skipped content rows: QTS_TEST_CORPUS unset\n");
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
