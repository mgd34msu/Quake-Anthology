/* C checks for the q3 bot foundation: q3/library, ai-state, ai-context, movement
 * state, travel routing, prediction and the library/log checkpoint API. Sources:
 * tests/bots/behavior/{log-save,state,ai-save}.test.ts,
 * tests/bots/behavior/library/checkpoint.test.ts and TS oracle runs (see each
 * test). Every callback checks its sentinel ctx. GPL-2.0-or-later. */
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

#include "qa/bots/behavior/library/log.h"
#include "qa/bots/behavior/prediction.h"
#include "qa/bots/behavior/q3/ai_context.h"
#include "qa/bots/behavior/q3/ai_state.h"
#include "qa/bots/behavior/q3/library.h"
#include "qa/bots/behavior/q3/movement_state.h"
#include "qa/bots/behavior/q3/travel/routing.h"
#include "qa/contracts/identity.h"
#include "qa/core/cvars/index.h"
#include "qa/persistence/value.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                                                \
  do {                                                                                             \
    checks++;                                                                                      \
    if (!(cond)) {                                                                                 \
      failures++;                                                                                  \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                              \
    }                                                                                              \
  } while (0)
#define CHECK_STR(a, b)                                                                            \
  do {                                                                                             \
    checks++;                                                                                      \
    const char *check_a = (a), *check_b = (b);                                                     \
    if (strcmp(check_a, check_b) != 0) {                                                           \
      failures++;                                                                                  \
      fprintf(stderr, "FAIL %s:%d: [%s] != [%s]\n", __FILE__, __LINE__, check_a, check_b);         \
    }                                                                                              \
  } while (0)
#define CHECK_ABORTS(...)                                                                          \
  do {                                                                                             \
    checks++;                                                                                      \
    fflush(stderr);                                                                                \
    pid_t pid = fork();                                                                            \
    if (pid == 0) {                                                                                \
      (void)prctl(PR_SET_DUMPABLE, 0);                                                             \
      if (freopen("/dev/null", "w", stderr) == NULL)                                               \
        _exit(2);                                                                                  \
      __VA_ARGS__;                                                                                 \
      _exit(0);                                                                                    \
    }                                                                                              \
    int status = 0;                                                                                \
    waitpid(pid, &status, 0);                                                                      \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {                                   \
      failures++;                                                                                  \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__, __LINE__, #__VA_ARGS__);       \
    }                                                                                              \
  } while (0)

static int sentinel;
#define CTX_OK(ctx) CHECK((ctx) == (void *)&sentinel)
static qa_save_value *disk(const qa_save_value *value) {
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(value, &length);
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  free(bytes);
  return decoded;
}

/* ------------------------------ log-save.test.ts ------------------------------ */
typedef struct log_fixture {
  uint8_t bytes[256];
  size_t length;
  int writes, opens, resumes, closes;
} log_fixture;
static log_fixture log_fx;
typedef struct log_stream {
  int64_t position;
} log_stream;
static log_stream streams[2];
static qa_bot_log_io_result log_ok(void) { return (qa_bot_log_io_result){QA_LOG_IO_OK}; }
static qa_bot_log_io_result stream_write(void *ctx, const uint8_t *bytes, size_t n) {
  CHECK(ctx == &streams[0] || ctx == &streams[1]);
  log_stream *s = ctx;
  log_fx.writes++;
  for (size_t i = 0; i < n; i++)
    log_fx.bytes[s->position++] = bytes[i];
  if ((size_t)s->position > log_fx.length)
    log_fx.length = (size_t)s->position;
  return log_ok();
}
static qa_bot_log_io_result stream_flush(void *ctx) {
  CHECK(ctx == &streams[0] || ctx == &streams[1]);
  return log_ok();
}
static qa_bot_log_io_result stream_close(void *ctx) {
  CHECK(ctx == &streams[0] || ctx == &streams[1]);
  log_fx.closes++;
  return log_ok();
}
static int64_t stream_checkpoint(void *ctx) { return ((log_stream *)ctx)->position; }
static qa_bot_log_open_result make_stream(log_stream *s, int64_t position, bool checkpoint) {
  s->position = position;
  qa_bot_log_open_result r;
  memset(&r, 0, sizeof r);
  r.opened = true;
  r.stream.ctx = s;
  r.stream.write = stream_write;
  r.stream.flush = stream_flush;
  r.stream.close = stream_close;
  r.stream.has_checkpoint = checkpoint;
  r.stream.checkpoint_position = checkpoint ? stream_checkpoint : NULL;
  return r;
}
static void log_print(void *ctx, int severity, const char *text) {
  CTX_OK(ctx);
  (void)severity;
  (void)text;
}
static bool open_checkpoints = true;
static qa_bot_log_open_result log_open(void *ctx, const char *filename) {
  CTX_OK(ctx);
  (void)filename;
  log_fx.opens++;
  return make_stream(&streams[0], 0, open_checkpoints);
}
static qa_bot_log_open_result log_resume(void *ctx, const char *filename, int64_t position) {
  CTX_OK(ctx);
  log_fx.resumes++;
  CHECK_STR(filename, "bot.log");
  CHECK(position == (int64_t)log_fx.length);
  return make_stream(&streams[1], position, true);
}
static void log_setup(qa_bot_log *log, qa_bot_lib_vars *vars, double time) {
  qa_bot_log_init(log, vars);
  log->time = time;
  log->print = log_print;
  log->print_ctx = &sentinel;
  log->open_file = log_open;
  log->open_ctx = &sentinel;
  log->resume_file = log_resume;
  log->resume_ctx = &sentinel;
}
static void test_log_restore(void) {
  memset(&log_fx, 0, sizeof log_fx);
  qa_bot_lib_vars vars;
  qa_bot_lib_vars_init(&vars);
  qa_bot_lib_vars_set(&vars, "log", "1");
  qa_bot_log original, restored;
  log_setup(&original, &vars, 1);
  qa_bot_log_open(&original, "bot.log");
  qa_bot_log_write_time_stamped(&original, "before");
  qa_save_value *capture = qa_bot_log_capture_save_state(&original);
  qa_save_value *saved = disk(capture);
  qa_save_value_free(capture);
  (void)qa_bot_log_dispose_resources(&original);
  int before = log_fx.writes;
  log_setup(&restored, &vars, 1);
  qa_bot_log_restore_save_state(&restored, saved);
  CHECK(log_fx.writes == before);
  CHECK(log_fx.opens == 1);
  CHECK(log_fx.resumes == 1);
  qa_bot_log_write_time_stamped(&restored, "after");
  log_fx.bytes[log_fx.length] = 0;
  CHECK_STR((const char *)log_fx.bytes, "0   00:00:01:00   before\r\n1   00:00:01:00   after\r\n");
  (void)qa_bot_log_dispose_resources(&restored);
  qa_save_value_free(saved);
  qa_bot_log_destroy(&original);
  qa_bot_log_destroy(&restored);
  qa_bot_lib_vars_destroy(&vars);
}
static void test_log_non_resumable(void) {
  memset(&log_fx, 0, sizeof log_fx);
  qa_bot_lib_vars vars;
  qa_bot_lib_vars_init(&vars);
  qa_bot_lib_vars_set(&vars, "log", "1");
  qa_bot_log log;
  log_setup(&log, &vars, 0);
  open_checkpoints = false;
  qa_bot_log_open(&log, "bot.log");
  CHECK_ABORTS((void)qa_bot_log_capture_save_state(&log));
  CHECK(log_fx.closes == 0);
  (void)qa_bot_log_dispose_resources(&log);
  open_checkpoints = true;
  qa_bot_log_destroy(&log);
  qa_bot_lib_vars_destroy(&vars);
}

/* ------------------------------ state.test.ts ------------------------------ */
static qa_vec3 auth_origin = {24, 48, 64}, auth_velocity = {1, 2, 3};
static int32_t auth_stats[16], auth_ammo[16];
static qa_vec3 a_origin(void *ctx) {
  CTX_OK(ctx);
  return auth_origin;
}
static void a_set_origin(void *ctx, qa_vec3 v) {
  CTX_OK(ctx);
  auth_origin = v;
}
static qa_vec3 a_velocity(void *ctx) {
  CTX_OK(ctx);
  return auth_velocity;
}
static void a_set_velocity(void *ctx, qa_vec3 v) {
  CTX_OK(ctx);
  auth_velocity = v;
}
static bool stats_read(void *ctx, double i, int32_t *out) {
  CTX_OK(ctx);
  *out = auth_stats[(int)i];
  return true;
}
static void stats_write(void *ctx, double i, int32_t v) {
  CTX_OK(ctx);
  auth_stats[(int)i] = v;
}
static bool ammo_read(void *ctx, double i, int32_t *out) {
  CTX_OK(ctx);
  *out = auth_ammo[(int)i];
  return true;
}
static void ammo_write(void *ctx, double i, int32_t v) {
  CTX_OK(ctx);
  auth_ammo[(int)i] = v;
}
static void test_state_detached(void) {
  qa_q3_base_player_authority_binding authority = {
      &sentinel, a_origin, a_set_origin, a_velocity, a_set_velocity,
      {&sentinel, stats_read, stats_write}, {&sentinel, ammo_read, ammo_write}};
  qa_q3_base_player_state player, empty;
  CHECK(qa_q3_base_player_state_init(&player, QA_Q3_BASE_PRODUCT_BASEQ3, &authority));
  qa_q3_base_player_state_slots_set(&player.stats, 0, 125);
  qa_q3_base_player_state_slots_set(&player.ammo, 2, 100);
  qa_bot_state_bots_a bot;
  CHECK(qa_bot_state_bots_a_init(&bot, QA_Q3_BASE_PRODUCT_BASEQ3, NULL));
  qa_copy_bot_player_state_bots_a(&bot, &player);
  CHECK(bot.src->cur_ps.origin.x == 24 && bot.src->cur_ps.origin.y == 48 && bot.src->cur_ps.origin.z == 64);
  CHECK(bot.src->cur_ps.ammo[2] == 100);
  CHECK(qa_q3_base_player_state_init(&empty, QA_Q3_BASE_PRODUCT_BASEQ3, NULL));
  qa_copy_bot_player_state_bots_a(&bot, &empty);
  CHECK(bot.src->cur_ps.origin.x == 0 && bot.src->cur_ps.origin.y == 0 && bot.src->cur_ps.origin.z == 0);
  CHECK(bot.src->cur_ps.stats[0] == 0 && bot.src->cur_ps.ammo[2] == 0);
  qa_vec3 origin = qa_q3_base_player_state_record_origin(&player);
  CHECK(origin.x == 24 && origin.y == 48 && origin.z == 64);
  CHECK(qa_q3_base_player_state_slots_get(&player.stats, 0) == 125);
  CHECK(qa_q3_base_player_state_slots_get(&player.ammo, 2) == 100);
  qa_q3_base_player_state empty_mp;
  CHECK(qa_q3_base_player_state_init(&empty_mp, QA_Q3_BASE_PRODUCT_MISSIONPACK, NULL));
  CHECK_ABORTS(qa_copy_bot_player_state_bots_a(&bot, &empty_mp));
  qa_q3_base_player_state_record_destroy(&empty_mp);
  qa_q3_base_player_state_record_destroy(&empty);
  qa_q3_base_player_state_record_destroy(&player);
  qa_bot_state_bots_a_destroy(&bot);
}

/* ------------------------------ ai-save.test.ts ------------------------------ */
static qa_bot_waypoint_bots_a restored_point;
static int64_t waypoint_zero(void *ctx, const qa_bot_waypoint_bots_a *w) {
  CTX_OK(ctx);
  return w == NULL ? -1 : 0;
}
static qa_bot_waypoint_bots_a *waypoint_restored(void *ctx, int64_t id) {
  CTX_OK(ctx);
  return id < 0 ? NULL : &restored_point;
}
static int64_t waypoint_null(void *ctx, const qa_bot_waypoint_bots_a *w) {
  CTX_OK(ctx);
  (void)w;
  return -1;
}
static qa_bot_waypoint_bots_a *waypoint_none(void *ctx, int64_t id) {
  CTX_OK(ctx);
  (void)id;
  return NULL;
}
static void remap_plus_100(void *ctx, int64_t n, int64_t g, int64_t *on, int64_t *og) {
  CTX_OK(ctx);
  *on = n;
  *og = g + 100;
}
static void remap_same(void *ctx, int64_t n, int64_t g, int64_t *on, int64_t *og) {
  CTX_OK(ctx);
  *on = n;
  *og = g;
}
static double debug_zero(void *ctx) {
  CTX_OK(ctx);
  return 0;
}
static void memory_print(void *ctx, const char *text) {
  CTX_OK(ctx);
  (void)text;
}
static qa_save_value *memory_value(qa_q3_game_memory *memory) {
  qa_q3_game_memory_save_state state = qa_q3_game_memory_capture_save_state(memory);
  qa_save_value *o = qa_save_value_object();
  qa_save_value_set(o, "pool", qa_save_value_bytes(state.pool, state.pool_length));
  qa_save_value_set(o, "allocPoint", qa_save_value_number(state.alloc_point));
  qa_q3_game_memory_save_state_destroy(&state);
  return o;
}
static void test_ai_retained_heap(void) {
  qa_q3_game_memory *memory = calloc(1, sizeof *memory), *restored_memory = calloc(1, sizeof *memory);
  qa_q3_game_memory_init(memory, &sentinel, debug_zero, memory_print);
  qa_q3_game_memory_allocation_write_string(qa_q3_game_memory_allocate(memory, 23), "retained");
  qa_bot_state_store_bots_a states, restored;
  qa_bot_state_store_bots_a_init(&states, QA_Q3_BASE_PRODUCT_BASEQ3);
  qa_bot_state_bots_a *state = qa_bot_state_store_acquire(&states, 3, memory);
  qa_bot_waypoint_bots_a point;
  memset(&point, 0, sizeof point);
  state->src->inuse = 1;
  state->src->client = 3;
  state->ai_node = QA_AI_NODE_BATTLE_CHASE;
  state->src->bot_think_residual = 29;
  state->src->inventory[17] = 86;
  state->src->cur_ps.origin.x = -0.0f;
  qa_bot_state_set_text(state->src->characterfile, sizeof state->src->characterfile, "bots/sarge_c.c");
  state->patrol_points = state->current_patrol_point = state->checkpoints = &point;
  qa_bot_activate_goal_bots_a *activation = &state->activate_goal_heap[2];
  activation->next = activation;
  activation->src->time = 13.25f;
  state->activate_stack = activation;
  state->has_scripted_order = true;
  state->scripted_order.progress = QA_BOT_ORDER_SUCCESS;
  state->scripted_order.order.kind = QA_BOT_ORDER_FOLLOW;
  state->scripted_order.order.entity.number = 4;
  state->scripted_order.order.entity.generation = 7;
  state->setup.kind = QA_BOT_SETUP_FAILED;
  state->setup.stage = QA_BOT_SETUP_CHAT_FILE;
  state->setup.error_code = 27;
  qa_save_value *captured = qa_bot_state_store_capture_save_state(&states, memory, waypoint_zero, &sentinel);
  qa_save_value *image = disk(captured), *pool_value = memory_value(memory), *pool = disk(pool_value);
  qa_q3_game_memory_init(restored_memory, &sentinel, debug_zero, memory_print);
  qa_q3_game_memory_restore_save_state(restored_memory, pool);
  qa_bot_state_store_bots_a_init(&restored, QA_Q3_BASE_PRODUCT_BASEQ3);
  qa_bot_state_store_restore_save_state(&restored, image, restored_memory, waypoint_restored, &sentinel,
                                        remap_plus_100, &sentinel);
  qa_bot_state_bots_a *result = qa_bot_state_store_get(&restored, 3);
  CHECK(result != NULL);
  if (result != NULL) {
    CHECK(result->activate_stack == &result->activate_goal_heap[2]);
    CHECK(result->activate_stack->next == result->activate_stack);
    CHECK(result->current_patrol_point == &restored_point);
    CHECK(result->checkpoints == result->patrol_points);
    CHECK(result->src->bot_think_residual == 29);
    CHECK(result->src->inventory[17] == 86);
    CHECK(result->src->cur_ps.origin.x == 0 && signbit(result->src->cur_ps.origin.x));
    CHECK(result->has_scripted_order && result->scripted_order.progress == QA_BOT_ORDER_SUCCESS);
    CHECK(result->scripted_order.order.kind == QA_BOT_ORDER_FOLLOW);
    CHECK(result->scripted_order.order.entity.number == 4 && result->scripted_order.order.entity.generation == 107);
    CHECK(result->setup.kind == QA_BOT_SETUP_FAILED && result->setup.stage == QA_BOT_SETUP_CHAT_FILE &&
          result->setup.error_code == 27);
    CHECK(result->ai_node == QA_AI_NODE_BATTLE_CHASE);
    CHECK_STR(qa_bot_state_text(result, result->src->characterfile, "characterfile"), "bots/sarge_c.c");
    CHECK(qa_q3_game_memory_allocated_bytes(restored_memory) == qa_q3_game_memory_allocated_bytes(memory));
    result->src->inventory[17] = 99;
    CHECK(state->src->inventory[17] == 86);
    CHECK(result->has_source_allocation);
  }
  qa_save_value *empty = qa_save_value_array();
  CHECK_ABORTS(qa_bot_state_store_restore_save_state(&restored, empty, restored_memory, waypoint_none,
                                                     &sentinel, remap_same, &sentinel));
  qa_save_value_free(empty);
  qa_save_value_free(captured);
  qa_save_value_free(image);
  qa_save_value_free(pool_value);
  qa_save_value_free(pool);
  qa_bot_state_store_bots_a_destroy(&states);
  qa_bot_state_store_bots_a_destroy(&restored);
  free(memory);
  free(restored_memory);
}
static void test_ai_pool_disagreement(void) {
  qa_q3_game_memory *memory = calloc(1, sizeof *memory);
  qa_q3_game_memory_init(memory, &sentinel, debug_zero, memory_print);
  qa_bot_state_store_bots_a states, restored;
  qa_bot_state_store_bots_a_init(&states, QA_Q3_BASE_PRODUCT_BASEQ3);
  qa_bot_state_bots_a *state = qa_bot_state_store_acquire(&states, 0, memory);
  state->src->inuse = 1;
  qa_save_value *image = qa_bot_state_store_capture_save_state(&states, memory, waypoint_null, &sentinel);
  qa_save_value *first = image->items[0];
  qa_save_value *bytes = (qa_save_value *)qa_save_value_get(qa_save_value_get(first, "state"), "bytes");
  bytes->bytes[0] = 0;
  qa_save_value *saved = disk(image);
  qa_bot_state_store_bots_a_init(&restored, QA_Q3_BASE_PRODUCT_BASEQ3);
  CHECK_ABORTS(qa_bot_state_store_restore_save_state(&restored, saved, memory, waypoint_none, &sentinel,
                                                     remap_same, &sentinel));
  CHECK(state->src->inuse == 1);
  qa_save_value_free(image);
  qa_save_value_free(saved);
  qa_bot_state_store_bots_a_destroy(&states);
  qa_bot_state_store_bots_a_destroy(&restored);
  free(memory);
}
static void test_cvar_cached_restore(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "bot-cvar-save"));
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context.session = owner.session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  qa_cvar_registry *registry = qa_cvar_registry_create(&options);
  qa_bot_cvar_bots_a cell, restored;
  qa_bot_cvar_bots_a_init(&cell, registry);
  qa_bot_cvar_register_bots_a(&cell, "bot_thinktime", "100", 0);
  qa_bot_cvar_write_integer(&cell, 17);
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  if (qa_cvar_registry_set(registry, "bot_thinktime", "200", true, &snapshot))
    qa_cvar_read_free(&snapshot);
  qa_save_value *captured = qa_bot_cvar_capture_save_state(&cell), *saved = disk(captured);
  qa_bot_cvar_bots_a_init(&restored, registry);
  qa_bot_cvar_restore_save_state(&restored, saved);
  CHECK_STR(restored.value, "100");
  CHECK(restored.integer_value == 17);
  qa_bot_cvar_update_bots_a(&restored);
  CHECK_STR(restored.value, "200");
  CHECK(restored.integer_value == 200);
  CHECK_ABORTS(qa_bot_cvar_write_integer(&restored, 0.5));
  qa_save_value_free(captured);
  qa_save_value_free(saved);
  qa_cvar_registry_destroy(registry);
  qa_identity_owner_destroy(&owner);
}

/* ------------------- library/checkpoint.test.ts (movement handles) ------------------- */
static qa_call_steps no_command(void *ctx, int64_t client, const char *command) {
  CTX_OK(ctx);
  (void)client;
  (void)command;
  return qa_call_steps_done(NULL);
}
static double move_time(void *ctx) {
  CTX_OK(ctx);
  return 0;
}
static void move_print(void *ctx, int severity, const char *text) {
  CTX_OK(ctx);
  (void)severity;
  (void)text;
}
static qa_bot_lib_vars *active_vars;
static qa_bot_lib_var *move_lib_var(void *ctx, const char *name, const char *value) {
  CTX_OK(ctx);
  return qa_bot_lib_vars_get_or_create(active_vars, name, value);
}
static bool edge_unused(void *ctx, int64_t client, int64_t id) {
  CTX_OK(ctx);
  (void)client;
  (void)id;
  return false;
}
static void test_movement_checkpoint(void) {
  qa_bot_memory memory, restored_memory;
  qa_bot_memory_init(&memory);
  qa_bot_lib_vars variables, restored_variables;
  qa_bot_lib_vars_init_memory(&variables, &memory);
  qa_bot_action_buffer actions, restored_actions;
  qa_bot_action_buffer_init(&actions);
  qa_bot_action_host host = {no_command, &sentinel};
  qa_bot_action_buffer_bind(&actions, host, &memory);
  qa_bot_action_buffer_setup(&actions, 4);
  qa_bot_move_state_store movement, restored_movement;
  qa_bot_move_state_store_init(&movement);
  qa_bot_move_state_host move_host = {&sentinel, move_time, move_print, move_lib_var};
  qa_bot_move_state_store_bind(&movement, move_host, &memory);
  active_vars = &variables;
  qa_bot_move_state_store_setup(&movement);
  qa_bot_lib_vars_set(&variables, "sv_gravity", "743.25");
  qa_bot_lib_vars_set_not_modified(&variables, "sv_gravity");
  int64_t handle = qa_bot_move_state_store_allocate(&movement);
  qa_bot_move_state *state = qa_bot_move_state_store_from_handle(&movement, handle);
  CHECK(state != NULL);
  state->src->client = 2;
  state->src->origin = qa_bot_fv3((qa_vec3){12.25, -0.0, 77});
  state->src->avoid_reach = 91;
  state->src->avoid_reach_time = 4.25f;
  qa_bot_action_move(&actions, 2, (qa_vec3){1, 0, 0}, 213);
  qa_bot_input input = qa_bot_action_get_input(&actions, 2, 0.25);
  qa_bot_memory_capture capture;
  CHECK(qa_bot_memory_checkpoint(&memory, &capture));
  qa_save_value *saved_variables = qa_bot_lib_vars_checkpoint(&variables, &capture);
  qa_save_value *saved_actions = qa_bot_action_buffer_checkpoint(&actions, &capture);
  qa_save_value *saved_movement = qa_bot_move_state_store_checkpoint(&movement, &capture, &variables);
  qa_bot_memory_dispose(&memory);
  CHECK_ABORTS((void)qa_bot_move_state_record(state));
  qa_bot_memory_init(&restored_memory);
  qa_bot_memory_restored allocations;
  qa_bot_memory_restore(&restored_memory, capture.image, &allocations);
  qa_bot_lib_vars_init_memory(&restored_variables, &restored_memory);
  qa_bot_lib_vars_restore(&restored_variables, saved_variables, &allocations);
  qa_bot_action_buffer_init(&restored_actions);
  qa_bot_action_buffer_bind(&restored_actions, host, &restored_memory);
  qa_bot_action_buffer_restore(&restored_actions, saved_actions, &allocations);
  qa_bot_move_state_store_init(&restored_movement);
  qa_bot_move_state_store_bind(&restored_movement, move_host, &restored_memory);
  active_vars = &restored_variables;
  qa_bot_move_state_store_restore(&restored_movement, saved_movement, &allocations, &restored_variables,
                                  edge_unused, &sentinel);
  qa_bot_input again = qa_bot_action_get_input(&restored_actions, 2, 0.25);
  CHECK(again.think_time == input.think_time && again.speed == input.speed &&
        again.direction.x == input.direction.x && again.direction.y == input.direction.y &&
        again.direction.z == input.direction.z && again.view_angles.x == input.view_angles.x &&
        again.view_angles.y == input.view_angles.y && again.view_angles.z == input.view_angles.z &&
        again.action_flags == input.action_flags && again.weapon == input.weapon);
  CHECK(input.speed == 213 && input.direction.x == 1);
  qa_bot_move_state *restored_state = qa_bot_move_state_store_from_handle(&restored_movement, handle);
  CHECK(restored_state != NULL);
  if (restored_state != NULL) {
    CHECK(restored_state->src->origin.x == 12.25f);
    CHECK(restored_state->src->origin.y == 0 && signbit(restored_state->src->origin.y));
    CHECK(restored_state->src->avoid_reach == 91 && restored_state->src->avoid_reach_time == 4.25f);
  }
  CHECK(restored_movement.sv_gravity == qa_bot_lib_vars_get(&restored_variables, "sv_gravity"));
  CHECK(!qa_bot_lib_vars_changed(&restored_variables, "sv_gravity"));
  qa_bot_lib_vars_set(&restored_variables, "sv_gravity", "812");
  CHECK(qa_bot_lib_var_value(restored_movement.sv_gravity) == 812);
  CHECK(qa_bot_move_state_store_allocate(&restored_movement) == handle + 1);
  qa_bot_memory_dispose(&restored_memory);
  qa_save_value_free(saved_variables);
  qa_save_value_free(saved_actions);
  qa_save_value_free(saved_movement);
  qa_bot_memory_capture_destroy(&capture);
  qa_bot_memory_restored_destroy(&allocations);
  qa_bot_move_state_store_destroy(&movement);
  qa_bot_move_state_store_destroy(&restored_movement);
  qa_bot_action_buffer_destroy(&actions);
  qa_bot_action_buffer_destroy(&restored_actions);
  qa_bot_lib_vars_destroy(&variables);
  qa_bot_lib_vars_destroy(&restored_variables);
  qa_bot_memory_destroy(&memory);
  qa_bot_memory_destroy(&restored_memory);
}

/* ------------------- routing (TS oracle: scratchpad/oracle.ts) ------------------- */
static void test_routing_oracle(void) {
  CHECK(qa_movement_angle_difference(350, 10) == -20);
  CHECK(qa_movement_angle_difference(10, 350) == 20);
  CHECK(qa_movement_angle_difference(-170.25, 170.5) == 19.25f);
  CHECK(qa_distance_from_line_squared((qa_vec3){1, 2, 3}, (qa_vec3){0, 0, 0}, (qa_vec3){10, 0, 0}) == 13);
  CHECK(qa_distance_from_line_squared((qa_vec3){-5, 1, 0}, (qa_vec3){0, 0, 0}, (qa_vec3){10, 0, 0}) == 26);
  CHECK(qa_distance_from_line_squared((qa_vec3){3.3, -7.1, 2}, (qa_vec3){1, 1, 1}, (qa_vec3){4, 9, -2}) ==
        71.900009155273438);
  qa_travel_reachability reach;
  memset(&reach, 0, sizeof reach);
  reach.reach.start = (qa_vec3){10, 0, 0};
  reach.reach.end = (qa_vec3){20, 0, 0};
  qa_bot_avoid_spot spots[2] = {{{15, 1, 0}, 4, 2}, {{5, 0.5f, 0}, 2, 1}};
  reach.reach.travel_type = QA_TRAVEL_WALK;
  CHECK(qa_avoid_movement_spots((qa_vec3){0, 0, 0}, reach, spots, 1) == 2);
  CHECK(qa_avoid_movement_spots((qa_vec3){0, 0, 0}, reach, spots, 2) == 1);
  reach.reach.travel_type = QA_TRAVEL_JUMP;
  CHECK(qa_avoid_movement_spots((qa_vec3){0, 0, 0}, reach, spots, 1) == 0);
}

/* ------------------- q3/library binding (TS oracle) ------------------- */
typedef struct file_entry {
  const char *path, *text;
} file_entry;
static const file_entry FILES[] = {
    {"botfiles/weapons.c", "weaponinfo { name \"Gun\" number 1 projectile \"bullet\" numprojectiles 1 speed 0 } "
                           "projectileinfo { name \"bullet\" damage 10 }"},
    {"botfiles/items.c", "iteminfo \"weapon_gun\" { name \"Gun\" model \"models/gun.md3\" modelindex 1 type 1 "
                         "index 1 respawntime 30 mins {-15,-15,-15} maxs {15,15,15} }"}};
static const uint8_t *library_read(void *ctx, const char *path, size_t *out_len) {
  CTX_OK(ctx);
  for (size_t i = 0; i < sizeof FILES / sizeof FILES[0]; i++)
    if (strcmp(FILES[i].path, path) == 0) {
      *out_len = strlen(FILES[i].text);
      return (const uint8_t *)FILES[i].text;
    }
  return NULL;
}
static char library_prints[2048];
static void library_print(void *ctx, int severity, const char *text) {
  CTX_OK(ctx);
  size_t used = strlen(library_prints);
  snprintf(library_prints + used, sizeof library_prints - used, "%d:%s|", severity, text);
}
static int64_t random_state = 7;
static int64_t library_random(void *ctx) {
  CTX_OK(ctx);
  random_state = (random_state * 1103515245 + 12345) & 0x7fffffff;
  return random_state;
}
static double library_ms(void *ctx) {
  CTX_OK(ctx);
  return 0;
}
static void library_command(void *ctx, int64_t client, const char *command) {
  CTX_OK(ctx);
  (void)client;
  (void)command;
}
static qa_bot_log_open_result library_open_log(void *ctx, const char *filename) {
  CTX_OK(ctx);
  (void)filename;
  qa_bot_log_open_result r;
  memset(&r, 0, sizeof r);
  return r;
}
static void test_library_binding(void) {
  qa_source_bot_library_options_bots_a options;
  memset(&options, 0, sizeof options);
  options.ctx = &sentinel;
  options.read_file = library_read;
  options.random.next_int = library_random;
  options.random.ctx = &sentinel;
  options.milliseconds = library_ms;
  options.print = library_print;
  options.client_command = library_command;
  options.open_log = library_open_log;
  qa_bot_library_bots_a *library = malloc(sizeof *library);
  qa_bot_library_bots_a_init(library, &options);
  qa_bot_lib_vars_set(&library->variables, "nochat", "1");
  library_prints[0] = 0;
  CHECK(qa_bot_library_setup_bots_a(library) == 0);
  CHECK(qa_bot_library_time_bots_a(library) == 0);
  CHECK_STR(library_prints, "1:------- BotLib Initialization -------\n|3:loaded weapons.c|1:loaded items.c\n|"
                            "3:counldn't load syn.c|3:counldn't load rnd.c|3:counldn't load match.c|");
  CHECK(qa_bot_library_start_frame_bots_a(library, 1.1) == 0);
  CHECK(qa_bot_library_time_bots_a(library) == 1.1000000238418579);
  CHECK(qa_bot_library_shutdown_bots_a(library) == 0);
  CHECK(qa_bot_memory_get_live_allocations(&library->memory) == 0);
  CHECK(qa_bot_library_shutdown_bots_a(library) == 0);
  qa_bot_library_bots_a_destroy(library);
  free(library);
}

/* ------------------- prediction (TS oracle) ------------------- */
static int stop_calls;
static qa_movement_input predict_input(void *ctx, const qa_movement_result *previous, int64_t frame,
                                       qa_vec3 command_move) {
  CTX_OK(ctx);
  (void)frame;
  qa_movement_input input;
  memset(&input, 0, sizeof input);
  input.kind = "q3";
  input.execution = "prediction";
  input.profile.id = "p";
  input.frame.elapsed.kind = QA_SOURCE_TIME_MILLISECONDS;
  input.frame.elapsed.value = 100;
  input.view_offset = command_move;
  input.state.kind = QA_MOVEMENT_STATE_Q3;
  if (previous != NULL)
    input.state = previous->state;
  else
    input.state.as.q3.origin = (qa_vec3){1, 2, 3};
  return input;
}
static qa_movement_result predict_move(void *ctx, qa_movement_input input, qa_movement_services services) {
  CTX_OK(ctx);
  (void)services;
  qa_movement_result result;
  memset(&result, 0, sizeof result);
  result.kind = "q3";
  result.status = "active";
  result.state = input.state;
  result.state.as.q3.origin.x += input.view_offset.x * 0.1;
  result.state.as.q3.velocity = (qa_vec3){input.view_offset.x, 0, 0};
  result.ground.kind = input.view_offset.x > 0 ? QA_TRACE_HIT_WORLD : QA_TRACE_HIT_NONE;
  result.water_level = 1;
  result.bounds = (qa_bounds){{-1, -1, -1}, {1, 1, 1}};
  return result;
}
static qa_bot_movement_stop predict_stop(void *ctx, const qa_movement_result *previous,
                                         const qa_movement_result *result, int64_t frame) {
  CTX_OK(ctx);
  (void)previous;
  (void)result;
  stop_calls++;
  qa_bot_movement_stop stop = {frame == 2 ? 8 : 0, {99, 0, 0}, frame == 2, frame == 2 ? 5 : 0};
  return stop;
}
static void test_prediction_oracle(void) {
  qa_bot_movement_prediction query;
  memset(&query, 0, sizeof query);
  query.entity_num = 3;
  query.origin = (qa_vec3){1, 2, 3};
  query.presence = 2;
  query.on_ground = true;
  query.command_move = (qa_vec3){400, 0, 0};
  query.command_frames = 2;
  query.max_frames = 4;
  query.frame_time = 0.1;
  query.stop_events = 8;
  qa_bot_movement_projection_bots_a projection;
  memset(&projection, 0, sizeof projection);
  projection.ctx = &sentinel;
  projection.provider.kind = "q3";
  projection.provider.id = "p";
  projection.provider.ctx = &sentinel;
  projection.provider.move = predict_move;
  projection.input = predict_input;
  projection.stop = predict_stop;
  stop_calls = 0;
  qa_bot_travel_prediction_result r = qa_project_bot_movement(query, projection);
  CHECK(r.end.x == 99 && r.end.y == 0 && r.end.z == 0);
  CHECK(r.has_end_area && r.end_area == 5);
  CHECK(r.velocity.x == 0 && r.velocity.y == 0 && r.velocity.z == 0);
  CHECK(r.frames == 3 && r.stop_event == 8);
  CHECK(r.trajectory_count == 4);
  CHECK(r.trajectory_count == 4 && r.trajectory[1].x == 41 && r.trajectory[2].x == 81 && r.trajectory[3].x == 99);
  CHECK(r.seconds == 0.30000000000000004);
  CHECK(!r.grounded && r.water_level == 1 && stop_calls == 3);
  qa_bot_travel_prediction_result_destroy(&r);
}

int main(void) {
  test_log_restore();
  test_log_non_resumable();
  test_state_detached();
  test_ai_retained_heap();
  test_ai_pool_disagreement();
  test_cvar_cached_restore();
  test_movement_checkpoint();
  test_routing_oracle();
  test_library_binding();
  test_prediction_oracle();
  printf("test_q3_foundation: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
