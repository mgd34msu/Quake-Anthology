/*
 * Port of src/movement/q3/animation.ts. GPL-2.0-or-later.
 * Effects are malloc'd in the result (caller frees); the TS TypeError
 * on a non-"q3" animation state becomes qa_range_error (same message).
 */
#ifndef QA_MOVEMENT_Q3_ANIMATION_H
#define QA_MOVEMENT_Q3_ANIMATION_H
#include <stdbool.h>
#include "qa/contracts/movement.h"
#include "qa/movement/q3/types.h"

typedef struct qa_q3_animation_context {
  qa_actor_animation_state animation;
  bool dead; double elapsed_milliseconds, buttons;
  const char *product; /* "baseq3" | "missionpack" */
  double event_sequence;
} qa_q3_animation_context;

qa_animation_step_result qa_q3_run_animation_operation(qa_q3_animation_request request, qa_q3_animation_context context);
qa_animation_step_result qa_q3_run_torso_operation(double animation, qa_q3_animation_context context, bool continue_animation);
/* Hook adapters: context is borrowed; torso takes the numeric animation (see types note). */
qa_animation_step_result qa_q3_source_animation(qa_q3_animation_request request, const qa_q3_hook_context *context);
qa_animation_step_result qa_q3_source_torso(double animation, const qa_q3_hook_context *context, bool continue_animation);
#endif
