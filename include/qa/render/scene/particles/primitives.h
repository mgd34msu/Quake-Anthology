/* Port of src/render/scene/particles/primitives.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_PARTICLES_PRIMITIVES_H
#define QA_RENDER_SCENE_PARTICLES_PRIMITIVES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/materials/geometry.h"

/* TS EntityGeometry: owned vertices/indices. */
typedef struct qa_entity_geometry {
  qa_material_vertex *vertices; /* owned */
  size_t vertex_count;
  double *indices; /* owned */
  size_t index_count;
} qa_entity_geometry;
void qa_entity_geometry_destroy(qa_entity_geometry *geometry);

/* TS RailPose. kind: "rail-core" | "rail-rings" | "lightning". */
typedef struct qa_rail_pose {
  const char *kind;
  qa_vec3 origin, old_origin;
  qa_vec4 shader_rgba;
} qa_rail_pose;
typedef struct qa_sprite_pose {
  qa_vec3 origin;
  double radius, rotation;
  qa_vec4 shader_rgba;
} qa_sprite_pose;
typedef struct qa_beam_pose {
  qa_vec3 origin, old_origin;
} qa_beam_pose;

typedef struct qa_rail_settings {
  double core_width, ring_width, segment_length;
} qa_rail_settings;
extern const qa_rail_settings qa_default_rail_settings;

/* settings NULL is the TS default. */
qa_entity_geometry qa_rail_geometry(qa_rail_pose entity, qa_vec3 view_origin,
                                    const qa_rail_settings *settings);
/* TS polyGeometry input vertex (position/texCoord/color). */
typedef struct qa_poly_input_vertex {
  qa_vec3 position;
  qa_vec2 tex_coord;
  qa_vec4 color;
} qa_poly_input_vertex;
qa_entity_geometry qa_poly_geometry(const qa_poly_input_vertex *vertices,
                                    size_t vertex_count);
qa_entity_geometry qa_sprite_geometry(qa_sprite_pose entity, qa_axis view_axis,
                                      bool mirror);
/* Index of the first overlapping fog bounds; -1 when none. */
double qa_sprite_fog(qa_vec3 origin, double radius, const qa_bounds *bounds,
                     size_t bounds_count);

/* DrawBatch-producing forms. project is fn+ctx. Result owns vertices
 * and indices (free with qa_particle_draw_batch_destroy). */
typedef qa_vec4 (*qa_particle_project_fn)(void *ctx, qa_vec3 point);
typedef struct qa_particle_draw_batch {
  qa_draw_batch batch;
  qa_render_vertex *owned_vertices;
  double *owned_indices;
} qa_particle_draw_batch;
void qa_particle_draw_batch_destroy(qa_particle_draw_batch *batch);

qa_particle_draw_batch qa_beam_batch(qa_beam_pose entity,
                                     qa_particle_project_fn project,
                                     void *project_ctx, qa_render_state previous,
                                     qa_renderer_image white_image);
qa_particle_draw_batch qa_default_model_batch(
    qa_model_transform entity, qa_particle_project_fn project,
    void *project_ctx, qa_render_state state, qa_renderer_image white_image);

#endif
