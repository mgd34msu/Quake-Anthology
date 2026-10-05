#ifndef QA_Q3_NATIVE_PLAYER_STATE_INTERNAL_H
#define QA_Q3_NATIVE_PLAYER_STATE_INTERNAL_H

#include "player_state.h"
#include "events_internal.h"
#include "weapon.h"
#include "../q3/internal.h"

typedef struct q3n_transition_history {
    qa_actor_id viewing_actor, followed_actor;
    uint32_t viewing_client;
    uint64_t source_frame;
    int32_t source_time, client_num, damage_event, viewheight, external_event, e_flags;
    int32_t event_sequence, events[2], persistant[15], powerups[16], health;
    bool valid;
} q3n_transition_history;
struct q3n_player_state {
    q3n_player_state_options options;
    const qa_q3_game *source_game;
    qa_q3_product product;
    q3n_transition_history history;
    q3n_player_feedback feedback;
    int32_t event_sequence, predictable_events[16];
    bool map_restart, busy;
};
static inline int q3nh_weapons_stat(qa_q3_product p) { return p==QA_Q3_TEAM_ARENA?3:2; }
static inline int q3nh_armor_stat(qa_q3_product p) { return p==QA_Q3_TEAM_ARENA?4:3; }
static inline int q3nh_dead_yaw_stat(qa_q3_product p) { return p==QA_Q3_TEAM_ARENA?5:4; }
static inline float q3nh_clamp(float x,float low,float high) { return x<low?low:x>high?high:x; }
static inline void q3nh_vectors(qa_vec3 a,qa_vec3 *forward,qa_vec3 *right,qa_vec3 *up)
{
    float yaw=(a.y * (3.14159274101257324219f / 180));
    float pitch=(a.x * (3.14159274101257324219f / 180));
    float roll=(a.z * (3.14159274101257324219f / 180));
    float sy=(float)sin((double)yaw),cy=(float)cos((double)yaw);
    float sp=(float)sin((double)pitch),cp=(float)cos((double)pitch);
    float sr=(float)sin((double)roll),cr=(float)cos((double)roll);
    if(forward)*forward=qa_v3((cp * cy),(cp * sy),-sp);
    if(right)*right=qa_v3((-((sr * sp) * cy) + (cr * sy)),
        (-((sr * sp) * sy) + -(cr * cy)),-(sr * cp));
    if(up)*up=qa_v3((((cr * sp) * cy) + (sr * sy)),
        (((cr * sp) * sy) + -(sr * cy)),(cr * cp));
}
static inline void q3nh_axis(qa_vec3 a,qa_vec3 axis[3])
{ q3nh_vectors(a,&axis[0],&axis[1],&axis[2]); axis[1]=q3ne_scale(axis[1],-1); }
static inline bool q3nh_float(qa_source_save_io *io,float *v)
{ return qa_source_save_f32(io,v) && isfinite(*v); }
static inline bool q3nh_vector(qa_source_save_io *io,qa_vec3 *v)
{ return qa_source_save_vec3(io,v) && qa_vec_finite(*v); }
static inline bool q3nh_handle(const qa_q3_presentation_assets *a,int32_t h,q3p_resource_kind kind)
{
    if(h<0)return false;
    if(!h)return true;
    size_t i=(size_t)h-1;
    switch(kind) {
    case Q3P_MODEL:return i<a->model_count && a->models[i];
    case Q3P_SKIN:return i<a->skin_count && a->skins[i];
    case Q3P_SHADER:return i<a->shader_count && a->shaders[i];
    case Q3P_SOUND:return i<a->sound_count && a->sounds[i];
    }
    return false;
}
static inline bool q3nh_capture(const qa_q3_presentation_assets *a,bool busy,qa_error *e)
{ return a && !busy && a->capturing && a->busy==1 && !a->codec_busy?true:
    q3ne_fail(e,QA_ERROR_ARGUMENT,"Native Q3 private codec requires its actual asset capture lease"); }
static inline bool q3nh_remote_basis_fields(qa_source_save_io *io,
    const qa_native_q3_remote_client_service *client)
{
    if(!client)return true;
    qa_native_q3_remote_client_basis basis;
    if(!qa_native_q3_remote_client_basis_read(client,&basis,io->error))return false;
    uint64_t owner=basis.connection.owner,generation=basis.connection.generation,epoch=basis.epoch;
    uint64_t restart=basis.restart_generation,configuration=basis.configuration_generation;
    uint64_t receiver=basis.client.receiver,service=basis.client.service_owner,publication=basis.publication_generation;
    uint32_t slot=basis.connection.slot,physical=basis.physical_client;
    int32_t message=basis.initial_message,command=basis.initial_command;
    return qa_source_save_u64(io,&owner) && owner==basis.connection.owner &&
        qa_source_save_u64(io,&generation) && generation==basis.connection.generation &&
        qa_source_save_u32(io,&slot) && slot==basis.connection.slot &&
        qa_source_save_u64(io,&epoch) && epoch==basis.epoch &&
        qa_source_save_u64(io,&restart) && restart==basis.restart_generation &&
        qa_source_save_u64(io,&configuration) && configuration==basis.configuration_generation &&
        qa_source_save_u64(io,&publication) && publication==basis.publication_generation &&
        qa_source_save_u64(io,&receiver) && receiver==basis.client.receiver &&
        qa_source_save_u64(io,&service) && service==basis.client.service_owner &&
        qa_source_save_u32(io,&physical) && physical==basis.physical_client &&
        qa_source_save_i32(io,&message) && message==basis.initial_message &&
        qa_source_save_i32(io,&command) && command==basis.initial_command;
}
#endif
