/* Port of src/materials/legacy-fog.ts. GPL-2.0-or-later. */
#ifndef QA_MATERIALS_LEGACY_FOG_H
#define QA_MATERIALS_LEGACY_FOG_H

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"

typedef struct qa_q1_fog { double density; qa_vec3 color; } qa_q1_fog;
typedef struct qa_q1_fog_transition {
  qa_q1_fog previous, target;
  double start, duration;
} qa_q1_fog_transition;

/* TS Q1FogState. */
typedef struct qa_q1_fog_state {
  qa_q1_fog previous, target;
  double start, duration;
} qa_q1_fog_state;

void qa_q1_fog_state_init(qa_q1_fog_state *state);
void qa_q1_fog_state_update(qa_q1_fog_state *state, qa_q1_fog value,
                            double time, double duration);
qa_q1_fog_transition qa_q1_fog_state_capture(const qa_q1_fog_state *state);
void qa_q1_fog_state_install(qa_q1_fog_state *state,
                             qa_q1_fog_transition value);
qa_q1_fog qa_q1_fog_state_sample(const qa_q1_fog_state *state, double time);

qa_q1_fog_transition qa_q1_world_fog(const char *entities);
double qa_global_fog_amount(double density_scaled, double frag_depth);
double qa_height_fog_fraction(double world_z, double start, double end);
double qa_height_fog_dir_z(double direction_z);
double qa_height_fog_extinction(double view_z, double world_z, double start,
                                double falloff, double direction_z);
double qa_height_fog_amount(double density, double depth, double extinction);
/* fog must be the "q2" SceneFog variant. */
qa_vec3 qa_q2_fog_color(qa_vec3 color, qa_scene_fog fog, qa_vec3 point,
                        qa_vec3 view, double depth, bool sky);
qa_vec3 qa_q1_fog_color(qa_vec3 color, qa_q1_fog fog, double depth);

#endif
