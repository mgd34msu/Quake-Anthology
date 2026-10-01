#ifndef QA_RENDER_GL_SAVE_H
#define QA_RENDER_GL_SAVE_H
#include "qa/render_save.h"
typedef struct qa_gl_restore_guard qa_gl_restore_guard;
/* Only active geometry rows are visited; ordinals remain the physical cache
 * positions even when earlier rows are already retirement-only. */
typedef bool (*qa_gl_mesh_visit_fn)(void *,uint64_t,uint64_t,const qa_scene_geometry *,size_t,qa_error *);
bool qa_gl_checkpoint_meshes(const qa_gl_renderer *,qa_gl_mesh_visit_fn,void *,qa_error *);
bool qa_gl_checkpoint(qa_gl_renderer *,const qa_render_checkpoint_refs *,qa_buffer *,qa_error *);
bool qa_gl_restore(qa_bytes,const qa_gl_options *,const qa_render_checkpoint_refs *,
                   const qa_gl_renderer *,qa_gl_renderer **,qa_gl_restore_guard **,qa_error *);
/* Fresh logical renderer with empty GPU residency. The actual native output
 * cut is retained until publication; separate programs/resources prepare late. */
bool qa_gl_create_detached(const qa_gl_options *,float gamma,qa_gl_renderer *,
    qa_gl_renderer **,qa_gl_restore_guard **,qa_error *);
bool qa_gl_restore_checkpoint(const qa_gl_restore_guard *,const qa_render_checkpoint_refs *,qa_buffer *,qa_error *);
bool qa_gl_handoff_prepare(qa_gl_restore_guard *,qa_error *);
bool qa_gl_handoff_ready(const qa_gl_restore_guard *,qa_error *);
void qa_gl_handoff(qa_gl_restore_guard *);
void qa_gl_restore_guard_destroy(qa_gl_restore_guard *);
#endif
