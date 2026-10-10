#ifndef QA_FRONTEND_PARTICLE_DELIVERY_H
#define QA_FRONTEND_PARTICLE_DELIVERY_H
#include "qa/frontend.h"
#include "qa/scene.h"
#include "qa/application_native_q2_delivery.h"
#include "qa/network_q2_messages.h"
#include "qa/network_q1_nq.h"
/* seat is the physical submitter, independent of the authored scene view. */
bool frontend_particle_draw(qa_frontend *, uint32_t seat, const qa_scene_world_input *, qa_error *);
bool frontend_particle_world(qa_frontend *, uint32_t seat, qa_scene_world_input *, qa_error *);
bool frontend_particle_q2_temporary(qa_frontend *, const qa_application_protocol_event *,
    const qa_application_q2_audience *, const qa_q2_temp_entity *, const qa_actor_id *, qa_error *);
bool frontend_particle_q1_temporary(qa_frontend *, qa_actor_owner, qa_actor_id,
    const qa_q1_temp *, qa_actor_id beam_actor, bool quakeworld, qa_error *);
bool frontend_particle_visual_read(qa_frontend *, qa_actor_id,
    qa_application_visual_view *, bool *found, qa_error *);
bool frontend_particle_q2_entity(qa_frontend *, uint32_t seat, const qa_application_visual_view *,
    const qa_scene_world_input *, qa_scene_frame *, bool *beam, qa_error *);
bool frontend_particle_q1_entity(qa_frontend *, const qa_application_visual_view *,
    const qa_model *, qa_error *);
#endif
