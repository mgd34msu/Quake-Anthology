#ifndef QA_SCENE_WORLD_SAVE_H
#define QA_SCENE_WORLD_SAVE_H
#include "qa/scene.h"
typedef struct qa_scene_world_image_refs {
    void *context;
    bool (*encode)(void *, const qa_scene_image *, uint64_t *, qa_error *);
    /* Returns a borrowed immutable version in the world's resource owner. */
    bool (*decode)(void *, uint64_t, const qa_scene_image **, qa_error *);
} qa_scene_world_image_refs;
/* Legacy lighting includes actual retained texture/sky/lightmap versions,
 * texel buffers and lightstyle caches. The existing candidate world must own
 * byte-identical BSP/options/static descriptors. Restore does not regenerate
 * images or lighting, and publishes only after complete validation. */
bool qa_scene_world_lighting_checkpoint(const qa_scene_world *, const qa_scene_world_image_refs *, qa_buffer *, qa_error *);
bool qa_scene_world_lighting_restore(qa_scene_world *, qa_bytes, const qa_scene_world_image_refs *, qa_error *);
typedef struct qa_scene_world_checkpoint_refs {
    qa_scene_world_image_refs images;
    void *context;
    bool (*material_encode)(void *, const qa_material *, uint64_t *, qa_error *);
    bool (*material_decode)(void *, uint64_t, const qa_material **, qa_error *);
    bool (*frame_encode)(void *, const qa_scene_frame *, uint64_t *, qa_error *);
    bool (*frame_decode)(void *, uint64_t, const qa_scene_frame **, qa_error *);
} qa_scene_world_checkpoint_refs;
/* The candidate is the actual already admitted world. Its immutable source,
 * topology, mesh/patch data and constructor policy must match exactly. Its
 * resource inventory, renderer order and material owner are restored first.
 * Process-local world/model/mesh identities stay attached to that qualified
 * geometry and are mapped by the enclosing frame/content dictionary. */
bool qa_scene_world_checkpoint(const qa_scene_world *, const qa_scene_world_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_scene_world_restore(qa_scene_world *, qa_bytes, const qa_scene_world_checkpoint_refs *, qa_error *);
const qa_scene_mesh *qa_scene_world_mesh_at(const qa_scene_world *, size_t);
size_t qa_scene_world_model_count(const qa_scene_world *);
uint64_t qa_scene_world_model_identity_at(const qa_scene_world *, size_t);
qa_scene_resources *qa_scene_world_resource_owner(const qa_scene_world *);
qa_material_library *qa_scene_world_material_owner(const qa_scene_world *);
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
