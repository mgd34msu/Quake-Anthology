#ifndef QA_RENDER_SAVE_FIELDS_H
#define QA_RENDER_SAVE_FIELDS_H
#include "qa/render_save.h"
#include "qa/source_save.h"
static bool render_save_rect(qa_source_save_io *io,qa_scene_rect *rect)
{
    return qa_source_save_i32(io,&rect->x) && qa_source_save_i32(io,&rect->y) &&
        qa_source_save_u32(io,&rect->width) && qa_source_save_u32(io,&rect->height);
}
static bool render_save_view(qa_source_save_io *io,qa_scene_view *view)
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
static bool render_save_image(qa_source_save_io *io,const qa_render_checkpoint_refs *refs,const qa_scene_image **image)
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
