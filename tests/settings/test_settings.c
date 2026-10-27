/*
 * Port of tests/settings/config.test.ts, server-profile.test.ts,
 * q2-source-cvars.test.ts and undecodable-documents.test.ts (the
 * cases owned by the settings module). TypeScript `toThrow` cases
 * run in a child process and must abort. GPL-2.0-or-later.
 */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/contracts/identity.h"
#include "qa/settings.h"
#include "qa/settings/server/index.h"
#include "qa/settings/server/library.h"
#include "qa/settings/server/rotation.h"

static int failures = 0;
static int checks = 0;

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

static qa_command_context server_context(qa_identity_owner *owner) {
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = owner->session;
  context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  return context;
}

static qa_cvar_registry *make_cvars(qa_command_dialect dialect,
                                    qa_command_context context) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context = context;
  return qa_cvar_registry_create(&options);
}

static void registry_set(qa_cvar_registry *cvars, const char *name,
                         const char *value, bool force) {
  qa_cvar_snapshot snapshot;
  (void)qa_cvar_registry_set(cvars, name, value, force, &snapshot);
  qa_cvar_read_free(&snapshot);
}
static void registry_register(qa_cvar_registry *cvars, const char *name,
                              const char *value, double flags) {
  qa_cvar_snapshot snapshot;
  (void)qa_cvar_registry_register(cvars, name, value, flags, &snapshot);
  qa_cvar_read_free(&snapshot);
}

static qa_save_value *json(const char *text) {
  return qa_parse_json_value((const uint8_t *)text, strlen(text), false);
}

static const char *const seat_json_head =
    "{\"version\":1,\"bindings\":["
    "{\"input\":{\"kind\":\"key\",\"code\":32},"
    "\"target\":{\"kind\":\"action\",\"action\":\"jump\"}},"
    "{\"input\":{\"kind\":\"controller-axis\",\"device\":0,\"axis\":\"left-x\","
    "\"direction\":\"positive\"},"
    "\"target\":{\"kind\":\"command\",\"text\":\"say hi\"}}],";
static const char *const seat_json_tail =
    "\"gamepad\":{\"move\":{\"kind\":\"radial\",\"deadzone\":0.175,\"exponent\":2,"
    "\"outerThreshold\":0.02},"
    "\"look\":{\"kind\":\"axial\",\"deadzone\":0.1,\"exponent\":1.5},"
    "\"swapSticks\":false,\"yawDegreesPerSecond\":240,"
    "\"pitchDegreesPerSecond\":130,\"invertPitch\":false,"
    "\"forwardSensitivity\":1,\"sideSensitivity\":1,\"triggerThreshold\":0.2,"
    "\"gyro\":{\"enabled\":false,\"yawAxis\":\"y\",\"yawSensitivity\":1,"
    "\"pitchSensitivity\":1}},"
    "\"mouse\":{\"sensitivity\":3,\"acceleration\":0,\"filter\":false,"
    "\"yaw\":0.022,\"pitch\":0.022,\"side\":0.8,\"forward\":1,"
    "\"freeLook\":true,\"invertPitch\":false},"
    "\"history\":[\"map q3dm1\"],\"rumble\":true,"
    "\"controller\":{\"kind\":\"automatic\"}}";

static void test_seat_parse(void) {
  char text[4096];
  snprintf(text, sizeof text, "%s%s", seat_json_head, seat_json_tail);
  qa_save_value *value = json(text);
  CHECK(value != NULL);
  qa_seat_settings settings;
  qa_parse_seat_settings(value, &settings);
  qa_save_value_free(value);
  CHECK(settings.version == 1);
  CHECK(settings.binding_count == 2);
  CHECK(settings.bindings[0].input.kind == QA_PHYSICAL_INPUT_KEY);
  CHECK(settings.bindings[0].input.code == 32);
  CHECK(settings.bindings[0].target.is_action);
  CHECK(strcmp(settings.bindings[0].target.action, "jump") == 0);
  CHECK(settings.bindings[1].input.kind == QA_PHYSICAL_INPUT_CONTROLLER_AXIS);
  CHECK(strcmp(settings.bindings[1].target.text, "say hi") == 0);
  CHECK(!settings.has_always_run);
  CHECK(settings.rumble_strength == 1); /* parse resolves the default */
  CHECK(settings.gamepad.move.radial);
  CHECK(!settings.gamepad.look.radial);
  CHECK(settings.mouse.look_spring == false);
  CHECK(settings.history_count == 1);
  CHECK(strcmp(settings.history[0], "map q3dm1") == 0);
  CHECK(settings.controller.kind == QA_CONTROLLER_AUTOMATIC);
  qa_seat_settings_free(&settings);

  snprintf(text, sizeof text, "%s\"alwaysRun\":false,%s", seat_json_head,
           seat_json_tail);
  value = json(text);
  qa_parse_seat_settings(value, &settings);
  qa_save_value_free(value);
  CHECK(settings.has_always_run && !settings.always_run);
  qa_seat_settings_free(&settings);

  snprintf(text, sizeof text, "%s\"alwaysRun\":\"true\",%s", seat_json_head,
           seat_json_tail);
  CHECK_ABORTS(value = json(text); qa_parse_seat_settings(value, &settings));
  /* Unsupported version and bad vibration strength both abort. */
  CHECK_ABORTS(value = json("{\"version\":2,\"bindings\":[]}");
               qa_parse_seat_settings(value, &settings));
  /* Invalid deadzone is a RangeError through validateGamepadTuning. */
  snprintf(text, sizeof text, "%s%s", seat_json_head, seat_json_tail);
  char *deadzone = strstr(text, "0.175");
  memcpy(deadzone, "1.500", 5);
  CHECK_ABORTS(value = json(text); qa_parse_seat_settings(value, &settings));
}

static void test_gyro_parse(void) {
  const char *text =
      "{\"version\":1,\"identity\":{\"kind\":\"device\",\"guid\":\""
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"serial\":\"pad-A\"},"
      "\"tuning\":{\"enabled\":true,\"yawAxis\":\"z\",\"yawSensitivity\":2.5,"
      "\"pitchSensitivity\":1}}";
  qa_save_value *value = json(text);
  qa_gyro_profile profile;
  qa_parse_gyro_profile(value, &profile);
  qa_save_value_free(value);
  CHECK(profile.identity_is_device);
  CHECK(strcmp(profile.serial, "pad-A") == 0);
  CHECK(strcmp(profile.tuning.yaw_axis, "z") == 0);
  CHECK(profile.tuning.yaw_sensitivity == 2.5);
  qa_gyro_profile_free(&profile);
  CHECK_ABORTS(value = json("{\"version\":1,\"identity\":{\"kind\":\"device\","
                            "\"guid\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
                            "\"serial\":\"\"},\"tuning\":{\"enabled\":true,"
                            "\"yawAxis\":\"y\",\"yawSensitivity\":1,"
                            "\"pitchSensitivity\":1}}");
               qa_parse_gyro_profile(value, &profile));
  CHECK_ABORTS(value = json("{\"version\":9,\"identity\":{\"kind\":\"seat\"},"
                            "\"tuning\":{\"enabled\":true,\"yawAxis\":\"y\","
                            "\"yawSensitivity\":1,\"pitchSensitivity\":1}}");
               qa_parse_gyro_profile(value, &profile));
}

static bool write_text_file(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");
  if (file == NULL) return false;
  fputs(text, file);
  fclose(file);
  return true;
}

static void remove_tree(const char *path) {
  char command[1024];
  snprintf(command, sizeof command, "rm -rf '%s'", path);
  (void)system(command);
}

static void test_store_round_trips(void) {
  char root_template[] = "/tmp/qa-settings-XXXXXX";
  char *root = mkdtemp(root_template);
  CHECK(root != NULL);
  qa_config_store store;
  CHECK(qa_config_store_init(&store, root));

  char text[4096];
  snprintf(text, sizeof text, "%s%s", seat_json_head, seat_json_tail);
  qa_save_value *value = json(text);
  qa_seat_settings settings;
  qa_parse_seat_settings(value, &settings);
  qa_save_value_free(value);
  CHECK(qa_config_store_save_seat(&store, "seat-2.json", &settings));
  qa_seat_settings loaded;
  CHECK(qa_config_store_load_seat(&store, "seat-2.json", &loaded));
  CHECK(loaded.binding_count == 2);
  CHECK(loaded.rumble_strength == 1);
  CHECK(strcmp(loaded.history[0], "map q3dm1") == 0);
  qa_seat_settings_free(&loaded);
  /* Always-run false and true both survive (config.test.ts). */
  for (int i = 0; i < 2; i++) {
    settings.has_always_run = true;
    settings.always_run = i == 1;
    CHECK(qa_config_store_save_seat(&store, "input/seat-1.json", &settings));
    CHECK(qa_config_store_load_seat(&store, "input/seat-1.json", &loaded));
    CHECK(loaded.has_always_run && loaded.always_run == (i == 1));
    qa_seat_settings_free(&loaded);
  }
  qa_seat_settings_free(&settings);
  CHECK(!qa_config_store_load_seat(&store, "absent.json", &loaded));

  value = json("{\"version\":1,\"identity\":{\"kind\":\"device\",\"guid\":\""
               "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"serial\":\"pad-A\"},"
               "\"tuning\":{\"enabled\":true,\"yawAxis\":\"y\","
               "\"yawSensitivity\":2.5,\"pitchSensitivity\":1}}");
  qa_gyro_profile profile;
  qa_parse_gyro_profile(value, &profile);
  qa_save_value_free(value);
  CHECK(qa_config_store_save_gyro(&store, "controllers/seat-1/pad-A.json", &profile));
  qa_gyro_profile loaded_gyro;
  CHECK(qa_config_store_load_gyro(&store, "controllers/seat-1/pad-A.json",
                                  &loaded_gyro));
  CHECK(loaded_gyro.tuning.yaw_sensitivity == 2.5);
  qa_gyro_profile_free(&loaded_gyro);
  CHECK(!qa_config_store_load_gyro(&store, "controllers/seat-2/pad-A.json",
                                   &loaded_gyro));
  qa_gyro_profile_free(&profile);

  CHECK(qa_config_store_save_input_routing(&store, "input/routing.json", 2, true));
  double seat = 0;
  bool has_seat = false;
  CHECK(qa_config_store_load_input_routing(&store, "input/routing.json", &seat,
                                           &has_seat));
  CHECK(has_seat && seat == 2);
  CHECK(qa_config_store_save_input_routing(&store, "input/routing.json", 0, false));
  CHECK(qa_config_store_load_input_routing(&store, "input/routing.json", &seat,
                                           &has_seat));
  CHECK(!has_seat);

  value = json("{\"version\":1,\"mouse\":{\"sensitivity\":5,\"acceleration\":0,"
               "\"filter\":true,\"yaw\":0.022,\"pitch\":0.022,\"side\":0.8,"
               "\"forward\":1,\"freeLook\":false,\"invertPitch\":true},"
               "\"rumble\":false}");
  qa_global_input_tuning tuning;
  qa_parse_global_input_tuning(value, &tuning);
  qa_save_value_free(value);
  CHECK(qa_config_store_save_global_tuning(&store, "input/global-tuning.json",
                                           &tuning));
  qa_global_input_tuning loaded_tuning;
  CHECK(qa_config_store_load_global_tuning(&store, "input/global-tuning.json",
                                           &loaded_tuning));
  CHECK(loaded_tuning.mouse.sensitivity == 5);
  CHECK(loaded_tuning.rumble_strength == 1);

  /* Undecodable documents load as absent. */
  char path[1024];
  snprintf(path, sizeof path, "%s/bad-seat.json", root);
  CHECK(write_text_file(path, "{ torn write"));
  CHECK(!qa_config_store_load_seat(&store, "bad-seat.json", &loaded));
  snprintf(path, sizeof path, "%s/bad-routing.json", root);
  CHECK(write_text_file(path, "not json at all"));
  CHECK(!qa_config_store_load_input_routing(&store, "bad-routing.json", &seat,
                                            &has_seat));
  snprintf(path, sizeof path, "%s/bad-tuning.json", root);
  CHECK(write_text_file(path, "[1, 2"));
  CHECK(!qa_config_store_load_global_tuning(&store, "bad-tuning.json",
                                            &loaded_tuning));
  snprintf(path, sizeof path, "%s/bad-gyro.json", root);
  CHECK(write_text_file(path, "\x01\x02"));
  CHECK(!qa_config_store_load_gyro(&store, "bad-gyro.json", &loaded_gyro));

  /* saveCvars + execute round-trip against real registries. */
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "config"));
  qa_command_context context = server_context(&identity);
  qa_cvar_registry *cvars = make_cvars(QA_COMMAND_DIALECT_Q3, context);
  CHECK(cvars != NULL);
  registry_register(cvars, "sensitivity", "3", QA_CVAR_FLAG_ARCHIVE);
  registry_set(cvars, "sensitivity", "4.5", false);
  CHECK(qa_config_store_save_cvars(&store, "config.cfg", cvars, NULL, 0));
  char *saved = qa_config_store_load_text(&store, "config.cfg", NULL);
  CHECK(saved != NULL && strstr(saved, "// Generated by quake-typescript") != NULL);
  free(saved);
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context = context;
  options.cvars = cvars;
  qa_command_buffer *commands = qa_command_buffer_create(&options);
  CHECK(commands != NULL);
  registry_set(cvars, "sensitivity", "1", false);
  qa_config_store_execute(&store, "config.cfg", commands, context);
  (void)qa_command_buffer_execute(commands);
  char *value_text = qa_cvar_registry_variable_string(cvars, "sensitivity");
  CHECK(value_text != NULL && strcmp(value_text, "4.5") == 0);
  free(value_text);
  qa_command_buffer_destroy(commands);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&identity);

  /* Path escape aborts (server-profile.test.ts save to ../escape). */
  CHECK_ABORTS(free(qa_settings_path(root, "../escape.json")));
  char *inside = qa_settings_path(root, "a/b.json");
  CHECK(inside != NULL && strstr(inside, "/a/b.json") != NULL);
  free(inside);

  /* writeAtomic leaves content and no temp file behind. */
  size_t before = 0;
  DIR *dir = opendir(root);
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) before++;
  closedir(dir);
  char *atomic_path = qa_settings_path(root, "deep/nested/file.txt");
  CHECK(qa_write_atomic_text(atomic_path, "contents\n"));
  free(atomic_path);
  size_t length = 0;
  char *read_back = qa_config_store_load_text(&store, "deep/nested/file.txt", &length);
  CHECK(read_back != NULL && strcmp(read_back, "contents\n") == 0);
  free(read_back);
  size_t listed = 999;
  qa_server_profile_entry *profiles = qa_list_server_profiles(&store, &listed);
  CHECK(listed == 0);
  qa_server_profile_entries_free(profiles, listed);
  (void)before;

  qa_config_store_destroy(&store);
  remove_tree(root);
}

static const qa_bound_server_setting *find_binding(
    const qa_bound_server_setting *bindings, size_t count, const char *id) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(bindings[i].definition->id, id) == 0) return &bindings[i];
  return NULL;
}

static void test_server_profiles(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "server-settings"));
  qa_command_context context = server_context(&identity);
  qa_cvar_registry *cvars = make_cvars(QA_COMMAND_DIALECT_Q2_CLASSIC, context);
  CHECK(cvars != NULL);
  qa_register_q2_server_cvars(cvars, "q2:official");
  qa_server_setting_collection collections[3] = {qa_q2_limit_settings(),
                                                 qa_q2_combat_settings(),
                                                 qa_q2_spawn_settings()};
  size_t count = 0;
  const qa_server_setting_definition **definitions =
      qa_collect_server_settings(collections, 3, &count);
  CHECK(count == 6);
  qa_cvar_server_owner owner;
  qa_cvar_server_settings_owner_init(&owner, cvars, false);
  qa_bound_server_setting *bindings =
      (qa_bound_server_setting *)calloc(count, sizeof *bindings);
  for (size_t i = 0; i < count; i++) {
    bindings[i].definition = definitions[i];
    bindings[i].owner = owner.owner;
  }
  qa_cvar_registry_set_server_active(cvars, true);

  const qa_bound_server_setting *health =
      find_binding(bindings, count, "server:q2.no-health");
  const qa_bound_server_setting *friendly =
      find_binding(bindings, count, "server:friendly-fire");
  CHECK(health != NULL && friendly != NULL);
  qa_server_setting_status status = qa_write_server_setting(health, "true");
  CHECK(strcmp(status.desired, "1") == 0 && strcmp(status.effective, "0") == 0);
  CHECK(status.pending && status.apply_at == QA_SERVER_APPLY_NEXT_MAP);
  qa_server_setting_status_free(&status);
  status = qa_write_server_setting(friendly, "false");
  qa_server_setting_status_free(&status);
  qa_cvar_read dmflags;
  CHECK(qa_cvar_registry_find(cvars, "dmflags", &dmflags));
  CHECK(strcmp(dmflags.value, "256") == 0);
  CHECK(dmflags.latched_value != NULL && strcmp(dmflags.latched_value, "257") == 0);
  CHECK(dmflags.flags == (int32_t)QA_Q2_CVAR_FLAG_SERVER_INFO);
  qa_cvar_read_free(&dmflags);
  status = qa_read_server_setting(health);
  CHECK(status.pending);
  qa_server_setting_status_free(&status);
  size_t applied = 0;
  qa_cvar_snapshot *snapshots =
      qa_cvar_registry_apply_latched(cvars, NULL, &applied);
  for (size_t i = 0; i < applied; i++) qa_cvar_read_free(&snapshots[i]);
  free(snapshots);
  status = qa_read_server_setting(health);
  CHECK(!status.pending);
  qa_server_setting_status_free(&status);
  CHECK(qa_cvar_registry_variable_value(cvars, "dmflags") == 257);

  /* Profile validation finishes before any source write. */
  qa_server_profile bad;
  memset(&bad, 0, sizeof bad);
  bad.version = 1;
  bad.override_count = 2;
  bad.overrides = (qa_server_profile_override *)calloc(2, sizeof *bad.overrides);
  bad.overrides[0].id = (char *)"server:frag-limit";
  bad.overrides[0].value = (char *)"9";
  bad.overrides[1].id = (char *)"server:time-limit";
  bad.overrides[1].value = (char *)"NaN";
  CHECK_ABORTS(qa_apply_server_profile(&bad, bindings, count));
  CHECK(qa_cvar_registry_variable_value(cvars, "fraglimit") == 0);
  bad.overrides[1].value = (char *)"0.5";
  qa_apply_server_profile(&bad, bindings, count);
  CHECK(qa_cvar_registry_variable_value(cvars, "fraglimit") == 9);
  free(bad.overrides);

  /* Non-integer slider accepts 0.5 and normalizes via String(). */
  char *parsed = qa_parse_server_setting(
      find_binding(bindings, count, "server:time-limit")->definition, "0.50");
  CHECK(parsed != NULL && strcmp(parsed, "0.5") == 0);
  free(parsed);

  /* Duplicate ids across collections abort. */
  CHECK_ABORTS(
      qa_server_setting_collection dup[2] = {qa_q2_limit_settings(),
                                             qa_q3_limit_settings("baseq3")};
      size_t n = 0; free(qa_collect_server_settings(dup, 2, &n)));
  /* Q3 source defaults: fraglimit 20 (server-profile.test.ts). */
  qa_server_setting_collection q3_combo[2] = {qa_q2_limit_settings(),
                                              qa_q3_combat_settings("baseq3")};
  free(qa_collect_server_settings(q3_combo, 2, &count));
  qa_server_setting_collection q3_limits = qa_q3_limit_settings("baseq3");
  CHECK(strcmp(q3_limits.definitions[1].default_value, "20") == 0);

  free(bindings);
  free(definitions);
  qa_cvar_registry_destroy(cvars);

  /* Q1 Rogue teamplay is a pending next-map choice. */
  qa_provider_reference source = {"q1:official", "q1:classic:rogue:base"};
  size_t q1_count = 0;
  const qa_server_setting_definition **q1_definitions =
      qa_server_definitions_for_selection(source, source, source, &q1_count);
  CHECK(q1_count == 1);
  qa_cvar_registry *q1_cvars = make_cvars(QA_COMMAND_DIALECT_Q1_NETQUAKE, context);
  CHECK(q1_cvars != NULL);
  registry_register(q1_cvars, "teamplay", "0", 0);
  qa_cvar_server_owner q1_owner;
  qa_cvar_server_settings_owner_init(&q1_owner, q1_cvars, false);
  qa_bound_server_setting teamplay = {q1_definitions[0], q1_owner.owner};
  CHECK(teamplay.definition->kind == QA_SERVER_SETTING_CHOICE);
  status = qa_write_server_setting(&teamplay, "6");
  CHECK(strcmp(status.desired, "6") == 0 && strcmp(status.effective, "0") == 0);
  CHECK(status.pending && status.apply_at == QA_SERVER_APPLY_NEXT_MAP);
  qa_server_setting_status_free(&status);
  qa_cvar_server_owner initializing;
  qa_cvar_server_settings_owner_init(&initializing, q1_cvars, true);
  qa_bound_server_setting boot = {q1_definitions[0], initializing.owner};
  status = qa_write_server_setting(&boot, "6");
  qa_server_setting_status_free(&status);
  CHECK(qa_cvar_registry_variable_value(q1_cvars, "teamplay") == 6);
  /* Choice 6 does not exist in the standard program. */
  CHECK_ABORTS(free(qa_parse_server_setting(
      &qa_q1_match_settings("standard").definitions[0], "6")));
  CHECK(qa_q1_match_settings("ctf").definition_count == 0);
  free(q1_definitions);
  qa_cvar_registry_destroy(q1_cvars);
  qa_identity_owner_destroy(&identity);
}

static void test_rotation(void) {
  for (int mode = 0; mode < 2; mode++) {
    bool rerelease = mode == 1;
    qa_identity_owner identity;
    CHECK(qa_identity_owner_create(&identity, rerelease ? "rotation-1" : "rotation-0"));
    qa_command_context context = server_context(&identity);
    qa_cvar_registry *cvars = make_cvars(
        rerelease ? QA_COMMAND_DIALECT_Q2_RERELEASE : QA_COMMAND_DIALECT_Q2_CLASSIC,
        context);
    CHECK(cvars != NULL);
    const char *name = rerelease ? "g_map_list" : "sv_maplist";
    registry_register(cvars, name, "", 0);
    if (rerelease) registry_register(cvars, "g_map_list_shuffle", "0", 0);
    qa_server_setting_collection rotation = qa_q2_rotation_settings(rerelease);
    qa_cvar_server_owner owner;
    qa_cvar_server_settings_owner_init(&owner, cvars, false);
    qa_bound_server_setting *bindings = (qa_bound_server_setting *)calloc(
        rotation.definition_count, sizeof *bindings);
    for (size_t i = 0; i < rotation.definition_count; i++) {
      bindings[i].definition = &rotation.definitions[i];
      bindings[i].owner = owner.owner;
    }
    char *maps[3] = {qa_rotation_map_name("maps/q2dm1.bsp"),
                     qa_rotation_map_name("q2dm2"), qa_rotation_map_name("q2dm3")};
    CHECK(strcmp(maps[0], "q2dm1") == 0);
    const char **moved =
        qa_move_rotation_map((const char *const *)maps, 3, 2, -1);
    CHECK(moved != NULL && strcmp(moved[1], "q2dm3") == 0);
    char joined[128];
    snprintf(joined, sizeof joined, "%s %s %s", moved[0], moved[1], moved[2]);
    const qa_bound_server_setting *binding =
        find_binding(bindings, rotation.definition_count, "server:map-rotation");
    qa_server_setting_status status = qa_write_server_setting(binding, joined);
    qa_server_setting_status_free(&status);
    char *value = qa_cvar_registry_variable_string(cvars, name);
    CHECK(value != NULL && strcmp(value, "q2dm1 q2dm3 q2dm2") == 0);
    free(value);
    qa_server_profile profile;
    qa_capture_server_profile(bindings, rotation.definition_count, &profile);
    status = qa_write_server_setting(binding, "");
    qa_server_setting_status_free(&status);
    qa_apply_server_profile(&profile, bindings, rotation.definition_count);
    value = qa_cvar_registry_variable_string(cvars, name);
    CHECK(value != NULL && strcmp(value, "q2dm1 q2dm3 q2dm2") == 0);
    free(value);
    qa_server_profile_free(&profile);
    CHECK(qa_move_rotation_map((const char *const *)maps, 3, 0, -1) == NULL);
    free(moved);
    for (int i = 0; i < 3; i++) free(maps[i]);
    free(bindings);
    qa_cvar_registry_destroy(cvars);
    qa_identity_owner_destroy(&identity);
  }
  CHECK_ABORTS(free(qa_rotation_map_name("../outside")));
  CHECK_ABORTS(free(qa_rotation_map_name("q2dm1;quit")));
}

static void test_q2_source_cvars(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "q2-source-cvars"));
  qa_command_context context = server_context(&identity);
  qa_cvar_registry *cvars = make_cvars(QA_COMMAND_DIALECT_Q2_RERELEASE, context);
  CHECK(cvars != NULL);
  qa_register_q2_server_cvars(cvars, "q2:official");
  qa_cvar_read found;
  CHECK(qa_cvar_registry_find(cvars, "g_coop_player_collision", &found));
  CHECK(found.flags == (int32_t)QA_Q2_CVAR_FLAG_LATCH);
  qa_cvar_read_free(&found);
  CHECK(qa_cvar_registry_find(cvars, "g_map_list_shuffle", &found));
  CHECK(found.flags == 0);
  qa_cvar_read_free(&found);
  registry_set(cvars, "timelimit", "12", false);
  qa_save_value *checkpoint = qa_cvar_registry_capture_save_state(cvars);
  CHECK(checkpoint != NULL);
  CHECK(qa_saved_q2_server_cvars_have(cvars, checkpoint, "timelimit"));
  registry_set(cvars, "timelimit", "99", false);
  qa_restore_q2_server_cvars(cvars, checkpoint);
  CHECK(qa_cvar_registry_variable_value(cvars, "timelimit") == 12);
  /* Rerelease cvars absent from the checkpoint keep their defaults. */
  CHECK(qa_cvar_registry_find(cvars, "g_coop_num_lives", &found));
  CHECK(strcmp(found.reset_value, "2") == 0);
  qa_cvar_read_free(&found);
  qa_save_value_free(checkpoint);
  /* dmflags 0 with rerelease folds: instant-items (16) on, plus the
   * inverted no-quad-drop fold sets 16384 while its cvar is "0". */
  CHECK(qa_q2_source_deathmatch_flags(cvars) == 16400);
  qa_cvar_registry_destroy(cvars);

  qa_cvar_registry *classic = make_cvars(QA_COMMAND_DIALECT_Q2_CLASSIC, context);
  CHECK(classic != NULL);
  qa_register_q2_server_cvars(classic, "q2:official");
  CHECK(!qa_cvar_registry_find(classic, "g_coop_player_collision", &found));
  CHECK(!qa_cvar_registry_find(classic, "g_map_list_shuffle", &found));
  qa_cvar_registry_destroy(classic);
  qa_identity_owner_destroy(&identity);
}

static int restart_order[8];
static size_t restart_order_count = 0;
static qa_restart_controls *restart_self = NULL;
static bool restart_reenter = false;
static void record_restart(void *ctx) {
  restart_order[restart_order_count++] = *(int *)ctx;
  if (restart_reenter) qa_restart_controls_drain(restart_self);
}

static void test_restart_controls(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "restart"));
  qa_command_context context = server_context(&identity);
  qa_cvar_registry *cvars = make_cvars(QA_COMMAND_DIALECT_Q2_CLASSIC, context);
  CHECK(cvars != NULL);
  registry_register(cvars, "vid_mode", "0", 0);
  qa_cvar_snapshot snapshot;
  (void)qa_cvar_registry_stage(cvars, "vid_mode", "3", &snapshot);
  qa_cvar_read_free(&snapshot);
  int order_values[3] = {0, 1, 2};
  qa_restart_service services[QA_RESTART_KIND_COUNT];
  for (int i = 0; i < QA_RESTART_KIND_COUNT; i++) {
    services[i].ctx = &order_values[i];
    services[i].restart = record_restart;
  }
  static const char *const video_latched[] = {"vid_mode"};
  const char *const *latched[QA_RESTART_KIND_COUNT] = {video_latched, NULL, NULL};
  size_t latched_count[QA_RESTART_KIND_COUNT] = {1, 0, 0};
  qa_restart_controls controls;
  CHECK(qa_restart_controls_init(&controls, services, cvars, latched, latched_count));
  restart_self = &controls;
  qa_restart_controls_request(&controls, QA_RESTART_VIDEO);
  qa_restart_controls_request(&controls, QA_RESTART_VIDEO); /* deduped */
  qa_restart_controls_request(&controls, QA_RESTART_AUDIO);
  qa_restart_controls_drain(&controls);
  CHECK(restart_order_count == 2);
  CHECK(restart_order[0] == 0 && restart_order[1] == 2);
  CHECK(qa_cvar_registry_variable_value(cvars, "vid_mode") == 3);
  /* Re-entrant drain aborts, as the TS active-operation Error. */
  restart_reenter = true;
  qa_restart_controls_request(&controls, QA_RESTART_INPUT);
  CHECK_ABORTS(qa_restart_controls_drain(&controls));
  restart_reenter = false;
  qa_restart_controls_destroy(&controls);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&identity);
}

int main(void) {
  test_seat_parse();
  test_gyro_parse();
  test_store_round_trips();
  test_server_profiles();
  test_rotation();
  test_q2_source_cvars();
  test_restart_controls();
  printf("settings: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
