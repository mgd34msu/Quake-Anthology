/*
 * Port of src/render/scene/particles/q3-types.ts (types only).
 * ParticleClientState fields the TS reads through getters become
 * pointers to caller storage. GPL-2.0-or-later.
 */
#ifndef QA_RENDER_SCENE_PARTICLES_Q3_TYPES_H
#define QA_RENDER_SCENE_PARTICLES_Q3_TYPES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"

/* TS ParticleShader: identity is the pointer. */
typedef struct qa_particle_shader { const char *name; } qa_particle_shader;

/* TS ParticleResources. register_shader NULL result is TS null; the
 * TS Promise is synchronous here. */
typedef struct qa_particle_resources {
  void *ctx;
  const qa_particle_shader *(*register_shader)(void *ctx, const char *name);
} qa_particle_resources;

typedef struct qa_ref_poly_vertex {
  qa_vec3 position;
  qa_vec2 tex_coord;
  qa_vec4 color;
} qa_ref_poly_vertex;
typedef struct qa_ref_poly {
  const qa_particle_shader *shader; /* nullable */
  qa_ref_poly_vertex *vertices;     /* owned in system outputs */
  size_t vertex_count;
} qa_ref_poly;

/* TS ParticleClientState. */
typedef struct qa_particle_client_state {
  const double *time;          /* borrowed */
  const qa_axis *view_axis;    /* borrowed (refdef.viewAxis) */
  const qa_vec3 *snap_origin;  /* NULL is TS snap null */
} qa_particle_client_state;

/* TS ParticleClientEntity (currentState fields). */
typedef struct qa_particle_client_entity {
  qa_vec3 origin, origin2, angles, angles2;
  double time, time2, frame;
} qa_particle_client_entity;

/* TS ParticleTrace. solidity: "clear" | "start-solid" | "all-solid". */
typedef struct qa_particle_trace {
  qa_vec3 end;
  double entity_num;
  const char *solidity;
  double fraction;
} qa_particle_trace;

/* TS ParticleTracer. */
typedef struct qa_particle_tracer {
  void *ctx;
  qa_particle_trace (*trace)(void *ctx, qa_vec3 start, qa_vec3 end,
                             qa_bounds bounds, double pass_entity,
                             double contents);
} qa_particle_tracer;

#endif
