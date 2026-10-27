/* Port of src/render/scene/particles/legacy.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_PARTICLES_LEGACY_H
#define QA_RENDER_SCENE_PARTICLES_LEGACY_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/materials/geometry.h"
#include "qa/render/scene/models/sprites.h"
#include "qa/render/scene/particles/primitives.h"

/* TS ParticlePreparationContext. indexed_profile: "q1" | "q2". */
typedef struct qa_particle_preparation_context {
  qa_scene_camera camera;
  const char *indexed_profile;
  void *palette_color_ctx;
  qa_vec3 (*palette_color)(void *ctx, double index);
} qa_particle_preparation_context;

qa_model_owned_geometry qa_prepare_particle_geometry(
    const qa_scene_particle *particles, size_t particle_count,
    const qa_particle_preparation_context *context);
/* Result owns its render vertices/indices (material vertices map 1:1
 * to RenderVertex position/texCoord/color). */
typedef struct qa_legacy_particle_batch {
  qa_draw_batch batch;
  qa_render_vertex *owned_vertices;
  double *owned_indices;
} qa_legacy_particle_batch;
void qa_legacy_particle_batch_destroy(qa_legacy_particle_batch *batch);
qa_legacy_particle_batch qa_prepare_particle_batch(
    const qa_scene_particle *particles, size_t particle_count,
    const qa_particle_preparation_context *context, qa_renderer_image texture,
    qa_particle_project_fn project, void *project_ctx);

qa_model_owned_geometry qa_q2_beam_geometry(qa_vec3 origin, qa_vec3 old_origin,
                                            double diameter, qa_vec4 color);
qa_legacy_particle_batch qa_q2_beam_batch(
    qa_vec3 origin, qa_vec3 old_origin, double diameter, qa_vec4 color,
    qa_particle_project_fn project, void *project_ctx,
    qa_renderer_image white_image, qa_render_state state);

/* TS legacyParticleImage: level pixels owned in scratch (8x8x4). */
typedef struct qa_legacy_particle_image {
  qa_render_image image;
  qa_image_level level;
  uint8_t *pixels; /* owned */
} qa_legacy_particle_image;
void qa_legacy_particle_image_destroy(qa_legacy_particle_image *image);
/* profile NULL is the TS default "q2". */
qa_legacy_particle_image qa_legacy_particle_image_make(const char *profile);

/* TS Q1ParticleType (exact strings) and Q1ParticleState. */
typedef struct qa_q1_particle_state {
  qa_vec3 origin, velocity;
  double color, ramp, die;
  const char *type;
} qa_q1_particle_state;
qa_q1_particle_state qa_advance_q1_particle(qa_q1_particle_state particle,
                                            double seconds, double gravity);

typedef struct qa_q2_particle_state {
  double spawn_milliseconds;
  qa_vec3 origin, velocity, acceleration;
  double color, alpha, alpha_velocity;
} qa_q2_particle_state;
/* False is TS null. */
bool qa_sample_q2_particle(qa_q2_particle_state particle, double milliseconds,
                           qa_scene_particle *out);

#endif
