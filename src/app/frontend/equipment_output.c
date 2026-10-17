#include "internal.h"
#include "equipment_output.h"
#include "qa/q3_asset_shader.h"
#include "qa/q3_assets_save.h"
#include <math.h>

bool frontend_equipment_output_collect(frontend_equipment_output *output,
    qa_q3_presentation_assets *assets,const qa_q3_ref_entity *ref,qa_error *error)
{
    if (!ref || assets!=output->assets || output->count==output->capacity)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Selected output exceeds its actual registry or capacity");
    output->refs[output->count++]=*ref;
    return true;
}
void frontend_equipment_output_parent(frontend_equipment_output *output,
    const qa_q3_ref_entity *parent,bool alpha)
{
    for (size_t i=0;i<output->count;++i) {
        output->refs[i].flags=parent->flags;
        if (alpha) output->refs[i].color[3]=parent->color[3];
        output->refs[i].lighting_origin=parent->lighting_origin;
        output->refs[i].shadow_plane=parent->shadow_plane;
    }
}
size_t frontend_equipment_output_count(const frontend_equipment_output *output)
{
    return !output?0:!output->source_style?output->count:
        output->base_count*output->pass_count+output->count-output->base_count;
}
bool frontend_equipment_output_source_style(frontend_equipment_output *output,
    qa_q3_presentation_assets *assets,const qa_q3_ref_entity *parent,qa_error *error)
{
    if (!output || output->view || output->source_style || !assets || !parent ||
        !output->current(output->context) || !qa_q3_assets_idle(assets) ||
        parent->kind!=QA_Q3_REF_MODEL || !qa_vec_finite(parent->lighting_origin) ||
        !isfinite(parent->shadow_plane))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Selected source style requires its actual torso and registries");
    size_t base;
    if (!output->base_group(output->context,&base,error)) return false;
    frontend_equipment_output_parent(output,parent,false);
    output->source_assets=assets; output->base_count=base; output->source_style=true;
    return true;
}
bool frontend_equipment_output_source_pass(frontend_equipment_output *output,
    const qa_q3_ref_entity *pass,qa_error *error)
{
    const qa_material *material;
    if (!output || !output->source_style || !pass || !isfinite(pass->shader_time) ||
        !output->current(output->context))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Selected source pass lost its actual owner");
    if (!qa_q3_assets_shader_read(output->source_assets,pass->custom_shader,&material,error)) return false;
    size_t remaining=output->count-output->base_count;
    if (output->pass_count>=(1022-remaining)/output->base_count)
        return frontend_fail(error,QA_ERROR_FORMAT,"Selected held output exceeds its source orders");
    if (output->pass_count==output->pass_capacity) {
        size_t capacity=output->pass_capacity?output->pass_capacity*2:4;
        if (capacity>1022) capacity=1022;
        qa_q3_ref_entity *passes=qa_arena_grow(output->storage,output->passes,
            output->pass_count*sizeof(*passes),capacity*sizeof(*passes),_Alignof(qa_q3_ref_entity),error);
        if (!passes) return false;
        output->passes=passes; output->pass_capacity=capacity;
    }
    output->passes[output->pass_count++]=*pass;
    return true;
}
static qa_q3_ref_entity shifted(const qa_q3_ref_entity *source,qa_vec3 offset)
{
    qa_q3_ref_entity ref=*source;
    ref.origin=qa_vec_add(ref.origin,offset); ref.old_origin=qa_vec_add(ref.old_origin,offset);
    ref.lighting_origin=qa_vec_add(ref.lighting_origin,offset); ref.shadow_plane+=offset.z;
    return ref;
}
bool frontend_equipment_output_submit(frontend_equipment_output *output,
    qa_q3_presentation *presentation,qa_vec3 offset,const qa_q3_scene_options *options,
    uint32_t order,qa_scene_frame *frame,qa_error *error)
{
    if (!output || !output->current(output->context) || !presentation || !options || !frame ||
        !qa_vec_finite(offset) || order>1022 || frontend_equipment_output_count(output)>1022-order)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Selected submission lost its actual owner or source orders");
    qa_q3_ref_entity local_pose[7];
    const qa_q3_ref_entity *refs=output->refs;
    if (output->carrier.model) {
        qa_model_transform transform;
        bool visible;
        if (!frontend_held_carrier_sample(&output->carrier,&options->world.view,
            frame,&transform,&visible,error)) return false;
        if (!visible) return true;
        qa_q3_ref_entity *posed=local_pose;
        if (output->count>sizeof(local_pose)/sizeof(*local_pose)) {
            posed=qa_arena_alloc(&frame->storage,output->count*sizeof(*posed),
                _Alignof(qa_q3_ref_entity),error);
            if (!posed) return false;
        }
        for (size_t i=0;i<output->count;++i)
            frontend_held_carrier_ref(&transform,output->refs+i,posed+i);
        refs=posed;
    }
    for (size_t i=0;i<output->count;++i) {
        qa_q3_ref_entity ref=shifted(refs+i,offset);
        if (!qa_vec_finite(ref.origin) || !qa_vec_finite(ref.old_origin) ||
            !qa_vec_finite(ref.lighting_origin) || !isfinite(ref.shadow_plane))
            return frontend_fail(error,QA_ERROR_FORMAT,"Selected offset exceeds finite scene coordinates");
    }
    size_t first=0;
    if (output->source_style) {
        for (size_t pass=0;pass<output->pass_count;++pass)
            for (size_t part=0;part<output->base_count;++part) {
                qa_q3_ref_entity ref=shifted(refs+part,offset);
                if (!qa_q3_presentation_selected_registered_pass(presentation,output->assets,&ref,
                    output->source_assets,output->passes+pass,options,order++,frame,error)) return false;
            }
        first=output->base_count;
    }
    for (size_t i=first;i<output->count;++i) {
        qa_q3_ref_entity ref=shifted(refs+i,offset);
        if (!qa_q3_presentation_selected_registered(presentation,output->assets,&ref,
            options,order++,frame,error)) return false;
    }
    return true;
}
void frontend_equipment_output_destroy(frontend_equipment_output *output)
{ if (output) output->release(output->context); }
