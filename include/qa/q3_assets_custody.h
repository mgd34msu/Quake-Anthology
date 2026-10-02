#ifndef QA_Q3_ASSETS_CUSTODY_H
#define QA_Q3_ASSETS_CUSTODY_H
#include "qa/q3_presentation.h"

/* Allocation references do not validate handles or invoke parent callbacks. */
bool qa_q3_assets_retain(qa_q3_presentation_assets *, qa_error *);
void qa_q3_assets_release(qa_q3_presentation_assets *);
/* A retired parent keeps the exact holder namespace borrowed by its child. */
qa_q3_presentation_assets *qa_q3_assets_parent(const qa_q3_presentation_assets *);
bool qa_q3_assets_retired(const qa_q3_presentation_assets *);
/* After the real presentation parent closes, retire borrowed service pointers
 * without touching retained handle values or their actual owned references. */
bool qa_q3_assets_services_retire(qa_q3_presentation_assets *, qa_error *);
typedef struct qa_q3_registry_retirement qa_q3_registry_retirement;
/* Preparation holds the real parent mutation lease and changes no published
 * namespace. Dispose failed preparations before resuming Source dispatch. */
bool qa_q3_presentation_retire_world_prepare(qa_q3_presentation *,
    qa_q3_registry_retirement **, qa_error *);
bool qa_q3_presentation_retire_world_ready(const qa_q3_registry_retirement *, qa_error *);
bool qa_q3_presentation_retire_world_dispose(qa_q3_registry_retirement **, qa_error *);
/* After ready for the whole roster, commit transfers one owning registry ref. */
void qa_q3_presentation_retire_world_commit(qa_q3_registry_retirement **,
    qa_q3_presentation_assets **);
/* Returns one owning reference to the new registry installed on presentation. */
bool qa_q3_presentation_retire_world_retained(qa_q3_presentation *,
    qa_q3_presentation_assets **, qa_error *);
/* Each real selected provider is held once by its physical registry. */
bool qa_q3_assets_provider_hold(qa_q3_presentation_assets *,
    const qa_q3_presentation_provider *, qa_error *);
size_t qa_q3_assets_provider_count(const qa_q3_presentation_assets *);
bool qa_q3_assets_provider_at(const qa_q3_presentation_assets *, size_t,
    qa_q3_presentation_provider *);
/* Imported rows already name genuine providers; retain them without selection. */
bool qa_q3_assets_custody_restore(qa_q3_presentation_assets *, qa_error *);
typedef struct qa_q3_asset_map_custody {
    qa_scene_world *world;
    qa_collision_geometry *geometry;
    /* Source registries require this owner. Generic callers retain their
     * existing externally borrowed BSP-byte contract when it is absent. */
    const qa_resource *resource;
} qa_q3_asset_map_custody;
bool qa_q3_assets_map_hold(qa_q3_presentation_assets *, qa_scene_world *,
    qa_collision_geometry *, qa_error *);
size_t qa_q3_assets_map_count(const qa_q3_presentation_assets *);
bool qa_q3_assets_map_at(const qa_q3_presentation_assets *, size_t, qa_q3_asset_map_custody *);
/* Empty cold candidate only. Retains the literal already imported map roster
 * before replacing constructor custody, preserving saved ordinal order. The
 * current pair must match the actual constructor/prebound pair and appear in
 * the roster when present. Failure leaves that candidate unchanged. */
bool qa_q3_assets_prepare_restored_custody_maps(qa_q3_presentation_assets *,
    const qa_q3_asset_map_custody *, size_t, qa_scene_world *,
    qa_collision_geometry *, qa_error *);
#endif
