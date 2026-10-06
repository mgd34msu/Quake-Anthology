#include "native_components.h"
#include "component_scene.h"
#include "qa/application_q3_components.h"
#include "qa/q3_assets_save.h"
#include "qa/q3_model_opening.h"

typedef struct component_child {
    qa_application_q3_component_draw draw;
    qa_application_q3_component_body_lease *lease;
    qa_application_q3_component_bodies bodies;
} component_child;
typedef struct component_pose {
    qa_actor_id actor;
    qa_application_q3_body_part part;
    qa_q3_ref_entity ref;
    const qa_resource *resource;
    bool base;
} component_pose;
struct frontend_native_components {
    const q3n_frame *frame;
    component_child *children;
    size_t count;
    component_pose *poses;
    size_t pose_count;
    qa_scene_light *lights,*projected;
    uint32_t first_order,packet_order;
    bool submitted;
};
static bool current(frontend_native_q3 *row,qa_error *error)
{
    struct frontend_native_components *owner=row->components;
    if(!owner || !row->frame_active || !frontend_native_q3_cut(row,owner->frame,error)) return false;
    for(size_t i=0;i<owner->count;++i)
        if(!qa_application_q3_component_draw_current(row->frontend->application,&owner->children[i].draw) ||
            (owner->children[i].draw.bodies && !qa_application_q3_component_bodies_current(&owner->children[i].bodies)))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Native component Draw changed its genuine private output");
    return true;
}
void frontend_native_components_release(frontend_native_q3 *row)
{
    struct frontend_native_components *owner=row->components;
    if(!owner) return;
    for(size_t i=owner->count;i>0;--i) qa_application_q3_component_bodies_return(&owner->children[i-1].lease);
    free(owner->children); free(owner->poses); free(owner->lights); free(owner->projected);
    free(owner); row->components=NULL;
}
bool frontend_native_components_prepare(frontend_native_q3 *row,const q3n_frame *frame,qa_error *error)
{
    if(row->components || !row->frame_active || !frontend_native_q3_cut(row,frame,error)) return false;
    size_t count=qa_application_q3_component_scene_count(frame->application);
    if(!count) return true;
    if(count>SIZE_MAX/sizeof(component_child)) return frontend_fail(error,QA_ERROR_MEMORY,"Native component roster exceeds allocation");
    struct frontend_native_components *owner=calloc(1,sizeof(*owner));
    if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual native component Draw");
    owner->frame=frame; row->components=owner;
    owner->children=calloc(count,sizeof(*owner->children));
    if(!owner->children) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining native component output roster");
    for(size_t i=0;i<count;++i) {
        component_child *child=&owner->children[i];
        if(!qa_application_q3_component_draw_prepare(frame->application,i,row->view.seat,frame->viewing_actor,
            &frame->refdef.origin,frame->refdef.axis,frame->time,frame->frame_milliseconds,
            frame->source.source_frame.number,&child->draw,error)) return false;
        ++owner->count;
        if(child->draw.bodies && (!qa_application_q3_component_bodies_borrow(child->draw.bodies,&child->lease,&child->bodies,error) ||
            child->bodies.assets!=child->draw.assets)) return false;
    }
    return current(row,error);
}
bool frontend_native_components_body(frontend_native_q3 *row,const q3n_frame *frame,qa_actor_id actor,uint32_t part,
    const qa_q3_ref_entity *ref,bool base,bool *consumed,qa_error *error)
{
    *consumed=false;
    struct frontend_native_components *owner=row->components;
    if(!owner) return true;
    if(owner->frame!=frame || !current(row,error) || part>3) return false;
    bool selected=false;
    for(size_t i=0;i<owner->count;++i) for(size_t j=0;j<owner->children[i].bodies.count;++j) {
        qa_application_q3_component_actor body;
        if(!qa_application_q3_component_body_at(&owner->children[i].bodies,j,&body)) return false;
        if(qa_actor_id_equal(actor,body.actor)) selected=true;
    }
    if(!selected) return true;
    qa_q3_model_opening opening;
    if(!ref || ref->kind!=QA_Q3_REF_MODEL || ref->model<=0 ||
        !qa_q3_assets_model_opening(row->view.assets,(size_t)(ref->model-1),QA_Q3_MODEL_PRIMARY_OPENING,&opening,error)) return false;
    if(owner->pose_count==SIZE_MAX/sizeof(*owner->poses)) return false;
    component_pose *poses=realloc(owner->poses,(owner->pose_count+1)*sizeof(*poses));
    if(!poses) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual native component body pose");
    owner->poses=poses;
    static const qa_application_q3_body_part parts[4]={QA_APPLICATION_Q3_BODY_LOWER,QA_APPLICATION_Q3_BODY_UPPER,
        QA_APPLICATION_Q3_BODY_HEAD,QA_APPLICATION_Q3_BODY};
    poses[owner->pose_count++]=(component_pose){actor,parts[part],*ref,opening.resource,base};
    *consumed=true; return current(row,error);
}
void frontend_native_components_clear(frontend_native_q3 *row)
{ if(row->components) row->components->pose_count=0; }
bool frontend_native_components_scene_prepare(frontend_native_q3 *row,qa_q3_scene_options *options,qa_error *error)
{
    struct frontend_native_components *owner=row->components;
    if(!owner || options->world.no_world) return true;
    if(!current(row,error) || options->first_entity>1022 || owner->pose_count>1022-options->first_entity) return false;
    owner->first_order=options->first_entity; options->first_entity+=(uint32_t)owner->pose_count;
    owner->packet_order=options->first_entity;
    size_t entities=0,lights=0;
    for(size_t i=0;!owner->submitted && i<owner->count;++i) {
        const component_child *child=&owner->children[i]; size_t packets=0;
        if(!frontend_component_scene_packet_count(row->frontend,child->draw.frontend_identity,child->draw.sequence,&packets,error)) return false;
        for(size_t j=0;j<packets;++j) {
            frontend_component_scene_packet packet;
            if(!frontend_component_scene_packet_read(row->frontend,child->draw.frontend_identity,child->draw.sequence,j,&packet,error) ||
                packet.entity_count>SIZE_MAX-entities || packet.light_count>SIZE_MAX-lights) return false;
            entities+=packet.entity_count; lights+=packet.light_count;
        }
    }
    if(entities>1022-options->first_entity || lights>SIZE_MAX-options->world.light_count ||
        options->world.light_count+lights>SIZE_MAX/sizeof(*owner->lights)) return false;
    options->first_entity+=(uint32_t)entities;
    if(!lights) return true;
    size_t count=options->world.light_count+lights;
    qa_scene_light *merged=malloc(count*sizeof(*merged)),*projected=malloc(32*sizeof(*projected));
    if(!merged || !projected) { free(merged); free(projected); return frontend_fail(error,QA_ERROR_MEMORY,"Retaining native component scene lights"); }
    size_t used=options->world.light_count,projected_count=options->world.projected_light_count;
    if(projected_count>32) { free(merged); free(projected); return false; }
    if(used) memcpy(merged,options->world.lights,used*sizeof(*merged));
    if(projected_count) memcpy(projected,options->world.projected_lights,projected_count*sizeof(*projected));
    for(size_t i=0;i<owner->count;++i) {
        const component_child *child=&owner->children[i]; size_t packets=0;
        if(!frontend_component_scene_packet_count(row->frontend,child->draw.frontend_identity,child->draw.sequence,&packets,error)) { free(merged); free(projected); return false; }
        for(size_t j=0;j<packets;++j) {
            frontend_component_scene_packet packet;
            if(!frontend_component_scene_packet_read(row->frontend,child->draw.frontend_identity,child->draw.sequence,j,&packet,error)) { free(merged); free(projected); return false; }
            if(packet.light_count) memcpy(merged+used,packet.lights,packet.light_count*sizeof(*merged));
            used+=packet.light_count;
            for(size_t k=0;k<packet.light_count && projected_count<32;++k) projected[projected_count++]=packet.lights[k];
        }
    }
    free(owner->lights); free(owner->projected); owner->lights=merged; owner->projected=projected;
    options->world.lights=merged; options->world.light_count=count;
    options->world.projected_lights=projected; options->world.projected_light_count=projected_count;
    return current(row,error);
}
static bool pass_matches(qa_application_q3_body_part pose,qa_application_q3_body_part part)
{ return pose==QA_APPLICATION_Q3_BODY || part==QA_APPLICATION_Q3_BODY || pose==part; }
bool frontend_native_components_scene_submit(frontend_native_q3 *row,const qa_q3_scene_options *options,qa_scene_frame *frame,qa_error *error)
{
    struct frontend_native_components *owner=row->components;
    if(!owner || options->world.no_world) return true;
    if(!current(row,error)) return false;
    for(size_t i=0;i<owner->pose_count;++i) {
        const component_pose *pose=&owner->poses[i]; bool visible=true;
        for(size_t j=0;j<owner->count;++j) for(size_t k=0;k<owner->children[j].bodies.count;++k) {
            qa_application_q3_component_actor body;
            if(!qa_application_q3_component_body_at(&owner->children[j].bodies,k,&body)) return false;
            if(!qa_actor_id_equal(pose->actor,body.actor)) continue;
            if(!body.count) visible=false;
            for(size_t m=0;m<body.count;++m) if(pass_matches(pose->part,body.parts[m].part) && !body.parts[m].base) visible=false;
        }
        uint32_t order=owner->first_order+(uint32_t)i;
        if((!pose->base || visible) && !qa_q3_presentation_source_body_pass(row->view.presentation,&pose->ref,
            row->view.assets,&pose->ref,owner->frame->time,options,order,frame,error)) return false;
        bool posed=false;
        for(size_t j=0;j<i;++j) if(qa_actor_id_equal(owner->poses[j].actor,pose->actor) &&
            owner->poses[j].part==pose->part && owner->poses[j].resource==pose->resource) { posed=true; break; }
        if(posed) continue;
        for(size_t j=0;j<owner->count;++j) for(size_t k=0;k<owner->children[j].bodies.count;++k) {
            const component_child *child=&owner->children[j]; qa_application_q3_component_actor body;
            if(!qa_application_q3_component_body_at(&child->bodies,k,&body)) return false;
            if(!qa_actor_id_equal(pose->actor,body.actor)) continue;
            for(size_t m=0;m<body.count;++m) {
                const qa_application_q3_component_part *part=&body.parts[m];
                if(!pass_matches(pose->part,part->part)) continue;
                for(size_t n=0;n<part->count;++n) {
                    size_t occurrence=0; bool seen=false,equal=false;
                    for(size_t x=0;x<=m;++x) {
                        const qa_application_q3_component_part *prior=&body.parts[x];
                        if(prior->helper!=part->helper) continue;
                        size_t limit=x==m?n:prior->count;
                        for(size_t y=0;y<limit;++y) {
                            if(!qa_q3_presentation_source_body_material_equal(row->view.presentation,child->bodies.assets,
                                part->passes+n,prior->passes+y,&equal,error)) return false;
                            if(equal) ++occurrence;
                        }
                    }
                    for(size_t x=0;x<m && !seen;++x) {
                        const qa_application_q3_component_part *prior=&body.parts[x];
                        if(prior->helper==part->helper || !pass_matches(pose->part,prior->part)) continue;
                        size_t matches=0;
                        for(size_t y=0;y<prior->count;++y) {
                            if(!qa_q3_presentation_source_body_material_equal(row->view.presentation,child->bodies.assets,
                                part->passes+n,prior->passes+y,&equal,error)) return false;
                            if(equal) ++matches;
                        }
                        if(matches>occurrence) seen=true;
                    }
                    if(!seen && !qa_q3_presentation_source_body_pass(row->view.presentation,&pose->ref,
                        child->bodies.assets,part->passes+n,child->bodies.time_ms,options,order,frame,error)) return false;
                }
            }
        }
    }
    uint32_t order=owner->packet_order;
    for(size_t i=0;!owner->submitted && i<owner->count;++i) {
        const component_child *child=&owner->children[i]; size_t packets=0;
        if(!frontend_component_scene_packet_count(row->frontend,child->draw.frontend_identity,child->draw.sequence,&packets,error)) return false;
        for(size_t j=0;j<packets;++j) {
            frontend_component_scene_packet packet;
            if(!frontend_component_scene_packet_read(row->frontend,child->draw.frontend_identity,child->draw.sequence,j,&packet,error)) return false;
            for(size_t k=0;k<packet.entity_count;++k,++order)
                if(!qa_q3_presentation_source_component_entity(row->view.presentation,child->draw.assets,packet.entities+k,
                    packet.definition.time,options,order,frame,error)) return false;
            for(size_t k=0;k<packet.polygon_count;++k) {
                const qa_q3_scene_polygon *polygon=&packet.polygons[k];
                if(polygon->first>packet.vertex_count || polygon->count>packet.vertex_count-polygon->first) return false;
                if(!qa_q3_presentation_source_component_poly(row->view.presentation,child->draw.assets,polygon->shader,
                    packet.vertices+polygon->first,polygon->count,&polygon->fog,packet.definition.time,options,frame,error)) return false;
            }
        }
    }
    owner->submitted=true; return current(row,error);
}
bool frontend_native_components_hud(frontend_native_q3 *row,qa_error *error)
{
    qa_frontend *f=row->frontend;
    qa_application_native_q3_presentation source;
    if(row->components || !frontend_native_q3_current(row) ||
        !qa_application_native_q3_presentation_read(f->application,row->view.source_owner,&source,error)) return false;
    for(size_t i=0;i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view view;
        if(!frontend_component_scene_read(f,i,&view,error)) return false;
        if(view.retired || view.origin!=APPLICATION_Q3_COMPONENT_SCENE_LOCAL || !view.begun ||
            view.physical_seat!=row->view.seat || view.sequence!=source.source_frame.number ||
            !qa_actor_id_equal(view.viewer,frontend_native_q3_actor(row))) continue;
        if(!qa_application_q3_component_scene_hud(f->application,view.identity,view.physical_seat,view.viewer,view.sequence,error) ||
            !frontend_component_scene_pictures(f,view.identity,view.sequence,row->view.presentation,&f->frame,error)) return false;
    }
    return frontend_native_q3_current(row);
}
