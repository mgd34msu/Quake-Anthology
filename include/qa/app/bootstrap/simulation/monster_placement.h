/* Port of src/app/bootstrap/simulation/monster-placement.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_MONSTER_PLACEMENT_H
#define QA_APP_BOOTSTRAP_SIMULATION_MONSTER_PLACEMENT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
typedef struct qa_app_sim_placement_offset { double x; double y; } qa_app_sim_placement_offset;
/* TS offsetsWithin: sorted by dist^2, then y, then x. Malloc'd; caller frees. */
qa_app_sim_placement_offset *qa_app_sim_placement_offsets_within(double radius, size_t *out_count);
bool qa_app_sim_monster_same_vector(qa_vec3 left, qa_vec3 right);
#endif
