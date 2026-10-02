#include "body.h"
#include "attachments.h"
#include "frame.h"

#include <string.h>

void q3n_player_reset(q3n_player_pose *state, qa_vec3 angles)
{
    memset(&state->legs, 0, sizeof(state->legs));
    memset(&state->torso, 0, sizeof(state->torso));
    state->legs.yaw_angle = state->torso.yaw_angle = angles.y;
    state->torso.pitch_angle = angles.x;
}

static bool received_current(const q3n_frame *frame,const q3n_remote_entity *source,const q3n_compiled_entity *compiled,
    const q3n_client_info *client,qa_error *error)
{
    if(!frame) return true;
    const q3n_client_info *live=q3n_clients_get(frame->clients,client->physical_client);
    if(q3n_frame_current(frame) && (compiled?q3n_compiled_entity_current(compiled):q3n_remote_entity_current(source)) &&
        q3n_clients_assets(frame->clients)==frame->assets &&
        (compiled?q3n_clients_compiled_current(frame->clients,&frame->compiled->source,error):
            q3n_clients_remote_current(frame->clients,&frame->remote->source,error)) && live &&
        live->configstring_revision==client->configstring_revision && live->media_revision==client->media_revision)
        return true;
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"Remote player body lost its reached entity or client media"); return false;
}
static bool build(qa_q3_presentation_assets *assets, q3n_player_pose *state,
    const q3n_client_info *client, const qa_q3_entity *source, qa_vec3 origin,
    qa_vec3 angles, const q3n_body_options *options, q3n_player_body *out,
    const q3n_frame *frame,const q3n_remote_entity *remote,const q3n_compiled_entity *compiled,qa_error *error)
{
    if (source->clientNum < 0 || source->clientNum >= (int32_t)QA_Q3_SOURCE_CLIENTS ||
        client->physical_client != (uint32_t)source->clientNum) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Bad clientNum on native Q3 player entity"); return false;
    }
    q3n_player_body body = {0};
    if (!client->info_valid || (source->number == options->local_view_client &&
        options->third_person && options->camera_mode)) { *out = body; return true; }
    q3n_pose_axes axes;
    if (!q3n_player_angles(state, &client->animations, source, angles, options->time,
        options->frame_milliseconds, options->swing_speed, &axes, error)) return false;
    for (unsigned i = 0; i < 3; ++i) {
        qa_q3_ref_entity *part = &body.parts[i];
        part->kind = QA_Q3_REF_MODEL; part->model = client->models[i]; part->custom_skin = client->skins[i];
        part->lighting_origin = origin; part->shadow_plane = options->shadow_plane;
        part->flags = 128;
        if (source->number == options->local_view_client && !options->third_person) part->flags |= 2;
        if (options->shadow_mode == 3 && options->shadow_visible) part->flags |= 256;
    }
    memcpy(body.parts[0].axis, axes.legs, sizeof(axes.legs));
    memcpy(body.parts[1].axis, axes.torso, sizeof(axes.torso));
    memcpy(body.parts[2].axis, axes.head, sizeof(axes.head));
    if (!options->no_player_animations) {
        float speed = ((uint32_t)source->powerups & (UINT32_C(1) << 3)) ? 1.5f : 1.0f;
        int32_t legs_animation = state->legs.yawing && (source->legsAnim & ~128) == 22 ? 24 : source->legsAnim;
        if (!q3n_lerp_run(&client->animations, &state->legs.animation, legs_animation,
            options->time, speed, options->animations_disabled, error) ||
            !q3n_lerp_run(&client->animations, &state->torso.animation, source->torsoAnim,
            options->time, speed, options->animations_disabled, error)) return false;
        body.parts[0].old_frame = state->legs.animation.old_frame;
        body.parts[0].frame = state->legs.animation.frame;
        body.parts[0].back_lerp = state->legs.animation.back_lerp;
        body.parts[1].old_frame = state->torso.animation.old_frame;
        body.parts[1].frame = state->torso.animation.frame;
        body.parts[1].back_lerp = state->torso.animation.back_lerp;
    }
    body.parts[0].origin = body.parts[0].old_origin = origin;
    body.count = 1;
    if (!client->models[0] || !client->models[1]) { *out = body; return true; }
    if (!q3n_attach(assets, &body.parts[1], &body.parts[0], "tag_torso", true, error) ||
        !received_current(frame,remote,compiled,client,error)) return false;
    body.count = 2;
    if (!client->models[2]) { *out = body; return true; }
    if (!q3n_attach(assets, &body.parts[2], &body.parts[1], "tag_head", true, error) ||
        !received_current(frame,remote,compiled,client,error)) return false;
    body.count = 3; *out = body; return true;
}

bool q3n_player_body_build(qa_q3_presentation_assets *assets,q3n_player_pose *state,
    const q3n_client_info *client,const qa_q3_entity *source,qa_vec3 origin,
    qa_vec3 angles,const q3n_body_options *options,q3n_player_body *out,qa_error *error)
{
    return build(assets,state,client,source,origin,angles,options,out,NULL,NULL,NULL,error);
}
bool q3n_player_body_build_remote(const q3n_frame *frame,const q3n_remote_entity *source,
    const q3n_client_info *client,const q3n_body_options *options,q3n_player_body *out,qa_error *error)
{
    if(!frame || !frame->remote || !source || source->frame!=frame->remote || !source->current ||
        !source->presentation || (!source->predicted && (!source->published || !source->current_valid)) || !client || !options || !out ||
        options->time!=frame->time || options->frame_milliseconds!=frame->frame_milliseconds ||
        !frame->assets || q3n_clients_get(frame->clients,client->physical_client)!=client ||
        !received_current(frame,source,NULL,client,error)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Remote body requires its actual reached player and frame"); return false;
    }
    q3n_client_info media=*client;
    q3n_player_pose pose=source->presentation->player;
    q3n_player_body body;
    if(!build(frame->assets,&pose,&media,source->current,source->presentation->lerp_origin,
        source->presentation->lerp_angles,options,&body,frame,source,NULL,error) ||
        !received_current(frame,source,NULL,&media,error)) return false;
    source->presentation->player=pose; *out=body; return true;
}
bool q3n_player_body_build_compiled(const q3n_frame *frame,const q3n_compiled_entity *source,
    const q3n_client_info *client,const q3n_body_options *options,q3n_player_body *out,qa_error *error)
{
    if (!frame || !frame->compiled || !source || source->frame!=frame->compiled || !source->current ||
        !source->presentation || (!source->predicted && (!source->published || !source->current_valid)) ||
        !client || !options || !out || options->time!=frame->time ||
        options->frame_milliseconds!=frame->frame_milliseconds ||
        q3n_clients_get(frame->clients,client->physical_client)!=client ||
        !received_current(frame,NULL,source,client,error)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Compiled body requires its actual full actor and client media"); return false;
    }
    q3n_client_info media=*client; q3n_player_pose pose=source->presentation->player; q3n_player_body body;
    if (!build(frame->assets,&pose,&media,source->current,source->presentation->lerp_origin,
        source->presentation->lerp_angles,options,&body,frame,NULL,source,error) ||
        !received_current(frame,NULL,source,&media,error)) return false;
    source->presentation->player=pose; *out=body; return true;
}
