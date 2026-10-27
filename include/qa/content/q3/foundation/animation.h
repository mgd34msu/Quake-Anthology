/* Port of src/content/q3/foundation/animation.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_FOUNDATION_ANIMATION_H
#define QA_CONTENT_Q3_FOUNDATION_ANIMATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/foundation/animation_config.h"
enum { QA_Q3_ANIMATION_TOGGLE_BIT = 128 };
typedef struct qa_q3_lerp_frame {
  double old_frame, old_frame_time, frame, frame_time, back_lerp, animation_number;
  const qa_q3_animation *current_animation; /* NULL = TS null */
  double animation_time;
} qa_q3_lerp_frame;
typedef struct qa_q3_run_lerp_frame_input {
  double time_ms, new_animation, speed_scale; bool no_player_animations;
} qa_q3_run_lerp_frame_input;
/* TS AnimationSet = Pick<PlayerAnimationConfig,"animations">. */
typedef struct qa_q3_animation_set {
  const qa_q3_animation *animations; const bool *present; size_t count;
} qa_q3_animation_set;
qa_q3_animation_set qa_q3_animation_set_from_config(const qa_q3_player_animation_config *config);
typedef void (*qa_q3_lerp_print_fn)(void *ctx, const char *message);
qa_q3_lerp_frame qa_q3_create_lerp_frame(void);
void qa_q3_set_lerp_frame_animation(qa_q3_animation_set config, qa_q3_lerp_frame *state,
    double new_animation, qa_q3_lerp_print_fn print, void *print_ctx);
void qa_q3_run_lerp_frame(qa_q3_animation_set config, qa_q3_lerp_frame *state,
    qa_q3_run_lerp_frame_input input, qa_q3_lerp_print_fn print, void *print_ctx);
void qa_q3_clear_lerp_frame(qa_q3_animation_set config, qa_q3_lerp_frame *state,
    double animation, double time_ms, qa_q3_lerp_print_fn print, void *print_ctx);
#endif
