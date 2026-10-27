/*
 * Port of src/movement/q1/quakeworld.ts: QuakeWorld player movement
 * from QuakeWorld/client/pmove.c and server/sv_user.c.
 * Copyright (C) 1996-1997 Id Software, Inc. GPL-2.0-or-later.
 * C accommodations: the command-slice generator becomes a malloc'd
 * array (caller frees) built with the same recursive halving; the
 * provider factory mallocs its options context (caller frees
 * provider.ctx); results take over context arrays (caller frees).
 * As in netquake, the TS input-application catch path is unreachable
 * because movement errors abort.
 */
#ifndef QA_MOVEMENT_Q1_QUAKEWORLD_H
#define QA_MOVEMENT_Q1_QUAKEWORLD_H

#include <stddef.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/protocol.h"
#include "qa/movement/q1/types.h"

/* Malloc'd slice list (caller frees); *out_count receives its length. */
qa_qw_user_command *qa_quakeworld_command_slices(qa_qw_user_command command, double maximum_milliseconds,
                                                 size_t *out_count);

qa_movement_result qa_move_quakeworld(qa_qw_movement_input input, qa_movement_services services,
                                      qa_q1_movement_options options);
qa_movement_provider qa_create_qw_movement_provider(qa_provider_id id, qa_q1_movement_options options);

#endif
