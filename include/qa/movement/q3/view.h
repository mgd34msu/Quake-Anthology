/* Port of src/movement/q3/view.ts. GPL-2.0-or-later. */
#ifndef QA_MOVEMENT_Q3_VIEW_H
#define QA_MOVEMENT_Q3_VIEW_H
#include <stddef.h>
#include "qa/contracts/math.h"
typedef struct qa_q3_view_angles_result { qa_vec3 angles, delta; } qa_q3_view_angles_result;
/* intermission: TS readonly number[] membership test. */
qa_q3_view_angles_result qa_q3_view_angles(qa_vec3 command, qa_vec3 delta, qa_vec3 previous,
                                           double health, double movement_type,
                                           const double *intermission, size_t intermission_count);
#endif
