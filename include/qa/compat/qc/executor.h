/* Port of src/compat/qc/executor.ts. GPL-2.0-or-later.
 * Checkpoint produced by capture is caller-owned storage whose
 * byte views point into its malloc'd buffers (destroy frees).
 * Raw entity table views borrow machine entity bytes; the address
 * space token is a static cell per table storage. */
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
typedef struct qa_compat_qc_quakec_executor {
  qa_execution_profile profile; qa_compat_qc_machine *machine; qa_compat_qc_executor_host host;
  qa_guest_checkpoint checkpoint_storage; uint8_t *host_state_bytes; qa_compat_qc_machine_snapshot snapshot;
  uint8_t return_bytes[12];
} qa_compat_qc_quakec_executor;
/* Aborts (machine.fail) when profile disagrees. Profile must be the quakec variant. */
void qa_compat_qc_quakec_executor_init(qa_compat_qc_quakec_executor *e, qa_execution_profile profile, qa_compat_qc_machine *machine, qa_compat_qc_executor_host host);
void qa_compat_qc_quakec_executor_destroy(qa_compat_qc_quakec_executor *e);
qa_guest_call_result qa_compat_qc_quakec_executor_invoke(qa_compat_qc_quakec_executor *e, qa_guest_call_context context, const qa_guest_call_value *arguments, size_t argument_count);
/* Returned checkpoint borrows executor storage (valid until next checkpoint/destroy). */
const qa_guest_checkpoint *qa_compat_qc_quakec_executor_checkpoint(qa_compat_qc_quakec_executor *e);
void qa_compat_qc_quakec_executor_restore(qa_compat_qc_quakec_executor *e, const qa_guest_checkpoint *checkpoint);
bool qa_compat_qc_capture_checkpoint(qa_compat_qc_machine *machine, qa_module_identity module, qa_compat_qc_executor_host host_adapter, qa_guest_checkpoint *out, qa_compat_qc_machine_snapshot *out_snapshot, uint8_t **out_host_bytes);
bool qa_compat_qc_restore_checkpoint(qa_compat_qc_machine *machine, qa_module_identity module, qa_compat_qc_executor_host host_adapter, const qa_guest_checkpoint *checkpoint);
typedef struct qa_compat_qc_raw_table_state {
  qa_compat_qc_machine *machine; qa_module_identity module; qa_guest_layout layout;
  void *ctx; bool (*current_actor)(void *ctx, int64_t slot, qa_actor_id *out);
  char address_space_token; qa_raw_entity_view view_storage;
} qa_compat_qc_raw_table_state;
qa_raw_entity_table qa_compat_qc_create_raw_entity_table(qa_compat_qc_machine *machine, qa_module_identity module, qa_guest_layout layout, void *ctx, bool (*current_actor)(void *ctx, int64_t slot, qa_actor_id *out), qa_compat_qc_raw_table_state *storage);
#endif
