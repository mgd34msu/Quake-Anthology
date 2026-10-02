#include "entity_save.h"
#include <math.h>

static bool scalar(qa_source_save_io *io,float *v)
{ return qa_source_save_f32(io,v) && isfinite(*v); }
static bool vector(qa_source_save_io *io,qa_vec3 *v)
{ return qa_source_save_vec3(io,v) && qa_vec_finite(*v); }
static bool lerp(qa_source_save_io *io,q3n_lerp_frame *f)
{
    return qa_source_save_i32(io,&f->old_frame) && qa_source_save_i32(io,&f->old_frame_time) &&
        qa_source_save_i32(io,&f->frame) && qa_source_save_i32(io,&f->frame_time) && scalar(io,&f->back_lerp) &&
        qa_source_save_i32(io,&f->animation_number) && qa_source_save_i32(io,&f->animation_time) &&
        qa_source_save_bool(io,&f->selected);
}
static bool pose(qa_source_save_io *io,q3n_pose_frame *p)
{
    return lerp(io,&p->animation) && scalar(io,&p->yaw_angle) && scalar(io,&p->pitch_angle) &&
        qa_source_save_bool(io,&p->yawing) && qa_source_save_bool(io,&p->pitching);
}
bool q3n_entity_codec_ref(qa_source_save_io *io,q3n_entity *s,void *context,
    bool (*actor_fields)(void *,qa_source_save_io *,qa_actor_id *))
{
    return io && s && actor_fields && actor_fields(context,io,&s->actor) && qa_source_save_u32(io,&s->physical) &&
        qa_source_save_u64(io,&s->client_media_revision) && qa_source_save_bool(io,&s->valid) &&
        qa_source_save_bool(io,&s->event_only_fired) && qa_source_save_bool(io,&s->teleport_bit) &&
        qa_source_save_bool(io,&s->loop_stopped) && qa_source_save_i32(io,&s->previous_event) &&
        qa_source_save_i32(io,&s->snapshot_time) && qa_source_save_i32(io,&s->trail_time) &&
        qa_source_save_i32(io,&s->dust_trail_time) && qa_source_save_i32(io,&s->misc_time) &&
        qa_source_save_i32(io,&s->muzzle_flash_time) && vector(io,&s->lerp_origin) && vector(io,&s->lerp_angles) &&
        pose(io,&s->player.legs) && pose(io,&s->player.torso) && qa_source_save_i32(io,&s->player.pain_time) &&
        qa_source_save_bool(io,&s->player.pain_direction) && q3n_player_fx_codec(io,&s->player_fx) &&
        scalar(io,&s->barrel_angle) && qa_source_save_i32(io,&s->barrel_time) &&
        qa_source_save_bool(io,&s->barrel_spinning) && qa_source_save_bool(io,&s->lightning_firing) &&
        qa_source_save_bool(io,&s->railgun_flash) && vector(io,&s->rail_impact);
}
static bool actor_fields(void *context,qa_source_save_io *io,qa_actor_id *actor)
{ (void)context; return qa_source_save_actor(io,actor); }
bool q3n_entity_codec(qa_source_save_io *io,q3n_entity *s)
{ return q3n_entity_codec_ref(io,s,NULL,actor_fields); }
