/* Port of src/materials/q2-lighting.ts. GPL-2.0-or-later. */
#ifndef QA_MATERIALS_Q2_LIGHTING_H
#define QA_MATERIALS_Q2_LIGHTING_H

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"

/* TS DynamicLightSample = Q2FragmentLight without shadow. */
typedef struct qa_dynamic_light_sample {
  qa_vec3 origin;
  double radius;
  qa_vec3 color;
  double scale;
  const qa_q2_light_cone *cone; /* nullable */
} qa_dynamic_light_sample;

double qa_point_light_falloff(double distance, double radius);
double qa_spot_cone_attenuation(qa_vec3 direction, qa_vec3 cone_direction,
                                 double cone_cos);
qa_vec3 qa_calc_dynamic_light_contribution(qa_dynamic_light_sample light,
    qa_vec3 position, qa_vec3 normal);
void qa_calc_dynamic_light_contribution_into(qa_vec3 *out,
    qa_dynamic_light_sample light, qa_vec3 position, qa_vec3 normal);

#endif
