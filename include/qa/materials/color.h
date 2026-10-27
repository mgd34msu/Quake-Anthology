/* Port of src/materials/color.ts. GPL-2.0-or-later. */
#ifndef QA_MATERIALS_COLOR_H
#define QA_MATERIALS_COLOR_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/materials/deform.h"
#include "qa/materials/geometry.h"
#include "qa/materials/q3_lighting.h"
#include "qa/materials/shader_types.h"

/* TS StageColorContext. lighting/noise borrow caller storage. */
typedef struct qa_stage_color_context {
  double time, identity_light;
  qa_vec4 entity_rgba;
  const qa_entity_lighting *lighting; /* nullable */
  qa_vec3 view_origin, local_view_origin;
  const qa_renderer_noise *noise;
  qa_vec4 previous_color;
} qa_stage_color_context;

/* has_source_rgb false = TS sourceRgb null; source_rgb is a
 * SourceColorGenerator value otherwise. */
qa_vec4 qa_evaluate_stage_color(qa_mat_shader_stage stage,
    qa_material_vertex vertex, qa_stage_color_context context,
    bool skip_alpha, bool has_source_rgb, int source_rgb);

double qa_animated_picture_index(double time, double frequency, double count);
qa_vec3 qa_diffuse_color(qa_vec3 normal, qa_entity_lighting lighting);
double qa_specular_alpha(qa_vec3 position, qa_vec3 normal,
                         qa_vec3 viewer_origin);

#endif
