#ifndef QA_Q3_ASSETS_SAVE_H
#define QA_Q3_ASSETS_SAVE_H
#include "qa/q3_presentation.h"

typedef struct qa_q3_asset_model_holder {
    bool present, has_lods, owns_world, shared_parent;
    bool source_registration, registration_bad;
    qa_model_format source_kind;
    uint32_t source_num_lods;
    qa_q3_presentation_provider provider;
    const qa_resource *resource, *lod_resources[3];
    const qa_model *sources[3];
    const qa_model_lod_set *lods;
    qa_scene_model *scenes[3];
    const qa_model *source_md4;
    const qa_resource *source_md4_resource;
    qa_scene_model *source_md4_scene;
    qa_scene_world *world;
    uint32_t inline_model;
} qa_q3_asset_model_holder;
typedef struct qa_q3_asset_skin_holder {
    qa_q3_presentation_provider provider;
    const qa_resource *resource;
    const qa_model_skin_map *map;
    bool shared_parent;
} qa_q3_asset_skin_holder;
typedef struct qa_q3_asset_model_lease {
    void *context;
    void (*release)(void *);
} qa_q3_asset_model_lease;

/* Hold the registry while its actual model, skin and service records are
 * traversed. End the lease after the borrowed observations return. */
bool qa_q3_assets_capture_begin(qa_q3_presentation_assets *, qa_error *);
void qa_q3_assets_capture_end(qa_q3_presentation_assets *);
/* Strict parent retirement preflight. This reads the real live registry and
 * all retained model/world descendants; source activity, codecs and outer
 * capture tokens reject readiness. It changes no owner state. */
bool qa_q3_assets_idle(const qa_q3_presentation_assets *);
bool qa_q3_assets_model_count(const qa_q3_presentation_assets *, size_t *, qa_error *);
bool qa_q3_assets_model_holder(const qa_q3_presentation_assets *, size_t ordinal,
    qa_q3_asset_model_holder *, qa_error *);
bool qa_q3_assets_skin_count(const qa_q3_presentation_assets *, size_t *, qa_error *);
bool qa_q3_assets_skin_holder(const qa_q3_presentation_assets *, size_t ordinal,
    qa_q3_asset_skin_holder *, qa_error *);
bool qa_q3_assets_services(const qa_q3_presentation_assets *,
    qa_q3_presentation_asset_options *, qa_scene_world **, qa_collision_geometry **, qa_error *);
#endif
