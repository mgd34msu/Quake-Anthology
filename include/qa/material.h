#ifndef QA_MATERIAL_H
#define QA_MATERIAL_H
#include "qa/scene.h"
#include "qa/material_source_scratch.h"
float qa_material_fog_factor(float s, float t);

typedef struct qa_material_order_entry qa_material_order_entry;
/* One renderer owner spans independently mounted provider libraries. All
 * operations are serialized. Libraries retain it; scene frames borrow it. */
qa_material_order *qa_material_order_create(qa_error *);
void qa_material_order_destroy(qa_material_order *);
bool qa_material_order_prepare(qa_material_order *, qa_error *);
bool qa_material_order_rank(const qa_material_order *, const qa_material *, uint32_t *, qa_error *);
const qa_material *qa_material_order_sorted_at(const qa_material_order *, uint32_t);
/* Physical membership, including an unpublished record; never prepares sort. */
bool qa_material_order_has_record(const qa_material_order *, const qa_material *);
/* Published renderer-wide registrations; array lives in scratch, materials
 * borrow until library/order mutation. Observation does not prepare queues. */
bool qa_material_order_snapshot(const qa_material_order *, bool sorted, qa_arena *,
                                const qa_material *const **, size_t *, qa_error *);

typedef enum qa_material_wave_kind { QA_WAVE_SIN, QA_WAVE_SQUARE, QA_WAVE_TRIANGLE,
    QA_WAVE_SAWTOOTH, QA_WAVE_INVERSE_SAWTOOTH, QA_WAVE_NOISE, QA_WAVE_NONE } qa_material_wave_kind;
typedef struct qa_material_wave { qa_material_wave_kind kind; float base, amplitude, phase, frequency; } qa_material_wave;
typedef enum qa_material_color_kind {
    QA_COLOR_IDENTITY, QA_COLOR_IDENTITY_LIGHTING, QA_COLOR_VERTEX, QA_COLOR_EXACT_VERTEX,
    QA_COLOR_ONE_MINUS_VERTEX, QA_COLOR_ENTITY, QA_COLOR_ONE_MINUS_ENTITY,
    QA_COLOR_CONSTANT, QA_COLOR_WAVE, QA_COLOR_LIGHTING_DIFFUSE, QA_COLOR_LIGHTING_SPECULAR,
    QA_COLOR_PORTAL, QA_COLOR_SKIP, QA_COLOR_BAD
} qa_material_color_kind;
typedef enum qa_material_tcgen { QA_TC_TEXTURE, QA_TC_LIGHTMAP, QA_TC_ENVIRONMENT,
    QA_TC_VECTOR, QA_TC_IDENTITY, QA_TC_FOG, QA_TC_BAD } qa_material_tcgen;
typedef enum qa_material_tcmod_kind { QA_TCMOD_SCROLL, QA_TCMOD_SCALE, QA_TCMOD_ROTATE,
    QA_TCMOD_TRANSFORM, QA_TCMOD_TURBULENCE, QA_TCMOD_STRETCH, QA_TCMOD_ENTITY_TRANSLATE,
    QA_TCMOD_NONE } qa_material_tcmod_kind;
typedef struct qa_material_tcmod { qa_material_tcmod_kind kind; float values[6]; qa_material_wave wave; } qa_material_tcmod;
typedef enum qa_material_deform_kind { QA_DEFORM_WAVE, QA_DEFORM_NORMAL, QA_DEFORM_BULGE,
    QA_DEFORM_MOVE, QA_DEFORM_AUTOSPRITE, QA_DEFORM_AUTOSPRITE2, QA_DEFORM_PROJECTION_SHADOW,
    QA_DEFORM_TEXT, QA_DEFORM_NONE } qa_material_deform_kind;
typedef struct qa_material_deform { qa_material_deform_kind kind; qa_material_wave wave;
    qa_vec3 vector; float spread, width, height, speed; uint32_t text_index; } qa_material_deform;
typedef struct qa_material_stage {
    qa_scene_state state;
    qa_scene_image **images;
    size_t image_count;
    float animation_frequency;
    char **image_names;
    char *video_name;
    uint64_t video_identity;
    bool lightmap, is_lightmap, vertex_lightmap, clamp, detail, video, retain_texture, invalid_blend;
    qa_scene_fog_effect fog_adjustment;
    qa_material_color_kind rgb, alpha;
    qa_scene_vec4 constant;
    qa_material_wave rgb_wave, alpha_wave;
    float portal_range;
    qa_material_tcgen tcgen;
    qa_vec3 tc_vectors[2];
    qa_material_tcmod *tcmods;
    size_t tcmod_count;
} qa_material_stage;
typedef struct qa_material_profile {
    bool detail_textures, vertex_lighting, ui_fullscreen, permedia2;
    bool multitexture, texture_env_add, ignore_fast_path;
} qa_material_profile;
struct qa_material {
    qa_material_library *library;
    char *name;
    uint64_t identity, revision;
    uint32_t registration, sorted_index;
    int32_t lightmap_index;
    qa_material_order_entry *order_entry;
    qa_game_family family;
    bool default_shader;
    qa_material_profile profile;
    /* Library-owned procedural images shared by every registered material. */
    const qa_scene_image *fog_image, *dlight_image;
    float sort, clamp_time, portal_range;
    qa_scene_cull cull;
    bool polygon_offset, sky, no_mipmaps, no_picmip, entity_mergable;
    uint32_t surface_flags, content_flags;
    qa_scene_fog fog;
    float sky_height;
    char *sky_outer, *sky_inner;
    const qa_scene_image *sky_outer_images[6], *sky_inner_images[6];
    qa_vec3 sun_light, sun_direction;
    bool has_sun;
    qa_material_stage *stages;
    size_t stage_count;
    qa_material_deform *deforms;
    size_t deform_count;
    const qa_material *remapped;
    float remap_time_offset;
    /* The original renderer stores remap time on the selected target shader.
     * Shared registrations retain their independent per-binding offset. */
    float source_time_offset;
    bool source_remap;
};
typedef enum qa_material_iterator { QA_MATERIAL_GENERIC, QA_MATERIAL_SKY,
    QA_MATERIAL_VERTEX_LIT, QA_MATERIAL_LIGHTMAPPED } qa_material_iterator;
/* Uses the same retained-stage planner as submission, without geometry, queue
 * execution or resource loading. Fragment lighting selects its actual path. */
bool qa_material_diagnostic_plan(const qa_material *, bool fragment_lighting,
                                  size_t *passes, qa_material_iterator *, qa_error *);
typedef struct qa_material_context {
    qa_scene_view view;
    qa_scene_matrix model;
    /* Color and light channel units are normalized: one equals source byte 255. */
    qa_scene_vec4 entity_color;
    qa_vec3 ambient, directed, light_direction, local_view_origin;
    qa_vec2 entity_texcoord;
    float identity_light, time_offset, shadow_plane;
    /* Entity setup packs ambient alpha as one; the unlit world default is zero. */
    float ambient_alpha;
    double seconds;
    int64_t milliseconds;
    const qa_scene_image *lightmap;
    qa_scene_fog fog;
    /* Q3 brush fog uses camera depth and an optional inward boundary plane.
     * A zero scale disables this volume path. Global scene fog is independent. */
    float fog_tc_scale;
    bool fog_has_surface;
    qa_scene_plane fog_surface;
    qa_vec3 fog_volume_color;
    const qa_scene_light *lights;
    size_t light_count;
    /* Q2 rerelease fragment lights also illuminate authored Q3 stages. */
    const qa_scene_shadow_light *fragment_lights;
    size_t fragment_light_count;
    const qa_scene_image *shadow_atlas;
    float shadow_near;
    bool fragment_lighting;
    uint32_t light_mask, entity, fog_index;
    bool source_entity_cell;
    bool mirror, non_normalized_axis, projection_shadow;
    bool source_primitives, source_depth_hack, source_sky_depth, source_picture;
    bool source_dlighted;
    bool source_dlight_before_overflow;
    const qa_scene_world *source_light_world;
    qa_scene_world *source_sky_world;
    float source_sky_far_clip;
    uint32_t source_light_surface;
    qa_scene_matrix source_picture_projection;
    qa_material_source_scratch *source_scratch;
    const qa_scene_image *source_white;
    qa_scene_recipient_image_fn source_recipient_image;
    void *source_recipient_context;
    qa_scene_source_diagnostics source_diagnostics;
    qa_scene_source_diagnostics_read_fn source_diagnostics_read;
    void *source_diagnostics_context;
    int32_t (*source_picture_clock)(void *);
    void *source_picture_clock_context;
    qa_material_source_writer source_writer;
    const qa_material *source_default_material;
    size_t source_grid_columns, source_grid_rows;
    bool (*source_model_pose)(void *, int32_t, int32_t, float,
        qa_scene_frame *, qa_scene_mesh *, qa_error *);
    int32_t source_model_frame, source_model_old_frame;
    float source_model_back_lerp;
    bool (*source_model_retain)(void *, qa_error *);
    void (*source_model_release)(void *);
    void *source_model_context;
    qa_q3_presentation_assets *source_model_assets;
    bool source_cell_geometry;
    bool (*source_entity_surface)(void *, const qa_q3_ref_entity *, const qa_material *,
        struct qa_material_context *, qa_scene_frame *, qa_scene_mesh *, bool *direct, qa_error *);
    void *source_entity_surface_context;
    bool (*source_surface)(void *, const qa_material *, const qa_material *,
        const qa_scene_mesh *, const struct qa_material_context *, qa_scene_frame *, qa_error *);
    void *source_surface_context;
    /* NUL-terminated source-byte glyph rows, as stored in Q3 refdef text. */
    const char *const *texts;
    size_t text_count;
    /* Resolve dynamic video images at the reached draw, retaining the returned
     * immutable version through frame ownership. NULL uses the loaded image. */
    const qa_scene_image *(*video_image)(void *, const char *, double, qa_error *);
    /* The initial image identity distinguishes independent playback instances
     * of the same movie. Prefer this callback when the host owns such instances. */
    const qa_scene_image *(*video_frame)(void *, uint64_t, double, qa_error *);
    void *video_context;
} qa_material_context;
bool qa_material_source_scene_sky(qa_material_source_scratch *, const qa_material_context *, qa_error *);
bool qa_material_source_commands(const qa_material *, const qa_material_context *,
    qa_scene_frame *, size_t first_command, qa_error *);
qa_material_library *qa_material_library_create(qa_scene_resources *, qa_material_order *, qa_error *);
/* Registration starts cinematics in source directive order. The returned image
 * is borrowed; the library retains it. NULL means the cinematic did not start. */
typedef const qa_scene_image *(*qa_material_video_start_fn)(void *, const char *, qa_error *);
void qa_material_library_set_video_start(qa_material_library *, qa_material_video_start_fn, void *);
/* Set before registering content. Changes after registrations require rebuilding
 * the material library and its dependent scene resources at renderer restart. */
bool qa_material_library_set_profile(qa_material_library *, const qa_material_profile *, qa_error *);
/* Called by an actual Source renderer constructor before content registration.
 * The retained tag distinguishes Source restart profiles from shared recipes. */
bool qa_material_library_set_source_profile(qa_material_library *, const qa_material_profile *, qa_error *);
bool qa_material_library_has_source_profile(const qa_material_library *);
bool qa_material_library_source_profile_read(const qa_material_library *, qa_material_profile *, qa_error *);
/* Actual Source initialization, after script loading and before scene admission. */
bool qa_material_library_source_shaders_initialize(qa_material_library *,
    const qa_scene_image_options *, qa_error *);
typedef bool (*qa_material_source_upload_fn)(void *, bool allow_picmip, bool mipmap,
    qa_q3_image_upload_options *, qa_error *);
/* Bind the actual Source renderer's current upload producer. Restored image
 * and material records keep their saved first-upload profiles unchanged. */
bool qa_material_library_set_source_upload(qa_material_library *, qa_material_source_upload_fn, void *, qa_error *);
bool qa_material_library_source_upload_is(const qa_material_library *, qa_material_source_upload_fn, const void *);
typedef bool (*qa_material_source_ui_fullscreen_fn)(void *, bool *, qa_error *);
bool qa_material_library_set_source_ui_fullscreen(qa_material_library *, qa_material_source_ui_fullscreen_fn, void *, qa_error *);
bool qa_material_library_source_ui_fullscreen_is(const qa_material_library *, qa_material_source_ui_fullscreen_fn, const void *);
void qa_material_library_destroy(qa_material_library *);
bool qa_material_library_retain(qa_material_library *, qa_error *);
/* Retains the actual registered material's library and owned records. */
bool qa_material_retain(const qa_material *, qa_error *);
void qa_material_release(const qa_material *);
bool qa_material_library_parse(qa_material_library *, qa_bytes, const qa_scene_image_options *, qa_error *);
/* Own the authored shader dependency decoder family and palette separately
 * from the eventual renderer recipient. Only these scope fields are copied. */
bool qa_material_library_parse_scoped(qa_material_library *, qa_bytes, const qa_scene_image_options *, qa_error *);
bool qa_material_library_load_scripts(qa_material_library *, qa_vfs *, const qa_scene_image_options *, qa_error *);
const qa_material *qa_material_find(const qa_material_library *, const char *);
bool qa_material_has_authored(const qa_material_library *, const char *);
/* A registered recipient variant keeps the original compiled program and
 * maps only its real retained image inputs into Source uploads. */
bool qa_material_source_q3_variant(qa_material_library *, const qa_material *,
    const qa_q3_image_upload_options *, const qa_material **, qa_error *);
/* Source skyParms rebuilds one renderer-global cloud table. */
float qa_material_library_cloud_height(const qa_material_library *);
bool qa_material_library_sun(const qa_material_library *, qa_vec3 *light, qa_vec3 *direction);
typedef enum qa_material_registration_kind {
    QA_MATERIAL_DYNAMIC, QA_MATERIAL_LIGHTMAP, QA_MATERIAL_VERTEX,
    QA_MATERIAL_WHITE, QA_MATERIAL_PICTURE, QA_MATERIAL_DEFAULT,
    QA_MATERIAL_STENCIL_SHADOW
} qa_material_registration_kind;
bool qa_material_registration_read(const qa_material *, qa_material_registration_kind *);
bool qa_material_register_kind(qa_material_library *, const char *, const qa_scene_image_options *,
                                qa_material_registration_kind, const qa_material **, qa_error *);
/* World identity must be stable and unique for that world's lifetime. Q3 source
 * registration and ordering distinguish lightmap indexes even when scripts match.
 * Negative indexes retain Q3 sentinels: -1 dynamic, -2 white, -3 vertex, -4 picture. */
bool qa_material_register_world(qa_material_library *, const char *, const qa_scene_image_options *,
                                 uint64_t world_identity, int32_t lightmap_index,
                                 qa_material_registration_kind, const char *base_name,
                                 const qa_scene_image *base_image,
                                 const qa_material **, qa_error *);
/* Generated atlas images override scripts/files for new registrations. The
 * first image per canonical name wins; defaulted handles recover in place. */
bool qa_material_register_generated_picture(qa_material_library *, const char *,
                                            const qa_scene_image *, const qa_material **, qa_error *);
/* Advance stage-owned sampling variants as well as resource-loaded GIFs. */
bool qa_material_library_animate(qa_material_library *, double seconds, qa_scene_frame *, qa_error *);
bool qa_material_register(qa_material_library *, const char *, const qa_scene_image_options *,
                           bool lightmapped, const qa_material **, qa_error *);
bool qa_material_remap(qa_material_library *, const char *, const char *, float, qa_error *);
typedef enum qa_material_source_remap_status {
    QA_MATERIAL_SOURCE_REMAP_APPLIED,
    QA_MATERIAL_SOURCE_REMAP_ORIGINAL_DEFAULT,
    QA_MATERIAL_SOURCE_REMAP_TARGET_DEFAULT
} qa_material_source_remap_status;
/* Source binds all currently admitted original-name variants to one actual
 * target shader. A defaulted name is a successful warning result; no binding
 * changes. Future registrations receive no inferred Source remap recipe. */
bool qa_material_remap_source(qa_material_library *, const char *, const char *, float,
    qa_material_source_remap_status *, qa_error *);
bool qa_scene_world_remap_source(qa_scene_world *, const char *, const char *, float,
    qa_material_source_remap_status *, qa_error *);
float qa_material_wave_evaluate(const qa_material_wave *, double seconds);
float qa_material_sine(unsigned index);
bool qa_material_submit(const qa_material *, const qa_scene_mesh *, const qa_material_context *,
                         qa_scene_frame *, qa_error *);
/* Empty output means the material does not cast an ordinary geometry shadow.
 * Otherwise output shares its input or owns transient vertices in the frame. */
bool qa_material_shadow_mesh(const qa_material *, const qa_scene_mesh *, const qa_material_context *,
                              qa_scene_frame *, qa_scene_mesh *, qa_error *);

#endif
