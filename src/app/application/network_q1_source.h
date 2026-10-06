#ifndef APPLICATION_NETWORK_Q1_SOURCE_H
#define APPLICATION_NETWORK_Q1_SOURCE_H
#include "guest_qc_internal.h"

struct application_qc_state *application_network_q1_qc_observation(qa_application *,
    qa_actor_owner expected_owner, qa_error *);
struct application_qc_state *application_network_q1_qc_source(qa_application *,
    qa_actor_owner expected_owner, qa_error *);
bool application_network_q1_qc_client(struct application_qc_state *, qa_actor_id,
    uint32_t *, qa_error *);
bool application_network_q1_qc_entity(struct application_qc_state *, qa_actor_id,
    uint32_t *, int32_t *, qa_error *);
#endif
