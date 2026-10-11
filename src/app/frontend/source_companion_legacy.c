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
    const frontend_source_companion_selection *selection;
    size_t next;
    bool skip_view;
} legacy_light_cursor;
static bool legacy_light_read(void *context,qa_scene_light *light,const qa_q3_refdef **definition,
    bool *view,qa_error *error)
{
    (void)error;
    legacy_light_cursor *cursor=context;
    while(cursor->next<cursor->selection->light_count) {
        const frontend_source_companion_light *row=cursor->selection->lights+cursor->next++;
        if(row->view && cursor->skip_view) continue;
        *light=row->light; *definition=&row->definition; *view=row->view;
        return true;
    }
    return false;
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
    owner->frontend=f; owner->actor=actor; owner->world=world; owner->capture=capture;
    if (!frontend_source_companion_presentation_read(f,&capture,&owner->presentation,error) ||
        !frontend_source_companion_selection_read(f,&capture,&owner->selection,error)) return false;
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
    for(size_t i=0;i<owner->selection->light_count;++i)
        if(!owner->selection->lights[i].view || !input->view.clip_enabled) ++added;
    if(!added) return frontend_source_companion_current(f,&capture);
    legacy_light_cursor cursor={.selection=owner->selection,.skip_view=input->view.clip_enabled};
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
    for(size_t i=0;i<owner->selection->ref_count;++i) {
        const frontend_source_companion_ref *row=owner->selection->refs+i;
        if(row->view && owner->options.world.view.clip_enabled) continue;
        qa_q3_ref_entity ref=row->ref;
        if(row->view) qa_q3_ref_entity_remap(&ref,row->definition.origin,row->definition.axis,
            owner->options.world.view.origin,owner->options.world.view.axis);
        if(!qa_q3_presentation_completed_entity(owner->presentation,owner->capture.assets,owner->world,
            &ref,row->definition.time,&owner->options,order++,frame,error)) return false;
    }
    for(size_t i=0;i<owner->selection->polygon_count;++i) {
        const frontend_source_companion_polygon *row=owner->selection->polygons+i;
        if(row->view && owner->options.world.view.clip_enabled) continue;
        const qa_scene_vertex *vertices=row->vertices;
        if(row->view && row->polygon.count) {
            qa_scene_vertex *transformed=qa_arena_alloc(&frame->storage,row->polygon.count*sizeof(*transformed),
                _Alignof(qa_scene_vertex),error);
            if(!transformed) return false;
            qa_scene_vertices_remap(transformed,vertices,row->polygon.count,
                row->definition.origin,row->definition.axis,owner->options.world.view.origin,owner->options.world.view.axis);
            vertices=transformed;
        }
        if(!qa_q3_presentation_completed_poly(owner->presentation,owner->capture.assets,owner->world,
            row->polygon.shader,vertices,row->polygon.count,&row->polygon.fog,row->definition.time,
            &owner->options,frame,error)) return false;
    }
    return frontend_source_companion_current(f,&owner->capture);
}
