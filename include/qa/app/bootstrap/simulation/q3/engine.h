/* Port of src/app/bootstrap/simulation/q3/engine.ts: Q3GameEngine, the game
 * module as the Q3 server game drives it (id's GAME_* entry points over an
 * interpreted QVM or a native process image), and Q3GuestCheckpoint (a qvm or
 * native-guest qa_guest_checkpoint). The TS interface becomes a function
 * table plus self; every async entry point is synchronous in C.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_ENGINE_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_ENGINE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qvm/game_data.h"
#include "qa/contracts/execution.h"

typedef struct qa_app_q3_game_engine qa_app_q3_game_engine;
typedef struct qa_app_q3_game_engine_ops {
  /* TS active: a call into the module is running or waiting on a host operation. */
  bool (*active)(qa_app_q3_game_engine *engine);
  void (*initialize)(qa_app_q3_game_engine *engine, double level_time, double random_seed, bool restart);
  void (*shutdown)(qa_app_q3_game_engine *engine, bool restart);
  /* The denial message (malloc'd, caller frees) or NULL (TS null). */
  char *(*client_connect)(qa_app_q3_game_engine *engine, double client, bool first_time, bool is_bot);
  void (*client_begin)(qa_app_q3_game_engine *engine, double client);
  void (*client_userinfo_changed)(qa_app_q3_game_engine *engine, double client);
  /* From inside a trap this enters the module again, as SV_DropClient does. */
  void (*client_disconnect)(qa_app_q3_game_engine *engine, double client);
  void (*client_command_async)(qa_app_q3_game_engine *engine, double client, const char *const *argv, size_t argc);
  void (*client_command)(qa_app_q3_game_engine *engine, double client, const char *const *argv, size_t argc);
  void (*client_think)(qa_app_q3_game_engine *engine, double client);
  void (*run_frame)(qa_app_q3_game_engine *engine, double time);
  bool (*console_command)(qa_app_q3_game_engine *engine, const char *const *argv, size_t argc);
  void (*bot_frame)(qa_app_q3_game_engine *engine, double time);
  /* Owned by the engine; valid until the next checkpoint or destroy. */
  const qa_guest_checkpoint *(*checkpoint)(qa_app_q3_game_engine *engine);
  void (*restore)(qa_app_q3_game_engine *engine, const qa_guest_checkpoint *checkpoint);
  /* Releases the module without running its shutdown entry point. */
  void (*retire)(qa_app_q3_game_engine *engine);
  /* C only: frees the engine (after retire). */
  void (*destroy)(qa_app_q3_game_engine *engine);
} qa_app_q3_game_engine_ops;

struct qa_app_q3_game_engine {
  const qa_app_q3_game_engine_ops *ops;
  const char *kind; /* "qvm" | "native" */
  qa_module_identity module; /* borrowed from the engine's module */
  qa_qvm_game_data *data;    /* the located entity and player tables */
};

/* TS q3GuestHostState: interpreted checkpoints keep the server game's record as
 * host state, native ones as runtime state. */
static inline const qa_guest_private_state *qa_app_q3_guest_host_state(const qa_guest_checkpoint *checkpoint) {
  return checkpoint->kind == QA_GUEST_CHECKPOINT_QVM ? &checkpoint->host_state : &checkpoint->runtime_state;
}
#endif
