#ifndef QA_SCENE_RESOURCES_INTERNAL_H
#define QA_SCENE_RESOURCES_INTERNAL_H
#include "qa/scene.h"
#include "qa/strings.h"

typedef struct owned_image owned_image;
typedef struct qa_scene_resources_capture qa_scene_resources_capture;
typedef struct scene_names { qa_strings *strings; size_t references; owned_image *images; size_t image_count; } scene_names;
typedef struct image_lineage { uint64_t revision; size_t references; } image_lineage;
struct owned_image {
    qa_scene_image image;
    scene_names *names;
    qa_scene_image_level *levels;
    image_lineage *lineage;
    owned_image *next, *previous;
    bool listed;
};
typedef struct image_cache {
    uint64_t source, logical_source;
    qa_resource *source_record, *logical_record;
    qa_mount_id source_mount, logical_mount;
    qa_string_id name;
    qa_scene_image_options options;
    uint8_t palette[768], translation[256];
    qa_scene_image *image;
} image_cache;
struct qa_scene_resources {
    qa_vfs *vfs;
    scene_names *names;
    qa_scene_image *white, *missing;
    image_cache *cache;
    size_t cache_count, cache_capacity;
    qa_buffer palettes[3];
    qa_scene_image_policy policies[3];
    bool has_policy[3], registrations_started;
    unsigned fullbright_first;
    qa_scene_resources_capture *capture;
    bool continuation_active, detached;
};
#endif
