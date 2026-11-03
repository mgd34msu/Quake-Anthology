/* Port of src/app/bootstrap/simulation/q3/server-traps.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/server_traps.h"
#include <string.h>
#include "qa/compat/qvm/abi.h"

void qa_app_q3_server_traps_init(qa_app_q3_server_traps *traps, qa_app_q3_server_traps_services services) {
  traps->services = services;
}

/* Without the bot library, the game's bot entry points see id's "used before being setup" answers. False = TS null. */
static bool disabled_bot(qa_app_q3_server_traps *traps, const qa_qvm_host_call *call, qa_qvm_host_result *out) {
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, "qagame") != 0) return false;
  const qa_app_q3_server_traps_services *s = &traps->services;
  switch (call->code) {
  case QA_QVM_GAME_IMPORT_BOTLIB_SETUP: *out = 0; return true;
  case QA_QVM_GAME_IMPORT_BOTLIB_AAS_INITIALIZED: *out = 0; return true;
  case QA_QVM_GAME_IMPORT_BOTLIB_SHUTDOWN: s->print(s->ctx, "BotLibShutdown: bot library used before being setup\n"); *out = 1; return true;
  case QA_QVM_GAME_IMPORT_BOTLIB_START_FRAME: s->print(s->ctx, "BotStartFrame: bot library used before being setup\n"); *out = 1; return true;
  case QA_QVM_GAME_IMPORT_BOTLIB_LOAD_MAP: s->print(s->ctx, "BotLoadMap: bot library used before being setup\n"); *out = 1; return true;
  case QA_QVM_GAME_IMPORT_BOTLIB_UPDATENTITY: s->print(s->ctx, "BotUpdateEntity: bot library used before being setup\n"); *out = 1; return true;
  case QA_QVM_GAME_IMPORT_BOTLIB_TEST: *out = 0; return true;
  default: return false;
  }
}

qa_qvm_host_result qa_app_q3_server_traps_dispatch(qa_app_q3_server_traps *traps, const qa_qvm_host_call *call) {
  const qa_app_q3_server_traps_services *s = &traps->services;
  qa_qvm_host_result result = 0;
  if (qa_qvm_common_syscall(call, *s->common, &result)) return result;
  if (qa_qvm_file_syscall(call, s->files, &result)) return result;
  if (qa_qvm_server_game_syscall(call, s->game, &result)) return result;
  if (s->bots != NULL && s->bots(s->ctx, call, &result)) return result;
  if (disabled_bot(traps, call, &result)) return result;
  qa_qvm_reject_syscall(call);
}
