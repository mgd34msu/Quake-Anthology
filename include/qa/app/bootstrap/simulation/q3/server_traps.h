/* Port of src/app/bootstrap/simulation/q3/server-traps.ts: Q3ServerTraps,
 * sv_game.c's SV_GameSystemCalls for either execution form. An interpreted
 * and a native module issue the same trap numbers with the same argument
 * words, so one table serves both; memory access goes through the call's own
 * pointer space. Q3BotTraps is the bots(ctx) member: false (TS null) when the
 * bot library is not attached. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_SERVER_TRAPS_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_SERVER_TRAPS_H
#include "qa/compat/qvm/common_syscalls.h"
#include "qa/compat/qvm/file_syscalls.h"
#include "qa/compat/qvm/server_game_syscalls.h"
#include "qa/compat/qvm/syscalls.h"

typedef struct qa_app_q3_server_traps_services {
  const qa_qvm_common_services *common; /* role "qagame"; borrowed */
  qa_qvm_files *files;                  /* borrowed */
  const qa_qvm_server_game_services *game; /* borrowed */
  void *ctx;
  /* TS bots()?.syscall(call): false when there is no bot library or it does not serve the call. */
  bool (*bots)(void *ctx, const qa_qvm_host_call *call, qa_qvm_host_result *out);
  void (*print)(void *ctx, const char *text);
} qa_app_q3_server_traps_services;

typedef struct qa_app_q3_server_traps { qa_app_q3_server_traps_services services; } qa_app_q3_server_traps;

void qa_app_q3_server_traps_init(qa_app_q3_server_traps *traps, qa_app_q3_server_traps_services services);
/* dispatch(call): common, file, server-game, bot library, disabled bot, then reject (raises). */
qa_qvm_host_result qa_app_q3_server_traps_dispatch(qa_app_q3_server_traps *traps, const qa_qvm_host_call *call);
#endif
