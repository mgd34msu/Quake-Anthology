/* Port of src/compat/qc/mod-objectives.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QC_MOD_OBJECTIVES_H
#define QA_COMPAT_QC_MOD_OBJECTIVES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/machine.h"
#include "qa/contracts/mod_callbacks.h"
void qa_compat_qc_validate_objective_storage(const qa_compat_qc_program *program, qa_qc_mod_objective_storage storage, const char *type);
typedef struct qa_compat_qc_objective_current { void *ctx; void (*fn)(void *ctx, bool has_reference, int64_t reference); } qa_compat_qc_objective_current;
typedef struct qa_compat_qc_objective_resolved { qa_compat_qc_words *words; int64_t offset; bool has_reference; int64_t reference; } qa_compat_qc_objective_resolved;
qa_compat_qc_objective_resolved qa_compat_qc_resolve_objective_storage(qa_compat_qc_machine *m, qa_qc_mod_objective_storage storage, qa_compat_qc_objective_current current);
#endif
