/* Selected cg_marks.c particles, id Software, GPL-2.0-or-later.
 * Behavioral donor: render/scene/particles/q3-system.ts, cg_marks.c profile. */
#include "particles_internal.h"
#include "pose.h"
#include <stdio.h>

static float add(float a, float b) { volatile float v = a + b; return v; }
static float mul(float a, float b) { volatile float v = a * b; return v; }
static float divide(float a, float b) { volatile float v = a / b; return v; }
static int32_t word(uint32_t v) { return v <= INT32_MAX ? (int32_t)v : -1 - (int32_t)(UINT32_MAX - v); }
static int32_t integer(float v)
{ return v >= -2147483648.0f && v < 2147483648.0f ? (int32_t)v : INT32_MIN; }
static qa_vec3 scale(qa_vec3 v, float n) { return qa_v3(mul(v.x,n),mul(v.y,n),mul(v.z,n)); }
static qa_vec3 sum(qa_vec3 a, qa_vec3 b) { return qa_v3(add(a.x,b.x),add(a.y,b.y),add(a.z,b.z)); }
static qa_vec3 ma(qa_vec3 a, float n, qa_vec3 v) { return sum(a,scale(v,n)); }
bool q3np_fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error,code,0,"%s",message); return false; }
static bool current(const q3n_frame *f, qa_error *error)
{
    if (f && f->unified_effects) {
        return f->particles && f->particles->initialized && !f->particles->remote_source &&
            f->particles->assets==f->assets && f->particles->product==f->unified_effects->product &&
            f->events && f->presentation && q3n_frame_current(f) ? true :
            q3np_fail(error,QA_ERROR_ARGUMENT,"Unified Q3 particles lost their actual CLIENT source and dictionary");
    }
    if(f&&f->remote) {
        if(!f->particles||!f->particles->initialized||!f->events||!f->presentation||
           f->particles->assets!=f->assets||f->particles->product!=f->remote->source.basis.product||
           f->particles->remote_source!=f->remote->source.owner||!q3n_frame_snapshot_player(f)||
           !q3n_frame_current(f))
            return q3np_fail(error,QA_ERROR_ARGUMENT,"Native Q3 particles require their actual remote source and snapshot");
        return true;
    }
    if (!(f && f->particles && f->particles->initialized && f->application && f->events &&
        !f->particles->remote_source &&
        f->presentation && f->particles->assets == f->assets && f->particles->product == f->source.product &&
        f->time == f->source.source_time_ms && f->has_local_player &&
        qa_application_native_q3_presentation_current(f->application,&f->source)))
        return q3np_fail(error,QA_ERROR_ARGUMENT,"Native Q3 particles require their actual current source frame");
    uint32_t physical; qa_actor_id actor; qa_q3_player player; bool found;
    if (!qa_application_native_q3_presentation_local(f->application,&f->source,f->seat,
        &physical,&actor,&player,&found,error)) return false;
    return found && physical==f->viewing_client && qa_actor_id_equal(actor,f->viewing_actor) ? true :
        q3np_fail(error,QA_ERROR_ARGUMENT,"Native Q3 particle viewing actor was superseded");
}
static bool source_current(q3n_particles *o, qa_application *app,
    const qa_application_native_q3_presentation *cut, int32_t time, qa_error *error)
{
    return !o->remote_source && app && cut && cut->product == o->product && time == cut->source_time_ms &&
        qa_application_native_q3_presentation_current(app,cut) ? true :
        q3np_fail(error,QA_ERROR_ARGUMENT,"Native Q3 particle registration source was superseded");
}
static void reset(q3n_particles *o, int32_t time)
{
    memset(o->slots,0,sizeof(o->slots));
    for (int32_t i=0;i<Q3N_PARTICLE_CAPACITY;++i) o->slots[i].next=i+1==Q3N_PARTICLE_CAPACITY?-1:i+1;
    o->active=-1; o->free=0; o->count=0; o->old_time=(float)time;
}
bool q3n_particles_create(qa_q3_presentation_assets *assets, qa_q3_product product,
    q3n_particles **out, qa_error *error)
{
    if (!assets || !out || *out || (product!=QA_Q3_ARENA && product!=QA_Q3_TEAM_ARENA))
        return q3np_fail(error,QA_ERROR_ARGUMENT,"Invalid native Q3 particle owner services");
    q3n_particles *o=calloc(1,sizeof(*o));
    if (!o) return q3np_fail(error,QA_ERROR_MEMORY,"Allocating native Q3 particle pool");
    o->assets=assets; o->product=product; reset(o,0); *out=o; return true;
}
bool q3n_particles_create_remote(qa_q3_presentation_assets *assets,q3n_remote_source *source,q3n_particles **out,qa_error *error)
{
    q3n_remote_source_view view;
    if(!source||!q3n_remote_source_read(source,&view,error)||!q3n_remote_source_current(&view))return false;
    if(!q3n_particles_create(assets,view.basis.product,out,error))return false;
    (*out)->remote_source=source; return true;
}
bool q3n_particles_idle(const q3n_particles *o) { return o && !o->busy; }
void q3n_particles_destroy(q3n_particles *o) { if (q3n_particles_idle(o)) free(o); }
void q3n_particles_round(q3n_particles *o, int32_t time) { if (q3n_particles_idle(o)) reset(o,time); }
bool q3n_particles_load(q3n_particles *o, qa_application *app,
    const qa_application_native_q3_presentation *cut, int32_t time, qa_error *error)
{
    if (!q3n_particles_idle(o) || !source_current(o,app,cut,time,error)) return false;
    o->busy=true; o->initialized=false; reset(o,time); bool ok=true;
    for (int32_t i=0;i<Q3N_PARTICLE_FRAMES && ok;++i) {
        char name[32]; snprintf(name,sizeof(name),"explode1%d",i+1);
        ok=qa_q3_register_shader(o->assets,name,true,&o->shaders[i],error) && source_current(o,app,cut,time,error);
    }
    o->initialized=ok; o->busy=false; return ok;
}
static bool registration_current(const q3n_particles *o,const q3n_frame *f,qa_error *e)
{
    return f&&f->remote&&f->particles==o&&f->assets==o->assets&&
        f->presentation&&f->presentation->options.assets==o->assets&&
        f->remote->source.owner==o->remote_source&&f->remote->source.basis.product==o->product&&
        (f->remote->snapshots.stage==Q3N_REMOTE_INITIALIZATION||
         f->remote->snapshots.stage==Q3N_REMOTE_SNAPSHOT_CALLBACK)&&q3n_frame_current(f)?true:
        q3np_fail(e,QA_ERROR_ARGUMENT,"Remote particle registration requires its genuine entered Init or reached command frame");
}
bool q3n_particles_load_remote(q3n_particles *o,const q3n_frame *f,qa_error *e)
{
    if(!q3n_particles_idle(o)||!registration_current(o,f,e))return false;
    o->busy=true; o->initialized=false; reset(o,f->time); bool ok=true;
    for(int32_t i=0;i<Q3N_PARTICLE_FRAMES&&ok;++i) {
        char name[32]; snprintf(name,sizeof(name),"explode1%d",i+1);
        ok=qa_q3_register_shader(o->assets,name,true,&o->shaders[i],e)&&registration_current(o,f,e);
    }
    o->initialized=ok; o->busy=false; return ok;
}
static bool unified_registration_current(q3n_particles *o,const q3n_frame *f,qa_error *e)
{
    return f && f->unified_effects && f->particles==o && !o->remote_source &&
        f->assets==o->assets && f->unified_effects->product==o->product && f->presentation &&
        q3n_frame_current(f) ? true : q3np_fail(e,QA_ERROR_ARGUMENT,"Unified particle registration lost its actual CLIENT receipt");
}
bool q3n_particles_load_unified(q3n_particles *o,const q3n_frame *f,qa_error *e)
{
    if (!q3n_particles_idle(o) || !unified_registration_current(o,f,e)) return false;
    if (o->initialized) return true;
    o->busy=true; reset(o,f->time); bool okay=true;
    for (int32_t i=0; i<Q3N_PARTICLE_FRAMES && okay; ++i) {
        char name[32]; snprintf(name,sizeof(name),"explode1%d",i+1);
        okay=qa_q3_register_shader(o->assets,name,true,&o->shaders[i],e) && unified_registration_current(o,f,e);
    }
    o->initialized=okay; o->busy=false; return okay;
}
static bool same_animation(const char *name)
{
    const char *expected="explode1";
    for (;*name && *expected;++name,++expected) {
        unsigned char c=(unsigned char)*name; if (c>='A' && c<='Z') c+='a'-'A';
        if (c!=(unsigned char)*expected) return false;
    }
    return !*name && !*expected;
}
bool q3n_particles_explosion(const q3n_frame *f, const char *name, qa_vec3 origin,
    qa_vec3 velocity, int32_t duration, float start_size, float end_size, qa_error *error)
{
    if (!current(f,error) || !q3n_particles_idle(f->particles) || !name) return false;
    if (!same_animation(name)) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"CG_ParticleExplosion: unknown animation string: %s\n",name); return false;
    }
    if (!qa_vec_finite(origin) || !qa_vec_finite(velocity) || !isfinite(start_size) || !isfinite(end_size) ||
        start_size!=truncf(start_size) || end_size!=truncf(end_size) ||
        start_size < -2147483648.0f || start_size >= 2147483648.0f ||
        end_size < -2147483648.0f || end_size >= 2147483648.0f)
        return q3np_fail(error,QA_ERROR_ARGUMENT,"Native Q3 explosion sizes require source int32 values");
    q3n_particles *o=f->particles; if (o->free==-1) return true;
    o->busy=true; int32_t index=o->free; q3n_particle *p=&o->slots[index];
    o->free=p->next; p->next=o->active; o->active=index; ++o->count;
    p->time=(float)f->time; p->alpha=0.5f; p->alpha_velocity=0;
    if (duration<0) { duration=word(0u-(uint32_t)duration); p->roll=0; }
    else p->roll=integer(mul(q3n_events_crandom(f->events),179));
    p->width=p->height=start_size; p->end_width=p->end_height=end_size;
    p->end_time=(float)word((uint32_t)f->time+(uint32_t)duration); p->type=6;
    p->origin=origin; p->velocity=velocity; p->acceleration=qa_v3(0,0,0);
    bool ok=current(f,error); o->busy=false; return ok;
}
bool q3n_particles_weapon_explosion(void *context, const q3n_frame *f, const char *name,
    qa_vec3 origin, qa_vec3 velocity, int32_t duration, float start_size, float end_size, qa_error *error)
{
    if (!f || context!=f->particles) return q3np_fail(error,QA_ERROR_ARGUMENT,"Native rocket requires its real particle child");
    return q3n_particles_explosion(f,name,origin,velocity,duration,start_size,end_size,error);
}
static qa_vec3 vector_angles(qa_vec3 v)
{
    float yaw,pitch;
    if (v.x==0 && v.y==0) { yaw=0; pitch=v.z>0?90:270; }
    else {
        yaw=v.x!=0?divide(mul((float)atan2((double)v.y,(double)v.x),180),3.14159274101257324219f):v.y>0?90:270;
        if (yaw<0) yaw=add(yaw,360);
        float forward=(float)sqrt((double)add(mul(v.x,v.x),mul(v.y,v.y)));
        pitch=divide(mul((float)atan2((double)v.z,(double)forward),180),3.14159274101257324219f);
        if (pitch<0) pitch=add(pitch,360);
    }
    return qa_v3(-pitch,yaw,0);
}
static bool draw(const q3n_frame *f, q3n_particle *p, qa_vec3 origin, qa_error *error)
{
    float ratio=divide(add((float)f->time,-p->time),add(p->end_time,-p->time));
    if (ratio>=1) ratio=0.9999f;
    float width=add(p->width,mul(ratio,add(p->end_width,-p->width)));
    float height=add(p->height,mul(ratio,add(p->end_height,-p->height)));
    const qa_q3_player *player=q3n_frame_snapshot_player(f);
    qa_vec3 eye=f->unified_effects ? f->refdef.origin : qa_v3(player->origin[0],player->origin[1],player->origin[2]);
    qa_vec3 distance=sum(eye,scale(origin,-1));
    float length=(float)sqrt((double)add(add(mul(distance.x,distance.x),mul(distance.y,distance.y)),mul(distance.z,distance.z)));
    if (length<divide(width,1.5f)) return true;
    int32_t index=integer((float)floor((double)mul(ratio,Q3N_PARTICLE_FRAMES)));
    if (index<0 || index>=Q3N_PARTICLE_FRAMES) return q3np_fail(error,QA_ERROR_FORMAT,"Particle animation frame outside source range");
    p->shader=f->particles->shaders[index];
    qa_vec3 right=f->refdef.axis[1],up=f->refdef.axis[2];
    if (p->roll) {
        qa_vec3 angles=vector_angles(f->refdef.axis[0]),axis[3]; angles.z=add(angles.z,(float)p->roll);
        q3n_angles_axis(angles,axis); right=scale(axis[1],-1); up=axis[2];
    }
    qa_q3_poly_vertex vertices[4]={0};
    vertices[0].position=ma(ma(origin,-height,up),-width,right);
    vertices[1].position=ma(vertices[0].position,mul(2,height),up);
    vertices[2].position=ma(vertices[1].position,mul(2,width),right);
    vertices[3].position=ma(vertices[2].position,mul(-2,height),up);
    const qa_scene_vec2 uv[4]={{0,0},{0,1},{1,1},{1,0}};
    for (size_t i=0;i<4;++i) { vertices[i].texcoord=uv[i]; memset(vertices[i].color,255,4); }
    return !p->shader || (qa_q3_presentation_poly(f->presentation,p->shader,vertices,4,error) && current(f,error));
}
bool q3n_particles_add(const q3n_frame *f, qa_error *error)
{
    if (!current(f,error) || !q3n_particles_idle(f->particles)) return false;
    q3n_particles *o=f->particles; o->busy=true;
    memcpy(o->view_axes,f->refdef.axis,sizeof(o->view_axes));
    qa_vec3 angles=vector_angles(o->view_axes[0]);
    o->view_roll=add(o->view_roll,mul(add((float)f->time,-o->old_time),0.1f));
    angles.z=add(angles.z,mul(o->view_roll,0.9f)); q3n_angles_axis(angles,o->rotated_axes);
    o->rotated_axes[1]=scale(o->rotated_axes[1],-1); o->old_time=(float)f->time;
    bool ok=true; int32_t *link=&o->active;
    while (*link!=-1 && ok) {
        int32_t index=*link; q3n_particle *p=&o->slots[index];
        float elapsed=mul(add((float)f->time,-p->time),0.001f);
        if (add(p->alpha,mul(elapsed,p->alpha_velocity))<=0 || (float)f->time>p->end_time) {
            *link=p->next; p->next=o->free; o->free=index; p->type=0; p->alpha=0; --o->count; continue;
        }
        qa_vec3 origin=sum(sum(p->origin,scale(p->velocity,elapsed)),scale(p->acceleration,mul(elapsed,elapsed)));
        ok=draw(f,p,origin,error); link=&p->next;
    }
    if (ok) ok=current(f,error);
    o->busy=false; return ok;
}
