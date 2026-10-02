#include "player_state_internal.h"

static bool fields(qa_source_save_io *io,q3n_player_state *o)
{
    const char *signature=o->options.compiled_source?"Q3PC":o->options.remote_client?"Q3PR":"Q3PS";
    uint8_t magic[4]; memcpy(magic,signature,4); uint32_t version=1,product=(uint32_t)o->product,seat=o->options.seat;
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,signature,4) || !qa_source_save_u32(io,&version) || version!=1 ||
       !qa_source_save_u32(io,&product) || product!=(uint32_t)o->product || !qa_source_save_u32(io,&seat) || seat!=o->options.seat ||
       !q3nh_remote_basis_fields(io,o->options.remote_client) ||
       (o->options.compiled_source && !q3n_compiled_source_fields(io,o->options.compiled_source)))return false;
    q3n_transition_history *h=&o->history;
    if(!o->options.remote_client && !o->options.compiled_source) {
        if(!qa_source_save_bool(io,&h->valid) || !qa_source_save_actor(io,&h->viewing_actor) ||
           !qa_source_save_actor(io,&h->followed_actor) || !qa_source_save_u32(io,&h->viewing_client) || h->viewing_client>=64 ||
           !qa_source_save_u64(io,&h->source_frame) || !qa_source_save_i32(io,&h->source_time) ||
           !qa_source_save_i32(io,&h->client_num) || h->client_num<0 || h->client_num>=64 ||
           !qa_source_save_i32(io,&h->damage_event) || !qa_source_save_i32(io,&h->viewheight) ||
           !qa_source_save_i32(io,&h->external_event) || !qa_source_save_i32(io,&h->event_sequence) ||
           !qa_source_save_i32(io,&h->health) || !qa_source_save_i32(io,&h->e_flags))return false;
        for(unsigned i=0;i<2;++i)if(!qa_source_save_i32(io,&h->events[i]))return false;
        for(unsigned i=0;i<15;++i)if(!qa_source_save_i32(io,&h->persistant[i]))return false;
        for(unsigned i=0;i<16;++i)if(!qa_source_save_i32(io,&h->powerups[i]))return false;
    }
    q3n_player_feedback *g=&o->feedback;
    if(!qa_source_save_i32(io,&g->duck_time) || !qa_source_save_i32(io,&g->attacker_time) ||
       !qa_source_save_i32(io,&g->damage_kick_end_time) || !qa_source_save_i32(io,&g->low_ammo_warning) ||
       g->low_ammo_warning<0 || g->low_ammo_warning>2 || !q3nh_float(io,&g->duck_change) ||
       !q3nh_float(io,&g->damage_time) || !q3nh_float(io,&g->damage_x) || !q3nh_float(io,&g->damage_y) ||
       g->damage_x < -1 || g->damage_x > 1 || g->damage_y < -1 || g->damage_y > 1 ||
       !q3nh_float(io,&g->damage_roll) || !q3nh_float(io,&g->damage_pitch) || !q3nh_float(io,&g->damage_value) ||
       g->damage_value<0 || g->damage_value>10 || !qa_source_save_i32(io,&g->reward_time) ||
       !qa_source_save_i32(io,&g->reward_stack) || g->reward_stack<0 || g->reward_stack>9 ||
       !qa_source_save_i32(io,&g->timelimit_warnings) || (g->timelimit_warnings&~7) ||
       !qa_source_save_i32(io,&g->fraglimit_warnings) || (g->fraglimit_warnings&~7) ||
       !qa_source_save_bool(io,&g->this_frame_teleport))return false;
    for(unsigned i=0;i<10;++i) {
        q3n_reward *r=&g->rewards[i];
        if(!qa_source_save_i32(io,&r->sound) || !q3nh_handle(o->options.assets,r->sound,Q3P_SOUND) ||
           !qa_source_save_i32(io,&r->shader) || !q3nh_handle(o->options.assets,r->shader,Q3P_SHADER) ||
           !qa_source_save_i32(io,&r->count))return false;
    }
    if(!qa_source_save_i32(io,&o->event_sequence))return false;
    for(unsigned i=0;i<16;++i)if(!qa_source_save_i32(io,&o->predictable_events[i]))return false;
    return qa_source_save_bool(io,&o->map_restart) &&
        (o->options.remote_client || o->options.compiled_source || !h->valid || (h->viewing_actor.registry && h->followed_actor.registry));
}
bool q3n_player_state_checkpoint(const q3n_player_state *borrowed,qa_buffer *out,qa_error *e)
{
    if(!borrowed || !out || out->data || out->size || !q3nh_capture(borrowed->options.assets,borrowed->busy,e))return false;
    q3n_player_state *o=(q3n_player_state *)borrowed; o->busy=true; q3n_player_state copy=*o;
    qa_source_save_io io={0}; bool ok=qa_source_save_writer(&io,qa_application_session(o->options.application),e) &&
        fields(&io,&copy) && qa_source_save_finish(&io,out);
    if(!ok && e && e->code==QA_OK)q3ne_fail(e,QA_ERROR_FORMAT,"Native Q3 playerstate continuation is inconsistent");
    qa_source_save_dispose(&io); o->busy=false; return ok;
}
bool q3n_player_state_restore(q3n_player_state *o,qa_bytes bytes,qa_error *e)
{
    if(!o || !q3nh_capture(o->options.assets,o->busy,e))return false;
    o->busy=true; q3n_player_state candidate={.options=o->options,.source_game=o->source_game,.product=o->product};
    qa_source_save_io io={0}; bool ok=qa_source_save_reader(&io,qa_application_session(o->options.application),bytes,e) &&
        fields(&io,&candidate) && qa_source_save_finish(&io,NULL);
    if(ok) { *o=candidate; o->busy=true; }
    else if(e && e->code==QA_OK)q3ne_fail(e,QA_ERROR_FORMAT,"Saved native Q3 playerstate continuation is inconsistent");
    qa_source_save_dispose(&io); o->busy=false; return ok;
}
