/*
 * Port of src/movement/q3/slide-move.ts (bg_slidemove.c / PM_ClipVelocity).
 * Copyright (C) 1999-2005 Id Software, Inc. GPL-2.0-or-later.
 * C accommodation: SlideMoveContext is a mutable struct (TS class in
 * move.ts implements it); debug may be NULL. qa_q3_step_slide_move
 * takes a pointer so two slide passes share it.
 */
#ifndef QA_MOVEMENT_Q3_SLIDE_MOVE_H
#define QA_MOVEMENT_Q3_SLIDE_MOVE_H
#include <stdbool.h>
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/movement/q3/types.h"

typedef struct qa_q3_slide_move_context {
  void *ctx;
  qa_q3_motion *state;
  double frame_time;
  qa_bounds bounds;
  double mask;
  qa_q3_trace_fn trace;
  bool has_ground_normal; qa_vec3 ground_normal;
  double impact_speed;
  void (*touch)(void *ctx, qa_trace_result trace);
  void (*event)(void *ctx, double event);
  void (*debug)(void *ctx, const char *message); /* NULL = TS undefined */
} qa_q3_slide_move_context;

qa_vec3 qa_q3_clip_velocity(qa_vec3 velocity, qa_vec3 normal, double overbounce);
bool qa_q3_slide_move(qa_q3_slide_move_context *context, bool gravity);
void qa_q3_step_slide_move(qa_q3_slide_move_context *context, bool gravity);
#endif
