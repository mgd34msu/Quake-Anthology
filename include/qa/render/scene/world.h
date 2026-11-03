/* Port of src/render/scene/world.ts. GPL-2.0-or-later.
 * async load/prepareImages are synchronous. Prepared operation lists are
 * owned by qa_world_operations results; batch storage from the materials
 * ports is destroyed with them. */
#ifndef QA_RENDER_SCENE_WORLD_H
#define QA_RENDER_SCENE_WORLD_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/materials/deform.h"
#include "qa/materials/evaluate.h"
#include "qa/materials/fog.h"
#include "qa/materials/lighting.h"
#include "qa/render/scene/material_registrations.h"
#include "qa/render/scene/patch_lod.h"
#include "qa/render/scene/portal.h"
#include "qa/render/scene/q2_sky.h"
#include "qa/render/scene/shaders.h"
#include "qa/render/scene/shadows.h"
#include "qa/render/scene/submissions.h"
#include "qa/render/scene/textures.h"
#include "qa/render/scene/view.h"
#include "qa/render/scene/visibility.h"
typedef qa_q3_world_geometry qa_q3_scene_world;
typedef struct qa_world_surface_lightmap {
  qa_lightmap_face face; qa_renderer_image image, direct; const char *encoding;
} qa_world_surface_lightmap;
typedef struct qa_world_surface {
  const char *shader_name; /* owned by scene */
  const qa_scene_texture *base_texture;
  double index; qa_bounds bounds;
  bool has_plane; qa_plane plane;
  qa_material_geometry geometry;
  bool is_q3;
  /* q3 */
  qa_registered_scene_material shader; bool has_shader;
  const qa_renderer_image *lightmap_image;
  qa_patch_grid *grid;
  const qa_fog_volume *fog; double fog_index;
  bool flare, skip;
  /* legacy */
  qa_render_material material;
  const qa_renderer_image *fullbright;
  const qa_world_surface_lightmap *legacy_lightmap;
  bool has_q1_sky; qa_renderer_image q1_sky_solid, q1_sky_overlay;
} qa_world_surface;
typedef struct qa_world_scene_options {
  bool has_subdivisions; double subdivisions;
  bool has_q3_lightmap_overbright; double q3_lightmap_overbright;
  /* TS q3IdentityLight?: tr.identityLight, scaling fog volume colors (default 1). */
  bool has_q3_identity_light; double q3_identity_light;
  const char *q1_lightmap_encoding; /* nullable */
  bool has_q1_water_alpha; double q1_water_alpha;
  const char *q2_sky_name;          /* nullable */
  bool has_q2_light_modulate; double q2_light_modulate;
} qa_world_scene_options;
typedef struct qa_world_surface_admission {
  qa_source_scene_order *view;
  void *_impl; /* per-scene submitted sets + cached operations */
} qa_world_surface_admission;
void qa_create_world_surface_admission(qa_source_scene_order *view, qa_world_surface_admission *out);
void qa_world_surface_admission_destroy(qa_world_surface_admission *a);
typedef struct qa_world_inline_model {
  double model; qa_scene_model_transform transform;
  bool has_animation_frame; double animation_frame;
  bool has_alternate_animation; bool alternate_animation;
  bool has_casts_shadow; bool casts_shadow;
  bool has_entity_rgba; qa_vec4 entity_rgba;
} qa_world_inline_model;
typedef const qa_render_operation *(*qa_world_prepare_flare_fn)(void *ctx, const qa_world_surface *surface, const qa_material_draw_context *context, size_t *out_count);
typedef struct qa_world_view_input {
  qa_world_visibility_options visibility;
  bool no_world_model, world_space_batches;
  qa_world_surface_admission *source;
  qa_scene_camera camera;
  bool target_is_seat; qa_seat_id target_seat; const char *target_preview_id;
  qa_source_time time;
  const qa_render_view_clear *clear; /* nullable */
  const double *q1_styles; size_t q1_style_count;
  const qa_q2_light_style *q2_styles; size_t q2_style_count;
  const qa_surface_dynamic_light *lights; size_t light_count;
  /* false is TS lights undefined; SceneModelRenderer.prepare tells it from []. */
  bool has_lights;
  bool has_q2_fragment_lighting;
  const qa_q2_fragment_light *fragment_lights; size_t fragment_light_count;
  const qa_q2_shadow_atlas *fragment_atlas;
  bool has_q1_fog; qa_scene_fog q1_fog;
  bool has_q2_fog; qa_scene_fog q2_fog;
  const qa_q2_sky_view *q2_sky;
  const qa_q2_sky_view *source_sky;
  bool has_animation_frame; double animation_frame;
  bool has_alternate_animation; bool alternate_animation;
  bool has_curve_error; double curve_error;
  bool has_identity_light; double identity_light;
  const char *const *render_text; size_t render_text_count;
  const qa_scene_operation *operations; size_t operation_count;
  const qa_render_operation *before_view; size_t before_view_count;
  /* materialContext partial */
  const qa_entity_lighting *material_lighting;
  bool has_material_entity_rgba; qa_vec4 material_entity_rgba;
  const qa_projection_shadow_context *material_projection_shadow;
  const qa_world_inline_model *inline_models; size_t inline_model_count;
  qa_world_prepare_flare_fn prepare_flare; void *prepare_flare_ctx;
  /* TS modelShadows?: 2 | 3 (cg_shadows stencil volumes / projection
   * shadows); 0 is TS undefined. */
  double model_shadows;
} qa_world_view_input;
typedef struct qa_prepared_world_view {
  qa_render_view view;
  qa_visible_world visibility;
  qa_image_resource_operation *image_operations; size_t image_operation_count;
  void *_storage; /* merged/fogged operation storage */
} qa_prepared_world_view;
void qa_prepared_world_view_destroy(qa_prepared_world_view *v);
typedef struct qa_world_operations {
  /* borrowed scene operations valid until the next prepare on the scene */
  const qa_scene_operation *operations; size_t count;
  void *_storage; /* owned when returned standalone (prepareModel) */
} qa_world_operations;
void qa_world_operations_destroy(qa_world_operations *o);
typedef struct qa_fog_selection { double index; qa_fog_volume volume; } qa_fog_selection;
typedef struct qa_world_material_context {
  qa_material_draw_context context;
  qa_scene_view_projector projector;
  qa_fog_coordinates_state fog_state;
  qa_mat_draw_context_fog fog_value;
  qa_scene_model_transform model; bool has_model;
  qa_polygon_offset polygon_offset_value;
  qa_entity_lighting lighting_value;
} qa_world_material_context;
typedef struct qa_world_scene {
  qa_decoded_world map;
  qa_scene_shader_registry *shaders; /* rebindable via prepare/commitImages flows internally */
  qa_world_surface *surfaces; size_t surface_count;
  qa_bounds bounds;
  qa_renderer_image fog_image, dlight_image;
  qa_renderer_noise noise;
  qa_fog_selection *fog_selections; size_t fog_selection_count;
  qa_shader_world_identity *material_world;
  qa_world_scene_options options;
  void *_impl;
  qa_world_visibility_cache visibility_cache;
} qa_world_scene;
/* options NULL is TS {}. shaders must outlive the scene. */
void qa_world_scene_load(qa_decoded_world map, qa_scene_shader_registry *shaders, const qa_world_scene_options *options, qa_world_scene *out);
void qa_world_scene_destroy(qa_world_scene *scene);
void qa_world_scene_material_context(qa_world_scene *scene, const qa_world_view_input *input, const qa_scene_model_transform *model, const qa_fog_volume *fog, qa_world_material_context *out);
void qa_world_scene_prepare_model(qa_world_scene *scene, double model_index, qa_scene_model_transform transform, const qa_world_view_input *input, const qa_source_entity_order *entity, qa_world_operations *out);
typedef struct qa_world_shadow_model {
  qa_shadow_caster caster;
  qa_shadow_mesh *meshes; qa_vec3 **owned_positions; size_t mesh_count;
  void *_storage;
} qa_world_shadow_model;
void qa_world_shadow_model_destroy(qa_world_shadow_model *m);
void qa_world_scene_shadow_model(qa_world_scene *scene, double model_index, qa_scene_model_transform transform, const qa_world_view_input *input, qa_world_shadow_model *out);
bool qa_world_scene_prepare_shadows(qa_world_scene *scene, const qa_scene_light *lights, size_t light_count, const qa_world_view_input *input, const qa_shadow_caster *casters, size_t caster_count, const qa_shadow_atlas_options *options, qa_prepared_shadows *out);
/* visibility NULL computes it (caller then reads nothing back; use prepareView). */
void qa_world_scene_prepare_world_operations(qa_world_scene *scene, const qa_world_view_input *input, const qa_visible_world *visibility, qa_world_operations *out);
void qa_world_scene_prepare_view(qa_world_scene *scene, const qa_world_view_input *input, qa_prepared_world_view *out);
void qa_world_scene_prepare_views(qa_world_scene *scene, const qa_world_view_input *input, const qa_portal_entity *portals, size_t portal_count, qa_prepared_world_view *out_views, size_t *out_count);
void qa_world_scene_close(qa_world_scene *scene);
typedef qa_scene_shader_registry *(*qa_world_source_for_fn)(void *ctx, qa_scene_shader_registry *source);
/* replacement loaded into caller storage; commit adopts its data. */
void qa_world_scene_prepare_images(qa_world_scene *scene, qa_scene_shader_registry *shaders, qa_world_source_for_fn source_for, void *source_for_ctx, qa_world_scene *replacement);
void qa_world_scene_validate_images(qa_world_scene *scene, qa_world_scene *replacement);
void qa_world_scene_commit_images(qa_world_scene *scene, qa_world_scene *replacement);
qa_scene_raw_remap_binding *qa_world_scene_raw_remap_bindings(qa_world_scene *scene, const char *name, size_t *out_count);
void qa_world_scene_publish_raw_remap(qa_world_scene *scene, const char *name, const qa_registered_scene_material *materials, size_t count, qa_scene_shader_registry *source, double time_offset);
qa_shader_remap_result qa_world_scene_remap_shader(qa_world_scene *scene, const char *original, const char *replacement, double time_offset, qa_scene_shader_registry *source, qa_scene_current_fn current, void *current_ctx);
/* The qa_scene_world_service view of this scene (material_registrations). */
qa_scene_world_service qa_world_scene_world_service(qa_world_scene *scene);
#endif
