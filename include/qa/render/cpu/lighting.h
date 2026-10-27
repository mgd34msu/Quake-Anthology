/* Port of src/render/cpu/lighting.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_CPU_LIGHTING_H
#define QA_RENDER_CPU_LIGHTING_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/render/cpu/triangle_kernel.h"
typedef struct qa_cpu_vertex { qa_vec4 position; qa_vec2 tex_coord; qa_vec4 color; qa_vec2 tex_coord2; qa_vec3 world_position; qa_vec3 world_normal; } qa_cpu_vertex;
typedef struct qa_cpu_lighting { qa_batch_lighting parameters; const qa_depth_image_level *depth; } qa_cpu_lighting;
typedef struct qa_cpu_triangle_lighting { qa_cpu_lighting base; qa_vec3 positions[3]; qa_vec3 normals[3]; } qa_cpu_triangle_lighting;
/* Culled lights are written to light_storage (capacity = input count);
 * result parameters point there when any light was dropped. */
qa_cpu_lighting qa_cull_triangle_lights(qa_cpu_lighting lighting, const qa_vec3 positions[3], qa_q2_fragment_light *light_storage);
typedef struct qa_cpu_world_attributes { qa_vec3 world_position; qa_vec3 world_normal; } qa_cpu_world_attributes;
qa_cpu_world_attributes qa_cpu_world_attributes_for(qa_batch_lighting lighting, double index);
double qa_cpu_shadow_visibility(qa_vec3 position, qa_vec3 origin, double radius, qa_q2_shadow_projection shadow, qa_q2_shadow_atlas atlas, qa_depth_image_level depth, bool model);
qa_cpu_sample qa_shade_q2_fragment(const qa_cpu_lighting *lighting, qa_vec3 position, qa_vec3 normal, qa_vec4 vertex, const qa_cpu_sample *texel);
void qa_shade_q2_fragment_into(qa_cpu_sample *out, const qa_cpu_lighting *lighting, qa_vec3 position, qa_vec3 normal, qa_vec4 vertex, const qa_cpu_sample *texel);
qa_vec3 qa_cpu_interpolate_world(qa_vec3 a, qa_vec3 b, qa_vec3 c, double wa, double wb, double wc);
void qa_cpu_interpolate_world_into(qa_vec3 *out, qa_vec3 a, qa_vec3 b, qa_vec3 c, double wa, double wb, double wc);
#endif
