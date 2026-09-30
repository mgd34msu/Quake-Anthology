#ifndef QA_SCENE_WORLD_SAVE_H
#define QA_SCENE_WORLD_SAVE_H
#include "qa/scene.h"
uint64_t qa_scene_world_identity(const qa_scene_world *);
/* Actual submission transactions must return before world owner changes. */
bool qa_scene_world_idle(const qa_scene_world *);
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
