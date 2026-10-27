/* Port of src/movement/q3/animation.ts. GPL-2.0-or-later. */
#include "qa/movement/q3/animation.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"

/* TS result() emits the animation effect on object identity change
 * (a spread copy), even when every field compares equal. */
static qa_animation_step_result result_changed(qa_q3_animation_context context, qa_animation_state state,
                                               bool changed, const double *events, size_t event_count) {
  size_t count = (changed ? 1 : 0) + event_count;
  qa_movement_effect *effects = count ? malloc(count * sizeof *effects) : NULL;
  if (count && effects == NULL) qa_range_error("Out of memory");
  size_t i = 0;
  if (changed) {
    effects[i] = (qa_movement_effect){0};
    effects[i].kind = QA_MOVEMENT_EFFECT_ANIMATION; effects[i].provider = context.animation.provider;
    effects[i].animation_before = context.animation.state; effects[i].animation_after = state; i++;
  }
  for (size_t e = 0; e < event_count; e++) {
    effects[i] = (qa_movement_effect){0};
    effects[i].kind = QA_MOVEMENT_EFFECT_EVENT;
    effects[i].value = (qa_predictable_movement_event){context.animation.provider, context.event_sequence, events[e], 0};
    i++;
  }
  qa_animation_step_result r = {{context.animation.provider, state}, effects, count};
  return r;
}
static qa_animation_state require_q3(qa_q3_animation_context context, const char *message) {
  if (context.animation.state.kind == NULL || strcmp(context.animation.state.kind, "q3") != 0)
    qa_range_error("%s", message);
  return context.animation.state;
}

qa_animation_step_result qa_q3_run_animation_operation(qa_q3_animation_request request, qa_q3_animation_context context) {
  qa_animation_state state = require_q3(context, "Q3 animation operations require the selected Q3 character adapter");
  switch (request.kind) {
    case QA_Q3_ANIM_LEGS: {
      double timer = request.force ? 0 : state.legs_timer_milliseconds;
      if (context.dead || timer > 0 || (!request.force && (double)(qa_int32(state.legs) & ~128) == request.animation)) {
        qa_animation_state next = state; next.legs_timer_milliseconds = timer;
        return result_changed(context, timer == state.legs_timer_milliseconds ? state : next, timer != state.legs_timer_milliseconds, NULL, 0);
      }
      qa_animation_state next = state; next.legs_timer_milliseconds = timer;
      next.legs = (double)(((qa_int32(state.legs) & 128) ^ 128) | qa_int32(request.animation));
      return result_changed(context, next, true, NULL, 0);
    }
    case QA_Q3_ANIM_LEGS_TIMER: {
      qa_animation_state next = state; next.legs_timer_milliseconds = request.milliseconds;
      return result_changed(context, next, true, NULL, 0);
    }
    case QA_Q3_ANIM_DROP_TIMERS: {
      double elapsed = context.elapsed_milliseconds;
      qa_animation_state next = state;
      if (state.legs_timer_milliseconds > 0) next.legs_timer_milliseconds = fmax(0, state.legs_timer_milliseconds - elapsed);
      if (state.torso_timer_milliseconds > 0) next.torso_timer_milliseconds = fmax(0, state.torso_timer_milliseconds - elapsed);
      return result_changed(context, next, true, NULL, 0);
    }
    case QA_Q3_ANIM_GESTURE: {
      if (state.torso_timer_milliseconds != 0) return result_changed(context, state, false, NULL, 0);
      if (qa_int32(context.buttons) & QA_Q3_BUTTON_GESTURE) {
        qa_animation_state next = state;
        if (!context.dead) next.torso = (double)(((qa_int32(state.torso) & 128) ^ 128) | QA_Q3_ANIM_TORSO_GESTURE);
        next.torso_timer_milliseconds = 34 * 66 + 50;
        double event = QA_Q3_EV_TAUNT;
        return result_changed(context, next, true, &event, 1);
      }
      if (strcmp(context.product, "missionpack") == 0) {
        static const int gestures[][2] = {{QA_Q3_BUTTON_GETFLAG, QA_Q3_ANIM_TORSO_GETFLAG},
          {QA_Q3_BUTTON_GUARDBASE, QA_Q3_ANIM_TORSO_GUARDBASE}, {QA_Q3_BUTTON_PATROL, QA_Q3_ANIM_TORSO_PATROL},
          {QA_Q3_BUTTON_FOLLOWME, QA_Q3_ANIM_TORSO_FOLLOWME}, {QA_Q3_BUTTON_AFFIRMATIVE, QA_Q3_ANIM_TORSO_AFFIRMATIVE},
          {QA_Q3_BUTTON_NEGATIVE, QA_Q3_ANIM_TORSO_NEGATIVE}};
        for (size_t i = 0; i < sizeof gestures / sizeof gestures[0]; i++) {
          if (qa_int32(context.buttons) & gestures[i][0]) {
            qa_animation_state next = state;
            if (!context.dead) next.torso = (double)(((qa_int32(state.torso) & 128) ^ 128) | gestures[i][1]);
            next.torso_timer_milliseconds = 600;
            return result_changed(context, next, true, NULL, 0);
          }
        }
      }
      return result_changed(context, state, false, NULL, 0);
    }
  }
  return result_changed(context, state, false, NULL, 0);
}

qa_animation_step_result qa_q3_run_torso_operation(double animation, qa_q3_animation_context context, bool continue_animation) {
  qa_animation_state state = require_q3(context, "Q3 torso operations require the selected Q3 character adapter");
  if (context.dead || (continue_animation &&
      ((double)(qa_int32(state.torso) & ~128) == animation || state.torso_timer_milliseconds > 0)))
    return result_changed(context, state, false, NULL, 0);
  qa_animation_state next = state;
  next.torso = (double)(((qa_int32(state.torso) & 128) ^ 128) | qa_int32(animation));
  return result_changed(context, next, true, NULL, 0);
}

static qa_q3_animation_context character_context(const qa_q3_hook_context *context) {
  qa_q3_animation_context c = {context->animation, context->motion->pm_type >= QA_Q3_PM_DEAD,
      context->frame.elapsed.value, context->command.buttons, context->motion->product,
      context->motion->event_sequence};
  return c;
}
qa_animation_step_result qa_q3_source_animation(qa_q3_animation_request request, const qa_q3_hook_context *context) {
  return qa_q3_run_animation_operation(request, character_context(context));
}
qa_animation_step_result qa_q3_source_torso(double animation, const qa_q3_hook_context *context, bool continue_animation) {
  return qa_q3_run_torso_operation(animation, character_context(context), continue_animation);
}
