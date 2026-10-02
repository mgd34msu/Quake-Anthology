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

/* Hold the real registry through collection and all aggregate codecs. Source
 * registration, mutation, seat creation/destruction and owner destruction
 * reject this lease. Nested codec entry is rejected. End the lease exactly
 * once, after all borrowed observations and resolver calls have returned. */
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
/* Cold attachment to an empty isolated registry; repeating the same genuine
 * map pair is allowed. Dispatches no source service. */
bool qa_q3_assets_prepare_restored_map(qa_q3_presentation_assets *,
    qa_scene_world *, qa_collision_geometry *, qa_error *);

typedef struct qa_q3_asset_owner_refs {
    void *context;
    /* Exact retired registry parents are restored before their shared rows. */
    bool (*registry_encode)(void *, const qa_q3_presentation_assets *, uint64_t *, qa_error *);
    bool (*registry_decode)(void *, uint64_t, qa_q3_presentation_assets **, qa_error *);
    /* Includes the genuine sound/media owners and source service policy. */
    bool (*services_encode)(void *, const qa_q3_presentation_asset_options *, uint64_t *, qa_error *);
    bool (*services_qualify)(void *, uint64_t, const qa_q3_presentation_asset_options *, qa_error *);
    bool (*provider_encode)(void *, const qa_q3_presentation_provider *, uint64_t *, qa_error *);
    bool (*provider_decode)(void *, uint64_t, qa_q3_presentation_provider *, qa_error *);
    bool (*resource_encode)(void *, const qa_resource *, uint64_t *pool, uint64_t *resource, qa_error *);
    bool (*resource_decode)(void *, uint64_t pool, uint64_t resource, const qa_resource **, qa_error *);
    bool (*model_encode)(void *, const qa_model *, uint64_t *, qa_error *);
    bool (*model_decode)(void *, uint64_t, qa_bytes source, const qa_model **, qa_error *);
    /* Returns an owning token for the actual parsed holder. The release token
     * survives destruction of the codec/dictionary callback context. It is
     * released only after the dependent adopted scene owners are destroyed. */
    bool (*model_retain)(void *, const qa_model *, qa_q3_asset_model_lease *, qa_error *);
    bool (*scene_encode)(void *, const qa_scene_model *, uint64_t *, qa_error *);
    bool (*scene_decode)(void *, uint64_t, qa_scene_model **, qa_error *);
    bool (*world_encode)(void *, const qa_scene_world *, uint64_t *, qa_error *);
    bool (*world_decode)(void *, uint64_t, qa_scene_world **, qa_error *);
    bool (*collision_encode)(void *, const qa_collision_geometry *, uint64_t *, qa_error *);
    bool (*collision_decode)(void *, uint64_t, qa_collision_geometry **, qa_error *);
    bool (*material_encode)(void *, const qa_material *, uint64_t *, qa_error *);
    bool (*material_decode)(void *, uint64_t, const qa_material **, qa_error *);
    bool (*audio_encode)(void *, const qa_audio_asset *, uint64_t *, qa_error *);
    bool (*audio_decode)(void *, uint64_t, qa_audio_asset **, qa_error *);
    /* Owned row qualification is separate from borrowed map resolution. These
     * pure checks run for all unique roots before any destructor transfers. */
    bool (*scene_owned_ready)(void *, uint64_t, size_t model_row, qa_error *);
    bool (*world_owned_ready)(void *, uint64_t, size_t model_row, qa_error *);
    /* Lookups above borrow genuine already imported owners. After complete
     * decode, these no-fail transfers clear construction ownership from the
     * aggregate. Each unique scene/owned preview world transfers once. They
     * may only transfer ownership, never dispatch a source service. */
    void (*scene_adopt)(void *, uint64_t);
    void (*world_adopt)(void *, uint64_t);
} qa_q3_asset_owner_refs;

bool qa_q3_assets_owner_checkpoint(qa_q3_presentation_assets *, qa_session *,
    const qa_q3_asset_owner_refs *, qa_buffer *, qa_error *);
/* Reads only the literal supported header/parent prefix for aggregate import
 * ordering, with no resolver or allocation. Zero means no parent. This does
 * not qualify the remaining payload; owner_restore performs full admission. */
bool qa_q3_assets_owner_parent_key(qa_bytes, uint64_t *, qa_error *);
/* Candidate services and map bindings are genuine qualified owners. Parsed
 * model holders and scenes/worlds/materials/audio already exist in the same
 * aggregate dictionary. Import copies private skin/name/LOD metadata and
 * attaches those exact owners. It does not register, select, acquire, parse,
 * rasterize or construct an image/material/model/world. Decode and readiness
 * use an isolated registry; failure disposes its private metadata and leases,
 * leaving the destination and its prebound map/services unchanged for retry.
 * Success publishes into the same destination allocation before adoption. */
bool qa_q3_assets_owner_restore(qa_q3_presentation_assets *, qa_session *,
    const qa_q3_asset_owner_refs *, qa_bytes, qa_error *);
#endif
