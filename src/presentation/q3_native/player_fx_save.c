#include "player_fx.h"

static bool finite_value(qa_source_save_io *io,float *v)
{ return qa_source_save_f32(io,v) && isfinite(*v); }
bool q3n_player_fx_codec(qa_source_save_io *io,q3n_player_fx_state *s)
{
    q3n_lerp_frame *f=&s->flag;
    if(!qa_source_save_i32(io,&f->old_frame) || !qa_source_save_i32(io,&f->old_frame_time) ||
        !qa_source_save_i32(io,&f->frame) || !qa_source_save_i32(io,&f->frame_time) ||
        !finite_value(io,&f->back_lerp) || !qa_source_save_i32(io,&f->animation_number) ||
        !qa_source_save_i32(io,&f->animation_time) || !qa_source_save_bool(io,&f->selected) ||
        (f->selected && f->animation_number!=34 && f->animation_number!=35) ||
        !finite_value(io,&s->flag_yaw) || !qa_source_save_bool(io,&s->flag_yawing) ||
        !qa_source_save_u32(io,&s->skull_count) || s->skull_count>10)return false;
    /* Even inactive positions are continuation data, retained exactly. */
    for(unsigned i=0;i<10;++i)if(!qa_source_save_vec3(io,&s->skull_positions[i]) || !qa_vec_finite(s->skull_positions[i]))return false;
    return true;
}
