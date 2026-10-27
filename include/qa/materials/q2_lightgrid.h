/* Port of src/materials/q2-lightgrid.ts. GPL-2.0-or-later. */
#ifndef QA_MATERIALS_Q2_LIGHTGRID_H
#define QA_MATERIALS_Q2_LIGHTGRID_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/materials/lighting.h"

/* False = TS null; on true, out params view into grid samples. */
bool qa_mat_lookup_q2_lightgrid(qa_q2_lightgrid grid, qa_vec3 point,
    const qa_q2_lightgrid_sample **out, size_t *out_count);

typedef struct qa_q2_lighting_adjustment {
  double add, modulate, saturation;
} qa_q2_lighting_adjustment;

qa_vec3 qa_adjust_q2_lighting(qa_vec3 color,
                              qa_q2_lighting_adjustment adjustment);
/* False = TS null. adjustment NULL = TS default {0,1,1}. */
bool qa_q2_light_grid_point(qa_q2_lightgrid grid, qa_vec3 position,
    const qa_q2_light_style *styles, size_t style_count,
    const qa_q2_lighting_adjustment *adjustment, qa_vec3 *out);

#endif
