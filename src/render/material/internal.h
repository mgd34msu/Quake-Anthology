#ifndef QA_MATERIAL_INTERNAL_H
#define QA_MATERIAL_INTERNAL_H
#include "qa/material.h"

float qa_material_noise(float x, float y, float z, float time);
float qa_material_sine(unsigned index);
unsigned qa_material_table_index(float value);
float qa_material_inverse_sqrt(float square);
qa_vec3 qa_material_fast_normalize(qa_vec3 value);
qa_scene_vec2 qa_material_fog_coordinates(const qa_material_context *, qa_vec3 local_position);
typedef struct material_color_state {
    qa_scene_vec4 entity;
    float rgb_wave, alpha_wave;
} material_color_state;
bool material_color_prepare(const qa_material_stage *, const qa_material_context *, float,
                            material_color_state *, qa_error *);
bool material_color_vertex(const qa_material_stage *, const qa_scene_vertex *,
                           const qa_material_context *, const material_color_state *,
                           qa_scene_vec4 previous, qa_scene_vec4 *, qa_error *);
typedef struct material_tcmod_state {
    qa_scene_vec2 scroll, translate;
    float sine, cosine, scale, now;
    double rotation;
    bool rotation_valid;
} material_tcmod_state;
void material_tcmod_prepare(const qa_material_tcmod *, const qa_material_context *, float, material_tcmod_state *);
void material_tcmods_prepare(const qa_material_stage *, const qa_material_context *, float, material_tcmod_state *);
bool material_texcoord_vertex(const qa_material_stage *, const qa_scene_vertex *,
                              const qa_material_context *, float, const material_tcmod_state *,
                              qa_scene_vec2 *, qa_error *);
bool qa_material_stage_texcoord(const qa_material_stage *, const qa_scene_vertex *,
                                const qa_material_context *, float time,
                                qa_scene_vec2 *, qa_error *);
bool qa_material_stage_color(const qa_material_stage *, const qa_scene_vertex *,
                             const qa_material_context *, float time,
                             qa_scene_vec4 previous, qa_scene_vec4 *, qa_error *);
bool qa_material_deform_mesh(const qa_material *, const qa_scene_mesh *,
                             const qa_material_context *, float time,
                             qa_scene_frame *, qa_scene_mesh *, qa_error *);
bool material_source_sort(qa_scene_group **, size_t, qa_error *);
#endif
