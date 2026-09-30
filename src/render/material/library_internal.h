#ifndef QA_MATERIAL_LIBRARY_INTERNAL_H
#define QA_MATERIAL_LIBRARY_INTERNAL_H
#include "internal.h"

enum { QA_MATERIAL_MAX_STAGES = 8, QA_MATERIAL_MAX_ANIMATION = 8,
       QA_MATERIAL_MAX_TCMODS = 4, QA_MATERIAL_MAX_DEFORMS = 3,
       QA_MATERIAL_BUCKETS = 1024, QA_MATERIAL_MAX_REGISTERED = 16384 };

typedef struct qa_material_script {
    char *name;
    uint8_t *text;
    size_t size;
    struct qa_material_script *next;
} qa_material_script;

typedef struct qa_material_record {
    qa_material material;
    qa_scene_image_options options;
    qa_material_registration_kind kind;
    uint64_t world_identity;
    int32_t lightmap_index;
    uint8_t *palette, *translation;
    char *base_name;
    const qa_scene_image *base_image;
    struct qa_material_record *next;
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
    qa_scene_resources *resources;
    qa_material_order *order;
    qa_scene_image *fog_image, *dlight_image;
    qa_material_script *scripts[QA_MATERIAL_BUCKETS];
    qa_material_record *records[QA_MATERIAL_BUCKETS];
    qa_material_record **ordered;
    size_t count, capacity;
    qa_material_remap_record *remaps;
    qa_material_generated *generated;
    qa_material_video_start_fn video_start;
    void *video_context;
    qa_material_profile profile;
    qa_vec3 sun_light, sun_direction;
    float sky_height;
    bool has_sun;
};

bool qa_material_order_retain(qa_material_order *, qa_error *);
bool qa_material_order_reserve(qa_material_order *, const qa_material *, qa_material_order_entry **, qa_error *);
bool qa_material_order_publish(qa_material_order_entry *, qa_error *);
void qa_material_order_remove(qa_material_order_entry *);
void qa_material_order_changed(qa_material_order_entry *);

char *qa_material_string(const char *, qa_error *);
char *qa_material_name(const char *, qa_error *);
unsigned qa_material_hash(const char *);
void qa_material_stage_init(qa_material_stage *);
void qa_material_stage_clear(qa_material_stage *);
void qa_material_clear(qa_material *);
void qa_material_finish(qa_material *, int32_t lightmap_index);
bool qa_material_script_catalog(qa_material_library *, qa_bytes, qa_error *);
bool qa_material_script_register(qa_material_library *, qa_material *, qa_bytes,
                                 const qa_scene_image_options *, int32_t lightmap_index,
                                 const char *base_name, const qa_scene_image *base_image, qa_error *);
bool qa_material_sample_image(qa_material_library *, const qa_scene_image *, bool mipmap,
                               qa_scene_wrap, qa_scene_image **, qa_error *);
bool qa_material_stage_image(qa_material_stage *, size_t, const char *,
                             qa_scene_image *, qa_error *);
#endif
