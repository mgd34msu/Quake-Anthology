/* Port of src/compat/qvm/game.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/game.h"

#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static qa_qvm_host_result game_host(void *ctx, const qa_qvm_host_call *call) {
  qa_qvm_game *game = ctx;
  if (strcmp(call->kind, "engine") == 0 && strcmp(call->role, "qagame") == 0 &&
      call->code == QA_QVM_GAME_IMPORT_G_LOCATE_GAME_DATA) {
    qa_qvm_game_data_locate(&game->data, qa_qvm_host_call_word(call, 1),
                            qa_qvm_host_call_word(call, 2), qa_qvm_host_call_word(call, 3),
                            qa_qvm_host_call_word(call, 4), qa_qvm_host_call_word(call, 5));
    return 0;
  }
  if (game->host == NULL) return qa_qvm_reject_syscall_host(game->host_ctx, call);
  return game->host(game->host_ctx, call);
}

void qa_qvm_game_init(qa_qvm_game *game, const qa_qvm_module_options *options) {
  if (strcmp(options->artifact->role, "qagame") != 0)
    qa_range_error("QvmGame requires a qagame artifact");
  game->host = options->host;
  game->host_ctx = options->host_ctx;
  qa_qvm_module_options wrapped = *options;
  wrapped.host = game_host;
  wrapped.host_ctx = game;
  qa_qvm_module_init(&game->module, &wrapped);
  const qa_qvm_game_memory memory = qa_qvm_memory_game_memory(game->module.memory);
  qa_qvm_game_data_init(&game->data, &memory,
                        qa_qvm_module_get_abi_profile(&game->module));
}

void qa_qvm_game_destroy(qa_qvm_game *game) {
  if (game == NULL) return;
  qa_qvm_game_data_destroy(&game->data);
  qa_qvm_module_destroy(&game->module);
}

qa_q3_api_identity qa_qvm_game_get_api(const qa_qvm_game *game) {
  qa_qvm_abi_profile profile = qa_qvm_module_get_abi_profile(&game->module);
  qa_q3_api_identity api = {QA_Q3_API_QAGAME,
                            profile != NULL && strcmp(profile, "q3-modern") == 0 ? 8 : 7};
  return api;
}

static void call_words(qa_qvm_game *game, const int32_t *words, size_t count) {
  (void)qa_qvm_module_call(&game->module, words, count, 0);
}
static void call_words_async(qa_qvm_game *game, const int32_t *words, size_t count) {
  (void)qa_qvm_module_call_async(&game->module, words, count, 0, NULL, NULL);
}

void qa_qvm_game_initialize(qa_qvm_game *game, double level_time, double random_seed, bool restart) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_INIT, qa_int32(level_time), qa_int32(random_seed),
                     restart ? 1 : 0};
  call_words(game, words, 4);
}
void qa_qvm_game_shutdown(qa_qvm_game *game, bool restart) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_SHUTDOWN, restart ? 1 : 0};
  call_words(game, words, 2);
}
char *qa_qvm_game_client_connect(qa_qvm_game *game, double client, bool first_time, bool is_bot) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_CLIENT_CONNECT, qa_int32(client),
                     first_time ? 1 : 0, is_bot ? 1 : 0};
  int32_t denied = qa_qvm_module_call(&game->module, words, 4, 0);
  return denied == 0 ? NULL : qa_qvm_memory_read_string(game->module.memory, denied);
}
void qa_qvm_game_client_begin(qa_qvm_game *game, double client) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_CLIENT_BEGIN, qa_int32(client)};
  call_words(game, words, 2);
}
void qa_qvm_game_client_userinfo_changed(qa_qvm_game *game, double client) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_CLIENT_USERINFO_CHANGED, qa_int32(client)};
  call_words(game, words, 2);
}
void qa_qvm_game_client_disconnect(qa_qvm_game *game, double client) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_CLIENT_DISCONNECT, qa_int32(client)};
  call_words(game, words, 2);
}
void qa_qvm_game_client_command(qa_qvm_game *game, double client, const char *const *arguments,
                                size_t argument_count) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_CLIENT_COMMAND, qa_int32(client)};
  (void)qa_qvm_module_command(&game->module, words, 2, arguments, argument_count);
}
void qa_qvm_game_client_think(qa_qvm_game *game, double client) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_CLIENT_THINK, qa_int32(client)};
  call_words(game, words, 2);
}
void qa_qvm_game_run_frame(qa_qvm_game *game, double time) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_RUN_FRAME, qa_int32(time)};
  call_words(game, words, 2);
}
bool qa_qvm_game_console_command(qa_qvm_game *game, const char *const *arguments,
                                 size_t argument_count) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_CONSOLE_COMMAND};
  return qa_qvm_module_command(&game->module, words, 1, arguments, argument_count) != 0;
}
void qa_qvm_game_bot_frame(qa_qvm_game *game, double time) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_BOTAI_START_FRAME, qa_int32(time)};
  call_words(game, words, 2);
}

void qa_qvm_game_initialize_async(qa_qvm_game *game, double level_time, double random_seed,
                                  bool restart) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_INIT, qa_int32(level_time), qa_int32(random_seed),
                     restart ? 1 : 0};
  call_words_async(game, words, 4);
}
void qa_qvm_game_shutdown_async(qa_qvm_game *game, bool restart) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_SHUTDOWN, restart ? 1 : 0};
  call_words_async(game, words, 2);
}
char *qa_qvm_game_client_connect_async(qa_qvm_game *game, double client, bool first_time,
                                       bool is_bot) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_CLIENT_CONNECT, qa_int32(client),
                     first_time ? 1 : 0, is_bot ? 1 : 0};
  int32_t denied = qa_qvm_module_call_async(&game->module, words, 4, 0, NULL, NULL);
  return denied == 0 ? NULL : qa_qvm_memory_read_string(game->module.memory, denied);
}
void qa_qvm_game_client_begin_async(qa_qvm_game *game, double client) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_CLIENT_BEGIN, qa_int32(client)};
  call_words_async(game, words, 2);
}
void qa_qvm_game_client_userinfo_changed_async(qa_qvm_game *game, double client) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_CLIENT_USERINFO_CHANGED, qa_int32(client)};
  call_words_async(game, words, 2);
}
void qa_qvm_game_client_disconnect_async(qa_qvm_game *game, double client) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_CLIENT_DISCONNECT, qa_int32(client)};
  call_words_async(game, words, 2);
}
void qa_qvm_game_client_command_async(qa_qvm_game *game, double client,
                                      const char *const *arguments, size_t argument_count) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_CLIENT_COMMAND, qa_int32(client)};
  (void)qa_qvm_module_command_async(&game->module, words, 2, arguments, argument_count, NULL, NULL);
}
void qa_qvm_game_client_think_async(qa_qvm_game *game, double client) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_CLIENT_THINK, qa_int32(client)};
  call_words_async(game, words, 2);
}
void qa_qvm_game_run_frame_async(qa_qvm_game *game, double time) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_RUN_FRAME, qa_int32(time)};
  call_words_async(game, words, 2);
}
bool qa_qvm_game_console_command_async(qa_qvm_game *game, const char *const *arguments,
                                       size_t argument_count) {
  int32_t words[] = {QA_QVM_GAME_EXPORT_GAME_CONSOLE_COMMAND};
  return qa_qvm_module_command_async(&game->module, words, 1, arguments, argument_count, NULL,
                                     NULL) != 0;
}
void qa_qvm_game_restart(qa_qvm_game *game, const uint8_t *bytes, size_t length) {
  qa_qvm_module_restart(&game->module, bytes, length);
  qa_qvm_game_data_clear(&game->data);
}
void qa_qvm_game_retire(qa_qvm_game *game) { qa_qvm_module_retire(&game->module); }
