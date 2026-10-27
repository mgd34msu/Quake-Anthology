/* Port of src/compat/qvm/body-scope.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_BODY_SCOPE_H
#define QA_COMPAT_QVM_BODY_SCOPE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/compat/qvm/image.h"
#include "qa/contracts/qvm_mod_presentation.h"

/* TS ReadonlyMap<number, QvmBodyPart> as a parallel pair list. */
typedef struct qa_qvm_body_call {
  double call;
  qa_qvm_body_part part;
} qa_qvm_body_call;

typedef struct qa_qvm_body_calls {
  qa_qvm_body_call *entries; /* owned */
  size_t count;
} qa_qvm_body_calls;

/* False only on OOM. Caller frees entries with qa_qvm_body_calls_destroy. */
bool qa_qvm_qualify_body_calls(const qa_qvm_image *image, qa_qvm_scene_body body,
                               qa_qvm_body_calls *out);
void qa_qvm_body_calls_destroy(qa_qvm_body_calls *calls);

#endif
