/*
 * Port of src/compat/qvm/mod-actor-frame.ts. The TS class name
 * collides with the landed contract type qa_qvm_mod_actor_frame (the
 * declaration), so the runtime class is qa_qvm_mod_actor_frame_runtime.
 * The image is the mirrored instruction list plus data extents from
 * qa_resolved_qvm_artifact fields passed explicitly.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_MOD_ACTOR_FRAME_H
#define QA_COMPAT_QVM_MOD_ACTOR_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/module.h"
#include "qa/contracts/qvm_mod_actor_frame.h"
#include "qa/contracts/qvm_mod_callbacks.h"

typedef struct qa_qvm_actor_store {
  int64_t address;
  int64_t value;
} qa_qvm_actor_store;

/* Fills out[0..store_count). */
void qa_qvm_actor_bootstrap(const int64_t *stores, size_t store_count,
                            const qa_qvm_instruction *instructions, size_t instruction_count,
                            int64_t initialized_data_length, int64_t data_length, int64_t bss_length,
                            const qa_qvm_mod_actor_record *records, size_t record_count,
                            qa_qvm_actor_store *out);
void qa_qvm_validate_mod_actor_frame(const qa_qvm_mod_actor_frame *definition,
                                     const qa_qvm_instruction *instructions, size_t instruction_count,
                                     int64_t initialized_data_length, int64_t bss_length,
                                     const qa_qvm_mod_actor_record *record, int64_t inuse);

typedef bool (*qa_qvm_actor_owned_fn)(void *ctx, int32_t pointer);
typedef void (*qa_qvm_actor_invoke_fn)(void *ctx);

typedef struct qa_qvm_mod_actor_frame_runtime {
  const qa_qvm_mod_actor_frame *definition;
  qa_qvm_module *module;
  qa_qvm_actor_owned_fn owned;
  void *owned_ctx;
  size_t removal_id;
  bool has_active;
  bool active_completed;
  int64_t *local_offsets; /* owned, per owned filter */
  size_t local_count;
  int64_t enter_operand;
} qa_qvm_mod_actor_frame_runtime;

void qa_qvm_mod_actor_frame_runtime_init(qa_qvm_mod_actor_frame_runtime *frame,
                                         const qa_qvm_mod_actor_frame *definition, qa_qvm_module *module,
                                         const qa_qvm_instruction *instructions, size_t instruction_count,
                                         qa_qvm_actor_owned_fn owned, void *owned_ctx);
bool qa_qvm_mod_actor_frame_runtime_run(qa_qvm_mod_actor_frame_runtime *frame,
                                        qa_qvm_actor_invoke_fn invoke, void *invoke_ctx);
void qa_qvm_mod_actor_frame_runtime_close(qa_qvm_mod_actor_frame_runtime *frame);
void qa_qvm_mod_actor_frame_runtime_destroy(qa_qvm_mod_actor_frame_runtime *frame);

#endif
