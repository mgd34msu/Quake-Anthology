#ifndef QA_SCENE_WORLD_SAVE_H
#define QA_SCENE_WORLD_SAVE_H
#include "qa/scene.h"
/* The caller supplies the actual already opened immutable map resource. */
bool qa_scene_world_source_resource_bind(qa_scene_world *, const qa_resource *, qa_error *);
const qa_resource *qa_scene_world_source_resource_read(const qa_scene_world *);
typedef enum qa_scene_world_identity_kind {
    QA_SCENE_WORLD_IDENTITY_WORLD,
    QA_SCENE_WORLD_IDENTITY_MODEL,
    QA_SCENE_WORLD_IDENTITY_MESH
} qa_scene_world_identity_kind;
const qa_scene_mesh *qa_scene_world_mesh_at(const qa_scene_world *, size_t);
size_t qa_scene_world_model_count(const qa_scene_world *);
uint64_t qa_scene_world_model_identity_at(const qa_scene_world *, size_t);
qa_scene_resources *qa_scene_world_resource_owner(const qa_scene_world *);
qa_material_library *qa_scene_world_material_owner(const qa_scene_world *);
uint64_t qa_scene_world_identity(const qa_scene_world *);
/* Actual submission transactions must return before world owner changes. */
bool qa_scene_world_idle(const qa_scene_world *);
bool qa_scene_world_observation_ready(const qa_scene_world *);
/* Borrow the actual retained constructor policy while its owner permits source
 * observation. Text and byte spans stay owned by the world/capture lifetime. */
bool qa_scene_world_options_read(const qa_scene_world *, qa_scene_world_options *);
typedef struct qa_scene_world_capture qa_scene_world_capture;
/* Actual submission/admission and destruction remain excluded for the whole
 * aggregate. Read-only component capture may run under this opaque token. */
bool qa_scene_world_capture_begin(const qa_scene_world *, qa_scene_world_capture **, qa_error *);
void qa_scene_world_capture_end(qa_scene_world_capture *);
typedef struct qa_scene_world_material_binding {
    const qa_material *current, *destination;
    const qa_material *base_current, *base_destination;
} qa_scene_world_material_binding;
size_t qa_scene_world_material_binding_count(const qa_scene_world *);
bool qa_scene_world_material_binding_at(const qa_scene_world *, size_t, qa_scene_world_material_binding *);
/* Bindings preserve surface index order and identify actual current borrowed
 * records. Every destination belongs to the fully restored library/order.
 * The caller keeps the validated array and both libraries alive through apply. */
bool qa_scene_world_materials_rebind_ready(const qa_scene_world *, const qa_material_library *current,
    const qa_material_library *destination, const qa_scene_world_material_binding *, size_t, qa_error *);
void qa_scene_world_materials_rebind(qa_scene_world *, qa_material_library *, const qa_scene_world_material_binding *);
#endif
