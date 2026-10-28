/*
 * Port of src/compat/qvm/mod-input.ts. The user-command reader is
 * mirrored from src/compat/qvm/client-state-record.ts (parallel
 * slice). Applications/captures are pointer stacks; output() results
 * are malloc'd (caller frees).
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_MOD_INPUT_H
#define QA_COMPAT_QVM_MOD_INPUT_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/compat/qvm/module.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/qvm_mod_callbacks.h"
#include "qa/network/q3/message.h"
#include "qa/world/session/mod_clients.h"

qa_q3_wire_user_command qa_qvmy_read_user_command(const qa_qvm_writable_view *view, qa_qvm_abi_profile profile);

typedef struct qa_qvm_mod_input_operations {
  qa_qvm_module *module;
  qa_qvm_abi_profile abi_profile;
  const qa_qvm_mod_actor_record *records;
  size_t record_count;
  void *ctx;
  int64_t (*pointer)(void *ctx, qa_actor_id actor, const char *record);
  bool (*live)(void *ctx, qa_actor_id actor);
  void (*player_state)(void *ctx, qa_actor_id actor, qa_q3_player_state *out);
} qa_qvm_mod_input_operations;

typedef void (*qa_qvm_input_run_fn)(void *ctx);
typedef void (*qa_qvm_input_remove_fn)(void *ctx);

typedef struct qa_qvm_mod_input {
  qa_qvm_mod_input_operations operations;
  size_t *removal_ids;
  size_t removal_count;
  void **groups; /* owned declaration groups, parallel to removal_ids */
  size_t group_count;
  qa_mod_client_application **applications;
  size_t application_count;
  size_t application_capacity;
  void *captures; /* opaque stack */
  size_t capture_count;
  size_t capture_capacity;
} qa_qvm_mod_input;

void qa_qvm_mod_input_init(qa_qvm_mod_input *input, const qa_qvm_mod_input_operations *operations,
                           const qa_qvm_mod_input_output *outputs, size_t output_count);
void qa_qvm_mod_input_destroy(qa_qvm_mod_input *input);
void qa_qvm_mod_input_open(qa_qvm_mod_input *input, qa_mod_client_application *application,
                           qa_qvm_input_remove_fn *out_remove, void **out_remove_ctx);
/* Malloc'd output array; caller frees. */
qa_mod_client_input_output *qa_qvm_mod_input_run_output(qa_qvm_mod_input *input,
                                                    const qa_qvm_mod_input_output *outputs, size_t output_count,
                                                    qa_mod_client_application *application,
                                                    qa_qvm_input_run_fn run, void *run_ctx, size_t *out_count);
void qa_qvm_mod_input_close(qa_qvm_mod_input *input);

#endif
