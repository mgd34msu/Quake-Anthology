/*
 * Port of src/render/scene/models/light-sampler.ts. C accommodations:
 *  - WorldScene / WorldViewInput / WorldSurface (TS owner:
 *    src/render/scene/world.ts, unported) are mirrored below with
 *    collision-proof names, carrying only the fields this slice and
 *    renderer.ts read. materialContext/fogSelections/shader registry
 *    behaviors arrive as fn+ctx / opaque handles.
 *  - prepareBrushFace (TS owner: src/render/scene/geometry.ts,
 *    unported) is mirrored locally in light_sampler.c for the
 *    LightmapFace the sampler consumes.
 * GPL-2.0-or-later.
 */
#ifndef QA_RENDER_SCENE_MODELS_LIGHT_SAMPLER_H
#define QA_RENDER_SCENE_MODELS_LIGHT_SAMPLER_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/time.h"
#include "qa/materials/lighting.h"
#include "qa/materials/q3_lighting.h"

/* Mirrored legacy WorldSurface fields (TS owner: world.ts). */
typedef struct qa_render_scene_world_surface {
  bool is_legacy;
  double index;
  bool has_lightmap_face;
  qa_lightmap_face lightmap_face;
  bool material_is_q1;
  const char *material_surface; /* q1 material surface string */
  double material_surface_flags; /* q2 material surfaceFlags */
} qa_render_scene_world_surface;

/* Mirrored fog selection (TS WorldScene.fogSelections entry). */
typedef struct qa_render_scene_fog_selection {
  qa_bounds volume_bounds;
  double index;
  const void *volume; /* opaque TS FogVolume */
} qa_render_scene_fog_selection;

/* Mirrored WorldScene (TS owner: world.ts). */
typedef struct qa_render_scene_world {
  qa_decoded_world map;
  const qa_render_scene_world_surface *surfaces;
  size_t surface_count;
  bool has_q3_lightmap_overbright;
  double q3_lightmap_overbright;
  bool has_q2_light_modulate;
  double q2_light_modulate;
  bool has_sun;
  qa_vec3 sun_direction;
  const qa_render_scene_fog_selection *fog_selections;
  size_t fog_selection_count;
  const void *shaders; /* opaque TS SceneShaderRegistry */
  /* TS world.shaders.registrations.owner.remapDefinition(name):
   * false is TS null; on true the replacement/source outs are set. */
  void *remap_ctx;
  bool (*remap_definition)(void *ctx, const char *name,
                           const char **replacement_out,
                           const void **source_registry_out);
  bool (*registry_has_cinematic)(void *ctx, const void *registry,
                                 const char *name);
  /* TS WorldScene.materialContext(input, transform, fog?): fills the
   * caller's material draw context fields this slice then overrides. */
  void *material_context_ctx;
  void (*material_context)(void *ctx, const void *input,
                           qa_vec3 origin, qa_axis axis,
                           const void *fog_volume, void *out_context);
} qa_render_scene_world;

/* Mirrored WorldViewInput fields read by this slice (TS: world.ts). */
typedef struct qa_render_scene_world_view_input {
  qa_scene_camera camera;
  qa_source_time time;
  const double *q1_styles; /* NULL is TS undefined */
  size_t q1_style_count;
  const qa_q2_light_style *q2_styles;
  size_t q2_style_count;
  const qa_surface_dynamic_light *lights;
  size_t light_count;
  bool has_lights;
  bool has_q2_fragment_lighting;
  const qa_q2_fragment_light *q2_fragment_lights;
  size_t q2_fragment_light_count;
  const qa_q2_shadow_atlas *q2_fragment_atlas; /* nullable */
  const qa_q3_dynamic_light *q3_lights;
  size_t q3_light_count;
  bool has_identity_light;
  double identity_light;
  bool world_space_batches;
} qa_render_scene_world_view_input;

/* TS ModelLightSample. has_floor false is TS floor null. */
typedef struct qa_model_light_sample {
  qa_vec3 color;
  bool has_floor;
  qa_vec3 floor_point;
  qa_plane floor_plane;
} qa_model_light_sample;

typedef struct qa_model_light_sampler qa_model_light_sampler;
struct qa_model_light_sampler {
  const qa_render_scene_world *world; /* borrowed */
  bool has_grid;
  qa_light_grid grid;
  /* unlit legacy faces by surface index */
  double *unlit_indices;
  qa_lightmap_face *unlit_faces;
  size_t unlit_count;
};

/* False only on OOM. */
bool qa_model_light_sampler_init(qa_model_light_sampler *sampler,
                                 const qa_render_scene_world *world);
void qa_model_light_sampler_destroy(qa_model_light_sampler *sampler);
qa_model_light_sample qa_model_light_sampler_sample(
    qa_model_light_sampler *sampler, qa_vec3 point,
    const qa_render_scene_world_view_input *input, bool include_dynamic);
qa_entity_lighting qa_model_light_sampler_entity_lighting(
    qa_model_light_sampler *sampler, const qa_scene_entity *entity,
    const qa_render_scene_world_view_input *input, bool no_world_model);

#endif
