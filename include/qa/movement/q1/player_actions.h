/*
 * Port of src/movement/q1/player-actions.ts: movement portions of
 * Quake progs106/client.qc, callable by the game provider.
 * Copyright (C) 1996-1997 Id Software, Inc. GPL-2.0-or-later.
 */
#ifndef QA_MOVEMENT_Q1_PLAYER_ACTIONS_H
#define QA_MOVEMENT_Q1_PLAYER_ACTIONS_H

#include "qa/contracts/movement.h"
#include "qa/movement/q1/types.h"

/** TS Q1JumpResult. action: "none" | "jump" | "swim". */
typedef struct qa_q1_jump_result {
  qa_q1_movement_state state;
  const char *action;
} qa_q1_jump_result;

/** Gamecode owns jump/swim sounds and timers. Do not run in addition to QC. */
qa_q1_jump_result qa_q1_player_jump(qa_q1_movement_state state, qa_movement_services services);

/** Original QC's point traces and 225-unit impulse differ from QW's 310 impulse. */
qa_q1_movement_state qa_q1_check_water_jump(qa_q1_movement_input input, qa_movement_services services,
                                            qa_q1_movement_options options);

#endif
