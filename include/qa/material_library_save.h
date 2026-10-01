#ifndef QA_MATERIAL_LIBRARY_SAVE_H
#define QA_MATERIAL_LIBRARY_SAVE_H
#include "qa/material.h"

typedef struct qa_material_library_checkpoint_refs {
    void *context;
    bool (*image_encode)(void *, const qa_scene_image *, uint64_t *, qa_error *);
    bool (*image_decode)(void *, uint64_t, const qa_scene_image **, qa_error *);
    bool (*image_identity_encode)(void *, uint64_t, uint64_t *, qa_error *);
    bool (*image_identity_decode)(void *, uint64_t, uint64_t *, qa_error *);
    bool (*world_encode)(void *, uint64_t, uint64_t *, qa_error *);
    bool (*world_decode)(void *, uint64_t, uint64_t *, qa_error *);
    bool (*resource_encode)(void *, const qa_resource *, uint64_t *pool, uint64_t *resource, qa_error *);
    bool (*resource_decode)(void *, uint64_t pool, uint64_t resource, const qa_resource **, qa_error *);
} qa_material_library_checkpoint_refs;
/* The aggregate holds each library across namespace collection and all codecs.
 * Nested capture leases are read-only; ordinary mutations and destruction are
 * rejected until the final lease ends. The resource owner has its own lease. */
bool qa_material_library_capture_begin(const qa_material_library *, qa_error *);
void qa_material_library_capture_end(const qa_material_library *);
bool qa_material_library_idle(const qa_material_library *);
qa_material_library *qa_material_library_create_detached(qa_scene_resources *, qa_error *);
bool qa_material_library_empty_detached(const qa_material_library *);
/* The immutable catalog precedes registered records and renderer order.
 * Resource resolvers inspect the actual qualified retained content graph.
 * Saved inline script bytes retain their ordinary parse-call order. Restore
 * imports exact definition spans without parsing, file acquisition, image
 * creation or material registration. Builtin images come from the image graph. */
bool qa_material_library_catalog_checkpoint(const qa_material_library *,
    const qa_material_library_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_material_library_catalog_restore(qa_scene_resources *, qa_bytes,
    const qa_material_library_checkpoint_refs *, qa_material_library **, qa_error *);
size_t qa_material_library_catalog_resource_count(const qa_material_library *);
const qa_resource *qa_material_library_catalog_resource_at(const qa_material_library *, size_t);
/* qualified_content is the imported actual catalog. The decoded records keep
 * its content owners and verify exact catalog order and bytes. Video services
 * bind only after restoration and never dispatch during import. */
bool qa_material_library_checkpoint(const qa_material_library *, const qa_material_library_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_material_library_restore(const qa_material_library *qualified_content, qa_bytes,
    const qa_material_library_checkpoint_refs *, qa_material_library **, qa_error *);
/* The actual source services can already borrow the target's stable address.
 * Target owns no catalog, images, records, order or callbacks. Only a complete
 * qualified decode is adopted; failure leaves that empty owner unchanged. */
bool qa_material_library_restore_into_empty(qa_material_library *target,
    const qa_material_library *qualified_content, qa_bytes,
    const qa_material_library_checkpoint_refs *, qa_error *);
bool qa_material_library_bind_order(qa_material_library *, qa_material_order *, qa_error *);
bool qa_material_library_bind_video_start(qa_material_library *, qa_material_video_start_fn, void *, qa_error *);
/* Exact retained registration records, including internal/generated entries. */
const qa_material *qa_material_library_record_at(const qa_material_library *, size_t);
size_t qa_material_library_record_count(const qa_material_library *);
typedef struct qa_material_library_record_view {
    const qa_material *material;
    const qa_scene_image_options *options;
    qa_material_registration_kind kind;
    uint64_t world_identity;
    int32_t lightmap_index;
    const char *base_name;
    const qa_scene_image *base_image;
} qa_material_library_record_view;
bool qa_material_library_record_read(const qa_material_library *, size_t, qa_material_library_record_view *);
qa_scene_resources *qa_material_library_resource_owner(const qa_material_library *);
const qa_material_order *qa_material_library_order_owner(const qa_material_library *);
bool qa_material_library_order_ready(const qa_material_library *);
#endif
