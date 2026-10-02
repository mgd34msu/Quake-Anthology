#ifndef QA_RENDER_SAVE_FIELDS_H
#define QA_RENDER_SAVE_FIELDS_H
#include "qa/render_save.h"
#include "qa/source_save.h"
#include <math.h>
static inline bool render_save_pipeline(qa_source_save_io *io,qa_scene_state *state)
{
    uint32_t blend_source=state->blend_source,blend_destination=state->blend_destination,
        depth=state->depth_test,alpha=state->alpha_test,cull=state->cull,
        stencil_test=state->stencil_test,fail=state->stencil_fail,
        depth_fail=state->stencil_depth_fail,depth_pass=state->stencil_depth_pass;
    if (!qa_source_save_u32(io,&blend_source) || blend_source>QA_BLEND_SRC_ALPHA_SATURATE ||
        !qa_source_save_u32(io,&blend_destination) || blend_destination>QA_BLEND_SRC_ALPHA_SATURATE ||
        !qa_source_save_u32(io,&depth) || depth>QA_DEPTH_DISABLED ||
        !qa_source_save_u32(io,&alpha) || alpha>QA_ALPHA_GE128 ||
        !qa_source_save_u32(io,&cull) || cull>QA_CULL_BACK ||
        !qa_source_save_bool(io,&state->depth_write) || !qa_source_save_bool(io,&state->color_write) ||
        !qa_source_save_bool(io,&state->polygon_offset) || !qa_source_save_bool(io,&state->wireframe) ||
        !qa_source_save_f32(io,&state->depth_near) || !isfinite(state->depth_near) ||
        !qa_source_save_f32(io,&state->depth_far) || !isfinite(state->depth_far) ||
        !qa_source_save_f32(io,&state->offset_factor) || !isfinite(state->offset_factor) ||
        !qa_source_save_f32(io,&state->offset_units) || !isfinite(state->offset_units) ||
        !qa_source_save_f32(io,&state->line_width) || !isfinite(state->line_width) || state->line_width<=0 ||
        !qa_source_save_bool(io,&state->stencil_enabled) ||
        !qa_source_save_u32(io,&stencil_test) || stencil_test>QA_STENCIL_NOTEQUAL ||
        !qa_source_save_u32(io,&state->stencil_reference) ||
        !qa_source_save_u32(io,&state->stencil_compare_mask) || !qa_source_save_u32(io,&state->stencil_write_mask) ||
        !qa_source_save_u32(io,&fail) || fail>QA_STENCIL_INVERT ||
        !qa_source_save_u32(io,&depth_fail) || depth_fail>QA_STENCIL_INVERT ||
        !qa_source_save_u32(io,&depth_pass) || depth_pass>QA_STENCIL_INVERT) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        state->blend_source=(qa_scene_blend)blend_source;
        state->blend_destination=(qa_scene_blend)blend_destination;
        state->depth_test=(qa_scene_depth)depth; state->alpha_test=(qa_scene_alpha)alpha;
        state->cull=(qa_scene_cull)cull; state->stencil_test=(qa_scene_stencil_test)stencil_test;
        state->stencil_fail=(qa_scene_stencil_op)fail;
        state->stencil_depth_fail=(qa_scene_stencil_op)depth_fail;
        state->stencil_depth_pass=(qa_scene_stencil_op)depth_pass;
    }
    return true;
}
static inline bool render_save_rect(qa_source_save_io *io,qa_scene_rect *rect)
{
    return qa_source_save_i32(io,&rect->x) && qa_source_save_i32(io,&rect->y) &&
        qa_source_save_u32(io,&rect->width) && qa_source_save_u32(io,&rect->height);
}
static inline bool render_save_view(qa_source_save_io *io,qa_scene_view *view)
{
    if (!render_save_rect(io,&view->viewport) || !qa_source_save_vec3(io,&view->origin)) return false;
    for (size_t i=0;i<3;++i) if (!qa_source_save_vec3(io,view->axis+i)) return false;
    for (size_t i=0;i<16;++i) if (!qa_source_save_f32(io,view->projection.m+i)) return false;
    return qa_source_save_bool(io,&view->clear_color) && qa_source_save_bool(io,&view->clear_depth) &&
        qa_source_save_bool(io,&view->clear_stencil) && qa_source_save_bool(io,&view->clip_enabled) &&
        qa_source_save_bool(io,&view->mirror) && qa_source_save_f32(io,&view->color.x) &&
        qa_source_save_f32(io,&view->color.y) && qa_source_save_f32(io,&view->color.z) &&
        qa_source_save_f32(io,&view->color.w) && qa_source_save_f32(io,&view->depth) &&
        qa_source_save_vec3(io,&view->clip_plane.normal) && qa_source_save_f32(io,&view->clip_plane.distance) &&
        qa_source_save_u32(io,&view->seat);
}
static inline bool render_save_image(qa_source_save_io *io,const qa_render_checkpoint_refs *refs,const qa_scene_image **image)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; uint64_t key=0;
    if (!reading && *image && (!refs || !refs->image_encode ||
        !refs->image_encode(refs->context,*image,&key,io->error) || !key)) return false;
    if (!qa_source_save_u64(io,&key)) return false;
    if (reading) {
        const qa_scene_image *actual=NULL;
        if (key && (!refs || !refs->image_decode || !refs->image_decode(refs->context,key,&actual,io->error) || !actual)) return false;
        qa_scene_image_retain(actual); *image=actual;
    }
    return true;
}
#endif
