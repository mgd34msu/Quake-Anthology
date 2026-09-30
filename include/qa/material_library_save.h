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
} qa_material_library_checkpoint_refs;
/* qualified_content owns the actual candidate script catalog and resource
 * namespace. The decoded library verifies that catalog byte-for-byte and
 * borrows its installed video service binding. Restore dispatches no service.
 * Restored libraries remain detached until the full renderer order is restored
 * against their records and bound with qa_material_library_bind_order. */
bool qa_material_library_checkpoint(const qa_material_library *, const qa_material_library_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_material_library_restore(const qa_material_library *qualified_content, qa_bytes,
    const qa_material_library_checkpoint_refs *, qa_material_library **, qa_error *);
bool qa_material_library_bind_order(qa_material_library *, qa_material_order *, qa_error *);
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
bool qa_material_library_order_ready(const qa_material_library *);
#endif
