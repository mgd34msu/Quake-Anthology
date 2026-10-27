/*
 * Port of src/movement/q1/netquake.ts: NetQuake player movement from
 * WinQuake/sv_user.c, sv_phys.c and rerelease donor extensions.
 * Copyright (C) 1996-1997 Id Software, Inc. GPL-2.0-or-later.
 * C accommodations:
 *  - The provider factory mallocs its options context (caller frees
 *    provider.ctx); allocation failure aborts, as TS has no OOM.
 *  - The TS try/catch that reports input-application failure is
 *    unreachable in C: movement errors abort via qa_range_error, so
 *    end(state, failed) is only called on the success path.
 *  - Results take over the context contact/effect arrays (caller
 *    frees), as in qa_q1_finish_movement.
 */
#ifndef QA_MOVEMENT_Q1_NETQUAKE_H
#define QA_MOVEMENT_Q1_NETQUAKE_H

#include "qa/contracts/identity.h"
#include "qa/contracts/movement.h"
#include "qa/movement/q1/types.h"

qa_q1_movement_state qa_prepare_netquake(qa_q1_movement_input input, qa_movement_services services,
                                         qa_q1_movement_options options);
qa_movement_result qa_physics_netquake(qa_q1_movement_input input, qa_movement_services services,
                                        qa_q1_movement_options options);
qa_movement_result qa_move_netquake(qa_q1_movement_input input, qa_movement_services services,
                                     qa_q1_movement_options options);
qa_movement_provider qa_create_q1_movement_provider(qa_provider_id id, qa_q1_movement_options options);

#endif
