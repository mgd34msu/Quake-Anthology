/* Port of src/compat/qc/mod-input.ts. GPL-2.0-or-later.
 * open() cleanup is an owned heap cell freed by calling it.
 * output results are malloc'd (caller frees array). */
#ifndef QA_COMPAT_QC_MOD_INPUT_H
#define QA_COMPAT_QC_MOD_INPUT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/machine.h"
#include "qa/world/session/mod_client_input_values.h"
typedef struct qa_compat_qc_mod_input_field { int64_t offset; int64_t words; qa_mod_actor_field declaration; } qa_compat_qc_mod_input_field;
typedef struct qa_compat_qc_mod_input_cleanup {
  void *ctx; void (*fn)(void *ctx);
} qa_compat_qc_mod_input_cleanup;
typedef struct qa_compat_qc_mod_input {
  qa_compat_qc_machine *machine;
  const qa_compat_qc_mod_input_field *fields; size_t field_count;
  void *ctx; int64_t (*reference)(void *ctx, qa_actor_id actor); bool (*live)(void *ctx, qa_actor_id actor);
  qa_mod_client_application **frames; size_t frame_count, frame_capacity;
  struct qa_compat_qc_mod_input_capture *captures; size_t capture_count, capture_capacity;
} qa_compat_qc_mod_input;
void qa_compat_qc_mod_input_init(qa_compat_qc_mod_input *in, qa_compat_qc_machine *machine, const qa_compat_qc_mod_input_field *fields, size_t field_count, void *ctx, int64_t (*reference)(void *, qa_actor_id), bool (*live)(void *, qa_actor_id));
void qa_compat_qc_mod_input_destroy(qa_compat_qc_mod_input *in);
qa_compat_qc_mod_input_cleanup qa_compat_qc_mod_input_open(qa_compat_qc_mod_input *in, qa_mod_client_application *application);
qa_mod_client_input_values qa_compat_qc_mod_input_values(qa_compat_qc_mod_input *in, qa_mod_client_application *application);
void qa_compat_qc_mod_input_observe_call(qa_compat_qc_mod_input *in, qa_compat_qc_call_site call);
qa_mod_client_input_output *qa_compat_qc_mod_input_output(qa_compat_qc_mod_input *in, const qa_mod_qc_input_output *outputs, size_t output_count, qa_mod_client_application *application, void *run_ctx, void (*run)(void *run_ctx), size_t *out_count);
bool qa_compat_qc_mod_input_get_active(const qa_compat_qc_mod_input *in);
#endif
