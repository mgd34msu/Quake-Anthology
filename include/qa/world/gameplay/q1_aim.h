/* Port of src/world/gameplay/q1-aim.ts. GPL-2.0-or-later. */
#ifndef QA_WORLD_GAMEPLAY_Q1_AIM_H
#define QA_WORLD_GAMEPLAY_Q1_AIM_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/world.h"

/** TS Q1AimTargets. */
typedef struct qa_q1_aim_targets {
  const qa_actor_id *targets;
  size_t target_count;
  void *ctx;
  qa_body_state_result (*body)(void *ctx, qa_actor_id actor);
  bool (*eligible)(void *ctx, qa_actor_id actor);
  bool (*trace)(void *ctx, qa_vec3 start, qa_vec3 end, qa_actor_id *out); /* false = TS null */
} qa_q1_aim_targets;

qa_vec3 qa_aim_q1(qa_vec3 origin, qa_vec3 forward, double threshold, qa_q1_aim_targets targets);

#endif
