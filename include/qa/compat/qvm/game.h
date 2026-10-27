/* Port of src/compat/qvm/game.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_GAME_H
#define QA_COMPAT_QVM_GAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/game_data.h"
#include "qa/compat/qvm/module.h"
#include "qa/contracts/execution.h"

typedef struct qa_qvm_game {
  qa_qvm_module module;
  qa_qvm_game_data data;
  qa_qvm_host_fn host;
  void *host_ctx;
} qa_qvm_game;

void qa_qvm_game_init(qa_qvm_game *game, const qa_qvm_module_options *options);
void qa_qvm_game_destroy(qa_qvm_game *game);
qa_q3_api_identity qa_qvm_game_get_api(const qa_qvm_game *game);

void qa_qvm_game_initialize(qa_qvm_game *game, double level_time, double random_seed, bool restart);
void qa_qvm_game_shutdown(qa_qvm_game *game, bool restart);
/* Malloc'd denial string, NULL when the client may connect. */
char *qa_qvm_game_client_connect(qa_qvm_game *game, double client, bool first_time, bool is_bot);
void qa_qvm_game_client_begin(qa_qvm_game *game, double client);
void qa_qvm_game_client_userinfo_changed(qa_qvm_game *game, double client);
void qa_qvm_game_client_disconnect(qa_qvm_game *game, double client);
void qa_qvm_game_client_command(qa_qvm_game *game, double client, const char *const *arguments,
                                size_t argument_count);
void qa_qvm_game_client_think(qa_qvm_game *game, double client);
void qa_qvm_game_run_frame(qa_qvm_game *game, double time);
bool qa_qvm_game_console_command(qa_qvm_game *game, const char *const *arguments,
                                 size_t argument_count);
void qa_qvm_game_bot_frame(qa_qvm_game *game, double time);
/* Async TS variants are synchronous in C but keep distinct names. */
void qa_qvm_game_initialize_async(qa_qvm_game *game, double level_time, double random_seed,
                                  bool restart);
void qa_qvm_game_shutdown_async(qa_qvm_game *game, bool restart);
char *qa_qvm_game_client_connect_async(qa_qvm_game *game, double client, bool first_time,
                                       bool is_bot);
void qa_qvm_game_client_begin_async(qa_qvm_game *game, double client);
void qa_qvm_game_client_userinfo_changed_async(qa_qvm_game *game, double client);
void qa_qvm_game_client_disconnect_async(qa_qvm_game *game, double client);
void qa_qvm_game_client_command_async(qa_qvm_game *game, double client,
                                      const char *const *arguments, size_t argument_count);
void qa_qvm_game_client_think_async(qa_qvm_game *game, double client);
void qa_qvm_game_run_frame_async(qa_qvm_game *game, double time);
bool qa_qvm_game_console_command_async(qa_qvm_game *game, const char *const *arguments,
                                       size_t argument_count);
void qa_qvm_game_restart(qa_qvm_game *game, const uint8_t *bytes, size_t length);
void qa_qvm_game_retire(qa_qvm_game *game);

#endif
