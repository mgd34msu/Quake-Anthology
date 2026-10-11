#include "source_companion_legacy.h"
#include "q3_color_policy.h"
#include "qa/q3_presentation_save.h"


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
bool frontend_source_companion_legacy_prepare(qa_frontend *f,uint32_t physical,qa_actor_id actor,
    qa_scene_world *world,qa_game_family family,qa_scene_world_input *input,
    frontend_source_companion_legacy *owner,qa_error *error)
{
    if (!f || !world || !input || !owner || input->view.seat!=physical ||
        (family!=QA_GAME_Q1 && family!=QA_GAME_Q2))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Legacy companion requires its actual recipient world and prepared view");
    *owner=(frontend_source_companion_legacy){0};
    frontend_source_companion_view capture; bool present=false;
    if (!frontend_source_companion_read(f,physical,actor,&capture,&present,error)) return false;
    if (!present) return true;
    qa_application_equipment_view selection;
    if (!qa_application_equipment_read(f->application,actor,&selection,error) ||
        !selection.selected || !selection.original_qvm || selection.provider!=capture.receipt.client.source.source_owner)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Legacy companion left its actual selected GAME and full actor");
    owner->frontend=f; owner->actor=actor; owner->world=world; owner->capture=capture;
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
    owner->lights=qa_arena_alloc(&f->frame.storage,total*sizeof(*owner->lights),_Alignof(qa_scene_light),error);
    if (projected_added) owner->projected=qa_arena_alloc(&f->frame.storage,(input->projected_light_count+projected_added)*sizeof(*owner->projected),_Alignof(qa_scene_light),error);
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
            if (view) light.origin=qa_axis_remap_point(packet.definition.origin,packet.definition.axis,input->view.origin,input->view.axis,light.origin);
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
    if (!owner || !owner->frontend) return true;
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
                qa_q3_ref_entity_remap(&ref,packet.definition.origin,packet.definition.axis,
                    owner->options.world.view.origin,owner->options.world.view.axis);
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
                transformed=qa_arena_alloc(&frame->storage,polygon->count*sizeof(*transformed),_Alignof(qa_scene_vertex),error);
                if (!transformed) return frontend_fail(error,QA_ERROR_MEMORY,"Transforming actual Source view polygon");
                qa_scene_vertices_remap(transformed,vertices,polygon->count,
                    packet.definition.origin,packet.definition.axis,owner->options.world.view.origin,owner->options.world.view.axis);
                vertices=transformed;
            }
            bool okay=qa_q3_presentation_completed_poly(owner->presentation,owner->capture.assets,owner->world,
                polygon->shader,vertices,polygon->count,&polygon->fog,packet.definition.time,&owner->options,frame,error);
            if (!okay) return false;
        }
    }
    return frontend_source_companion_current(f,&owner->capture);
}
