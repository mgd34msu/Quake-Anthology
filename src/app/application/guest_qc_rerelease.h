#ifndef APPLICATION_GUEST_QC_RERELEASE_H
#define APPLICATION_GUEST_QC_RERELEASE_H
#include "guest_qc_internal.h"
bool application_qc_rerelease_import(struct application_qc_state *,qa_qc_instance *,qa_qc_builtin,qa_error *);
void application_qc_rerelease_reset(struct application_qc_state *);
void application_qc_rerelease_destroy(struct application_qc_state *);
bool application_qc_rerelease_checkpoint(struct application_qc_state *,qa_buffer *,qa_error *);
bool application_qc_rerelease_restore(struct application_qc_state *,qa_bytes,qa_error *);
bool application_qc_rerelease_command(application_provider *,qa_actor_id,qa_movement_command *,qa_error *);
bool application_qc_rerelease_frame(struct application_qc_state *,qa_error *);
void application_qc_rerelease_released(struct application_qc_state *,qa_actor_id);
#endif
