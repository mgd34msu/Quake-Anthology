/* Port of src/compat/qc/executor.ts. GPL-2.0-or-later.
 * Checkpoint produced by capture is caller-owned storage whose
 * byte views point into its malloc'd buffers (destroy frees).
 * Raw entity table views borrow machine entity bytes; the address
 * space token is a cell per table storage. The TS `count` getter is
 * table.live_count(table.ctx); table.count is the count at creation. */
#ifndef QA_COMPAT_QC_EXECUTOR_H
#define QA_COMPAT_QC_EXECUTOR_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/machine.h"
#include "qa/contracts/execution.h"
typedef struct qa_compat_qc_host_saved_state {
  qa_guest_private_state state;
  const qa_random_state *random; size_t random_count;
  const qa_saved_guest_callback_binding *callbacks; size_t callback_count;
} qa_compat_qc_host_saved_state;
typedef struct qa_compat_qc_executor_host {
  void *ctx;
  qa_compat_qc_host_saved_state (*checkpoint)(void *ctx);
  void (*restore)(void *ctx, qa_compat_qc_host_saved_state saved);
} qa_compat_qc_executor_host;
bool qa_compat_qc_capture_checkpoint(qa_compat_qc_machine *machine, qa_module_identity module, qa_compat_qc_executor_host host_adapter, qa_guest_checkpoint *out, qa_compat_qc_machine_snapshot *out_snapshot, uint8_t **out_host_bytes);
/* TS preflightQcCheckpoint: validates and decodes a checkpoint without touching the machine or host. */
void qa_compat_qc_preflight_checkpoint(qa_compat_qc_machine *machine, qa_module_identity module, const qa_guest_checkpoint *checkpoint);
bool qa_compat_qc_restore_checkpoint(qa_compat_qc_machine *machine, qa_module_identity module, qa_compat_qc_executor_host host_adapter, const qa_guest_checkpoint *checkpoint);
typedef struct qa_compat_qc_raw_table_state {
  qa_compat_qc_machine *machine; qa_module_identity module; qa_guest_layout layout;
  void *ctx; bool (*current_actor)(void *ctx, int64_t slot, qa_actor_id *out);
  char address_space_token;
  struct qa_compat_qc_raw_view_context *views; /* one per entity capacity, owned */
} qa_compat_qc_raw_table_state;
/* Frees the per-slot view contexts; views from this table die with it. */
void qa_compat_qc_raw_table_state_destroy(qa_compat_qc_raw_table_state *storage);
qa_raw_entity_table qa_compat_qc_create_raw_entity_table(qa_compat_qc_machine *machine, qa_module_identity module, qa_guest_layout layout, void *ctx, bool (*current_actor)(void *ctx, int64_t slot, qa_actor_id *out), qa_compat_qc_raw_table_state *storage);
#endif
