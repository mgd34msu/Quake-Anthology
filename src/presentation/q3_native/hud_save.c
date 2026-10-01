#include "hud_internal.h"

static bool fields(qa_source_save_io *io,q3n_hud *o)
{
    uint8_t magic[4]={'Q','3','H','D'}; uint32_t version=1,product=(uint32_t)o->product,seat=o->options.seat;
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"Q3HD",4) || !qa_source_save_u32(io,&version) || version!=1 ||
       !qa_source_save_u32(io,&product) || product!=(uint32_t)o->product || !qa_source_save_u32(io,&seat) || seat!=o->options.seat)return false;
    q3n_hud_state *s=&o->state;
    if(!qa_source_save_bytes(io,s->center_print,sizeof(s->center_print)) || !memchr(s->center_print,0,sizeof(s->center_print)) ||
       !qa_source_save_i32(io,&s->center_print_time) || !qa_source_save_i32(io,&s->center_print_y) ||
       !qa_source_save_i32(io,&s->center_print_char_width) || !qa_source_save_i32(io,&s->center_print_lines) ||
       s->center_print_lines<0 || s->center_print_lines>1024 || !qa_source_save_i32(io,&s->crosshair_client) ||
       s->crosshair_client<0 || s->crosshair_client>=64 || !qa_source_save_i32(io,&s->crosshair_client_time) ||
       !qa_source_save_i32(io,&s->score_fade_time) || !qa_source_save_i32(io,&s->deferred_player_loading) ||
       !qa_source_save_i32(io,&s->prox_time) || !qa_source_save_i32(io,&s->prox_counter) || !qa_source_save_i32(io,&s->prox_tick) ||
       !qa_source_save_i32(io,&s->head_start_time) || !qa_source_save_i32(io,&s->head_end_time) ||
       !q3nh_float(io,&s->head_start_yaw) || !q3nh_float(io,&s->head_end_yaw) ||
       !q3nh_float(io,&s->head_start_pitch) || !q3nh_float(io,&s->head_end_pitch) ||
       !qa_source_save_bool(io,&s->show_scores) || !qa_source_save_bool(io,&s->scoreboard_showing) ||
       !qa_source_save_bool(io,&s->scoreboard_first_time))return false;
    for(unsigned i=0;i<4;++i)if(!qa_source_save_i32(io,&o->previous_times[i]))return false;
    if(!qa_source_save_i32(io,&o->previous_milliseconds) || !qa_source_save_i32(io,&o->fps_index) ||
       !qa_source_save_i32(io,&o->scores_request_time) || !qa_source_save_i32(io,&o->frame_count) ||
       !qa_source_save_i32(io,&o->snapshot_count))return false;
    for(unsigned i=0;i<128;++i)if(!qa_source_save_i32(io,&o->frame_samples[i]) ||
        !qa_source_save_i32(io,&o->snapshot_samples[i]) || !qa_source_save_i32(io,&o->snapshot_flags[i]))return false;
    return qa_source_save_i32(io,&o->oldest_command_time) && qa_source_save_bool(io,&o->has_oldest_command);
}
bool q3n_hud_checkpoint(const q3n_hud *borrowed,qa_buffer *out,qa_error *e)
{
    if(!borrowed || !out || out->data || out->size || !q3nh_capture(borrowed->options.assets,borrowed->busy,e))return false;
    q3n_hud *o=(q3n_hud *)borrowed; o->busy=true; q3n_hud copy=*o;
    qa_source_save_io io={0}; bool ok=qa_source_save_writer(&io,NULL,e) && fields(&io,&copy) && qa_source_save_finish(&io,out);
    if(!ok && e && e->code==QA_OK)q3ne_fail(e,QA_ERROR_FORMAT,"Native Q3 HUD continuation is inconsistent");
    qa_source_save_dispose(&io); o->busy=false; return ok;
}
bool q3n_hud_restore(q3n_hud *o,qa_bytes bytes,qa_error *e)
{
    if(!o || !q3nh_capture(o->options.assets,o->busy,e))return false;
    o->busy=true; q3n_hud candidate={.options=o->options,.source_game=o->source_game,.product=o->product};
    qa_source_save_io io={0}; bool ok=qa_source_save_reader(&io,NULL,bytes,e) && fields(&io,&candidate) && qa_source_save_finish(&io,NULL);
    if(ok) { *o=candidate; o->busy=true; }
    else if(e && e->code==QA_OK)q3ne_fail(e,QA_ERROR_FORMAT,"Saved native Q3 HUD continuation is inconsistent");
    qa_source_save_dispose(&io); o->busy=false; return ok;
}
