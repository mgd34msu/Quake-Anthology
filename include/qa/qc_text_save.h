#ifndef QA_QC_TEXT_SAVE_H
#define QA_QC_TEXT_SAVE_H
#include "qa/qc.h"
#include "qa/q1_save.h"

/* Capture reads the actual Source fields at a completed idle boundary.
 * Header fields remain the application's responsibility. Outputs must be
 * empty. Actual ABI free flags determine which physical rows are empty. */
bool qa_qc_text_capture(const qa_qc_instance *,qa_q1_save_record *,
                         qa_q1_save_record **,size_t *,qa_error *);
/* The host must first unlink the old candidate bodies, release displaced
 * dynamic actors, and bind every nonempty saved physical row to its actual
 * actor. All rows beyond the saved count must be free. This installs the
 * complete count before parsing forward entity references, clears saved raw
 * entity fields, and applies source-order pairs without guest/store callbacks.
 * Saved definitions and function names are qualified before import. Only an
 * isolated candidate may be imported: failure can leave partial guest state.
 * Host body linking and header publication follow this operation. */
bool qa_qc_text_program_ready(const qa_qc_program *,const qa_q1_save_data *,qa_error *);
bool qa_qc_text_import(qa_qc_instance *,const qa_q1_save_data *,qa_error *);
/* Read imported raw body fields before any borrowed-field projection can
 * replace them. This is pure; the host publishes the body and links it. */
bool qa_qc_text_body_read(const qa_qc_instance *,uint32_t slot,qa_body_state *,qa_error *);
#endif
