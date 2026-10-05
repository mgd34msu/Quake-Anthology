/* id Software CG_ImpactMark/CG_AddMarks; GPL-2.0-or-later. */
#include "events_internal.h"

static bool impact(const q3n_frame *f, const q3n_impact_mark *r, qa_error *error)
{
    q3n_events *o=f->events; if(!f->event_settings->add_marks)return true;
    if(!q3ne_current(f,error))return false;
    if(!isfinite(r->radius) || r->radius<=0 || !qa_vec_finite(r->origin) ||
       !qa_vec_finite(r->direction) || !isfinite(r->orientation))
        return q3ne_fail(error,QA_ERROR_ARGUMENT,"CG_ImpactMark requires finite geometry and positive radius");
    if(q3ne_dot(r->direction,r->direction)==0)return true;
    for(unsigned i=0;i<4;++i)if(!isfinite(r->color[i]) || r->color[i]<0 || r->color[i]>1)
        return q3ne_fail(error,QA_ERROR_ARGUMENT,"CG_ImpactMark requires unit source colors");
    qa_vec3 normal=q3ne_normalize(r->direction), axis2=q3ne_rotate(normal,q3ne_perpendicular(normal),r->orientation);
    qa_vec3 axis1=q3ne_cross(normal,axis2), points[4], output[384]; q3n_mark_fragment fragments[128]; size_t count=0;
    const float signs[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
    for(unsigned i=0;i<4;++i)points[i]=q3ne_sum(q3ne_sum(r->origin,q3ne_scale(q3ne_scale(axis1,r->radius),signs[i][0])),q3ne_scale(q3ne_scale(axis2,r->radius),signs[i][1]));
    if(!o->options.mark_fragments(o->options.context,f,points,4,q3ne_scale(r->direction,-20),output,384,fragments,128,&count,error) ||
       !q3ne_current(f,error))return false;
    if(count>128)return q3ne_fail(error,QA_ERROR_FORMAT,"Actual mark projector exceeded its source fragment destination");
    float scale=(0.5f / r->radius);
    for(size_t i=0;i<count;++i) {
        q3n_mark_fragment fragment=fragments[i]; uint32_t vertices=fragment.count>10?10:fragment.count;
        if(fragment.first>384 || fragment.count>384-fragment.first)
            return q3ne_fail(error,QA_ERROR_FORMAT,"Actual mark projector returned an invalid point span");
        q3n_stored_mark mark={.time=f->time,.shader=r->shader,.alpha_fade=r->alpha_fade,.count=vertices};
        memcpy(mark.color,r->color,sizeof(mark.color));
        for(uint32_t j=0;j<vertices;++j) {
            qa_q3_poly_vertex *v=&mark.vertices[j]; v->position=output[fragment.first+j];
            qa_vec3 delta=q3ne_difference(v->position,r->origin);
            v->texcoord=(qa_scene_vec2){(0.5f + (q3ne_dot(delta,axis1) * scale)),(0.5f + (q3ne_dot(delta,axis2) * scale))};
            for(unsigned k=0;k<4;++k)v->color[k]=q3ne_byte((r->color[k] * 255));
        }
        if(r->temporary) {
            if(!qa_q3_presentation_poly(f->presentation,r->shader,mark.vertices,vertices,error) || !q3ne_current(f,error))return false;
            continue;
        }
        if(o->mark_count==Q3N_MARK_CAPACITY) {
            int32_t oldest=o->marks[o->mark_count-1].time;
            do { --o->mark_count; } while(o->mark_count && o->marks[o->mark_count-1].time==oldest);
        }
        memmove(o->marks+1,o->marks,o->mark_count*sizeof(o->marks[0])); o->marks[0]=mark; ++o->mark_count;
    }
    return true;
}
bool q3n_marks_impact(const q3n_frame *f, const q3n_impact_mark *r, qa_error *error)
{
    if(!r || !q3ne_current(f,error))return false;
    q3n_events *o=f->events; bool own_lease=!o->busy;
    if(own_lease)o->busy=true;
    bool ok=impact(f,r,error);
    if(own_lease)o->busy=false;
    return ok;
}
static void fade_rgb(q3n_stored_mark *m, int32_t fade)
{
    for(uint32_t i=0;i<m->count;++i)for(unsigned c=0;c<3;++c)
        m->vertices[i].color[c]=q3ne_byte((m->color[c] * (float)fade));
}
static bool submit(const q3n_frame *f, qa_error *error)
{
    q3n_events *o=f->events; if(!f->event_settings->add_marks)return true;
    if(!q3ne_current(f,error))return false;
    for(uint32_t i=0;i<o->mark_count;) {
        q3n_stored_mark *m=&o->marks[i]; int32_t expires=q3ne_plus(m->time,10000);
        if(f->time>expires) {
            --o->mark_count; memmove(m,m+1,(o->mark_count-i)*sizeof(*m)); continue;
        }
        if(m->shader==q3n_media_read(f->media)->graphics[Q3N_G_ENERGY_MARK]) {
            int32_t fade=q3ne_int((450 + -(450 * ((float)q3ne_sub(f->time,m->time) / 3000))));
            if(fade<255 && m->count && m->vertices[0].color[0])fade_rgb(m,fade<0?0:fade);
        }
        int32_t remaining=q3ne_sub(expires,f->time);
        if(remaining<1000) {
            int32_t fade=q3ne_word(255u*(uint32_t)remaining)/1000;
            if(m->alpha_fade)for(uint32_t j=0;j<m->count;++j)m->vertices[j].color[3]=(uint8_t)(uint32_t)fade;
            else fade_rgb(m,fade);
        }
        if(!qa_q3_presentation_poly(f->presentation,m->shader,m->vertices,m->count,error) || !q3ne_current(f,error))return false;
        ++i;
    }
    return true;
}
bool q3n_marks_submit(const q3n_frame *f, qa_error *error)
{
    if(!q3ne_current(f,error))return false;
    q3n_events *o=f->events;
    if(o->busy)return q3ne_fail(error,QA_ERROR_ARGUMENT,"Native Q3 marks are already producing");
    o->busy=true; bool ok=submit(f,error); o->busy=false; return ok;
}
