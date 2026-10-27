/* Port of src/compat/qvm/game-input.ts. The TS synchronous/asynchronous
 * apply paths collapse to the synchronous C path (docs/PORTING.md), so
 * the "cannot await disconnect" guard has no C counterpart.
 * GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_GAME_INPUT_H
#define QA_COMPAT_QVM_GAME_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/game.h"
#include "qa/compat/qvm/module.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/mod_client_outputs.h"
#include "qa/contracts/time.h"
#include "qa/world/session/mod_client_applications.h"
#include "qa/world/session/mod_clients.h"

typedef struct qa_qvm_input_definition {
  qa_module_identity module;
  double entity_stride, client_stride, client_pointer;
  const double *intermission;
  size_t intermission_count;
  bool has_movement_modes;
  double movement_mode_normal, movement_mode_noclip, movement_mode_freeze;
  double entry_client_think, entry_run_client, entry_client_spawn, entry_move, entry_slice;
} qa_qvm_input_definition;

/* TS QvmInputSource. The run closures become fn + ctx. */
typedef int32_t (*qa_qvm_input_run_fn)(void *ctx);
typedef struct qa_qvm_input_source {
  qa_qvm_game *game;
  qa_qvm_input_definition definition;
  void *ctx;
  void (*retiring)(void *ctx, double slot, qa_mod_client_identity identity);
  bool (*retired)(void *ctx, double slot);
  int32_t (*disconnect)(void *ctx, double slot, qa_mod_client_identity identity,
                        qa_qvmy_function_call *call);
  int32_t (*movement)(void *ctx, double slot, qa_qvm_input_run_fn run, void *run_ctx);
} qa_qvm_input_source;

/* TS QvmInputServices. client_outputs/spawned/movement may be NULL (TS
 * optional); client_outputs returning false is the TS null result. */
typedef struct qa_qvm_input_services {
  void *ctx;
  qa_mod_client_applications *applications;
  bool (*client_outputs)(void *ctx, qa_actor_id actor, qa_mod_client_movement_outputs *out);
  bool (*identity)(void *ctx, double slot, qa_mod_client_identity *out);
  bool (*live)(void *ctx, qa_mod_client_identity identity);
  const qa_mod_client_command_receipt *(*accepted)(void *ctx, qa_actor_id actor);
  qa_frame_context (*frame)(void *ctx);
  void (*spawned)(void *ctx, qa_mod_client_identity identity);
  int32_t (*movement)(void *ctx, qa_qvmy_function_call *call, const char *kind,
                      qa_qvm_input_run_fn run, void *run_ctx);
  qa_mod_client_unsubscribe (*on_release)(void *ctx, void (*listener)(void *ctx, qa_actor_id actor),
                                          void *listener_ctx);
} qa_qvm_input_services;

typedef struct qa_qvm_input_binding qa_qvm_input_binding;

/* NULL only on OOM. `source` is copied; its game pointer is borrowed. */
qa_qvm_input_binding *qa_qvm_input_binding_create(qa_qvm_input_source source,
                                                  qa_qvm_input_services services);
void qa_qvm_input_binding_destroy(qa_qvm_input_binding *binding);
void qa_qvm_input_binding_close(qa_qvm_input_binding *binding);

#endif
