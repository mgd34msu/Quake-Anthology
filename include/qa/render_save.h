#ifndef QA_RENDER_SAVE_H
#define QA_RENDER_SAVE_H
#include "qa/render_cpu.h"
#include "qa/render_gl.h"
typedef struct qa_render_checkpoint_refs {
    void *context;
    bool (*image_encode)(void *,const qa_scene_image *,uint64_t *,qa_error *);
    bool (*image_decode)(void *,uint64_t,const qa_scene_image **,qa_error *);
    bool (*geometry_encode)(void *,const qa_scene_geometry *,uint64_t *,qa_error *);
    bool (*geometry_decode)(void *,uint64_t,const qa_scene_geometry **,qa_error *);
    bool (*mesh_identity_encode)(void *,uint64_t,uint64_t *,qa_error *);
    bool (*mesh_identity_decode)(void *,uint64_t,uint64_t *,qa_error *);
    bool (*material_encode)(void *,const qa_material *,uint64_t *,qa_error *);
    bool (*material_decode)(void *,uint64_t,const qa_material **,qa_error *);
} qa_render_checkpoint_refs;
/* Image and geometry ordinals each follow their genuine physical retained
 * rows. The caller protects the renderer/resource lifetime throughout. */
typedef bool (*qa_render_resource_visit_fn)(void *,const qa_scene_image *,const qa_scene_geometry *,size_t,qa_error *);
bool qa_cpu_checkpoint_resources(const qa_cpu_renderer *,qa_render_resource_visit_fn,void *,qa_error *);
bool qa_cpu_checkpoint(const qa_cpu_renderer *,const qa_render_checkpoint_refs *,qa_buffer *,qa_error *);
/* Detached private buffers and retained image aliases; no presentation call. */
bool qa_cpu_restore(qa_bytes,const qa_cpu_options *,const qa_render_checkpoint_refs *,qa_cpu_renderer **,qa_error *);
bool qa_gl_checkpoint_resources(const qa_gl_renderer *,qa_render_resource_visit_fn,void *,qa_error *);
#endif
