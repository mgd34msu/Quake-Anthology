/*
 * Port of src/movement/q1/pusher.ts: SV_PushMove and the separately
 * selected rotating-pusher extension.
 * Copyright (C) 1996-1997 Id Software, Inc. GPL-2.0-or-later.
 * The result's moved array is malloc'd; the caller frees it.
 */
#ifndef QA_MOVEMENT_Q1_PUSHER_H
#define QA_MOVEMENT_Q1_PUSHER_H

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/movement/q1/types.h"

/** TS Q1PusherInput. movement: "translate" | "rotate". */
typedef struct qa_q1_pusher_input {
  qa_actor_id actor;
  double elapsed_seconds;
  const char *movement;
} qa_q1_pusher_input;

/** TS Q1PushInput. */
typedef struct qa_q1_push_input {
  qa_actor_id actor;
  qa_vec3 displacement;
  qa_vec3 angular_displacement;
  double elapsed_seconds;
} qa_q1_push_input;

/** Velocity-driven source entry; displacement callers share the same transaction. */
qa_q1_pusher_result qa_move_q1_pusher(qa_q1_pusher_input input, qa_q1_pusher_services services);
/** A single source pusher transaction. The shared scheduler chooses its traversal. */
qa_q1_pusher_result qa_push_q1_pusher(qa_q1_push_input input, qa_q1_pusher_services services);
/** Think timing uses local pusher time, including blocked moves that do not advance. */
qa_q1_pusher_result qa_step_q1_pusher(qa_q1_pusher_input input, qa_q1_pusher_services services);

#endif
