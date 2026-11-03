/*
 * Port of src/render/scene/models/light-sampler.ts. The sampler reads the
 * WorldScene (render/scene/world.h) it was built over on every call, as TS
 * reads this.world: surfaces, options and shaders.sun. C accommodation:
 *  - prepareBrushFace (TS owner: src/render/scene/geometry.ts) is computed
 *    locally in light_sampler.c for the LightmapFace the sampler consumes:
 *    qa_prepare_brush_face in geometry.c keeps the face lighting in one
 *    static slot shared by every map, which a sampler retaining faces for
 *    its world's lifetime cannot use.
 * GPL-2.0-or-later.
 */
#ifndef QA_RENDER_SCENE_MODELS_LIGHT_SAMPLER_H
#define QA_RENDER_SCENE_MODELS_LIGHT_SAMPLER_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/materials/lighting.h"
#include "qa/materials/q3_lighting.h"
#include "qa/render/scene/world.h"

/* TS ModelLightSample. has_floor false is TS floor null. */
typedef struct qa_model_light_sample {
  qa_vec3 color;
  bool has_floor;
  qa_vec3 floor_point;
  qa_plane floor_plane;
} qa_model_light_sample;

typedef struct qa_model_light_sampler qa_model_light_sampler;
struct qa_model_light_sampler {
  const qa_world_scene *world; /* borrowed; must outlive the sampler */
  bool has_grid;
  qa_light_grid grid;
  /* unlit legacy faces by surface index (TS unlitFaces Map) */
  double *unlit_indices;
  qa_lightmap_face *unlit_faces;
  size_t unlit_count;
};

/* TS new ModelLightSampler(world). False only on OOM. */
bool qa_model_light_sampler_init(qa_model_light_sampler *sampler,
                                 const qa_world_scene *world);
void qa_model_light_sampler_destroy(qa_model_light_sampler *sampler);
/* TS sample(point, input, includeDynamic = true). */
qa_model_light_sample qa_model_light_sampler_sample(
    qa_model_light_sampler *sampler, qa_vec3 point,
    const qa_world_view_input *input, bool include_dynamic);
/* TS entityLighting(entity, input, noWorldModel = false). */
qa_entity_lighting qa_model_light_sampler_entity_lighting(
    qa_model_light_sampler *sampler, const qa_scene_entity *entity,
    const qa_world_view_input *input, bool no_world_model);

#endif
