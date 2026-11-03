/* Port of src/app/bootstrap/simulation/mod-callbacks.ts. GPL-2.0-or-later.
 * TS registerModCallbacks is generic over Callback extends
 * ModCallbackBinding: callbacks is an element array of `stride` bytes
 * whose qa_mod_callback_binding sits at `binding_offset` (for
 * qa_mod_callback use offsetof(qa_mod_callback, binding)); execute
 * receives the element pointer. The closures TS captures live in one
 * block per callback, freed when registrations->resources closes. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_MOD_CALLBACKS_H
#define QA_APP_BOOTSTRAP_SIMULATION_MOD_CALLBACKS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/mod_callbacks.h"
#include "qa/contracts/time.h"
#include "qa/world/session/mods.h"
double qa_app_sim_mod_callback_seconds(qa_source_time time);
/* TS ReadonlyMap<ModCallbackInput, ModRuntimeValue>: insertion order
 * with Map.set semantics (a repeated key keeps its first position).
 * Actor values point into `actors`; pass the map by pointer only. */
#define QA_APP_SIM_MOD_CALLBACK_INPUT_CAPACITY 12
typedef struct qa_app_sim_mod_callback_inputs {
  size_t count;
  qa_mod_callback_input names[QA_APP_SIM_MOD_CALLBACK_INPUT_CAPACITY];
  qa_mod_runtime_value values[QA_APP_SIM_MOD_CALLBACK_INPUT_CAPACITY];
  qa_actor_id actors[QA_APP_SIM_MOD_CALLBACK_INPUT_CAPACITY];
} qa_app_sim_mod_callback_inputs;
/* Map.get: NULL is TS undefined. */
const qa_mod_runtime_value *qa_app_sim_mod_callback_inputs_get(const qa_app_sim_mod_callback_inputs *inputs,
                                                              const char *name);
typedef struct qa_app_sim_mod_callback_host {
  void *ctx;
  qa_source_time (*time)(void *ctx);
  /* TS execute: false is the TS null result. */
  bool (*execute)(void *ctx, const void *callback, const qa_app_sim_mod_callback_inputs *inputs, double *out);
} qa_app_sim_mod_callback_host;
void qa_app_sim_register_mod_callbacks(const void *callbacks, size_t count, size_t stride, size_t binding_offset,
                                       qa_mod_registrations *registrations, qa_app_sim_mod_callback_host host);
#endif
