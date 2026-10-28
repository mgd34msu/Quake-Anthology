/* Port of src/materials/fog.ts. GPL-2.0-or-later. */
#ifndef QA_MATERIALS_FOG_H
#define QA_MATERIALS_FOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/materials/shader_types.h"

typedef struct qa_fog_brush_map_entry { double brush, visible_side; } qa_fog_brush_map_entry;
typedef struct qa_fog_brush_entry { double first_side, side_count; } qa_fog_brush_entry;
typedef struct qa_fog_brush_side_entry { double plane; } qa_fog_brush_side_entry;
typedef struct qa_fog_brush_map {
  const qa_fog_brush_map_entry *fogs; size_t fog_count;
  const qa_fog_brush_entry *brushes; size_t brush_count;
  const qa_fog_brush_side_entry *brush_sides; size_t brush_side_count;
  const qa_plane *planes; size_t plane_count;
} qa_fog_brush_map;

typedef struct qa_fog_volume {
  qa_bounds bounds;
  bool has_surface;
  qa_plane surface;
  qa_vec4 color;
  double tc_scale;
} qa_fog_volume;

/* TS FogAdjustment/FogPass exact strings. */
typedef const char *qa_fog_adjustment_kind;
typedef const char *qa_fog_pass;

qa_fog_volume qa_prepare_fog_volume(qa_fog_brush_map map, double index,
                                    qa_q3_material_fog parameters);

/* Closure state of TS fogCoordinates' returned function. */
typedef struct qa_fog_coordinates_state {
  qa_vec3 distance_vector;
  double offset;
  bool has_surface;
  qa_plane surface;
  double eye_depth;
} qa_fog_coordinates_state;
qa_fog_coordinates_state qa_fog_coordinates(qa_fog_volume fog, qa_vec3 origin,
                                            qa_vec3 forward);
qa_vec2 qa_fog_coordinates_at(qa_fog_coordinates_state state, qa_vec3 position);

double qa_fog_factor(double s, double t);
/* pixels NULL allocates; non-NULL requires length 256*32*4. */
qa_image_level qa_create_fog_texture(uint8_t *pixels, size_t length);

qa_fog_adjustment_kind qa_fog_adjustment(qa_mat_shader_stage stage,
                                         qa_mat_shader_stage first);
qa_vec4 qa_attenuate_fog_color(qa_vec4 color, const char *adjustment,
                               qa_vec2 coordinates);
double qa_shader_sort(const qa_mat_shader_definition *shader);
qa_fog_pass qa_shader_fog_pass(const qa_mat_shader_definition *shader);
/* pass must not be "none". */
qa_render_state qa_fog_pass_state(const char *pass, const char *cull);

#endif
