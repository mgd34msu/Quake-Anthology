/* Port of src/render/scene/models/lighting.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_MODELS_LIGHTING_H
#define QA_RENDER_SCENE_MODELS_LIGHTING_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"

enum { QA_Q2_SHELL_MASK = 1024 | 2048 | 4096 | 65536 | 131072 };

/* False is TS null. flags is a TS number; bit tests use ToInt32. */
bool qa_q2_shell_color(double flags, qa_vec3 *out);
qa_vec3 qa_q2_alias_light(double flags, qa_vec3 sampled, double time_seconds,
                          bool monochrome, bool infrared);
qa_vec3 qa_alias_shadow_point(qa_vec3 point, qa_vec3 shade_vector,
                              double entity_height, double floor_height);
qa_vec3 qa_q1_alias_shadow_point(qa_vec3 point, qa_vec3 shade_vector,
                                 double entity_height, double floor_height);
qa_vec3 qa_q1_alias_shadow_direction(double yaw);
/* Malloc'd result; caller frees. modulate/monochrome mirror TS defaults
 * when has_modulate is false (1). */
qa_q2_model_shadow_light *qa_alias_shadow_light_fractions(
    qa_vec3 origin, qa_vec3 shade, const qa_q2_fragment_light *lights,
    size_t light_count, double modulate, bool monochrome, size_t *out_count);
double qa_alias_shade_divisor(qa_vec3 shade);

#endif
