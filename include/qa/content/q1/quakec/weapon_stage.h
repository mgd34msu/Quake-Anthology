/* Port of src/content/q1/quakec/weapon-stage.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_QUAKEC_WEAPON_STAGE_H
#define QA_CONTENT_Q1_QUAKEC_WEAPON_STAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q1/quakec/compat_types.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/qc_weapon_stage.h"

/* TS QcClientStageCall. call may borrow a declaration's arrays or
 * point at arg_storage for the synthesized one-input form. */
typedef struct qa_q1qc_client_stage_call {
  int64_t function_index;
  qa_mod_source_call call;
  qa_mod_callback_value arg_storage[1];
} qa_q1qc_client_stage_call;
/* TS RepeatGate. */
typedef struct qa_q1qc_repeat_gate {
  qa_q1qc_inline_region region;
  int64_t released;
  double value;
} qa_q1qc_repeat_gate;
typedef struct qa_q1qc_weapon_stage_client {
  qa_q1qc_client_stage_call spawn, select_spawn;
  const char *objectives_kind; /* "none" | "call" */
  qa_q1qc_client_stage_call objectives_call; /* kind "call" */
} qa_q1qc_weapon_stage_client;
/* TS QcWeaponStage (heap owned). */
typedef struct qa_q1qc_weapon_stage {
  int64_t dispatcher;
  bool has_client;
  qa_q1qc_weapon_stage_client client;
  int64_t *continuations; size_t continuation_count;
  qa_q1qc_repeat_gate *repeats; size_t repeat_count;
} qa_q1qc_weapon_stage;
bool qa_q1qc_weapon_stage_has_continuation(const qa_q1qc_weapon_stage *stage, int64_t function_index);
qa_q1qc_weapon_stage *qa_q1qc_derived_weapon_stage(const qa_q1qc_program *program);
qa_q1qc_weapon_stage *qa_q1qc_declared_weapon_stage(const qa_q1qc_program *program,
                                                    const qa_qc_weapon_stage_declaration *declared);
/* declared NULL = TS undefined. */
qa_q1qc_weapon_stage *qa_q1qc_weapon_stage_create(
    const qa_q1qc_program *program,
    const qa_qc_primary_weapon_stage_declaration *declared);
void qa_q1qc_weapon_stage_destroy(qa_q1qc_weapon_stage *stage);

double qa_q1qc_invoke_client_stage(qa_q1qc_machine *machine,
                                   const qa_q1qc_client_stage_call *stage,
                                   qa_actor_id actor, double time,
                                   qa_q1qc_actor_reference_fn reference, void *reference_ctx);
int64_t qa_q1qc_client_stage_self(qa_q1qc_machine *machine,
                                  const qa_q1qc_client_stage_call *stage);

/* TS QcWeaponStageBinding. */
typedef struct qa_q1qc_weapon_stage_binding {
  const qa_q1qc_weapon_stage *stage;
  qa_q1qc_machine_fn machine; void *machine_ctx;
  bool (*selected)(void *ctx, int64_t reference); void *selected_ctx;
} qa_q1qc_weapon_stage_binding;
bool qa_q1qc_weapon_stage_settled(const qa_q1qc_weapon_stage_binding *binding, int64_t reference);
/* TS composeFunctions / composeRegions results; storage owned here. */
typedef struct qa_q1qc_weapon_function_composed {
  const qa_q1qc_weapon_stage_binding *binding;
  qa_q1qc_function_boundary inner;
  int64_t *functions; size_t function_count;
  qa_q1qc_function_boundary boundary;
} qa_q1qc_weapon_function_composed;
void qa_q1qc_weapon_compose_functions(qa_q1qc_weapon_function_composed *composed,
                                      const qa_q1qc_weapon_stage_binding *binding,
                                      qa_q1qc_function_boundary inner);
void qa_q1qc_weapon_function_composed_destroy(qa_q1qc_weapon_function_composed *composed);
typedef struct qa_q1qc_weapon_region_composed {
  const qa_q1qc_weapon_stage_binding *binding;
  qa_q1qc_inline_boundary inner;
  qa_q1qc_inline_region *regions; size_t region_count;
  qa_q1qc_inline_boundary boundary;
} qa_q1qc_weapon_region_composed;
void qa_q1qc_weapon_compose_regions(qa_q1qc_weapon_region_composed *composed,
                                    const qa_q1qc_weapon_stage_binding *binding,
                                    qa_q1qc_inline_boundary inner);
void qa_q1qc_weapon_region_composed_destroy(qa_q1qc_weapon_region_composed *composed);

#endif
