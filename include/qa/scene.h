#ifndef QA_SCENE_H
#define QA_SCENE_H

#include "qa/arena.h"
#include "qa/bsp.h"
#include "qa/image.h"
#include "qa/model.h"
#include "qa/vfs.h"

typedef struct qa_material qa_material;
typedef struct qa_material_library qa_material_library;
typedef struct qa_material_order qa_material_order;
typedef struct qa_scene_resources qa_scene_resources;
typedef struct qa_scene_world qa_scene_world;
typedef struct qa_scene_model qa_scene_model;
typedef struct qa_scene_geometry qa_scene_geometry;

typedef struct qa_scene_vec2 { float x, y; } qa_scene_vec2;
typedef struct qa_scene_vec4 { float x, y, z, w; } qa_scene_vec4;
typedef struct qa_scene_matrix { float m[16]; } qa_scene_matrix;
typedef struct qa_scene_rect { int32_t x, y; uint32_t width, height; } qa_scene_rect;
typedef struct qa_scene_rect_f { float x, y, width, height; } qa_scene_rect_f;
typedef struct qa_scene_plane { qa_vec3 normal; float distance; } qa_scene_plane;
typedef enum qa_scene_family { QA_SCENE_Q1, QA_SCENE_Q2, QA_SCENE_Q3 } qa_scene_family;
typedef enum qa_scene_wrap { QA_SCENE_REPEAT, QA_SCENE_CLAMP } qa_scene_wrap;
typedef enum qa_scene_filter {
    QA_SCENE_NEAREST, QA_SCENE_LINEAR, QA_SCENE_NEAREST_MIPMAP_NEAREST,
    QA_SCENE_LINEAR_MIPMAP_NEAREST, QA_SCENE_NEAREST_MIPMAP_LINEAR,
    QA_SCENE_LINEAR_MIPMAP_LINEAR
} qa_scene_filter;
typedef enum qa_scene_image_kind { QA_SCENE_RGBA8, QA_SCENE_RGB8, QA_SCENE_DEPTH32F } qa_scene_image_kind;
/* Color pixels occupy four bytes even for RGB8; depth pixels are float32. */
typedef struct qa_scene_image_level { uint32_t width, height; const void *pixels; size_t bytes; } qa_scene_image_level;
/* Versions own their pixels. Backends key residency by identity and revision.
 * Frame references keep replaced versions alive until the frame is reset. */
typedef struct qa_scene_image {
    uint64_t identity, revision;
    const char *name;
    qa_scene_image_kind kind;
    qa_scene_wrap wrap;
    qa_scene_filter filter;
    qa_scene_vec4 border;
    const qa_scene_image_level *levels;
    size_t level_count;
    uint32_t logical_width, logical_height;
    /* GIF playback uses the donor's fixed 10 Hz clock. Element zero is NULL
     * and denotes this version; subsequent entries are owned immutable images. */
    const struct qa_scene_image *const *animation;
    size_t animation_count;
    size_t references;
} qa_scene_image;
typedef enum qa_scene_image_usage { QA_IMAGE_USAGE_DEFAULT, QA_IMAGE_USAGE_SKIN,
    QA_IMAGE_USAGE_SPRITE, QA_IMAGE_USAGE_WALL, QA_IMAGE_USAGE_PICTURE, QA_IMAGE_USAGE_SKY } qa_scene_image_usage;
typedef struct qa_scene_image_options {
    qa_scene_family family;
    qa_scene_wrap wrap;
    qa_scene_filter filter;
    qa_scene_image_usage usage;
    bool mipmap, transparent, fullbright_only;
    int transparent_index;
    qa_bytes palette_rgb, translation;
} qa_scene_image_options;
typedef enum qa_scene_image_format { QA_SCENE_IMAGE_PNG, QA_SCENE_IMAGE_JPG,
    QA_SCENE_IMAGE_TGA, QA_SCENE_IMAGE_JPEG, QA_SCENE_IMAGE_BMP, QA_SCENE_IMAGE_GIF } qa_scene_image_format;
typedef struct qa_scene_image_policy {
    int32_t override_level;
    /* Source image-type bits: skin1, sprite2, wall4, picture8, sky16. */
    uint32_t override_usages;
    qa_scene_image_format formats[6];
    size_t format_count;
    bool source_formats;
} qa_scene_image_policy;

qa_scene_resources *qa_scene_resources_create(qa_vfs *, qa_error *);
void qa_scene_resources_destroy(qa_scene_resources *);
/* Live immutable image versions allocated by this resource owner. Array lives
 * in scratch; images borrow until the next owner/image mutation. No loading. */
bool qa_scene_resources_images(const qa_scene_resources *, qa_arena *,
                               const qa_scene_image *const **, size_t *, qa_error *);
/* Set image policy/fullbright range before content loads. Recreate resources
 * and dependent worlds/models when these registration settings change. */
bool qa_scene_image_policy_controls(int32_t override_level, uint32_t usage_mask,
                                    const char *formats, qa_scene_image_policy *, qa_error *);
bool qa_scene_resources_set_image_policy(qa_scene_resources *, qa_scene_family,
                                         const qa_scene_image_policy *, qa_error *);
bool qa_scene_resources_set_fullbright_first(qa_scene_resources *, unsigned, qa_error *);
unsigned qa_scene_resources_fullbright_first(const qa_scene_resources *);
uint64_t qa_scene_identity(void);
/* Returned palette borrows the resource service and is RGB, 256 entries. */
bool qa_scene_resources_palette(qa_scene_resources *, qa_scene_family, qa_bytes *, qa_error *);
bool qa_scene_image_create(qa_scene_resources *, const char *, qa_scene_image_kind,
                          const qa_scene_image_level *, size_t, qa_scene_wrap,
                          qa_scene_filter, qa_scene_vec4, qa_scene_image **, qa_error *);
bool qa_scene_image_load(qa_scene_resources *, const char *, const qa_scene_image_options *,
                        qa_scene_image **, qa_error *);
bool qa_scene_image_replace(qa_scene_resources *, const qa_scene_image *, size_t level,
                           const qa_scene_image_level *, qa_scene_image **, qa_error *);
bool qa_scene_image_sample(qa_scene_resources *, const qa_scene_image *, bool mipmap,
                           qa_scene_wrap, qa_scene_image **, qa_error *);
void qa_scene_image_retain(const qa_scene_image *);
void qa_scene_image_release(const qa_scene_image *);
const qa_scene_image *qa_scene_image_at_time(const qa_scene_image *, double seconds);
const qa_scene_image *qa_scene_white(const qa_scene_resources *);
const qa_scene_image *qa_scene_missing(const qa_scene_resources *);

typedef struct qa_scene_vertex {
    qa_vec3 position, normal;
    qa_scene_vec2 texcoord, lightmap;
    qa_scene_vec4 color;
} qa_scene_vertex;
/* Takes both malloc-compatible arrays only on success, without copying. Finish
 * writing before publishing the first frame; replace rather than mutate a
 * published version. Producer/frame references own the arrays. Retain requires
 * an existing active reference; cached retirement records cannot be revived.
 * References are atomic, but callers must synchronize frame publication and never reset a
 * frame while a backend consumes it. */
qa_scene_geometry *qa_scene_geometry_adopt(qa_scene_vertex *, uint32_t *, qa_error *);
void qa_scene_geometry_retain(const qa_scene_geometry *);
void qa_scene_geometry_release(const qa_scene_geometry *);
/* Backend residency keeps only the retirement record alive. It cannot prolong
 * active geometry indefinitely across multiple renderer caches. */
void qa_scene_geometry_cache_retain(const qa_scene_geometry *);
void qa_scene_geometry_cache_release(const qa_scene_geometry *);
bool qa_scene_geometry_active(const qa_scene_geometry *);
typedef enum qa_scene_primitive { QA_SCENE_TRIANGLES, QA_SCENE_LINES } qa_scene_primitive;
/* Nonzero identity/revision denotes immutable geometry owned by geometry.
 * Identity zero uses streaming storage. Such a mesh may still borrow retained
 * indices or vertices; preserve geometry when making that transient copy. */
typedef struct qa_scene_mesh {
    uint64_t identity, revision;
    const qa_scene_vertex *vertices;
    const uint32_t *indices;
    size_t vertex_count, index_count;
    qa_bounds bounds;
    qa_scene_primitive primitive;
    const qa_scene_geometry *geometry;
} qa_scene_mesh;
typedef enum qa_scene_blend {
    QA_BLEND_ZERO, QA_BLEND_ONE, QA_BLEND_SRC_COLOR, QA_BLEND_ONE_MINUS_SRC_COLOR,
    QA_BLEND_SRC_ALPHA, QA_BLEND_ONE_MINUS_SRC_ALPHA, QA_BLEND_DST_ALPHA,
    QA_BLEND_ONE_MINUS_DST_ALPHA, QA_BLEND_DST_COLOR, QA_BLEND_ONE_MINUS_DST_COLOR,
    QA_BLEND_SRC_ALPHA_SATURATE
} qa_scene_blend;
typedef enum qa_scene_depth { QA_DEPTH_ALWAYS, QA_DEPTH_LEQUAL, QA_DEPTH_EQUAL, QA_DEPTH_LESS } qa_scene_depth;
typedef enum qa_scene_alpha { QA_ALPHA_NONE, QA_ALPHA_GT0, QA_ALPHA_LT128, QA_ALPHA_GE128 } qa_scene_alpha;
typedef enum qa_scene_cull { QA_CULL_NONE, QA_CULL_FRONT, QA_CULL_BACK } qa_scene_cull;
typedef enum qa_scene_stencil_op { QA_STENCIL_KEEP, QA_STENCIL_ZERO, QA_STENCIL_REPLACE, QA_STENCIL_INCREMENT, QA_STENCIL_DECREMENT, QA_STENCIL_INVERT } qa_scene_stencil_op;
typedef enum qa_scene_stencil_test { QA_STENCIL_ALWAYS, QA_STENCIL_EQUAL, QA_STENCIL_NOTEQUAL } qa_scene_stencil_test;
typedef struct qa_scene_state {
    qa_scene_blend blend_source, blend_destination;
    qa_scene_depth depth_test;
    qa_scene_alpha alpha_test;
    qa_scene_cull cull;
    bool depth_write, color_write, polygon_offset, wireframe;
    float depth_near, depth_far, offset_factor, offset_units, line_width;
    bool stencil_enabled;
    qa_scene_stencil_test stencil_test;
    uint32_t stencil_reference, stencil_compare_mask, stencil_write_mask;
    qa_scene_stencil_op stencil_fail, stencil_depth_fail, stencil_depth_pass;
} qa_scene_state;
typedef enum qa_scene_texture_environment { QA_TEXTURE_MODULATE, QA_TEXTURE_ADD, QA_TEXTURE_REPLACE } qa_scene_texture_environment;
typedef enum qa_scene_fog_kind { QA_FOG_NONE, QA_FOG_CONSTANT, QA_FOG_EXP2, QA_FOG_Q2 } qa_scene_fog_kind;
typedef enum qa_scene_fog_effect { QA_FOG_COLOR, QA_FOG_RGB, QA_FOG_ALPHA, QA_FOG_RGBA, QA_FOG_OVERLAY, QA_FOG_NO_EFFECT } qa_scene_fog_effect;
typedef struct qa_scene_fog {
    qa_scene_fog_kind kind;
    qa_scene_fog_effect effect;
    qa_vec3 color, height_color, height_end_color;
    float density, amount, sky_factor, height_density, height_start, height_end, height_falloff;
    float far_depth;
    bool sky_drawn;
} qa_scene_fog;
typedef struct qa_scene_light {
    qa_vec3 origin, color, direction;
    float radius, minimum, scale, cos_half_angle;
    bool additive, spot, casts_shadow;
    uint64_t identity, revision;
    uint32_t shadow_resolution;
    qa_scene_family family;
} qa_scene_light;
typedef struct qa_scene_shadow_light {
    qa_scene_light light;
    qa_scene_vec4 atlas_rect;
    qa_scene_matrix shadow_matrix;
    bool point_shadow, shadow_valid;
    qa_vec3 model_fraction;
} qa_scene_shadow_light;
typedef enum qa_scene_lighting_kind { QA_LIGHT_VERTEX, QA_LIGHT_Q2_WORLD, QA_LIGHT_Q2_MODEL_SHADOW } qa_scene_lighting_kind;
typedef enum qa_scene_light_pass { QA_LIGHT_PASS_TEXTURE, QA_LIGHT_PASS_LIGHTMAP,
    QA_LIGHT_PASS_MATERIAL_LIGHTMAP, QA_LIGHT_PASS_MODEL } qa_scene_light_pass;
typedef struct qa_scene_draw {
    qa_scene_mesh mesh;
    qa_scene_matrix model, mvp;
    const qa_scene_image *textures[2];
    /* Preserve the preceding binding for a source stage whose video/image
     * registration intentionally left that texture unit unchanged. */
    bool retain_texture[2];
    uint8_t texture_count;
    qa_scene_texture_environment environment;
    qa_scene_state state;
    qa_scene_fog fog;
    qa_scene_lighting_kind lighting;
    qa_scene_light_pass light_pass;
    const qa_scene_shadow_light *lights;
    size_t light_count;
    const qa_scene_image *shadow_atlas;
    float shadow_near, shade_scale;
    bool model_shade_scale;
    bool luminance_alpha;
    /* Packed Q3 shader/entity/fog/light order or caller's ordered sequence. */
    uint64_t sort_key;
    uint32_t entity, fog_index, light_mask;
} qa_scene_draw;
typedef struct qa_scene_view {
    /* Every viewport, including a depth target, uses top-left pixel origin. */
    qa_scene_rect viewport;
    qa_vec3 origin, axis[3];
    qa_scene_matrix projection;
    bool clear_color, clear_depth, clear_stencil, clip_enabled, mirror;
    qa_scene_vec4 color;
    float depth;
    qa_scene_plane clip_plane;
    uint32_t seat;
} qa_scene_view;
typedef enum qa_scene_draw_buffer { QA_DRAW_FRONT, QA_DRAW_BACK, QA_DRAW_BACK_LEFT, QA_DRAW_BACK_RIGHT } qa_scene_draw_buffer;
typedef enum qa_scene_command_kind {
    QA_SCENE_COMMAND_VIEW, QA_SCENE_COMMAND_DRAW, QA_SCENE_COMMAND_TARGET,
    QA_SCENE_COMMAND_OPACITY_BEGIN, QA_SCENE_COMMAND_OPACITY_END,
    QA_SCENE_COMMAND_FOG, QA_SCENE_COMMAND_DRAW_BUFFER, QA_SCENE_COMMAND_SWAP,
    QA_SCENE_COMMAND_IMAGE
} qa_scene_command_kind;
typedef struct qa_scene_command {
    qa_scene_command_kind kind;
    union {
        qa_scene_view view;
        qa_scene_draw draw;
        struct { const qa_scene_image *image; } target; /* NULL restores display target. */
        struct { float value; } opacity;
        struct { qa_scene_fog fog; qa_scene_view view; } fog;
        struct { qa_scene_draw_buffer buffer; bool clear; } draw_buffer;
        /* Publish a new immutable version, including any retained binding with
         * the same identity. This preserves source update-image behavior. */
        const qa_scene_image *image;
    } data;
} qa_scene_command;
typedef enum qa_scene_group_kind { QA_SCENE_GROUP_COMPILED, QA_SCENE_GROUP_SOURCE,
    QA_SCENE_GROUP_SEQUENCE } qa_scene_group_kind;
typedef struct qa_scene_group {
    size_t first, count, ordinal;
    qa_scene_group_kind kind;
    const qa_material *material;
    float priority;
    uint32_t entity, fog, dlight, source_sort;
} qa_scene_group;
/* Owns its arrays; do not shallow-copy a frame. Reset only once every consuming
 * backend has completed it. Commands are contiguous; transient geometry lives
 * in storage. Frame geometry pins survive command rollback until reset. */
typedef struct qa_scene_frame {
    uint64_t sequence, owner;
    /* Borrowed renderer registration owner. Libraries and their material
     * records outlive preparation of every pending source group. */
    qa_material_order *material_order;
    qa_arena storage;
    qa_scene_command *commands;
    size_t command_count, command_capacity;
    const qa_scene_image **images;
    size_t image_count, image_capacity;
    const qa_scene_geometry **geometries;
    size_t geometry_count, geometry_capacity;
    qa_scene_group *groups;
    size_t group_count, group_capacity;
    qa_scene_group **sort_groups;
    size_t sort_group_capacity;
    qa_scene_command *sort_commands;
    size_t sort_command_capacity;
} qa_scene_frame;
void qa_scene_frame_init(qa_scene_frame *, uint64_t owner);
bool qa_scene_frame_material_order(qa_scene_frame *, qa_material_order *, qa_error *);
void qa_scene_frame_reset(qa_scene_frame *, uint64_t sequence);
void qa_scene_frame_destroy(qa_scene_frame *);
bool qa_scene_frame_emit(qa_scene_frame *, const qa_scene_command *, qa_error *);
bool qa_scene_frame_draw(qa_scene_frame *, const qa_scene_draw *, qa_error *);
/* Pin borrowed retained geometry, including intermediate shadow-caster data,
 * until reset. No command is emitted. NULL geometry needs no reference. */
bool qa_scene_frame_geometry(qa_scene_frame *, const qa_scene_geometry *, qa_error *);
bool qa_scene_frame_image(qa_scene_frame *, const qa_scene_image *, qa_error *);
/* Publish GIF frame versions before any views, using the global render clock. */
bool qa_scene_resources_animate(qa_scene_resources *, double seconds, qa_scene_frame *, qa_error *);
/* Mark the just-appended commands as one indivisible surface/model group.
 * Ungrouped commands are barriers. Mark groups in append order; never nest.
 * Source shader ranks are resolved at finish, after all registrations exist. */
bool qa_scene_frame_group(qa_scene_frame *, size_t first, qa_scene_group_kind,
                          const qa_material *, float priority, uint32_t entity,
                          uint32_t fog, uint32_t dlight, qa_error *);
/* Finish a whole view only after world, models and effects are appended. It
 * sorts pending groups once and optionally appends the Q2 depth-fog pass. */
bool qa_scene_frame_finish(qa_scene_frame *, const qa_scene_view *, const qa_scene_fog *, qa_error *);
bool qa_scene_frame_picture(qa_scene_frame *, const qa_scene_image *, qa_scene_rect target,
                            qa_scene_rect rect, qa_scene_vec4 uv, qa_scene_vec4 color, qa_error *);
bool qa_scene_frame_picture_f(qa_scene_frame *, const qa_scene_image *, qa_scene_rect target,
                              qa_scene_rect_f rect, qa_scene_vec4 uv, qa_scene_vec4 color, qa_error *);
/* Clipped pixel-space quad owned by frame storage. Empty output means no overlap. */
bool qa_scene_picture_geometry(qa_scene_frame *, qa_scene_rect target, qa_scene_rect_f,
                               qa_scene_vec4 uv, qa_scene_vec4 color, qa_scene_mesh *, qa_error *);
void qa_scene_state_default(qa_scene_state *);
void qa_scene_matrix_identity(qa_scene_matrix *);
qa_scene_matrix qa_scene_matrix_multiply(qa_scene_matrix, qa_scene_matrix);
qa_scene_matrix qa_scene_view_matrix(const qa_scene_view *);
qa_scene_matrix qa_scene_projection(float fov_x, float fov_y, float near_clip, float far_clip);
qa_scene_vec4 qa_scene_matrix_point(qa_scene_matrix, qa_vec3);
qa_scene_matrix qa_scene_model_matrix(const qa_model_transform *);
/* Four lateral planes plus the optional portal plane; near/far clipping belongs
 * to rasterization. Q3 source traversal deliberately selects only the first4. */
size_t qa_scene_frustum(const qa_scene_view *, qa_scene_plane planes[6]);
bool qa_scene_bounds_visible(qa_bounds, const qa_scene_plane *, size_t);

typedef enum qa_scene_q1_lightmap_encoding { QA_Q1_LIGHTMAP_RGB,
    QA_Q1_LIGHTMAP_INVERTED_LUMINANCE, QA_Q1_LIGHTMAP_INVERTED_ALPHA } qa_scene_q1_lightmap_encoding;
typedef struct qa_scene_world_options {
    qa_scene_image_options images;
    float subdivisions, q1_water_alpha, q2_light_modulate;
    uint32_t q3_overbright;
    const char *q2_sky;
    qa_bytes external_lit;
    qa_scene_q1_lightmap_encoding q1_lightmap_encoding;
} qa_scene_world_options;
typedef struct qa_scene_world_entity {
    qa_vec3 ambient, directed, light_direction;
    qa_scene_vec2 shader_texcoord;
    float shader_time, shadow_plane;
    bool non_normalized_axis, projection_shadow;
} qa_scene_world_entity;
typedef struct qa_scene_world_input {
    qa_scene_view view;
    double seconds;
    int64_t milliseconds;
    bool no_world, no_vis, no_cull, alternate_animation;
    qa_vec3 pvs_origin;
    bool use_pvs_origin;
    int32_t secondary_cluster;
    bool use_secondary_cluster;
    const uint8_t *visible_areas;
    size_t visible_area_bytes;
    const float *q1_styles;
    const qa_vec3 *q2_styles;
    size_t style_count;
    const qa_scene_light *lights;
    size_t light_count;
    bool use_projected_lights, source_order;
    const qa_scene_light *projected_lights;
    size_t projected_light_count;
    qa_scene_fog fog;
    float curve_error, identity_light;
    uint32_t animation_frame;
    bool use_animation_frame;
    const qa_scene_shadow_light *shadow_lights;
    size_t shadow_light_count;
    const qa_scene_image *shadow_atlas;
    const qa_scene_image *sky_images[6];
    bool override_sky, sky_auto_rotate;
    float sky_rotation;
    qa_vec3 sky_axis;
    const char *const *render_texts;
    size_t render_text_count;
    const qa_scene_image *(*video_frame)(void *, uint64_t initial_image_identity, double, qa_error *);
    void *video_context;
    /* Flare admission remains source-owned; this callback submits its visible
     * geometry into the same view without introducing another renderer. */
    bool (*flare)(void *, uint32_t surface, qa_vec3 origin, qa_vec3 color,
                  qa_vec3 normal, const qa_scene_view *, qa_scene_frame *, qa_error *);
    void *flare_context;
    /* Optional inline-model material context, borrowed for one submission. */
    const qa_scene_world_entity *entity_material;
} qa_scene_world_input;
/* World retains a private immutable BSP byte copy and owns render resources;
 * material library and resource service must outlive it. */
bool qa_scene_world_create(const qa_bsp_view *, qa_scene_resources *, qa_material_library *,
                           const qa_scene_world_options *, qa_scene_world **, qa_error *);
void qa_scene_world_destroy(qa_scene_world *);
int32_t qa_scene_world_leaf(const qa_scene_world *, qa_vec3);
bool qa_scene_world_submit(qa_scene_world *, const qa_scene_world_input *, qa_scene_frame *, qa_error *);
bool qa_scene_world_submit_model(qa_scene_world *, uint32_t model, const qa_model_transform *,
                                 const qa_scene_world_input *, uint32_t entity,
                                 qa_scene_vec4 color, qa_scene_frame *, qa_error *);
bool qa_scene_world_sample_light(const qa_scene_world *, qa_vec3 point, qa_vec3 *ambient,
                                 qa_vec3 *directed, qa_vec3 *direction);
qa_bounds qa_scene_world_bounds(const qa_scene_world *);
bool qa_scene_world_sky_drawn(const qa_scene_world *);
bool qa_scene_world_remap(qa_scene_world *, const char *original, const char *replacement,
                          float time_offset, qa_error *);
typedef struct qa_scene_fog_volume {
    uint32_t index;
    qa_scene_fog fog;
    float tc_scale;
    bool has_surface;
    qa_scene_plane surface;
} qa_scene_fog_volume;
/* Returns the first source fog containing the sphere. A miss clears out. */
bool qa_scene_world_fog_for_sphere(const qa_scene_world *, qa_vec3, float,
                                  qa_scene_fog_volume *out);
/* Inclusive bounds overlap, in source fog order. Invalid bounds or a miss clear out. */
bool qa_scene_world_fog_for_bounds(const qa_scene_world *, qa_bounds,
                                  qa_scene_fog_volume *out);

typedef struct qa_scene_model_input qa_scene_model_input;
typedef struct qa_scene_model_attachment {
    const char *tag;
    qa_scene_model *model;
    const qa_scene_model_input *input;
} qa_scene_model_attachment;
struct qa_scene_model_input {
    qa_scene_view view;
    qa_model_transform transform;
    qa_vec3 previous_origin, ambient, directed, light_direction;
    qa_scene_vec4 color;
    qa_scene_family family;
    uint32_t frame, old_frame, skin, flags, entity, lod;
    float back_lerp, radius, rotation, shadow_plane, identity_light;
    double seconds, sync_base;
    const qa_model_pose *pose;
    size_t pose_count;
    const qa_material *custom_material;
    const qa_model_skin_map *custom_skin;
    qa_scene_fog fog;
    const char *source_path;
    const qa_model_replacement *replacement;
    const qa_model_animation *animation;
    const qa_scene_model_attachment *attachments;
    size_t attachment_count;
    bool view_model, player, infrared, monochrome, no_cull, planar_shadow, shadow_only;
    bool non_normalized_axis;
    bool source_order, fog_has_surface;
    uint32_t fog_index;
    float fog_tc_scale;
    qa_scene_plane fog_surface;
    uint32_t shadow_mode;
    bool model_beam;
    float beam_segment_length;
    uint8_t left_hand;
    float shader_time, q1_overbright;
    qa_scene_vec2 shader_texcoord;
    const char *const *render_texts;
    size_t render_text_count;
    const qa_scene_image *(*video_frame)(void *, uint64_t initial_image_identity, double, qa_error *);
    void *video_context;
    const qa_scene_shadow_light *shadow_lights;
    size_t shadow_light_count;
    const qa_scene_image *shadow_atlas;
};
/* Borrows immutable decoded model; caller keeps it alive until destruction. */
bool qa_scene_model_create(const qa_model *, qa_scene_resources *, qa_material_library *,
                           const qa_scene_image_options *, qa_scene_model **, qa_error *);
void qa_scene_model_destroy(qa_scene_model *);
uint32_t qa_scene_model_effect_flags(const qa_scene_model *);
bool qa_scene_model_submit(qa_scene_model *, const qa_scene_model_input *, qa_scene_frame *, qa_error *);
uint32_t qa_scene_model_select_lod(const qa_scene_model_input *, uint32_t count,
                                  float radius, float lod_scale, float lod_bias);

typedef struct qa_scene_portal {
    qa_vec3 origin, old_origin, axis[3];
    float rotation_speed, rotation_offset, range;
    bool mirror, oscillate;
} qa_scene_portal;
/* Source allows one child portal per unclipped seat view. A found child is
 * submitted before its parent with its returned PVS origin and no recursion. */
bool qa_scene_world_portal_view(qa_scene_world *, const qa_scene_world_input *,
                                const qa_scene_portal *, size_t, qa_scene_view *,
                                qa_vec3 *pvs_origin, bool *found, qa_error *);
bool qa_scene_portal_view(const qa_scene_view *, qa_scene_plane, const qa_scene_portal *,
                          double seconds, qa_scene_view *, qa_vec3 *pvs_origin);
bool qa_scene_particle(qa_scene_frame *, const qa_scene_view *, qa_vec3 origin,
                       float radius, float rotation, qa_scene_vec4 color,
                       const qa_scene_image *, bool additive, qa_error *);
bool qa_scene_beam(qa_scene_frame *, const qa_scene_view *, qa_vec3 start, qa_vec3 end,
                   float width, qa_scene_vec4 color, const qa_scene_image *, qa_error *);
bool qa_scene_sky(qa_scene_frame *, const qa_scene_view *,
                  const qa_scene_image *const images[6], float radius, float rotation,
                  qa_vec3 axis, qa_scene_vec4 color, qa_error *);
typedef struct qa_scene_shadow_caster {
    const qa_scene_mesh *meshes;
    size_t mesh_count;
    qa_scene_matrix transform;
    qa_bounds bounds;
    uint64_t identity, revision;
    bool world_geometry;
} qa_scene_shadow_caster;
bool qa_scene_world_shadow_caster(qa_scene_world *, uint32_t model,
                                  const qa_model_transform *, const qa_scene_world_input *,
                                  qa_scene_frame *, qa_scene_shadow_caster *, qa_error *);
bool qa_scene_model_shadow_caster(qa_scene_model *, const qa_scene_model_input *,
                                  qa_scene_frame *, qa_scene_shadow_caster *, qa_error *);
typedef struct qa_scene_shadows qa_scene_shadows;
qa_scene_shadows *qa_scene_shadows_create(qa_scene_resources *, qa_error *);
void qa_scene_shadows_destroy(qa_scene_shadows *);
bool qa_scene_shadows_prepare(qa_scene_shadows *, const qa_scene_light *, size_t,
                              const qa_scene_shadow_caster *, size_t, qa_scene_frame *,
                              const qa_scene_shadow_light **, size_t *,
                              const qa_scene_image **, qa_error *);

#endif
