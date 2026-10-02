#ifndef QA_FRONTEND_PARTICLE_DELIVERY_H
#define QA_FRONTEND_PARTICLE_DELIVERY_H
#include "qa/frontend.h"
#include "qa/scene.h"
#include "qa/application_native_q2_delivery.h"
#include "qa/network_q2_messages.h"
/* seat is the physical submitter, independent of the authored scene view. */
bool frontend_particle_draw(qa_frontend *, uint32_t seat, const qa_scene_view *, qa_error *);
bool frontend_particle_world(qa_frontend *, uint32_t seat, qa_scene_world_input *, qa_error *);
bool frontend_particle_q2_temporary(qa_frontend *, const qa_application_protocol_event *,
    const qa_application_q2_audience *, const qa_q2_temp_entity *, qa_error *);
#endif
