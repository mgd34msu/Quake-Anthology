#include "source_companion_legacy.h"
#include "q3_color_policy.h"
#include "qa/q3_presentation_save.h"


bool frontend_source_companion_lights_merge(qa_arena *storage,qa_scene_world_input *world,
    size_t count,frontend_source_companion_light_read read,void *context,qa_error *error)
{
    if(!count) return true;
    if((world->light_count && !world->lights) || world->projected_light_count>32 ||
        (world->projected_light_count && !world->projected_lights) ||
        count>SIZE_MAX/sizeof(qa_scene_light) || world->light_count>SIZE_MAX/sizeof(qa_scene_light)-count)
        return frontend_fail(error,QA_ERROR_FORMAT,"Companion lights exceed the recipient spans");
    size_t total=world->light_count+count,projected=world->projected_light_count;
    size_t added=count<32-projected?count:32-projected;
    qa_scene_light *lights=qa_arena_alloc(storage,total*sizeof(*lights),_Alignof(qa_scene_light),error);
    qa_scene_light *projected_lights=added?qa_arena_alloc(storage,(projected+added)*sizeof(*projected_lights),_Alignof(qa_scene_light),error):NULL;
    if(!lights || (added && !projected_lights)) return false;
    if(world->light_count) memcpy(lights,world->lights,world->light_count*sizeof(*lights));
    if(added && projected) memcpy(projected_lights,world->projected_lights,projected*sizeof(*projected_lights));
    for(size_t i=0;i<count;++i) {
        qa_scene_light light;const qa_q3_refdef *definition;bool view;
        if(!read(context,&light,&definition,&view,error)) return false;
        if(view) light.origin=qa_axis_remap_point(definition->origin,definition->axis,world->view.origin,world->view.axis,light.origin);
        lights[world->light_count+i]=light;
        if(i<added) projected_lights[projected+i]=light;
    }
    world->lights=lights;world->light_count=total;
    if(added) { world->projected_lights=projected_lights;world->projected_light_count=projected+added; }
    return true;
}

typedef struct legacy_light_cursor {
    qa_frontend *frontend;
    const frontend_source_companion_view *capture;
    qa_actor_id actor;
    frontend_source_companion_packet packet;
    size_t next_packet,next_light;
    bool skip_view;
} legacy_light_cursor;
static bool legacy_light_read(void *context,qa_scene_light *light,const qa_q3_refdef **definition,
    bool *view,qa_error *error)
{
    legacy_light_cursor *cursor=context;
    for(;;) {
        while(cursor->next_light<cursor->packet.light_count) {
            size_t i=cursor->next_light++;
            if(!cursor->packet.light_actors || !qa_actor_id_equal(cursor->packet.light_actors[i],cursor->actor)) continue;
            *view=cursor->packet.light_views && cursor->packet.light_views[i];
            if(*view && cursor->skip_view) continue;
            *light=cursor->packet.lights[i];*definition=&cursor->packet.definition;return true;
        }
        if(cursor->next_packet>=cursor->capture->packet_count) return false;
        if(!frontend_source_companion_packet_read(cursor->frontend,cursor->capture,cursor->next_packet++, &cursor->packet,error)) return false;
        cursor->next_light=cursor->packet.definition.flags&1?cursor->packet.light_count:0;
    }
}


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
    legacy_light_cursor cursor={.frontend=f,.capture=&capture,.actor=actor,.skip_view=input->view.clip_enabled};
    if(!frontend_source_companion_lights_merge(&f->frame.storage,input,added,legacy_light_read,&cursor,error) ||
        !frontend_source_companion_current(f,&capture)) return false;
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
