/*
 * Port of src/materials/sky.ts. Built geometry arrays are malloc'd;
 * free with qa_sky_geometry_destroy. GPL-2.0-or-later.
 */
#ifndef QA_MATERIALS_SKY_H
#define QA_MATERIALS_SKY_H

#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/materials/deform.h"
#include "qa/materials/geometry.h"

extern const char *const qa_sky_face_suffixes[6];

typedef struct qa_sky_index_strip {
  double *indices; size_t index_count; /* owned */
} qa_sky_index_strip;
typedef struct qa_sky_face {
  double face;
  qa_deform_geometry geometry; /* arrays owned */
  qa_sky_index_strip *strips; size_t strip_count; /* owned */
} qa_sky_face;
typedef struct qa_sky_geometry {
  qa_sky_face *box; size_t face_count; /* owned */
  qa_deform_geometry clouds;           /* arrays owned */
} qa_sky_geometry;
void qa_sky_geometry_destroy(qa_sky_geometry *geometry);

qa_vec3 qa_sky_vector(double face, double s, double t, double size);
qa_vec2 qa_cloud_tex_coord(double face, double s, double t, double height);

typedef struct qa_sky_face_bounds {
  double min_s, min_t, max_s, max_t;
} qa_sky_face_bounds;

/* TS SkyBuilder. */
typedef struct qa_sky_builder {
  qa_vec2 cloud_coords[6][81];
  qa_sky_face_bounds bounds[6];
  int bounds_count;
} qa_sky_builder;
void qa_sky_builder_init(qa_sky_builder *builder);
void qa_sky_builder_initialize_cloud_coordinates(qa_sky_builder *builder,
                                                 double height);
void qa_sky_builder_clip(qa_sky_builder *builder,
                         const qa_deform_geometry *meshes, size_t mesh_count,
                         qa_vec3 origin);
qa_sky_geometry qa_sky_builder_build(qa_sky_builder *builder, qa_vec3 origin,
                                     double far);

#endif
