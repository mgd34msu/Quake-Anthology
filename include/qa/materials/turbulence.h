/* Port of src/materials/turbulence.ts: Q1 gl_warp_sin table.
 * GPL-2.0-or-later. */
#ifndef QA_MATERIALS_TURBULENCE_H
#define QA_MATERIALS_TURBULENCE_H

typedef enum qa_turbulence_profile {
  QA_TURBULENCE_Q1, QA_TURBULENCE_Q2
} qa_turbulence_profile;

double qa_source_turbulence(double phase, qa_turbulence_profile profile);

#endif
