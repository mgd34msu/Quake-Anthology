/* Port of src/compat/qvm/mod-objectives.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_MOD_OBJECTIVES_H
#define QA_COMPAT_QVM_MOD_OBJECTIVES_H

#include <stdint.h>

#include "qa/compat/qvm/memory.h"
#include "qa/contracts/qvm_mod_callbacks.h"

typedef void (*qa_qvm_objective_current_fn)(void *ctx);

void qa_qvm_validate_objective_address(int64_t end, const qa_qvm_mod_objective_address *address);
int64_t qa_qvm_resolve_objective_address(qa_qvm_memory *memory, int64_t end,
                                         const qa_qvm_mod_objective_address *address,
                                         qa_qvm_objective_current_fn current, void *current_ctx);

#endif
