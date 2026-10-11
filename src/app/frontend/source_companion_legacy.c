#include "source_companion_legacy.h"
#include "scene_lights.h"
#include "q3_color_policy.h"
#include "qa/q3_presentation_save.h"


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
    if(!frontend_scene_lights_companion_merge(&f->frame.storage,input,owner->selection,
        input->view.clip_enabled,error) || !frontend_source_companion_current(f,&capture)) return false;
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
