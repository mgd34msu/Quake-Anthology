#ifndef QA_SCENE_RESOURCES_INTERNAL_H
#define QA_SCENE_RESOURCES_INTERNAL_H
#include "qa/scene.h"
#include "qa/strings.h"

typedef struct owned_image owned_image;
typedef struct qa_scene_resources_capture qa_scene_resources_capture;
typedef struct scene_names { qa_strings *strings; size_t references; owned_image *images; size_t image_count; qa_scene_resources *owner; } scene_names;
typedef struct image_lineage { uint64_t revision; size_t references; } image_lineage;
struct owned_image {
    qa_scene_image image;
    scene_names *names;
    qa_scene_image_level *levels;
    image_lineage *lineage;
    const qa_scene_image *sampling_source;
    bool sampling_mipmap;
    const qa_scene_image *source_variant_source;
    qa_scene_resources *source_variant_owner;
    qa_q3_image_upload_options source_variant_upload;
    owned_image *variant_next;
    owned_image *next, *previous;
    bool listed;
};
typedef struct image_cache {
    uint64_t source, logical_source;
    qa_resource *source_record, *logical_record;
    qa_mount_id source_mount, logical_mount;
    qa_string_id name;
    qa_scene_image_options options;
    bool exact_file;
    uint8_t palette[768], translation[256];
    qa_scene_image *image;
} image_cache;
struct qa_scene_resources {
    size_t references;
    qa_vfs *vfs;
    scene_names *names;
    qa_scene_image *white, *missing;
    qa_scene_image *source_white, *source_missing, *source_identity;
    qa_q3_image_upload_options source_builtins_upload;
    bool source_builtins;
    qa_scene_source_image_admit_fn source_admit;
    void *source_admit_context;
    owned_image *variants;
    image_cache *cache;
    size_t cache_count, cache_capacity;
    qa_buffer palettes[3];
    qa_resource *palette_resources[3];
    qa_vfs_acquisition palette_openings[3];
    qa_scene_image_policy policies[3];
    bool has_policy[3], registrations_started;
    unsigned fullbright_first;
    qa_scene_resources_capture *capture;
    qa_scene_resource_policy *policy_pending;
    qa_scene_resources *policy_source;
    bool continuation_active, detached;
};
bool scene_resource_variant_parent_retain(qa_scene_resources *, const qa_scene_image *,
                                          qa_scene_resources **, qa_error *);
#endif
