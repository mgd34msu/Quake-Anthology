/* Port of src/compat/qc/source-call.ts. GPL-2.0-or-later.
 * TS Error throws abort via qa_range_error. Inputs map = array. */
#ifndef QA_COMPAT_QC_SOURCE_CALL_H
#define QA_COMPAT_QC_SOURCE_CALL_H
#include <stddef.h>
#include "qa/compat/qc/machine.h"
#include "qa/contracts/mod_callbacks.h"
typedef struct qa_compat_qc_mod_input_entry { qa_mod_callback_input name; qa_mod_runtime_value value; } qa_compat_qc_mod_input_entry;
typedef struct qa_compat_qc_actor_reference { void *ctx; int64_t (*fn)(void *ctx, const qa_actor_id *actor); } qa_compat_qc_actor_reference;
qa_compat_qc_value_type qa_compat_qc_source_value_type(qa_mod_callback_value value);
void qa_compat_qc_validate_source_call(const qa_compat_qc_program *program, qa_mod_source_call call, const qa_mod_callback_input *available, size_t available_count, const char *label);
void qa_compat_qc_write_source_value(qa_compat_qc_machine *m, qa_compat_qc_words *words, int64_t offset, qa_mod_runtime_value value, qa_compat_qc_actor_reference reference);
typedef struct qa_compat_qc_source_execute { void *ctx; double (*fn)(void *ctx, int64_t argument_count); } qa_compat_qc_source_execute;
double qa_compat_qc_with_source_call(qa_compat_qc_machine *m, qa_mod_source_call call, const qa_compat_qc_mod_input_entry *inputs, size_t input_count, qa_compat_qc_actor_reference reference, qa_compat_qc_source_execute execute);
#endif
