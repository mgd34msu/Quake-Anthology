#ifndef QA_MATERIAL_LIBRARY_INTERNAL_H
#define QA_MATERIAL_LIBRARY_INTERNAL_H
#include "internal.h"

enum { QA_MATERIAL_MAX_STAGES = 8, QA_MATERIAL_MAX_ANIMATION = 8,
       QA_MATERIAL_MAX_TCMODS = 4, QA_MATERIAL_MAX_DEFORMS = 3,
       QA_MATERIAL_BUCKETS = 1024, QA_MATERIAL_MAX_REGISTERED = 16384 };

typedef struct qa_material_catalog_source {
    qa_resource *resource;
    qa_bytes bytes;
    uint8_t *owned_bytes;
    qa_sha256_digest digest;
    qa_scene_family dependency_family;
    uint8_t dependency_palette[768];
    bool dependency_scope, dependency_has_palette;
    struct qa_material_catalog_source *next;
} qa_material_catalog_source;

typedef struct qa_material_script {
    char *name;
    uint8_t *text;
    size_t size;
    qa_material_catalog_source *source;
    size_t source_offset, name_offset, name_size;
    struct qa_material_script *next;
} qa_material_script;

typedef struct qa_material_video_receipt {
    size_t command, command_end, offset, end;
    char *source;
    const qa_scene_image *image;
    struct qa_material_video_receipt *next;
} qa_material_video_receipt;
typedef struct qa_material_record {
    qa_material material;
    qa_scene_image_options options;
    qa_material_registration_kind kind;
    uint64_t world_identity;
    int32_t lightmap_index;
    uint8_t *palette, *translation;
    char *base_name;
    const qa_scene_image *base_image;
    qa_material_video_receipt *videos;
    struct qa_material_record *next;
    struct qa_material_record *admission_parent;
    struct qa_material_record *source_variant_parent;
    qa_q3_image_upload_options source_variant_upload;
    uint64_t source_variant_revision;
} qa_material_record;

typedef struct qa_material_remap_record {
    char *original, *replacement;
    float time_offset;
    struct qa_material_remap_record *next;
} qa_material_remap_record;
typedef struct qa_material_generated {
    char *name;
    const qa_scene_image *image;
    const qa_material *picture;
    struct qa_material_generated *next;
} qa_material_generated;

struct qa_material_library {
    size_t references;
    qa_scene_resources *resources;
    qa_material_order *order;
    qa_scene_image *fog_image, *dlight_image;
    qa_material_script *scripts[QA_MATERIAL_BUCKETS];
    qa_material_catalog_source *catalog_sources, *catalog_tail, *catalog_current;
    qa_material_record *records[QA_MATERIAL_BUCKETS];
    qa_material_record **ordered;
    size_t count, capacity;
    qa_material_remap_record *remaps;
    qa_material_generated *generated;
    qa_material_video_start_fn video_start;
    void *video_context;
    qa_material_profile profile;
    bool source_profile;
    qa_material_source_upload_fn source_upload;
    void *source_upload_context;
    qa_material_source_ui_fullscreen_fn source_ui_fullscreen;
    void *source_ui_context;
    qa_vec3 sun_light, sun_direction;
    float sky_height;
    bool has_sun;
    size_t capture_depth;
    bool mutating, catalog_ready, video_required;
    qa_scene_material_image_policy *image_policy;
    const qa_material_record *refresh_record;
    qa_material_record *registration_record;
    const qa_material_library *policy_source;
    bool policy_sealed, policy_video;
};

bool qa_material_order_retain(qa_material_order *, qa_error *);
bool qa_material_order_reserve(qa_material_order *, const qa_material *, qa_material_order_entry **, qa_error *);
bool qa_material_order_publish(qa_material_order_entry *, qa_error *);
void qa_material_order_remove(qa_material_order_entry *);
void qa_material_order_changed(qa_material_order_entry *);
bool qa_material_order_image_policy_add(qa_material_order_image_policy *, qa_material_order *, qa_error *);
bool qa_material_order_image_policy_current(const qa_material_order_image_policy *);
bool qa_material_order_image_policy_associated(const qa_material_order *);

char *qa_material_string(const char *, qa_error *);
char *qa_material_name(const char *, qa_error *);
unsigned qa_material_hash(const char *);
void qa_material_stage_init(qa_material_stage *);
void qa_material_stage_clear(qa_material_stage *);
void qa_material_clear(qa_material *);
void qa_material_videos_clear(qa_material_record *);
void qa_material_finish(qa_material *, int32_t lightmap_index, bool source_profile);
bool qa_material_script_catalog(qa_material_library *, qa_bytes, qa_error *);
bool qa_material_script_register(qa_material_library *, qa_material *, qa_bytes,
                                 const qa_scene_image_options *, int32_t lightmap_index,
                                 const char *base_name, const qa_scene_image *base_image, qa_error *);
bool qa_material_sample_image(qa_material_library *, const qa_scene_image *, bool mipmap,
                               qa_scene_wrap, qa_scene_image **, qa_error *);
bool qa_material_stage_image(qa_material_stage *, size_t, const char *,
                             qa_scene_image *, qa_error *);
const qa_material_record *qa_material_record_resolve(const qa_material_library *,
                                                     const qa_material *);
#endif
