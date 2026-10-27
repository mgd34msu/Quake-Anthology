/*
 * Port of src/render/scene/models/renderer.ts. C accommodations:
 *  - SceneTexture/SceneTextureLoader (TS owner: textures.ts),
 *    SceneShaderRegistry/currentRemap (TS owners: shaders.ts,
 *    material-registrations.ts), SceneModelGroup constructors (TS
 *    owner: submissions.ts; see types.h) and ShadowCaster/ShadowMesh
 *    (TS owner: shadows.ts) are mirrored here with collision-proof
 *    names; async preloads are synchronous and the pending/WeakMap
 *    batch caches are dropped (values identical).
 *  - floodSkin (skin.ts), cameraFrustum/createViewProjector
 *    (view.ts), entityCastsShadow/shadowMaterialGeometry
 *    (shadow-geometry.ts) and shadowCaster/shadowMesh (shadows.ts)
 *    are mirrored locally in renderer.c.
 *  - The per-entity light cache keys on entity identity fields
 *    (resource id pointer + origin + skin); TS keys on the object.
 * GPL-2.0-or-later.
 */
#ifndef QA_RENDER_SCENE_MODELS_RENDERER_H
#define QA_RENDER_SCENE_MODELS_RENDERER_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/content.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/materials/shader_types.h"
#include "qa/render/scene/models/light_sampler.h"
#include "qa/render/scene/models/replacements.h"
#include "qa/render/scene/models/types.h"

/* Mirrored SceneTexture (TS owner: textures.ts). */
typedef struct qa_render_scene_texture {
  const char *name;
  double width, height;
  qa_renderer_image image;
  qa_render_image content;
  const qa_renderer_image *fullbright; /* nullable */
  qa_renderer_image fullbright_value;
} qa_render_scene_texture;

/* Mirrored SceneTextureLoader surface used here. The loader struct
 * address is its identity (provider.textures comparison in TS). */
typedef struct qa_render_scene_texture_loader {
  void *ctx;
  qa_render_scene_texture white, missing;
  /* TS register(name, content, sampling, source?): source NULL when
   * the TS passes no source image. */
  qa_render_scene_texture (*register_texture)(
      void *ctx, const char *name, qa_render_image content,
      qa_texture_sampling sampling, const qa_renderer_image *source);
  /* TS load(name, {family, usage, mipmap}): false is TS null. */
  bool (*load)(void *ctx, const char *name, qa_game_family family,
               const char *usage, bool mipmap, qa_render_scene_texture *out);
} qa_render_scene_texture_loader;

/* Mirrored SceneShaderRegistry surface used here. */
typedef struct qa_render_scene_shader_registry {
  void *ctx;
  const qa_mat_compiled_material *(*register_shader)(void *ctx, const char *name);
  bool (*has_cinematic)(void *ctx, const char *name);
  /* TS currentRemap: NULL when no remap; *time_offset_out set then. */
  const qa_mat_compiled_material *(*current_remap)(
      void *ctx, const qa_mat_compiled_material *material,
      double *time_offset_out);
} qa_render_scene_shader_registry;

/* TS ModelRenderProvider. palette NULL is TS null. */
typedef struct qa_model_render_provider {
  bool has_model_policy;
  qa_model_replacement_policy model_policy;
  qa_game_family family;
  const qa_palette *palette;
  qa_palette palette_value;
  qa_render_scene_texture_loader textures;
  qa_render_scene_shader_registry shaders;
} qa_model_render_provider;

/* Mirrored ShadowMesh/ShadowCaster (TS owner: shadows.ts). Owned. */
typedef struct qa_render_scene_shadow_mesh {
  qa_vec3 *positions; /* owned */
  size_t position_count;
  const double *indices; /* borrowed unless owned_indices */
  double *owned_indices;
  size_t index_count;
} qa_render_scene_shadow_mesh;
typedef struct qa_render_scene_shadow_caster {
  qa_vec3 origin;
  double radius;
  qa_render_scene_shadow_mesh *meshes; /* owned */
  size_t mesh_count;
} qa_render_scene_shadow_caster;
void qa_render_scene_shadow_casters_destroy(
    qa_render_scene_shadow_caster *casters, size_t count);

typedef struct qa_scene_model_renderer qa_scene_model_renderer;
struct qa_scene_model_renderer {
  qa_model_render_provider provider;
  const qa_render_scene_world *world; /* borrowed */
  qa_model_light_sampler lighting;
  void *materials; /* private key -> material map */
  size_t material_count, material_capacity;
  const void *textures_identity;
};

/* TS SourceOptions closure: fn NULL yields empty options. */
typedef qa_model_source_options (*qa_model_source_options_fn)(
    void *ctx, const qa_scene_entity *entity);

bool qa_scene_model_renderer_init(qa_scene_model_renderer *renderer,
                                  qa_model_render_provider provider,
                                  const qa_render_scene_world *world);
void qa_scene_model_renderer_destroy(qa_scene_model_renderer *renderer);
/* Synchronous preload. Cinematic deferral aborts with the TS Error. */
void qa_scene_model_renderer_preload(qa_scene_model_renderer *renderer,
                                     const qa_scene_entity *entities,
                                     size_t entity_count,
                                     qa_model_source_options_fn options,
                                     void *options_ctx);
void qa_scene_model_renderer_preload_model(
    qa_scene_model_renderer *renderer,
    const qa_resolved_resource_reference *resource,
    const qa_decoded_model *model, qa_model_source_options options,
    bool allow_cinematics);
/* Owned groups. skinning_frame NULL is TS undefined. */
qa_scene_model_group *qa_scene_model_renderer_prepare(
    qa_scene_model_renderer *renderer, const qa_scene_entity *entities,
    size_t entity_count, const qa_render_scene_world_view_input *input,
    qa_model_source_options_fn source_options, void *source_options_ctx,
    const qa_model_skinning_frame *skinning_frame, size_t *out_count);
/* retain_body NULL is TS undefined. Owned casters. */
typedef bool (*qa_model_retain_body_fn)(void *ctx,
                                        qa_render_scene_shadow_sphere sphere);
qa_render_scene_shadow_caster *qa_scene_model_renderer_prepare_shadow_casters(
    qa_scene_model_renderer *renderer, const qa_scene_entity *entities,
    size_t entity_count, const qa_render_scene_world_view_input *input,
    qa_model_source_options_fn options, void *options_ctx,
    const qa_model_skinning_frame *skinning_frame,
    qa_model_retain_body_fn retain_body, void *retain_body_ctx,
    size_t *out_count);

#endif
