#include "view_internal.h"

static bool fields(qa_source_save_io *io,q3n_view *o)
{
    uint8_t magic[4]={'Q','3','V','W'}; uint32_t version=1,product=(uint32_t)o->product,seat=o->options.seat;
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"Q3VW",4) || !qa_source_save_u32(io,&version) || version!=1 ||
       !qa_source_save_u32(io,&product) || product!=(uint32_t)o->product || !qa_source_save_u32(io,&seat) || seat!=o->options.seat)return false;
    q3n_view_state *v=&o->state;
    if(!qa_source_save_i32(io,&v->bob_cycle) || v->bob_cycle<0 || v->bob_cycle>1 ||
       !qa_source_save_i32(io,&v->next_orbit_time) || !qa_source_save_i32(io,&v->zoom_time) ||
       !qa_source_save_i32(io,&v->predicted_error_time) || !q3nh_float(io,&v->bob_fraction_sin) ||
       v->bob_fraction_sin<0 || v->bob_fraction_sin>1 || !q3nh_float(io,&v->xy_speed) || v->xy_speed<0 ||
       !q3nh_float(io,&v->zoom_sensitivity) || !q3nh_vector(io,&v->kick_angles) ||
       !q3nh_vector(io,&v->kick_origin) || !q3nh_vector(io,&v->predicted_error) ||
       !qa_source_save_bool(io,&v->zoomed) || !qa_source_save_bool(io,&v->hyperspace) ||
       !qa_source_save_bool(io,&v->test_gun) || !qa_source_save_bytes(io,o->test_model_name,sizeof(o->test_model_name)) ||
       !memchr(o->test_model_name,0,sizeof(o->test_model_name)))return false;
    qa_q3_ref_entity *r=&o->test_model;
    if(!qa_source_save_i32(io,&r->model) || !q3nh_handle(o->options.assets,r->model,Q3P_MODEL) ||
       !qa_source_save_i32(io,&r->flags) || !qa_source_save_i32(io,&r->frame) ||
       !qa_source_save_i32(io,&r->old_frame) || !qa_source_save_i32(io,&r->skin) ||
       !q3nh_float(io,&r->back_lerp) || !q3nh_vector(io,&r->origin))return false;
    for(unsigned i=0;i<3;++i)if(!q3nh_vector(io,&r->axis[i]))return false;
    return qa_source_save_bytes(io,r->color,sizeof(r->color));
}
bool q3n_view_checkpoint(const q3n_view *borrowed,qa_buffer *out,qa_error *e)
{
    if(!borrowed || !out || out->data || out->size || !q3nh_capture(borrowed->options.assets,borrowed->busy,e))return false;
    q3n_view *o=(q3n_view *)borrowed; o->busy=true; q3n_view copy=*o;
    qa_source_save_io io={0}; bool ok=qa_source_save_writer(&io,NULL,e) && fields(&io,&copy) && qa_source_save_finish(&io,out);
    if(!ok && e && e->code==QA_OK)q3ne_fail(e,QA_ERROR_FORMAT,"Native Q3 view continuation is inconsistent");
    qa_source_save_dispose(&io); o->busy=false; return ok;
}
bool q3n_view_restore(q3n_view *o,qa_bytes bytes,qa_error *e)
{
    if(!o || !q3nh_capture(o->options.assets,o->busy,e))return false;
    o->busy=true; q3n_view candidate={.options=o->options,.source_game=o->source_game,.product=o->product};
    qa_source_save_io io={0}; bool ok=qa_source_save_reader(&io,NULL,bytes,e) && fields(&io,&candidate) && qa_source_save_finish(&io,NULL);
    if(ok) { *o=candidate; o->busy=true; }
    else if(e && e->code==QA_OK)q3ne_fail(e,QA_ERROR_FORMAT,"Saved native Q3 view continuation is inconsistent");
    qa_source_save_dispose(&io); o->busy=false; return ok;
}
