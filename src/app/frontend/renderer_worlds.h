#ifndef QA_FRONTEND_RENDERER_WORLDS_H
#define QA_FRONTEND_RENDERER_WORLDS_H
#include "world_inventory.h"
typedef struct frontend_renderer_worlds frontend_renderer_worlds;
typedef struct frontend_renderer_worlds_view {
    const qa_scene_world *world;
    const qa_resource *resource;
    const qa_vfs *files;
    qa_scene_resources *images;
    qa_material_library *materials;
    bool private_heaps;
} frontend_renderer_worlds_view;
bool frontend_renderer_worlds_read(const qa_frontend *,frontend_renderer_worlds_view *,bool *,qa_error *);
bool frontend_renderer_worlds_checkpoint(qa_frontend *,const frontend_world_inventory *,qa_buffer *,qa_error *);
bool frontend_renderer_worlds_prepare_restored(qa_frontend *,qa_bytes,qa_error *);
bool frontend_renderer_worlds_attach_restored(qa_frontend *,frontend_world_inventory *,qa_error *);
bool frontend_renderer_worlds_bind_restored(qa_frontend *,qa_error *);
bool frontend_renderer_worlds_prune(qa_frontend *,qa_error *);
bool frontend_renderer_worlds_destroy(frontend_renderer_worlds **,qa_error *);
bool frontend_renderer_worlds_idle(const frontend_renderer_worlds *);
#endif
