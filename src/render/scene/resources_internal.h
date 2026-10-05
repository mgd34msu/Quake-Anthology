#ifndef QA_SCENE_RESOURCES_INTERNAL_H
#define QA_SCENE_RESOURCES_INTERNAL_H
#include "qa/scene.h"
#include "qa/strings.h"

typedef struct owned_image owned_image;
typedef struct qa_scene_resources_capture qa_scene_resources_capture;
typedef struct scene_names { qa_strings *strings; size_t references; owned_image *images; size_t image_count; qa_scene_resources *owner; } scene_names;
typedef struct image_lineage { uint64_t revision; size_t references; } image_lineage;
typedef struct recipient_image_binding {
    struct recipient_image_binding *next;
    const qa_scene_image *source;
} recipient_image_binding;
typedef struct image_asset_recipe {
    qa_resource *source, *palette_source;
    char *path;
    qa_scene_image_options options;
    uint8_t palette[768], translation[256];
    uint32_t fullbright_first, gif_frame;
    bool palette_attempted, generic_upload, recipient;
    qa_status palette_error;
    qa_q3_image_upload_options recipient_upload;
    uint8_t kind, level_count, sky_layer;
    uint64_t offsets[4];
    uint32_t widths[4], heights[4], overbright;
    bool flood_skin, generate_mips, quake64, post_upload, post_mipmap;
    qa_palette_layer layer;
    int32_t fullbright_last;
} image_asset_recipe;
struct owned_image {
    qa_scene_image image;
    bool embedded_png;
    scene_names *names;
    qa_scene_image_level *levels;
    image_lineage *lineage;
    uint64_t creation_sequence;
    const qa_scene_image *sampling_source;
    bool sampling_mipmap;
    const qa_scene_image *source_variant_source;
    bool generic_variant, generic_variant_mipmap;
    bool recipient_first_upload;
    recipient_image_binding *recipient_bindings;
    qa_image recipient_source;
    image_asset_recipe *asset;
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
    char *logical_path;
    qa_vfs_acquisition source_opening, logical_opening;
    qa_resource *palette_source;
    qa_vfs_acquisition palette_opening;
    bool palette_attempted;
    qa_status palette_error;
    qa_string_id name;
    qa_scene_image_options options;
    bool exact_file;
    uint8_t palette[768], translation[256];
    qa_scene_image *image;
} image_cache;
typedef struct image_alias {
    struct image_alias *next;
    char *name, *request, *source_path, *logical_path;
    qa_resource *source, *logical_source, *palette_source;
    qa_vfs_acquisition source_opening, logical_opening, palette_opening;
    qa_scene_image_options decode_options;
    uint8_t palette[768], translation[256];
    qa_status source_error;
    bool palette_attempted;
    qa_status palette_error;
} image_alias;
struct qa_scene_resources {
    size_t references;
    qa_vfs *vfs;
    const qa_scene_embedded_image *embedded_images;
    size_t embedded_image_count;
    scene_names *names;
    qa_scene_image *white, *missing;
    qa_scene_image *source_white, *source_missing, *source_identity;
    qa_scene_image *source_scratch[32], *source_dlight, *source_fog;
    qa_q3_image_upload_options source_builtins_upload;
    bool source_builtins;
    qa_scene_source_image_admit_fn source_admit;
    void *source_admit_context;
    owned_image *variants;
    image_cache *cache;
    image_alias *aliases;
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
void scene_resource_alias_free(image_alias *);
qa_scene_image_alias_source scene_resource_alias_source(const image_alias *);
qa_scene_image_kind scene_resource_q3_image_kind(qa_q3_texture_format);
void scene_image_dlight_pixels(uint8_t [16 * 16 * 4]);
void scene_image_fog_pixels(uint8_t [256 * 32 * 4]);
void scene_image_skin_flood(uint8_t *, uint32_t, uint32_t, uint8_t, uint8_t, size_t *);
bool scene_resource_image_decode(qa_scene_resources *, const char *, const image_asset_recipe *,
                                 qa_scene_image **, qa_error *);
void scene_image_asset_palette(qa_scene_resources *, image_asset_recipe *, const qa_scene_image_options *);
bool scene_image_asset_copy(qa_scene_image *, const image_asset_recipe *, qa_error *);
bool scene_image_asset_source_bind(qa_scene_image *, const qa_resource *, qa_error *);
bool scene_resource_indexed_image(qa_scene_resources *, const char *, const qa_indexed_level *, size_t,
    const qa_scene_image_options *, const qa_palette_options *, bool, qa_scene_vec4,
    qa_scene_image **, qa_error *);
bool scene_resource_sky_layer(qa_scene_resources *, const char *, const qa_scene_image *, qa_bytes,
    bool, bool, qa_scene_image **, qa_error *);
void qaw_q3_shift_color(const uint8_t [3], uint32_t, uint8_t [3]);
#endif
