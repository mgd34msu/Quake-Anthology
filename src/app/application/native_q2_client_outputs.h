#ifndef APPLICATION_NATIVE_Q2_CLIENT_OUTPUTS_H
#define APPLICATION_NATIVE_Q2_CLIENT_OUTPUTS_H
#include "client_outputs.h"
#include "native_q2_callbacks.h"
struct application_native_q2_client_outputs;
struct application_provider;
bool application_native_q2_client_outputs_create(struct application_native_q2 *,
    struct application_native_q2_client_outputs **,qa_error *);
void application_native_q2_client_outputs_destroy(struct application_native_q2_client_outputs **);
bool application_native_q2_client_outputs_admit(struct application_native_q2 *,qa_actor_id,qa_error *);
bool application_native_q2_client_outputs_publish(struct application_native_q2 *,qa_error *);
void application_native_q2_client_outputs_release(struct application_native_q2 *,qa_actor_id);
bool application_native_q2_client_outputs_finish_restore(struct application_native_q2 *,qa_error *);
bool application_native_q2_client_outputs_capture(struct application_native_q2 *,qa_buffer *,qa_error *);
bool application_native_q2_client_outputs_restore(struct application_native_q2 *,qa_bytes,qa_error *);
uint8_t application_native_q2_client_outputs_claimed(const struct application_native_q2_client_outputs *);
bool application_native_q2_control_outputs(const qa_application *,qa_actor_id,application_client_outputs *,qa_error *);
bool application_client_output_claim_available(const struct application_provider *,uint8_t,qa_error *);
#endif
