#ifndef QA_FRONTEND_Q2_CLIENT_LERP_H
#define QA_FRONTEND_Q2_CLIENT_LERP_H

#include "qa/application_native_q2_client.h"
#include <string.h>

static inline float frontend_q2_lerp_angle(float a, float b, float fraction)
{
    float delta = fmodf(b - a, 360);
    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;
    return a + fraction * delta;
}
static inline qa_vec3 frontend_q2_lerp_angles(qa_vec3 a, qa_vec3 b, float fraction)
{
    return qa_v3(frontend_q2_lerp_angle(a.x,b.x,fraction),
        frontend_q2_lerp_angle(a.y,b.y,fraction),frontend_q2_lerp_angle(a.z,b.z,fraction));
}
static inline bool frontend_q2_lerp_near(qa_vec3 a, qa_vec3 b, float limit)
{
    return fabsf(a.x-b.x)<=limit && fabsf(a.y-b.y)<=limit && fabsf(a.z-b.z)<=limit;
}
static inline bool frontend_q2_lerp_live_angles(qa_q2_edition edition, int32_t type, uint32_t flags)
{
    return type < (edition==QA_Q2_CLASSIC ? 2 : 4) &&
        !(edition==QA_Q2_RERELEASE && (flags&256u));
}
static inline qa_vec3 frontend_q2_lerp_camera_angles(
    const qa_application_native_q2_player_sample *player, qa_vec3 predicted)
{
    qa_vec3 angles=frontend_q2_lerp_live_angles(player->edition,player->movement_type,player->movement_flags) ?
        predicted : player->view_angles;
    return qa_vec_add(angles,player->kick_angles);
}
static inline bool frontend_q2_lerp_entity_continuous(
    const qa_application_native_q2_entity_sample *previous,
    const qa_application_native_q2_entity_sample *current)
{
    return !memcmp(previous->models,current->models,sizeof(current->models)) &&
        frontend_q2_lerp_near(previous->origin,current->origin,512) &&
        current->event!=6 && current->event!=7;
}
static inline void frontend_q2_lerp_entity(
    const qa_application_native_q2_entity_sample *previous,
    const qa_application_native_q2_entity_sample *current, float fraction,
    qa_application_native_q2_entity_sample *out, uint32_t *old_frame)
{
    *out=*current;
    *old_frame=previous->frame;
    out->angles=frontend_q2_lerp_angles(previous->angles,current->angles,fraction);
    if (!(current->render_flags&(64u|128u))) {
        out->origin=qa_vec_lerp(previous->origin,current->origin,fraction);
        out->previous_origin=out->origin;
    }
}
static inline void frontend_q2_lerp_player(
    const qa_application_native_q2_player_sample *previous,
    const qa_application_native_q2_player_sample *current, float fraction,
    qa_application_native_q2_player_sample *out, uint32_t *old_gun_frame)
{
    *out=*current;
    out->origin=qa_vec_lerp(previous->origin,current->origin,fraction);
    out->view_angles=frontend_q2_lerp_angles(previous->view_angles,current->view_angles,fraction);
    out->view_offset=qa_vec_lerp(previous->view_offset,current->view_offset,fraction);
    out->kick_angles=frontend_q2_lerp_angles(previous->kick_angles,current->kick_angles,fraction);
    out->gun_angles=frontend_q2_lerp_angles(previous->gun_angles,current->gun_angles,fraction);
    out->gun_offset=qa_vec_lerp(previous->gun_offset,current->gun_offset,fraction);
    out->fov=previous->fov+fraction*(current->fov-previous->fov);
    *old_gun_frame=current->gun_frame && current->gun_model==previous->gun_model ? previous->gun_frame : current->gun_frame;
}

#endif
