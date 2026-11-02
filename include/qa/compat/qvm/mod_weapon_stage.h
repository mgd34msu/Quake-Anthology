/*
 * Port of src/compat/qvm/mod-weapon-stage.ts. The dispatcher
 * definition aliases the landed contract weapon-stage struct (its
 * continuation field is unused by the dispatcher, as the TS Pick).
 * Cleanup stacks become removal-id arrays; open()/prepare cleanups
 * are fn + ctx pairs.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_MOD_WEAPON_STAGE_H
#define QA_COMPAT_QVM_MOD_WEAPON_STAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/module.h"
#include "qa/contracts/qvm_mod_items.h"
#include "qa/world/session/mod_clients.h"

/* TS QvmWeaponDispatcherDefinition. */
typedef qa_qvm_weapon_stage qa_qvm_weapon_dispatcher_definition;

void qa_qvm_validate_weapon_dispatcher(const qa_qvm_weapon_stage *stage,
                                       const qa_qvm_instruction *instructions, size_t instruction_count);
void qa_qvm_validate_weapon_stage(const qa_qvm_weapon_stage *stage,
                                  const qa_qvm_instruction *instructions, size_t instruction_count);

typedef struct qa_qvm_posture {
  qa_bounds bounds;
  int32_t view_height;
  int32_t ground;
} qa_qvm_posture;

/* TS QvmWeaponDispatcherOperations. */
typedef struct qa_qvm_weapon_dispatcher_ops {
  qa_qvm_module *module;
  void *ctx;
  bool (*actor)(void *ctx, const qa_qvm_weapon_actor *source, qa_qvmy_function_call *call, qa_actor_id *out);
  int64_t (*pointer)(void *ctx, qa_actor_id actor, const char *record);
  bool (*live)(void *ctx, qa_actor_id actor);
  bool (*selected)(void *ctx, qa_actor_id actor);
  qa_qvm_cancellation_scope (*cancellation)(void *ctx, qa_actor_id actor, qa_qvmy_function_call *call);
  void (*attempted)(void *ctx, qa_actor_id actor, int32_t value);
  void (*accepted)(void *ctx, qa_actor_id actor, int32_t value);
  void (*completed)(void *ctx, qa_actor_id actor, bool reached_attack_decision);
  /* Optional; returns an optional cleanup through the out params. */
  void (*prepare)(void *ctx, qa_actor_id actor, qa_qvmy_function_call *call,
                  void (**cleanup)(void *cleanup_ctx), void **cleanup_ctx);
} qa_qvm_weapon_dispatcher_ops;

typedef struct qa_qvm_weapon_dispatcher_runtime {
  const qa_qvm_weapon_stage *definition;
  qa_qvm_weapon_dispatcher_ops operations;
  size_t removal_ids[2];
  void *dispatchers;  /* opaque stack */
  size_t dispatcher_count;
  size_t dispatcher_capacity;
  void *evaluations;  /* opaque stack */
  size_t evaluation_count;
  size_t evaluation_capacity;
} qa_qvm_weapon_dispatcher_runtime;

void qa_qvm_weapon_dispatcher_runtime_init(qa_qvm_weapon_dispatcher_runtime *dispatcher,
                                   const qa_qvm_weapon_stage *definition,
                                   const qa_qvm_weapon_dispatcher_ops *operations);
void qa_qvm_weapon_dispatcher_runtime_destroy(qa_qvm_weapon_dispatcher_runtime *dispatcher);
bool qa_qvm_weapon_dispatcher_runtime_settled(qa_qvm_weapon_dispatcher_runtime *dispatcher, qa_actor_id actor);
/* Returns the item or NULL (TS null when selected == 0). */
qa_item_id qa_qvm_weapon_dispatcher_runtime_active(qa_qvm_weapon_dispatcher_runtime *dispatcher, qa_actor_id actor);
bool qa_qvm_weapon_dispatcher_runtime_matches(qa_qvm_weapon_dispatcher_runtime *dispatcher, qa_actor_id actor,
                                      const qa_qvm_item_test *tests, size_t test_count);
int32_t qa_qvm_weapon_dispatcher_runtime_evaluate(qa_qvm_weapon_dispatcher_runtime *dispatcher, qa_actor_id actor,
                                          const qa_qvm_region_evaluation *region,
                                          const int64_t *inputs, size_t input_count);
void qa_qvm_weapon_dispatcher_runtime_close(qa_qvm_weapon_dispatcher_runtime *dispatcher);

/* TS Operations of QvmModWeaponStage. */
typedef struct qa_qvm_mod_weapon_stage_operations {
  qa_qvm_module *module;
  void *ctx;
  int64_t (*pointer)(void *ctx, qa_actor_id actor, const char *record);
  bool (*live)(void *ctx, qa_actor_id actor);
  bool (*selected)(void *ctx, qa_actor_id actor);
  int32_t (*invoke)(void *ctx, qa_actor_id actor, const qa_qvm_mod_source_call *call);
  void (*attempted)(void *ctx, qa_actor_id actor, int32_t value);
  void (*accepted)(void *ctx, qa_actor_id actor, int32_t value);
  void (*completed)(void *ctx, qa_actor_id actor);
  qa_qvm_posture (*posture)(void *ctx, qa_actor_id actor);
} qa_qvm_mod_weapon_stage_operations;

typedef int32_t (*qa_qvm_weapon_run_fn)(void *ctx);

typedef struct qa_qvm_mod_weapon_stage {
  const qa_qvm_weapon_stage *definition;
  qa_qvm_mod_weapon_stage_operations operations;
  qa_qvm_weapon_dispatcher_runtime dispatcher;
  size_t removal_ids[2];
  qa_mod_client_application **applications;
  size_t application_count;
  size_t application_capacity;
  void *inputs; /* opaque stack */
  size_t input_count;
  size_t input_capacity;
} qa_qvm_mod_weapon_stage;

void qa_qvm_mod_weapon_stage_init(qa_qvm_mod_weapon_stage *stage, const qa_qvm_weapon_stage *definition,
                                  int64_t input_entry, const qa_qvm_mod_weapon_stage_operations *operations);
void qa_qvm_mod_weapon_stage_destroy(qa_qvm_mod_weapon_stage *stage);
/* Returns a removal closure (fn + ctx) through the out params. */
void qa_qvm_mod_weapon_stage_open(qa_qvm_mod_weapon_stage *stage, qa_mod_client_application *application,
                                  void (**remove)(void *ctx), void **remove_ctx);
int32_t qa_qvm_mod_weapon_stage_apply(qa_qvm_mod_weapon_stage *stage, qa_actor_id actor,
                                      qa_qvm_weapon_run_fn run, void *run_ctx);
void qa_qvm_mod_weapon_stage_cancel_retired(qa_qvm_mod_weapon_stage *stage, qa_actor_id actor);
bool qa_qvm_mod_weapon_stage_settled(qa_qvm_mod_weapon_stage *stage, qa_actor_id actor);
qa_item_id qa_qvm_mod_weapon_stage_active(qa_qvm_mod_weapon_stage *stage, qa_actor_id actor);
void qa_qvm_mod_weapon_stage_close(qa_qvm_mod_weapon_stage *stage);

#endif
