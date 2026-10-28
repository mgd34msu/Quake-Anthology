/*
 * Port of key cases from tests/bots/behavior (library + q3 AI helpers).
 * TypeScript `toThrow(RangeError)` cases run in a child process and must
 * abort. GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/bots/behavior.h"

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

static qa_vec3 v3(double x, double y, double z) { return (qa_vec3){x, y, z}; }

static void test_orders(void) {
  CHECK(qa_bot_order_status(NULL) == 0);
  qa_bot_order_state ok = {{QA_BOT_ORDER_POINT, {0, 0, 0}, {0, 0}}, QA_BOT_ORDER_SUCCESS};
  qa_bot_order_state prog = {{QA_BOT_ORDER_POINT, {0, 0, 0}, {0, 0}}, QA_BOT_ORDER_IN_PROGRESS};
  qa_bot_order_state err = {{QA_BOT_ORDER_POINT, {0, 0, 0}, {0, 0}}, QA_BOT_ORDER_ERROR};
  CHECK(qa_bot_order_status(&ok) == 1);
  CHECK(qa_bot_order_status(&prog) == 2);
  CHECK(qa_bot_order_status(&err) == 0);
  CHECK(qa_bot_order_active(&prog));
  CHECK(!qa_bot_order_active(&err));
  qa_bot_order p1 = {QA_BOT_ORDER_POINT, {0, 0, 0}, {0, 0}};
  qa_bot_order p2 = {QA_BOT_ORDER_POINT, {3, 4, 0}, {0, 0}};
  qa_bot_order p3 = {QA_BOT_ORDER_POINT, {30, 40, 0}, {0, 0}};
  CHECK(qa_same_bot_order(p1, p2));
  CHECK(!qa_same_bot_order(p1, p3));
  qa_bot_order f1 = {QA_BOT_ORDER_FOLLOW, {0, 0, 0}, {7, 2}};
  qa_bot_order f2 = {QA_BOT_ORDER_FOLLOW, {0, 0, 0}, {7, 3}};
  CHECK(!qa_same_bot_order(f1, f2));
}

static void test_libvars(void) {
  CHECK(qa_lib_var_string_value("123") == 123.0f);
  CHECK(qa_lib_var_string_value("12a") == 0.0f);
  CHECK(fabsf(qa_lib_var_string_value("1.5") - 1.5f) < 1e-6f);
  qa_bot_lib_vars vars;
  qa_bot_lib_vars_init(&vars);
  CHECK(qa_bot_lib_vars_value(&vars, "sv_gravity", "800") == 800.0f);
  CHECK(strcmp(qa_bot_lib_vars_get_string(&vars, "SV_GRAVITY"), "800") == 0);
  qa_bot_lib_vars_set(&vars, "sv_gravity", "10");
  CHECK(qa_bot_lib_vars_get_value(&vars, "sv_gravity") == 10.0f);
  CHECK(qa_bot_lib_vars_changed(&vars, "sv_gravity"));
  qa_bot_lib_vars_set_not_modified(&vars, "sv_gravity");
  CHECK(!qa_bot_lib_vars_changed(&vars, "sv_gravity"));
  qa_bot_lib_vars_destroy(&vars);
}

static int64_t fake_next_int(void *ctx) {
  int64_t *values = ctx;
  int64_t v = values[0];
  memmove(values, values + 1, 15 * sizeof *values);
  return v;
}
static void test_genetic(void) {
  double ranks[3] = {10, 20, 30};
  int64_t seq[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  qa_bot_random_bots_a rnd = {fake_next_int, seq, NULL, NULL};
  qa_genetic_selection_result r =
      qa_genetic_parents_and_child_selection(ranks, 3, rnd, NULL, NULL, NULL, NULL);
  CHECK(r.kind == QA_GENETIC_SELECTED);
  CHECK(r.parent1 != r.parent2 && r.child != r.parent1 && r.child != r.parent2);
  double few[2] = {1, 2};
  int64_t seq2[16] = {0};
  qa_bot_random_bots_a rnd2 = {fake_next_int, seq2, NULL, NULL};
  r = qa_genetic_parents_and_child_selection(few, 2, rnd2, NULL, NULL, NULL, NULL);
  CHECK(r.kind == QA_GENETIC_ERROR && r.reason == QA_GENETIC_TOO_FEW_VALID_RANKS);
}

static void test_memory(void) {
  qa_bot_memory mem;
  qa_bot_memory_init(&mem);
  qa_bot_memory_allocation a;
  CHECK(qa_bot_memory_allocate(&mem, 16, QA_BOT_MEMORY_HEAP, true, &a));
  CHECK(a.length == 16 && a.bytes[0] == 0);
  CHECK(qa_bot_memory_get_live_allocations(&mem) == 1);
  qa_bot_memory_free(&mem, &a);
  CHECK(qa_bot_memory_get_live_allocations(&mem) == 0);
  CHECK_ABORTS(qa_bot_memory_allocate(&mem, -1, QA_BOT_MEMORY_HEAP, true, &a));
  qa_bot_memory_destroy(&mem);
}

static void test_pure(void) {
  CHECK(qa_travel_flag_for_type(QA_TRAVEL_WALK) == QA_TFL_WALK);
  CHECK(qa_travel_flag_for_type(999) == QA_TFL_INVALID);
  CHECK(qa_is_source_goal_number(0x40000000));
  CHECK(!qa_is_source_goal_number(5));
  CHECK(!qa_is_source_goal_number(0x40000000 + 0.5));
  CHECK(qa_chat_text_equals("Hello", "hello", false));
  CHECK(!qa_chat_text_equals("Hello", "hello", true));
  CHECK(!qa_chat_text_equals("Hello", NULL, false));
  CHECK(qa_string_contains_bots_a("Hello World", "world", false) == 6);
  CHECK(qa_string_contains_bots_a("Hello", "x", false) == -1);
  CHECK(qa_string_contains_bots_a(NULL, "x", false) == -1);
  qa_bot_goal goal;
  memset(&goal, 0, sizeof goal);
  goal.origin = v3(0, 0, 0);
  goal.mins = v3(-10, -10, -10);
  goal.maxs = v3(10, 10, 10);
  CHECK(qa_touching_goal(v3(0, 0, 0), goal));
  CHECK(!qa_touching_goal(v3(100, 0, 0), goal));
  CHECK(fabsf(qa_bot_angle_difference(350, 10) - (-20.0f)) < 1e-4f);
  CHECK(qa_movement_distance_squared(v3(0, 0, 0), v3(3, 4, 0)) == 25.0);
  char out[64];
  qa_bot_clean_string_bots_a("^1Red^7 ok", out, sizeof out);
  CHECK(strcmp(out, "Red ok") == 0);
  qa_remove_color_escape_sequences_bots_a("^1Red", out, sizeof out);
  CHECK(strcmp(out, "Red") == 0);
  const char *sub = qa_stristr_bots_a("Hello World", "world");
  CHECK(sub != NULL && strcmp(sub, "World") == 0);
  qa_game_ai_context_bots_a ctx;
  CHECK(qa_game_ai_context_bots_a_init(&ctx, 8));
  qa_vec3 gps = qa_bot_gps_to_position_bots_a(&ctx, "10 -20 30");
  CHECK(gps.x == 10 && gps.y == -20 && gps.z == 30);
  qa_game_ai_context_bots_a_destroy(&ctx);
}

static void test_observations_actions_assets(void) {
  qa_bot_entity_observations obs;
  CHECK(qa_bot_entity_observations_init(&obs, 8));
  qa_bot_entity_update up;
  memset(&up, 0, sizeof up);
  up.origin = v3(1, 2, 3);
  qa_bot_entity_observations_update(&obs, 3, &up, 2.0);
  qa_aas_entity_info_bots_a info = qa_bot_entity_observations_info(&obs, 3);
  CHECK(info.valid && info.origin.x == 1);
  CHECK(qa_bot_entity_observations_next_entity(&obs, 0) == 3);
  CHECK_ABORTS(qa_bot_entity_observations_update(&obs, 99, &up, 0));
  qa_bot_entity_observations_destroy(&obs);

  qa_bot_action_buffer actions;
  qa_bot_action_buffer_init(&actions);
  qa_bot_action_buffer_setup(&actions, 4);
  qa_bot_action_jump(&actions, 1);
  qa_bot_input in = qa_bot_action_get_input(&actions, 1, 0.1);
  CHECK((in.action_flags & QA_BOT_ACTION_JUMP) != 0);
  qa_bot_action_reset_input(&actions, 1);
  qa_bot_action_jump(&actions, 1);
  in = qa_bot_action_get_input(&actions, 1, 0.1);
  CHECK((in.action_flags & QA_BOT_ACTION_JUMP) == 0);
  qa_bot_action_move(&actions, 1, v3(1, 0, 0), 9999);
  in = qa_bot_action_get_input(&actions, 1, 0.1);
  CHECK(in.speed == 400.0f);
  qa_bot_action_buffer_destroy(&actions);

  qa_bot_asset_files files;
  qa_bot_asset_files_init(&files);
  const uint8_t payload[3] = {1, 2, 3};
  CHECK(qa_bot_asset_files_add(&files, "bots\\a.bot", payload, 3));
  CHECK(qa_bot_asset_files_read(&files, "BOTS/a.bot", NULL, 0) == 3);
  CHECK(qa_bot_asset_files_read(&files, "bots/missing.bot", NULL, 0) == -1);
  const char *names[4];
  CHECK(qa_bot_asset_files_list(&files, "bots", ".bot", names, 4) == 1);
  qa_bot_asset_files_destroy(&files);
}

static void test_prediction_abort(void) {
  qa_bot_state_bots_a state;
  memset(&state, 0, sizeof state);
  state.enemy = -1;
  CHECK_ABORTS((void)qa_bot_inventory_value_bots_a(&state, 9999));
  qa_bot_move_state_store store;
  qa_bot_move_state_store_init(&store);
  int64_t h = qa_bot_move_state_store_allocate(&store);
  CHECK(h == 1);
  CHECK(qa_bot_move_state_store_from_handle(&store, h) != NULL);
  CHECK(qa_bot_move_state_store_from_handle(&store, 99) == NULL);
  qa_bot_move_state_store_shutdown(&store);
}

static const uint8_t *script_read_file(void *ctx, const char *path, size_t *out_len) {
  (void)ctx;
  const char *text = NULL;
  if (strcmp(path, "root.bot") == 0) text = "#include \"child.inc\"\n#define KEEP 1\n\"hi\" 42\n";
  if (strcmp(path, "child.inc") == 0) text = "\"kid\"\n";
  if (strcmp(path, "botfiles/x.bot") == 0) text = "\"x\"\n";
  if (text == NULL) return NULL;
  *out_len = strlen(text);
  return (const uint8_t *)text;
}
static int script_print_count = 0;
static int script_last_severity = 0;
static void script_print(void *ctx, int severity, const char *text) {
  (void)ctx;
  (void)text;
  script_print_count++;
  script_last_severity = severity;
}
static void script_noop_print(void *ctx, const char *text) {
  (void)ctx;
  (void)text;
}

/* bots/behavior/library/script-sources.ts over the real engine. */
static void test_script_sources(void) {
  qa_ui_script_global_defines *globals =
      qa_ui_script_global_defines_create(NULL, NULL, NULL);
  CHECK(globals != NULL);
  qa_bot_script_sources sources;
  qa_bot_script_sources_init(&sources);
  sources.globals = globals;
  sources.read_file = script_read_file;
  sources.print = script_print;
  sources.common_print = script_noop_print;
  qa_bot_script_sources_set_base_folder(&sources, "a%%b");
  CHECK(strcmp(sources.base_folder, "a%b") == 0);
  CHECK_ABORTS(qa_bot_script_sources_set_base_folder(&sources, "a%d"));
  int64_t handle = qa_bot_script_sources_load(&sources, "root.bot");
  CHECK(handle == 1);
  qa_ui_source_token_memory *raw = NULL;
  bool read = false;
  CHECK(qa_bot_script_sources_read_token_result(&sources, handle, &raw, &read) && read);
  char text[QA_UI_SOURCE_TOKEN_STRING_BYTES];
  qa_ui_source_token_string(raw, text);
  CHECK(strcmp(text, "\"kidhi\"") == 0);
  qa_ui_script_token_record record;
  CHECK(qa_bot_script_sources_read_token(&sources, handle, &record));
  CHECK(record.token->kind == QA_UI_SCRIPT_KIND_NUMBER && record.integer_value == 42);
  qa_ui_script_token_record_release(&record);
  CHECK(qa_bot_script_sources_read_token_result(&sources, handle, &raw, &read) && !read);
  char filename[QA_BOT_MAX_SOURCE_PATH + 1];
  int64_t line = 0;
  CHECK(qa_bot_script_sources_file_and_line(&sources, handle, filename, &line));
  CHECK(strcmp(filename, "root.bot") == 0 && line >= 1);
  script_print_count = 0;
  qa_bot_script_sources_check_open(&sources);
  CHECK(script_print_count == 1 && script_last_severity == 3);
  CHECK(qa_bot_script_sources_free(&sources, handle));
  CHECK(!qa_bot_script_sources_free(&sources, handle));
  CHECK(!qa_bot_script_sources_read_token_result(&sources, handle, &raw, &read));
  CHECK(!qa_bot_script_sources_read_token_result(&sources, 0, &raw, &read));
  CHECK(qa_bot_script_sources_load(&sources, "missing.bot") == 0);
  qa_ui_script_source resolved;
  CHECK(qa_bot_script_sources_resolve_root(&sources, "x.bot", &resolved));
  CHECK(strcmp(sources.base_folder, "botfiles") == 0 && resolved.storage != NULL);
  qa_ui_source_script_destroy(resolved.storage);
  qa_bot_script_sources_dispose_resources(&sources);
  CHECK_ABORTS(qa_bot_script_sources_load(&sources, "root.bot"));
  qa_bot_script_sources_destroy(&sources);
  qa_ui_script_global_defines_destroy(globals);
}

int main(void) {
  test_orders();
  test_libvars();
  test_genetic();
  test_memory();
  test_pure();
  test_observations_actions_assets();
  test_prediction_abort();
  test_script_sources();
  if (failures == 0) {
    printf("bots-a: %d checks passed\n", checks);
    return 0;
  }
  fprintf(stderr, "bots-a: %d/%d checks FAILED\n", failures, checks);
  return 1;
}
