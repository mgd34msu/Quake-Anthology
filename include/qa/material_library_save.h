#ifndef QA_MATERIAL_LIBRARY_SAVE_H
#define QA_MATERIAL_LIBRARY_SAVE_H
#include "qa/material.h"

/* Read leases retain the library and its renderer order while their records
 * are traversed. Mutation and destruction resume after the final lease. */
bool qa_material_library_capture_begin(const qa_material_library *, qa_error *);
void qa_material_library_capture_end(const qa_material_library *);
bool qa_material_library_idle(const qa_material_library *);
qa_material_library *qa_material_library_create_detached(qa_scene_resources *, qa_error *);
typedef struct qa_material_script_view {
    const char *name;
    qa_bytes body, catalog;
    const qa_resource *resource;
    size_t source_offset, name_offset, name_size;
    bool dependency_scope;
    qa_scene_family dependency_family;
    qa_bytes dependency_palette;
} qa_material_script_view;
/* Borrow the actual admitted definition; no registration, parsing or lookup
 * of new content. The enclosing library retains its catalog source. */
bool qa_material_library_script_read(const qa_material_library *, const char *, qa_material_script_view *);
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
