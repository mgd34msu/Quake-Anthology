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
bool frontend_renderer_worlds_count(const qa_frontend *,size_t *,qa_error *);
bool frontend_renderer_worlds_read_at(const qa_frontend *,size_t,frontend_renderer_worlds_view *,qa_error *);
bool frontend_renderer_worlds_prune(qa_frontend *,qa_error *);
bool frontend_renderer_worlds_destroy(frontend_renderer_worlds **,qa_error *);
bool frontend_renderer_worlds_idle(const frontend_renderer_worlds *);
#endif
