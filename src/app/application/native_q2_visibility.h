#ifndef APPLICATION_NATIVE_Q2_VISIBILITY_H
#define APPLICATION_NATIVE_Q2_VISIBILITY_H

#include "guest_native_q2_private.h"
#include "qa/network_q2_session.h"

typedef struct application_native_q2_visibility application_native_q2_visibility;

bool application_native_q2_visibility_complete(struct application_native_q2 *, qa_error *);
bool application_native_q2_visibility_read(struct application_native_q2 *, uint32_t,
    qa_actor_id, uint32_t, qa_actor_id, bool *, qa_error *);
bool application_native_q2_visibility_ready(struct application_native_q2 *,
    const qa_network_q2_player *, size_t, bool *, qa_error *);
bool application_native_q2_visibility_validate(struct application_native_q2 *, qa_error *);
void application_native_q2_visibility_invalidate(struct application_native_q2 *);
void application_native_q2_visibility_released(struct application_native_q2 *, qa_actor_id);
void application_native_q2_visibility_destroy(application_native_q2_visibility **);
bool application_native_q2_visibility_capture(struct application_native_q2 *, qa_buffer *, qa_error *);
bool application_native_q2_visibility_restore(struct application_native_q2 *, qa_bytes,
    const qa_source_frame *, const application_native_q2_client *, bool,
    application_native_q2_visibility **, qa_error *);

#endif
