/*
 * Port of src/materials/lighting.ts. Returned image pixel buffers are
 * malloc'd; free with qa_built_lightmap_destroy / free(level pixels).
 * GPL-2.0-or-later.
 */
#ifndef QA_MATERIALS_LIGHTING_H
#define QA_MATERIALS_LIGHTING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"

typedef struct qa_q2_light_style { qa_vec3 rgb; double white; } qa_q2_light_style;

double qa_q1_light_style(const char *map, double time, int interpolation);
qa_q2_light_style qa_q2_light_style_value(const char *map, double milliseconds);

/* TS LightmapProjection. kind: "classic"|"decoupled". */
typedef struct qa_lightmap_projection {
  const char *kind;
  qa_texture_projection texture;
  qa_vec2 texture_mins;
  qa_decoupled_lightmap mapping;
} qa_lightmap_projection;

qa_vec2 qa_lightmap_coordinates(qa_vec3 position,
                                qa_lightmap_projection projection);

/* TS LightmapFace. lighting NULL = TS null. */
typedef struct qa_lightmap_face {
  double width, height;
  const qa_bsp_lighting *lighting;
  double offset;
  const double *styles; size_t style_count;
  qa_plane plane;
  qa_lightmap_projection projection;
} qa_lightmap_face;

typedef struct qa_surface_dynamic_light {
  qa_vec3 origin;
  double radius, minimum;
  qa_vec3 color;
} qa_surface_dynamic_light;

/* TS Q1LightmapEncoding strings: "rgb"|"inverted-luminance"|"inverted-alpha". */
typedef const char *qa_q1_lightmap_encoding;

typedef struct qa_built_lightmap {
  qa_image_level image; /* pixels malloc'd */
  const char *encoding;
} qa_built_lightmap;
void qa_built_lightmap_destroy(qa_built_lightmap *built);

typedef struct qa_build_q1_options {
  const char *encoding; /* NULL = TS undefined */
  bool fullbright;
  const qa_surface_dynamic_light *dynamic_lights; size_t dynamic_light_count;
} qa_build_q1_options;
typedef struct qa_build_q2_options {
  bool has_modulate;
  double modulate;
  const char *mono; /* NULL = "0" */
  const qa_surface_dynamic_light *dynamic_lights; size_t dynamic_light_count;
} qa_build_q2_options;

qa_built_lightmap qa_build_q1_lightmap(qa_lightmap_face face,
    const double *styles, size_t style_count, qa_build_q1_options options);
qa_built_lightmap qa_build_q2_lightmap(qa_lightmap_face face,
    const qa_q2_light_style *styles, size_t style_count,
    qa_build_q2_options options);

qa_vec2 qa_lightmap_atlas_coordinates(qa_vec3 position,
    qa_lightmap_projection projection, qa_vec2 location,
    double atlas_width, double atlas_height);
/* False = TS null (outside). filtering: "nearest"|"bilinear". */
bool qa_sample_lightmap(qa_image_level image, qa_vec2 uv,
                        const char *filtering, qa_vec4 *out);

/* TS LightmapAtlas. */
typedef struct qa_lightmap_atlas {
  double width, height;
  int32_t *heights;
} qa_lightmap_atlas;
void qa_lightmap_atlas_init(qa_lightmap_atlas *atlas, double width,
                            double height);
void qa_lightmap_atlas_destroy(qa_lightmap_atlas *atlas);
void qa_lightmap_atlas_clear(qa_lightmap_atlas *atlas);
/* False = TS null. */
bool qa_lightmap_atlas_allocate(qa_lightmap_atlas *atlas, double width,
                                double height, qa_vec2 *out);

qa_image_level qa_direct_lightmap_pixels(qa_built_lightmap built);

#endif
