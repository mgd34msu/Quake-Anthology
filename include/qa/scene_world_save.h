#ifndef QA_SCENE_WORLD_SAVE_H
#define QA_SCENE_WORLD_SAVE_H
#include "qa/scene.h"
/* The caller supplies the actual already opened immutable map resource. */
bool qa_scene_world_source_resource_bind(qa_scene_world *, const qa_resource *, qa_error *);
const qa_resource *qa_scene_world_source_resource_read(const qa_scene_world *);
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
typedef enum qa_scene_world_identity_kind {
    QA_SCENE_WORLD_IDENTITY_WORLD,
    QA_SCENE_WORLD_IDENTITY_MODEL,
    QA_SCENE_WORLD_IDENTITY_MESH
} qa_scene_world_identity_kind;
typedef struct qa_scene_world_owner_refs {
    qa_scene_world_checkpoint_refs state;
    void *context;
    bool (*geometry_encode)(void *, const qa_scene_geometry *, uint64_t *, qa_error *);
    bool (*geometry_decode)(void *, uint64_t, const qa_scene_geometry **, qa_error *);
    /* Readonly qualification of actual immutable map/content inputs, including
     * external lighting and palette/translation policy. No acquisition. */
    bool (*source_qualify)(void *, qa_bytes, const qa_scene_world_options *, qa_error *);
    /* Resolve the actual preallocated scene namespace. Material/frame imports
     * use these same identities. It must not mint IDs or execute source code. */
    bool (*identity_decode)(void *, qa_scene_world_identity_kind, size_t ordinal,
        uint64_t saved, uint64_t *installed, qa_error *);
    /* Optional canonical capture mapping for an imported owner. Qualifies its
     * physical producer and returns the original saved identity without
     * modifying the installed world. NULL preserves actual identities. */
    bool (*identity_encode)(void *, qa_scene_world_identity_kind, size_t ordinal,
        uint64_t installed, uint64_t *saved, qa_error *);
} qa_scene_world_owner_refs;
typedef struct qa_scene_world_saved_identity {
    qa_scene_world_identity_kind kind;
    size_t ordinal;
    uint64_t saved;
} qa_scene_world_saved_identity;
/* Read the actual saved identity inventory before material/frame imports.
 * The complete static owner is decoded into temporary detached allocations,
 * qualified against real source/image/geometry owners and then destroyed.
 * No namespace IDs are minted. Free the returned array with free(). */
bool qa_scene_world_owner_identities_read(const qa_bsp_view *qualified_source, qa_scene_resources *, qa_bytes,
    const qa_scene_world_owner_refs *, qa_scene_world_saved_identity **, size_t *, qa_error *);
/* Full detached owner construction preserves saved static geometry allocations
 * and mutable continuation. Resources/palette/images, geometry and material
 * tables must exist first. Immutable BSP parsing qualifies source records; no
 * world builder, image/material admission or lighting update runs. The caller
 * owns qualified_source and every resolver target through the whole decode.
 * Only a completely decoded owner is returned; outputs must be empty. */
bool qa_scene_world_owner_checkpoint(const qa_scene_world *, const qa_scene_world_owner_refs *, qa_buffer *, qa_error *);
bool qa_scene_world_owner_restore(const qa_bsp_view *qualified_source, qa_scene_resources *, qa_material_library *,
    qa_bytes, const qa_scene_world_owner_refs *, qa_scene_world **, qa_error *);
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
