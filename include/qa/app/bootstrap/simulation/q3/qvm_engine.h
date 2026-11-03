/* Port of src/app/bootstrap/simulation/q3/qvm-engine.ts: Q3QvmEngine, an
 * interpreted qagame. Every trap reaches the shared trap table, and a trap may
 * enter the module again through the suspended interpreter. The options are
 * TS Pick<QvmModuleOptions, "artifact" | "semantics" | "hostState"> plus the
 * traps and assertCurrent closures (fn + ctx). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_QVM_ENGINE_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_QVM_ENGINE_H
#include "qa/app/bootstrap/simulation/q3/engine.h"
#include "qa/compat/qvm/game.h"
#include "qa/compat/qvm/module.h"

typedef struct qa_app_q3_qvm_engine_options {
  const qa_resolved_qvm_artifact *artifact; /* borrowed; outlives the engine */
  const char *semantics;
  const qa_qvm_host_state *host_state; /* borrowed */
  void *ctx;
  qa_qvm_host_result (*traps)(void *ctx, const qa_qvm_host_call *call);
  void (*assert_current)(void *ctx);
} qa_app_q3_qvm_engine_options;

/* NULL on allocation failure; TS constructor throws raise. */
qa_app_q3_game_engine *qa_app_q3_qvm_engine_create(qa_app_q3_qvm_engine_options options);
/* TS Q3QvmEngine.game; NULL when engine is not an interpreted module. */
qa_qvm_game *qa_app_q3_qvm_engine_game(qa_app_q3_game_engine *engine);
#endif
