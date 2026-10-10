#include "source_companion_legacy.h"
#include "q3_color_policy.h"
#include "qa/q3_presentation_save.h"

struct frontend_source_companion_legacy {
    qa_frontend *frontend;
    qa_actor_id actor;
    qa_scene_world *world;
    frontend_source_companion_view capture;
    qa_q3_presentation *presentation;
    qa_application_q3_weapon_models models;
    bool registered;
    qa_q3_scene_options options;
    qa_scene_light *lights,*projected;
};
static qa_vec3 vector(const qa_vec3 source[3],const qa_vec3 target[3],qa_vec3 value)
{
    qa_vec3 result={0};
    for (size_t i=0;i<3;++i) result=qa_vec_add(result,qa_vec_scale(target[i],qa_vec_dot(value,source[i])));
    return result;
}
static qa_vec3 point(const qa_q3_refdef *source,const qa_scene_view *target,qa_vec3 value)
{ return qa_vec_add(target->origin,vector(source->axis,target->axis,qa_vec_sub(value,source->origin))); }
static bool owned(const frontend_source_companion_legacy *owner,const frontend_source_companion_packet *packet,
    size_t ordinal,bool *view)
{
    const qa_q3_ref_entity *ref=packet->entities+ordinal;
    bool match=packet->entity_actors && qa_actor_id_equal(packet->entity_actors[ordinal],owner->actor);
    *view=match && packet->entity_views && packet->entity_views[ordinal];
    bool attributed=packet->entity_actors && packet->entity_actors[ordinal].registry!=0;
    if (!match && !attributed && owner->registered && (ref->flags&4) && ref->kind==QA_Q3_REF_MODEL && ref->model>0 &&
        (ref->model==owner->models.gun || ref->model==owner->models.hands ||
        ref->model==owner->models.barrel || ref->model==owner->models.flash)) match=*view=true;
    return match;
}
void frontend_source_companion_legacy_dispose(frontend_source_companion_legacy **slot)
{
    if (!slot || !*slot) return;
    free((*slot)->lights); free((*slot)->projected); free(*slot); *slot=NULL;
}
bool frontend_source_companion_legacy_prepare(qa_frontend *f,uint32_t physical,qa_actor_id actor,
    qa_scene_world *world,qa_game_family family,qa_scene_world_input *input,
    frontend_source_companion_legacy **out,qa_error *error)
{
    if (!f || !world || !input || !out || *out || input->view.seat!=physical ||
        (family!=QA_GAME_Q1 && family!=QA_GAME_Q2))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Legacy companion requires its actual recipient world and prepared view");
    frontend_source_companion_view capture; bool present=false;
    if (!frontend_source_companion_read(f,physical,actor,&capture,&present,error)) return false;
    if (!present) return true;
    qa_application_equipment_view selection;
    if (!qa_application_equipment_read(f->application,actor,&selection,error) ||
        !selection.selected || !selection.original_qvm || selection.provider!=capture.receipt.client.source.source_owner)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Legacy companion left its actual selected GAME and full actor");
    frontend_source_companion_legacy *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining completed legacy companion submission");
    *out=owner; owner->frontend=f; owner->actor=actor; owner->world=world; owner->capture=capture;
    if (!frontend_source_companion_presentation_read(f,&capture,&owner->presentation,error) ||
        !qa_application_equipment_q3_models_read(f->application,&selection,capture.receipt.client.source.receiver,
            capture.receipt.client.source.seat,&owner->models,&owner->registered,error)) return false;
    qa_q3_presentation_binding binding;
    if (!qa_q3_presentation_binding_read(owner->presentation,&binding,error)) return false;
    owner->options=(qa_q3_scene_options){.world=*input,.world_family=family,
        .lod_scale=binding.options.lod_scale,.lod_bias=binding.options.lod_bias,
        .ambient_scale=.6f,.directed_scale=1,.near_clip=binding.options.near_clip,
        .rail={.core_width=binding.options.rail_core_width,.ring_width=binding.options.rail_ring_width,
            .segment_length=binding.options.rail_segment_length},.split_screen=f->options.seats>1};
    owner->options.world.identity_light=1;
    qa_scene_state_default(&owner->options.state);
    if (!frontend_q3_generic_recipient(f,&owner->options.world,error)) return false;
    size_t added=0;
    for (size_t i=0;i<capture.packet_count;++i) {
        frontend_source_companion_packet packet;
        if (!frontend_source_companion_packet_read(f,&capture,i,&packet,error)) return false;
        if (packet.definition.flags&1) continue;
        for (size_t j=0;j<packet.light_count;++j) {
            if (!packet.light_actors || !qa_actor_id_equal(packet.light_actors[j],actor) ||
                (packet.light_views && packet.light_views[j] && input->view.clip_enabled)) continue;
            if (added==SIZE_MAX) return false;
            ++added;
        }
    }
    if (!added) return frontend_source_companion_current(f,&capture);
    if ((input->light_count && !input->lights) || (input->projected_light_count && !input->projected_lights) ||
        input->projected_light_count>32 || added>SIZE_MAX/sizeof(*owner->lights) ||
        input->light_count>SIZE_MAX/sizeof(*owner->lights)-added)
        return frontend_fail(error,QA_ERROR_FORMAT,"Legacy companion lights exceed the actual recipient spans");
    size_t total=input->light_count+added,projected_added=added<32-input->projected_light_count?added:32-input->projected_light_count;
    owner->lights=malloc(total*sizeof(*owner->lights));
    if (projected_added) owner->projected=malloc((input->projected_light_count+projected_added)*sizeof(*owner->projected));
    if (!owner->lights || (projected_added && !owner->projected))
        return frontend_fail(error,QA_ERROR_MEMORY,"Combining actual legacy recipient lights");
    if (input->light_count) memcpy(owner->lights,input->lights,input->light_count*sizeof(*owner->lights));
    if (projected_added && input->projected_light_count)
        memcpy(owner->projected,input->projected_lights,input->projected_light_count*sizeof(*owner->projected));
    size_t at=input->light_count,projected_at=0;
    for (size_t i=0;i<capture.packet_count;++i) {
        frontend_source_companion_packet packet;
        if (!frontend_source_companion_packet_read(f,&capture,i,&packet,error)) return false;
        if (packet.definition.flags&1) continue;
        for (size_t j=0;j<packet.light_count;++j) {
            if (!packet.light_actors || !qa_actor_id_equal(packet.light_actors[j],actor)) continue;
            bool view=packet.light_views && packet.light_views[j];
            if (view && input->view.clip_enabled) continue;
            qa_scene_light light=packet.lights[j];
            if (view) light.origin=point(&packet.definition,&input->view,light.origin);
            owner->lights[at++]=light;
            if (projected_at<projected_added) owner->projected[input->projected_light_count+projected_at]=light;
            ++projected_at;
        }
    }
    if (!frontend_source_companion_current(f,&capture)) return false;
    input->lights=owner->lights; input->light_count=total;
    if (projected_added) { input->projected_lights=owner->projected; input->projected_light_count+=projected_added; }
    owner->options.world=*input; owner->options.world.identity_light=1;
    return frontend_q3_generic_recipient(f,&owner->options.world,error);
}
bool frontend_source_companion_legacy_submit(frontend_source_companion_legacy *owner,qa_scene_frame *frame,qa_error *error)
{
    if (!owner) return true;
    qa_frontend *f=owner->frontend;
    if (frame!=&f->frame || !frontend_source_companion_current(f,&owner->capture))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Legacy companion submission lost its actual retained output");
    uint32_t order=0;
    for (size_t i=0;i<owner->capture.packet_count;++i) {
        frontend_source_companion_packet packet;
        if (!frontend_source_companion_packet_read(f,&owner->capture,i,&packet,error)) return false;
        if (packet.definition.flags&1) continue;
        for (size_t j=0;j<packet.entity_count;++j) {
            bool view=false;
            if (!owned(owner,&packet,j,&view) || (view && owner->options.world.view.clip_enabled)) continue;
            qa_q3_ref_entity ref=packet.entities[j];
            if (view) {
                ref.origin=point(&packet.definition,&owner->options.world.view,ref.origin);
                ref.old_origin=point(&packet.definition,&owner->options.world.view,ref.old_origin);
                ref.lighting_origin=point(&packet.definition,&owner->options.world.view,ref.lighting_origin);
                for (size_t k=0;k<3;++k) ref.axis[k]=vector(packet.definition.axis,owner->options.world.view.axis,ref.axis[k]);
            }
            if (!qa_q3_presentation_completed_entity(owner->presentation,owner->capture.assets,owner->world,
                &ref,packet.definition.time,&owner->options,order++,frame,error)) return false;
        }
        for (size_t j=0;j<packet.polygon_count;++j) {
            if (!packet.polygon_actors || !qa_actor_id_equal(packet.polygon_actors[j],owner->actor)) continue;
            bool view=packet.polygon_views && packet.polygon_views[j];
            if (view && owner->options.world.view.clip_enabled) continue;
            const qa_q3_scene_polygon *polygon=packet.polygons+j;
            if (polygon->first>packet.vertex_count || polygon->count>packet.vertex_count-polygon->first ||
                polygon->count>SIZE_MAX/sizeof(*packet.vertices)) return false;
            const qa_scene_vertex *vertices=polygon->count?packet.vertices+polygon->first:NULL;
            qa_scene_vertex *transformed=NULL;
            if (view && polygon->count) {
                transformed=malloc(polygon->count*sizeof(*transformed));
                if (!transformed) return frontend_fail(error,QA_ERROR_MEMORY,"Transforming actual Source view polygon");
                memcpy(transformed,vertices,polygon->count*sizeof(*transformed));
                for (size_t k=0;k<polygon->count;++k)
                    transformed[k].position=point(&packet.definition,&owner->options.world.view,transformed[k].position);
                vertices=transformed;
            }
            bool okay=qa_q3_presentation_completed_poly(owner->presentation,owner->capture.assets,owner->world,
                polygon->shader,vertices,polygon->count,&polygon->fog,packet.definition.time,&owner->options,frame,error);
            free(transformed); if (!okay) return false;
        }
    }
    return frontend_source_companion_current(f,&owner->capture);
}
