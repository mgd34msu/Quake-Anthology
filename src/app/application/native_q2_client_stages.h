#ifndef APPLICATION_NATIVE_Q2_CLIENT_STAGES_H
#define APPLICATION_NATIVE_Q2_CLIENT_STAGES_H
#include "native_q2_callbacks.h"
#include "guest_q3_mod.h"
struct application_native_q2_stages;
struct application_native_q2_input;
bool application_native_q2_stages_prepare(struct application_native_q2 *,qa_error *);
bool application_native_q2_stages_advance(struct application_native_q2 *,const qa_source_frame *,qa_error *);
bool application_native_q2_stages_idle(const struct application_native_q2_stages *);
bool application_native_q2_stages_close(struct application_native_q2 *,qa_error *);
void application_native_q2_stages_released(struct application_native_q2 *,qa_actor_id);
uint64_t application_native_q2_stages_frame(const struct application_native_q2 *);
bool application_native_q2_stages_capture(struct application_native_q2 *,qa_buffer *,qa_error *);
bool application_native_q2_stages_restore(struct application_native_q2 *,qa_bytes,qa_error *);
bool application_native_q2_input_begin(struct application_native_q2 *,qa_actor_id,bool slice,
    bool (*values)(void *,application_q3_mod_inputs *,qa_error *),
    bool (*output)(void *,const application_q3_mod_output *,qa_error *),void *,
    struct application_native_q2_input **,qa_error *);
bool application_native_q2_input_complete(struct application_native_q2_input *,bool,qa_error *);
bool application_native_q2_input_abort(struct application_native_q2_input **,qa_error *);
#endif
