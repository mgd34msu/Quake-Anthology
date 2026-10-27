/* Port of src/content/q2/foundation/motion.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_MOTION_H
#define QA_CONTENT_Q2_FOUNDATION_MOTION_H
#include "qa/content/q2/foundation/callbacks.h"
typedef struct qa_q2_linear_curve_checkpoint { const double *positions; size_t position_count; double frame, subframe, subframes; } qa_q2_linear_curve_checkpoint;
typedef struct qa_q2_linear_motion_entry {
  qa_saved_actor_id actor; qa_vec3 direction, destination, reference;
  double remaining, current_speed, move_speed, next_speed, decel_distance;
  const char *done; bool has_curve; qa_q2_linear_curve_checkpoint curve;
} qa_q2_linear_motion_entry;
typedef struct qa_q2_linear_motion_checkpoint { const qa_q2_linear_motion_entry *entries; size_t count; } qa_q2_linear_motion_checkpoint;
typedef struct qa_q2_linear_motion qa_q2_linear_motion;
qa_q2_linear_motion *qa_q2_linear_motion_create(const char *prefix);
void qa_q2_linear_motion_destroy(qa_q2_linear_motion *m);
bool qa_q2_linear_motion_destination(qa_q2_linear_motion *m, qa_q2_entity *e, qa_vec3 *out);
qa_q2_callback_definitions qa_q2_linear_motion_callbacks(qa_q2_linear_motion *m);
qa_q2_linear_motion_checkpoint qa_q2_linear_motion_capture(qa_q2_linear_motion *m, qa_q2_game_services *g);
void qa_q2_linear_motion_restore(qa_q2_linear_motion *m, qa_q2_game_services *g, qa_q2_linear_motion_checkpoint cp);
void qa_q2_linear_motion_move_to(qa_q2_linear_motion *m, qa_q2_entity *e, qa_q2_game_services *g, qa_vec3 dest, qa_q2_think done);
#endif
