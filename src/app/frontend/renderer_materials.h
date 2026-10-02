#ifndef QA_FRONTEND_RENDERER_MATERIALS_H
#define QA_FRONTEND_RENDERER_MATERIALS_H
#include "internal.h"
#include "qa/persistence_content.h"
typedef struct frontend_renderer_materials frontend_renderer_materials;
typedef struct frontend_renderer_materials_view {
    qa_material_library *library;
    qa_scene_resources *images;
    qa_vfs *mounts;
    qa_scene_resources *lightmap_images;
    qa_vfs *lightmap_mounts;
} frontend_renderer_materials_view;
/* Retained shader and lightmap parents without an ordinary owner are present.
 * Existing provider banks and libraries keep their original namespace. */
bool frontend_renderer_materials_read(const qa_frontend *,frontend_renderer_materials_view *,bool *,qa_error *);
bool frontend_renderer_materials_prune(qa_frontend *,qa_error *);
bool frontend_renderer_materials_destroy(frontend_renderer_materials **,qa_error *);
bool frontend_renderer_materials_checkpoint(qa_frontend *,qa_buffer *,qa_error *);
bool frontend_renderer_materials_prepare_restored(qa_frontend *,qa_bytes,qa_error *);
bool frontend_renderer_materials_bind_restored(qa_frontend *,qa_error *);
#endif
