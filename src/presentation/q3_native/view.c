/* CG view calculations, id Software 1999-2005, GPL-2.0-or-later. */
#include "view_internal.h"

static bool current(q3n_view *o,const q3n_frame *f,qa_error *e)
{
    return o && f && !o->busy && o->options.application==f->application &&
        o->options.assets==f->assets && o->options.seat==f->seat &&
        o->product==q3n_frame_product(f) && q3n_frame_predicted_player(f) &&
        (f->compiled?o->options.compiled_source==f->compiled->source.owner &&
            f->compiled->stage==Q3N_COMPILED_COMPLETED_FRAME:
            f->remote?!o->options.compiled_source && o->options.remote_client==f->remote->client &&
            f->remote->snapshots.stage==Q3N_REMOTE_COMPLETED_FRAME:
            !o->options.remote_client && !o->options.compiled_source && o->source_game==f->source.source_game && f->time==f->source.source_time_ms) &&
        q3ne_current(f,e)?true:
        q3ne_fail(e,QA_ERROR_ARGUMENT,"Native Q3 view requires its exact completed source and seat");
}
static bool test_current(q3n_view *o,const q3n_frame *f,qa_error *e)
{
    if (f && f->compiled) return o && !o->busy && o->options.application==f->application &&
        o->options.assets==f->assets && o->options.seat==f->seat && o->product==q3n_frame_product(f) &&
        o->options.compiled_source==f->compiled->source.owner && f->compiled->stage==Q3N_COMPILED_CONSOLE &&
        q3n_frame_current(f) && q3ne_current(f,e) ? true :
        q3ne_fail(e,QA_ERROR_ARGUMENT,"Compiled test model requires its actual entered CLIENT console");
    if(!f || !f->remote)return current(o,f,e);
    return o && !o->busy && !o->options.compiled_source && o->options.application==f->application &&
        o->options.assets==f->assets && o->options.seat==f->seat &&
        o->product==q3n_frame_product(f) && o->options.remote_client==f->remote->client &&
        f->remote->snapshots.stage==Q3N_REMOTE_CONSOLE && q3n_frame_current(f) && q3ne_current(f,e)?true:
        q3ne_fail(e,QA_ERROR_ARGUMENT,"Test model requires its actual entered CLIENT console and retained camera");
}
bool q3n_view_create(const q3n_view_options *options,q3n_view **out,qa_error *e)
{
    if(!options || !out || *out || !options->assets || !options->source || !options->application ||
       options->remote_client || options->compiled_source || !options->set_view_size || !options->set_third_person_angle_value || !options->print ||
       !qa_application_native_q3_presentation_current(options->application,options->source))
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Native Q3 view requires actual GAME and its cached CGAME cvars");
    q3n_view *o=calloc(1,sizeof(*o));
    if(!o)return q3ne_fail(e,QA_ERROR_MEMORY,"Allocating native Q3 view");
    o->options=*options; o->options.source=NULL; o->source_game=options->source->source_game;
    o->product=options->source->product; o->state.zoom_sensitivity=1;
    *out=o; return true;
}
bool q3n_view_create_restored(const q3n_view_options *options,q3n_view **out,qa_error *e)
{
    qa_native_q3_client_basis basis;
    if(!options || !out || *out || !options->assets || options->remote_client || options->compiled_source || !options->set_view_size || !options->set_third_person_angle_value ||
       !options->print || !options->client || !qa_native_q3_client_basis_read(options->client,&basis,e) ||
       basis.application!=options->application || basis.seat!=options->seat)
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Restored Q3 view requires its actual installed source and client basis");
    q3n_view *o=calloc(1,sizeof(*o));
    if(!o)return q3ne_fail(e,QA_ERROR_MEMORY,"Allocating restored native Q3 view");
    o->options=*options; o->options.source=NULL; o->source_game=basis.source_game; o->product=basis.product; *out=o; return true;
}
bool q3n_view_create_remote(const q3n_view_options *options,q3n_view **out,qa_error *e)
{
    qa_native_q3_remote_client_basis basis;
    if(!options || !out || *out || !options->assets || options->source || options->client || options->compiled_source ||
       !options->remote_client || !options->set_view_size || !options->set_third_person_angle_value || !options->print ||
       !qa_native_q3_remote_client_basis_read(options->remote_client,&basis,e) ||
       basis.application!=options->application || basis.client.seat!=options->seat)
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Remote Q3 view requires its actual retained CLIENT basis");
    q3n_view *o=calloc(1,sizeof(*o));
    if(!o)return q3ne_fail(e,QA_ERROR_MEMORY,"Allocating remote native Q3 view");
    o->options=*options; o->product=basis.product; *out=o; return true;
}
bool q3n_view_create_compiled(const q3n_view_options *options,q3n_view **out,qa_error *e)
{
    q3n_compiled_source_view source;
    if (!options || !out || *out || !options->assets || options->source || options->client || options->remote_client ||
        !options->compiled_source || !options->set_view_size || !options->set_third_person_angle_value || !options->print ||
        !q3n_compiled_source_checkpoint_read(options->compiled_source,&source,e) || source.basis.assets!=options->assets ||
        source.basis.application!=options->application || source.basis.seat!=options->seat)
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Compiled view requires its real CLIENT declaration and cvar owner");
    q3n_view *o=calloc(1,sizeof(*o));
    if (!o) return q3ne_fail(e,QA_ERROR_MEMORY,"Allocating compiled Q3 view");
    o->options=*options; o->product=source.basis.product; *out=o; return true;
}
void q3n_view_destroy(q3n_view *o) { if(o && !o->busy)free(o); }
bool q3n_view_idle(const q3n_view *o) { return o && !o->busy; }
const q3n_view_state *q3n_view_read(const q3n_view *o) { return o?&o->state:NULL; }
void q3n_view_zoom(q3n_view *o,bool down,int32_t time)
{ if(o && !o->busy && o->state.zoomed!=down) { o->state.zoomed=down; o->state.zoom_time=time; } }
void q3n_view_kick(q3n_view *o,qa_vec3 angles,qa_vec3 origin)
{ if(o && !o->busy && qa_vec_finite(angles) && qa_vec_finite(origin)) { o->state.kick_angles=angles; o->state.kick_origin=origin; } }
void q3n_view_error(q3n_view *o,qa_vec3 error,int32_t time)
{ if(o && !o->busy && !o->options.remote_client && !o->options.compiled_source && qa_vec_finite(error)) { o->state.predicted_error=error; o->state.predicted_error_time=time; } }
void q3n_view_hyperspace(q3n_view *o,bool value) { if(o && !o->busy && !o->options.remote_client && !o->options.compiled_source)o->state.hyperspace=value; }
static void first_person(q3n_view *o,q3n_frame *f,const q3n_view_settings *s,const q3n_player_feedback *g)
{
    const qa_q3_player *p=q3n_frame_predicted_player(f); q3n_view_state *v=&o->state;
    qa_q3_refdef *r=&f->refdef;
    if(q3n_frame_snapshot_player(f)->pmType==5)return;
    const qa_q3_player *snapshot=q3n_frame_snapshot_player(f);
    if(snapshot->stats[0]<=0) {
        f->view_angles=qa_v3(-15,(float)snapshot->stats[q3nh_dead_yaw_stat(o->product)],40);
        r->origin.z=(r->origin.z + (float)p->viewheight); return;
    }
    qa_vec3 angles=q3ne_sum(f->view_angles,v->kick_angles);
    q3n_damage_feedback damage={.time=g->damage_time,.pitch=g->damage_pitch,.roll=g->damage_roll};
    angles=q3n_damage_feedback_angles(&damage,f->time,angles);
    /* Source refdef axes are still zero here, before AnglesToAxis. */
    qa_vec3 velocity=q3ne_array(p->velocity);
    angles.x=(angles.x + (q3ne_dot(velocity,r->axis[0]) * s->run_pitch));
    angles.z=(angles.z + -(q3ne_dot(velocity,r->axis[1]) * s->run_roll));
    float speed=fmaxf(v->xy_speed,200),pitch=((v->bob_fraction_sin * s->bob_pitch) * speed);
    float roll=((v->bob_fraction_sin * s->bob_roll) * speed);
    if(p->pmFlags&1) { pitch=(pitch * 3); roll=(roll * 3); }
    if(v->bob_cycle&1)roll=-roll;
    angles.x=(angles.x + pitch); angles.z=(angles.z + roll); f->view_angles=angles;
    float height=(r->origin.z + (float)p->viewheight);
    int32_t delta=q3ne_sub(f->time,g->duck_time);
    if(delta<100)height=(height + -((g->duck_change * (float)q3ne_sub(100,delta)) / 100));
    height=(height + fminf(6,((v->bob_fraction_sin * v->xy_speed) * s->bob_up)));
    const q3n_event_state *events=q3n_events_state(f->events); delta=q3ne_sub(f->time,events->land_time);
    if(delta<150)height=(height + (events->land_change * ((float)delta / 150)));
    else if(delta<450)height=(height + (events->land_change * (1 + -((float)q3ne_sub(delta,150) / 300))));
    delta=q3ne_sub(f->time,events->step_time);
    if(delta<200)height=(height + -((events->step_change * (float)q3ne_sub(200,delta)) / 200));
    r->origin=q3ne_sum(qa_v3(r->origin.x,r->origin.y,height),v->kick_origin);
}
static bool third_person(q3n_view *o,q3n_frame *f,const q3n_view_settings *s,float angle,qa_error *e)
{
    const qa_q3_player *p=q3n_frame_predicted_player(f); qa_q3_refdef *r=&f->refdef;
    r->origin.z=(r->origin.z + (float)p->viewheight); qa_vec3 focus_angles=f->view_angles;
    if(p->stats[0]<=0)focus_angles.y=f->view_angles.y=(float)p->stats[q3nh_dead_yaw_stat(o->product)];
    if(focus_angles.x>45)focus_angles.x=45;
    qa_vec3 forward,right; q3nh_vectors(focus_angles,&forward,NULL,NULL);
    qa_vec3 focus=q3ne_sum(r->origin,q3ne_scale(forward,512)),view=r->origin; view.z=(view.z + 8);
    f->view_angles.x=(f->view_angles.x * 0.5f); q3nh_vectors(f->view_angles,&forward,&right,NULL);
    float radians=((angle / 180) * 3.14159274101257324219f);
    view=q3ne_sum(view,q3ne_scale(forward,(-s->third_person_range * (float)cos((double)radians))));
    view=q3ne_sum(view,q3ne_scale(right,(-s->third_person_range * (float)sin((double)radians))));
    if(!s->camera_mode) {
        qa_trace_result trace; qa_bounds bounds={qa_v3(-4,-4,-4),qa_v3(4,4,4)};
        if(!q3n_events_trace(f,r->origin,view,bounds,p->clientNum,1,&trace,e))return false;
        if(trace.fraction!=1) {
            view=trace.end; view.z=(view.z + ((1 + -trace.fraction) * 32));
            if(!q3n_events_trace(f,r->origin,view,bounds,p->clientNum,1,&trace,e))return false;
            view=trace.end;
        }
    }
    r->origin=view; focus=q3ne_difference(focus,view);
    float distance=fmaxf(1,(sqrtf(((focus.x * focus.x) + (focus.y * focus.y)))));
    f->view_angles.x=(-57.295780181884765625f * (float)atan2((double)focus.z,(double)distance));
    f->view_angles.y=(f->view_angles.y + -angle); return true;
}
static bool fov(q3n_view *o,q3n_frame *f,const q3n_view_settings *s,bool *in_water,qa_error *e)
{
    qa_q3_refdef *r=&f->refdef; float horizontal=90;
    if(q3n_frame_predicted_player(f)->pmType!=5) {
        horizontal=s->dm_flags&16?90:q3nh_clamp(s->fov,1,160); float zoom=q3nh_clamp(s->zoom_fov,1,160);
        float fraction=((float)q3ne_sub(f->time,o->state.zoom_time) / 150);
        if(o->state.zoomed)horizontal=fraction>1?zoom:(horizontal + (fraction * (zoom + -horizontal)));
        else if(fraction<=1)horizontal=(zoom + (fraction * (horizontal + -zoom)));
    }
    float radians=((horizontal / 360) * 3.14159274101257324219f);
    float tangent=((float)sin((double)radians) / (float)cos((double)radians));
    float x=((float)r->width / tangent);
    float vertical=(((float)atan2((double)r->height,(double)x) * 360) / 3.14159274101257324219f);
    uint32_t contents;
    if(!q3n_events_point_contents(f,r->origin,-1,&contents,e))return false;
    *in_water=(contents&(8|16|32))!=0;
    if(*in_water) {
        float phase=(((((float)f->time / 1000) * 0.4f) * 3.14159274101257324219f) * 2);
        float wave=(float)sin((double)phase); horizontal=(horizontal + wave); vertical=(vertical + -wave);
    }
    r->fov_x=horizontal; r->fov_y=vertical;
    o->state.zoom_sensitivity=o->state.zoomed?(vertical / 75):1; return true;
}
bool q3n_view_frame(q3n_view *o,q3n_frame *f,const q3n_view_settings *s,const q3n_player_state *ps,
    qa_scene_rect viewport,bool *in_water,qa_error *e)
{
    if(!s || !in_water || !ps || !current(o,f,e) || viewport.width<2 || viewport.height<2 ||
       !q3n_player_state_idle(ps) || ps->source_game!=o->source_game || ps->options.application!=o->options.application ||
       ps->options.assets!=o->options.assets || ps->options.seat!=o->options.seat ||
       ps->options.remote_client!=o->options.remote_client || ps->options.compiled_source!=o->options.compiled_source)return false;
    const q3n_player_feedback *g=q3n_player_state_feedback(ps); o->busy=true;
    const qa_q3_player *snapshot=q3n_frame_snapshot_player(f),*predicted=q3n_frame_predicted_player(f);
    int32_t size=snapshot->pmType==5?100:s->view_size; bool ok=true;
    if(size<30 || size>100) { size=size<30?30:100; ok=o->options.set_view_size(o->options.context,size,e) && q3ne_current(f,e); }
    memset(&f->refdef,0,sizeof(f->refdef)); qa_q3_refdef *r=&f->refdef;
    r->width=(int32_t)(((int64_t)viewport.width*size)/100)&~1;
    r->height=(int32_t)(((int64_t)viewport.height*size)/100)&~1;
    if(r->width<=0 || r->height<=0) {
        o->busy=false; return q3ne_fail(e,QA_ERROR_ARGUMENT,"Q3 camera requires a positive authored viewport");
    }
    r->x=((int32_t)viewport.width-r->width)/2; r->y=((int32_t)viewport.height-r->height)/2;
    r->time=f->time; r->origin=q3ne_array(predicted->origin); f->view_angles=q3ne_array(predicted->viewangles);
    if(f->remote)memcpy(r->area_mask,f->remote->snapshots.snapshot->area_mask,sizeof(r->area_mask));
    if(f->compiled)memcpy(r->area_mask,f->compiled->snapshot->area_mask,sizeof(r->area_mask));
    f->third_person=s->third_person || snapshot->stats[0]<=0;
    if(predicted->pmType!=5) {
        q3n_view_state *v=&o->state; v->bob_cycle=(predicted->bobCycle&128)>>7;
        v->bob_fraction_sin=fabsf((float)sin((double)(((float)(predicted->bobCycle&127) / 127) * 3.14159274101257324219f)));
        v->xy_speed=(sqrtf(((predicted->velocity[0] * predicted->velocity[0]) + (predicted->velocity[1] * predicted->velocity[1]))));
        float angle=s->third_person_angle;
        if(s->camera_orbit_integer && f->time>v->next_orbit_time) {
            v->next_orbit_time=q3ne_plus(f->time,s->camera_orbit_delay); angle=(angle + s->camera_orbit_value);
            if(ok)ok=o->options.set_third_person_angle_value(o->options.context,angle,e) && q3ne_current(f,e);
        }
        if(!f->remote && !f->compiled && g->this_frame_teleport) { v->predicted_error=qa_v3(0,0,0); v->predicted_error_time=0; }
        if(s->error_decay>0) {
            int32_t error_time=f->compiled?f->compiled->prediction_error_time:f->remote?f->remote->prediction.prediction_error_time:v->predicted_error_time;
            qa_vec3 error=f->compiled?f->compiled->prediction_error:f->remote?f->remote->prediction.prediction_error:v->predicted_error;
            float factor=((s->error_decay + -(float)q3ne_sub(f->time,error_time)) / s->error_decay);
            if(factor>0 && factor<1)r->origin=q3ne_sum(r->origin,q3ne_scale(error,factor));
            else if(f->compiled) {
                if(ok)ok=q3n_compiled_frame_prediction_error_clear(f->compiled,e) && q3ne_current(f,e);
            } else if(f->remote) {
                if(ok)ok=q3n_remote_frame_prediction_error_clear(f->remote,e) && q3ne_current(f,e);
            } else v->predicted_error_time=0;
        }
        if(ok && f->third_person)ok=third_person(o,f,s,angle,e);
        else if(ok)first_person(o,f,s,g);
        if(f->compiled?f->compiled->hyperspace:f->remote?f->remote->prediction.hyperspace:v->hyperspace)r->flags|=1|4;
    }
    if(ok && o->options.camera_override) {
        qa_application_camera_view camera; bool found=false;
        ok=o->options.camera_override(o->options.camera_context,f,&camera,&found,e) && q3ne_current(f,e);
        if(ok && found) {
            r->origin=qa_vec_add(camera.origin,camera.view_offset);
            f->view_angles=camera.angles;
        }
    }
    q3nh_axis(f->view_angles,r->axis);
    if(ok)ok=fov(o,f,s,in_water,e) && q3ne_current(f,e);
    o->busy=false; return ok;
}
bool q3n_view_damage_blob(q3n_view *o,const q3n_frame *f,const q3n_view_settings *s,const q3n_player_state *ps,qa_error *e)
{
    if(!s || !ps || !current(o,f,e) || ps->source_game!=o->source_game || ps->options.assets!=o->options.assets ||
       ps->options.application!=o->options.application || ps->options.seat!=o->options.seat ||
       ps->options.remote_client!=o->options.remote_client || ps->options.compiled_source!=o->options.compiled_source)return false;
    const q3n_player_feedback *g=q3n_player_state_feedback(ps);
    int32_t elapsed=q3ne_int(((float)f->time + -g->damage_time));
    if(g->damage_value==0.0f || s->ragepro || elapsed<=0 || elapsed>=500 || f->third_person)return true;
    qa_q3_ref_entity r={.kind=QA_Q3_REF_SPRITE,.flags=4,.custom_shader=q3n_media_read(f->media)->graphics[Q3N_G_VIEW_BLOOD]};
    r.origin=q3ne_sum(f->refdef.origin,q3ne_scale(f->refdef.axis[0],8));
    r.origin=q3ne_sum(r.origin,q3ne_scale(f->refdef.axis[1],(g->damage_x * -8)));
    r.origin=q3ne_sum(r.origin,q3ne_scale(f->refdef.axis[2],(g->damage_y * 8)));
    r.radius=(g->damage_value * 3); r.color[0]=r.color[1]=r.color[2]=255;
    r.color[3]=q3ne_byte((200 * (1 + -((float)elapsed / 500))));
    o->busy=true; bool ok=qa_q3_presentation_entity(f->presentation,&r,e) && q3ne_current(f,e); o->busy=false; return ok;
}
void q3n_view_test_clear(q3n_view *o)
{ if(o && !o->busy) { memset(o->test_model_name,0,sizeof(o->test_model_name)); memset(&o->test_model,0,sizeof(o->test_model)); o->state.test_gun=false; } }
bool q3n_view_test_model(q3n_view *o,const q3n_frame *f,const char *name,const float *back_lerp,bool gun,qa_error *e)
{
    if(!test_current(o,f,e))return false;
    memset(&o->test_model,0,sizeof(o->test_model)); o->busy=true; bool ok=true;
    if(name) {
        snprintf(o->test_model_name,sizeof(o->test_model_name),"%s",name);
        ok=qa_q3_register_model(f->assets,o->test_model_name,&o->test_model.model,e) && q3ne_current(f,e);
        if(ok && back_lerp) { o->test_model.back_lerp=*back_lerp; o->test_model.frame=1; }
        if(ok && !o->test_model.model)o->options.print(o->options.context,"Can't register model\n");
        else if(ok) {
            o->test_model.origin=q3ne_sum(f->refdef.origin,q3ne_scale(f->refdef.axis[0],100));
            q3nh_axis(qa_v3(0,(180 + f->view_angles.y),0),o->test_model.axis);
            o->state.test_gun=false;
        }
    }
    if(ok && gun) { o->state.test_gun=true; o->test_model.flags=1|8|4; }
    o->busy=false; return ok;
}
void q3n_view_test_step(q3n_view *o,bool skin,int32_t delta)
{
    if(!o || o->busy)return;
    int32_t *value=skin?&o->test_model.skin:&o->test_model.frame;
    *value=q3ne_plus(*value,delta); if(delta<0 && *value<0)*value=0;
    char text[64]; snprintf(text,sizeof(text),"%s %i\n",skin?"skin":"frame",*value); o->options.print(o->options.context,text);
}
bool q3n_view_test_submit(q3n_view *o,const q3n_frame *f,const q3n_view_settings *s,qa_error *e)
{
    if(!s || !current(o,f,e))return false;
    if(!o->test_model.model)return true;
    o->busy=true; bool ok=qa_q3_register_model(f->assets,o->test_model_name,&o->test_model.model,e) && q3ne_current(f,e);
    if(ok && !o->test_model.model)o->options.print(o->options.context,"Can't register model\n");
    else if(ok) {
        if(o->state.test_gun) {
            memcpy(o->test_model.axis,f->refdef.axis,sizeof(o->test_model.axis));
            o->test_model.origin=q3ne_sum(f->refdef.origin,q3ne_scale(f->refdef.axis[0],s->gun_x));
            o->test_model.origin=q3ne_sum(o->test_model.origin,q3ne_scale(f->refdef.axis[1],s->gun_y));
            o->test_model.origin=q3ne_sum(o->test_model.origin,q3ne_scale(f->refdef.axis[2],s->gun_z));
        }
        ok=qa_q3_presentation_entity(f->presentation,&o->test_model,e) && q3ne_current(f,e);
    }
    o->busy=false; return ok;
}
void q3n_view_round(q3n_view *o)
{ (void)o; /* CG_MapRestart retains zoom, orbit, kicks, camera and test model. */ }
