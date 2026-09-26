#ifndef QA_MATERIAL_INTERNAL_H
#define QA_MATERIAL_INTERNAL_H
#include "qa/material.h"

float qa_material_noise(float x, float y, float z, float time);
float qa_material_sine(unsigned index);
unsigned qa_material_table_index(float value);
float qa_material_inverse_sqrt(float square);
qa_vec3 qa_material_fast_normalize(qa_vec3 value);
float qa_material_fog_factor(float s, float t);
qa_scene_vec2 qa_material_fog_coordinates(const qa_material_context *, qa_vec3 local_position);
bool qa_material_stage_texcoord(const qa_material_stage *, const qa_scene_vertex *,
                                const qa_material_context *, float time,
                                qa_scene_vec2 *, qa_error *);
bool qa_material_stage_color(const qa_material_stage *, const qa_scene_vertex *,
                             const qa_material_context *, float time,
                             qa_scene_vec4 previous, qa_scene_vec4 *, qa_error *);
bool qa_material_deform_mesh(const qa_material *, const qa_scene_mesh *,
                             const qa_material_context *, float time,
                             qa_scene_frame *, qa_scene_mesh *, qa_error *);
#endif
