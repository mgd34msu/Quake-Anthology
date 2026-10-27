/* Port of src/content/q1/quakec/pickup-callers.ts. Reader results
 * borrow strings from the save tree; nested arrays and the resource/
 * scalar boxes are malloc'd, released by the destroy below.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_QUAKEC_PICKUP_CALLERS_H
#define QA_CONTENT_Q1_QUAKEC_PICKUP_CALLERS_H

#include <stddef.h>

#include "qa/content/q1/quakec/pickup_stage.h"
#include "qa/contracts/qc_pickup_callers.h"
#include "qa/persistence/value.h"

qa_qc_pickup_caller_declaration qa_q1qc_read_pickup_caller(const qa_save_reader *reader);
void qa_q1qc_pickup_caller_declaration_destroy(qa_qc_pickup_caller_declaration *declaration);
/* TS qcDeclaredPickupStages: malloc'd stage array. */
qa_q1qc_pickup_stage *qa_q1qc_declared_pickup_stages(
    const qa_q1qc_program *program,
    const qa_qc_pickup_caller_declaration *declarations, size_t declaration_count,
    size_t *out_count);

#endif
