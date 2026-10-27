/*
 * Port of src/contracts/render.ts (types only): images, shader stages,
 * materials, draw batches, render views/commands, and the renderer
 * backend interface. TS `symbol` owner identity becomes an opaque
 * pointer; Float32Array/Float64Array become pointer+count; TS
 * closures in backend methods become fn ptr + ctx.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_RENDER_H
#define QA_CONTRACTS_RENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/time.h"

typedef struct qa_image_level {
  double width, height;
  qa_byte_view pixels;
} qa_image_level;
typedef struct qa_depth_image_level {
  double width, height;
  const float *pixels;
  size_t pixel_count;
} qa_depth_image_level;

typedef struct qa_palette {
  qa_byte_view colors;
  qa_resolved_resource_reference source;
} qa_palette;

/* TS PaletteTransparency: {kind:"opaque"} | {kind:"index",index} |
 * {kind:"q1-fence",index:255}. */
typedef struct qa_palette_transparency {
  const char *kind;
  double index;
} qa_palette_transparency;

typedef struct qa_fullbright_range { double first, last; } qa_fullbright_range;

/* TS RenderImage union on kind: "indexed8" (levels nonempty in TS,
 * palette/transparency/fullbright/translation) | "rgba8" (levels,
 * borderColor) | "depth32f" (depth levels). */
typedef enum qa_render_image_kind {
  QA_RENDER_IMAGE_INDEXED8, QA_RENDER_IMAGE_RGBA8, QA_RENDER_IMAGE_DEPTH32F
} qa_render_image_kind;
typedef struct qa_render_image {
  qa_render_image_kind kind;
  const qa_image_level *levels; size_t level_count;
  const qa_depth_image_level *depth_levels; size_t depth_level_count;
  qa_palette palette;
  qa_palette_transparency transparency;
  const qa_fullbright_range *fullbright;  /* nullable */
  const qa_byte_view *translation;        /* nullable */
  qa_vec4 border_color;
} qa_render_image;

/* TS TextureFilter (exact strings): "nearest"|"linear"|
 * "nearest-mipmap-nearest"|"linear-mipmap-nearest"|
 * "nearest-mipmap-linear"|"linear-mipmap-linear". */
typedef const char *qa_texture_filter;
typedef struct qa_texture_sampling {
  const char *wrap; /* "repeat" | "clamp" */
  qa_texture_filter filter;
} qa_texture_sampling;

typedef struct qa_renderer_resource_owner {
  const void *identity; /* TS symbol */
  qa_session_id session;
  double generation;
} qa_renderer_resource_owner;

/* RendererImage.source: {kind:"resource",resource} |
 * {kind:"generated",name}. */
typedef struct qa_renderer_image {
  qa_renderer_resource_owner owner;
  double ordinal;
  bool source_is_resource;
  qa_resolved_resource_reference source_resource;
  const char *source_name;
  double width, height;
} qa_renderer_image;

/* TS ImageResourceOperation union on kind: "create-image" |
 * "update-image"(level, content ImageLevel|DepthImageLevel) |
 * "release-image" | "texture-mode"(filter). */
typedef enum qa_image_resource_operation_kind {
  QA_IMAGE_OP_CREATE, QA_IMAGE_OP_UPDATE, QA_IMAGE_OP_RELEASE, QA_IMAGE_OP_TEXTURE_MODE
} qa_image_resource_operation_kind;
typedef struct qa_image_resource_operation {
  qa_image_resource_operation_kind kind;
  qa_renderer_image image;
  qa_render_image content;
  qa_texture_sampling sampling;
  double level;
  const qa_image_level *update_level;        /* update, one of these */
  const qa_depth_image_level *update_depth_level;
  qa_texture_filter filter;
} qa_image_resource_operation;

/* TS BlendFactor (exact strings): "zero"|"one"|"src-color"|
 * "one-minus-src-color"|"dst-color"|"one-minus-dst-color"|"src-alpha"|
 * "one-minus-src-alpha"|"dst-alpha"|"one-minus-dst-alpha"|
 * "src-alpha-saturate". */
typedef const char *qa_blend_factor;
typedef struct qa_blend_state { qa_blend_factor source, destination; } qa_blend_state;
typedef struct qa_polygon_offset { double factor, units; } qa_polygon_offset;

/* depthTest: "less-equal"|"equal"|"always"; alphaTest:
 * "none"|"gt0"|"lt128"|"ge128"; cull: "none"|"back"|"front". */
typedef struct qa_render_state {
  qa_blend_state blend;
  const char *depth_test;
  bool depth_write;
  const char *alpha_test;
  const char *cull;
  double depth_range[2];
  const qa_polygon_offset *polygon_offset; /* nullable */
} qa_render_state;

/* Waveform kind: "none"|"sin"|"square"|"triangle"|"sawtooth"|
 * "inversesawtooth"|"noise". */
typedef struct qa_waveform {
  const char *kind;
  double base, amplitude, phase, frequency;
} qa_waveform;

/* ColorGenerator kinds: "identity"|"identitylighting"|"entity"|
 * "oneminusentity"|"vertex"|"exactvertex"|"lightingdiffuse"|
 * "oneminusvertex" | "const"(color) | "wave"(wave). AlphaGenerator
 * kinds: "identity"|"entity"|"oneminusentity"|"vertex"|
 * "lightingspecular"|"oneminusvertex" | "const"(alpha) | "wave" |
 * "portal"(range). TexCoordGenerator: "texture"|"lightmap"|
 * "environment" | "vector"(s,t). */
typedef struct qa_color_generator {
  const char *kind;
  qa_vec3 color;
  qa_waveform wave;
} qa_color_generator;
typedef struct qa_alpha_generator {
  const char *kind;
  double alpha;
  qa_waveform wave;
  double range;
} qa_alpha_generator;
typedef struct qa_tex_coord_generator {
  const char *kind;
  qa_vec3 s, t;
} qa_tex_coord_generator;
/* TexCoordModifier kinds: "scale"|"scroll"(amount) |
 * "stretch"|"turb"(wave) | "rotate"(degreesPerSecond) |
 * "transform"(matrix[4], translation) | "entitytranslate"|"none". */
typedef struct qa_tex_coord_modifier {
  const char *kind;
  qa_vec2 amount;
  qa_waveform wave;
  double degrees_per_second;
  double matrix[4];
  qa_vec2 translation;
} qa_tex_coord_modifier;
/* ShaderMap kinds: "image"(name,clamp) | "lightmap"|"whiteimage"|
 * "none" | "animation"(frequency,frames) | "video"(name). */
typedef struct qa_shader_map {
  const char *kind;
  const char *name;
  bool clamp;
  double frequency;
  qa_string_view_list frames;
} qa_shader_map;

typedef struct qa_shader_stage_source_state {
  bool active;
  double state_bits, rgb_gen, alpha_gen, tc_gen;
  qa_waveform rgb_wave, alpha_wave;
  bool is_lightmap, vertex_lightmap;
} qa_shader_stage_source_state;

typedef struct qa_shader_stage {
  qa_shader_map map;
  qa_blend_state blend;
  const char *depth_test;
  bool depth_write;
  const char *alpha_test;
  bool detail;
  qa_color_generator color;
  qa_alpha_generator alpha;
  qa_tex_coord_generator coordinates;
  const qa_tex_coord_modifier *modifiers; size_t modifier_count;
  qa_shader_stage_source_state source_state;
} qa_shader_stage;

/* VertexDeformation kinds: "projectionshadow"|"autosprite"|
 * "autosprite2"|"none" | "text"(index) | "wave"(spread,wave) |
 * "normal"(amplitude,frequency) | "move"(direction,wave) |
 * "bulge"(width,height,speed). */
typedef struct qa_vertex_deformation {
  const char *kind;
  double index, spread;
  qa_waveform wave;
  double amplitude, frequency;
  qa_vec3 direction;
  double width, height, speed;
} qa_vertex_deformation;

/* SurfaceLighting: "unlit" | "vertex" | "lightmap"(image,styles) |
 * "decoupled-lightmap"(image,styles,mapping). */
typedef struct qa_surface_lighting {
  const char *kind;
  qa_renderer_image image;
  qa_double_view styles;
  qa_decoupled_lightmap mapping;
} qa_surface_lighting;

typedef struct qa_q1_material_frame {
  qa_renderer_image image;
  double start_tenths, end_tenths;
} qa_q1_material_frame;
typedef struct qa_q3_material_sky {
  const char *outer_box; /* nullable strings */
  const char *inner_box;
  double cloud_height;
} qa_q3_material_sky;
typedef struct qa_q3_material_fog { qa_vec3 color; double depth_for_opaque; } qa_q3_material_fog;
typedef struct qa_q3_material_sun {
  qa_vec3 color;
  double intensity, azimuth, elevation;
} qa_q3_material_sun;

/* TS RenderMaterial union on kind: "q1"|"q2"|"q3" (fields per TS). */
typedef struct qa_render_material {
  const char *kind;
  const char *name;
  qa_renderer_image texture;                     /* q1 */
  qa_surface_lighting lighting;
  const qa_q1_material_frame *animation; size_t animation_count;
  const qa_q1_material_frame *alternate_animation; size_t alternate_animation_count;
  const char *surface; /* q1: "ordinary"|"sky"|"water"|"slime"|"lava"|"teleport"|"fence" */
  double alpha;
  const qa_renderer_image *frames; size_t frame_count; /* q2 */
  double surface_flags;
  const char *material;
  bool flowing, warp;
  const qa_shader_stage *stages; size_t stage_count;   /* q3 */
  const qa_vertex_deformation *deformations; size_t deformation_count;
  qa_string_view_list surface_parameters;
  bool has_sort;
  double sort;
  const char *cull;
  bool polygon_offset, no_mipmaps, no_picmip, entity_mergeable;
  double portal_range, clamp_time;
  const qa_q3_material_sky *sky;   /* nullable */
  const qa_q3_material_fog *fog;   /* nullable */
  const qa_q3_material_sun *sun;   /* nullable */
} qa_render_material;

typedef struct qa_render_vertex {
  qa_vec4 position;
  qa_vec2 tex_coord;
  qa_vec4 color;
} qa_render_vertex;
typedef struct qa_multitexture_vertex {
  qa_render_vertex vertex;
  qa_vec2 tex_coord2;
} qa_multitexture_vertex;

/** TS DynamicImageSource: resolve(apply callback). */
typedef struct qa_dynamic_image_source {
  void *ctx;
  qa_renderer_image (*resolve)(void *ctx,
      void (*apply)(void *apply_ctx, qa_image_resource_operation operation),
      void *apply_ctx);
} qa_dynamic_image_source;

/* TextureBinding: "dynamic-image"(source) | "bind-image"(image) |
 * "retain-current-texture". */
typedef enum qa_texture_binding_kind {
  QA_TEXTURE_BINDING_DYNAMIC_IMAGE, QA_TEXTURE_BINDING_BIND_IMAGE,
  QA_TEXTURE_BINDING_RETAIN_CURRENT
} qa_texture_binding_kind;
typedef struct qa_texture_binding {
  qa_texture_binding_kind kind;
  qa_dynamic_image_source source;
  qa_renderer_image image;
} qa_texture_binding;

typedef struct qa_texture_bundle {
  qa_texture_binding binding;
  const char *environment; /* "modulate" | "add" | "replace" */
} qa_texture_bundle;

/* Q2ShadowProjection: "none" | "cone"(matrix,atlasRect) | "point"(atlasRect). */
typedef struct qa_q2_shadow_projection {
  const char *kind;
  qa_mat4 matrix;
  qa_vec4 atlas_rect;
} qa_q2_shadow_projection;

typedef struct qa_q2_shadow_atlas {
  qa_renderer_image image;
  double texel_size, near_plane;
} qa_q2_shadow_atlas;

typedef struct qa_q2_light_cone { qa_vec3 direction; double cos_half_angle; } qa_q2_light_cone;
typedef struct qa_q2_fragment_light {
  qa_vec3 origin;
  double radius;
  qa_vec3 color;
  double scale;
  const qa_q2_light_cone *cone; /* nullable */
  qa_q2_shadow_projection shadow;
  qa_vec3 fraction; /* "model" pass lights extend with fraction */
} qa_q2_fragment_light;

typedef struct qa_q2_model_shadow_light {
  qa_vec3 origin;
  double radius;
  qa_vec3 fraction;
  qa_q2_shadow_projection shadow;
} qa_q2_model_shadow_light;

/* BatchLighting: "vertex" | "q2-world"(worldPositions,normals,atlas,
 * pass "lightmap"|"texture"|"material-lightmap" lights |
 * pass "model" lights+shadeScale) | "q2-model-shadow". */
typedef struct qa_batch_lighting {
  const char *kind;
  const qa_vec3 *world_positions; size_t world_position_count;
  const qa_vec3 *normals; size_t normal_count;
  const qa_q2_shadow_atlas *atlas; /* nullable in q2-world */
  qa_q2_shadow_atlas atlas_value;  /* q2-model-shadow */
  const char *pass;
  const qa_q2_fragment_light *lights; size_t light_count;
  const qa_q2_model_shadow_light *model_shadow_lights; size_t model_shadow_light_count;
  bool has_shade_scale;
  double shade_scale;
} qa_batch_lighting;

/* BatchFog: "exp2"(optional effect "color"|"none"|"rgb"|"alpha"|
 * "rgba"|"overlay", color, density) | "constant"(color, amount). */
typedef struct qa_batch_fog {
  const char *kind;
  const char *effect; /* optional */
  qa_vec3 color;
  double density, amount;
} qa_batch_fog;

/* TS DrawBatch = BatchData & ("triangles" | "lines"+lineWidth) &
 * ("single" vertices | "pair" multitexture vertices + secondTexture).
 * space: optional "clip"|"world"; textureEffect optional
 * "luminance-alpha". */
typedef struct qa_draw_batch {
  const qa_batch_fog *fog;              /* optional */
  const char *texture_effect;           /* optional */
  const char *space;                    /* optional */
  qa_double_view indices;
  qa_texture_binding texture;
  qa_render_state state;
  qa_batch_lighting lighting;
  bool primitive_is_lines;
  double line_width;
  bool texturing_is_pair;
  const qa_render_vertex *vertices; size_t vertex_count;
  const qa_multitexture_vertex *pair_vertices; size_t pair_vertex_count;
  qa_texture_bundle second_texture;
} qa_draw_batch;

typedef struct qa_rect { double x, y, width, height; } qa_rect;
typedef struct qa_texture_rect { double s1, t1, s2, t2; } qa_texture_rect;

/* SceneCamera.clip: {kind:"none"} | {kind:"portal",plane,mirror}. */
typedef struct qa_scene_camera {
  qa_vec3 origin;
  qa_axis axis;
  qa_mat4 projection;
  qa_rect viewport;
  bool clip_is_portal;
  qa_plane clip_plane;
  bool clip_mirror;
} qa_scene_camera;

typedef struct qa_q2_height_fog_point { qa_vec3 color; double distance; } qa_q2_height_fog_point;
typedef struct qa_q2_height_fog {
  qa_q2_height_fog_point start, end;
  double density, falloff;
} qa_q2_height_fog;

/* TS SceneFog union on kind: "none" | "q1" | "q2"(+height) | "q3"(plane|null). */
typedef struct qa_scene_fog {
  const char *kind;
  qa_vec3 color;
  double density, sky_factor, depth_for_opaque;
  qa_q2_height_fog height;
  const qa_plane *plane; /* q3, nullable */
} qa_scene_fog;

typedef struct qa_depth_atlas_draw {
  const double *positions; /* TS Float64Array, clip-space xyzw per vertex */
  size_t position_count;
  qa_double_view indices;
  const char *cull;
  const qa_polygon_offset *polygon_offset; /* nullable */
} qa_depth_atlas_draw;

typedef struct qa_depth_atlas_pass {
  qa_rect viewport;
  bool has_clear_depth;
  double clear_depth;
  const qa_depth_atlas_draw *draws; size_t draw_count;
} qa_depth_atlas_pass;

typedef struct qa_q2_fog_operation {
  /* kind "q2-fog" */
  qa_scene_camera camera;
  qa_scene_fog fog; /* the "q2" variant */
  double far_depth;
  bool sky_drawn;
} qa_q2_fog_operation;

typedef struct qa_vertex_position_tex_coord {
  qa_vec4 position;
  qa_vec2 tex_coord;
} qa_vertex_position_tex_coord;
typedef struct qa_sky_strip {
  const qa_vertex_position_tex_coord *vertices;
  size_t vertex_count;
} qa_sky_strip;

/* TS RenderOperation union on kind: "draw"|"object-opacity"|"q2-fog"|
 * "depth-atlas"|"depth-range"|"cull"|"polygon-offset"|
 * "disable-portal-clip"|"sky-side"|"shadow-volume"|"shadow-finish". */
typedef enum qa_render_operation_kind {
  QA_RENDER_OP_DRAW, QA_RENDER_OP_OBJECT_OPACITY, QA_RENDER_OP_Q2_FOG,
  QA_RENDER_OP_DEPTH_ATLAS, QA_RENDER_OP_DEPTH_RANGE, QA_RENDER_OP_CULL,
  QA_RENDER_OP_POLYGON_OFFSET, QA_RENDER_OP_DISABLE_PORTAL_CLIP,
  QA_RENDER_OP_SKY_SIDE, QA_RENDER_OP_SHADOW_VOLUME, QA_RENDER_OP_SHADOW_FINISH
} qa_render_operation_kind;
typedef struct qa_render_operation {
  qa_render_operation_kind kind;
  const qa_draw_batch *batches; size_t batch_count;
  double opacity;
  qa_q2_fog_operation fog;
  qa_renderer_image image;
  const qa_depth_atlas_pass *passes; size_t pass_count;
  double range[2];
  const char *cull;
  const qa_polygon_offset *polygon_offset; /* nullable */
  qa_vec4 color;
  const qa_sky_strip *strips; size_t strip_count;
  const qa_vec4 *positions; size_t position_count;
  qa_double_view indices;
  bool mirror;
  qa_renderer_image white_image;
  qa_vec4 finish_positions[4];
} qa_render_operation;

typedef struct qa_render_view_clear {
  double depth;
  const qa_vec4 *color; /* nullable */
  bool stencil;
} qa_render_view_clear;

typedef struct qa_render_view_state {
  qa_rect viewport;
  const qa_render_view_clear *clear; /* nullable */
  const qa_vec4 *clip_plane;         /* nullable */
  bool has_view_projection;
  qa_mat4 view_projection;
} qa_render_view_state;

/* RenderView.target: {kind:"seat",seat} | {kind:"preview",id}. */
typedef struct qa_render_view {
  qa_render_view_state state;
  bool target_is_seat;
  qa_seat_id target_seat;
  const char *target_preview_id;
  qa_source_time time;
  const qa_render_operation *before_view; size_t before_view_count;
  const qa_render_operation *operations; size_t operation_count;
} qa_render_view;

/* TS RendererDrawBuffer = "front"|"back"|"back-left"|"back-right". */
typedef const char *qa_renderer_draw_buffer;

/* TS RenderCommand union on kind: "draw-buffer"|"image-resource"|
 * "set-color"|"stretch-pic"|"view"|"swap-buffers". */
typedef enum qa_render_command_kind {
  QA_RENDER_COMMAND_DRAW_BUFFER, QA_RENDER_COMMAND_IMAGE_RESOURCE,
  QA_RENDER_COMMAND_SET_COLOR, QA_RENDER_COMMAND_STRETCH_PIC,
  QA_RENDER_COMMAND_VIEW, QA_RENDER_COMMAND_SWAP_BUFFERS
} qa_render_command_kind;
typedef struct qa_render_command {
  qa_render_command_kind kind;
  qa_renderer_draw_buffer buffer;
  bool clear;
  qa_image_resource_operation operation;
  qa_vec4 color;
  qa_rect rect;
  qa_texture_rect uv;
  qa_renderer_image image;
  qa_render_view view;
} qa_render_command;

typedef struct qa_render_frame {
  qa_renderer_resource_owner owner;
  double sequence;
  const qa_render_command *commands; size_t command_count;
} qa_render_frame;

/** TS PreparedBackendDraw. */
typedef struct qa_prepared_backend_draw {
  void *ctx;
  void (*begin)(void *ctx);
  void (*apply_texture)(void *ctx, double unit, qa_texture_binding operation);
  void (*draw)(void *ctx);
  void (*cleanup)(void *ctx);
} qa_prepared_backend_draw;

/** TS RendererBackend. drawImmediate excludes "draw"/"object-opacity"
 * operations; withObjectOpacity takes a draw closure (fn ptr + ctx). */
typedef struct qa_renderer_backend {
  void *ctx;
  qa_renderer_resource_owner owner;
  double width, height, stencil_bits;
  void (*apply_image_resource)(void *ctx, qa_image_resource_operation operation);
  void (*select_draw_buffer)(void *ctx, qa_renderer_draw_buffer buffer, bool clear);
  void (*set_overdraw_measurement)(void *ctx, bool enabled);
  void (*read_stencil_overdraw)(void *ctx, uint8_t *destination, size_t length);
  double (*read_depth_pixel)(void *ctx, double window_x, double window_y);
  void (*begin_view)(void *ctx, qa_render_view_state view);
  void (*with_object_opacity)(void *ctx, double opacity,
                              void (*draw)(void *draw_ctx), void *draw_ctx);
  void (*draw_immediate)(void *ctx, qa_render_operation operation);
  qa_prepared_backend_draw (*prepare_geometry)(void *ctx, qa_draw_batch batch);
  void (*clear_color_buffer)(void *ctx);
  void (*draw_show_image)(void *ctx, qa_renderer_image image, qa_rect rect,
                          bool proportional);
  void (*finish)(void *ctx);
  void (*close)(void *ctx);
} qa_renderer_backend;

#endif
