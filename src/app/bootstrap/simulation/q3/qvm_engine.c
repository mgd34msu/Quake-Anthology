/* Port of src/app/bootstrap/simulation/q3/qvm-engine.ts. Async entry points are synchronous. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/qvm_engine.h"
#include <stdlib.h>
#include <string.h>
#include "qa/compat/qvm/abi.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

typedef struct qvm_engine {
  qa_app_q3_game_engine base;
  qa_qvm_game game;
  qa_app_q3_qvm_engine_options options;
  const qa_qvm_host_call *current_call;
  bool has_checkpoint;
  qa_guest_checkpoint checkpoint;
} qvm_engine;

static const qa_app_q3_game_engine_ops qvm_ops;
static qvm_engine *self_of(qa_app_q3_game_engine *engine) { return (qvm_engine *)engine; }

/* host(call): currentCall is restored on a raise too (TS catch). */
typedef struct host_frame { qvm_engine *engine; const qa_qvm_host_call *previous; } host_frame;
static void host_frame_restore(void *arg) { host_frame *frame = arg; frame->engine->current_call = frame->previous; }
static qa_qvm_host_result host(void *ctx, const qa_qvm_host_call *call) {
  qvm_engine *engine = ctx;
  engine->options.assert_current(engine->options.ctx);
  host_frame frame = {engine, engine->current_call};
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, host_frame_restore, &frame);
  engine->current_call = call;
  qa_qvm_host_result result = engine->options.traps(engine->options.ctx, call);
  engine->options.assert_current(engine->options.ctx);
  qa_cleanup_pop(&cleanup);
  engine->current_call = frame.previous;
  return result;
}

qa_app_q3_game_engine *qa_app_q3_qvm_engine_create(qa_app_q3_qvm_engine_options options) {
  qvm_engine *engine = calloc(1, sizeof *engine);
  if (engine == NULL) return NULL;
  engine->options = options;
  qa_qvm_module_options module;
  memset(&module, 0, sizeof module);
  module.artifact = options.artifact;
  module.host = host;
  module.host_ctx = engine;
  module.host_state = options.host_state;
  module.semantics = options.semantics;
  qa_qvm_game_init(&engine->game, &module);
  engine->base.ops = &qvm_ops;
  engine->base.kind = "qvm";
  engine->base.module = engine->game.module.profile.module;
  engine->base.data = &engine->game.data;
  return &engine->base;
}
qa_qvm_game *qa_app_q3_qvm_engine_game(qa_app_q3_game_engine *engine) {
  return engine != NULL && engine->ops == &qvm_ops ? &self_of(engine)->game : NULL;
}

static bool e_active(qa_app_q3_game_engine *base) {
  qvm_engine *engine = self_of(base);
  qa_qvm_interpreter_runtime *runtime = engine->game.module.interpreter;
  return (runtime != NULL && runtime->is_active(runtime->ctx)) || engine->current_call != NULL;
}
static void e_initialize(qa_app_q3_game_engine *base, double level_time, double seed, bool restart) {
  qa_qvm_game_initialize_async(&self_of(base)->game, level_time, seed, restart);
}
static void e_shutdown(qa_app_q3_game_engine *base, bool restart) { qa_qvm_game_shutdown_async(&self_of(base)->game, restart); }
static char *e_client_connect(qa_app_q3_game_engine *base, double client, bool first_time, bool is_bot) {
  return qa_qvm_game_client_connect_async(&self_of(base)->game, client, first_time, is_bot);
}
static void e_client_begin(qa_app_q3_game_engine *base, double client) { qa_qvm_game_client_begin_async(&self_of(base)->game, client); }
static void e_client_userinfo_changed(qa_app_q3_game_engine *base, double client) {
  qa_qvm_game_client_userinfo_changed_async(&self_of(base)->game, client);
}
static void validate_current(void *ctx) {
  qvm_engine *engine = ctx;
  engine->options.assert_current(engine->options.ctx);
}
static void e_client_disconnect(qa_app_q3_game_engine *base, double client) {
  qvm_engine *engine = self_of(base);
  const qa_qvm_host_call *call = engine->current_call;
  if (call == NULL) qa_qvm_game_client_disconnect_async(&engine->game, client);
  else {
    int32_t args[10] = {QA_QVM_GAME_EXPORT_GAME_CLIENT_DISCONNECT, (int32_t)client, 0, 0, 0, 0, 0, 0, 0, 0};
    (void)call->base.invoke_async(call->base.call_ctx, args, 10, 0, validate_current, engine);
  }
}
static void e_client_command_async(qa_app_q3_game_engine *base, double client, const char *const *argv, size_t argc) {
  qa_qvm_game_client_command_async(&self_of(base)->game, client, argv, argc);
}
static void e_client_command(qa_app_q3_game_engine *base, double client, const char *const *argv, size_t argc) {
  qa_qvm_game_client_command(&self_of(base)->game, client, argv, argc);
}
static void e_client_think(qa_app_q3_game_engine *base, double client) { qa_qvm_game_client_think_async(&self_of(base)->game, client); }
static void e_run_frame(qa_app_q3_game_engine *base, double time) { qa_qvm_game_run_frame_async(&self_of(base)->game, time); }
static bool e_console_command(qa_app_q3_game_engine *base, const char *const *argv, size_t argc) {
  return qa_qvm_game_console_command_async(&self_of(base)->game, argv, argc);
}
static void e_bot_frame(qa_app_q3_game_engine *base, double time) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_BOTAI_START_FRAME, qa_int32(time)};
  (void)qa_qvm_module_call_async(&self_of(base)->game.module, words, 2, 0, NULL, NULL);
}
static void free_checkpoint(qvm_engine *engine) {
  if (!engine->has_checkpoint) return;
  free((void *)engine->checkpoint.data.bytes);
  free((void *)engine->checkpoint.host_state.bytes.bytes);
  engine->has_checkpoint = false;
}
static const qa_guest_checkpoint *e_checkpoint(qa_app_q3_game_engine *base) {
  qvm_engine *engine = self_of(base);
  free_checkpoint(engine);
  qa_qvm_module_checkpoint(&engine->game.module, &engine->checkpoint);
  engine->has_checkpoint = true;
  return &engine->checkpoint;
}
static void e_restore(qa_app_q3_game_engine *base, const qa_guest_checkpoint *checkpoint) {
  if (checkpoint->kind != QA_GUEST_CHECKPOINT_QVM) qa_raise("Error", 0, 0, "Interpreted Q3 restore requires a qvm checkpoint");
  qa_qvm_module_restore(&self_of(base)->game.module, checkpoint);
}
static void e_retire(qa_app_q3_game_engine *base) { qa_qvm_game_retire(&self_of(base)->game); }
static void e_destroy(qa_app_q3_game_engine *base) {
  if (base == NULL) return;
  qvm_engine *engine = self_of(base);
  qa_qvm_game_destroy(&engine->game);
  free_checkpoint(engine);
  free(engine);
}

static const qa_app_q3_game_engine_ops qvm_ops = {
  e_active, e_initialize, e_shutdown, e_client_connect, e_client_begin, e_client_userinfo_changed, e_client_disconnect,
  e_client_command_async, e_client_command, e_client_think, e_run_frame, e_console_command, e_bot_frame,
  e_checkpoint, e_restore, e_retire, e_destroy,
};
