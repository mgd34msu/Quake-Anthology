/* Port of src/movement/q3/postures.ts. GPL-2.0-or-later. */
#include "qa/movement/q3/postures.h"

const qa_bounds qa_q3_source_standing_bounds = {{-15, -15, -24}, {15, 15, 32}};
const qa_q3_postures qa_q3_source_postures = {
    26,
    {{{-15, -15, -24}, {15, 15, 16}}, 12},
    {{{-15, -15, -24}, {15, 15, -8}}, -16},
    {{-42, -42, -42}, {42, 42, 42}}};

qa_fixed_movement_pose qa_q3_invulnerability_pose(bool expanded, qa_q3_postures postures) {
  qa_fixed_movement_pose pose = {true,
                                 expanded ? postures.invulnerability_expanded : postures.crouched.bounds,
                                 postures.crouched.view_height};
  return pose;
}
