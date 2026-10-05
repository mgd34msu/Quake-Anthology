#ifndef QA_RENDER_SAVE_FIELDS_H
#define QA_RENDER_SAVE_FIELDS_H
#include "qa/scene.h"
#include "qa/source_save.h"
static inline bool render_save_vertex(qa_source_save_io *io,qa_scene_vertex *vertex)
{
    return qa_source_save_vec3(io,&vertex->position) && qa_source_save_vec3(io,&vertex->normal) &&
        qa_source_save_f32(io,&vertex->texcoord.x) && qa_source_save_f32(io,&vertex->texcoord.y) &&
        qa_source_save_f32(io,&vertex->lightmap.x) && qa_source_save_f32(io,&vertex->lightmap.y) &&
        qa_source_save_f32(io,&vertex->color.x) && qa_source_save_f32(io,&vertex->color.y) &&
        qa_source_save_f32(io,&vertex->color.z) && qa_source_save_f32(io,&vertex->color.w);
}
#endif
