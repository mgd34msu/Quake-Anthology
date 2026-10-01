#ifndef QA_FRONTEND_SCENE_INVENTORY_H
#define QA_FRONTEND_SCENE_INVENTORY_H
#include "capture.h"
#include "scene_refs.h"

typedef struct frontend_scene_inventory frontend_scene_inventory;
typedef struct frontend_world_source {
    const qa_scene_world *world;
    const qa_resource *resource;
    const qa_vfs *files;
    qa_scene_resources *images;
    qa_material_library *materials;
} frontend_world_source;
/* Collects genuine live cache/registry/token provenance against the current
 * application content graph. The actual frontend capture remains held until
 * this inventory and every component callback have been disposed. */
bool frontend_scene_inventory_capture(qa_frontend *, frontend_scene_inventory **, qa_error *);
void frontend_scene_inventory_destroy(frontend_scene_inventory *);
frontend_model_inventory *frontend_scene_inventory_models(const frontend_scene_inventory *);
size_t frontend_scene_inventory_world_count(const frontend_scene_inventory *);
bool frontend_scene_inventory_world_at(const frontend_scene_inventory *, size_t, frontend_world_source *);
size_t frontend_scene_inventory_model_count(const frontend_scene_inventory *);
const qa_scene_model *frontend_scene_inventory_model_at(const frontend_scene_inventory *, size_t);
/* These root ordinals are the same nonzero physical keys used by QWON/QMON.
 * Genuine event lights/static audio and the frame are collected afterward. */
bool frontend_scene_inventory_namespace(const frontend_scene_inventory *, frontend_scene_namespace *, qa_error *);
#endif
