/*
 * Port of src/render/scene/models/types.ts. C accommodations:
 *  - ModelSkinningFrame's WeakMap caches are dropped (pure-function
 *    memoization; values identical, identity not preserved), so the
 *    frame is an opaque marker.
 *  - SourceSceneOrder/SourceEntityOrder (TS owner:
 *    src/render/scene/submissions.ts, unported) and ShadowSphere
 *    (TS owner: src/render/scene/shadows.ts, unported) are mirrored
 *    below with collision-proof names.
 *  - Optional fields use has_* flags; TS string|null|undefined
 *    collapses to NULL where the TS only tests `!= null`.
 *  - Prepared surfaces/entities own their vertex storage; free with
 *    qa_prepared_model_entity_destroy (prepare.h).
 * GPL-2.0-or-later.
 */
#ifndef QA_RENDER_SCENE_MODELS_TYPES_H
#define QA_RENDER_SCENE_MODELS_TYPES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/materials/geometry.h"
#include "qa/render/scene/models/replacements.h"

/* Mirrored ShadowSphere (TS owner: src/render/scene/shadows.ts). */
typedef struct qa_render_scene_shadow_sphere {
  qa_vec3 origin;
  double radius;
} qa_render_scene_shadow_sphere;

/* Mirrored SceneModelGroup (TS owner: src/render/scene/submissions.ts):
 * the group constructors there wrap batches with ordering metadata;
 * only the fields this slice sets/reads are carried. batches owned. */
typedef struct qa_scene_model_group {
  const char *kind; /* "compiled" | "source" | "sequence" */
  const void *material; /* TS CompiledMaterial/RegisteredSceneMaterial */
  const char *sequence; /* sequenceDrawGroup name */
  const void *source_view;   /* mirrored SourceSceneOrder */
  const void *source_entity; /* mirrored SourceEntityOrder refentity */
  double surface, fog, dlight;
  qa_draw_batch *batches; /* owned */
  size_t batch_count;
} qa_scene_model_group;
void qa_scene_model_group_destroy(qa_scene_model_group *group);
void qa_scene_model_groups_destroy(qa_scene_model_group *groups, size_t count);

typedef struct qa_model_skinning_frame { void *reserved; } qa_model_skinning_frame;

/* TS ModelImageSelection. */
typedef enum qa_model_image_selection_kind {
  QA_MODEL_IMAGE_EXTERNAL, QA_MODEL_IMAGE_INDEXED,
  QA_MODEL_IMAGE_WHITE, QA_MODEL_IMAGE_DEFAULT
} qa_model_image_selection_kind;
typedef struct qa_model_image_selection {
  qa_model_image_selection_kind kind;
  const char *name; /* external/indexed; borrowed unless owned_name */
  char *owned_name;
  double width, height;         /* indexed */
  qa_byte_view pixels;          /* indexed, borrowed */
  bool has_transparent_index;   /* indexed; false is TS null */
  double transparent_index;
  bool fullbright;              /* indexed */
  const char *reason; /* default: "missing-skin-surface" | "no-skin" */
} qa_model_image_selection;

typedef struct qa_model_custom_skin_entry {
  const char *name;
  const char *shader;
} qa_model_custom_skin_entry;

/* TS ModelSourceOptions. */
typedef struct qa_model_source_options {
  bool has_source;
  const void *source_view;
  const void *source_entity;
  bool has_indexed_skin;
  qa_indexed_model_skin indexed_skin;
  bool has_model_beam;
  double model_beam_segment_length;
  bool has_sync_base;
  double sync_base;
  bool has_sprite_roll;
  double sprite_roll;
  const char *custom_shader; /* NULL is TS null/undefined */
  const qa_model_custom_skin_entry *custom_skin; /* NULL is null/undefined */
  size_t custom_skin_count;
  const qa_q3_mesh_model *const *q3_lods; /* NULL is undefined; entries nullable */
  size_t q3_lod_count;
  bool has_lod_scale;
  double lod_scale;
  bool has_lod_bias;
  double lod_bias;
  bool has_non_normalized_axes;
  bool non_normalized_axes;
  bool has_no_world_model;
  bool no_world_model;
  bool has_shader_tex_coord;
  qa_vec2 shader_tex_coord;
  bool has_left_hand;
  double left_hand; /* 0 | 1 | 2 */
  bool has_infrared;
  bool infrared;
  bool has_view_model;
  bool view_model;
  bool has_planar_shadow;
  bool planar_shadow;
  bool has_player;
  bool player;
  bool has_overbright_models;
  bool overbright_models;
  bool has_player_colors;
  double player_color_top, player_color_bottom;
} qa_model_source_options;

/* TS ModelVertexLighting closure. */
typedef qa_vec3 (*qa_model_vertex_lighting_fn)(void *ctx, qa_vec3 normal,
                                               qa_vec3 position, double corner);
/* TS PreparedVertexLighting. key NULL is TS null. */
typedef struct qa_prepared_vertex_lighting {
  qa_model_vertex_lighting_fn shade;
  void *shade_ctx;
  const char *key;
} qa_prepared_vertex_lighting;

typedef struct qa_model_preparation_context qa_model_preparation_context;
/* TS ModelPreparationContext callbacks (fn NULL is TS undefined). */
typedef bool (*qa_model_retain_shadow_body_fn)(
    void *ctx, const qa_scene_entity *entity,
    qa_render_scene_shadow_sphere sphere,
    const qa_model_image_selection *images, size_t image_count,
    qa_model_source_options options);
typedef qa_model_source_options (*qa_model_options_fn)(void *ctx,
                                                       const qa_scene_entity *entity);
typedef qa_vec3 (*qa_model_light_vertex_fn)(void *ctx, const qa_scene_entity *entity,
                                            qa_vec3 normal, qa_vec3 position);
typedef qa_prepared_vertex_lighting (*qa_model_prepare_vertex_lighting_fn)(
    void *ctx, const qa_scene_entity *entity, qa_model_source_options options);
typedef qa_vec3 (*qa_model_palette_color_fn)(void *ctx, const qa_scene_entity *entity,
                                             double index);

struct qa_model_preparation_context {
  const qa_model_skinning_frame *skinning_frame; /* nullable */
  bool has_model_policy;
  qa_model_replacement_policy model_policy;
  const char *purpose; /* NULL | "view" | "shadow" */
  qa_scene_camera camera;
  double time_seconds;
  const qa_plane *frustum; /* NULL is TS undefined */
  size_t frustum_count;
  bool no_cull;
  qa_model_retain_shadow_body_fn retain_shadow_body;
  void *retain_shadow_body_ctx;
  qa_model_options_fn options;
  void *options_ctx;
  qa_model_light_vertex_fn light_vertex;
  void *light_vertex_ctx;
  qa_model_prepare_vertex_lighting_fn prepare_vertex_lighting;
  void *prepare_vertex_lighting_ctx;
  qa_model_palette_color_fn palette_color;
  void *palette_color_ctx;
};

/* TS PreparedModelSurface. local/world vertex storage and any
 * generated index storage are owned (see prepare.h destroy). */
typedef struct qa_prepared_model_surface {
  double surface_index;
  bool has_fog_sphere;
  qa_vec3 fog_local_origin;
  double fog_radius;
  qa_model_source_options options;
  const char *name; /* borrowed unless owned_name */
  char *owned_name;
  qa_scene_entity entity; /* shallow copy, as TS shares the reference */
  qa_model_transform transform;
  qa_model_image_selection image;
  qa_material_geometry local_geometry;
  qa_material_geometry geometry;
  qa_material_vertex *owned_local_vertices;
  qa_material_vertex *owned_world_vertices;
  double *owned_indices;
  double depth_range[2];
  const char *cull;
  const char *alpha_test;
  bool translucent, unlit, mirror_weapon;
} qa_prepared_model_surface;

/* TS PreparedModelEntity. Owned recursively. */
typedef struct qa_prepared_model_entity qa_prepared_model_entity;
struct qa_prepared_model_entity {
  qa_scene_entity entity;
  double frame, previous_frame;
  bool frame_fallback;
  double lod;
  bool has_bounds;
  qa_bounds bounds;
  const char *cull; /* "in" | "clip" | "out" */
  bool personal_model;
  qa_prepared_model_surface *surfaces;
  size_t surface_count;
  qa_prepared_model_entity *attachments;
  size_t attachment_count;
  const char **missing_attachments; /* borrowed tag strings */
  size_t missing_attachment_count;
  double model_effect_flags;
};

/* TS ModelGroupContext. draw returns owned groups (may be NULL/0). */
typedef qa_scene_model_group *(*qa_model_group_draw_fn)(
    void *ctx, const qa_prepared_model_surface *surface, size_t *out_count);
typedef struct qa_model_group_context {
  void *ctx;
  qa_model_group_draw_fn draw;
} qa_model_group_context;

/* TS modelImage: levels/palette storage is caller-owned scratch the
 * result views into (pass level + fullbright storage, may be static
 * thread-local scratch owned by this module when NULL). */
typedef struct qa_model_image_scratch {
  qa_image_level level;
  qa_fullbright_range fullbright;
  qa_byte_view translation;
} qa_model_image_scratch;
qa_render_image qa_model_image(const qa_model_image_selection *selection,
                               qa_palette palette, const qa_byte_view *translation,
                               qa_model_image_scratch *scratch);
qa_vec4 qa_byte_color(qa_vec4 color);

#endif
